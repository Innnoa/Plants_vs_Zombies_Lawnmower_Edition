#include "network/udp/udp_server.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <string>

#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "network/tcp/tcp_session.hpp"

namespace {
constexpr std::chrono::seconds kEndpointTtl{10};
constexpr int kUdpSocketBufferBytes = 256 * 1024;
}  // namespace

// 构造
UdpServer::UdpServer(asio::io_context& io, uint16_t port)
    : io_context_(io), socket_(io_context_, udp::endpoint(udp::v4(), port)) {
  asio::error_code ec;
  socket_.set_option(
      asio::socket_base::receive_buffer_size(kUdpSocketBufferBytes), ec);
  if (ec) {
    spdlog::warn("UDP 设置接收缓冲区失败: {}", ec.message());
  }
  ec.clear();
  socket_.set_option(asio::socket_base::send_buffer_size(kUdpSocketBufferBytes),
                     ec);
  if (ec) {
    spdlog::warn("UDP 设置发送缓冲区失败: {}", ec.message());
  }
}

void UdpServer::Start() { DoReceive(); }

void UdpServer::DoReceive() {
  socket_.async_receive_from(
      asio::buffer(recv_buffer_), remote_endpoint_,
      [this](const asio::error_code& ec, std::size_t bytes) {
        if (!ec && bytes > 0) {
          lawnmower::Packet packet;
          if (packet.ParseFromArray(recv_buffer_.data(),
                                    static_cast<int>(bytes))) {
            HandlePacket(packet, remote_endpoint_);
          } else {
            spdlog::debug("UDP 解析 Packet 失败，长度 {}", bytes);
          }
        } else if (ec != asio::error::operation_aborted) {
          spdlog::warn("UDP 接收失败: {}", ec.message());
        }
        DoReceive();
      });
}

void UdpServer::HandlePacket(const lawnmower::Packet& packet,
                             const udp::endpoint& from) {
  using lawnmower::MessageType;
  switch (packet.msg_type()) {
    case MessageType::MSG_C2S_PLAYER_INPUT:
      HandlePlayerInput(packet, from);
      break;
    default:
      spdlog::debug("UDP 收到未处理消息类型 {}",
                    static_cast<int>(packet.msg_type()));
      break;
  }
}

void UdpServer::HandlePlayerInput(const lawnmower::Packet& packet,
                                  const udp::endpoint& from) {
  lawnmower::C2S_PlayerInput input;
  if (!input.ParseFromString(packet.payload())) {
    spdlog::debug("UDP 输入解析失败");
    return;
  }

  const uint32_t player_id = input.player_id();
  if (player_id == 0) {
    spdlog::debug("UDP 输入缺少 player_id");
    return;
  }

  if (input.session_token().empty() ||
      !TcpSession::VerifyToken(player_id, input.session_token())) {
    spdlog::debug("UDP 输入令牌校验失败 player_id={}", player_id);
    return;
  }

  auto room_opt = RoomManager::Instance().GetPlayerRoom(player_id);
  if (!room_opt.has_value()) {
    spdlog::debug("UDP 输入: player {} 不在任何房间，丢弃", player_id);
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    player_endpoints_[player_id] =
        EndpointInfo{from, *room_opt, std::chrono::steady_clock::now()};
  }

  uint32_t room_id = 0;
  if (!GameManager::Instance().HandlePlayerInput(player_id, input, &room_id)) {
    spdlog::debug("UDP 输入: player {} 未被受理", player_id);
  }
}

// UDP广播
std::size_t UdpServer::BroadcastState(
    uint32_t room_id, const lawnmower::S2C_GameStateSync& sync) {
  const auto targets = EndpointsForRoom(room_id);
  if (targets.empty()) {
    return 0;
  }

  lawnmower::Packet packet;
  packet.set_msg_type(lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC);
  packet.set_payload(sync.SerializeAsString());

  std::shared_ptr<const std::string> data =
      std::make_shared<std::string>(packet.SerializeAsString());

  if (spdlog::should_log(spdlog::level::debug)) {
    spdlog::debug("UDP 广播房间 {} 状态，players={} enemies={}，目标端点 {}",
                  room_id, sync.players_size(), sync.enemies_size(),
                  targets.size());
  }
  for (const auto& endpoint : targets) {
    SendPacket(data, endpoint);
  }

  return targets.size();
}

std::size_t UdpServer::BroadcastDeltaState(
    uint32_t room_id, const lawnmower::S2C_GameStateDeltaSync& sync) {
  const auto targets = EndpointsForRoom(room_id);
  if (targets.empty()) {
    return 0;
  }

  lawnmower::Packet packet;
  packet.set_msg_type(lawnmower::MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC);
  packet.set_payload(sync.SerializeAsString());

  std::shared_ptr<const std::string> data =
      std::make_shared<std::string>(packet.SerializeAsString());

  if (spdlog::should_log(spdlog::level::debug)) {
    spdlog::debug(
        "UDP 广播房间 {} 状态增量，players={} enemies={}，目标端点 {}", room_id,
        sync.players_size(), sync.enemies_size(), targets.size());
  }
  for (const auto& endpoint : targets) {
    SendPacket(data, endpoint);
  }

  return targets.size();
}

std::vector<udp::endpoint> UdpServer::EndpointsForRoom(uint32_t room_id) {
  const auto now = std::chrono::steady_clock::now();
  std::vector<udp::endpoint> endpoints;

  std::lock_guard<std::mutex> lock(mutex_);
  endpoints.reserve(player_endpoints_.size());
  for (auto it = player_endpoints_.begin(); it != player_endpoints_.end();) {
    const bool expired = (now - it->second.last_seen) > kEndpointTtl;
    if (expired) {
      it = player_endpoints_.erase(it);
      continue;
    }
    if (it->second.room_id == room_id) {
      endpoints.push_back(it->second.endpoint);
    }
    ++it;
  }

  return endpoints;
}

void UdpServer::SendPacket(const std::shared_ptr<const std::string>& data,
                           const udp::endpoint& to) {
  if (!data || data->empty()) {
    return;
  }

  socket_.async_send_to(
      asio::buffer(*data), to,
      [data, to](const asio::error_code& ec, std::size_t bytes) {
        if (ec == asio::error::operation_aborted) {
          return;
        }
        if (!spdlog::should_log(spdlog::level::debug)) {
          return;
        }
        const std::string addr = to.address().to_string();
        if (ec) {
          spdlog::debug("UDP 发送到 {}:{} 失败: {}", addr, to.port(),
                        ec.message());
        } else {
          spdlog::debug("UDP 发送 {} bytes 到 {}:{}", bytes, addr, to.port());
        }
      });
}
