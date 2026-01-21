#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>
#include <utility>
#include <vector>

#include "game/managers/game_manager.hpp"

namespace {
constexpr double kEnemyReplanIntervalSeconds = 0.25;
constexpr float kEnemyWaypointReachRadius = 12.0f;
constexpr double kEnemyDespawnDelaySeconds =
    3.0;  // 死亡敌人保留时间（用于客户端表现）

struct NavGrid {
  int cells_x = 0;
  int cells_y = 0;
  int cell_size = 0;
};

int ClampInt(int v, int lo, int hi) { return std::min(std::max(v, lo), hi); }

int ToIndex(const NavGrid& grid, int x, int y) { return y * grid.cells_x + x; }

std::pair<int, int> WorldToCell(const NavGrid& grid, float x, float y) {
  const int cx =
      ClampInt(static_cast<int>(x / static_cast<float>(grid.cell_size)), 0,
               std::max(0, grid.cells_x - 1));
  const int cy =
      ClampInt(static_cast<int>(y / static_cast<float>(grid.cell_size)), 0,
               std::max(0, grid.cells_y - 1));
  return {cx, cy};
}

std::pair<float, float> CellCenterWorld(const NavGrid& grid, int cx, int cy) {
  const float fx =
      (static_cast<float>(cx) + 0.5f) * static_cast<float>(grid.cell_size);
  const float fy =
      (static_cast<float>(cy) + 0.5f) * static_cast<float>(grid.cell_size);
  return {fx, fy};
}

float Heuristic(const std::pair<int, int>& a, const std::pair<int, int>& b) {
  const float dx = static_cast<float>(a.first - b.first);
  const float dy = static_cast<float>(a.second - b.second);
  return std::sqrt(dx * dx + dy * dy);
}

bool FindPathAstar(const NavGrid& grid, const std::pair<int, int>& start,
                   const std::pair<int, int>& goal,
                   std::vector<std::pair<int, int>>* out_path,
                   std::vector<int>& came_from, std::vector<float>& g_score,
                   std::vector<uint8_t>& closed) {
  if (out_path == nullptr) {
    return false;
  }
  out_path->clear();
  if (grid.cells_x <= 0 || grid.cells_y <= 0) {
    return false;
  }

  const int total = grid.cells_x * grid.cells_y;
  if (total <= 0) {
    return false;
  }

  came_from.assign(static_cast<std::size_t>(total), -1);
  g_score.assign(static_cast<std::size_t>(total),
                 std::numeric_limits<float>::infinity());
  closed.assign(static_cast<std::size_t>(total), 0);

  const int start_idx = ToIndex(grid, start.first, start.second);
  const int goal_idx = ToIndex(grid, goal.first, goal.second);
  if (start_idx < 0 || start_idx >= total || goal_idx < 0 ||
      goal_idx >= total) {
    return false;
  }

  struct OpenNode {
    int idx = 0;
    float f = 0.0f;
    float g = 0.0f;
  };
  auto cmp = [](const OpenNode& a, const OpenNode& b) { return a.f > b.f; };
  std::priority_queue<OpenNode, std::vector<OpenNode>, decltype(cmp)> open(cmp);

  g_score[static_cast<std::size_t>(start_idx)] = 0.0f;
  open.push(OpenNode{start_idx, Heuristic(start, goal), 0.0f});

  constexpr std::array<std::pair<int, int>, 8> kDirs = {{
      {1, 0},
      {-1, 0},
      {0, 1},
      {0, -1},
      {1, 1},
      {1, -1},
      {-1, 1},
      {-1, -1},
  }};

  bool found = false;
  while (!open.empty()) {
    const OpenNode cur = open.top();
    open.pop();
    if (cur.idx == goal_idx) {
      found = true;
      break;
    }
    if (closed[static_cast<std::size_t>(cur.idx)] != 0) {
      continue;
    }
    closed[static_cast<std::size_t>(cur.idx)] = 1;

    const int cx = cur.idx % grid.cells_x;
    const int cy = cur.idx / grid.cells_x;

    for (const auto& [dx, dy] : kDirs) {
      const int nx = cx + dx;
      const int ny = cy + dy;
      if (nx < 0 || ny < 0 || nx >= grid.cells_x || ny >= grid.cells_y) {
        continue;
      }
      const int nidx = ToIndex(grid, nx, ny);
      if (closed[static_cast<std::size_t>(nidx)] != 0) {
        continue;
      }

      const float step_cost =
          (dx == 0 || dy == 0) ? 1.0f : std::numbers::sqrt2_v<float>;
      const float tentative_g =
          g_score[static_cast<std::size_t>(cur.idx)] + step_cost;
      if (tentative_g < g_score[static_cast<std::size_t>(nidx)]) {
        came_from[static_cast<std::size_t>(nidx)] = cur.idx;
        g_score[static_cast<std::size_t>(nidx)] = tentative_g;
        const std::pair<int, int> ncell{nx, ny};
        const float f = tentative_g + Heuristic(ncell, goal);
        open.push(OpenNode{nidx, f, tentative_g});
      }
    }
  }

  if (!found) {
    return false;
  }

  // Reconstruct path (goal -> start).
  std::vector<int> rev;
  rev.reserve(32);
  int cur = goal_idx;
  while (cur >= 0 && cur < total) {
    rev.push_back(cur);
    if (cur == start_idx) {
      break;
    }
    cur = came_from[static_cast<std::size_t>(cur)];
  }

  if (rev.empty() || rev.back() != start_idx) {
    return false;
  }

  out_path->reserve(rev.size());
  for (auto it = rev.rbegin(); it != rev.rend(); ++it) {
    const int idx = *it;
    const int x = idx % grid.cells_x;
    const int y = idx / grid.cells_x;
    out_path->push_back({x, y});
  }
  return true;
}
}  // namespace

