#pragma once

#include <asio/steady_timer.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config/enemy_types_config.hpp"
#include "config/item_types_config.hpp"
#include "config/player_roles_config.hpp"
#include "config/server_config.hpp"
#include "game/managers/room_manager.hpp"
#include "message.pb.h"

// 游戏管理器：负责场景初始化、玩家状态更新与同步
class UdpServer;

class GameManager {
 public:
  static GameManager& Instance();

  GameManager(const GameManager&) = delete;
  GameManager& operator=(const GameManager&) = delete;

  // 为指定房间创建场景并生成初始 SceneInfo（覆盖已存在的同房间场景）
  lawnmower::SceneInfo CreateScene(const RoomManager::RoomSnapshot& snapshot);

  // 构造完整的状态同步（通常用于游戏开始时的全量同步）
  [[nodiscard]] bool BuildFullState(uint32_t room_id,
                                    lawnmower::S2C_GameStateSync* sync);

  // 注册 io_context（用于定时广播状态同步）
  void SetIoContext(asio::io_context* io);

  // 注册 UDP 服务（用于高频同步）
  void SetUdpServer(UdpServer* udp);
  [[nodiscard]] UdpServer* GetUdpServer() const { return udp_server_; }
  [[nodiscard]] asio::io_context* GetIoContext() const { return io_context_; }
  void SetConfig(const ServerConfig& cfg) { config_ = cfg; }
  void SetPlayerRolesConfig(const PlayerRolesConfig& cfg) {
    player_roles_config_ = cfg;
  }
  void SetEnemyTypesConfig(const EnemyTypesConfig& cfg) {
    enemy_types_config_ = cfg;
  }
  void SetItemsConfig(const ItemsConfig& cfg) { items_config_ = cfg; }

  // 在游戏开始后为房间启动固定逻辑帧循环与状态同步
  void StartGameLoop(uint32_t room_id);

  // 处理玩家输入，将其入队等待逻辑帧处理；返回 false 表示未找到玩家或场景
  [[nodiscard]] bool HandlePlayerInput(uint32_t player_id,
                                       const lawnmower::C2S_PlayerInput& input,
                                       uint32_t* room_id);

  // 判断给定坐标是否在指定房间的地图边界内（基于场景宽高）
  [[nodiscard]] bool IsInsideMap(uint32_t room_id,
                                 const lawnmower::Vector2& position) const;

  // 玩家断线/离开时清理场景信息
  void RemovePlayer(uint32_t player_id);

 private:
  GameManager() = default;

  struct SceneConfig {  // 默认场景配置
    uint32_t width = 2000;
    uint32_t height = 2000;
    uint32_t tick_rate = 60;
    uint32_t state_sync_rate = 30;
    float move_speed = 200.0f;
  };

  // 玩家运行时状态
  struct PlayerRuntime {
    lawnmower::PlayerState state; // 玩家状态
    std::string player_name; // 玩家名
    uint32_t last_input_seq = 0; // 已处理的最新输入序号
    // last
    lawnmower::Vector2 last_sync_position;  // delta 同步基线位置
    float last_sync_rotation = 0.0f;        // delta 同步基线朝向
    bool last_sync_is_alive = true;         // delta 同步基线存活状态
    uint32_t last_sync_input_seq = 0;       // delta 同步基线输入序号

    std::deque<lawnmower::C2S_PlayerInput> pending_inputs; // 待处理输入队列
    bool wants_attacking = false; // 攻击意图
    double attack_cooldown_seconds = 0.0; // 攻击冷却剩余
    uint32_t locked_target_enemy_id = 0; // 锁定目标
    double target_refresh_elapsed = 0.0; // 目标刷新计时
    bool has_attack_dir = false; // 是否有攻击方向
    float last_attack_dir_x = 1.0f; // 最近攻击方向x向量
    float last_attack_dir_y = 0.0f; // 最近攻击方向y向量
    float last_attack_rotation = 0.0f; // 最近攻击朝向角
    uint64_t last_attack_dir_log_tick = 0; // 最近记录攻击方向的tick
    uint64_t last_projectile_spawn_log_tick = 0; // 最近记录射弹生成信息的tick
    int32_t kill_count = 0; // 击杀数
    int32_t damage_dealt = 0; // 伤害总量
    bool low_freq_dirty = false; // 低频/全量同步字段变化标记
    bool dirty = false; // 高频同步字段变化标记
  };

