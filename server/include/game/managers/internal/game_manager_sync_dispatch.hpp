#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "message.pb.h"

class TcpSession;
class UdpServer;

namespace game_manager_sync_dispatch {

uint32_t EstimateStatePacketCount(bool force_full_sync, bool built_sync,
                                  bool built_delta,
                                  const lawnmower::S2C_GameStateSync& sync,
                                  const lawnmower::S2C_GameStateDeltaSync& delta);

uint32_t QueuedStatePacketCount(uint32_t room_id);

std::uintptr_t FirstDeferredStatePreparedTcpIdentity(uint32_t room_id);

std::uintptr_t FirstDeferredStatePreparedUdpIdentity(uint32_t room_id);

uint32_t FirstDeferredStateFrozenTick(uint32_t room_id);

uint64_t FirstDeferredStateFrozenServerTime(uint32_t room_id);

std::uintptr_t FirstDeferredStateTemplateIdentity(uint32_t room_id);

void SendFullSnapshotToSessions(
    const std::vector<std::weak_ptr<TcpSession>>& sessions,
    const lawnmower::S2C_GameStateSync& sync);

void SendFullSnapshotToSession(
    const std::shared_ptr<TcpSession>& session,
    const lawnmower::S2C_GameStateSync& sync);

uint32_t DispatchStateSyncPayloads(
    uint32_t room_id, uint64_t dispatch_tick, UdpServer* udp_server,
    const std::vector<std::weak_ptr<TcpSession>>& sessions, bool force_full_sync,
    bool built_sync, bool built_delta,
    const lawnmower::S2C_GameStateSync& sync,
    const lawnmower::S2C_GameStateDeltaSync& delta, uint32_t packet_budget);

void ClearRoomDispatchState(uint32_t room_id);

}  // namespace game_manager_sync_dispatch
