#include <cmath>
#include <vector>

#include "game/managers/game_manager.hpp"
#include "internal/game_manager_internal_utils.hpp"

namespace {
using game_manager_internal::FillDeltaTiming;
using game_manager_internal::FillSyncTiming;

constexpr float kDeltaPositionEpsilon = 1e-4f;  // delta 位置/朝向变化阈值
}  // namespace

void GameManager::FillPlayerHighFreq(const PlayerRuntime& runtime,
                                     lawnmower::PlayerState* out) {
  if (out == nullptr) {
    return;
  }
  out->Clear();
  out->set_player_id(runtime.state.player_id());
  out->set_rotation(runtime.state.rotation());
  out->set_is_alive(runtime.state.is_alive());
  out->set_last_processed_input_seq(runtime.last_input_seq);
  // 因 position 是自定义类型，没有 set 函数，故直接赋值
  *out->mutable_position() = runtime.state.position();
}

void GameManager::FillPlayerForSync(PlayerRuntime& runtime,
                                    lawnmower::PlayerState* out) {
  if (out == nullptr) {
    return;
  }
  // 是否发生低频全量变化
  if (runtime.low_freq_dirty) {
    // 全量状态
    *out = runtime.state;
    out->set_last_processed_input_seq(runtime.last_input_seq);
  } else {
    // 只填充高频字段
    FillPlayerHighFreq(runtime, out);
  }
}

bool GameManager::PositionChanged(const lawnmower::Vector2& current,
                                  const lawnmower::Vector2& last) {
  // 如果当前位移与上次位移的绝对值大于可接受位移最小值，则为位置变化
  return std::abs(current.x() - last.x()) > kDeltaPositionEpsilon ||
         std::abs(current.y() - last.y()) > kDeltaPositionEpsilon;
}

bool GameManager::PositionChanged(float current_x, float current_y,
                                  float last_x, float last_y) {
  return std::abs(current_x - last_x) > kDeltaPositionEpsilon ||
         std::abs(current_y - last_y) > kDeltaPositionEpsilon;
}

void GameManager::UpdatePlayerLastSync(PlayerRuntime& runtime) {
  runtime.last_sync_position = runtime.state.position();
  runtime.last_sync_rotation = runtime.state.rotation();
  runtime.last_sync_is_alive = runtime.state.is_alive();
  runtime.last_sync_input_seq = runtime.last_input_seq;
}

void GameManager::UpdateEnemyLastSync(EnemyRuntime& runtime) {
  runtime.last_sync_position = runtime.state.position();
  runtime.last_sync_health = runtime.state.health();
  runtime.last_sync_is_alive = runtime.state.is_alive();
}

void GameManager::UpdateItemLastSync(ItemRuntime& runtime) {
  runtime.last_sync_x = runtime.x;
  runtime.last_sync_y = runtime.y;
  runtime.last_sync_is_picked = runtime.is_picked;
  runtime.last_sync_type_id = runtime.type_id;
}

void GameManager::MarkPlayerDirty(Scene& scene, uint32_t player_id,
                                  PlayerRuntime& runtime, bool low_freq) {
  if (low_freq) {
    runtime.low_freq_dirty = true;
  }
  runtime.dirty = true;
  if (!runtime.dirty_queued) {
    scene.dirty_player_ids.push_back(player_id);
    runtime.dirty_queued = true;
  }
}

void GameManager::MarkEnemyDirty(Scene& scene, uint32_t enemy_id,
                                 EnemyRuntime& runtime) {
  runtime.dirty = true;
  if (!runtime.dirty_queued) {
    scene.dirty_enemy_ids.push_back(enemy_id);
    runtime.dirty_queued = true;
  }
}

void GameManager::MarkItemDirty(Scene& scene, uint32_t item_id,
                                ItemRuntime& runtime) {
  runtime.dirty = true;
  if (!runtime.dirty_queued) {
    scene.dirty_item_ids.push_back(item_id);
    runtime.dirty_queued = true;
  }
}

