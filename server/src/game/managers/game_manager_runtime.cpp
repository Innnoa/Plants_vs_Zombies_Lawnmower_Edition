#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "network/tcp/tcp_session.hpp"

// 简单的伪随机数生成器, state 是随机数种子指针
uint32_t GameManager::NextRng(uint32_t* state) {
  if (state == nullptr) {
    return 0;
  }
  // 线性同余法
  // LCG: fast & deterministic for gameplay purposes.
  *state = (*state * 1664525u) + 1013904223u;
  return *state;
}

// 获取一个[0,1）的浮点随机值
float GameManager::NextRngUnitFloat(uint32_t* state) {
  const uint32_t r = NextRng(state);
  // Use high 24 bits to build [0,1) float.
  // 取r的高24位，再乘 2e-24 即可把整数映射为浮点数
  return static_cast<float>((r >> 8) & 0x00FFFFFF) * (1.0f / 16777216.0f);
}

std::size_t GameManager::GetPredictionHistoryLimit(const Scene& scene) const {
  double tick_interval = scene.tick_interval.count();
  if (tick_interval <= 0.0) {
    tick_interval = config_.tick_rate > 0
                        ? 1.0 / static_cast<double>(config_.tick_rate)
                        : 1.0 / 60.0;
  }
  const double seconds = std::max(0.1f, config_.prediction_history_seconds);
  const std::size_t limit =
      static_cast<std::size_t>(std::ceil(seconds / tick_interval));
  return std::max<std::size_t>(1, limit);
}

bool GameManager::HasPendingInputWithSeq(const PlayerRuntime& runtime,
                                         uint32_t seq) {
  if (seq == 0) {
    return false;
  }
  return std::any_of(runtime.pending_inputs.begin(), runtime.pending_inputs.end(),
                     [seq](const lawnmower::C2S_PlayerInput& input) {
                       return input.input_seq() == seq;
                     });
}

void GameManager::InsertPendingInputOrdered(
    PlayerRuntime* runtime, const lawnmower::C2S_PlayerInput& input) {
  if (runtime == nullptr) {
    return;
  }

  const auto it = std::upper_bound(
      runtime->pending_inputs.begin(), runtime->pending_inputs.end(), input,
      [](const lawnmower::C2S_PlayerInput& left,
         const lawnmower::C2S_PlayerInput& right) {
        return left.input_seq() < right.input_seq();
      });
  runtime->pending_inputs.insert(it, input);
}

void GameManager::AdvanceContiguousInputSeq(PlayerRuntime* runtime) {
  if (runtime == nullptr) {
    return;
  }

  while (true) {
    const uint32_t next_seq = runtime->last_contiguous_input_seq + 1;
    auto it = runtime->processed_input_seq_window.find(next_seq);
    if (it == runtime->processed_input_seq_window.end()) {
      break;
    }
    runtime->processed_input_seq_window.erase(it);
    runtime->last_contiguous_input_seq = next_seq;
  }
  runtime->state.set_last_processed_input_seq(runtime->last_contiguous_input_seq);
}

void GameManager::RecordProcessedInputSegment(
    PlayerRuntime* runtime, const lawnmower::C2S_PlayerInput& input,
    uint64_t applied_tick, double delta_seconds, float move_direction_x,
    float move_direction_y, float rotation) {
  if (runtime == nullptr || input.input_seq() == 0 || delta_seconds <= 0.0) {
    return;
  }

  PlayerRuntime::ProcessedInputSegment segment;
  segment.input_seq = input.input_seq();
  segment.input_tick = input.has_input_time() ? input.input_time().tick() : 0u;
  segment.applied_tick = applied_tick;
  segment.delta_seconds = static_cast<float>(delta_seconds);
  segment.move_direction_x = move_direction_x;
  segment.move_direction_y = move_direction_y;
  segment.rotation = rotation;
  runtime->processed_input_segments.push_back(segment);
}

void GameManager::PushInitialPlayerHistory(PlayerRuntime* runtime, uint64_t tick) {
  if (runtime == nullptr) {
    return;
  }

  PlayerRuntime::HistoryEntry entry;
  entry.tick = tick;
  entry.position = runtime->state.position();
  entry.rotation = runtime->state.rotation();
  entry.health = runtime->state.health();
  entry.is_alive = runtime->state.is_alive();
  entry.last_processed_input_seq = runtime->last_contiguous_input_seq;
  runtime->history.push_back(entry);
}

void GameManager::TrimPredictionHistoryLocked(const Scene& scene,
                                             PlayerRuntime* runtime) {
  if (runtime == nullptr) {
    return;
  }

  const std::size_t limit = GetPredictionHistoryLimit(scene);
  while (runtime->history.size() > (limit + 1)) {
    runtime->history.pop_front();
  }

  const uint64_t min_tick = scene.tick > limit
                                ? (scene.tick - static_cast<uint64_t>(limit))
                                : 0u;
  while (!runtime->processed_input_segments.empty() &&
         runtime->processed_input_segments.front().applied_tick < min_tick) {
    runtime->processed_input_segments.pop_front();
  }
}

void GameManager::RecordPlayerHistoryLocked(Scene& scene) {
  for (auto& [_, runtime] : scene.players) {
    PlayerRuntime::HistoryEntry entry;
    entry.tick = scene.tick;
    entry.position = runtime.state.position();
    entry.rotation = runtime.state.rotation();
    entry.health = runtime.state.health();
    entry.is_alive = runtime.state.is_alive();
    entry.last_processed_input_seq = runtime.last_contiguous_input_seq;
    runtime.history.push_back(entry);
    TrimPredictionHistoryLocked(scene, &runtime);
  }
}

