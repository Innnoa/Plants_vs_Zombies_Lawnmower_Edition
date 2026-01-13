#pragma once

#include <array>
#include <asio.hpp>
#include <atomic>
#include <cstdint>
#include <deque>
#include <google/protobuf/message.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "message.pb.h"

using tcp = asio::ip::tcp;

// 单个 TCP 会话，负责收发包与消息分发
class TcpSession : public std::enable_shared_from_this<TcpSession> {
 public:
  explicit TcpSession(tcp::socket socket);

  void start();
  void SendProto(lawnmower::MessageType type,
                 const google::protobuf::Message& message);
  static bool VerifyToken(uint32_t player_id, std::string_view token);

 private:
  static std::string GenerateToken();
  static void RegisterToken(uint32_t player_id, std::string token);
  static void RevokeToken(uint32_t player_id);

  void read_header();
  void read_body(std::size_t length);
  void do_write();
  void handle_packet(const lawnmower::Packet& packet);
  void send_packet(const lawnmower::Packet& packet);
  void handle_disconnect();

  tcp::socket socket_;
  std::array<char, sizeof(uint32_t)> length_buffer_{};
  std::vector<char> read_buffer_;  // 读缓冲区
  std::deque<std::string> write_queue_;
  bool closed_ = false;
  uint32_t player_id_ = 0;
  std::string player_name_;
  std::string session_token_;
  static std::atomic<uint32_t> next_player_id_;
  static std::atomic<uint32_t> active_sessions_;  // 原子变量用于存储活跃会话
  static std::unordered_map<uint32_t, std::string>
      session_tokens_;  // player_id -- token
  static std::mutex token_mutex_;
};
