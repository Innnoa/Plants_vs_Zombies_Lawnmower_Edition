#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <spdlog/spdlog.h>
#include <string_view>

#include "game/managers/game_manager.hpp"

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

// 道具效果类型映射（与 game_manager.cpp 保持一致）
lawnmower::ItemEffectType ResolveItemEffectType(std::string_view effect) {
  if (effect == "heal") {
    return lawnmower::ITEM_EFFECT_HEAL;
  }
  if (effect == "exp") {
    return lawnmower::ITEM_EFFECT_EXP;
  }
  if (effect == "speed") {
    return lawnmower::ITEM_EFFECT_SPEED;
  }
  return lawnmower::ITEM_EFFECT_NONE;
}

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

  auto mark_player_low_freq_dirty = [](PlayerRuntime& runtime) {
    runtime.low_freq_dirty = true;
    runtime.dirty = true;
  };

  auto grant_exp = [&](PlayerRuntime& player, uint32_t exp_reward) {
    if (exp_reward == 0) {
      return;
    }
    player.state.set_exp(player.state.exp() + exp_reward);
    mark_player_low_freq_dirty(player);

    // 升级：允许单次击杀连升多级
    while (player.state.exp_to_next() > 0 &&
           player.state.exp() >= player.state.exp_to_next()) {
      player.state.set_exp(player.state.exp() - player.state.exp_to_next());
      player.state.set_level(player.state.level() + 1);

      const uint32_t next_exp = static_cast<uint32_t>(std::llround(
                                    player.state.exp_to_next() * 1.25)) +
                                25u;
      player.state.set_exp_to_next(std::max<uint32_t>(1, next_exp));
      player.pending_upgrade_count += 1;

      lawnmower::S2C_PlayerLevelUp evt;
      evt.set_player_id(player.state.player_id());
      evt.set_new_level(player.state.level());
      evt.set_exp_to_next(player.state.exp_to_next());
      level_ups->push_back(std::move(evt));
    }
  };

  // 玩家攻击（方案二）：自动锁定最近敌人生成射弹，方向独立于玩家朝向。
  const float projectile_speed = std::clamp(
      config_.projectile_speed > 0.0f ? config_.projectile_speed : 420.0f, 1.0f,
      5000.0f);
  const float projectile_radius = std::clamp(
      config_.projectile_radius > 0.0f ? config_.projectile_radius : 6.0f, 0.5f,
      128.0f);
  const double projectile_ttl_seconds =
      std::clamp(config_.projectile_ttl_seconds > 0.0f
                     ? static_cast<double>(config_.projectile_ttl_seconds)
                     : 2.5,
                 0.05, 30.0);
  const uint32_t projectile_ttl_ms = static_cast<uint32_t>(
      std::clamp(std::llround(projectile_ttl_seconds * 1000.0), 1LL, 30000LL));

  const uint32_t max_shots_per_tick =
      std::clamp(config_.projectile_max_shots_per_tick > 0
                     ? config_.projectile_max_shots_per_tick
                     : 4u,
                 1u, 64u);

  const double attack_min_interval = std::max(
      1e-3,
      config_.projectile_attack_min_interval_seconds > 0.0f
          ? static_cast<double>(config_.projectile_attack_min_interval_seconds)
          : kMinAttackIntervalSeconds);
  const double attack_max_interval = std::max(
      attack_min_interval,
      config_.projectile_attack_max_interval_seconds > 0.0f
          ? static_cast<double>(config_.projectile_attack_max_interval_seconds)
          : kMaxAttackIntervalSeconds);
  const double tick_interval_seconds =
      scene.tick_interval.count() > 0.0
          ? scene.tick_interval.count()
          : (config_.tick_rate > 0
                 ? 1.0 / static_cast<double>(config_.tick_rate)
                 : 1.0 / 60.0);
  const bool allow_catchup =
      dt_seconds <= tick_interval_seconds * 1.5;  // 轻微抖动允许补发

  auto rotation_dir = [](float rotation_deg) -> std::pair<float, float> {
    const float rad = rotation_deg * std::numbers::pi_v<float> / 180.0f;
    return {std::cos(rad), std::sin(rad)};
  };

  auto rotation_from_dir = [](float dir_x, float dir_y) -> float {
    if (std::abs(dir_x) < 1e-6f && std::abs(dir_y) < 1e-6f) {
      return 0.0f;
    }
    const float angle_rad = std::atan2(dir_y, dir_x);
    return angle_rad * 180.0f / std::numbers::pi_v<float>;
  };

  auto compute_projectile_origin =
      [&](const PlayerRuntime& player,
          float facing_dir_x) -> std::pair<float, float> {
    const float side = facing_dir_x >= 0.0f ? kProjectileMouthOffsetSide
                                            : -kProjectileMouthOffsetSide;
    return {player.state.position().x() + side,
            player.state.position().y() + kProjectileMouthOffsetUp};
  };

  auto find_nearest_enemy_id = [&](const PlayerRuntime& player) -> uint32_t {
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
  };

  auto resolve_locked_target =
      [&](PlayerRuntime& player) -> const EnemyRuntime* {
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
      const uint32_t nearest_id = find_nearest_enemy_id(player);
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
  };

  auto log_attack_dir_fallback = [&](PlayerRuntime& player, uint32_t target_id,
                                     const char* reason) {
    if (scene.tick <
        player.last_attack_dir_log_tick + kAttackDirFallbackLogIntervalTicks) {
      return;
    }
    player.last_attack_dir_log_tick = scene.tick;
    spdlog::debug("Projectile dir fallback: player={} target={} reason={}",
                  player.state.player_id(), target_id, reason);
  };

  // 低频射弹调试日志
  auto log_projectile_spawn = [&](PlayerRuntime& player, uint32_t projectile_id,
                                  const EnemyRuntime& target, float origin_x,
                                  float origin_y, float dir_x, float dir_y,
                                  float rotation) {
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
  };

  auto resolve_projectile_direction =
      [&](PlayerRuntime& player, const EnemyRuntime& target, float* out_dir_x,
          float* out_dir_y, float* out_rotation) -> bool {
    if (out_dir_x == nullptr || out_dir_y == nullptr ||
        out_rotation == nullptr) {
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
        log_attack_dir_fallback(player, target.state.enemy_id(),
                                "zero_dir_use_cached");
      } else {
        const auto [fallback_x, fallback_y] =
            rotation_dir(player.state.rotation());
        facing_dir_x = fallback_x;
        facing_dir_y = fallback_y;
        log_attack_dir_fallback(player, target.state.enemy_id(),
                                "zero_dir_use_player_rotation");
      }
    } else {
      const float inv_len = 1.0f / std::sqrt(facing_len_sq);
      facing_dir_x *= inv_len;
      facing_dir_y *= inv_len;
    }

    const auto [origin_x, origin_y] =
        compute_projectile_origin(player, facing_dir_x);
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
    *out_rotation = rotation_from_dir(dir_x, dir_y);
    player.has_attack_dir = true;
    player.last_attack_dir_x = dir_x;
    player.last_attack_dir_y = dir_y;
    player.last_attack_rotation = *out_rotation;
    return true;
  };

  auto spawn_projectile = [&](uint32_t owner_player_id, PlayerRuntime& player,
                              const EnemyRuntime& target, int32_t damage,
                              float dir_x, float dir_y, float rotation) {
    if (damage <= 0) {
      return;
    }
    const auto [start_x, start_y] = compute_projectile_origin(player, dir_x);

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
    proj.speed = projectile_speed;
    proj.damage = damage;
    proj.has_buff = player.state.has_buff();
    proj.buff_id = player.state.buff_id();
    proj.is_friendly = true;
    proj.remaining_seconds = projectile_ttl_seconds;

    scene.projectiles.emplace(proj.projectile_id, proj);
    log_projectile_spawn(player, proj.projectile_id, target, start_x, start_y,
                         dir_x, dir_y, rotation);

    lawnmower::ProjectileState spawn;
    spawn.set_projectile_id(proj.projectile_id);
    spawn.set_owner_player_id(owner_player_id);
    spawn.mutable_position()->set_x(start_x);
    spawn.mutable_position()->set_y(start_y);
    spawn.set_rotation(rotation);
    spawn.set_ttl_ms(projectile_ttl_ms);
    auto* meta = spawn.mutable_projectile();
    meta->set_speed(static_cast<uint32_t>(std::max(0.0f, proj.speed)));
    meta->set_has_buff(proj.has_buff);
    meta->set_buff_id(proj.buff_id);
    meta->set_is_friendly(proj.is_friendly);
    meta->set_damage(static_cast<uint32_t>(std::max<int32_t>(0, proj.damage)));
    projectile_spawns->push_back(std::move(spawn));
  };

  const uint32_t max_items_alive =
      items_config_.max_items_alive > 0 ? items_config_.max_items_alive : 64;

  std::vector<std::pair<uint32_t, uint32_t>> drop_candidates;
  drop_candidates.reserve(items_config_.items.size());
  uint32_t drop_weight_total = 0;
  for (const auto& [type_id, item] : items_config_.items) {
    if (ResolveItemEffectType(item.effect) != lawnmower::ITEM_EFFECT_HEAL) {
      continue;
    }
    if (item.drop_weight == 0) {
      continue;
    }
    drop_candidates.emplace_back(type_id, item.drop_weight);
    drop_weight_total += item.drop_weight;
  }

  auto pick_drop_type_id = [&]() -> uint32_t {
    if (drop_weight_total == 0) {
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
  };

  auto spawn_drop_item = [&](float x, float y, uint32_t type_id) {
    if (scene.items.size() >= max_items_alive) {
      return;
    }
    const ItemTypeConfig& type = ResolveItemType(type_id);
    const lawnmower::ItemEffectType effect_type =
        ResolveItemEffectType(type.effect);
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
    runtime.dirty = true;
    scene.items.emplace(runtime.item_id, runtime);

    auto& dropped = dropped_items->emplace_back();
    dropped.set_item_id(runtime.item_id);
    dropped.set_type_id(runtime.type_id);
    dropped.set_is_picked(false);
    dropped.set_effect_type(runtime.effect_type);
    dropped.mutable_position()->set_x(runtime.x);
    dropped.mutable_position()->set_y(runtime.y);
    *has_dirty = true;
  };

  auto try_drop_from_enemy = [&](const EnemyRuntime& enemy) {
    if (drop_weight_total == 0) {
      return;
    }
    const EnemyTypeConfig& type = ResolveEnemyType(enemy.state.type_id());
    const uint32_t chance = std::min(type.drop_chance, 100u);
    if (chance == 0) {
      return;
    }
    const float roll = NextRngUnitFloat(&scene.rng_state) * 100.0f;
    if (roll >= static_cast<float>(chance)) {
      return;
    }
    const uint32_t type_id = pick_drop_type_id();
    if (type_id == 0) {
      return;
    }
    spawn_drop_item(enemy.state.position().x(), enemy.state.position().y(),
                    type_id);
  };

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
    const EnemyRuntime* target = resolve_locked_target(player);
    if (target == nullptr || !resolve_projectile_direction(
                                 player, *target, &dir_x, &dir_y, &rotation)) {
      player.attack_cooldown_seconds =
          std::max(player.attack_cooldown_seconds, 0.0);
      continue;
    }

    const double interval = PlayerAttackIntervalSeconds(
        player.state.attack_speed(), attack_min_interval, attack_max_interval);
    uint32_t fired = 0;
    const uint32_t max_shots_this_tick =
        allow_catchup ? std::min<uint32_t>(max_shots_per_tick, 2u) : 1u;
    while (player.attack_cooldown_seconds <= 1e-6 &&
           fired < max_shots_this_tick) {
      player.attack_cooldown_seconds += interval;
      fired += 1;

      int32_t damage = std::max<int32_t>(1, player.state.attack());
      if (player.state.has_buff()) {
        damage = static_cast<int32_t>(std::llround(damage * 1.2));
      }

      if (player.state.critical_hit_rate() > 0) {
        const float chance = std::clamp(
            static_cast<float>(player.state.critical_hit_rate()) / 1000.0f,
            0.0f, 1.0f);
        if (NextRngUnitFloat(&scene.rng_state) < chance) {
          damage *= 2;
        }
      }

      spawn_projectile(player_id, player, *target, damage, dir_x, dir_y,
                       rotation);
    }
  }

  // 推进射弹并检测命中：方案二不做射弹逐帧同步，客户端收到 spawn 后本地模拟。
  const float map_w = static_cast<float>(scene.config.width);
  const float map_h = static_cast<float>(scene.config.height);

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

    if (proj.remaining_seconds <= 0.0) {
      despawn = true;
      reason = lawnmower::PROJECTILE_DESPAWN_EXPIRED;
    } else {
      const float combined_radius = projectile_radius + kEnemyCollisionRadius;
      float best_t = std::numeric_limits<float>::infinity();
      for (auto& [enemy_id, enemy] : scene.enemies) {
        if (!enemy.state.is_alive()) {
          continue;
        }
        const float ex = enemy.state.position().x();
        const float ey = enemy.state.position().y();
        float hit_t = 0.0f;
        if (!SegmentCircleOverlap(prev_x, prev_y, next_x, next_y, ex, ey,
                                  combined_radius, &hit_t)) {
          continue;
        }
        if (hit_t < best_t) {
          best_t = hit_t;
          hit_enemy = &enemy;
          hit_enemy_id = enemy_id;
        }
      }

      if (hit_enemy != nullptr) {
        const float hit_x = prev_x + (next_x - prev_x) * best_t;
        const float hit_y = prev_y + (next_y - prev_y) * best_t;
        proj.x = hit_x;
        proj.y = hit_y;
        despawn = true;
        reason = lawnmower::PROJECTILE_DESPAWN_HIT;

        const int32_t prev_hp = hit_enemy->state.health();
        const int32_t dealt =
            std::min(proj.damage, std::max<int32_t>(0, prev_hp));
        hit_enemy->state.set_health(
            std::max<int32_t>(0, prev_hp - proj.damage));
        hit_enemy->dirty = true;
        *has_dirty = true;

        auto owner_it = scene.players.find(proj.owner_player_id);
        if (owner_it != scene.players.end()) {
          owner_it->second.damage_dealt += dealt;
        }

        if (hit_enemy->state.health() <= 0) {
          hit_enemy->state.set_is_alive(false);
          if (hit_enemy->is_attacking ||
              hit_enemy->attack_target_player_id != 0) {
            hit_enemy->is_attacking = false;
            hit_enemy->attack_target_player_id = 0;
            lawnmower::EnemyAttackStateDelta delta;
            delta.set_enemy_id(hit_enemy->state.enemy_id());
            delta.set_is_attacking(false);
            delta.set_target_player_id(0);
            enemy_attack_states->push_back(std::move(delta));
          }
          hit_enemy->dead_elapsed_seconds = 0.0;
          hit_enemy->force_sync_left =
              std::max(hit_enemy->force_sync_left, kEnemySpawnForceSyncCount);
          hit_enemy->dirty = true;

          try_drop_from_enemy(*hit_enemy);

          lawnmower::S2C_EnemyDied died;
          died.set_enemy_id(hit_enemy->state.enemy_id());
          died.set_killer_player_id(proj.owner_player_id);
          died.set_wave_id(hit_enemy->state.wave_id());
          *died.mutable_position() = hit_enemy->state.position();
          enemy_dieds->push_back(std::move(died));

          if (owner_it != scene.players.end()) {
            owner_it->second.kill_count += 1;
            const uint32_t exp_reward = static_cast<uint32_t>(std::max<int32_t>(
                0, ResolveEnemyType(hit_enemy->state.type_id()).exp_reward));
            grant_exp(owner_it->second, exp_reward);
          }
        }
      } else if (proj.x < 0.0f || proj.y < 0.0f || proj.x > map_w ||
                 proj.y > map_h) {
        despawn = true;
        reason = lawnmower::PROJECTILE_DESPAWN_OUT_OF_BOUNDS;
      }
    }

    if (despawn) {
      lawnmower::ProjectileDespawn evt;
      evt.set_projectile_id(proj.projectile_id);
      evt.set_reason(reason);
      evt.set_hit_enemy_id(hit_enemy_id);
      evt.mutable_position()->set_x(proj.x);
      evt.mutable_position()->set_y(proj.y);
      projectile_despawns->push_back(std::move(evt));

      scene.projectile_pool.push_back(std::move(proj));
      it = scene.projectiles.erase(it);
    } else {
      ++it;
    }
  }

  // 敌人近战攻击（基于配置的进入/退出距离阈值，带迟滞）
  for (auto& [enemy_id, enemy] : scene.enemies) {
    if (!enemy.state.is_alive()) {
      continue;
    }

    const EnemyTypeConfig& type = ResolveEnemyType(enemy.state.type_id());
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

    const float enter_sq = attack_enter_radius * attack_enter_radius;
    const float exit_sq = attack_exit_radius * attack_exit_radius;

    const float ex = enemy.state.position().x();
    const float ey = enemy.state.position().y();

    auto push_attack_state = [&](bool attacking, uint32_t target_id) {
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
    };

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
      for (auto& [pid, player] : scene.players) {
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

    if (target_player_id == 0) {
      push_attack_state(false, 0);
      continue;
    }

    push_attack_state(true, target_player_id);

    auto player_it = scene.players.find(target_player_id);
    if (player_it == scene.players.end()) {
      continue;
    }
    PlayerRuntime& player = player_it->second;
    if (!player.state.is_alive()) {
      continue;
    }

    // 仍在攻击范围，但冷却未结束：更新 attack state 后不结算伤害
    if (enemy.attack_cooldown_seconds > 1e-6) {
      continue;
    }

    const int32_t damage = std::max<int32_t>(0, type.damage);
    const double attack_interval_seconds = std::clamp(
        type.attack_interval_seconds > 0.0f
            ? static_cast<double>(type.attack_interval_seconds)
            : kDefaultEnemyAttackIntervalSeconds,
        kMinEnemyAttackIntervalSeconds, kMaxEnemyAttackIntervalSeconds);
    enemy.attack_cooldown_seconds = attack_interval_seconds;

    // 伤害为 0
    // 时不产生受伤事件（避免客户端误触发受击表现），但仍可用攻击状态做动画。
    if (damage <= 0) {
      continue;
    }
    const int32_t prev_hp = player.state.health();
    const int32_t dealt = std::min(damage, std::max<int32_t>(0, prev_hp));

    player.state.set_health(std::max<int32_t>(0, prev_hp - damage));
    mark_player_low_freq_dirty(player);

    lawnmower::S2C_PlayerHurt hurt;
    hurt.set_player_id(target_player_id);
    hurt.set_damage(static_cast<uint32_t>(dealt));
    hurt.set_remaining_health(player.state.health());
    hurt.set_source_id(enemy_id);
    player_hurts->push_back(std::move(hurt));

    if (player.state.health() <= 0) {
      player.state.set_is_alive(false);
      player.wants_attacking = false;
      mark_player_low_freq_dirty(player);
    }

    *has_dirty = true;
  }

  // 游戏结束判断：所有玩家死亡则 GameOver
  const std::size_t alive_after_combat =
      std::count_if(scene.players.begin(), scene.players.end(),
                    [](const auto& kv) { return kv.second.state.is_alive(); });
  if (alive_after_combat == 0 && !scene.players.empty()) {
    scene.game_over = true;

    lawnmower::S2C_GameOver over;
    over.set_victory(false);
    over.set_survive_time(static_cast<uint32_t>(std::max(0.0, scene.elapsed)));
    for (const auto& [pid, player] : scene.players) {
      auto* score = over.add_scores();
      score->set_player_id(pid);
      score->set_player_name(player.player_name);
      score->set_final_level(static_cast<int32_t>(player.state.level()));
      score->set_kill_count(player.kill_count);
      score->set_damage_dealt(player.damage_dealt);
    }
    *game_over = std::move(over);
  }
}