bool GameManager::ReconcilePlayerPredictionLocked(const Scene& scene,
                                                 uint32_t player_id,
                                                 PlayerRuntime* runtime) {
  if (runtime == nullptr || !runtime->needs_prediction_reconciliation) {
    return false;
  }

  const uint32_t from_seq = runtime->reconcile_from_input_seq;
  runtime->needs_prediction_reconciliation = false;
  runtime->reconcile_from_input_seq = 0;
  if (from_seq == 0) {
    return false;
  }

  uint64_t earliest_applied_tick = std::numeric_limits<uint64_t>::max();
  std::vector<PlayerRuntime::ProcessedInputSegment> replay_segments;
  replay_segments.reserve(runtime->processed_input_segments.size());
  for (const auto& segment : runtime->processed_input_segments) {
    if (segment.input_seq < from_seq) {
      continue;
    }
    replay_segments.push_back(segment);
    earliest_applied_tick =
        std::min(earliest_applied_tick, segment.applied_tick);
  }

  if (replay_segments.empty()) {
    spdlog::debug("player {} 重放纠偏跳过：无可回放片段 from_seq={}",
                  player_id, from_seq);
    return false;
  }

  const PlayerRuntime::HistoryEntry* base_snapshot = nullptr;
  for (auto it = runtime->history.rbegin(); it != runtime->history.rend(); ++it) {
    if (it->tick < earliest_applied_tick) {
      base_snapshot = &(*it);
      break;
    }
  }
  if (base_snapshot == nullptr) {
    spdlog::debug("player {} 重放纠偏跳过：无可用基线快照 earliest_tick={}",
                  player_id, earliest_applied_tick);
    return false;
  }

  std::sort(replay_segments.begin(), replay_segments.end(),
            [](const PlayerRuntime::ProcessedInputSegment& left,
               const PlayerRuntime::ProcessedInputSegment& right) {
              if (left.input_seq != right.input_seq) {
                return left.input_seq < right.input_seq;
              }
              return left.applied_tick < right.applied_tick;
            });

  lawnmower::Vector2 replay_position = base_snapshot->position;
  float replay_rotation = base_snapshot->rotation;
  const float move_speed = runtime->state.move_speed() > 0.0f
                               ? runtime->state.move_speed()
                               : scene.config.move_speed;
  for (const auto& segment : replay_segments) {
    const auto next_pos = ClampToMap(
        scene.config,
        replay_position.x() + segment.move_direction_x * move_speed *
                                  segment.delta_seconds,
        replay_position.y() + segment.move_direction_y * move_speed *
                                  segment.delta_seconds);
    replay_position = next_pos;
    replay_rotation = segment.rotation;
  }

  const bool changed =
      PositionChanged(replay_position, runtime->state.position()) ||
      std::abs(replay_rotation - runtime->state.rotation()) > 1e-4f;
  if (!changed) {
    return false;
  }

  SetPlayerPositionAndRotation(*runtime, replay_position, replay_rotation,
                               scene.tick);
  spdlog::debug(
      "player {} 执行迟到输入重放纠偏 from_seq={} base_tick={} replay_count={}",
      player_id, from_seq, base_snapshot->tick, replay_segments.size());
  return true;
}

void GameManager::ReconcilePlayerPredictionsLocked(Scene& scene,
                                                   bool* has_dirty) {
  if (has_dirty == nullptr) {
    return;
  }

  for (auto& [player_id, runtime] : scene.players) {
    if (!runtime.needs_prediction_reconciliation) {
      continue;
    }
    ReconcilePlayerPredictionLocked(scene, player_id, &runtime);
    MarkPlayerDirty(scene, player_id, runtime, true);
    *has_dirty = true;
  }
}

void GameManager::CollectExpiredPlayersLocked(
    const Scene& scene, double grace_seconds,
    std::vector<uint32_t>* out) const {
  if (out == nullptr) {
    return;
  }
  if (grace_seconds < 0.0) {
    return;
  }
  const auto now_steady = std::chrono::steady_clock::now();
  for (const auto& [player_id, runtime] : scene.players) {
    if (!runtime.is_connected) {
      const double disconnected_seconds =
          std::chrono::duration<double>(now_steady - runtime.disconnected_at)
              .count();
      if (disconnected_seconds >= grace_seconds) {
        out->push_back(player_id);
      }
    }
  }
}

bool GameManager::HandlePausedTickLocked(
    Scene& scene, double dt_seconds,
    const std::chrono::steady_clock::time_point& perf_start) {
  if (!scene.is_paused) {
    return false;
  }
  scene.tick += 1;
  const auto perf_end = std::chrono::steady_clock::now();
  const double perf_ms =
      std::chrono::duration<double, std::milli>(perf_end - perf_start).count();
  RecordPerfSampleLocked(scene, perf_ms, dt_seconds, true,
                         static_cast<uint32_t>(scene.dirty_player_ids.size()),
                         static_cast<uint32_t>(scene.dirty_enemy_ids.size()),
                         static_cast<uint32_t>(scene.dirty_item_ids.size()), 0,
                         0);
  return true;
}

void GameManager::CleanupExpiredPlayers(
    const std::vector<uint32_t>& expired_players) {
  for (const uint32_t player_id : expired_players) {
    spdlog::info("[disconnect] timeout player_id={}", player_id);
    RoomManager::Instance().RemovePlayer(player_id);
    RemovePlayer(player_id);
    TcpSession::RevokeToken(player_id);
  }
}
