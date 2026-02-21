#pragma once

#include <chrono>
#include <cstdint>

#include "message.pb.h"

namespace game_manager_internal {

// 统一获取服务器毫秒时间戳（steady_clock）
std::chrono::milliseconds NowMs();

// 统一填充低频/全量同步时间字段
void FillSyncTiming(uint32_t room_id, uint64_t tick,
                    lawnmower::S2C_GameStateSync* sync);

// 统一填充高频 delta 同步时间字段
void FillDeltaTiming(uint32_t room_id, uint64_t tick,
                     lawnmower::S2C_GameStateDeltaSync* sync);

}  // namespace game_manager_internal
