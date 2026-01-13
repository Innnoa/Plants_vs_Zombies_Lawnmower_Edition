#include "game/managers/game_manager.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "game/entities/enemy_types.hpp"

namespace {
// 碰撞/战斗相关（后续可考虑挪到配置）
constexpr float kPlayerCollisionRadius = 18.0f;
constexpr float kEnemyCollisionRadius = 16.0f;
constexpr double kEnemyAttackIntervalSeconds = 0.8;  // 同一敌人对同一玩家的伤害间隔

// 默认射速 clamp（若配置缺失/非法则回退）
constexpr double kMinAttackIntervalSeconds = 0.05;  // 射速上限（最小间隔，秒）
constexpr double kMaxAttackIntervalSeconds = 2.0;   // 射速下限（最大间隔，秒）

float DistanceSq(float ax, float ay, float bx, float by) {
  const float dx = ax - bx;
  const float dy = ay - by;
  return dx * dx + dy * dy;
}

bool CirclesOverlap(float ax, float ay, float ar, float bx, float by, float br) {
  const float r = ar + br;
  return DistanceSq(ax, ay, bx, by) <= r * r;
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
    std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
    std::optional<lawnmower::S2C_GameOver>* game_over,
    std::vector<lawnmower::ProjectileState>* projectile_spawns,
    std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
    bool* has_dirty) {
  if (player_hurts == nullptr || enemy_dieds == nullptr || level_ups == nullptr ||
      game_over == nullptr || projectile_spawns == nullptr ||
      projectile_despawns == nullptr || has_dirty == nullptr) {
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

      const uint32_t next_exp =
          static_cast<uint32_t>(std::llround(player.state.exp_to_next() * 1.25)) +
          25u;
      player.state.set_exp_to_next(std::max<uint32_t>(1, next_exp));

      player.state.set_max_health(player.state.max_health() + 10);
      player.state.set_health(player.state.max_health());
      player.state.set_attack(player.state.attack() + 2);

      lawnmower::S2C_PlayerLevelUp evt;
      evt.set_player_id(player.state.player_id());
      evt.set_new_level(player.state.level());
      evt.set_exp_to_next(player.state.exp_to_next());
      level_ups->push_back(std::move(evt));
    }
  };

  // 玩家攻击（方案二）：生成射弹并广播 spawn，射弹碰撞由服务端权威结算。
  const float projectile_speed = std::clamp(
      config_.projectile_speed > 0.0f ? config_.projectile_speed : 420.0f, 1.0f,
      5000.0f);
  const float projectile_radius = std::clamp(
      config_.projectile_radius > 0.0f ? config_.projectile_radius : 6.0f, 0.5f,
      128.0f);
  const float projectile_muzzle_offset = std::clamp(
      config_.projectile_muzzle_offset >= 0.0f ? config_.projectile_muzzle_offset
                                               : 22.0f,
      0.0f, 256.0f);
  const double projectile_ttl_seconds = std::clamp(
      config_.projectile_ttl_seconds > 0.0f
          ? static_cast<double>(config_.projectile_ttl_seconds)
          : 2.5,
      0.05, 30.0);
  const uint32_t projectile_ttl_ms = static_cast<uint32_t>(
      std::clamp(std::llround(projectile_ttl_seconds * 1000.0), 1LL, 30000LL));

  const uint32_t max_shots_per_tick = std::clamp(
      config_.projectile_max_shots_per_tick > 0
          ? config_.projectile_max_shots_per_tick
          : 4u,
      1u, 64u);

  const double attack_min_interval = std::max(
      1e-3, config_.projectile_attack_min_interval_seconds > 0.0f
                ? static_cast<double>(
                      config_.projectile_attack_min_interval_seconds)
                : kMinAttackIntervalSeconds);
  const double attack_max_interval = std::max(
      attack_min_interval,
      config_.projectile_attack_max_interval_seconds > 0.0f
          ? static_cast<double>(config_.projectile_attack_max_interval_seconds)
          : kMaxAttackIntervalSeconds);

