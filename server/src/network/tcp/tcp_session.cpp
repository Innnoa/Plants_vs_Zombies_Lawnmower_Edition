#include "network/tcp/tcp_session.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>
#include <utility>

#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "network/tcp/tcp_session_internal.hpp"

// 用于给 player 赋 id，next_player_id_ 是静态的
std::atomic<uint32_t> TcpSession::next_player_id_{1};
// 当前活跃会话数,原子量
std::atomic<uint32_t> TcpSession::active_sessions_{0};
// 会话token
std::unordered_map<uint32_t, std::string> TcpSession::session_tokens_{};
// mutex互斥锁
std::mutex TcpSession::token_mutex_;
std::atomic<uint32_t> TcpSession::packet_debug_log_stride_{
    static_cast<uint32_t>(tcp_session_internal::kPacketDebugLogStride)};
std::atomic<uint64_t> TcpSession::packet_debug_log_counter_{0};

// 构造
TcpSession::TcpSession(tcp::socket socket) : socket_(std::move(socket)) {}

// 服务器入口函数
void TcpSession::start() {
  // fetch_add 原子加，memory_order_relaxed 宽松操作
  active_sessions_.fetch_add(1, std::memory_order_relaxed);
  read_header();
}

// 专门用于填充 Packet 包，设置 Message_type 类型 + payload 内容
void TcpSession::SendProto(lawnmower::MessageType type,
                           const google::protobuf::Message& message) {
  if (closed_) {
    return;
  }
  lawnmower::Packet packet;
  packet.set_msg_type(type);  // 设置包类型
  packet.set_payload(
      message.SerializeAsString());  // message 序列化为字符串并存至packet
  send_packet(packet);               // 发包
}

void TcpSession::SendFramedPacket(
    const std::shared_ptr<const std::string>& framed,
    lawnmower::MessageType type, std::size_t payload_len,
    std::size_t body_len) {
  if (closed_ || framed == nullptr) {
    return;
  }

  if (spdlog::should_log(spdlog::level::debug) && ShouldLogPacketDebug()) {
    spdlog::debug(
        "TCP发送包 {}，payload长度 {} bytes，序列化后长度 {} "
        "bytes（含4字节包长总计 {} "
        "bytes）",
        tcp_session_internal::MessageTypeToString(type), payload_len, body_len,
        body_len + sizeof(uint32_t));
  }

  const bool write_in_progress = !write_queue_.empty();
  if (write_queue_.size() >= tcp_session_internal::kMaxWriteQueueSize) {
    spdlog::warn("发送队列过长({})，断开玩家 {}", write_queue_.size(),
                 player_id_);
    handle_disconnect();
    return;
  }
  write_queue_.push_back(framed);
  if (!write_in_progress) {
    do_write();
  }
}

// 断开连接
void TcpSession::handle_disconnect() {
  CloseSession(SessionCloseReason::kNetworkError);
}

void TcpSession::CloseSession(SessionCloseReason reason) {
  if (closed_) {
    return;
  }
  closed_ = true;

  const char* reason_text = reason == SessionCloseReason::kClientRequest
                                ? "client_request"
                                : "network_error";
  spdlog::info("[session] close reason={} player_id={}", reason_text,
               player_id_);

  // 清除该play_id 有关的结构
  if (player_id_ != 0) {
    // 主动退出时撤销令牌；网络异常断线保留令牌用于宽限期重连
    if (reason == SessionCloseReason::kClientRequest) {
      RevokeToken(player_id_);
    }
    GameManager::Instance().MarkPlayerDisconnected(player_id_);
    RoomManager::Instance().MarkPlayerDisconnected(player_id_);
  }

  // 标准断开连接
  asio::error_code ignored_ec;
  socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
  socket_.close(ignored_ec);
  active_sessions_.fetch_sub(1, std::memory_order_relaxed);  // 原子减1
}

