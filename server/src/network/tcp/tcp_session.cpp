#include "network/tcp/tcp_session.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <random>
#include <span>
#include <spdlog/spdlog.h>
#include <sstream>

#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "network/udp/udp_server.hpp"

namespace {
constexpr std::size_t kMaxPacketSize = 64 * 1024;  // 设定最大包大小
constexpr std::size_t kMaxWriteQueueSize = 1024;   // 防止慢连接无限堆积
constexpr std::size_t kTokenBytes = 16;            // 128bit 令牌

// 类型转字符串
std::string MessageTypeToString(lawnmower::MessageType type) {
  const std::string name = lawnmower::MessageType_Name(type);
  if (!name.empty()) {
    return name + "(" + std::to_string(static_cast<int>(type)) + ")";
  }
  return "UNKNOWN(" + std::to_string(static_cast<int>(type)) + ")";
}

// 向房间广播
void BroadcastToRoom(std::span<const std::weak_ptr<TcpSession>> sessions,
                     lawnmower::MessageType type,
                     const google::protobuf::Message& message) {
  for (const auto& weak_session : sessions) {
    if (auto session = weak_session.lock()) {
      session->SendProto(type, message);
    }
  }
}
}  // namespace

// 用于给 player 赋 id，next_player_id_ 是静态的
std::atomic<uint32_t> TcpSession::next_player_id_{1};
// 当前活跃会话数,原子量
std::atomic<uint32_t> TcpSession::active_sessions_{0};
// 会话token
std::unordered_map<uint32_t, std::string> TcpSession::session_tokens_{};
// mutex互斥锁
std::mutex TcpSession::token_mutex_;

// 构造
TcpSession::TcpSession(tcp::socket socket) : socket_(std::move(socket)) {}

// 服务器入口函数
void TcpSession::start() {
  // fetch_add 原子加，memory_order_relaxed 宽松操作
  active_sessions_.fetch_add(1, std::memory_order_relaxed);
  read_header();
}

// 生成Token
std::string TcpSession::GenerateToken() {
  std::array<uint8_t, kTokenBytes> buf{};
  // 生成静态随机数使用梅森旋转算法，大小为unsigned longlong,
  // rng重载了（）运算符，random_device{}()为种子
  static thread_local std::mt19937_64 rng(std::random_device{}());
  // 每轮写入 8 字节（uint64_t），两轮即可填满 16 字节（128bit token）。
  for (std::size_t i = 0; i < kTokenBytes; i += sizeof(uint64_t)) {
    const uint64_t v = rng();  // 获取随机数

    // 把随机数v的原始字节拷贝至buf的第i段
    // std::min的意义是让代码对任意token长度都安全，如果将来kTokenBytes不是8的倍数，
    // 则最后一轮只拷贝剩余的字节数，避免越界
    std::memcpy(buf.data() + i, &v,
                std::min(sizeof(uint64_t), kTokenBytes - i));
  }
  std::ostringstream oss;  // 用于将各种数据类型转化为string格式
  oss << std::hex;  // ostringstream重载了 << 用于将数据存至对象中/设置进制格式
  for (auto b : buf) {
    // 输出为两位 hex（如 0x0A -> "0a"）；width
    // 仅对下一次插入生效，因此放在循环内。
    oss.width(2);
    oss.fill('0');
    oss << static_cast<int>(b);  // 避免按字符输出
  }
  return oss.str();  // 返回oss内容
}

// 注册Token
void TcpSession::RegisterToken(uint32_t player_id, std::string token) {
  std::lock_guard<std::mutex> lock(token_mutex_);  // 互斥锁-上锁
  session_tokens_[player_id] = std::move(token);   // 添加该节点
}

// 验证Token
bool TcpSession::VerifyToken(uint32_t player_id, std::string_view token) {
  std::lock_guard<std::mutex> lock(token_mutex_);   // 互斥锁-上锁
  const auto it = session_tokens_.find(player_id);  // 寻找该节点
  return it != session_tokens_.end() && it->second == token;
}

