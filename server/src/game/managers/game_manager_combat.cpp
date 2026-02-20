#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <spdlog/spdlog.h>
#include <vector>

#include "game/managers/game_manager.hpp"
#include "internal/game_manager_misc_utils.hpp"

namespace {
// 碰撞/战斗相关（后续可考虑挪到配置）
constexpr float kPlayerCollisionRadius = 18.0f;
constexpr float kEnemyCollisionRadius = 16.0f;
constexpr double kDefaultEnemyAttackIntervalSeconds = 0.8;
constexpr double kMinEnemyAttackIntervalSeconds = 0.05;
constexpr double kMaxEnemyAttackIntervalSeconds = 10.0;
constexpr double kPlayerTargetRefreshIntervalSeconds = 0.2;
constexpr uint64_t kAttackDirFallbackLogIntervalTicks = 60;
constexpr uint64_t kProjectileSpawnLogIntervalTicks = 60;
// 射弹嘴部偏移（基于玩家中心点）
constexpr float kProjectileMouthOffsetUp = 18.0f;
constexpr float kProjectileMouthOffsetSide = 36.0f;

// 默认射速 clamp（若配置缺失/非法则回退）
constexpr double kMinAttackIntervalSeconds = 0.05;  // 射速上限（最小间隔，秒）
constexpr double kMaxAttackIntervalSeconds = 2.0;   // 射速下限（最大间隔，秒）

float DistanceSq(float ax, float ay, float bx, float by) {
  const float dx = ax - bx;
  const float dy = ay - by;
  return dx * dx + dy * dy;
}

bool CirclesOverlap(float ax, float ay, float ar, float bx, float by,
                    float br) {
  const float r = ar + br;
  return DistanceSq(ax, ay, bx, by) <= r * r;
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

double PlayerAttackIntervalSeconds(uint32_t attack_speed, double min_interval,
                                   double max_interval) {
  // attack_speed 语义：数值越大越快（默认 1 表示 1 次/秒）。
  if (attack_speed == 0) {
    return std::clamp(1.0, min_interval, max_interval);
  }
  const double interval = 1.0 / static_cast<double>(attack_speed);
  return std::clamp(interval, min_interval, max_interval);
}
}  // namespace

GameManager::CombatTickParams GameManager::BuildCombatTickParams(
    const Scene& scene, double dt_seconds) const {
  CombatTickParams params;
  params.projectile_speed = std::clamp(
      config_.projectile_speed > 0.0f ? config_.projectile_speed : 420.0f, 1.0f,
      5000.0f);
  params.projectile_radius = std::clamp(
      config_.projectile_radius > 0.0f ? config_.projectile_radius : 6.0f, 0.5f,
      128.0f);
  params.projectile_ttl_seconds =
      std::clamp(config_.projectile_ttl_seconds > 0.0f
                     ? static_cast<double>(config_.projectile_ttl_seconds)
                     : 2.5,
                 0.05, 30.0);
  params.projectile_ttl_ms = static_cast<uint32_t>(std::clamp(
      std::llround(params.projectile_ttl_seconds * 1000.0), 1LL, 30000LL));
  params.max_shots_per_tick =
      std::clamp(config_.projectile_max_shots_per_tick > 0
                     ? config_.projectile_max_shots_per_tick
                     : 4u,
                 1u, 64u);
  params.attack_min_interval = std::max(
      1e-3,
      config_.projectile_attack_min_interval_seconds > 0.0f
          ? static_cast<double>(config_.projectile_attack_min_interval_seconds)
          : kMinAttackIntervalSeconds);
  params.attack_max_interval = std::max(
      params.attack_min_interval,
      config_.projectile_attack_max_interval_seconds > 0.0f
          ? static_cast<double>(config_.projectile_attack_max_interval_seconds)
          : kMaxAttackIntervalSeconds);

  const double tick_interval_seconds =
      scene.tick_interval.count() > 0.0
          ? scene.tick_interval.count()
          : (config_.tick_rate > 0
                 ? 1.0 / static_cast<double>(config_.tick_rate)
                 : 1.0 / 60.0);
  params.allow_catchup = dt_seconds <= tick_interval_seconds * 1.5;
  return params;
}

void GameManager::MarkPlayerLowFreqDirtyForCombat(Scene& scene,
                                                  PlayerRuntime& runtime) {
  MarkPlayerDirty(scene, runtime.state.player_id(), runtime, true);
}

void GameManager::GrantExpForCombat(
    Scene& scene, PlayerRuntime& player, uint32_t exp_reward,
    std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups) {
  if (exp_reward == 0 || level_ups == nullptr) {
    return;
  }
  player.state.set_exp(player.state.exp() + exp_reward);
  MarkPlayerLowFreqDirtyForCombat(scene, player);

  // 升级：允许单次击杀连升多级
  while (player.state.exp_to_next() > 0 &&
         player.state.exp() >= player.state.exp_to_next()) {
    player.state.set_exp(player.state.exp() - player.state.exp_to_next());
    player.state.set_level(player.state.level() + 1);

    const uint32_t next_exp =
        static_cast<uint32_t>(std::llround(player.state.exp_to_next() * 1.25)) +
        25u;
    player.state.set_exp_to_next(std::max<uint32_t>(1, next_exp));
    player.pending_upgrade_count += 1;

    lawnmower::S2C_PlayerLevelUp evt;
    evt.set_player_id(player.state.player_id());
    evt.set_new_level(player.state.level());
    evt.set_exp_to_next(player.state.exp_to_next());
    level_ups->push_back(std::move(evt));
  }
}

std::pair<float, float> GameManager::RotationDir(float rotation_deg) {
  const float rad = rotation_deg * std::numbers::pi_v<float> / 180.0f;
  return {std::cos(rad), std::sin(rad)};
}

float GameManager::RotationFromDir(float dir_x, float dir_y) {
  if (std::abs(dir_x) < 1e-6f && std::abs(dir_y) < 1e-6f) {
    return 0.0f;
  }
  const float angle_rad = std::atan2(dir_y, dir_x);
  return angle_rad * 180.0f / std::numbers::pi_v<float>;
}

std::pair<float, float> GameManager::ComputeProjectileOrigin(
    const PlayerRuntime& player, float facing_dir_x) {
  const float side = facing_dir_x >= 0.0f ? kProjectileMouthOffsetSide
                                          : -kProjectileMouthOffsetSide;
  return {player.state.position().x() + side,
          player.state.position().y() + kProjectileMouthOffsetUp};
}

uint32_t GameManager::FindNearestEnemyIdForPlayerFire(
    const Scene& scene, const PlayerRuntime& player) const {
  const float px = player.state.position().x();
  const float py = player.state.position().y();
  float best_dist_sq = std::numeric_limits<float>::infinity();
  uint32_t best_id = 0;
  for (const auto& [enemy_id, enemy] : scene.enemies) {
    if (!enemy.state.is_alive()) {
      continue;
    }
    const float ex = enemy.state.position().x();
    const float ey = enemy.state.position().y();
    const float dist_sq = DistanceSq(px, py, ex, ey);
    if (dist_sq < best_dist_sq) {
      best_dist_sq = dist_sq;
      best_id = enemy_id;
    }
  }
  return best_id;
}

const GameManager::EnemyRuntime* GameManager::ResolveLockedTargetForPlayerFire(
    Scene& scene, PlayerRuntime& player, double dt_seconds) const {
  player.target_refresh_elapsed += std::max(0.0, dt_seconds);
  const EnemyRuntime* target = nullptr;
  if (player.locked_target_enemy_id != 0) {
    auto it = scene.enemies.find(player.locked_target_enemy_id);
    if (it != scene.enemies.end() && it->second.state.is_alive()) {
      target = &it->second;
    } else {
      player.locked_target_enemy_id = 0;
    }
  }

  const bool should_refresh =
      player.target_refresh_elapsed >= kPlayerTargetRefreshIntervalSeconds;
  if (target == nullptr || should_refresh) {
    const uint32_t nearest_id = FindNearestEnemyIdForPlayerFire(scene, player);
    if (nearest_id != 0) {
      player.locked_target_enemy_id = nearest_id;
      auto it = scene.enemies.find(nearest_id);
      if (it != scene.enemies.end()) {
        target = &it->second;
      } else {
        player.locked_target_enemy_id = 0;
        target = nullptr;
      }
    } else {
      player.locked_target_enemy_id = 0;
      target = nullptr;
    }
    player.target_refresh_elapsed = 0.0;
  }
  return target;
}

void GameManager::MaybeLogAttackDirFallback(const Scene& scene,
                                            PlayerRuntime& player,
                                            uint32_t target_id,
                                            const char* reason) const {
  if (scene.tick <
      player.last_attack_dir_log_tick + kAttackDirFallbackLogIntervalTicks) {
    return;
  }
  player.last_attack_dir_log_tick = scene.tick;
  spdlog::debug("Projectile dir fallback: player={} target={} reason={}",
                player.state.player_id(), target_id, reason);
}

void GameManager::MaybeLogProjectileSpawn(
    const Scene& scene, PlayerRuntime& player, uint32_t projectile_id,
    const EnemyRuntime& target, float origin_x, float origin_y, float dir_x,
    float dir_y, float rotation) const {
  if (scene.tick < player.last_projectile_spawn_log_tick +
                       kProjectileSpawnLogIntervalTicks) {
    return;
  }
  player.last_projectile_spawn_log_tick = scene.tick;
  spdlog::debug(
      "Projectile spawn: tick={} player={} projectile={} "
      "origin=({:.2f},{:.2f}) "
      "target={} target_pos=({:.2f},{:.2f}) dir=({:.3f},{:.3f}) rot={:.2f}",
      scene.tick, player.state.player_id(), projectile_id, origin_x, origin_y,
      target.state.enemy_id(), target.state.position().x(),
      target.state.position().y(), dir_x, dir_y, rotation);
}

bool GameManager::ResolveProjectileDirectionForPlayerFire(
    const Scene& scene, PlayerRuntime& player, const EnemyRuntime& target,
    float* out_dir_x, float* out_dir_y, float* out_rotation) const {
  if (out_dir_x == nullptr || out_dir_y == nullptr || out_rotation == nullptr) {
    return false;
  }

  const float px = player.state.position().x();
  const float py = player.state.position().y();
  float facing_dir_x = target.state.position().x() - px;
  float facing_dir_y = target.state.position().y() - py;
  const float facing_len_sq =
      facing_dir_x * facing_dir_x + facing_dir_y * facing_dir_y;
  if (facing_len_sq <= 1e-6f) {
    if (player.has_attack_dir) {
      facing_dir_x = player.last_attack_dir_x;
      facing_dir_y = player.last_attack_dir_y;
      MaybeLogAttackDirFallback(scene, player, target.state.enemy_id(),
                                "zero_dir_use_cached");
    } else {
      const auto [fallback_x, fallback_y] =
          RotationDir(player.state.rotation());
      facing_dir_x = fallback_x;
      facing_dir_y = fallback_y;
      MaybeLogAttackDirFallback(scene, player, target.state.enemy_id(),
                                "zero_dir_use_player_rotation");
    }
  } else {
    const float inv_len = 1.0f / std::sqrt(facing_len_sq);
    facing_dir_x *= inv_len;
    facing_dir_y *= inv_len;
  }

  const auto [origin_x, origin_y] =
      ComputeProjectileOrigin(player, facing_dir_x);
  float dir_x = target.state.position().x() - origin_x;
  float dir_y = target.state.position().y() - origin_y;
  const float len_sq = dir_x * dir_x + dir_y * dir_y;
  if (len_sq <= 1e-6f) {
    dir_x = facing_dir_x;
    dir_y = facing_dir_y;
  } else {
    const float inv_len = 1.0f / std::sqrt(len_sq);
    dir_x *= inv_len;
    dir_y *= inv_len;
  }

  *out_dir_x = dir_x;
  *out_dir_y = dir_y;
  *out_rotation = RotationFromDir(dir_x, dir_y);
  player.has_attack_dir = true;
  player.last_attack_dir_x = dir_x;
  player.last_attack_dir_y = dir_y;
  player.last_attack_rotation = *out_rotation;
  return true;
}

int32_t GameManager::ComputeProjectileDamageForPlayerFire(
    Scene& scene, const PlayerRuntime& player) const {
  int32_t damage = std::max<int32_t>(1, player.state.attack());
  if (player.state.has_buff()) {
    damage = static_cast<int32_t>(std::llround(damage * 1.2));
  }
  if (player.state.critical_hit_rate() > 0) {
    const float chance = std::clamp(
        static_cast<float>(player.state.critical_hit_rate()) / 1000.0f, 0.0f,
        1.0f);
    if (NextRngUnitFloat(&scene.rng_state) < chance) {
      damage *= 2;
    }
  }
  return damage;
}

void GameManager::SpawnProjectileForPlayerFire(
    Scene& scene, const CombatTickParams& params, uint32_t owner_player_id,
    PlayerRuntime& player, const EnemyRuntime& target, int32_t damage,
    float dir_x, float dir_y, float rotation,
    std::vector<lawnmower::ProjectileState>* projectile_spawns) {
  if (projectile_spawns == nullptr || damage <= 0) {
    return;
  }
  const auto [start_x, start_y] = ComputeProjectileOrigin(player, dir_x);

  ProjectileRuntime proj;
  if (!scene.projectile_pool.empty()) {
    proj = std::move(scene.projectile_pool.back());
    scene.projectile_pool.pop_back();
  }
  proj.projectile_id = scene.next_projectile_id++;
  proj.owner_player_id = owner_player_id;
  proj.x = start_x;
  proj.y = start_y;
  proj.dir_x = dir_x;
  proj.dir_y = dir_y;
  proj.rotation = rotation;
  proj.speed = params.projectile_speed;
  proj.damage = damage;
  proj.has_buff = player.state.has_buff();
  proj.buff_id = player.state.buff_id();
  proj.is_friendly = true;
  proj.remaining_seconds = params.projectile_ttl_seconds;

  scene.projectiles.emplace(proj.projectile_id, proj);
  MaybeLogProjectileSpawn(scene, player, proj.projectile_id, target, start_x,
                          start_y, dir_x, dir_y, rotation);

  lawnmower::ProjectileState spawn;
  spawn.set_projectile_id(proj.projectile_id);
  spawn.set_owner_player_id(owner_player_id);
  spawn.mutable_position()->set_x(start_x);
  spawn.mutable_position()->set_y(start_y);
  spawn.set_rotation(rotation);
  spawn.set_ttl_ms(params.projectile_ttl_ms);
  auto* meta = spawn.mutable_projectile();
  meta->set_speed(static_cast<uint32_t>(std::max(0.0f, proj.speed)));
  meta->set_has_buff(proj.has_buff);
  meta->set_buff_id(proj.buff_id);
  meta->set_is_friendly(proj.is_friendly);
  meta->set_damage(static_cast<uint32_t>(std::max<int32_t>(0, proj.damage)));
  projectile_spawns->push_back(std::move(spawn));
}

void GameManager::ProcessPlayerFireStage(
    Scene& scene, double dt_seconds, const CombatTickParams& params,
    std::vector<lawnmower::ProjectileState>* projectile_spawns) {
  if (projectile_spawns == nullptr) {
    return;
  }

  // 开火：attack_speed 控制射速；dt 被 clamp 后最多补几发，避免掉帧时 DPS
  // 丢失。
  for (auto& [player_id, player] : scene.players) {
    if (!player.state.is_alive() || !player.wants_attacking) {
      player.locked_target_enemy_id = 0;
      player.target_refresh_elapsed = 0.0;
      continue;
    }

    float dir_x = 0.0f;
    float dir_y = 0.0f;
    float rotation = player.state.rotation();
    const EnemyRuntime* target =
        ResolveLockedTargetForPlayerFire(scene, player, dt_seconds);
    if (target == nullptr ||
        !ResolveProjectileDirectionForPlayerFire(scene, player, *target, &dir_x,
                                                 &dir_y, &rotation)) {
      player.attack_cooldown_seconds =
          std::max(player.attack_cooldown_seconds, 0.0);
      continue;
    }

    const double interval = PlayerAttackIntervalSeconds(
        player.state.attack_speed(), params.attack_min_interval,
        params.attack_max_interval);
    uint32_t fired = 0;
    const uint32_t max_shots_this_tick =
        params.allow_catchup ? std::min<uint32_t>(params.max_shots_per_tick, 2u)
                             : 1u;
    while (player.attack_cooldown_seconds <= 1e-6 &&
           fired < max_shots_this_tick) {
      player.attack_cooldown_seconds += interval;
      fired += 1;

      const int32_t damage =
          ComputeProjectileDamageForPlayerFire(scene, player);
      SpawnProjectileForPlayerFire(scene, params, player_id, player, *target,
                                   damage, dir_x, dir_y, rotation,
                                   projectile_spawns);
    }
  }
}

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

void GameManager::BuildDropCandidatesForStage(
    std::vector<std::pair<uint32_t, uint32_t>>* drop_candidates,
    uint32_t* drop_weight_total) const {
  if (drop_candidates == nullptr || drop_weight_total == nullptr) {
    return;
  }
  drop_candidates->clear();
  drop_candidates->reserve(items_config_.items.size());
  *drop_weight_total = 0;
  for (const auto& [type_id, item] : items_config_.items) {
    if (game_manager_misc_utils::ResolveItemEffectType(item.effect) !=
        lawnmower::ITEM_EFFECT_HEAL) {
      continue;
    }
    if (item.drop_weight == 0) {
      continue;
    }
    drop_candidates->emplace_back(type_id, item.drop_weight);
    *drop_weight_total += item.drop_weight;
  }
}

uint32_t GameManager::PickDropTypeIdForStage(
    Scene& scene,
    const std::vector<std::pair<uint32_t, uint32_t>>& drop_candidates,
    uint32_t drop_weight_total) const {
  if (drop_candidates.empty() || drop_weight_total == 0) {
    return 0;
  }
  const uint32_t roll = NextRng(&scene.rng_state) % drop_weight_total;
  uint32_t accum = 0;
  for (const auto& [type_id, weight] : drop_candidates) {
    accum += weight;
    if (roll < accum) {
      return type_id;
    }
  }
  return drop_candidates.back().first;
}

void GameManager::SpawnDropItemForStage(
    Scene& scene, uint32_t type_id, float x, float y, uint32_t max_items_alive,
    std::vector<lawnmower::ItemState>* dropped_items, bool* has_dirty) {
  if (dropped_items == nullptr || has_dirty == nullptr) {
    return;
  }
  if (scene.items.size() >= max_items_alive) {
    return;
  }
  const ItemTypeConfig& type = ResolveItemType(type_id);
  const lawnmower::ItemEffectType effect_type =
      game_manager_misc_utils::ResolveItemEffectType(type.effect);
  if (effect_type == lawnmower::ITEM_EFFECT_NONE && type.effect != "none" &&
      !type.effect.empty()) {
    spdlog::warn("道具类型 {} effect={} 未识别，使用 NONE", type.type_id,
                 type.effect);
  }

  const auto clamped_pos = ClampToMap(scene.config, x, y);
  ItemRuntime runtime;
  if (!scene.item_pool.empty()) {
    runtime = std::move(scene.item_pool.back());
    scene.item_pool.pop_back();
  }
  runtime.item_id = scene.next_item_id++;
  runtime.type_id = type.type_id;
  runtime.effect_type = effect_type;
  runtime.x = clamped_pos.x();
  runtime.y = clamped_pos.y();
  runtime.is_picked = false;
  runtime.force_sync_left = 1;
  runtime.dirty = false;
  auto [it, _] = scene.items.emplace(runtime.item_id, runtime);
  MarkItemDirty(scene, it->first, it->second);

  auto& dropped = dropped_items->emplace_back();
  dropped.set_item_id(runtime.item_id);
  dropped.set_type_id(runtime.type_id);
  dropped.set_is_picked(false);
  dropped.mutable_position()->set_x(runtime.x);
  dropped.mutable_position()->set_y(runtime.y);
  *has_dirty = true;
}

void GameManager::ProcessEnemyDropStage(
    Scene& scene, const std::vector<uint32_t>& killed_enemy_ids,
    std::vector<lawnmower::ItemState>* dropped_items, bool* has_dirty) {
  if (dropped_items == nullptr || has_dirty == nullptr ||
      killed_enemy_ids.empty()) {
    return;
  }

  const uint32_t max_items_alive =
      items_config_.max_items_alive > 0 ? items_config_.max_items_alive : 64;
  std::vector<std::pair<uint32_t, uint32_t>> drop_candidates;
  uint32_t drop_weight_total = 0;
  BuildDropCandidatesForStage(&drop_candidates, &drop_weight_total);
  if (drop_weight_total == 0) {
    return;
  }

  for (const uint32_t enemy_id : killed_enemy_ids) {
    auto enemy_it = scene.enemies.find(enemy_id);
    if (enemy_it == scene.enemies.end() || enemy_it->second.state.is_alive()) {
      continue;
    }
    const EnemyTypeConfig& type =
        ResolveEnemyType(enemy_it->second.state.type_id());
    const uint32_t chance = std::min(type.drop_chance, 100u);
    if (chance == 0) {
      continue;
    }
    const float roll = NextRngUnitFloat(&scene.rng_state) * 100.0f;
    if (roll >= static_cast<float>(chance)) {
      continue;
    }
    const uint32_t type_id =
        PickDropTypeIdForStage(scene, drop_candidates, drop_weight_total);
    if (type_id == 0) {
      continue;
    }
    SpawnDropItemForStage(scene, type_id, enemy_it->second.state.position().x(),
                          enemy_it->second.state.position().y(),
                          max_items_alive, dropped_items, has_dirty);
  }
}

void GameManager::ResolveEnemyAttackRadiiForStage(const EnemyTypeConfig& type,
                                                  float* enter_radius,
                                                  float* exit_radius) const {
  if (enter_radius == nullptr || exit_radius == nullptr) {
    return;
  }
  float attack_enter_radius = type.attack_enter_radius;
  float attack_exit_radius = type.attack_exit_radius;
  if (attack_enter_radius <= 0.0f) {
    attack_enter_radius = kPlayerCollisionRadius + kEnemyCollisionRadius;
  }
  if (attack_exit_radius <= 0.0f) {
    attack_exit_radius = attack_enter_radius;
  }
  if (attack_exit_radius < attack_enter_radius) {
    attack_exit_radius = attack_enter_radius;
  }
  *enter_radius = attack_enter_radius;
  *exit_radius = attack_exit_radius;
}

uint32_t GameManager::SelectEnemyMeleeTargetForStage(const Scene& scene,
                                                     const EnemyRuntime& enemy,
                                                     float ex, float ey,
                                                     float enter_sq,
                                                     float exit_sq) const {
  uint32_t target_player_id = 0;
  float target_dist_sq = std::numeric_limits<float>::infinity();
  if (enemy.is_attacking && enemy.attack_target_player_id != 0) {
    auto target_it = scene.players.find(enemy.attack_target_player_id);
    if (target_it != scene.players.end() &&
        target_it->second.state.is_alive()) {
      const float px = target_it->second.state.position().x();
      const float py = target_it->second.state.position().y();
      const float dist_sq = DistanceSq(px, py, ex, ey);
      if (dist_sq <= exit_sq) {
        target_player_id = enemy.attack_target_player_id;
        target_dist_sq = dist_sq;
      }
    }
  }

  if (target_player_id == 0) {
    for (const auto& [pid, player] : scene.players) {
      if (!player.state.is_alive()) {
        continue;
      }
      const float px = player.state.position().x();
      const float py = player.state.position().y();
      const float dist_sq = DistanceSq(px, py, ex, ey);
      if (dist_sq > enter_sq) {
        continue;
      }
      if (dist_sq < target_dist_sq) {
        target_dist_sq = dist_sq;
        target_player_id = pid;
      }
    }
  }
  return target_player_id;
}

void GameManager::PushEnemyAttackStateForStage(
    uint32_t enemy_id, EnemyRuntime& enemy, bool attacking, uint32_t target_id,
    std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states) const {
  if (enemy_attack_states == nullptr) {
    return;
  }
  if (enemy.is_attacking == attacking &&
      enemy.attack_target_player_id == target_id) {
    return;
  }
  enemy.is_attacking = attacking;
  enemy.attack_target_player_id = target_id;
  lawnmower::EnemyAttackStateDelta delta;
  delta.set_enemy_id(enemy_id);
  delta.set_is_attacking(attacking);
  delta.set_target_player_id(target_id);
  enemy_attack_states->push_back(std::move(delta));
}

void GameManager::TryApplyEnemyMeleeDamageForStage(
    Scene& scene, uint32_t enemy_id, EnemyRuntime& enemy,
    uint32_t target_player_id, const EnemyTypeConfig& type,
    std::vector<lawnmower::S2C_PlayerHurt>* player_hurts, bool* has_dirty) {
  if (player_hurts == nullptr || has_dirty == nullptr) {
    return;
  }

  auto player_it = scene.players.find(target_player_id);
  if (player_it == scene.players.end()) {
    return;
  }
  PlayerRuntime& player = player_it->second;
  if (!player.state.is_alive()) {
    return;
  }

  // 仍在攻击范围，但冷却未结束：更新 attack state 后不结算伤害
  if (enemy.attack_cooldown_seconds > 1e-6) {
    return;
  }

  const int32_t damage = std::max<int32_t>(0, type.damage);
  const double attack_interval_seconds = std::clamp(
      type.attack_interval_seconds > 0.0f
          ? static_cast<double>(type.attack_interval_seconds)
          : kDefaultEnemyAttackIntervalSeconds,
      kMinEnemyAttackIntervalSeconds, kMaxEnemyAttackIntervalSeconds);
  enemy.attack_cooldown_seconds = attack_interval_seconds;

  // 伤害为0时不产生受伤事件（避免客户端误触发受击表现），仍保留攻击状态动画。
  if (damage <= 0) {
    return;
  }

  const int32_t prev_hp = player.state.health();
  const int32_t dealt = std::min(damage, std::max<int32_t>(0, prev_hp));
  player.state.set_health(std::max<int32_t>(0, prev_hp - damage));
  MarkPlayerLowFreqDirtyForCombat(scene, player);

  lawnmower::S2C_PlayerHurt hurt;
  hurt.set_player_id(target_player_id);
  hurt.set_damage(static_cast<uint32_t>(dealt));
  hurt.set_remaining_health(player.state.health());
  hurt.set_source_id(enemy_id);
  player_hurts->push_back(std::move(hurt));

  if (player.state.health() <= 0) {
    player.state.set_is_alive(false);
    player.wants_attacking = false;
    MarkPlayerLowFreqDirtyForCombat(scene, player);
  }
  *has_dirty = true;
}

void GameManager::ProcessEnemyMeleeStage(
    Scene& scene, double dt_seconds,
    std::vector<lawnmower::S2C_PlayerHurt>* player_hurts,
    std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
    bool* has_dirty) {
  if (player_hurts == nullptr || enemy_attack_states == nullptr ||
      has_dirty == nullptr) {
    return;
  }

  // 敌人近战攻击（基于配置的进入/退出距离阈值，带迟滞）
  for (auto& [enemy_id, enemy] : scene.enemies) {
    if (!enemy.state.is_alive()) {
      continue;
    }

    const EnemyTypeConfig& type = ResolveEnemyType(enemy.state.type_id());
    float attack_enter_radius = 0.0f;
    float attack_exit_radius = 0.0f;
    ResolveEnemyAttackRadiiForStage(type, &attack_enter_radius,
                                    &attack_exit_radius);
    const float enter_sq = attack_enter_radius * attack_enter_radius;
    const float exit_sq = attack_exit_radius * attack_exit_radius;
    const float ex = enemy.state.position().x();
    const float ey = enemy.state.position().y();

    const uint32_t target_player_id =
        SelectEnemyMeleeTargetForStage(scene, enemy, ex, ey, enter_sq, exit_sq);

    if (target_player_id == 0) {
      PushEnemyAttackStateForStage(enemy_id, enemy, false, 0,
                                   enemy_attack_states);
      continue;
    }

    PushEnemyAttackStateForStage(enemy_id, enemy, true, target_player_id,
                                 enemy_attack_states);
    TryApplyEnemyMeleeDamageForStage(scene, enemy_id, enemy, target_player_id,
                                     type, player_hurts, has_dirty);
  }
}

std::size_t GameManager::CountAlivePlayersAfterCombatForStage(
    const Scene& scene) {
  return std::count_if(
      scene.players.begin(), scene.players.end(),
      [](const auto& kv) { return kv.second.state.is_alive(); });
}

void GameManager::BuildGameOverMessageForStage(
    const Scene& scene, lawnmower::S2C_GameOver* out) const {
  if (out == nullptr) {
    return;
  }
  out->Clear();
  out->set_victory(false);
  out->set_survive_time(static_cast<uint32_t>(std::max(0.0, scene.elapsed)));
  for (const auto& [pid, player] : scene.players) {
    auto* score = out->add_scores();
    score->set_player_id(pid);
    score->set_player_name(player.player_name);
    score->set_final_level(static_cast<int32_t>(player.state.level()));
    score->set_kill_count(player.kill_count);
    score->set_damage_dealt(player.damage_dealt);
  }
}

void GameManager::UpdateGameOverForCombatStage(
    Scene& scene, std::optional<lawnmower::S2C_GameOver>* game_over) const {
  if (game_over == nullptr) {
    return;
  }
  const std::size_t alive_after_combat =
      CountAlivePlayersAfterCombatForStage(scene);
  if (alive_after_combat != 0 || scene.players.empty()) {
    return;
  }

  scene.game_over = true;
  lawnmower::S2C_GameOver over;
  BuildGameOverMessageForStage(scene, &over);
  *game_over = std::move(over);
}

void GameManager::ProcessCombatAndProjectiles(
    Scene& scene, double dt_seconds,
    std::vector<lawnmower::S2C_PlayerHurt>* player_hurts,
    std::vector<lawnmower::S2C_EnemyDied>* enemy_dieds,
    std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
    std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
    std::optional<lawnmower::S2C_GameOver>* game_over,
    std::vector<lawnmower::ProjectileState>* projectile_spawns,
    std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
    std::vector<lawnmower::ItemState>* dropped_items, bool* has_dirty) {
  if (player_hurts == nullptr || enemy_dieds == nullptr ||
      level_ups == nullptr || enemy_attack_states == nullptr ||
      game_over == nullptr || projectile_spawns == nullptr ||
      projectile_despawns == nullptr || dropped_items == nullptr ||
      has_dirty == nullptr) {
    return;
  }

  const CombatTickParams params = BuildCombatTickParams(scene, dt_seconds);
  std::vector<uint32_t> killed_enemy_ids;
  if (!scene.enemies.empty()) {
    killed_enemy_ids.reserve(scene.enemies.size());
  }

  ProcessPlayerFireStage(scene, dt_seconds, params, projectile_spawns);
  ProcessProjectileHitStage(scene, dt_seconds, params, enemy_dieds,
                            enemy_attack_states, level_ups, projectile_despawns,
                            &killed_enemy_ids, has_dirty);
  ProcessEnemyDropStage(scene, killed_enemy_ids, dropped_items, has_dirty);
  ProcessEnemyMeleeStage(scene, dt_seconds, player_hurts, enemy_attack_states,
                         has_dirty);
  UpdateGameOverForCombatStage(scene, game_over);
}
