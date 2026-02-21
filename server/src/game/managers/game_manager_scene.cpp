#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"
#include "internal/game_manager_internal_utils.hpp"

namespace {
using game_manager_internal::FillSyncTiming;
using game_manager_internal::NowMs;

constexpr float kSpawnRadius = 120.0f;       // 生成半径
constexpr int32_t kDefaultMaxHealth = 100;   // 默认最大血量
constexpr uint32_t kDefaultAttack = 10;      // 默认攻击力
constexpr uint32_t kDefaultExpToNext = 100;  // 默认升级所需经验
}  // namespace

// 将坐标限制在地图边界内
lawnmower::Vector2 GameManager::ClampToMap(const SceneConfig& cfg, float x,
                                           float y) const {
  lawnmower::Vector2 pos;
  // clamp 功能是把第一个参数限制在[第二个参数，第三个参数]
  pos.set_x(std::clamp(x, 0.0f, static_cast<float>(cfg.width)));
  pos.set_y(std::clamp(y, 0.0f, static_cast<float>(cfg.height)));
  return pos;
}

// 放置玩家
void GameManager::PlacePlayers(const SceneCreateSnapshot& snapshot,
                               Scene* scene) {
  if (scene == nullptr) {
    return;
  }

  const std::size_t count = snapshot.players.size();  // 玩家数量
  if (count == 0) {
    return;
  }

  const float center_x =
      static_cast<float>(scene->config.width) * 0.5f;  // 计算中心x
  const float center_y =
      static_cast<float>(scene->config.height) * 0.5f;  // 计算中心y

  const auto resolve_default_role = [&]() -> const PlayerRoleConfig* {
    const uint32_t desired_role_id = player_roles_config_.default_role_id > 0
                                         ? player_roles_config_.default_role_id
                                         : 1u;

    auto it = player_roles_config_.roles.find(desired_role_id);
    if (it != player_roles_config_.roles.end()) {
      // 找到了
      return &it->second;
    }

    const PlayerRoleConfig* best = nullptr;
    uint32_t best_id = 0;
    // 找最小的role_id
    for (const auto& [role_id, cfg] : player_roles_config_.roles) {
      if (best == nullptr || role_id < best_id) {
        best = &cfg;
        best_id = role_id;
      }
    }
    return best;
  };

  const PlayerRoleConfig* default_role = resolve_default_role();

  // 遍历每一个玩家, 计算初始位置
  for (std::size_t i = 0; i < count; ++i) {
    const auto& player = snapshot.players[i];
    const float angle =
        (2.0f * std::numbers::pi_v<float> * static_cast<float>(i)) /
        static_cast<float>(count);

    // 计算实际x/y的位置
    const float x = center_x + std::cos(angle) * kSpawnRadius;
    const float y = center_y + std::sin(angle) * kSpawnRadius;
    const auto clamped_pos = ClampToMap(scene->config, x, y);  // 限制边界

    // 设置基本信息
    PlayerRuntime runtime;
    runtime.state.set_player_id(player.player_id);
    runtime.player_name = player.player_name.empty()
                              ? ("玩家" + std::to_string(player.player_id))
                              : player.player_name;
    runtime.state.mutable_position()->set_x(clamped_pos.x());
    runtime.state.mutable_position()->set_y(clamped_pos.y());
    runtime.state.set_rotation(angle * 180.0f / std::numbers::pi_v<float>);

    const int32_t max_health =
        default_role != nullptr ? std::max<int32_t>(1, default_role->max_health)
                                : kDefaultMaxHealth;
    const uint32_t attack =
        default_role != nullptr ? default_role->attack : kDefaultAttack;
    const uint32_t attack_speed =
        default_role != nullptr
            ? std::max<uint32_t>(1, default_role->attack_speed)
            : 1u;
    const float move_speed =
        default_role != nullptr && default_role->move_speed > 0.0f
            ? default_role->move_speed
            : scene->config.move_speed;
    const uint32_t crit =
        default_role != nullptr ? default_role->critical_hit_rate : 0u;
    const uint32_t role_id =
        default_role != nullptr ? default_role->role_id : 0u;

    runtime.state.set_health(max_health);
    runtime.state.set_max_health(max_health);
    runtime.state.set_level(1);
    runtime.state.set_exp(0);
    runtime.state.set_exp_to_next(kDefaultExpToNext);
    runtime.state.set_is_alive(true);
    runtime.state.set_attack(attack);
    runtime.state.set_is_friendly(true);
    runtime.state.set_role_id(role_id);
    runtime.state.set_critical_hit_rate(crit);
    runtime.state.set_has_buff(false);
    runtime.state.set_buff_id(0);
    runtime.state.set_attack_speed(attack_speed);
    runtime.state.set_move_speed(move_speed);
    runtime.state.set_last_processed_input_seq(0);
    runtime.pending_upgrade_count = 0;
    runtime.refresh_remaining = upgrade_config_.refresh_limit;
    // 初始化 delta 同步基线
    runtime.last_sync_position = runtime.state.position();
    runtime.last_sync_rotation = runtime.state.rotation();
    runtime.last_sync_is_alive = runtime.state.is_alive();
    runtime.last_sync_input_seq = runtime.last_input_seq;

    // 将玩家对应玩家信息插入会话
    scene->players.emplace(player.player_id, std::move(runtime));
    player_scene_[player.player_id] = snapshot.room_id;  // 增加玩家对应房间结构
  }
}