// 读包头
void TcpSession::read_header() {
  // 从当前对象拿到一个std::shared_ptr<TcpSession>,
  // 防止this指针悬空，延长对象生命周期
  auto self = shared_from_this();
  // asio::buffer是设置异步缓冲区
  asio::async_read(
      socket_, asio::buffer(length_buffer_),
      [this, self](const asio::error_code& ec, std::size_t) {
        if (spdlog::should_log(spdlog::level::debug) &&
            ShouldLogPacketDebug()) {
          spdlog::debug("开始读取包长度");
        }
        if (ec) {
          spdlog::warn("读取包长度失败: {}", ec.message());
          handle_disconnect();
          return;
        }

        uint32_t net_len = 0;
        std::memcpy(&net_len, length_buffer_.data(), sizeof(net_len));
        // ntohl 是把网络字节序(大端) 转成主机字节序
        const uint32_t body_len = ntohl(net_len);
        if (spdlog::should_log(spdlog::level::debug) &&
            ShouldLogPacketDebug()) {
          spdlog::debug("收到包长度: {}", body_len);
        }
        if (body_len == 0 || body_len > tcp_session_internal::kMaxPacketSize) {
          spdlog::warn("包长度异常: {}", body_len);
          handle_disconnect();
          return;
        }

        // 按历史最大包长做reserve，避免包长逐步增大时频繁扩容抖动。
        max_read_body_len_ =
            std::max(max_read_body_len_, static_cast<std::size_t>(body_len));
        if (read_buffer_.capacity() < max_read_body_len_) {
          const std::size_t grow_capacity = read_buffer_.capacity() == 0
                                                ? max_read_body_len_
                                                : read_buffer_.capacity() * 2;
          read_buffer_.reserve(std::max(grow_capacity, max_read_body_len_));
        }
        read_buffer_.resize(body_len);
        spdlog::debug("包长度解析完成，开始读取包体");

        read_body(body_len);
      });
}

// 读包体
void TcpSession::read_body(std::size_t length) {
  auto self = shared_from_this();  // 保活：确保异步回调执行时会话对象仍存在
  asio::async_read(
      socket_, asio::buffer(read_buffer_, length),
      [this, self, length](const asio::error_code& ec, std::size_t) {
        // 一系列差错检测
        spdlog::debug("开始解析包体，长度 {} bytes", length);
        if (ec) {
          spdlog::warn("解析包体失败: {}", ec.message());
          handle_disconnect();
          return;
        }

        lawnmower::Packet packet;
        if (!packet.ParseFromArray(read_buffer_.data(),
                                   static_cast<int>(read_buffer_.size()))) {
          spdlog::warn("解析protobuf数据包失败，大小为 {} bytes",
                       read_buffer_.size());
          read_header();
          return;
        }
        if (spdlog::should_log(spdlog::level::debug)) {
          spdlog::debug(
              "包体解析完成: {}，payload长度 {} bytes，包体总长度 {} bytes",
              tcp_session_internal::MessageTypeToString(packet.msg_type()),
              packet.payload().size(), length);
        }
        // 识别包类型
        handle_packet(packet);
        if (closed_ || !socket_.is_open()) {
          return;
        }
        read_header();
      });
}

// 写操作
void TcpSession::do_write() {
  auto self = shared_from_this();  // 保活：确保异步回调执行时会话对象仍存在
  asio::async_write(socket_, asio::buffer(*write_queue_.front()),
                    [this, self](const asio::error_code& ec, std::size_t) {
                      if (ec) {
                        spdlog::warn("包写入失败: {}", ec.message());
                        handle_disconnect();
                        return;
                      }
                      write_queue_.pop_front();  // 弹出双端队列头
                      // 非空，仍有内容，继续写
                      if (!write_queue_.empty()) {
                        do_write();
                      }
                    });
}

