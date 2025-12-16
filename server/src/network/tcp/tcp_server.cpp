#include "network/tcp/tcp_server.hpp"
#include "game/managers/room_manager.hpp"
#include <spdlog/spdlog.h>
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <iostream>

namespace {
constexpr std::size_t kMaxPacketSize = 64 * 1024; // 设定最大包大小
}

std::atomic<uint32_t> TcpSession::next_player_id_{1}; // 用于给palyer赋id,next_player_id_是静态的

TcpSession::TcpSession(tcp::socket socket) : socket_(std::move(socket)) { //构造函数
}

void TcpSession::start() { // public,入口函数
  read_header();
}

// 专门用于发送Packet包，设置Message_type类型+payload内容（可以使用其他结构赋值）
void TcpSession::SendProto(lawnmower::MessageType type, const google::protobuf::Message& message) {
  if (closed_) { // 是否已经断开连接
    return;
  }
  lawnmower::Packet packet;
  packet.set_msg_type(type); // 设置Message_type类型
  packet.set_payload(message.SerializeAsString()); // 设置具体消息内容
  send_packet(packet); // 发包
}

// 用于服务器主动断开连接
void TcpSession::handle_disconnect() {
  if (closed_) {
    return;
  }
  closed_ = true; // 已经主动断开

  if (player_id_ != 0) { // 若存在player,则移除加入房间中的用户
    RoomManager::Instance().RemovePlayer(player_id_);
  }

  asio::error_code ignored_ec;
  socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
  socket_.close(ignored_ec); // 关闭socket
}

void TcpSession::read_header() {
  auto self = shared_from_this(); // 获取自身的shared_ptr
  asio::async_read(socket_, asio::buffer(length_buffer_), // 先读packet长度,异步非阻塞
    [this, self](const asio::error_code& ec, std::size_t) {
    std::cout<<"开始解析包长度\n";
      if (ec) {
        spdlog::warn("读取包长度失败: {}", ec.message());
        handle_disconnect();
        return;
      }

      uint32_t net_len = 0;
      std::memcpy(&net_len, length_buffer_.data(), sizeof(net_len));
      const uint32_t body_len = ntohl(net_len);
      std::cout<<"包长度: "<<body_len<<"\n";
      if (body_len == 0 || body_len > kMaxPacketSize) {
        spdlog::warn("包的长度为空: {}", body_len);
        handle_disconnect();
        return;
      }

      read_buffer_.resize(body_len);
      std::cout<<"解析包长度完成\n";
      
      read_body(body_len);
    }
  );
}

void TcpSession::read_body(std::size_t length) {
  auto self = shared_from_this(); // 获取自身的shared_ptr
  asio::async_read(socket_, asio::buffer(read_buffer_, length), // 然后读具体报文
    [this, self](const asio::error_code& ec, std::size_t) {
      std::cout<<"开始解析包体\n";
      if (ec) {
        spdlog::warn("解析包体失败: {}", ec.message());
        handle_disconnect();
        return;
      }

      lawnmower::Packet packet;
      if (!packet.ParseFromArray(read_buffer_.data(), static_cast<int>(read_buffer_.size()))) {
        spdlog::warn("解析protobuf数据包失败，大小为 {} bytes)", read_buffer_.size());
        read_header();
        return;
      }
      std::cout<<"解析包体完成\n";
      handle_packet(packet);
      if (closed_ || !socket_.is_open()) { // 已经主动断开，不再继续读
        return;
      }
      read_header();
    }
  );
}

void TcpSession::do_write() {
  auto self = shared_from_this(); // 获取自身的shared_ptr
  asio::async_write(socket_, asio::buffer(write_queue_.front()), // 注册异步写入回调
    [this, self](const asio::error_code& ec, std::size_t) {
      if (ec) {
        spdlog::warn("包写入失败: {}", ec.message());
        handle_disconnect();
        return;
      }
      write_queue_.pop_front();
      if (!write_queue_.empty()) {
        do_write();
      }
    }
  );
}