// 创建场景
lawnmower::SceneInfo GameManager::CreateScene(
    const SceneCreateSnapshot& snapshot) {
  StopGameLoop(snapshot.room_id);            // 清理旧的同步定时器
  std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁

  // 清理旧场景（防止重复开始游戏导致映射残留）
  auto existing = scenes_.find(snapshot.room_id);  // 房间对应会话map
  if (existing != scenes_.end()) {                 // 存在该会话
    for (const auto& [player_id, _] : existing->second.players) {
      player_scene_.erase(player_id);  // 玩家对应房间map
    }
    scenes_.erase(existing);  // 删除该会话
  }

  Scene scene;
  scene.config = BuildDefaultConfig();  // 构建默认配置
  scene.next_enemy_id = 1;
  scene.next_projectile_id = 1;
  scene.next_item_id = 1;
  scene.elapsed = 0.0;
  scene.spawn_elapsed = 0.0;
  scene.wave_id = 1;
  scene.game_over = false;
  scene.rng_state = snapshot.room_id ^ static_cast<uint32_t>(NowMs().count());
  if (scene.rng_state == 0) {
    scene.rng_state = 1;
  }
  ResetPerfStats(scene);

  scene.nav_cells_x = std::max(
      1,
      static_cast<int>((scene.config.width + kNavCellSize - 1) / kNavCellSize));
  scene.nav_cells_y =
      std::max(1, static_cast<int>((scene.config.height + kNavCellSize - 1) /
                                   kNavCellSize));
  const std::size_t nav_cells =
      static_cast<std::size_t>(scene.nav_cells_x * scene.nav_cells_y);
  scene.nav_came_from.assign(nav_cells, -1);
  scene.nav_g_score.assign(nav_cells, std::numeric_limits<float>::infinity());
  scene.nav_visit_epoch.assign(nav_cells, 0);
  scene.nav_closed_epoch.assign(nav_cells, 0);
  scene.nav_epoch = 0;

  const std::size_t max_enemies_alive =
      config_.max_enemies_alive > 0 ? config_.max_enemies_alive : 256;
  scene.players.reserve(snapshot.players.size());
  scene.enemies.reserve(max_enemies_alive);
  scene.enemy_pool.reserve(max_enemies_alive);
  scene.projectiles.reserve(max_enemies_alive);
  scene.projectile_pool.reserve(max_enemies_alive);
  const std::size_t max_items_alive =
      items_config_.max_items_alive > 0 ? items_config_.max_items_alive : 64;
  scene.items.reserve(max_items_alive);
  scene.item_pool.reserve(max_items_alive);
  scene.dirty_player_ids.reserve(snapshot.players.size());
  scene.dirty_enemy_ids.reserve(max_enemies_alive);
  scene.dirty_item_ids.reserve(max_items_alive);

  PlacePlayers(snapshot, &scene);  // 放置玩家

  // 生成敌人lambda
  auto spawn_enemy = [&](uint32_t type_id) {
    if (scene.enemies.size() >= max_enemies_alive) {
      return;
    }

    // 解析敌人类型
    const EnemyTypeConfig& type = ResolveEnemyType(type_id);

    const float map_w = static_cast<float>(scene.config.width);
    const float map_h = static_cast<float>(scene.config.height);
    const float t =
        NextRngUnitFloat(&scene.rng_state);  // 获取一个[0,1)的浮点随机值
    const uint32_t edge =
        NextRng(&scene.rng_state) % 4u;  // 获取一个0-3的随机值

    float x = 0.0f;
    float y = 0.0f;
    // 根据随机选中的地图边界生成敌人
    switch (edge) {
      case 0:  // left
        x = kEnemySpawnInset;
        y = t * map_h;
        break;
      case 1:  // right
        x = std::max(0.0f, map_w - kEnemySpawnInset);
        y = t * map_h;
        break;
      case 2:  // bottom
        x = t * map_w;
        y = kEnemySpawnInset;
        break;
      default:  // 3 - top
        x = t * map_w;
        y = std::max(0.0f, map_h - kEnemySpawnInset);
        break;
    }

    EnemyRuntime runtime;
    // 敌人运行时状态配置信息
    runtime.state.set_enemy_id(scene.next_enemy_id++);
    runtime.state.set_type_id(type.type_id);
    const auto clamped_pos = ClampToMap(scene.config, x, y);  // 限制边界
    runtime.state.mutable_position()->set_x(clamped_pos.x());
    runtime.state.mutable_position()->set_y(clamped_pos.y());
    runtime.state.set_health(type.max_health);
    runtime.state.set_max_health(type.max_health);
    runtime.state.set_is_alive(true);
    runtime.state.set_wave_id(scene.wave_id);
    runtime.state.set_is_friendly(false);
    // 初始化 delta 同步基线
    runtime.last_sync_position = runtime.state.position();
    runtime.last_sync_health = runtime.state.health();
    runtime.last_sync_is_alive = runtime.state.is_alive();
    runtime.force_sync_left = kEnemySpawnForceSyncCount;
    runtime.dirty = false;
    auto [it, _] =
        scene.enemies.emplace(runtime.state.enemy_id(), std::move(runtime));
    MarkEnemyDirty(scene, it->first, it->second);
  };

  // 初始敌人数量
  const std::size_t initial_enemy_count = std::min<std::size_t>(
      max_enemies_alive, std::max<std::size_t>(1, snapshot.players.size() * 2));
  for (std::size_t i = 0; i < initial_enemy_count; ++i) {
    // 生成敌人
    spawn_enemy(PickSpawnEnemyTypeId(&scene.rng_state));
  }
  scenes_[snapshot.room_id] = std::move(scene);  // 房间对应会话map

  lawnmower::SceneInfo scene_info;  // 场景信息
  // 设置必要信息
  scene_info.set_scene_id(snapshot.room_id);
  scene_info.set_width(scenes_[snapshot.room_id].config.width);
  scene_info.set_height(scenes_[snapshot.room_id].config.height);
  scene_info.set_tick_rate(scenes_[snapshot.room_id].config.tick_rate);
  scene_info.set_state_sync_rate(
      scenes_[snapshot.room_id].config.state_sync_rate);

  spdlog::info("创建场景: room_id={}, players={}", snapshot.room_id,
               snapshot.players.size());
  return scene_info;
}