  // 敌人运行时状态
  struct EnemyRuntime {
    lawnmower::EnemyState state;            // 要同步给客户端的敌人基础状态
    uint32_t target_player_id = 0;          // 寻路/追踪时的目标玩家id
    std::vector<std::pair<int, int>> path;  // A* 寻路生成的路径
    std::size_t path_index = 0;             // 当前走到路径中的哪一个节点
    double replan_elapsed =
        0.0;  // 距离上次重新寻路的累计时间(用于周期性重算路径)
    double attack_cooldown_seconds = 0.0;  // 敌人攻击冷却时间
    bool is_attacking = false;  // 是否处于攻击状态(用于客户端播放/停止攻击动画)
    uint32_t attack_target_player_id = 0;  // 当前攻击目前玩家ID
    double dead_elapsed_seconds =
        0.0;  // 敌人死亡后已过时间(用于死亡后延迟清理/复活逻辑)
    lawnmower::Vector2 last_sync_position;  // delta 同步基线位置
    int32_t last_sync_health = 0;           // delta 同步基线血量
    bool last_sync_is_alive = true;         // delta 同步基线存活状态
    uint32_t force_sync_left =
        0;  // 强制同步计数(即使没dirty也要同步几次，确保新生成/死亡被客户端看到)
    bool dirty = false;  // 是否有状态变动
  };

  struct ProjectileRuntime {
    uint32_t projectile_id = 0; // 射弹实例ID
    uint32_t owner_player_id = 0; // 发射者玩家ID
    float x = 0.0f; // 当前x坐标
    float y = 0.0f; // 当前y坐标
    float dir_x = 1.0f; // x单位向量
    float dir_y = 0.0f; // y单位向量
    float rotation = 0.0f; // 朝向角
    float speed = 0.0f; // 速度
    int32_t damage = 0; // 伤害值
    bool has_buff = false; // 是否携带Buff
    uint32_t buff_id = 0; // Buff ID 
    bool is_friendly = true; // 是否友方/敌方
    double remaining_seconds = 0.0; // 剩余存活时间(TTL)
  };

  struct ItemRuntime {
    uint32_t item_id = 0; // 道具实例ID
    uint32_t type_id = 0; // 道具类型ID
    lawnmower::ItemEffectType effect_type =
        lawnmower::ITEM_EFFECT_NONE; // 道具效果类型
    float x = 0.0f; // 当前x坐标
    float y = 0.0f; // 当前y坐标
    bool is_picked = false; // 是否已被拾取
    bool dirty = false; // 是否需要同步
  };

  struct Scene {
    SceneConfig config;                                   // 场景配置
    std::unordered_map<uint32_t, PlayerRuntime> players;  // 玩家运行时状态表
    std::unordered_map<uint32_t, EnemyRuntime> enemies;   // 敌人运行时状态表
    std::unordered_map<uint32_t, ProjectileRuntime>
        projectiles;                  // 射弹运行时状态表
    std::unordered_map<uint32_t, ItemRuntime> items;  // 道具运行时状态表
    uint32_t next_enemy_id = 1;       // 下一个生成敌人的自增id
    uint32_t next_projectile_id = 1;  // 下一个生成的射弹的自增id
    uint32_t next_item_id = 1;        // 下一个生成的道具自增id
    uint32_t wave_id = 0;             // 当前波次编号
    double elapsed = 0.0;             // 场景累计运行时间
    double spawn_elapsed = 0.0;       // 距上次刷怪的累计时间
    double item_spawn_elapsed = 0.0;  // 距上次生成道具的累计时间
    uint32_t rng_state = 1;           // 伪随机种子
    bool game_over = false;           // 是否已结束
    int nav_cells_x = 0;              // 寻路网格的行数
    int nav_cells_y = 0;              // 寻路网格的列数

