#include "network/udp/udp_server.hpp"

#include <algorithm>
#include <cstddef>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/message.h>
#include <limits>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "network/tcp/tcp_session.hpp"
#include "network/shared/shared_string_pool.hpp"

namespace {
constexpr std::chrono::seconds kEndpointTtl{10};
constexpr int kUdpSocketBufferBytes = 256 * 1024;
constexpr std::size_t kUdpPacketBudgetBytes = 1200;

template <typename TMessage>
std::shared_ptr<const std::string> BuildUdpPacketData(
    lawnmower::MessageType type, const TMessage& message) {
  lawnmower::Packet packet;
  packet.set_msg_type(type);
  const auto payload_size = static_cast<std::size_t>(message.ByteSizeLong());
  auto* payload = packet.mutable_payload();
  payload->resize(payload_size);
  if (payload_size > 0 &&
      !message.SerializeToArray(payload->data(), static_cast<int>(payload_size))) {
    return nullptr;
  }
  const auto packet_size = static_cast<std::size_t>(packet.ByteSizeLong());
  auto data = network_shared::AcquireSharedString(packet_size);
  data->resize(packet_size);
  if (packet_size > 0 &&
      !packet.SerializeToArray(data->data(), static_cast<int>(packet_size))) {
    return nullptr;
  }
  return data;
}

template <typename TMessage>
std::size_t ComputeUdpPacketSize(lawnmower::MessageType type,
                                 const TMessage& message) {
  using google::protobuf::io::CodedOutputStream;

  const auto payload_size =
      static_cast<uint32_t>(std::min<std::size_t>(message.ByteSizeLong(),
                                                  std::numeric_limits<uint32_t>::max()));
  return 1u + CodedOutputStream::VarintSize32(static_cast<uint32_t>(type)) +
         1u + CodedOutputStream::VarintSize32(payload_size) + payload_size;
}


std::size_t ComputeUdpPacketSizeFromPayload(lawnmower::MessageType type,
                                            std::size_t payload_size) {
  using google::protobuf::io::CodedOutputStream;

  const auto safe_payload = static_cast<uint32_t>(
      std::min<std::size_t>(payload_size, std::numeric_limits<uint32_t>::max()));
  return 1u + CodedOutputStream::VarintSize32(static_cast<uint32_t>(type)) +
         1u + CodedOutputStream::VarintSize32(safe_payload) + safe_payload;
}

template <typename TMessage>
std::size_t ComputeEmbeddedMessageFieldSize(int field_number,
                                            const TMessage& message) {
  using google::protobuf::io::CodedOutputStream;

  const auto entry_size = static_cast<uint32_t>(
      std::min<std::size_t>(message.ByteSizeLong(), std::numeric_limits<uint32_t>::max()));
  const uint32_t tag =
      static_cast<uint32_t>((static_cast<uint32_t>(field_number) << 3u) | 2u);
  return CodedOutputStream::VarintSize32(tag) +
         CodedOutputStream::VarintSize32(entry_size) + entry_size;
}

bool IsChunkEmpty(const lawnmower::S2C_GameStateSync& sync) {
  return sync.players_size() == 0 && sync.enemies_size() == 0 &&
         sync.items_size() == 0;
}

bool IsChunkEmpty(const lawnmower::S2C_GameStateDeltaSync& sync) {
  return sync.players_size() == 0 && sync.enemies_size() == 0 &&
         sync.items_size() == 0;
}

lawnmower::S2C_GameStateSync InitSyncChunk(
    const lawnmower::S2C_GameStateSync& source) {
  lawnmower::S2C_GameStateSync chunk;
  if (source.has_sync_time()) {
    *chunk.mutable_sync_time() = source.sync_time();
  }
  chunk.set_room_id(source.room_id());
  chunk.set_is_full_snapshot(source.is_full_snapshot());
  return chunk;
}

lawnmower::S2C_GameStateDeltaSync InitDeltaChunk(
    const lawnmower::S2C_GameStateDeltaSync& source) {
  lawnmower::S2C_GameStateDeltaSync chunk;
  if (source.has_sync_time()) {
    *chunk.mutable_sync_time() = source.sync_time();
  }
  chunk.set_room_id(source.room_id());
  return chunk;
}

void LogOversizedUdpObject(lawnmower::MessageType type, uint32_t room_id,
                           std::size_t packet_size) {
  spdlog::warn(
      "UDP 单对象包仍超预算 type={} room={} packet_size={} budget={}",
      lawnmower::MessageType_Name(type), room_id, packet_size,
      kUdpPacketBudgetBytes);
}

std::vector<lawnmower::S2C_GameStateSync> SplitStateSyncForUdp(
    const lawnmower::S2C_GameStateSync& sync) {
  using lawnmower::MessageType;

  if (sync.is_full_snapshot() ||
      ComputeUdpPacketSize(MessageType::MSG_S2C_GAME_STATE_SYNC, sync) <=
          kUdpPacketBudgetBytes) {
    return {sync};
  }

  std::vector<lawnmower::S2C_GameStateSync> chunks;
  lawnmower::S2C_GameStateSync current = InitSyncChunk(sync);
  std::size_t current_payload_size = current.ByteSizeLong();
  auto flush_current = [&]() {
    if (!IsChunkEmpty(current)) {
      chunks.push_back(std::move(current));
      current = InitSyncChunk(sync);
      current_payload_size = current.ByteSizeLong();
    }
  };
  auto append_with_budget = [&](const auto& entry, int field_number, auto add_entry) {
    const auto entry_size = ComputeEmbeddedMessageFieldSize(field_number, entry);
    if (!IsChunkEmpty(current) &&
        ComputeUdpPacketSizeFromPayload(MessageType::MSG_S2C_GAME_STATE_SYNC,
                                        current_payload_size + entry_size) >
            kUdpPacketBudgetBytes) {
      flush_current();
    }
    add_entry(&current, entry);
    current_payload_size += entry_size;
    const auto packet_size = ComputeUdpPacketSizeFromPayload(
        MessageType::MSG_S2C_GAME_STATE_SYNC, current_payload_size);
    if (packet_size > kUdpPacketBudgetBytes) {
      LogOversizedUdpObject(MessageType::MSG_S2C_GAME_STATE_SYNC, sync.room_id(),
                            packet_size);
    }
  };

  for (const auto& player : sync.players()) {
    append_with_budget(
        player, 2, [](lawnmower::S2C_GameStateSync* out, const auto& value) {
          *out->add_players() = value;
        });
  }
  for (const auto& enemy : sync.enemies()) {
    append_with_budget(
        enemy, 3, [](lawnmower::S2C_GameStateSync* out, const auto& value) {
          *out->add_enemies() = value;
        });
  }
  for (const auto& item : sync.items()) {
    append_with_budget(
        item, 4, [](lawnmower::S2C_GameStateSync* out, const auto& value) {
          *out->add_items() = value;
        });
  }
  flush_current();
  return chunks.empty() ? std::vector<lawnmower::S2C_GameStateSync>{sync}
                        : chunks;
}

std::vector<lawnmower::S2C_GameStateDeltaSync> SplitDeltaSyncForUdp(
    const lawnmower::S2C_GameStateDeltaSync& sync) {
  using lawnmower::MessageType;

  if (ComputeUdpPacketSize(MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC, sync) <=
      kUdpPacketBudgetBytes) {
    return {sync};
  }

  std::vector<lawnmower::S2C_GameStateDeltaSync> chunks;
  lawnmower::S2C_GameStateDeltaSync current = InitDeltaChunk(sync);
  std::size_t current_payload_size = current.ByteSizeLong();
  auto flush_current = [&]() {
    if (!IsChunkEmpty(current)) {
      chunks.push_back(std::move(current));
      current = InitDeltaChunk(sync);
      current_payload_size = current.ByteSizeLong();
    }
  };
  auto append_with_budget = [&](const auto& entry, int field_number, auto add_entry) {
    const auto entry_size = ComputeEmbeddedMessageFieldSize(field_number, entry);
    if (!IsChunkEmpty(current) &&
        ComputeUdpPacketSizeFromPayload(MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC,
                                        current_payload_size + entry_size) >
            kUdpPacketBudgetBytes) {
      flush_current();
    }
    add_entry(&current, entry);
    current_payload_size += entry_size;
    const auto packet_size = ComputeUdpPacketSizeFromPayload(
        MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC, current_payload_size);
    if (packet_size > kUdpPacketBudgetBytes) {
      LogOversizedUdpObject(MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC,
                            sync.room_id(), packet_size);
    }
  };

  for (const auto& player : sync.players()) {
    append_with_budget(
        player, 3, [](lawnmower::S2C_GameStateDeltaSync* out, const auto& value) {
          *out->add_players() = value;
        });
  }
  for (const auto& enemy : sync.enemies()) {
    append_with_budget(
        enemy, 4, [](lawnmower::S2C_GameStateDeltaSync* out, const auto& value) {
          *out->add_enemies() = value;
        });
  }
  for (const auto& item : sync.items()) {
    append_with_budget(
        item, 5, [](lawnmower::S2C_GameStateDeltaSync* out, const auto& value) {
          *out->add_items() = value;
        });
  }
  flush_current();
  return chunks.empty() ? std::vector<lawnmower::S2C_GameStateDeltaSync>{sync}
                        : chunks;
}
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
    auto existing = player_endpoints_.find(player_id);
    if (existing != player_endpoints_.end() && existing->second.room_id != *room_opt) {
      auto old_room_it = room_players_.find(existing->second.room_id);
      if (old_room_it != room_players_.end()) {
        old_room_it->second.erase(player_id);
        if (old_room_it->second.empty()) {
          room_players_.erase(old_room_it);
        }
      }
    }
    player_endpoints_[player_id] =
        EndpointInfo{from, *room_opt, std::chrono::steady_clock::now()};
    room_players_[*room_opt].insert(player_id);
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

