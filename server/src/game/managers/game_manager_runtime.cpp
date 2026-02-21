#include <algorithm>
#include <chrono>
#include <cmath>
#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"
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

void GameManager::RecordPlayerHistoryLocked(Scene& scene) {
  const std::size_t limit = GetPredictionHistoryLimit(scene);
  for (auto& [_, runtime] : scene.players) {
    PlayerRuntime::HistoryEntry entry;
    entry.tick = scene.tick;
    entry.position = runtime.state.position();
    entry.rotation = runtime.state.rotation();
    entry.health = runtime.state.health();
    entry.is_alive = runtime.state.is_alive();
    entry.last_processed_input_seq = runtime.last_input_seq;
    runtime.history.push_back(entry);
    while (runtime.history.size() > limit) {
      runtime.history.pop_front();
    }
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