// 解析敌人类型
const EnemyTypeConfig& GameManager::ResolveEnemyType(uint32_t type_id) const {
  // 后备配置
  static const EnemyTypeConfig kFallback{
      .type_id = 1,
      .name = "默认僵尸",
      .max_health = 30,
      .move_speed = 60.0f,
      .damage = 0,
      .exp_reward = 10,
  };

  if (type_id != 0) {
    auto it = enemy_types_config_.enemies.find(type_id);
    if (it != enemy_types_config_.enemies.end()) {
      // 找到了
      return it->second;
    }
  }

  // 查找是否有默认id
  const uint32_t default_id = enemy_types_config_.default_type_id > 1
                                  ? enemy_types_config_.default_type_id
                                  : kFallback.type_id;
  auto it = enemy_types_config_.enemies.find(default_id);
  if (it != enemy_types_config_.enemies.end()) {
    // 默认id对应类型存在
    return it->second;
  }

  // 判断类型容器是否有内容
  if (!enemy_types_config_.enemies.empty()) {
    // 是否首数据
    return enemy_types_config_.enemies.begin()->second;
  }

  // 实在没有就使用后背配置
  return kFallback;
}

uint32_t GameManager::PickSpawnEnemyTypeId(uint32_t* rng_state) const {
  if (rng_state == nullptr) {
    return ResolveEnemyType(0).type_id;
  }

  const auto& ids = enemy_types_config_.spawn_type_ids;
  if (ids.empty()) {
    return ResolveEnemyType(0).type_id;
  }

  return ids[static_cast<std::size_t>(NextRng(rng_state)) % ids.size()];
}