    // A*寻路的缓存数组,减少每次分配
    std::vector<int> nav_came_from;
    std::vector<float> nav_g_score;
    std::vector<uint8_t> nav_closed;

    uint64_t tick = 0;               // 逻辑帧计数
    double sync_accumulator = 0.0;   // 同步计时器累积,到达间隔则发送同步
    double full_sync_elapsed = 0.0;  // 距离上次全量同步的累计时间
    std::chrono::steady_clock::time_point last_tick_time;  // 上一次tick的时间点
    std::chrono::duration<double> tick_interval;           // 逻辑帧固定间隔
    std::chrono::duration<double> sync_interval;           // 状态同步间隔
    std::chrono::duration<double> full_sync_interval;      // 全量同步间隔
    std::shared_ptr<asio::steady_timer>
        loop_timer;  // Asio定时器，用于调度该房间的tick循环
  };

  static constexpr int kNavCellSize = 100;  // px
  static constexpr float kEnemySpawnInset =
      10.0f;  // 避免精确落在边界导致 clamp 抖动
  static constexpr uint32_t kEnemySpawnForceSyncCount =
      6;  // 新刷怪多发几次，降低 UDP 丢包影响
  static uint32_t NextRng(uint32_t* state);
  static float NextRngUnitFloat(uint32_t* state);

  SceneConfig BuildDefaultConfig() const;
  void PlacePlayers(const RoomManager::RoomSnapshot& snapshot, Scene* scene);
  [[nodiscard]] const EnemyTypeConfig& ResolveEnemyType(uint32_t type_id) const;
  [[nodiscard]] const ItemTypeConfig& ResolveItemType(uint32_t type_id) const;
  [[nodiscard]] uint32_t PickSpawnEnemyTypeId(uint32_t* rng_state) const;
  void ProcessEnemies(Scene& scene, double dt_seconds, bool* has_dirty);
  void ProcessItems(Scene& scene, double dt_seconds,
                    std::vector<lawnmower::ItemState>* dropped_items,
                    bool* has_dirty);
  void ProcessSceneTick(uint32_t room_id, double tick_interval_seconds);
  void ProcessCombatAndProjectiles(
      Scene& scene, double dt_seconds,
      std::vector<lawnmower::S2C_PlayerHurt>* player_hurts,
      std::vector<lawnmower::S2C_EnemyDied>* enemy_dieds,
      std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
      std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
      std::optional<lawnmower::S2C_GameOver>* game_over,
      std::vector<lawnmower::ProjectileState>* projectile_spawns,
      std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
      bool* has_dirty);
  void ScheduleGameTick(uint32_t room_id, std::chrono::microseconds interval,
                        const std::shared_ptr<asio::steady_timer>& timer,
                        double tick_interval_seconds);
  [[nodiscard]] bool ShouldRescheduleTick(
      uint32_t room_id, const std::shared_ptr<asio::steady_timer>& timer) const;
  void StopGameLoop(uint32_t room_id);
  lawnmower::Vector2 ClampToMap(const SceneConfig& cfg, float x, float y) const;

  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, Scene> scenes_;           // room_id -> scene
  std::unordered_map<uint32_t, uint32_t> player_scene_;  // player_id -> room_id

  asio::io_context* io_context_ = nullptr;
  UdpServer* udp_server_ = nullptr;
  ServerConfig config_;
  PlayerRolesConfig player_roles_config_;
  EnemyTypesConfig enemy_types_config_;
  ItemsConfig items_config_;
};
