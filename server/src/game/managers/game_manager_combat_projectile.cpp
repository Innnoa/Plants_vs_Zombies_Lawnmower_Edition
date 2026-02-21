#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "game/managers/game_manager.hpp"

namespace {
float DistanceSq(float ax, float ay, float bx, float by) {
  const float dx = ax - bx;
  const float dy = ay - by;
  return dx * dx + dy * dy;
}

// 线段与圆是否相交（用于连续碰撞检测，避免高速穿透）
bool SegmentCircleOverlap(float ax, float ay, float bx, float by, float cx,
                          float cy, float radius, float* out_t) {
  const float dx = bx - ax;
  const float dy = by - ay;
  const float len_sq = dx * dx + dy * dy;
  float t = 0.0f;
  if (len_sq > 1e-6f) {
    t = ((cx - ax) * dx + (cy - ay) * dy) / len_sq;
    t = std::clamp(t, 0.0f, 1.0f);
  }
  const float closest_x = ax + dx * t;
  const float closest_y = ay + dy * t;
  const float dist_sq = DistanceSq(closest_x, closest_y, cx, cy);
  if (dist_sq <= radius * radius) {
    if (out_t != nullptr) {
      *out_t = t;
    }
    return true;
  }
  return false;
}

constexpr float kEnemyCollisionRadius = 16.0f;
}  // namespace

void GameManager::BuildEnemyHitGridForProjectileStage(
    Scene& scene, EnemyHitGrid* out_grid) const {
  if (out_grid == nullptr) {
    return;
  }
  out_grid->cells_x = 0;
  out_grid->cells_y = 0;
  out_grid->cell_size = 0.0f;
  out_grid->enabled = scene.enemies.size() >= 16 && !scene.projectiles.empty();
  out_grid->cells.clear();
  if (!out_grid->enabled) {
    return;
  }

  const float map_w = static_cast<float>(scene.config.width);
  const float map_h = static_cast<float>(scene.config.height);
  out_grid->cell_size =
      kNavCellSize > 0 ? static_cast<float>(kNavCellSize) : 100.0f;
  out_grid->cells_x =
      std::max(1, static_cast<int>(std::ceil(map_w / out_grid->cell_size)));
  out_grid->cells_y =
      std::max(1, static_cast<int>(std::ceil(map_h / out_grid->cell_size)));
  out_grid->cells.resize(
      static_cast<std::size_t>(out_grid->cells_x * out_grid->cells_y));
  const int max_cx = out_grid->cells_x - 1;
  const int max_cy = out_grid->cells_y - 1;
  auto clamp_int = [](int v, int lo, int hi) {
    return std::min(std::max(v, lo), hi);
  };
  for (auto& [_, enemy] : scene.enemies) {
    if (!enemy.state.is_alive()) {
      continue;
    }
    const float ex = enemy.state.position().x();
    const float ey = enemy.state.position().y();
    const int cx = clamp_int(
        static_cast<int>(std::floor(ex / out_grid->cell_size)), 0, max_cx);
    const int cy = clamp_int(
        static_cast<int>(std::floor(ey / out_grid->cell_size)), 0, max_cy);
    out_grid->cells[static_cast<std::size_t>(cy * out_grid->cells_x + cx)]
        .push_back(&enemy);
  }
}

