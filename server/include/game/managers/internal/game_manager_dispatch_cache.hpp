#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <google/protobuf/message.h>

#include "message.pb.h"

namespace game_manager_dispatch_cache {

enum class PreparedTransport {
  kTcpFramed = 0,
  kUdpPacket = 1,
};

enum class CacheFamily {
  kTickEvents = 0,
  kStateSync = 1,
  kStateDelta = 2,
  kTargetedFullSync = 3,
};

struct PreparedPacketBytes {
  lawnmower::MessageType type = lawnmower::MessageType::MSG_UNKNOWN;
  PreparedTransport transport = PreparedTransport::kTcpFramed;
  std::shared_ptr<const std::string> bytes;
  std::size_t payload_len = 0;
  std::size_t body_len = 0;
};

PreparedPacketBytes MaterializePacketBytes(
    lawnmower::MessageType type, PreparedTransport transport,
    const google::protobuf::Message& message);

PreparedPacketBytes MaterializeOrReusePacket(
    uint32_t room_id, uint64_t dispatch_tick, CacheFamily family,
    uint32_t slot, lawnmower::MessageType type, PreparedTransport transport,
    const google::protobuf::Message& message);

PreparedPacketBytes CachePreparedPacket(uint32_t room_id, uint64_t dispatch_tick,
                                        CacheFamily family, uint32_t slot,
                                        const PreparedPacketBytes& packet);

void ClearRoomDispatchCache(uint32_t room_id);

void ClearAllDispatchCachesForTests();

}  // namespace game_manager_dispatch_cache