// 识别包类型
void TcpSession::handle_packet(const lawnmower::Packet& packet) {
  using lawnmower::MessageType;
  if (spdlog::should_log(spdlog::level::debug) && ShouldLogPacketDebug()) {
    spdlog::debug("开始处理消息 {}",
                  tcp_session_internal::MessageTypeToString(packet.msg_type()));
  }
  // 已拆分为独立处理函数
  switch (packet.msg_type()) {
    case MessageType::MSG_C2S_LOGIN:
      HandleLogin(packet.payload());
      break;
    case MessageType::MSG_C2S_HEARTBEAT:
      HandleHeartbeat(packet.payload());
      break;
    case MessageType::MSG_C2S_RECONNECT_REQUEST:
      HandleReconnectRequest(packet.payload());
      break;
    case MessageType::MSG_C2S_CREATE_ROOM:
      HandleCreateRoom(packet.payload());
      break;
    case MessageType::MSG_C2S_GET_ROOM_LIST:
      HandleGetRoomList(packet.payload());
      break;
    case MessageType::MSG_C2S_JOIN_ROOM:
      HandleJoinRoom(packet.payload());
      break;
    case MessageType::MSG_C2S_LEAVE_ROOM:
      HandleLeaveRoom(packet.payload());
      break;
    case MessageType::MSG_C2S_SET_READY:
      HandleSetReady(packet.payload());
      break;
    case MessageType::MSG_C2S_REQUEST_QUIT:
      HandleRequestQuit();
      break;
    case MessageType::MSG_C2S_START_GAME:
      HandleStartGame(packet.payload());
      break;
    case MessageType::MSG_C2S_PLAYER_INPUT:
      HandlePlayerInput(packet.payload());
      break;
    case MessageType::MSG_C2S_UPGRADE_REQUEST_ACK:
      HandleUpgradeRequestAck(packet.payload());
      break;
    case MessageType::MSG_C2S_UPGRADE_OPTIONS_ACK:
      HandleUpgradeOptionsAck(packet.payload());
      break;
    case MessageType::MSG_C2S_UPGRADE_SELECT:
      HandleUpgradeSelect(packet.payload());
      break;
    case MessageType::MSG_C2S_UPGRADE_REFRESH_REQUEST:
      HandleUpgradeRefreshRequest(packet.payload());
      break;
    case MessageType::MSG_UNKNOWN:
    default:
      spdlog::warn(
          "未知操作类型: {}",
          tcp_session_internal::MessageTypeToString(packet.msg_type()));
  }
  spdlog::debug("完成处理消息 {}",
                tcp_session_internal::MessageTypeToString(packet.msg_type()));
}

// 发包
void TcpSession::send_packet(const lawnmower::Packet& packet) {
  const std::string data = packet.SerializeAsString();
  const uint32_t net_len = htonl(static_cast<uint32_t>(data.size()));

  if (spdlog::should_log(spdlog::level::debug)) {
    const auto payload_len = packet.payload().size();
    const auto body_len = data.size();
    // 打印分层长度：payload（业务消息）/ Packet 序列化后（含 msg_type 等）/ 加
    // 4 字节帧头后的总大小。
    spdlog::debug(
        "TCP发送包 {}，payload长度 {} bytes，序列化后长度 {} "
        "bytes（含4字节包长总计 {} "
        "bytes）",
        tcp_session_internal::MessageTypeToString(packet.msg_type()),
        payload_len, body_len, body_len + sizeof(net_len));
  }

  // 写内容
  auto framed = std::make_shared<std::string>();
  framed->resize(sizeof(net_len) + data.size());
  std::memcpy(framed->data(), &net_len, sizeof(net_len));
  std::memcpy(framed->data() + sizeof(net_len), data.data(), data.size());

  // 记录当前写队列状态，若为空，则之后做写操作
  const bool write_in_progress = !write_queue_.empty();
  if (write_queue_.size() >= tcp_session_internal::kMaxWriteQueueSize) {
    spdlog::warn("发送队列过长({})，断开玩家 {}", write_queue_.size(),
                 player_id_);
    handle_disconnect();
    return;
  }
  // 尾压入写队列
  write_queue_.push_back(std::move(framed));
  if (!write_in_progress) {
    do_write();
  }
}
