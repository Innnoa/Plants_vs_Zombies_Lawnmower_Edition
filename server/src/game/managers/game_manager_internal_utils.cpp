#include "internal/game_manager_internal_utils.hpp"

namespace game_manager_internal {

std::chrono::milliseconds NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch());
}

void FillSyncTiming(uint32_t room_id, uint64_t tick,
                    lawnmower::S2C_GameStateSync* sync) {
  if (sync == nullptr) {
    return;
  }

  const auto now_ms = NowMs();
  sync->set_room_id(room_id);
  auto* ts = sync->mutable_sync_time();
  ts->set_server_time(static_cast<uint64_t>(now_ms.count()));
  ts->set_tick(static_cast<uint32_t>(tick));
}

void FillDeltaTiming(uint32_t room_id, uint64_t tick,
                     lawnmower::S2C_GameStateDeltaSync* sync) {
  if (sync == nullptr) {
    return;
  }

  const auto now_ms = NowMs();
  sync->set_room_id(room_id);
  auto* ts = sync->mutable_sync_time();
  ts->set_server_time(static_cast<uint64_t>(now_ms.count()));
  ts->set_tick(static_cast<uint32_t>(tick));
}

}  // namespace game_manager_internal