  const auto chunks = SplitStateSyncForUdp(sync);

  if (spdlog::should_log(spdlog::level::debug)) {
    spdlog::debug(
        "UDP 广播房间 {} 状态，players={} enemies={} items={}，目标端点 {}，chunks={}",
        room_id, sync.players_size(), sync.enemies_size(), sync.items_size(),
        targets.size(), chunks.size());
  }
  for (const auto& chunk : chunks) {
    const auto data = BuildUdpPacketData(
        lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC, chunk);
    if (data == nullptr) {
      spdlog::warn("构造 UDP 状态包失败 room={}", room_id);
      continue;
    }
    for (const auto& endpoint : targets) {
      SendPacket(data, endpoint);
    }
  }

  return targets.size();
}

std::size_t UdpServer::BroadcastDeltaState(
    uint32_t room_id, const lawnmower::S2C_GameStateDeltaSync& sync) {
  const auto targets = EndpointsForRoom(room_id);
  if (targets.empty()) {
    return 0;
  }

  const auto chunks = SplitDeltaSyncForUdp(sync);

  if (spdlog::should_log(spdlog::level::debug)) {
    spdlog::debug(
        "UDP 广播房间 {} 状态增量，players={} enemies={} items={}，目标端点 {}，chunks={}",
        room_id, sync.players_size(), sync.enemies_size(), sync.items_size(),
        targets.size(), chunks.size());
  }
  for (const auto& chunk : chunks) {
    const auto data = BuildUdpPacketData(
        lawnmower::MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC, chunk);
    if (data == nullptr) {
      spdlog::warn("构造 UDP 增量包失败 room={}", room_id);
      continue;
    }
    for (const auto& endpoint : targets) {
      SendPacket(data, endpoint);
    }
  }

  return targets.size();
}

