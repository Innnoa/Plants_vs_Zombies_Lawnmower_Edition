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
