#pragma once

#include <array>
#include <asio.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "message.pb.h"

using udp = asio::ip::udp;

// 轻量 UDP 通道：收集客户端输入、广播状态同步
class UdpServer {
 public:
  UdpServer(asio::io_context& io, uint16_t port);

  // 开始异步接收（需与 io_context.run() 同步驱动）
  void Start();

  // 广播游戏状态到指定房间的已登记终端
  std::size_t BroadcastState(uint32_t room_id,
                             const lawnmower::S2C_GameStateSync& sync);
  // 广播游戏状态增量到指定房间的已登记终端
  std::size_t BroadcastDeltaState(
      uint32_t room_id, const lawnmower::S2C_GameStateDeltaSync& sync);

 private:
  struct EndpointInfo {
    udp::endpoint endpoint;
    uint32_t room_id = 0;
    std::chrono::steady_clock::time_point last_seen;
  };

  void DoReceive();
  void HandlePacket(const lawnmower::Packet& packet, const udp::endpoint& from);
  void HandlePlayerInput(const lawnmower::Packet& packet,
                         const udp::endpoint& from);
  void SendPacket(const std::shared_ptr<const std::string>& data,
                  const udp::endpoint& to);
  std::vector<udp::endpoint> EndpointsForRoom(uint32_t room_id);

  asio::io_context& io_context_;
  udp::socket socket_;
  std::array<char, 64 * 1024> recv_buffer_{};
  udp::endpoint remote_endpoint_;

  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, EndpointInfo> player_endpoints_;
};