void TcpSession::handle_packet(const lawnmower::Packet& packet){
  using lawnmower::MessageType;
  std::cout<<"开始识别包类型，类型为："<<packet.msg_type()<<"\n";
  switch (packet.msg_type()){
    case MessageType::MSG_C2S_LOGIN: { // 登录
      lawnmower::C2S_Login login;
      if (!login.ParseFromString(packet.payload())) {
        spdlog::warn("解析登录包体失败");
        break;
      }

      if (player_id_ != 0) { // 已登录
        lawnmower::S2C_LoginResult result;
        result.set_success(false);
        result.set_player_id(player_id_);
        result.set_message_login("重复登录");
        SendProto(MessageType::MSG_S2C_LOGIN_RESULT, result);
        break;
      }

      player_id_ = next_player_id_.fetch_add(1); // 原子变量++，赋给唯一id
      player_name_ = login.player_name().empty() ? ("玩家" + std::to_string(player_id_)) : login.player_name();

      lawnmower::S2C_LoginResult result; // 登录反馈
      result.set_success(true);
      result.set_player_id(player_id_);
      result.set_message_login("login success");

      SendProto(MessageType::MSG_S2C_LOGIN_RESULT, result); // 发送登录反馈
      spdlog::info("玩家登录: {} (id={})", player_name_, player_id_);
      break;
    }
    case MessageType::MSG_C2S_HEARTBEAT: { // 心跳回显（含服务器心跳反馈)
      lawnmower::C2S_Heartbeat heartbeat;
      if (!heartbeat.ParseFromString(packet.payload())) {
        spdlog::warn("解析心跳包失败");
        break;
      }

      lawnmower::S2C_Heartbeat reply; // 心跳反馈
      // 设置时间戳
      const auto now_ms = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
      reply.set_timestamp(now_ms);
      reply.set_online_players(1); // 当前房间内游戏中玩家个数

      SendProto(MessageType::MSG_S2C_HEARTBEAT, reply); // 发送心跳反馈
      break;
    }
    case MessageType::MSG_C2S_CREATE_ROOM: { // 创建房间请求
      lawnmower::C2S_CreateRoom request;
      if (!request.ParseFromString(packet.payload())) {
        spdlog::warn("解析创建房间包体失败");
        break;
      }

      lawnmower::S2C_CreateRoomResult result; // 创建房间反馈
      if (player_id_ == 0) {
        result.set_success(false);
        result.set_message_create("请先登录");
      } else {
        result = RoomManager::Instance().CreateRoom // 单例创建房间
          (player_id_, player_name_, weak_from_this(), request);
      }
      SendProto(MessageType::MSG_S2C_CREATE_ROOM_RESULT, result); // 发送创建房间反馈
      break;
    }
    case MessageType::MSG_C2S_GET_ROOM_LIST: { // 获取房间列表
      lawnmower::C2S_GetRoomList request; // 获取房间列表反馈
      if (!request.ParseFromString(packet.payload())) {
        spdlog::warn("解析房间列表请求失败");
        break;
      }

      lawnmower::S2C_RoomList list;
      if (player_id_ != 0) {
        list = RoomManager::Instance().GetRoomList(); // 单例获取房间列表
      }
      std::cout<<"发送房间列表\n";
      SendProto(MessageType::MSG_S2C_ROOM_LIST, list); // 发送获取房间列表反馈
      break;
    }
    case MessageType::MSG_C2S_JOIN_ROOM: { // 加入房间请求
      lawnmower::C2S_JoinRoom request;
      if (!request.ParseFromString(packet.payload())) {
        spdlog::warn("解析加入房间包体失败");
        break;
      }

      lawnmower::S2C_JoinRoomResult result; // 加入房间反馈
      if (player_id_ == 0) {
        result.set_success(false);
        result.set_message_join("请先登录");
      } else {
        result = RoomManager::Instance().JoinRoom // 单例加入房间
          (player_id_, player_name_, weak_from_this(), request);
      }
      SendProto(MessageType::MSG_S2C_JOIN_ROOM_RESULT, result); // 发送加入房间反馈
      break;
    }
    case MessageType::MSG_C2S_LEAVE_ROOM: { // 离开房间请求
      lawnmower::C2S_LeaveRoom request;
      if (!request.ParseFromString(packet.payload())) {
        spdlog::warn("解析离开房间包体失败");
        break;
      }

      lawnmower::S2C_LeaveRoomResult result; // 离开房间反馈
      if (player_id_ == 0) {
        result.set_success(false);
        result.set_message_leave("请先登录");
      } else {
        result = RoomManager::Instance().LeaveRoom(player_id_); // 单例离开房间
      }
      SendProto(MessageType::MSG_S2C_LEAVE_ROOM_RESULT, result); // 发送离开房间反馈
      break;
    }
    case MessageType::MSG_C2S_SET_READY: { // 设置准备状态
      lawnmower::C2S_SetReady request; // 设置准备状态反馈
      if (!request.ParseFromString(packet.payload())) {
        spdlog::warn("解析设置准备状态包体失败");
        break;
      }

      lawnmower::S2C_SetReadyResult result;
      if (player_id_ == 0) {
        result.set_success(false);
        result.set_message_ready("请先登录");
      } else {
        result = RoomManager::Instance().SetReady(player_id_, request); // 单列设置准备状态
      }
      SendProto(MessageType::MSG_S2C_SET_READY_RESULT, result); // 发送设置准备状态反馈
      break;
    }
    case MessageType::MSG_C2S_REQUEST_QUIT: { // 客户端主动断开连接
      spdlog::info("客户端请求断开连接");
      handle_disconnect();
      break;
    }
    case MessageType::MSG_UNKNOWN: // 未知类型
    default:
      spdlog::warn("未知操作类型: {}", static_cast<int>(packet.msg_type()));
  }
  std::cout<<"解析包类型完成\n";
  std::cout<<"=========================\n";
}

void TcpSession::send_packet(const lawnmower::Packet& packet){ // 发包
  const std::string data = packet.SerializeAsString();
  const uint32_t net_len = htonl(static_cast<uint32_t>(data.size()));

  std::string framed;
  framed.resize(sizeof(net_len) + data.size());
  std::memcpy(framed.data(), &net_len, sizeof(net_len)); // 包长度
  std::memcpy(framed.data() + sizeof(net_len), data.data(), data.size()); // 包体内容

  const bool write_in_progress = !write_queue_.empty(); // 检查写队列是否存在数据
  write_queue_.push_back(std::move(framed)); // 加入到写队列
  if (!write_in_progress) { // 写队列为空
    do_write();
  }
}

TcpServer::TcpServer(asio::io_context& io, uint16_t port):  // 构造函数
  io_context_(io), // 接收外部的io_context
  acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)) { // 监听和接受TCP连接，绑定端口
}

void TcpServer::start() { // public入口函数
  do_accept(); 
}

void TcpServer::do_accept() { // 创建异步非阻塞连接
  acceptor_.async_accept([this](const asio::error_code& ec, tcp::socket socket) { // 异步接受连接，注册回调函数
      // 这里的socket无需手动创建，async_accept会自动创建
    if (!ec) {
        std::make_shared<TcpSession>(std::move(socket))->start(); 
        // 将socket权限转移给Session,对于单个连接由TcpSession单独负责
    } // 立即返回，非阻塞，当有客户端连接时，asio调用回调函数
    do_accept(); // 再次创建一个新的连接，递归
  });
}
