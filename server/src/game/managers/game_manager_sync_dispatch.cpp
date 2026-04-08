#include "internal/game_manager_sync_dispatch.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <google/protobuf/io/coded_stream.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "internal/game_manager_internal_utils.hpp"
#include "network/shared/shared_string_pool.hpp"
#include "network/tcp/tcp_session.hpp"
#include "network/udp/udp_server.hpp"

namespace {
using game_manager_internal::FillDeltaTiming;
using game_manager_internal::FillSyncTiming;

constexpr std::size_t kUdpPacketBudgetBytes = 1200;

struct FramedPacket {
  lawnmower::MessageType type = lawnmower::MessageType::MSG_UNKNOWN;
  std::shared_ptr<const std::string> framed;
  std::size_t payload_len = 0;
  std::size_t body_len = 0;
};

enum class StatePacketRoute {
  kUdpPreferredTcpFallback = 0,
  kTcpOnly = 1,
};

struct PreparedStatePacket {
  bool is_sync = false;
  StatePacketRoute route = StatePacketRoute::kTcpOnly;
  bool target_session_only = false;
  std::weak_ptr<TcpSession> target_session;
  lawnmower::S2C_GameStateSync sync;
  lawnmower::S2C_GameStateDeltaSync delta;
};

std::atomic<uint64_t> g_next_snapshot_id{1};

std::unordered_map<uint32_t, std::deque<PreparedStatePacket>>&
DeferredStateBacklogs() {
  static auto* backlogs =
      new std::unordered_map<uint32_t, std::deque<PreparedStatePacket>>();
  return *backlogs;
}

std::deque<PreparedStatePacket>& DeferredStateBacklogForRoom(uint32_t room_id) {
  return DeferredStateBacklogs()[room_id];
}

void MaybeClearDeferredStateBacklog(uint32_t room_id) {
  auto& backlogs = DeferredStateBacklogs();
  auto it = backlogs.find(room_id);
  if (it != backlogs.end() && it->second.empty()) {
    backlogs.erase(it);
  }
}

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

  const auto payload_size = static_cast<uint32_t>(std::min<std::size_t>(
      message.ByteSizeLong(), std::numeric_limits<uint32_t>::max()));
  return 1u + CodedOutputStream::VarintSize32(static_cast<uint32_t>(type)) +
         1u + CodedOutputStream::VarintSize32(payload_size) + payload_size;
}

std::size_t ComputeUdpPacketSizeFromPayload(lawnmower::MessageType type,
                                            std::size_t payload_size) {
  using google::protobuf::io::CodedOutputStream;

  const auto safe_payload = static_cast<uint32_t>(std::min<std::size_t>(
      payload_size, std::numeric_limits<uint32_t>::max()));
  return 1u + CodedOutputStream::VarintSize32(static_cast<uint32_t>(type)) +
         1u + CodedOutputStream::VarintSize32(safe_payload) + safe_payload;
}