bool GameManager::FindProjectileHitEnemyForStage(
    Scene& scene, const CombatTickParams& params, const EnemyHitGrid& grid,
    float prev_x, float prev_y, float next_x, float next_y,
    EnemyRuntime** hit_enemy, uint32_t* hit_enemy_id, float* out_hit_t) const {
  if (hit_enemy == nullptr || hit_enemy_id == nullptr || out_hit_t == nullptr) {
    return false;
  }
  *hit_enemy = nullptr;
  *hit_enemy_id = 0;
  float best_t = std::numeric_limits<float>::infinity();
  const float combined_radius =
      params.projectile_radius + kEnemyCollisionRadius;
  auto test_enemy_hit = [&](EnemyRuntime& enemy) {
    if (!enemy.state.is_alive()) {
      return;
    }
    const float ex = enemy.state.position().x();
    const float ey = enemy.state.position().y();
    float hit_t = 0.0f;
    if (!SegmentCircleOverlap(prev_x, prev_y, next_x, next_y, ex, ey,
                              combined_radius, &hit_t)) {
      return;
    }
    if (hit_t < best_t) {
      best_t = hit_t;
      *hit_enemy = &enemy;
      *hit_enemy_id = enemy.state.enemy_id();
    }
  };

  auto clamp_int = [](int v, int lo, int hi) {
    return std::min(std::max(v, lo), hi);
  };
  if (grid.enabled) {
    const float min_x = std::min(prev_x, next_x) - combined_radius;
    const float max_x = std::max(prev_x, next_x) + combined_radius;
    const float min_y = std::min(prev_y, next_y) - combined_radius;
    const float max_y = std::max(prev_y, next_y) + combined_radius;
    const int max_cx = grid.cells_x - 1;
    const int max_cy = grid.cells_y - 1;
    const int min_cx = clamp_int(
        static_cast<int>(std::floor(min_x / grid.cell_size)), 0, max_cx);
    const int max_cx_range = clamp_int(
        static_cast<int>(std::floor(max_x / grid.cell_size)), 0, max_cx);
    const int min_cy = clamp_int(
        static_cast<int>(std::floor(min_y / grid.cell_size)), 0, max_cy);
    const int max_cy_range = clamp_int(
        static_cast<int>(std::floor(max_y / grid.cell_size)), 0, max_cy);
    for (int cy = min_cy; cy <= max_cy_range; ++cy) {
      for (int cx = min_cx; cx <= max_cx_range; ++cx) {
        const auto& cell =
            grid.cells[static_cast<std::size_t>(cy * grid.cells_x + cx)];
        for (EnemyRuntime* enemy : cell) {
          if (enemy == nullptr) {
            continue;
          }
          test_enemy_hit(*enemy);
        }
      }
    }
  } else {
    for (auto& [enemy_id, enemy] : scene.enemies) {
      (void)enemy_id;
      test_enemy_hit(enemy);
    }
  }
  *out_hit_t = best_t;
  return *hit_enemy != nullptr;
}

void GameManager::ApplyProjectileHitForStage(
    Scene& scene, ProjectileRuntime& proj, EnemyRuntime& hit_enemy,
    std::vector<lawnmower::S2C_EnemyDied>* enemy_dieds,
    std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
    std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
    std::vector<uint32_t>* killed_enemy_ids, bool* has_dirty) {
  if (enemy_dieds == nullptr || enemy_attack_states == nullptr ||
      level_ups == nullptr || killed_enemy_ids == nullptr ||
      has_dirty == nullptr) {
    return;
  }
  const uint32_t hit_enemy_id = hit_enemy.state.enemy_id();
  const int32_t prev_hp = hit_enemy.state.health();
  const int32_t dealt = std::min(proj.damage, std::max<int32_t>(0, prev_hp));
  hit_enemy.state.set_health(std::max<int32_t>(0, prev_hp - proj.damage));
  MarkEnemyDirty(scene, hit_enemy_id, hit_enemy);
  *has_dirty = true;

  auto owner_it = scene.players.find(proj.owner_player_id);
  if (owner_it != scene.players.end()) {
    owner_it->second.damage_dealt += dealt;
  }

  if (hit_enemy.state.health() > 0) {
    return;
  }

  hit_enemy.state.set_is_alive(false);
  if (hit_enemy.is_attacking || hit_enemy.attack_target_player_id != 0) {
    hit_enemy.is_attacking = false;
    hit_enemy.attack_target_player_id = 0;
    lawnmower::EnemyAttackStateDelta delta;
    delta.set_enemy_id(hit_enemy_id);
    delta.set_is_attacking(false);
    delta.set_target_player_id(0);
    enemy_attack_states->push_back(std::move(delta));
  }
  hit_enemy.dead_elapsed_seconds = 0.0;
  hit_enemy.force_sync_left =
      std::max(hit_enemy.force_sync_left, kEnemySpawnForceSyncCount);
  MarkEnemyDirty(scene, hit_enemy_id, hit_enemy);
  killed_enemy_ids->push_back(hit_enemy_id);

  lawnmower::S2C_EnemyDied died;
  died.set_enemy_id(hit_enemy_id);
  died.set_killer_player_id(proj.owner_player_id);
  died.set_wave_id(hit_enemy.state.wave_id());
  *died.mutable_position() = hit_enemy.state.position();
  enemy_dieds->push_back(std::move(died));

  if (owner_it != scene.players.end()) {
    owner_it->second.kill_count += 1;
    const uint32_t exp_reward = static_cast<uint32_t>(std::max<int32_t>(
        0, ResolveEnemyType(hit_enemy.state.type_id()).exp_reward));
    GrantExpForCombat(scene, owner_it->second, exp_reward, level_ups);
  }
}

