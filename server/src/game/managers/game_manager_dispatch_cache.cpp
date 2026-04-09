#include "game/managers/internal/game_manager_dispatch_cache.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <cstring>
#include <mutex>
#include <unordered_map>

#include "network/shared/packet_object_pool.hpp"
#include "network/shared/shared_string_pool.hpp"

namespace game_manager_dispatch_cache {
namespace {

struct CacheSlotKey {
  CacheFamily family = CacheFamily::kTickEvents;
  uint32_t slot = 0;
  lawnmower::MessageType type = lawnmower::MessageType::MSG_UNKNOWN;
  PreparedTransport transport = PreparedTransport::kTcpFramed;

  bool operator==(const CacheSlotKey& other) const = default;
};

struct CacheSlotKeyHash {
  std::size_t operator()(const CacheSlotKey& key) const {
    const auto family = static_cast<std::size_t>(key.family);
    const auto slot = static_cast<std::size_t>(key.slot);
    const auto type = static_cast<std::size_t>(key.type);
    const auto transport = static_cast<std::size_t>(key.transport);
    return (family * 1315423911u) ^ (slot * 2654435761u) ^
           (type * 2246822519u) ^ transport;
  }
};

struct RoomDispatchCache {
  uint64_t dispatch_tick = 0;
  bool has_dispatch_tick = false;
  std::unordered_map<CacheSlotKey, PreparedPacketBytes, CacheSlotKeyHash> packets;
};

std::mutex& GlobalCacheMutex() {
  static auto* mutex = new std::mutex();
  return *mutex;
}

std::unordered_map<uint32_t, RoomDispatchCache>& GlobalCaches() {
  static auto* caches = new std::unordered_map<uint32_t, RoomDispatchCache>();
  return *caches;
}

PreparedPacketBytes BuildPreparedPacketBytes(
    lawnmower::MessageType type, PreparedTransport transport,
    const google::protobuf::Message& message) {
  auto packet = network_shared::AcquireReusablePacket();
  packet->set_msg_type(type);

  const auto payload_len = static_cast<std::size_t>(message.ByteSizeLong());
  auto* payload = packet->mutable_payload();
  payload->resize(payload_len);
  if (payload_len > 0 &&
      !message.SerializeToArray(payload->data(), static_cast<int>(payload_len))) {
    return {type, transport, nullptr, 0, 0};
  }

  const auto body_len = static_cast<std::size_t>(packet->ByteSizeLong());
  if (transport == PreparedTransport::kUdpPacket) {
    auto bytes = network_shared::AcquireSharedString(body_len);
    bytes->resize(body_len);
    if (body_len > 0 &&
        !packet->SerializeToArray(bytes->data(), static_cast<int>(body_len))) {
      return {type, transport, nullptr, 0, 0};
    }
    return {type, transport, bytes, payload_len, body_len};
  }

  const uint32_t net_len = htonl(static_cast<uint32_t>(body_len));
  auto bytes = network_shared::AcquireSharedString(sizeof(net_len) + body_len);
  bytes->resize(sizeof(net_len) + body_len);
  std::memcpy(bytes->data(), &net_len, sizeof(net_len));
  if (body_len > 0 &&
      !packet->SerializeToArray(bytes->data() + sizeof(net_len),
                                static_cast<int>(body_len))) {
    return {type, transport, nullptr, 0, 0};
  }
  return {type, transport, bytes, payload_len, body_len};
}

}  // namespace

PreparedPacketBytes MaterializePacketBytes(
    lawnmower::MessageType type, PreparedTransport transport,
    const google::protobuf::Message& message) {
  return BuildPreparedPacketBytes(type, transport, message);
}

PreparedPacketBytes MaterializeOrReusePacket(
    uint32_t room_id, uint64_t dispatch_tick, CacheFamily family,
    uint32_t slot, lawnmower::MessageType type, PreparedTransport transport,
    const google::protobuf::Message& message) {
  const CacheSlotKey key{family, slot, type, transport};
  {
    std::lock_guard<std::mutex> lock(GlobalCacheMutex());
    auto& room_cache = GlobalCaches()[room_id];
    if (room_cache.has_dispatch_tick && room_cache.dispatch_tick != dispatch_tick) {
      room_cache.packets.clear();
      room_cache.dispatch_tick = dispatch_tick;
    } else if (!room_cache.has_dispatch_tick) {
      room_cache.dispatch_tick = dispatch_tick;
      room_cache.has_dispatch_tick = true;
    }

    const auto it = room_cache.packets.find(key);
    if (it != room_cache.packets.end()) {
      return it->second;
    }
  }

  const auto prepared = BuildPreparedPacketBytes(type, transport, message);
  if (prepared.bytes == nullptr) {
    return prepared;
  }

  std::lock_guard<std::mutex> lock(GlobalCacheMutex());
  auto& room_cache = GlobalCaches()[room_id];
  if (room_cache.has_dispatch_tick && room_cache.dispatch_tick != dispatch_tick) {
    room_cache.packets.clear();
    room_cache.dispatch_tick = dispatch_tick;
  } else if (!room_cache.has_dispatch_tick) {
    room_cache.dispatch_tick = dispatch_tick;
    room_cache.has_dispatch_tick = true;
  }

  auto [it, inserted] = room_cache.packets.emplace(key, prepared);
  return inserted ? prepared : it->second;
}

PreparedPacketBytes CachePreparedPacket(uint32_t room_id, uint64_t dispatch_tick,
                                        CacheFamily family, uint32_t slot,
                                        const PreparedPacketBytes& packet) {
  if (packet.bytes == nullptr) {
    return packet;
  }

  const CacheSlotKey key{family, slot, packet.type, packet.transport};
  std::lock_guard<std::mutex> lock(GlobalCacheMutex());
  auto& room_cache = GlobalCaches()[room_id];
  if (room_cache.has_dispatch_tick && room_cache.dispatch_tick != dispatch_tick) {
    room_cache.packets.clear();
    room_cache.dispatch_tick = dispatch_tick;
  } else if (!room_cache.has_dispatch_tick) {
    room_cache.dispatch_tick = dispatch_tick;
    room_cache.has_dispatch_tick = true;
  }

  auto [it, inserted] = room_cache.packets.emplace(key, packet);
  return inserted ? packet : it->second;
}

void ClearRoomDispatchCache(uint32_t room_id) {
  std::lock_guard<std::mutex> lock(GlobalCacheMutex());
  GlobalCaches().erase(room_id);
}

void ClearAllDispatchCachesForTests() {
  std::lock_guard<std::mutex> lock(GlobalCacheMutex());
  GlobalCaches().clear();
}

}  // namespace game_manager_dispatch_cache