// 撤销Token
void TcpSession::RevokeToken(uint32_t player_id) {
  std::lock_guard<std::mutex> lock(token_mutex_);  // 互斥锁-上锁
  session_tokens_.erase(player_id);                // 擦除该节点
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

  if (spdlog::should_log(spdlog::level::debug)) {
    spdlog::debug(
        "TCP发送包 {}，payload长度 {} bytes，序列化后长度 {} "
        "bytes（含4字节包长总计 {} "
        "bytes）",
        MessageTypeToString(type), payload_len, body_len,
        body_len + sizeof(uint32_t));
  }

  const bool write_in_progress = !write_queue_.empty();
  if (write_queue_.size() >= kMaxWriteQueueSize) {
    spdlog::warn("发送队列过长({})，断开玩家 {}", write_queue_.size(),
                 player_id_);
    handle_disconnect();
    return;
  }
  write_queue_.push_back(*framed);
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

  if (reason == SessionCloseReason::kClientRequest) {
    spdlog::info("客户端请求断开连接");
  }

  // 清除该play_id 有关的结构
  if (player_id_ != 0) {
    RevokeToken(player_id_);
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
  asio::async_read(socket_, asio::buffer(length_buffer_),
                   [this, self](const asio::error_code& ec, std::size_t) {
                     spdlog::debug("开始读取包长度");
                     if (ec) {
                       spdlog::warn("读取包长度失败: {}", ec.message());
                       handle_disconnect();
                       return;
                     }

                     uint32_t net_len = 0;
                     std::memcpy(&net_len, length_buffer_.data(),
                                 sizeof(net_len));
                     // ntohl 是把网络字节序(大端) 转成主机字节序
                     const uint32_t body_len = ntohl(net_len);
                     spdlog::debug("收到包长度: {}", body_len);
                     if (body_len == 0 || body_len > kMaxPacketSize) {
                       spdlog::warn("包长度异常: {}", body_len);
                       handle_disconnect();
                       return;
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
              MessageTypeToString(packet.msg_type()), packet.payload().size(),
              length);
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
  asio::async_write(socket_, asio::buffer(write_queue_.front()),
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

// 处理登录请求
void TcpSession::HandleLogin(const std::string& payload) {
  lawnmower::C2S_Login login;
  if (!login.ParseFromString(payload)) {
    spdlog::warn("解析登录包体失败");
    return;
  }

  if (player_id_ != 0) {
    lawnmower::S2C_LoginResult result;
    result.set_success(false);
    result.set_player_id(player_id_);
    result.set_message_login("重复登录");
    SendProto(lawnmower::MessageType::MSG_S2C_LOGIN_RESULT, result);
    return;
  }

  player_id_ = next_player_id_.fetch_add(1);  // 原子加1
  // 设置玩家名，若未输入玩家名则为玩家+id,否则则为玩家名
  player_name_ = login.player_name().empty()
                     ? ("玩家" + std::to_string(player_id_))
                     : login.player_name();
  // 获取Token
  session_token_ = GenerateToken();
  // 注册Token
  RegisterToken(player_id_, session_token_);

  lawnmower::S2C_LoginResult result;
  result.set_success(true);
  result.set_player_id(player_id_);
  result.set_message_login("login success");
  result.set_session_token(session_token_);

  SendProto(lawnmower::MessageType::MSG_S2C_LOGIN_RESULT, result);
  spdlog::info("玩家登录: {} (id={})", player_name_, player_id_);
}

// 处理心跳请求
void TcpSession::HandleHeartbeat(const std::string& payload) {
  lawnmower::C2S_Heartbeat heartbeat;
  if (!heartbeat.ParseFromString(payload)) {
    spdlog::warn("解析心跳包失败");
    return;
  }

  lawnmower::S2C_Heartbeat reply;
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch());
  reply.set_timestamp(static_cast<uint64_t>(now_ms.count()));
  reply.set_online_players(active_sessions_.load(std::memory_order_relaxed));

  SendProto(lawnmower::MessageType::MSG_S2C_HEARTBEAT, reply);
}

// 处理重连请求
void TcpSession::HandleReconnectRequest(const std::string& payload) {
  lawnmower::C2S_ReconnectRequest request;
  if (!request.ParseFromString(payload)) {
    spdlog::warn("解析重连请求包失败");
    return;
  }

  lawnmower::S2C_ReconnectAck ack;
  ack.set_player_id(request.player_id());
  ack.set_room_id(request.room_id());

  if (player_id_ != 0) {
    ack.set_success(false);
    ack.set_message("当前会话已登录");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    return;
  }
  if (request.player_id() == 0) {
    ack.set_success(false);
    ack.set_message("缺少玩家ID");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    return;
  }

  const auto room_opt =
      RoomManager::Instance().GetPlayerRoom(request.player_id());
  if (!room_opt.has_value()) {
    ack.set_success(false);
    ack.set_message("玩家不在房间");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    return;
  }
  const uint32_t target_room_id = *room_opt;
  if (request.room_id() != 0 && request.room_id() != target_room_id) {
    ack.set_success(false);
    ack.set_message("房间不匹配");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    return;
  }

  bool is_playing = false;
  std::string player_name;
  if (!RoomManager::Instance().AttachSession(request.player_id(),
                                             target_room_id, weak_from_this(),
                                             &is_playing, &player_name)) {
    ack.set_success(false);
    ack.set_message("重连失败");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    return;
  }

  if (is_playing) {
    GameManager::ReconnectSnapshot snapshot;
    if (!GameManager::Instance().TryReconnectPlayer(
            request.player_id(), target_room_id, request.last_input_seq(),
            request.last_server_tick(), &snapshot)) {
      RoomManager::Instance().MarkPlayerDisconnected(request.player_id());
      ack.set_success(false);
      ack.set_message("场景不存在");
      SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
      return;
    }
    ack.set_server_tick(static_cast<uint32_t>(snapshot.server_tick));
    ack.set_is_paused(snapshot.is_paused);
    if (player_name.empty()) {
      player_name = snapshot.player_name;
    }
  }

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch());
  ack.set_server_time_ms(static_cast<uint64_t>(now_ms.count()));
  ack.set_room_id(target_room_id);
  ack.set_is_playing(is_playing);

  std::string token = request.session_token();
  if (token.empty()) {
    token = GenerateToken();
  }
  RegisterToken(request.player_id(), token);
  session_token_ = token;
  ack.set_session_token(token);

  player_id_ = request.player_id();
  if (!player_name.empty()) {
    player_name_ = player_name;
  } else {
    player_name_ = "玩家" + std::to_string(player_id_);
  }

  ack.set_success(true);
  ack.set_message("reconnect success");
  SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);

  if (is_playing) {
    lawnmower::S2C_GameStateSync sync;
    if (GameManager::Instance().BuildFullState(target_room_id, &sync)) {
      SendProto(lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC, sync);
    }
  }
}

// 处理创建房间请求
void TcpSession::HandleCreateRoom(const std::string& payload) {
  lawnmower::C2S_CreateRoom request;
  if (!request.ParseFromString(payload)) {
    spdlog::warn("解析创建房间包体失败");
    return;
  }

  lawnmower::S2C_CreateRoomResult result;
  if (player_id_ == 0) {
    result.set_success(false);
    result.set_message_create("请先登录");
  } else {
    result = RoomManager::Instance().CreateRoom(player_id_, player_name_,
                                                weak_from_this(), request);
  }
  SendProto(lawnmower::MessageType::MSG_S2C_CREATE_ROOM_RESULT, result);
}

// 处理获取房间列表请求
void TcpSession::HandleGetRoomList(const std::string& payload) {
  lawnmower::C2S_GetRoomList request;
  if (!request.ParseFromString(payload)) {
    spdlog::warn("解析房间列表请求失败");
    return;
  }

  lawnmower::S2C_RoomList list;
  if (player_id_ != 0) {
    list = RoomManager::Instance().GetRoomList();
  }
  spdlog::debug("发送房间列表给玩家 {}", player_id_);
  SendProto(lawnmower::MessageType::MSG_S2C_ROOM_LIST, list);
}

// 处理加入房间请求
void TcpSession::HandleJoinRoom(const std::string& payload) {
  lawnmower::C2S_JoinRoom request;
  if (!request.ParseFromString(payload)) {
    spdlog::warn("解析加入房间包体失败");
    return;
  }

  lawnmower::S2C_JoinRoomResult result;
  if (player_id_ == 0) {
    result.set_success(false);
    result.set_message_join("请先登录");
  } else {
    result = RoomManager::Instance().JoinRoom(player_id_, player_name_,
                                              weak_from_this(), request);
  }
  SendProto(lawnmower::MessageType::MSG_S2C_JOIN_ROOM_RESULT, result);
}

// 处理离开房间请求
void TcpSession::HandleLeaveRoom(const std::string& payload) {
  lawnmower::C2S_LeaveRoom request;
  if (!request.ParseFromString(payload)) {
    spdlog::warn("解析离开房间包体失败");
    return;
  }

  lawnmower::S2C_LeaveRoomResult result;
  if (player_id_ == 0) {
    result.set_success(false);
    result.set_message_leave("请先登录");
  } else {
    result = RoomManager::Instance().LeaveRoom(player_id_);
  }
  SendProto(lawnmower::MessageType::MSG_S2C_LEAVE_ROOM_RESULT, result);
}

// 处理设置准备状态请求
void TcpSession::HandleSetReady(const std::string& payload) {
  lawnmower::C2S_SetReady request;
  if (!request.ParseFromString(payload)) {
    spdlog::warn("解析设置准备状态包体失败");
    return;
  }

  lawnmower::S2C_SetReadyResult result;
  if (player_id_ == 0) {
    result.set_success(false);
    result.set_message_ready("请先登录");
  } else {
    result = RoomManager::Instance().SetReady(player_id_, request);
  }
  SendProto(lawnmower::MessageType::MSG_S2C_SET_READY_RESULT, result);
}

// 处理断开连接请求
void TcpSession::HandleRequestQuit() {
  CloseSession(SessionCloseReason::kClientRequest);
}

// 处理开始游戏请求
void TcpSession::HandleStartGame(const std::string& payload) {
  lawnmower::C2S_StartGame request;
  if (!request.ParseFromString(payload)) {
    spdlog::warn("解析开始游戏请求失败");
    return;
  }

  lawnmower::S2C_GameStart result;
  auto snapshot = RoomManager::Instance().TryStartGame(player_id_, &result);
  if (!result.success()) {
    SendProto(lawnmower::MessageType::MSG_S2C_GAME_START, result);
    return;
  }

  const lawnmower::SceneInfo scene_info =
      GameManager::Instance().CreateScene(*snapshot);

  // mutable_scene() 返回 SceneInfo*（指向 result
  // 内部的子消息），解引用后整体赋值。
  // 等价写法：result.mutable_scene()->CopyFrom(scene_info);
  *result.mutable_scene() = scene_info;

  const auto sessions =
      RoomManager::Instance().GetRoomSessions(snapshot->room_id);
  // 广播
  BroadcastToRoom(sessions, lawnmower::MessageType::MSG_S2C_GAME_START, result);

  lawnmower::S2C_GameStateSync sync;
  if (GameManager::Instance().BuildFullState(snapshot->room_id, &sync)) {
    bool sent_udp = false;
    if (auto udp = GameManager::Instance().GetUdpServer()) {
      sent_udp = udp->BroadcastState(snapshot->room_id, sync) > 0;
    }
    if (!sent_udp) {
      // 尚未收到客户端 UDP，首帧用 TCP 兜底
      BroadcastToRoom(sessions, lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC,
                      sync);
    }
    GameManager::Instance().StartGameLoop(snapshot->room_id);
    // 启动后再延迟一个同步周期重发一次全量同步，避免客户端切屏/UDP未打通时错过首帧。
    if (auto io = GameManager::Instance().GetIoContext()) {
      auto timer = std::make_shared<asio::steady_timer>(*io);
      timer->expires_after(std::chrono::milliseconds(
          static_cast<int>(1000.0 / scene_info.state_sync_rate())));
      timer->async_wait(
          [room_id = snapshot->room_id, timer](const asio::error_code& ec) {
            if (ec == asio::error::operation_aborted) {
              return;
            }
            lawnmower::S2C_GameStateSync retry_sync;
            if (GameManager::Instance().BuildFullState(room_id, &retry_sync)) {
              bool sent_retry_udp = false;
              if (auto udp = GameManager::Instance().GetUdpServer()) {
                sent_retry_udp = udp->BroadcastState(room_id, retry_sync) > 0;
              }
              if (!sent_retry_udp) {
                const auto retry_sessions =
                    RoomManager::Instance().GetRoomSessions(room_id);
                BroadcastToRoom(retry_sessions,
                                lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC,
                                retry_sync);
              }
            }
          });
    }
  }
  spdlog::info("房间 {} 游戏开始", snapshot->room_id);
}

// 处理玩家输入请求
void TcpSession::HandlePlayerInput(const std::string& payload) {
  lawnmower::C2S_PlayerInput input;
  if (!input.ParseFromString(payload)) {
    spdlog::warn("解析玩家输入失败");
    return;
  }

  if (player_id_ == 0) {
    spdlog::warn("未登录玩家发送移动输入");
    return;
  }

  if (!input.session_token().empty() &&
      !VerifyToken(player_id_, input.session_token())) {
    spdlog::warn("玩家 {} 输入令牌校验失败", player_id_);
    return;
  }

  input.set_player_id(player_id_);

  uint32_t room_id = 0;
  if (!GameManager::Instance().HandlePlayerInput(player_id_, input, &room_id)) {
    spdlog::debug("玩家 {} 输入被拒绝或未找到场景", player_id_);
  }
}

// 处理升级请求确认
void TcpSession::HandleUpgradeRequestAck(const std::string& payload) {
  lawnmower::C2S_UpgradeRequestAck ack;
  if (!ack.ParseFromString(payload)) {
    spdlog::warn("解析升级请求确认失败");
    return;
  }
  if (player_id_ == 0) {
    spdlog::warn("未登录玩家发送升级请求确认");
    return;
  }
  ack.set_player_id(player_id_);
  if (!GameManager::Instance().HandleUpgradeRequestAck(player_id_, ack)) {
    spdlog::debug("玩家 {} 升级请求确认被拒绝", player_id_);
  }
}

// 处理升级选项确认
void TcpSession::HandleUpgradeOptionsAck(const std::string& payload) {
  lawnmower::C2S_UpgradeOptionsAck ack;
  if (!ack.ParseFromString(payload)) {
    spdlog::warn("解析升级选项确认失败");
    return;
  }
  if (player_id_ == 0) {
    spdlog::warn("未登录玩家发送升级选项确认");
    return;
  }
  ack.set_player_id(player_id_);
  if (!GameManager::Instance().HandleUpgradeOptionsAck(player_id_, ack)) {
    spdlog::debug("玩家 {} 升级选项确认被拒绝", player_id_);
  }
}

// 处理升级选择
void TcpSession::HandleUpgradeSelect(const std::string& payload) {
  lawnmower::C2S_UpgradeSelect select;
  if (!select.ParseFromString(payload)) {
    spdlog::warn("解析升级选择失败");
    return;
  }
  if (player_id_ == 0) {
    spdlog::warn("未登录玩家发送升级选择");
    return;
  }
  select.set_player_id(player_id_);
  if (!GameManager::Instance().HandleUpgradeSelect(player_id_, select)) {
    spdlog::debug("玩家 {} 升级选择被拒绝", player_id_);
  }
}

// 处理刷新升级请求
void TcpSession::HandleUpgradeRefreshRequest(const std::string& payload) {
  lawnmower::C2S_UpgradeRefreshRequest refresh;
  if (!refresh.ParseFromString(payload)) {
    spdlog::warn("解析刷新升级请求失败");
    return;
  }
  if (player_id_ == 0) {
    spdlog::warn("未登录玩家发送刷新升级请求");
    return;
  }
  refresh.set_player_id(player_id_);
  if (!GameManager::Instance().HandleUpgradeRefreshRequest(player_id_,
                                                           refresh)) {
    spdlog::debug("玩家 {} 刷新升级请求被拒绝", player_id_);
  }
}

// 识别包类型
void TcpSession::handle_packet(const lawnmower::Packet& packet) {
  using lawnmower::MessageType;
  spdlog::debug("开始处理消息 {}", MessageTypeToString(packet.msg_type()));
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
      spdlog::warn("未知操作类型: {}", MessageTypeToString(packet.msg_type()));
  }
  spdlog::debug("完成处理消息 {}", MessageTypeToString(packet.msg_type()));
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
        MessageTypeToString(packet.msg_type()), payload_len, body_len,
        body_len + sizeof(net_len));
  }

  // 写内容
  std::string framed;
  framed.resize(sizeof(net_len) + data.size());
  std::memcpy(framed.data(), &net_len, sizeof(net_len));
  std::memcpy(framed.data() + sizeof(net_len), data.data(), data.size());

  // 记录当前写队列状态，若为空，则之后做写操作
  const bool write_in_progress = !write_queue_.empty();
  if (write_queue_.size() >= kMaxWriteQueueSize) {
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