void GameManager::ProcessEnemies(Scene& scene, double dt_seconds,
                                 bool* has_dirty) {
  if (has_dirty == nullptr) {
    return;
  }

  const double wave_interval_seconds =
      config_.wave_interval_seconds > 0.0f
          ? static_cast<double>(config_.wave_interval_seconds)
          : 15.0;
  scene.wave_id = std::max<uint32_t>(
      1, 1u + static_cast<uint32_t>(scene.elapsed / wave_interval_seconds));

  const std::size_t alive_players =
      std::count_if(scene.players.begin(), scene.players.end(),
                    [](const auto& kv) { return kv.second.state.is_alive(); });

  // 清理已死亡的敌人（在客户端收到死亡事件后可移除渲染）
  for (auto it = scene.enemies.begin(); it != scene.enemies.end();) {
    EnemyRuntime& enemy = it->second;
    if (!enemy.state.is_alive()) {
      enemy.dead_elapsed_seconds += dt_seconds;
      if (enemy.force_sync_left == 0 &&
          enemy.dead_elapsed_seconds >= kEnemyDespawnDelaySeconds) {
        it = scene.enemies.erase(it);
        continue;
      }
    }
    ++it;
  }

  std::size_t alive_enemies =
      std::count_if(scene.enemies.begin(), scene.enemies.end(),
                    [](const auto& kv) { return kv.second.state.is_alive(); });

  const std::size_t max_enemies_alive =
      config_.max_enemies_alive > 0 ? config_.max_enemies_alive : 256;
  const std::size_t max_spawn_per_tick = config_.max_enemy_spawn_per_tick > 0
                                             ? config_.max_enemy_spawn_per_tick
                                             : 4;

  auto spawn_enemy = [&](uint32_t type_id) {
    if (alive_enemies >= max_enemies_alive) {
      return false;
    }
    if (alive_players == 0) {
      return false;
    }

    const EnemyTypeConfig& type = ResolveEnemyType(type_id);

    const float map_w = static_cast<float>(scene.config.width);
    const float map_h = static_cast<float>(scene.config.height);
    const float t = NextRngUnitFloat(&scene.rng_state);
    const uint32_t edge = NextRng(&scene.rng_state) % 4u;

    float x = 0.0f;
    float y = 0.0f;
    switch (edge) {
      case 0:
        x = kEnemySpawnInset;
        y = t * map_h;
        break;
      case 1:
        x = std::max(0.0f, map_w - kEnemySpawnInset);
        y = t * map_h;
        break;
      case 2:
        x = t * map_w;
        y = kEnemySpawnInset;
        break;
      default:
        x = t * map_w;
        y = std::max(0.0f, map_h - kEnemySpawnInset);
        break;
    }

    EnemyRuntime runtime;
    runtime.state.set_enemy_id(scene.next_enemy_id++);
    runtime.state.set_type_id(type.type_id);
    const auto clamped_pos = ClampToMap(scene.config, x, y);
    runtime.state.mutable_position()->set_x(clamped_pos.x());
    runtime.state.mutable_position()->set_y(clamped_pos.y());
    runtime.state.set_health(type.max_health);
    runtime.state.set_max_health(type.max_health);
    runtime.state.set_is_alive(true);
    runtime.state.set_wave_id(scene.wave_id);
    runtime.state.set_is_friendly(false);
    runtime.force_sync_left = kEnemySpawnForceSyncCount;
    runtime.dirty = true;
    scene.enemies.emplace(runtime.state.enemy_id(), std::move(runtime));
    alive_enemies += 1;
    return true;
  };

  if (alive_players > 0) {
    const double base_spawn =
        std::max(0.0, static_cast<double>(config_.enemy_spawn_base_per_second));
    const double per_player_spawn = std::max(
        0.0, static_cast<double>(config_.enemy_spawn_per_player_per_second));
    const double wave_growth_spawn = std::max(
        0.0, static_cast<double>(config_.enemy_spawn_wave_growth_per_second));
    const double wave_boost =
        static_cast<double>(scene.wave_id > 0 ? scene.wave_id - 1 : 0);
    const double spawn_rate = std::clamp(
        base_spawn + per_player_spawn * static_cast<double>(alive_players) +
            wave_growth_spawn * wave_boost,
        0.0, 30.0);
    const double spawn_interval = spawn_rate > 1e-6 ? 1.0 / spawn_rate : 0.0;

    scene.spawn_elapsed += dt_seconds;
    std::size_t spawned = 0;
    while (spawn_interval > 0.0 && scene.spawn_elapsed >= spawn_interval &&
           alive_enemies < max_enemies_alive && spawned < max_spawn_per_tick) {
      scene.spawn_elapsed -= spawn_interval;
      if (spawn_enemy(PickSpawnEnemyTypeId(&scene.rng_state))) {
        spawned += 1;
        *has_dirty = true;
      } else {
        break;
      }
    }
  }

  const NavGrid nav{scene.nav_cells_x, scene.nav_cells_y, kNavCellSize};
  const float reach_sq = kEnemyWaypointReachRadius * kEnemyWaypointReachRadius;

  auto nearest_player_id = [&](float x, float y) -> uint32_t {
    uint32_t best_id = 0;
    float best_dist_sq = std::numeric_limits<float>::infinity();
    for (const auto& [pid, pr] : scene.players) {
      if (!pr.state.is_alive()) {
        continue;
      }
      const float dx = pr.state.position().x() - x;
      const float dy = pr.state.position().y() - y;
      const float dist_sq = dx * dx + dy * dy;
      if (dist_sq < best_dist_sq) {
        best_dist_sq = dist_sq;
        best_id = pid;
      }
    }
    return best_id;
  };

  for (auto& [_, enemy] : scene.enemies) {
    if (!enemy.state.is_alive()) {
      continue;
    }

    enemy.attack_cooldown_seconds =
        std::max(0.0, enemy.attack_cooldown_seconds - dt_seconds);

    auto* pos = enemy.state.mutable_position();
    const float prev_x = pos->x();
    const float prev_y = pos->y();

    const uint32_t target_id = nearest_player_id(prev_x, prev_y);
    if (target_id == 0) {
      continue;
    }

    const auto target_it = scene.players.find(target_id);
    if (target_it == scene.players.end()) {
      continue;
    }

    const float target_x = target_it->second.state.position().x();
    const float target_y = target_it->second.state.position().y();

    const bool target_changed = (enemy.target_player_id != target_id);
    enemy.replan_elapsed += dt_seconds;

    if (target_changed || enemy.replan_elapsed >= kEnemyReplanIntervalSeconds) {
      enemy.target_player_id = target_id;
      enemy.replan_elapsed = 0.0;

      const auto start_cell = WorldToCell(nav, prev_x, prev_y);
      const auto goal_cell = WorldToCell(nav, target_x, target_y);
      if (start_cell == goal_cell) {
        enemy.path.clear();
        enemy.path_index = 0;
      } else {
        std::vector<std::pair<int, int>> new_path;
        if (FindPathAstar(nav, start_cell, goal_cell, &new_path,
                          scene.nav_came_from, scene.nav_g_score,
                          scene.nav_closed) &&
            new_path.size() > 1) {
          enemy.path = std::move(new_path);
          enemy.path_index = 1;  // 跳过起点格
        } else {
          enemy.path.clear();
          enemy.path_index = 0;
        }
      }
    }

    auto select_goal = [&]() -> std::pair<float, float> {
      if (enemy.path_index < enemy.path.size()) {
        const auto [cx, cy] = enemy.path[enemy.path_index];
        const auto [wx, wy] = CellCenterWorld(nav, cx, cy);
        const auto clamped = ClampToMap(scene.config, wx, wy);
        return {clamped.x(), clamped.y()};
      }
      return {target_x, target_y};
    };

    std::pair<float, float> goal = select_goal();
    for (int iter = 0; iter < 4; ++iter) {
      const float dx = goal.first - prev_x;
      const float dy = goal.second - prev_y;
      const float dist_sq = dx * dx + dy * dy;
      if (enemy.path_index < enemy.path.size() && dist_sq <= reach_sq) {
        enemy.path_index += 1;
        goal = select_goal();
        continue;
      }
      break;
    }

    const float dx = goal.first - prev_x;
    const float dy = goal.second - prev_y;
    const float dist_sq = dx * dx + dy * dy;
    if (dist_sq > 1e-6f) {
      const float inv_len = 1.0f / std::sqrt(dist_sq);
      const float dir_x = dx * inv_len;
      const float dir_y = dy * inv_len;

      const EnemyTypeConfig& type = ResolveEnemyType(enemy.state.type_id());
      const float speed = type.move_speed > 0.0f ? type.move_speed : 60.0f;

      const auto new_pos = ClampToMap(
          scene.config, prev_x + dir_x * speed * static_cast<float>(dt_seconds),
          prev_y + dir_y * speed * static_cast<float>(dt_seconds));
      const float new_x = new_pos.x();
      const float new_y = new_pos.y();
      if (std::abs(new_x - prev_x) > 1e-4f ||
          std::abs(new_y - prev_y) > 1e-4f) {
        pos->set_x(new_x);
        pos->set_y(new_y);
        enemy.dirty = true;
      }
    }

    if (enemy.dirty || enemy.force_sync_left > 0) {
      *has_dirty = true;
    }
  }
}