  auto rotation_dir = [](float rotation_deg) -> std::pair<float, float> {
    const float rad = rotation_deg * std::numbers::pi_v<float> / 180.0f;
    return {std::cos(rad), std::sin(rad)};
  };

  auto spawn_projectile = [&](uint32_t owner_player_id, PlayerRuntime& player,
                              int32_t damage) {
    if (damage <= 0) {
      return;
    }
    const float rotation = player.state.rotation();
    const auto [dir_x, dir_y] = rotation_dir(rotation);
    const float start_x =
        player.state.position().x() + dir_x * projectile_muzzle_offset;
    const float start_y =
        player.state.position().y() + dir_y * projectile_muzzle_offset;

    ProjectileRuntime proj;
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

  // 开火：attack_speed 控制射速；dt 被 clamp 后最多补几发，避免掉帧时 DPS 丢失。
  for (auto& [player_id, player] : scene.players) {
    if (!player.state.is_alive() || !player.wants_attacking) {
      continue;
    }

    const double interval =
        PlayerAttackIntervalSeconds(player.state.attack_speed(),
                                    attack_min_interval, attack_max_interval);
    uint32_t fired = 0;
    while (player.attack_cooldown_seconds <= 1e-6 && fired < max_shots_per_tick) {
      player.attack_cooldown_seconds += interval;
      fired += 1;

      int32_t damage = std::max<int32_t>(1, player.state.attack());
      if (player.state.has_buff()) {
        damage = static_cast<int32_t>(std::llround(damage * 1.2));
      }

      if (player.state.critical_hit_rate() > 0) {
        const float chance =
            std::clamp(static_cast<float>(player.state.critical_hit_rate()) /
                           1000.0f,
                       0.0f, 1.0f);
        if (NextRngUnitFloat(&scene.rng_state) < chance) {
          damage *= 2;
        }
      }

      spawn_projectile(player_id, player, damage);
    }
  }

  // 推进射弹并检测命中：方案二不做射弹逐帧同步，客户端收到 spawn 后本地模拟。
  const float map_w = static_cast<float>(scene.config.width);
  const float map_h = static_cast<float>(scene.config.height);

  for (auto it = scene.projectiles.begin(); it != scene.projectiles.end();) {
    ProjectileRuntime& proj = it->second;
    proj.remaining_seconds -= dt_seconds;
    proj.x += proj.dir_x * proj.speed * static_cast<float>(std::max(0.0, dt_seconds));
    proj.y += proj.dir_y * proj.speed * static_cast<float>(std::max(0.0, dt_seconds));

    bool despawn = false;
    lawnmower::ProjectileDespawnReason reason =
        lawnmower::PROJECTILE_DESPAWN_UNKNOWN;
    uint32_t hit_enemy_id = 0;

    if (proj.remaining_seconds <= 0.0) {
      despawn = true;
      reason = lawnmower::PROJECTILE_DESPAWN_EXPIRED;
    } else if (proj.x < 0.0f || proj.y < 0.0f || proj.x > map_w ||
               proj.y > map_h) {
      despawn = true;
      reason = lawnmower::PROJECTILE_DESPAWN_OUT_OF_BOUNDS;
    } else {
      for (auto& [enemy_id, enemy] : scene.enemies) {
        if (!enemy.state.is_alive()) {
          continue;
        }
        const float ex = enemy.state.position().x();
        const float ey = enemy.state.position().y();
        if (!CirclesOverlap(proj.x, proj.y, projectile_radius, ex, ey,
                            kEnemyCollisionRadius)) {
          continue;
        }

        hit_enemy_id = enemy_id;
        despawn = true;
        reason = lawnmower::PROJECTILE_DESPAWN_HIT;

        const int32_t prev_hp = enemy.state.health();
        const int32_t dealt = std::min(proj.damage, std::max<int32_t>(0, prev_hp));
        enemy.state.set_health(std::max<int32_t>(0, prev_hp - proj.damage));
        enemy.dirty = true;
        *has_dirty = true;

        auto owner_it = scene.players.find(proj.owner_player_id);
        if (owner_it != scene.players.end()) {
          owner_it->second.damage_dealt += dealt;
        }

        if (enemy.state.health() <= 0) {
          enemy.state.set_is_alive(false);
          enemy.dead_elapsed_seconds = 0.0;
          enemy.force_sync_left =
              std::max(enemy.force_sync_left, kEnemySpawnForceSyncCount);
          enemy.dirty = true;

          lawnmower::S2C_EnemyDied died;
          died.set_enemy_id(enemy.state.enemy_id());
          died.set_killer_player_id(proj.owner_player_id);
          died.set_wave_id(enemy.state.wave_id());
          *died.mutable_position() = enemy.state.position();
          enemy_dieds->push_back(std::move(died));

          if (owner_it != scene.players.end()) {
            owner_it->second.kill_count += 1;
            const EnemyType* type = FindEnemyType(enemy.state.type_id());
            const uint32_t exp_reward = static_cast<uint32_t>(
                std::max<int32_t>(0, type != nullptr ? type->exp_reward
                                                    : DefaultEnemyType().exp_reward));
            grant_exp(owner_it->second, exp_reward);
          }
        }

        break;  // 单体射弹：命中一个目标即消失
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

      it = scene.projectiles.erase(it);
    } else {
      ++it;
    }
  }

  // 敌人接触伤害（范围：敌人与玩家碰撞）
  for (auto& [enemy_id, enemy] : scene.enemies) {
    if (!enemy.state.is_alive()) {
      continue;
    }
    if (enemy.attack_cooldown_seconds > 1e-6) {
      continue;
    }

    const float ex = enemy.state.position().x();
    const float ey = enemy.state.position().y();

    uint32_t best_player_id = 0;
    float best_dist_sq = std::numeric_limits<float>::infinity();
    for (auto& [pid, player] : scene.players) {
      if (!player.state.is_alive()) {
        continue;
      }
      const float px = player.state.position().x();
      const float py = player.state.position().y();
      if (!CirclesOverlap(px, py, kPlayerCollisionRadius, ex, ey,
                          kEnemyCollisionRadius)) {
        continue;
      }
      const float dist_sq = DistanceSq(px, py, ex, ey);
      if (dist_sq < best_dist_sq) {
        best_dist_sq = dist_sq;
        best_player_id = pid;
      }
    }

    if (best_player_id == 0) {
      continue;
    }

    auto player_it = scene.players.find(best_player_id);
    if (player_it == scene.players.end()) {
      continue;
    }
    PlayerRuntime& player = player_it->second;
    if (!player.state.is_alive()) {
      continue;
    }

    const EnemyType* type = FindEnemyType(enemy.state.type_id());
    const int32_t damage =
        std::max<int32_t>(1, type != nullptr ? type->damage
                                            : DefaultEnemyType().damage);
    const int32_t prev_hp = player.state.health();
    const int32_t dealt = std::min(damage, std::max<int32_t>(0, prev_hp));

    player.state.set_health(std::max<int32_t>(0, prev_hp - damage));
    mark_player_low_freq_dirty(player);

    lawnmower::S2C_PlayerHurt hurt;
    hurt.set_player_id(best_player_id);
    hurt.set_damage(static_cast<uint32_t>(dealt));
    hurt.set_remaining_health(player.state.health());
    hurt.set_source_id(enemy_id);
    player_hurts->push_back(std::move(hurt));

    if (player.state.health() <= 0) {
      player.state.set_is_alive(false);
      player.wants_attacking = false;
      mark_player_low_freq_dirty(player);
    }

    enemy.attack_cooldown_seconds = kEnemyAttackIntervalSeconds;
    *has_dirty = true;
  }

  // 游戏结束判断：所有玩家死亡则 GameOver
  const std::size_t alive_after_combat = std::count_if(
      scene.players.begin(), scene.players.end(),
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

