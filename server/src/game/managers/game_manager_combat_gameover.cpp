#include <algorithm>
#include <optional>
#include <vector>

#include "game/managers/game_manager.hpp"

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