void UdpServer::RemovePlayerEndpointLocked(uint32_t player_id) {
  auto endpoint_it = player_endpoints_.find(player_id);
  if (endpoint_it == player_endpoints_.end()) {
    return;
  }
  const uint32_t room_id = endpoint_it->second.room_id;
  auto room_it = room_players_.find(room_id);
  if (room_it != room_players_.end()) {
    room_it->second.erase(player_id);
    if (room_it->second.empty()) {
      room_players_.erase(room_it);
    }
  }
  player_endpoints_.erase(endpoint_it);
}

std::vector<udp::endpoint> UdpServer::EndpointsForRoom(uint32_t room_id) {
  const auto now = std::chrono::steady_clock::now();
  std::vector<udp::endpoint> endpoints;

  std::lock_guard<std::mutex> lock(mutex_);
  auto room_it = room_players_.find(room_id);
  if (room_it == room_players_.end()) {
    return endpoints;
  }

  endpoints.reserve(room_it->second.size());
  for (auto it = room_it->second.begin(); it != room_it->second.end();) {
    const uint32_t player_id = *it;
    auto endpoint_it = player_endpoints_.find(player_id);
    if (endpoint_it == player_endpoints_.end()) {
      it = room_it->second.erase(it);
      continue;
    }
    const bool expired = (now - endpoint_it->second.last_seen) > kEndpointTtl;
    if (expired) {
      player_endpoints_.erase(endpoint_it);
      it = room_it->second.erase(it);
      continue;
    }
    endpoints.push_back(endpoint_it->second.endpoint);
    ++it;
  }

  if (room_it->second.empty()) {
    room_players_.erase(room_it);
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
