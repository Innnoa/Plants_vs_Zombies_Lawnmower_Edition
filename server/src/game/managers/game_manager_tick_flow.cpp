#include <algorithm>
#include <chrono>
#include <cmath>
#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "internal/game_manager_event_dispatch.hpp"
#include "internal/game_manager_misc_utils.hpp"
#include "internal/game_manager_sync_dispatch.hpp"

namespace {
constexpr float kDirectionEpsilonSq =
    1e-6f;  // 方向向量长度平方的极小阈值，小于此视为无效输入
constexpr float kMaxDirectionLengthSq = 1.21f;  // 方向向量长度平方的上限
constexpr double kMaxTickDeltaSeconds = 0.1;    // clamp 极端卡顿
constexpr double kMaxInputDeltaSeconds = 0.1;
}  // namespace

void GameManager::ConsumePlayerInputQueueLocked(const SceneConfig& scene_config,
                                                PlayerRuntime* runtime,
                                                double tick_interval_seconds,
                                                bool* moved,
                                                bool* consumed_input) const {
  if (runtime == nullptr || moved == nullptr || consumed_input == nullptr) {
    return;
  }

  double processed_seconds = 0.0;
  while (!runtime->pending_inputs.empty() &&
         processed_seconds < kMaxTickDeltaSeconds) {
    auto& input = runtime->pending_inputs.front();
    const float dx_raw = input.move_direction().x();
    const float dy_raw = input.move_direction().y();
    const float len_sq = dx_raw * dx_raw + dy_raw * dy_raw;

    const double reported_dt =
        input.delta_ms() > 0
            ? std::clamp(input.delta_ms() / 1000.0, 0.0, kMaxInputDeltaSeconds)
            : tick_interval_seconds;
    const double remaining_budget = kMaxTickDeltaSeconds - processed_seconds;
    const double input_dt = std::min(reported_dt, remaining_budget);

    const bool can_move = runtime->state.is_alive();
    if (len_sq >= kDirectionEpsilonSq && len_sq <= kMaxDirectionLengthSq &&
        input_dt > 0.0 && can_move) {
      const float len = std::sqrt(len_sq);
      const float dx = dx_raw / len;
      const float dy = dy_raw / len;

      const float speed = runtime->state.move_speed() > 0.0f
                              ? runtime->state.move_speed()
                              : scene_config.move_speed;

      auto* position = runtime->state.mutable_position();
      const auto new_pos =
          ClampToMap(scene_config,
                     position->x() + dx * speed * static_cast<float>(input_dt),
                     position->y() + dy * speed * static_cast<float>(input_dt));
      const float new_x = new_pos.x();
      const float new_y = new_pos.y();

      if (std::abs(new_x - position->x()) > 1e-4f ||
          std::abs(new_y - position->y()) > 1e-4f) {
        *moved = true;
      }

      position->set_x(new_x);
      position->set_y(new_y);
      runtime->state.set_rotation(
          game_manager_misc_utils::DegreesFromDirection(dx, dy));
      processed_seconds += input_dt;
      *consumed_input = true;
    } else {
      // 无效方向也要前进时间，防止队列阻塞
      processed_seconds += input_dt;
      *consumed_input = true;
    }

    // 更新序号（即便被拆分）
    if (input.input_seq() > runtime->last_input_seq) {
      runtime->last_input_seq = input.input_seq();
    }

    const double remaining_dt = reported_dt - input_dt;
    if (remaining_dt > 1e-5) {
      // 当前 tick 只消耗了一部分，保留剩余 delta_ms 在队首
      const uint32_t remaining_ms = static_cast<uint32_t>(
          std::clamp(std::llround(remaining_dt * 1000.0), 1LL,
                     static_cast<long long>(kMaxInputDeltaSeconds * 1000.0)));
      input.set_delta_ms(remaining_ms);
      break;
    }
    runtime->pending_inputs.pop_front();
  }
}

void GameManager::ProcessPlayerInputsLocked(Scene& scene,
                                            double tick_interval_seconds,
                                            double dt_seconds,
                                            bool* has_dirty) {
  if (has_dirty == nullptr) {
    return;
  }

  for (auto& [_, runtime] : scene.players) {
    runtime.attack_cooldown_seconds -= dt_seconds;
    if (!runtime.is_connected) {
      runtime.pending_inputs.clear();
      runtime.wants_attacking = false;
      runtime.has_attack_dir = false;
      continue;
    }
    bool moved = false;
    bool consumed_input = false;
    ConsumePlayerInputQueueLocked(scene.config, &runtime, tick_interval_seconds,
                                  &moved, &consumed_input);

    if (moved || consumed_input || runtime.low_freq_dirty) {
      MarkPlayerDirty(scene, runtime.state.player_id(), runtime, false);
      *has_dirty = true;
    }
  }
}