// 构建完整的游戏状态
bool GameManager::BuildFullState(uint32_t room_id,
                                 lawnmower::S2C_GameStateSync* sync) {
  if (sync == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);     // 互斥锁
  const auto scene_it = scenes_.find(room_id);  // 房间对应会话map
  if (scene_it == scenes_.end()) {
    return false;
  }

  sync->Clear();
  // 填充同步时间
  FillSyncTiming(room_id, scene_it->second.tick, sync);

  const Scene& scene = scene_it->second;
  if (!scene.players.empty()) {
    sync->mutable_players()->Reserve(static_cast<int>(scene.players.size()));
  }
  if (!scene.enemies.empty()) {
    sync->mutable_enemies()->Reserve(static_cast<int>(scene.enemies.size()));
  }
  if (!scene.items.empty()) {
    sync->mutable_items()->Reserve(static_cast<int>(scene.items.size()));
  }
  // 全量同步包的玩家信息的state赋值
  for (const auto& [_, runtime] : scene.players) {
    auto* player_state = sync->add_players();
    *player_state = runtime.state;
    player_state->set_last_processed_input_seq(runtime.last_input_seq);
  }
  // 全量同步包的敌人信息的state赋值
  for (const auto& [_, runtime] : scene.enemies) {
    auto* enemy_state = sync->add_enemies();
    *enemy_state = runtime.state;
  }
  // 全量同步包的道具信息的state赋值
  for (const auto& [_, item] : scene.items) {
    if (item.is_picked) {
      continue;
    }
    auto* item_state = sync->add_items();
    item_state->set_item_id(item.item_id);
    item_state->set_type_id(item.type_id);
    item_state->set_is_picked(item.is_picked);
    item_state->mutable_position()->set_x(item.x);
    item_state->mutable_position()->set_y(item.y);
  }
  return true;
}