template <typename TMessage>
std::size_t ComputeEmbeddedMessageFieldSize(int field_number,
                                            const TMessage& message) {
  using google::protobuf::io::CodedOutputStream;

  const auto entry_size = static_cast<uint32_t>(std::min<std::size_t>(
      message.ByteSizeLong(), std::numeric_limits<uint32_t>::max()));
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
  chunk.set_snapshot_id(source.snapshot_id());
  chunk.set_chunk_index(source.chunk_index());
  chunk.set_chunk_count(source.chunk_count());
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

uint32_t CountSyncObjects(const lawnmower::S2C_GameStateSync& sync) {
  return static_cast<uint32_t>(sync.players_size() + sync.enemies_size() +
                               sync.items_size());
}

lawnmower::S2C_GameStateSync InitFullSnapshotChunk(
    const lawnmower::S2C_GameStateSync& source, uint64_t snapshot_id) {
  lawnmower::S2C_GameStateSync chunk;
  if (source.has_sync_time()) {
    *chunk.mutable_sync_time() = source.sync_time();
  }
  chunk.set_room_id(source.room_id());
  chunk.set_is_full_snapshot(true);
  chunk.set_snapshot_id(snapshot_id);
  return chunk;
}

std::vector<lawnmower::S2C_GameStateSync> SplitFullSnapshotForTcp(
    const lawnmower::S2C_GameStateSync& sync) {
  if (!sync.is_full_snapshot()) {
    return {sync};
  }

  const uint32_t object_budget = std::max<uint32_t>(
      1, GameManager::Instance().GetConfig().room_sync_object_budget_per_tick);
  const uint64_t snapshot_id =
      g_next_snapshot_id.fetch_add(1, std::memory_order_relaxed);
  if (CountSyncObjects(sync) <= object_budget) {
    auto single = sync;
    single.set_snapshot_id(snapshot_id);
    single.set_chunk_index(0);
    single.set_chunk_count(1);
    return {single};
  }

  std::vector<lawnmower::S2C_GameStateSync> chunks;
  lawnmower::S2C_GameStateSync current =
      InitFullSnapshotChunk(sync, snapshot_id);
  uint32_t current_count = 0;
  auto flush_current = [&]() {
    if (current_count == 0) {
      return;
    }
    chunks.push_back(std::move(current));
    current = InitFullSnapshotChunk(sync, snapshot_id);
    current_count = 0;
  };
  auto append_entry = [&](const auto& value, auto add_entry) {
    if (current_count >= object_budget) {
      flush_current();
    }
    add_entry(&current, value);
    current_count += 1;
  };

  for (const auto& player : sync.players()) {
    append_entry(player, [](auto* out, const auto& entry) {
      *out->add_players() = entry;
    });
  }
  for (const auto& enemy : sync.enemies()) {
    append_entry(enemy, [](auto* out, const auto& entry) {
      *out->add_enemies() = entry;
    });
  }
  for (const auto& item : sync.items()) {
    append_entry(item, [](auto* out, const auto& entry) {
      *out->add_items() = entry;
    });
  }
  flush_current();

  const uint32_t chunk_count = static_cast<uint32_t>(chunks.size());
  for (uint32_t i = 0; i < chunk_count; ++i) {
    chunks[i].set_chunk_index(i);
    chunks[i].set_chunk_count(chunk_count);
  }
  return chunks;
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
  auto append_with_budget = [&](const auto& entry, int field_number,
                                auto add_entry) {
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
    append_with_budget(player, 2,
                       [](lawnmower::S2C_GameStateSync* out, const auto& value) {
                         *out->add_players() = value;
                       });
  }
  for (const auto& enemy : sync.enemies()) {
    append_with_budget(enemy, 3,
                       [](lawnmower::S2C_GameStateSync* out, const auto& value) {
                         *out->add_enemies() = value;
                       });
  }
  for (const auto& item : sync.items()) {
    append_with_budget(item, 4,
                       [](lawnmower::S2C_GameStateSync* out, const auto& value) {
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
  auto append_with_budget = [&](const auto& entry, int field_number,
                                auto add_entry) {
    const auto entry_size = ComputeEmbeddedMessageFieldSize(field_number, entry);
    if (!IsChunkEmpty(current) &&
        ComputeUdpPacketSizeFromPayload(
            MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC,
            current_payload_size + entry_size) > kUdpPacketBudgetBytes) {
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
        player, 3,
        [](lawnmower::S2C_GameStateDeltaSync* out, const auto& value) {
          *out->add_players() = value;
        });
  }
  for (const auto& enemy : sync.enemies()) {
    append_with_budget(
        enemy, 4,
        [](lawnmower::S2C_GameStateDeltaSync* out, const auto& value) {
          *out->add_enemies() = value;
        });
  }
  for (const auto& item : sync.items()) {
    append_with_budget(
        item, 5,
        [](lawnmower::S2C_GameStateDeltaSync* out, const auto& value) {
          *out->add_items() = value;
        });
  }
  flush_current();
  return chunks.empty() ? std::vector<lawnmower::S2C_GameStateDeltaSync>{sync}
                        : chunks;
}

FramedPacket BuildFramedPacket(lawnmower::MessageType type,
                               const google::protobuf::Message& message) {
  lawnmower::Packet packet;
  packet.set_msg_type(type);
  const auto payload_len = static_cast<std::size_t>(message.ByteSizeLong());
  auto* payload = packet.mutable_payload();
  payload->resize(payload_len);
  if (payload_len > 0 &&
      !message.SerializeToArray(payload->data(), static_cast<int>(payload_len))) {
    return {type, nullptr, 0, 0};
  }
  const auto body_len = static_cast<std::size_t>(packet.ByteSizeLong());
  const uint32_t net_len = htonl(static_cast<uint32_t>(body_len));
  auto framed = network_shared::AcquireSharedString(sizeof(net_len) + body_len);
  framed->resize(sizeof(net_len) + body_len);
  std::memcpy(framed->data(), &net_len, sizeof(net_len));
  if (body_len > 0 &&
      !packet.SerializeToArray(framed->data() + sizeof(net_len),
                               static_cast<int>(body_len))) {
    return {type, nullptr, 0, 0};
  }
  return {type, framed, payload_len, body_len};
}

void SendFramedToSessions(std::span<const std::weak_ptr<TcpSession>> sessions,
                          const FramedPacket& packet) {
  if (packet.framed == nullptr) {
    return;
  }
  for (const auto& weak_session : sessions) {
    if (auto session = weak_session.lock()) {
      session->SendFramedPacket(packet.framed, packet.type, packet.payload_len,
                                packet.body_len);
    }
  }
}

void SendSyncToSessions(std::span<const std::weak_ptr<TcpSession>> sessions,
                        const lawnmower::S2C_GameStateSync& sync) {
  const auto chunks = SplitFullSnapshotForTcp(sync);
  for (const auto& chunk : chunks) {
    const auto packet = BuildFramedPacket(
        lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC, chunk);
    if (packet.framed == nullptr) {
      spdlog::warn("构造全量同步 TCP 帧失败 room={}", sync.room_id());
      continue;
    }
    SendFramedToSessions(sessions, packet);
  }
}

void SendSingleSyncChunkToSessions(
    std::span<const std::weak_ptr<TcpSession>> sessions,
    const lawnmower::S2C_GameStateSync& sync) {
  const auto packet = BuildFramedPacket(
      lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC, sync);
  if (packet.framed == nullptr) {
    spdlog::warn("构造全量同步 TCP 帧失败 room={}", sync.room_id());
    return;
  }
  SendFramedToSessions(sessions, packet);
}

void SendDeltaToSessions(std::span<const std::weak_ptr<TcpSession>> sessions,
                         const lawnmower::S2C_GameStateDeltaSync& sync) {
  const auto packet = BuildFramedPacket(
      lawnmower::MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC, sync);
  if (packet.framed == nullptr) {
    spdlog::warn("构造增量同步 TCP 帧失败 room={}", sync.room_id());
    return;
  }
  SendFramedToSessions(sessions, packet);
}

bool HasSyncPayload(bool built_sync, const lawnmower::S2C_GameStateSync& sync) {
  return built_sync && (sync.players_size() > 0 || sync.enemies_size() > 0 ||
                        sync.items_size() > 0);
}

bool HasDeltaPayload(bool built_delta,
                     const lawnmower::S2C_GameStateDeltaSync& delta) {
  return built_delta && (delta.players_size() > 0 || delta.enemies_size() > 0 ||
                         delta.items_size() > 0);
}

std::vector<PreparedStatePacket> PrepareDeltaPackets(
    const lawnmower::S2C_GameStateDeltaSync& delta) {
  std::vector<PreparedStatePacket> prepared;
  const auto chunks = SplitDeltaSyncForUdp(delta);
  prepared.reserve(chunks.size());
  for (const auto& chunk : chunks) {
    PreparedStatePacket packet;
    packet.is_sync = false;
    packet.route = StatePacketRoute::kUdpPreferredTcpFallback;
    packet.delta = chunk;
    prepared.push_back(std::move(packet));
  }
  return prepared;
}

std::vector<PreparedStatePacket> PrepareSyncPackets(
    bool force_full_sync, bool has_delta_payload,
    const lawnmower::S2C_GameStateSync& sync) {
  std::vector<PreparedStatePacket> prepared;
  const bool allow_udp_sync = !force_full_sync && !has_delta_payload;
  if (allow_udp_sync) {
    const auto chunks = SplitStateSyncForUdp(sync);
    prepared.reserve(chunks.size());
    for (const auto& chunk : chunks) {
      PreparedStatePacket packet;
      packet.is_sync = true;
      packet.route = StatePacketRoute::kUdpPreferredTcpFallback;
      packet.sync = chunk;
      prepared.push_back(std::move(packet));
    }
    return prepared;
  }

  const auto chunks = SplitFullSnapshotForTcp(sync);
  prepared.reserve(chunks.size());
  for (const auto& chunk : chunks) {
    PreparedStatePacket packet;
    packet.is_sync = true;
    packet.route = StatePacketRoute::kTcpOnly;
    packet.sync = chunk;
    prepared.push_back(std::move(packet));
  }
  return prepared;
}

std::vector<PreparedStatePacket> PrepareTargetedSyncPackets(
    const std::shared_ptr<TcpSession>& session,
    const lawnmower::S2C_GameStateSync& sync) {
  std::vector<PreparedStatePacket> prepared;
  const auto chunks = SplitFullSnapshotForTcp(sync);
  prepared.reserve(chunks.size());
  for (const auto& chunk : chunks) {
    PreparedStatePacket packet;
    packet.is_sync = true;
    packet.route = StatePacketRoute::kTcpOnly;
    packet.target_session_only = true;
    packet.target_session = session;
    packet.sync = chunk;
    prepared.push_back(std::move(packet));
  }
  return prepared;
}

void RefreshPreparedStatePacketTiming(uint32_t room_id, uint64_t dispatch_tick,
                                      PreparedStatePacket* packet) {
  if (packet == nullptr) {
    return;
  }
  if (packet->is_sync) {
    FillSyncTiming(room_id, dispatch_tick, &packet->sync);
    return;
  }
  FillDeltaTiming(room_id, dispatch_tick, &packet->delta);
}

bool SendPreparedStatePacket(
    uint32_t room_id, uint64_t dispatch_tick, UdpServer* udp_server,
    std::span<const std::weak_ptr<TcpSession>> sessions,
    const PreparedStatePacket& prepared) {
  PreparedStatePacket packet = prepared;
  RefreshPreparedStatePacketTiming(room_id, dispatch_tick, &packet);
  std::vector<std::weak_ptr<TcpSession>> targeted_sessions_storage;
  std::span<const std::weak_ptr<TcpSession>> send_sessions = sessions;
  if (packet.target_session_only) {
    if (packet.target_session.expired()) {
      return true;
    }
    targeted_sessions_storage.push_back(packet.target_session);
    send_sessions = targeted_sessions_storage;
  }

  if (!packet.is_sync) {
    if (packet.route == StatePacketRoute::kUdpPreferredTcpFallback &&
        udp_server != nullptr &&
        udp_server->BroadcastDeltaState(room_id, packet.delta) > 0) {
      return true;
    }
    if (!send_sessions.empty()) {
      SendDeltaToSessions(send_sessions, packet.delta);
      return true;
    }
    return false;
  }

  if (packet.route == StatePacketRoute::kUdpPreferredTcpFallback &&
      udp_server != nullptr &&
      udp_server->BroadcastState(room_id, packet.sync) > 0) {
    return true;
  }
  if (!send_sessions.empty()) {
    SendSingleSyncChunkToSessions(send_sessions, packet.sync);
    return true;
  }
  return false;
}

uint32_t DispatchPreparedStatePackets(
    uint32_t room_id, uint64_t dispatch_tick, UdpServer* udp_server,
    std::span<const std::weak_ptr<TcpSession>> sessions, uint32_t packet_budget,
    std::vector<PreparedStatePacket> current_packets) {
  auto& backlog = DeferredStateBacklogForRoom(room_id);
  uint32_t sent_packets = 0;

  while (!backlog.empty() && sent_packets < packet_budget) {
    if (!SendPreparedStatePacket(room_id, dispatch_tick, udp_server, sessions,
                                 backlog.front())) {
      break;
    }
    backlog.pop_front();
    sent_packets += 1;
  }

  for (auto& packet : current_packets) {
    if (sent_packets < packet_budget &&
        SendPreparedStatePacket(room_id, dispatch_tick, udp_server, sessions,
                                packet)) {
      sent_packets += 1;
      continue;
    }
    backlog.push_back(std::move(packet));
  }

  MaybeClearDeferredStateBacklog(room_id);
  return sent_packets;
}
}  // namespace

namespace game_manager_sync_dispatch {

uint32_t EstimateStatePacketCount(bool force_full_sync, bool built_sync,
                                  bool built_delta,
                                  const lawnmower::S2C_GameStateSync& sync,
                                  const lawnmower::S2C_GameStateDeltaSync& delta) {
  uint32_t count = 0;
  const bool has_delta_payload = HasDeltaPayload(built_delta, delta);
  if (has_delta_payload) {
    count += static_cast<uint32_t>(PrepareDeltaPackets(delta).size());
  }
  if (HasSyncPayload(built_sync, sync)) {
    count += static_cast<uint32_t>(
        PrepareSyncPackets(force_full_sync, has_delta_payload, sync).size());
  }
  return count;
}

uint32_t QueuedStatePacketCount(uint32_t room_id) {
  const auto& backlogs = DeferredStateBacklogs();
  auto it = backlogs.find(room_id);
  if (it == backlogs.end()) {
    return 0;
  }
  return static_cast<uint32_t>(it->second.size());
}

void SendFullSnapshotToSessions(
    const std::vector<std::weak_ptr<TcpSession>>& sessions,
    const lawnmower::S2C_GameStateSync& sync) {
  auto packets = PrepareSyncPackets(true, false, sync);
  const uint32_t packet_budget = std::max<uint32_t>(
      1, GameManager::Instance().GetConfig().room_packet_budget_per_tick);
  DispatchPreparedStatePackets(sync.room_id(), sync.sync_time().tick(), nullptr,
                               sessions, packet_budget, std::move(packets));
}

void SendFullSnapshotToSession(
    const std::shared_ptr<TcpSession>& session,
    const lawnmower::S2C_GameStateSync& sync) {
  if (session == nullptr) {
    return;
  }
  const auto sessions = RoomManager::Instance().GetRoomSessions(sync.room_id());
  const uint32_t packet_budget = std::max<uint32_t>(
      1, GameManager::Instance().GetConfig().room_packet_budget_per_tick);
  auto packets = PrepareTargetedSyncPackets(session, sync);
  DispatchPreparedStatePackets(sync.room_id(), sync.sync_time().tick(),
                               nullptr, sessions, packet_budget,
                               std::move(packets));
}

uint32_t DispatchStateSyncPayloads(
    uint32_t room_id, uint64_t dispatch_tick, UdpServer* udp_server,
    const std::vector<std::weak_ptr<TcpSession>>& sessions, bool force_full_sync,
    bool built_sync, bool built_delta,
    const lawnmower::S2C_GameStateSync& sync,
    const lawnmower::S2C_GameStateDeltaSync& delta, uint32_t packet_budget) {
  const bool has_sync_payload = HasSyncPayload(built_sync, sync);
  const bool has_delta_payload = HasDeltaPayload(built_delta, delta);
  if (!has_sync_payload && !has_delta_payload) {
    MaybeClearDeferredStateBacklog(room_id);
    return DispatchPreparedStatePackets(room_id, dispatch_tick, udp_server,
                                        sessions, packet_budget, {});
  }

  std::vector<PreparedStatePacket> current_packets;
  if (has_delta_payload) {
    auto delta_packets = PrepareDeltaPackets(delta);
    current_packets.insert(current_packets.end(),
                           std::make_move_iterator(delta_packets.begin()),
                           std::make_move_iterator(delta_packets.end()));
  }
  if (has_sync_payload) {
    auto sync_packets = PrepareSyncPackets(force_full_sync, has_delta_payload,
                                           sync);
    current_packets.insert(current_packets.end(),
                           std::make_move_iterator(sync_packets.begin()),
                           std::make_move_iterator(sync_packets.end()));
  }

  return DispatchPreparedStatePackets(room_id, dispatch_tick, udp_server,
                                      sessions, packet_budget,
                                      std::move(current_packets));
}

}  // namespace game_manager_sync_dispatch