void GameManager::ReserveTickEventBuffersLocked(
    const Scene& scene, std::vector<lawnmower::S2C_PlayerHurt>* player_hurts,
    std::vector<lawnmower::S2C_EnemyDied>* enemy_dieds,
    std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
    std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
    std::vector<lawnmower::ProjectileState>* projectile_spawns,
    std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
    std::vector<lawnmower::ItemState>* dropped_items) const {
  if (player_hurts == nullptr || enemy_dieds == nullptr ||
      enemy_attack_states == nullptr || level_ups == nullptr ||
      projectile_spawns == nullptr || projectile_despawns == nullptr ||
      dropped_items == nullptr) {
    return;
  }

  if (!scene.players.empty()) {
    player_hurts->reserve(scene.players.size());
    level_ups->reserve(scene.players.size());
  }
  if (!scene.enemies.empty()) {
    enemy_dieds->reserve(scene.enemies.size());
    enemy_attack_states->reserve(scene.enemies.size());
  }
  if (!scene.projectiles.empty()) {
    projectile_spawns->reserve(scene.projectiles.size());
    projectile_despawns->reserve(scene.projectiles.size());
  }
  if (!scene.items.empty()) {
    dropped_items->reserve(scene.items.size());
  }
}

bool GameManager::TryBeginPendingUpgradeLocked(
    uint32_t room_id, Scene& scene,
    std::optional<lawnmower::S2C_UpgradeRequest>* upgrade_request) {
  if (upgrade_request == nullptr ||
      scene.upgrade_stage != UpgradeStage::kNone) {
    return false;
  }

  uint32_t candidate_player_id = 0;
  for (const auto& [player_id, runtime] : scene.players) {
    if (runtime.pending_upgrade_count > 0) {
      candidate_player_id = player_id;
      break;
    }
  }
  if (candidate_player_id == 0) {
    return false;
  }

  lawnmower::S2C_UpgradeRequest request;
  if (!BeginUpgradeLocked(room_id, scene, candidate_player_id,
                          lawnmower::UPGRADE_REASON_LEVEL_UP, &request)) {
    return false;
  }
  *upgrade_request = request;
  return true;
}

void GameManager::CaptureGameOverPerfLocked(
    Scene& scene, const std::optional<lawnmower::S2C_GameOver>& game_over,
    std::optional<PerfStats>* perf_to_save, uint32_t* perf_tick_rate,
    uint32_t* perf_sync_rate, double* perf_elapsed_seconds) {
  if (!game_over.has_value() || perf_to_save == nullptr ||
      perf_tick_rate == nullptr || perf_sync_rate == nullptr ||
      perf_elapsed_seconds == nullptr) {
    return;
  }

  scene.perf.end_time = std::chrono::system_clock::now();
  *perf_tick_rate = scene.config.tick_rate;
  *perf_sync_rate = scene.config.state_sync_rate;
  *perf_elapsed_seconds = scene.elapsed;
  *perf_to_save = std::move(scene.perf);
}

double GameManager::ComputeTickDeltaSecondsLocked(
    Scene& scene, double tick_interval_seconds) const {
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = scene.last_tick_time.time_since_epoch().count() == 0
                           ? scene.tick_interval
                           : now - scene.last_tick_time;
  scene.last_tick_time = now;
  const double elapsed_seconds =
      std::clamp(std::chrono::duration<double>(elapsed).count(), 0.0,
                 kMaxTickDeltaSeconds);
  return elapsed_seconds > 0.0 ? elapsed_seconds : tick_interval_seconds;
}

