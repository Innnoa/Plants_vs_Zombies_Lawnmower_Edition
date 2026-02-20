#pragma once

#include <cstdint>

#include "message.pb.h"

class UdpServer;

namespace game_manager_sync_dispatch {

void DispatchStateSyncPayloads(uint32_t room_id, UdpServer* udp_server,
                               bool force_full_sync, bool built_sync,
                               bool built_delta,
                               const lawnmower::S2C_GameStateSync& sync,
                               const lawnmower::S2C_GameStateDeltaSync& delta);

}  // namespace game_manager_sync_dispatch