void GameManager::ProcessItems(Scene& scene, bool* has_dirty) {
  if (has_dirty == nullptr) {
    return;
  }

  const std::size_t alive_players =
      std::count_if(scene.players.begin(), scene.players.end(),
                    [](const auto& kv) { return kv.second.state.is_alive(); });
  if (alive_players == 0) {
    return;
  }

  auto mark_player_low_freq_dirty = [&](PlayerRuntime& runtime) {
    MarkPlayerDirty(scene, runtime.state.player_id(), runtime, true);
  };

  const float pick_radius =
      items_config_.pick_radius > 0.0f ? items_config_.pick_radius : 24.0f;
  const float pick_radius_sq = pick_radius * pick_radius;

  for (auto& [_, item] : scene.items) {
    if (item.is_picked) {
      continue;
    }
    for (auto& [_, player] : scene.players) {
      if (!player.state.is_alive()) {
        continue;
      }
      const float dx = player.state.position().x() - item.x;
      const float dy = player.state.position().y() - item.y;
      const float dist_sq = dx * dx + dy * dy;
      if (dist_sq > pick_radius_sq) {
        continue;
      }

      item.is_picked = true;
      MarkItemDirty(scene, item.item_id, item);
      *has_dirty = true;

      if (item.effect_type == lawnmower::ITEM_EFFECT_HEAL) {
        const ItemTypeConfig& type = ResolveItemType(item.type_id);
        const int32_t heal_value = std::max<int32_t>(0, type.value);
        if (heal_value > 0) {
          const int32_t prev_hp = player.state.health();
          const int32_t max_hp = player.state.max_health();
          const int32_t next_hp = std::min(max_hp, prev_hp + heal_value);
          if (next_hp != prev_hp) {
            player.state.set_health(next_hp);
            mark_player_low_freq_dirty(player);
          }
        }
      }

      break;
    }
  }
}