void GameManager::BuildSyncPayloadsLocked(
    uint32_t room_id, Scene& scene, bool force_full_sync,
    const std::vector<uint32_t>& dirty_player_ids,
    const std::vector<uint32_t>& dirty_enemy_ids,
    const std::vector<uint32_t>& dirty_item_ids,
    lawnmower::S2C_GameStateSync* sync,
    lawnmower::S2C_GameStateDeltaSync* delta, bool* built_sync,
    bool* built_delta, uint32_t* perf_delta_items_size,
    uint32_t* perf_sync_items_size) {
  if (sync == nullptr || delta == nullptr || built_sync == nullptr ||
      built_delta == nullptr || perf_delta_items_size == nullptr ||
      perf_sync_items_size == nullptr) {
    return;
  }

  *perf_delta_items_size = 0;
  *perf_sync_items_size = 0;
  std::vector<uint32_t> items_to_remove;
  items_to_remove.reserve(scene.items.size());
  // 减少同步热路径中的 unordered_set::erase 抖动：
  // 本帧统一批量 clear，仅把需要续留脏状态的对象回填。
  std::vector<uint32_t> next_dirty_enemy_ids;
  next_dirty_enemy_ids.reserve(dirty_enemy_ids.size());
  if (force_full_sync) {
    FillSyncTiming(room_id, scene.tick, sync);
    sync->set_is_full_snapshot(true);
    if (!scene.players.empty()) {
      sync->mutable_players()->Reserve(static_cast<int>(scene.players.size()));
    }
    if (!scene.enemies.empty()) {
      sync->mutable_enemies()->Reserve(static_cast<int>(scene.enemies.size()));
    }
    if (!scene.items.empty()) {
      sync->mutable_items()->Reserve(static_cast<int>(scene.items.size()));
    }
    for (auto& [_, runtime] : scene.players) {
      FillPlayerForSync(runtime, sync->add_players());
      UpdatePlayerLastSync(runtime);
      runtime.dirty = false;
      runtime.low_freq_dirty = false;
      runtime.dirty_queued = false;
    }
    for (auto& [_, enemy] : scene.enemies) {
      auto* out = sync->add_enemies();
      *out = enemy.state;
      UpdateEnemyLastSync(enemy);
      enemy.dirty = false;
      enemy.dirty_queued = false;
      if (enemy.force_sync_left > 0) {
        enemy.force_sync_left -= 1;
      }
    }
    for (auto& [_, item] : scene.items) {
      if (item.is_picked) {
        item.dirty_queued = false;
        items_to_remove.push_back(item.item_id);
        continue;
      }
      auto* out = sync->add_items();
      out->set_item_id(item.item_id);
      out->set_type_id(item.type_id);
      out->set_is_picked(item.is_picked);
      out->mutable_position()->set_x(item.x);
      out->mutable_position()->set_y(item.y);
      UpdateItemLastSync(item);
      item.dirty = false;
      item.dirty_queued = false;
      item.force_sync_left = 0;
    }
    *perf_sync_items_size = static_cast<uint32_t>(sync->items_size());
    *built_sync = true;
    scene.full_sync_elapsed = 0.0;
    scene.dirty_player_ids.clear();
    scene.dirty_enemy_ids.clear();
    scene.dirty_item_ids.clear();
  } else {
    bool sync_inited = false;
    bool delta_inited = false;
    if (!scene.players.empty()) {
      delta->mutable_players()->Reserve(static_cast<int>(scene.players.size()));
    }
    if (!scene.enemies.empty()) {
      delta->mutable_enemies()->Reserve(static_cast<int>(scene.enemies.size()));
    }
    if (!scene.items.empty()) {
      delta->mutable_items()->Reserve(static_cast<int>(scene.items.size()));
    }

    for (const auto player_id : dirty_player_ids) {
      auto it = scene.players.find(player_id);
      if (it == scene.players.end()) {
        continue;
      }
      PlayerRuntime& runtime = it->second;
      runtime.dirty_queued = false;
      if (!runtime.dirty && !runtime.low_freq_dirty) {
        continue;
      }
      if (runtime.low_freq_dirty) {
        if (!sync_inited) {
          FillSyncTiming(room_id, scene.tick, sync);
          sync->set_is_full_snapshot(false);
          sync_inited = true;
        }
        FillPlayerForSync(runtime, sync->add_players());
        *built_sync = true;
        UpdatePlayerLastSync(runtime);
        runtime.dirty = false;
        runtime.low_freq_dirty = false;
        continue;
      }
      uint32_t changed_mask = 0;
      const auto& position = runtime.state.position();
      if (PositionChanged(position, runtime.last_sync_position)) {
        changed_mask |= lawnmower::PLAYER_DELTA_POSITION;
      }
      if (std::abs(runtime.state.rotation() - runtime.last_sync_rotation) >
          kDeltaPositionEpsilon) {
        changed_mask |= lawnmower::PLAYER_DELTA_ROTATION;
      }
      if (runtime.state.is_alive() != runtime.last_sync_is_alive) {
        changed_mask |= lawnmower::PLAYER_DELTA_IS_ALIVE;
      }
      if (runtime.last_input_seq != runtime.last_sync_input_seq) {
        changed_mask |= lawnmower::PLAYER_DELTA_LAST_PROCESSED_INPUT_SEQ;
      }
      if (changed_mask == 0) {
        runtime.dirty = false;
        continue;
      }
      if (!delta_inited) {
        FillDeltaTiming(room_id, scene.tick, delta);
        delta_inited = true;
      }
      auto* out = delta->add_players();
      out->set_player_id(runtime.state.player_id());
      out->set_changed_mask(changed_mask);
      if ((changed_mask & lawnmower::PLAYER_DELTA_POSITION) != 0) {
        *out->mutable_position() = position;
      }
      if ((changed_mask & lawnmower::PLAYER_DELTA_ROTATION) != 0) {
        out->set_rotation(runtime.state.rotation());
      }
      if ((changed_mask & lawnmower::PLAYER_DELTA_IS_ALIVE) != 0) {
        out->set_is_alive(runtime.state.is_alive());
      }
      if ((changed_mask & lawnmower::PLAYER_DELTA_LAST_PROCESSED_INPUT_SEQ) !=
          0) {
        out->set_last_processed_input_seq(
            static_cast<int32_t>(runtime.last_input_seq));
      }
      *built_delta = true;
      UpdatePlayerLastSync(runtime);
      runtime.dirty = false;
    }

    for (const auto enemy_id : dirty_enemy_ids) {
      auto it = scene.enemies.find(enemy_id);
      if (it == scene.enemies.end()) {
        continue;
      }
      EnemyRuntime& enemy = it->second;
      enemy.dirty_queued = false;
      if (!enemy.dirty && enemy.force_sync_left == 0) {
        continue;
      }
      if (enemy.force_sync_left > 0) {
        if (!sync_inited) {
          FillSyncTiming(room_id, scene.tick, sync);
          sync->set_is_full_snapshot(false);
          sync_inited = true;
        }
        auto* out = sync->add_enemies();
        *out = enemy.state;
        *built_sync = true;
        UpdateEnemyLastSync(enemy);
        enemy.dirty = false;
        enemy.force_sync_left -= 1;
        if (enemy.force_sync_left > 0) {
          next_dirty_enemy_ids.push_back(enemy_id);
          enemy.dirty_queued = true;
        }
        continue;
      }
      uint32_t changed_mask = 0;
      const auto& position = enemy.state.position();
      if (PositionChanged(position, enemy.last_sync_position)) {
        changed_mask |= lawnmower::ENEMY_DELTA_POSITION;
      }
      if (enemy.state.health() != enemy.last_sync_health) {
        changed_mask |= lawnmower::ENEMY_DELTA_HEALTH;
      }
      if (enemy.state.is_alive() != enemy.last_sync_is_alive) {
        changed_mask |= lawnmower::ENEMY_DELTA_IS_ALIVE;
      }
      if (changed_mask == 0) {
        enemy.dirty = false;
        continue;
      }
      if (!delta_inited) {
        FillDeltaTiming(room_id, scene.tick, delta);
        delta_inited = true;
      }
      auto* out = delta->add_enemies();
      out->set_enemy_id(enemy.state.enemy_id());
      out->set_changed_mask(changed_mask);
      if ((changed_mask & lawnmower::ENEMY_DELTA_POSITION) != 0) {
        *out->mutable_position() = position;
      }
      if ((changed_mask & lawnmower::ENEMY_DELTA_HEALTH) != 0) {
        out->set_health(enemy.state.health());
      }
      if ((changed_mask & lawnmower::ENEMY_DELTA_IS_ALIVE) != 0) {
        out->set_is_alive(enemy.state.is_alive());
      }
      *built_delta = true;
      UpdateEnemyLastSync(enemy);
      enemy.dirty = false;
    }

    for (const auto item_id : dirty_item_ids) {
      auto it = scene.items.find(item_id);
      if (it == scene.items.end()) {
        continue;
      }
      ItemRuntime& item = it->second;
      item.dirty_queued = false;
      if (!item.dirty) {
        continue;
      }
      uint32_t changed_mask = 0;
      if (item.force_sync_left > 0) {
        changed_mask |= lawnmower::ITEM_DELTA_POSITION;
        changed_mask |= lawnmower::ITEM_DELTA_IS_PICKED;
        changed_mask |= lawnmower::ITEM_DELTA_TYPE;
      } else {
        if (PositionChanged(item.x, item.y, item.last_sync_x,
                            item.last_sync_y)) {
          changed_mask |= lawnmower::ITEM_DELTA_POSITION;
        }
        if (item.is_picked != item.last_sync_is_picked) {
          changed_mask |= lawnmower::ITEM_DELTA_IS_PICKED;
        }
        if (item.type_id != item.last_sync_type_id) {
          changed_mask |= lawnmower::ITEM_DELTA_TYPE;
        }
      }
      if (changed_mask == 0) {
        item.dirty = false;
        continue;
      }
      if (!delta_inited) {
        FillDeltaTiming(room_id, scene.tick, delta);
        delta_inited = true;
      }
      auto* out = delta->add_items();
      out->set_item_id(item.item_id);
      out->set_changed_mask(changed_mask);
      if ((changed_mask & lawnmower::ITEM_DELTA_POSITION) != 0) {
        out->mutable_position()->set_x(item.x);
        out->mutable_position()->set_y(item.y);
      }
      if ((changed_mask & lawnmower::ITEM_DELTA_IS_PICKED) != 0) {
        out->set_is_picked(item.is_picked);
      }
      if ((changed_mask & lawnmower::ITEM_DELTA_TYPE) != 0) {
        out->set_type_id(item.type_id);
      }
      *built_delta = true;
      UpdateItemLastSync(item);
      item.dirty = false;
      if (item.force_sync_left > 0) {
        item.force_sync_left -= 1;
      }
      if (item.is_picked) {
        items_to_remove.push_back(item.item_id);
      }
    }
    if (delta_inited) {
      *perf_delta_items_size = static_cast<uint32_t>(delta->items_size());
    }
  }

  if (!items_to_remove.empty()) {
    for (const auto item_id : items_to_remove) {
      auto item_it = scene.items.find(item_id);
      if (item_it == scene.items.end()) {
        continue;
      }
      item_it->second.dirty_queued = false;
      scene.item_pool.push_back(std::move(item_it->second));
      scene.items.erase(item_it);
    }
  }
  if (!force_full_sync) {
    scene.dirty_player_ids.clear();
    scene.dirty_item_ids.clear();
    scene.dirty_enemy_ids.swap(next_dirty_enemy_ids);
  }
}
