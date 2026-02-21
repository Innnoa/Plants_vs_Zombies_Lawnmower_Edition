#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"

namespace {
constexpr uint32_t kFullSyncIntervalTicks = 180;  // 全量同步时间间隔
constexpr uint64_t kItemLogIntervalSeconds = 2;   // 道具日志输出间隔
}  // namespace

bool GameManager::HasPriorityEventsInTick(
    const std::vector<lawnmower::ProjectileState>& projectile_spawns,
    const std::vector<lawnmower::ProjectileDespawn>& projectile_despawns,
    const std::vector<lawnmower::ItemState>& dropped_items,
    const std::vector<lawnmower::S2C_PlayerHurt>& player_hurts,
    const std::vector<lawnmower::EnemyAttackStateDelta>& enemy_attack_states,
    const std::vector<lawnmower::S2C_EnemyDied>& enemy_dieds,
    const std::vector<lawnmower::S2C_PlayerLevelUp>& level_ups,
    const std::optional<lawnmower::S2C_GameOver>& game_over,
    const std::optional<lawnmower::S2C_UpgradeRequest>& upgrade_request) {
  return !projectile_spawns.empty() || !projectile_despawns.empty() ||
         !dropped_items.empty() || !player_hurts.empty() ||
         !enemy_attack_states.empty() || !enemy_dieds.empty() ||
         !level_ups.empty() || game_over.has_value() ||
         upgrade_request.has_value();
}

void GameManager::UpdateSyncSchedulingLocked(
    Scene& scene, double dt_seconds, double tick_interval_seconds,
    bool has_priority_events, bool has_dirty_players, bool has_dirty_enemies,
    bool has_dirty_items, bool* should_sync, bool* force_full_sync) const {
  if (should_sync == nullptr || force_full_sync == nullptr) {
    return;
  }

  scene.tick += 1;
  scene.sync_accumulator += dt_seconds;
  scene.full_sync_elapsed += dt_seconds;
  const double base_sync_interval = scene.sync_interval.count() > 0.0
                                        ? scene.sync_interval.count()
                                        : tick_interval_seconds;
  const double idle_light_seconds =
      std::max(0.0f, config_.sync_idle_light_seconds);
  const double idle_heavy_seconds = std::max(
      idle_light_seconds, static_cast<double>(config_.sync_idle_heavy_seconds));
  const double scale_light = std::max(1.0f, config_.sync_scale_light);
  const double scale_medium =
      std::max(scale_light, static_cast<double>(config_.sync_scale_medium));
  const double scale_idle =
      std::max(scale_medium, static_cast<double>(config_.sync_scale_idle));
  if (has_priority_events || has_dirty_players) {
    scene.sync_idle_elapsed = 0.0;
    scene.dynamic_sync_interval =
        std::chrono::duration<double>(base_sync_interval);
  } else {
    scene.sync_idle_elapsed += dt_seconds;
    double scale = 1.0;
    if (has_dirty_enemies || has_dirty_items) {
      scale = scene.sync_idle_elapsed >= idle_light_seconds ? scale_medium
                                                            : scale_light;
    } else {
      scale = scene.sync_idle_elapsed >= idle_heavy_seconds ? scale_idle
                                                            : scale_medium;
    }
    scene.dynamic_sync_interval =
        std::chrono::duration<double>(base_sync_interval * scale);
  }

  const double sync_interval = scene.dynamic_sync_interval.count() > 0.0
                                   ? scene.dynamic_sync_interval.count()
                                   : base_sync_interval;
  while (scene.sync_accumulator >= sync_interval) {
    scene.sync_accumulator -= sync_interval;
    *should_sync = true;
  }

  const double full_sync_interval_seconds =
      scene.full_sync_interval.count() > 0.0
          ? scene.full_sync_interval.count()
          : tick_interval_seconds * static_cast<double>(kFullSyncIntervalTicks);
  *force_full_sync = full_sync_interval_seconds > 0.0 &&
                     scene.full_sync_elapsed >= full_sync_interval_seconds;
}

void GameManager::MaybeLogItemSyncSnapshotLocked(
    uint32_t room_id, Scene& scene, std::size_t dropped_events, bool built_sync,
    bool built_delta, uint32_t perf_delta_items_size,
    uint32_t perf_sync_items_size) {
  const uint64_t log_interval_ticks =
      std::max<uint64_t>(1, static_cast<uint64_t>(scene.config.tick_rate) *
                                kItemLogIntervalSeconds);
  if (scene.tick < scene.last_item_log_tick + log_interval_ticks) {
    return;
  }

  scene.last_item_log_tick = scene.tick;
  spdlog::info(
      "[item] room={} tick={} items={} dirty_items={} "
      "dropped_events={} built_sync={} built_delta={} delta_items={} "
      "sync_items={}",
      room_id, scene.tick, scene.items.size(), scene.dirty_item_ids.size(),
      dropped_events, built_sync ? "true" : "false",
      built_delta ? "true" : "false", perf_delta_items_size,
      perf_sync_items_size);
}
