#include <algorithm>
#include <cmath>
#include <limits>

#include "game/managers/game_manager.hpp"

namespace {
float DistanceSq(float ax, float ay, float bx, float by) {
  const float dx = ax - bx;
  const float dy = ay - by;
  return dx * dx + dy * dy;
}

constexpr float kPlayerCollisionRadius = 18.0f;
constexpr float kEnemyCollisionRadius = 16.0f;
constexpr double kDefaultEnemyAttackIntervalSeconds = 0.8;
constexpr double kMinEnemyAttackIntervalSeconds = 0.05;
constexpr double kMaxEnemyAttackIntervalSeconds = 10.0;
}  // namespace

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