void GameManager::SimulateSceneFrameLocked(Scene& scene,
                                           const TickFrameContext& frame,
                                           TickOutputs* outputs,
                                           TickDirtyState* dirty_state) {
  if (outputs == nullptr || dirty_state == nullptr) {
    return;
  }

  bool has_dirty = false;
  ProcessPlayerInputsLocked(scene, frame.tick_interval_seconds,
                            frame.dt_seconds, &has_dirty);

  scene.elapsed += frame.dt_seconds;
  ProcessEnemies(scene, frame.dt_seconds, &has_dirty);
  ProcessItems(scene, &has_dirty);

  ProcessCombatAndProjectiles(
      scene, frame.dt_seconds, &outputs->player_hurts, &outputs->enemy_dieds,
      &outputs->enemy_attack_states, &outputs->level_ups, &outputs->game_over,
      &outputs->projectile_spawns, &outputs->projectile_despawns,
      &outputs->dropped_items, &has_dirty);
  TryBeginPendingUpgradeLocked(frame.room_id, scene, &outputs->upgrade_request);
  RecordPlayerHistoryLocked(scene);

  dirty_state->has_dirty_players = !scene.dirty_player_ids.empty();
  dirty_state->has_dirty_enemies = !scene.dirty_enemy_ids.empty();
  dirty_state->has_dirty_items = !scene.dirty_item_ids.empty();
}

void GameManager::BuildSceneSyncAndPerfLocked(Scene& scene,
                                              const TickFrameContext& frame,
                                              const TickDirtyState& dirty_state,
                                              TickOutputs* outputs) {
  if (outputs == nullptr) {
    return;
  }

  const bool has_dirty = dirty_state.has_dirty_players ||
                         dirty_state.has_dirty_enemies ||
                         dirty_state.has_dirty_items;
  const bool has_priority_events = HasPriorityEventsInTick(
      outputs->projectile_spawns, outputs->projectile_despawns,
      outputs->dropped_items, outputs->player_hurts,
      outputs->enemy_attack_states, outputs->enemy_dieds, outputs->level_ups,
      outputs->game_over, outputs->upgrade_request);
  UpdateSyncSchedulingLocked(
      scene, frame.dt_seconds, frame.tick_interval_seconds, has_priority_events,
      dirty_state.has_dirty_players, dirty_state.has_dirty_enemies,
      dirty_state.has_dirty_items, &outputs->should_sync,
      &outputs->force_full_sync);

  const bool want_sync = outputs->should_sync || outputs->force_full_sync;
  const bool need_sync = want_sync && (outputs->force_full_sync || has_dirty);
  if (need_sync) {
    BuildSyncPayloadsLocked(
        frame.room_id, scene, outputs->force_full_sync, scene.dirty_player_ids,
        scene.dirty_enemy_ids, scene.dirty_item_ids, &outputs->sync,
        &outputs->delta, &outputs->built_sync, &outputs->built_delta,
        &outputs->perf_delta_items_size, &outputs->perf_sync_items_size);
  }

  const auto perf_end = std::chrono::steady_clock::now();
  const double perf_ms =
      std::chrono::duration<double, std::milli>(perf_end - frame.perf_start)
          .count();
  RecordPerfSampleLocked(scene, perf_ms, frame.dt_seconds, false,
                         static_cast<uint32_t>(scene.dirty_player_ids.size()),
                         static_cast<uint32_t>(scene.dirty_enemy_ids.size()),
                         static_cast<uint32_t>(scene.dirty_item_ids.size()),
                         outputs->perf_delta_items_size,
                         outputs->perf_sync_items_size);

  CaptureGameOverPerfLocked(scene, outputs->game_over, &outputs->perf_to_save,
                            &outputs->perf_tick_rate, &outputs->perf_sync_rate,
                            &outputs->perf_elapsed_seconds);
  outputs->event_wave_id = scene.wave_id;
  outputs->event_tick = scene.tick;

  MaybeLogItemSyncSnapshotLocked(
      frame.room_id, scene, outputs->dropped_items.size(), outputs->built_sync,
      outputs->built_delta, outputs->perf_delta_items_size,
      outputs->perf_sync_items_size);
}

void GameManager::ProcessActiveSceneTickLocked(Scene& scene,
                                               const TickFrameContext& frame,
                                               TickOutputs* outputs) {
  if (outputs == nullptr) {
    return;
  }

  TickDirtyState dirty_state;
  SimulateSceneFrameLocked(scene, frame, outputs, &dirty_state);
  BuildSceneSyncAndPerfLocked(scene, frame, dirty_state, outputs);
}

