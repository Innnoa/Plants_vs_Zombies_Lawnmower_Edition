#include "internal/game_manager_sync_dispatch.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <cstring>
#include <memory>
#include <span>
#include <spdlog/spdlog.h>
#include <vector>

#include "game/managers/room_manager.hpp"
#include "network/tcp/tcp_session.hpp"
#include "network/udp/udp_server.hpp"

namespace {
struct FramedPacket {
  lawnmower::MessageType type = lawnmower::MessageType::MSG_UNKNOWN;
  std::shared_ptr<const std::string> framed;
  std::size_t payload_len = 0;
  std::size_t body_len = 0;
};

FramedPacket BuildFramedPacket(lawnmower::MessageType type,
                               const google::protobuf::Message& message) {
  lawnmower::Packet packet;
  packet.set_msg_type(type);
  const std::string payload = message.SerializeAsString();
  packet.set_payload(payload);
  const std::string body = packet.SerializeAsString();
  const uint32_t net_len = htonl(static_cast<uint32_t>(body.size()));
  std::string framed;
  framed.resize(sizeof(net_len) + body.size());
  std::memcpy(framed.data(), &net_len, sizeof(net_len));
  std::memcpy(framed.data() + sizeof(net_len), body.data(), body.size());
  return {type, std::make_shared<std::string>(std::move(framed)),
          payload.size(), body.size()};
}

void SendFramedToSessions(std::span<const std::weak_ptr<TcpSession>> sessions,
                          const FramedPacket& packet) {
  for (const auto& weak_session : sessions) {
    if (auto session = weak_session.lock()) {
      session->SendFramedPacket(packet.framed, packet.type, packet.payload_len,
                                packet.body_len);
    }
  }
}

void SendSyncToSessions(std::span<const std::weak_ptr<TcpSession>> sessions,
                        const lawnmower::S2C_GameStateSync& sync) {
  const auto packet =
      BuildFramedPacket(lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC, sync);
  SendFramedToSessions(sessions, packet);
}

void SendDeltaToSessions(std::span<const std::weak_ptr<TcpSession>> sessions,
                         const lawnmower::S2C_GameStateDeltaSync& sync) {
  const auto packet = BuildFramedPacket(
      lawnmower::MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC, sync);
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

struct RoomSessionCache {
  uint32_t room_id = 0;
  bool ready = false;
  std::vector<std::weak_ptr<TcpSession>> sessions;

  const std::vector<std::weak_ptr<TcpSession>>& Get() {
    if (!ready) {
      sessions = RoomManager::Instance().GetRoomSessions(room_id);
      ready = true;
    }
    return sessions;
  }
};

void SendDeltaSyncWithFallback(uint32_t room_id, UdpServer* udp_server,
                               const lawnmower::S2C_GameStateDeltaSync& delta,
                               RoomSessionCache* cache) {
  if (cache == nullptr) {
    return;
  }

  bool delta_sent_udp = false;
  if (udp_server != nullptr) {
    delta_sent_udp = udp_server->BroadcastDeltaState(room_id, delta) > 0;
  }
  if (delta_sent_udp) {
    return;
  }

  const auto& targets = cache->Get();
  if (!targets.empty()) {
    SendDeltaToSessions(targets, delta);
    return;
  }
  spdlog::debug("房间 {} 无可用会话，跳过 TCP 增量同步兜底", room_id);
}

void SendSyncWithFallback(uint32_t room_id, UdpServer* udp_server,
                          bool force_full_sync, bool has_delta_payload,
                          const lawnmower::S2C_GameStateSync& sync,
                          RoomSessionCache* cache) {
  if (cache == nullptr) {
    return;
  }

  bool sync_sent_udp = false;
  // Full sync 往往包含完整敌人列表，UDP 易发生分片丢包；优先走 TCP 兜底快照。
  // 若已发送增量，同一 tick 不再走 UDP，避免客户端判重丢包。
  const bool allow_udp_sync = !force_full_sync && !has_delta_payload;
  if (allow_udp_sync && udp_server != nullptr) {
    sync_sent_udp = udp_server->BroadcastState(room_id, sync) > 0;
  }
  if (sync_sent_udp) {
    return;
  }

  const auto& targets = cache->Get();
  if (!targets.empty()) {
    SendSyncToSessions(targets, sync);
    return;
  }
  spdlog::debug("房间 {} 无可用会话，跳过 TCP 同步兜底", room_id);
}
}  // namespace

namespace game_manager_sync_dispatch {

void DispatchStateSyncPayloads(uint32_t room_id, UdpServer* udp_server,
                               bool force_full_sync, bool built_sync,
                               bool built_delta,
                               const lawnmower::S2C_GameStateSync& sync,
                               const lawnmower::S2C_GameStateDeltaSync& delta) {
  const bool has_sync_payload = HasSyncPayload(built_sync, sync);
  const bool has_delta_payload = HasDeltaPayload(built_delta, delta);
  if (!has_sync_payload && !has_delta_payload) {
    return;
  }

  RoomSessionCache session_cache{.room_id = room_id};
  // 优先尝试 UDP 发送增量；若无 UDP 则走 TCP 兜底。
  if (has_delta_payload) {
    SendDeltaSyncWithFallback(room_id, udp_server, delta, &session_cache);
  }
  if (has_sync_payload) {
    SendSyncWithFallback(room_id, udp_server, force_full_sync,
                         has_delta_payload, sync, &session_cache);
  }
}

}  // namespace game_manager_sync_dispatch
