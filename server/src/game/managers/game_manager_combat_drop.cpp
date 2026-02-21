#include <algorithm>
#include <spdlog/spdlog.h>
#include <vector>

#include "game/managers/game_manager.hpp"
#include "internal/game_manager_misc_utils.hpp"

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