bool GameManager::IsProjectileOutOfBoundsForStage(const ProjectileRuntime& proj,
                                                  float map_w, float map_h) {
  return proj.x < 0.0f || proj.y < 0.0f || proj.x > map_w || proj.y > map_h;
}

void GameManager::PushProjectileDespawnForStage(
    const ProjectileRuntime& proj, lawnmower::ProjectileDespawnReason reason,
    uint32_t hit_enemy_id,
    std::vector<lawnmower::ProjectileDespawn>* projectile_despawns) {
  if (projectile_despawns == nullptr) {
    return;
  }
  lawnmower::ProjectileDespawn evt;
  evt.set_projectile_id(proj.projectile_id);
  evt.set_reason(reason);
  evt.set_hit_enemy_id(hit_enemy_id);
  evt.mutable_position()->set_x(proj.x);
  evt.mutable_position()->set_y(proj.y);
  projectile_despawns->push_back(std::move(evt));
}

void GameManager::ProcessProjectileHitStage(
    Scene& scene, double dt_seconds, const CombatTickParams& params,
    std::vector<lawnmower::S2C_EnemyDied>* enemy_dieds,
    std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
    std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
    std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
    std::vector<uint32_t>* killed_enemy_ids, bool* has_dirty) {
  if (enemy_dieds == nullptr || enemy_attack_states == nullptr ||
      level_ups == nullptr || projectile_despawns == nullptr ||
      killed_enemy_ids == nullptr || has_dirty == nullptr) {
    return;
  }

  const float map_w = static_cast<float>(scene.config.width);
  const float map_h = static_cast<float>(scene.config.height);
  EnemyHitGrid enemy_grid;
  BuildEnemyHitGridForProjectileStage(scene, &enemy_grid);

  for (auto it = scene.projectiles.begin(); it != scene.projectiles.end();) {
    ProjectileRuntime& proj = it->second;
    proj.remaining_seconds -= dt_seconds;
    const float prev_x = proj.x;
    const float prev_y = proj.y;
    const float delta_seconds = static_cast<float>(std::max(0.0, dt_seconds));
    const float next_x = prev_x + proj.dir_x * proj.speed * delta_seconds;
    const float next_y = prev_y + proj.dir_y * proj.speed * delta_seconds;
    proj.x = next_x;
    proj.y = next_y;

    bool despawn = false;
    lawnmower::ProjectileDespawnReason reason =
        lawnmower::PROJECTILE_DESPAWN_UNKNOWN;
    uint32_t hit_enemy_id = 0;
    EnemyRuntime* hit_enemy = nullptr;
    float hit_t = std::numeric_limits<float>::infinity();

    if (proj.remaining_seconds <= 0.0) {
      despawn = true;
      reason = lawnmower::PROJECTILE_DESPAWN_EXPIRED;
    } else if (FindProjectileHitEnemyForStage(
                   scene, params, enemy_grid, prev_x, prev_y, next_x, next_y,
                   &hit_enemy, &hit_enemy_id, &hit_t)) {
      const float hit_x = prev_x + (next_x - prev_x) * hit_t;
      const float hit_y = prev_y + (next_y - prev_y) * hit_t;
      proj.x = hit_x;
      proj.y = hit_y;
      despawn = true;
      reason = lawnmower::PROJECTILE_DESPAWN_HIT;
      ApplyProjectileHitForStage(scene, proj, *hit_enemy, enemy_dieds,
                                 enemy_attack_states, level_ups,
                                 killed_enemy_ids, has_dirty);
    } else if (IsProjectileOutOfBoundsForStage(proj, map_w, map_h)) {
      despawn = true;
      reason = lawnmower::PROJECTILE_DESPAWN_OUT_OF_BOUNDS;
    }

    if (despawn) {
      PushProjectileDespawnForStage(proj, reason, hit_enemy_id,
                                    projectile_despawns);

      scene.projectile_pool.push_back(std::move(proj));
      it = scene.projectiles.erase(it);
    } else {
      ++it;
    }
  }
}