void GameManager::FinalizeSceneTick(
    uint32_t room_id, const std::vector<uint32_t>& expired_players,
    bool paused_only,
    std::vector<lawnmower::ProjectileState>* projectile_spawns,
    std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
    const std::vector<lawnmower::ItemState>& dropped_items,
    const std::vector<lawnmower::EnemyAttackStateDelta>& enemy_attack_states,
    const std::vector<lawnmower::S2C_PlayerHurt>& player_hurts,
    const std::vector<lawnmower::S2C_EnemyDied>& enemy_dieds,
    const std::vector<lawnmower::S2C_PlayerLevelUp>& level_ups,
    const std::optional<lawnmower::S2C_GameOver>& game_over,
    const std::optional<lawnmower::S2C_UpgradeRequest>& upgrade_request,
    std::optional<PerfStats>* perf_to_save, uint32_t perf_tick_rate,
    uint32_t perf_sync_rate, double perf_elapsed_seconds, uint64_t event_tick,
    uint32_t event_wave_id, bool force_full_sync, bool built_sync,
    bool built_delta, const lawnmower::S2C_GameStateSync& sync,
    const lawnmower::S2C_GameStateDeltaSync& delta) {
  if (projectile_spawns == nullptr || projectile_despawns == nullptr ||
      perf_to_save == nullptr) {
    return;
  }

  CleanupExpiredPlayers(expired_players);

  if (paused_only) {
    return;
  }

  game_manager_misc_utils::DedupProjectileSpawns(projectile_spawns);
  game_manager_misc_utils::DedupProjectileDespawns(projectile_despawns);

  game_manager_event_dispatch::DispatchTickEvents(
      room_id, event_tick, event_wave_id, *projectile_spawns,
      *projectile_despawns, dropped_items, enemy_attack_states, player_hurts,
      enemy_dieds, level_ups, game_over, upgrade_request);

  if (game_over.has_value()) {
    // 等 GameOver 消息发送完再重置房间状态，避免客户端被 ROOM_UPDATE 提前切屏。
    if (!RoomManager::Instance().FinishGame(room_id)) {
      spdlog::warn("房间 {} 未找到，无法重置游戏状态", room_id);
    }
  }

  if (perf_to_save->has_value()) {
    SavePerfStatsToFile(room_id, **perf_to_save, perf_tick_rate, perf_sync_rate,
                        perf_elapsed_seconds);
  }

  game_manager_sync_dispatch::DispatchStateSyncPayloads(
      room_id, udp_server_, force_full_sync, built_sync, built_delta, sync,
      delta);
}

// 进程场景计时器
void GameManager::ProcessSceneTick(uint32_t room_id,
                                   double tick_interval_seconds) {
  TickFrameContext frame;
  frame.room_id = room_id;
  frame.tick_interval_seconds = tick_interval_seconds;
  TickOutputs outputs;
  std::vector<uint32_t> expired_players;
  bool paused_only = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      return;
    }

    Scene& scene = scene_it->second;
    if (scene.game_over) {
      return;
    }
    ReserveTickEventBuffersLocked(
        scene, &outputs.player_hurts, &outputs.enemy_dieds,
        &outputs.enemy_attack_states, &outputs.level_ups,
        &outputs.projectile_spawns, &outputs.projectile_despawns,
        &outputs.dropped_items);
    if (!scene.players.empty()) {
      expired_players.reserve(scene.players.size());
    }
    frame.perf_start = std::chrono::steady_clock::now();
    frame.dt_seconds =
        ComputeTickDeltaSecondsLocked(scene, tick_interval_seconds);

    const double grace_seconds =
        std::max(0.0, static_cast<double>(config_.reconnect_grace_seconds));
    CollectExpiredPlayersLocked(scene, grace_seconds, &expired_players);

    if (HandlePausedTickLocked(scene, frame.dt_seconds, frame.perf_start)) {
      paused_only = true;
    } else {
      ProcessActiveSceneTickLocked(scene, frame, &outputs);
    }
  }

  FinalizeSceneTick(
      room_id, expired_players, paused_only, &outputs.projectile_spawns,
      &outputs.projectile_despawns, outputs.dropped_items,
      outputs.enemy_attack_states, outputs.player_hurts, outputs.enemy_dieds,
      outputs.level_ups, outputs.game_over, outputs.upgrade_request,
      &outputs.perf_to_save, outputs.perf_tick_rate, outputs.perf_sync_rate,
      outputs.perf_elapsed_seconds, outputs.event_tick, outputs.event_wave_id,
      outputs.force_full_sync, outputs.built_sync, outputs.built_delta,
      outputs.sync, outputs.delta);
}
