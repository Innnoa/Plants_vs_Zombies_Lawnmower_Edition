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
#include "config/upgrade_config.hpp"
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
  void SetUpgradeConfig(const UpgradeConfig& cfg) { upgrade_config_ = cfg; }

  // 在游戏开始后为房间启动固定逻辑帧循环与状态同步
  void StartGameLoop(uint32_t room_id);

  // 处理玩家输入，将其入队等待逻辑帧处理；返回 false 表示未找到玩家或场景
  [[nodiscard]] bool HandlePlayerInput(uint32_t player_id,
                                       const lawnmower::C2S_PlayerInput& input,
                                       uint32_t* room_id);
  [[nodiscard]] bool HandleUpgradeRequestAck(
      uint32_t player_id, const lawnmower::C2S_UpgradeRequestAck& request);
  [[nodiscard]] bool HandleUpgradeOptionsAck(
      uint32_t player_id, const lawnmower::C2S_UpgradeOptionsAck& request);
  [[nodiscard]] bool HandleUpgradeSelect(
      uint32_t player_id, const lawnmower::C2S_UpgradeSelect& request);
  [[nodiscard]] bool HandleUpgradeRefreshRequest(
      uint32_t player_id, const lawnmower::C2S_UpgradeRefreshRequest& request);

  // 判断给定坐标是否在指定房间的地图边界内（基于场景宽高）
  [[nodiscard]] bool IsInsideMap(uint32_t room_id,
                                 const lawnmower::Vector2& position) const;

  // 玩家断线/离开时清理场景信息
  void RemovePlayer(uint32_t player_id);
  // 标记玩家断线（保留运行时状态）
  bool MarkPlayerDisconnected(uint32_t player_id);
  struct ReconnectSnapshot {
    uint32_t room_id = 0;      // 房间ID
    uint64_t server_tick = 0;  // 服务器当前tick
    bool is_paused = false;    // 是否暂停
    std::string player_name;   // 玩家名
  };
  // 重连后恢复玩家状态（重置输入基线等）
  [[nodiscard]] bool TryReconnectPlayer(uint32_t player_id, uint32_t room_id,
                                        uint32_t last_input_seq,
                                        uint32_t last_server_tick,
                                        ReconnectSnapshot* out);

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
    struct HistoryEntry {
      uint64_t tick = 0;                      // 逻辑帧编号
      lawnmower::Vector2 position;            // 位置
      float rotation = 0.0f;                  // 朝向
      int32_t health = 0;                     // 血量
      bool is_alive = true;                   // 是否存活
      uint32_t last_processed_input_seq = 0;  // 已处理输入序号
    };

    // 对齐优化：按位宽从大到小聚合，减少 padding。
    double attack_cooldown_seconds = 0.0;         // 攻击冷却剩余
    double target_refresh_elapsed = 0.0;          // 目标刷新计时
    uint64_t last_attack_dir_log_tick = 0;        // 最近记录攻击方向的tick
    uint64_t last_projectile_spawn_log_tick = 0;  // 最近记录射弹生成信息的tick
    std::chrono::steady_clock::time_point disconnected_at;  // 断线时间
    std::string player_name;                                // 玩家名
    lawnmower::Vector2 last_sync_position;  // delta 同步基线位置
    std::deque<lawnmower::C2S_PlayerInput> pending_inputs;  // 待处理输入队列
    std::deque<HistoryEntry> history;     // 历史缓冲（用于预测校验）
    lawnmower::PlayerState state;         // 玩家状态
    uint32_t last_input_seq = 0;          // 已处理的最新输入序号
    float last_sync_rotation = 0.0f;      // delta 同步基线朝向
    uint32_t last_sync_input_seq = 0;     // delta 同步基线输入序号
    uint32_t locked_target_enemy_id = 0;  // 锁定目标
    float last_attack_dir_x = 1.0f;       // 最近攻击方向x向量
    float last_attack_dir_y = 0.0f;       // 最近攻击方向y向量
    float last_attack_rotation = 0.0f;    // 最近攻击朝向角
    int32_t kill_count = 0;               // 击杀数
    int32_t damage_dealt = 0;             // 伤害总量
    uint32_t pending_upgrade_count = 0;   // 待处理升级次数
    uint32_t refresh_remaining = 0;       // 剩余刷新次数
    bool last_sync_is_alive = true;       // delta 同步基线存活状态
    bool wants_attacking = false;         // 攻击意图
    bool has_attack_dir = false;          // 是否有攻击方向
    bool is_connected = true;             // 是否在线
    bool low_freq_dirty = false;          // 低频/全量同步字段变化标记
    bool dirty = false;                   // 高频同步字段变化标记
    bool dirty_queued = false;            // 是否已进入脏队列（去重）
  };

  // 敌人运行时状态
  struct EnemyRuntime {
    lawnmower::EnemyState state;            // 要同步给客户端的敌人基础状态
    uint32_t target_player_id = 0;          // 寻路/追踪时的目标玩家id
    std::vector<std::pair<int, int>> path;  // A* 寻路生成的路径
    std::size_t path_index = 0;             // 当前走到路径中的哪一个节点
    std::pair<int, int> last_path_start_cell = {0, 0};  // 上次寻路起点格
    std::pair<int, int> last_path_goal_cell = {0, 0};   // 上次寻路终点格
    bool has_cached_path = false;                       // 是否有可复用路径
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
    bool dirty = false;         // 是否有状态变动
    bool dirty_queued = false;  // 是否已进入脏队列（去重）
  };

  struct ProjectileRuntime {
    uint32_t projectile_id = 0;      // 射弹实例ID
    uint32_t owner_player_id = 0;    // 发射者玩家ID
    float x = 0.0f;                  // 当前x坐标
    float y = 0.0f;                  // 当前y坐标
    float dir_x = 1.0f;              // x单位向量
    float dir_y = 0.0f;              // y单位向量
    float rotation = 0.0f;           // 朝向角
    float speed = 0.0f;              // 速度
    int32_t damage = 0;              // 伤害值
    bool has_buff = false;           // 是否携带Buff
    uint32_t buff_id = 0;            // Buff ID
    bool is_friendly = true;         // 是否友方/敌方
    double remaining_seconds = 0.0;  // 剩余存活时间(TTL)
  };

  struct ItemRuntime {
    uint32_t item_id = 0;  // 道具实例ID
    uint32_t type_id = 0;  // 道具类型ID
    lawnmower::ItemEffectType effect_type =
        lawnmower::ITEM_EFFECT_NONE;   // 道具效果类型
    float x = 0.0f;                    // 当前x坐标
    float y = 0.0f;                    // 当前y坐标
    bool is_picked = false;            // 是否已被拾取
    float last_sync_x = 0.0f;          // delta 同步基线x
    float last_sync_y = 0.0f;          // delta 同步基线y
    bool last_sync_is_picked = false;  // delta 同步基线拾取状态
    uint32_t last_sync_type_id = 0;    // delta 同步基线类型
    uint32_t force_sync_left = 0;      // 强制同步次数（用于新生成道具首包）
    bool dirty = false;                // 是否需要同步
    bool dirty_queued = false;         // 是否已进入脏队列（去重）
  };

  // 单帧性能采样
  struct PerfSample {
    uint64_t tick = 0;                // 逻辑帧编号
    double logic_ms = 0.0;            // 逻辑帧耗时（毫秒）
    double dt_seconds = 0.0;          // 逻辑步长（秒）
    uint32_t player_count = 0;        // 玩家数量
    uint32_t enemy_count = 0;         // 敌人数量
    uint32_t projectile_count = 0;    // 射弹数量
    uint32_t item_count = 0;          // 道具数量
    uint32_t dirty_player_count = 0;  // 脏玩家数量
    uint32_t dirty_enemy_count = 0;   // 脏敌人数量
    uint32_t dirty_item_count = 0;    // 脏道具数量
    bool is_paused = false;           // 是否处于暂停
    uint32_t delta_items_size = 0;    // delta 中道具数量
    uint32_t sync_items_size = 0;     // full sync 中道具数量
  };

  // 单局性能统计
  struct PerfStats {
    std::vector<PerfSample> samples;                   // 逐帧采样
    double total_ms = 0.0;                             // 累计耗时
    double max_ms = 0.0;                               // 最大耗时
    double min_ms = 0.0;                               // 最小耗时
    uint64_t tick_count = 0;                           // 采样帧数
    std::chrono::system_clock::time_point start_time;  // 开始时间
    std::chrono::system_clock::time_point end_time;    // 结束时间
  };

  enum class UpgradeStage {
    kNone = 0,
    kRequestSent = 1,
    kOptionsSent = 2,
    kWaitingSelect = 3,
  };

  struct Scene {
    SceneConfig config;                                   // 场景配置
    std::unordered_map<uint32_t, PlayerRuntime> players;  // 玩家运行时状态表
    std::unordered_map<uint32_t, EnemyRuntime> enemies;   // 敌人运行时状态表
    std::unordered_map<uint32_t, ProjectileRuntime>
        projectiles;                                  // 射弹运行时状态表
    std::unordered_map<uint32_t, ItemRuntime> items;  // 道具运行时状态表
    // 脏ID向量配合运行时 dirty_queued 去重，降低哈希开销。
    std::vector<uint32_t> dirty_player_ids;          // 脏玩家ID缓存
    std::vector<uint32_t> dirty_enemy_ids;           // 脏敌人ID缓存
    std::vector<uint32_t> dirty_item_ids;            // 脏道具ID缓存
    std::vector<EnemyRuntime> enemy_pool;            // 敌人复用池
    std::vector<ProjectileRuntime> projectile_pool;  // 射弹复用池
    std::vector<ItemRuntime> item_pool;              // 道具复用池
    uint32_t next_enemy_id = 1;                      // 下一个生成敌人的自增id
    uint32_t next_projectile_id = 1;                 // 下一个生成的射弹的自增id
    uint32_t next_item_id = 1;                       // 下一个生成的道具自增id
    uint32_t wave_id = 0;                            // 当前波次编号
    double elapsed = 0.0;                            // 场景累计运行时间
    double spawn_elapsed = 0.0;                      // 距上次刷怪的累计时间
    uint32_t rng_state = 1;                          // 伪随机种子
    bool game_over = false;                          // 是否已结束
    bool is_paused = false;                          // 是否暂停（升级流程）
    int nav_cells_x = 0;                             // 寻路网格的行数
    int nav_cells_y = 0;                             // 寻路网格的列数

    // A*寻路缓存：使用代际标记避免每次全量清空数组
    std::vector<int> nav_came_from;
    std::vector<float> nav_g_score;
    std::vector<uint32_t> nav_visit_epoch;
    std::vector<uint32_t> nav_closed_epoch;
    uint32_t nav_epoch = 0;

    uint64_t tick = 0;               // 逻辑帧计数
    double sync_accumulator = 0.0;   // 同步计时器累积,到达间隔则发送同步
    double sync_idle_elapsed = 0.0;  // 低活跃累计时间
    double full_sync_elapsed = 0.0;  // 距离上次全量同步的累计时间
    std::chrono::steady_clock::time_point last_tick_time;  // 上一次tick的时间点
    std::chrono::steady_clock::time_point next_tick_time;  // 下一帧调度时间点
    std::chrono::duration<double> tick_interval;           // 逻辑帧固定间隔
    uint64_t last_item_log_tick = 0;                       // 上次道具日志tick
    std::chrono::duration<double> sync_interval;           // 状态同步间隔
    std::chrono::duration<double> dynamic_sync_interval;   // 动态同步间隔
    std::chrono::duration<double> full_sync_interval;      // 全量同步间隔
    std::shared_ptr<asio::steady_timer>
        loop_timer;                  // Asio定时器，用于调度该房间的tick循环
    uint32_t upgrade_player_id = 0;  // 当前升级选择玩家
    UpgradeStage upgrade_stage = UpgradeStage::kNone;  // 当前升级阶段
    lawnmower::UpgradeReason upgrade_reason =
        lawnmower::UPGRADE_REASON_UNKNOWN;             // 升级触发原因
    std::vector<UpgradeEffectConfig> upgrade_options;  // 当前升级选项
    PerfStats perf;                                    // 性能统计
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
  void ProcessItems(Scene& scene, bool* has_dirty);
  void ConsumePlayerInputQueueLocked(const SceneConfig& scene_config,
                                     PlayerRuntime* runtime,
                                     double tick_interval_seconds, bool* moved,
                                     bool* consumed_input) const;
  void ProcessPlayerInputsLocked(Scene& scene, double tick_interval_seconds,
                                 double dt_seconds, bool* has_dirty);
  void ReserveTickEventBuffersLocked(
      const Scene& scene, std::vector<lawnmower::S2C_PlayerHurt>* player_hurts,
      std::vector<lawnmower::S2C_EnemyDied>* enemy_dieds,
      std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
      std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
      std::vector<lawnmower::ProjectileState>* projectile_spawns,
      std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
      std::vector<lawnmower::ItemState>* dropped_items) const;
  bool TryBeginPendingUpgradeLocked(
      uint32_t room_id, Scene& scene,
      std::optional<lawnmower::S2C_UpgradeRequest>* upgrade_request);
  void CaptureGameOverPerfLocked(
      Scene& scene, const std::optional<lawnmower::S2C_GameOver>& game_over,
      std::optional<PerfStats>* perf_to_save, uint32_t* perf_tick_rate,
      uint32_t* perf_sync_rate, double* perf_elapsed_seconds);
  double ComputeTickDeltaSecondsLocked(Scene& scene,
                                       double tick_interval_seconds) const;
  struct TickFrameContext {
    uint32_t room_id = 0;
    double tick_interval_seconds = 0.0;
    double dt_seconds = 0.0;
    std::chrono::steady_clock::time_point perf_start;
  };
  struct TickDirtyState {
    bool has_dirty_players = false;
    bool has_dirty_enemies = false;
    bool has_dirty_items = false;
  };
  struct TickOutputs {
    lawnmower::S2C_GameStateSync sync;
    lawnmower::S2C_GameStateDeltaSync delta;
    bool force_full_sync = false;
    bool should_sync = false;
    bool built_sync = false;
    bool built_delta = false;
    std::vector<lawnmower::S2C_PlayerHurt> player_hurts;
    std::vector<lawnmower::S2C_EnemyDied> enemy_dieds;
    std::vector<lawnmower::EnemyAttackStateDelta> enemy_attack_states;
    std::vector<lawnmower::S2C_PlayerLevelUp> level_ups;
    std::optional<lawnmower::S2C_GameOver> game_over;
    std::optional<lawnmower::S2C_UpgradeRequest> upgrade_request;
    std::vector<lawnmower::ProjectileState> projectile_spawns;
    std::vector<lawnmower::ProjectileDespawn> projectile_despawns;
    std::vector<lawnmower::ItemState> dropped_items;
    std::vector<uint32_t> expired_players;
    bool paused_only = false;
    std::optional<PerfStats> perf_to_save;
    uint32_t perf_tick_rate = 0;
    uint32_t perf_sync_rate = 0;
    double perf_elapsed_seconds = 0.0;
    uint32_t perf_delta_items_size = 0;
    uint32_t perf_sync_items_size = 0;
    uint64_t event_tick = 0;
    uint32_t event_wave_id = 0;
  };
  void SimulateSceneFrameLocked(Scene& scene, const TickFrameContext& frame,
                                TickOutputs* outputs,
                                TickDirtyState* dirty_state);
  void BuildSceneSyncAndPerfLocked(Scene& scene, const TickFrameContext& frame,
                                   const TickDirtyState& dirty_state,
                                   TickOutputs* outputs);
  void ProcessActiveSceneTickLocked(Scene& scene, const TickFrameContext& frame,
                                    TickOutputs* outputs);
  void FinalizeSceneTick(
      uint32_t room_id, const std::vector<uint32_t>& expired_players,
      bool paused_only,
      std::vector<lawnmower::ProjectileState>* projectile_spawns,
      std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
      const std::vector<lawnmower::ItemState>& dropped_items,
      const std::vector<lawnmower::EnemyAttackStateDelta>& enemy_attack_states,
      const std::vector<lawnmower::S2C_PlayerHurt>& player_hurts,
      const std::vector<lawnmower::S2C_EnemyDied>& enemy_dieds,
      const std::vector<lawnmower::S2C_PlayerLevelUp>& level_ups,
      const std::optional<lawnmower::S2C_GameOver>& game_over,
      const std::optional<lawnmower::S2C_UpgradeRequest>& upgrade_request,
      std::optional<PerfStats>* perf_to_save, uint32_t perf_tick_rate,
      uint32_t perf_sync_rate, double perf_elapsed_seconds, uint64_t event_tick,
      uint32_t event_wave_id, bool force_full_sync, bool built_sync,
      bool built_delta, const lawnmower::S2C_GameStateSync& sync,
      const lawnmower::S2C_GameStateDeltaSync& delta);
  void ProcessSceneTick(uint32_t room_id, double tick_interval_seconds);
  struct CombatTickParams {
    float projectile_speed = 0.0f;
    float projectile_radius = 0.0f;
    double projectile_ttl_seconds = 0.0;
    uint32_t projectile_ttl_ms = 0;
    uint32_t max_shots_per_tick = 1;
    double attack_min_interval = 0.05;
    double attack_max_interval = 2.0;
    bool allow_catchup = false;
  };
  CombatTickParams BuildCombatTickParams(const Scene& scene,
                                         double dt_seconds) const;
  void MarkPlayerLowFreqDirtyForCombat(Scene& scene, PlayerRuntime& runtime);
  void GrantExpForCombat(Scene& scene, PlayerRuntime& player,
                         uint32_t exp_reward,
                         std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups);
  static std::pair<float, float> RotationDir(float rotation_deg);
  static float RotationFromDir(float dir_x, float dir_y);
  static std::pair<float, float> ComputeProjectileOrigin(
      const PlayerRuntime& player, float facing_dir_x);
  uint32_t FindNearestEnemyIdForPlayerFire(const Scene& scene,
                                           const PlayerRuntime& player) const;
  const EnemyRuntime* ResolveLockedTargetForPlayerFire(Scene& scene,
                                                       PlayerRuntime& player,
                                                       double dt_seconds) const;
  void MaybeLogAttackDirFallback(const Scene& scene, PlayerRuntime& player,
                                 uint32_t target_id, const char* reason) const;
  void MaybeLogProjectileSpawn(const Scene& scene, PlayerRuntime& player,
                               uint32_t projectile_id,
                               const EnemyRuntime& target, float origin_x,
                               float origin_y, float dir_x, float dir_y,
                               float rotation) const;
  bool ResolveProjectileDirectionForPlayerFire(
      const Scene& scene, PlayerRuntime& player, const EnemyRuntime& target,
      float* out_dir_x, float* out_dir_y, float* out_rotation) const;
  int32_t ComputeProjectileDamageForPlayerFire(
      Scene& scene, const PlayerRuntime& player) const;
  void SpawnProjectileForPlayerFire(
      Scene& scene, const CombatTickParams& params, uint32_t owner_player_id,
      PlayerRuntime& player, const EnemyRuntime& target, int32_t damage,
      float dir_x, float dir_y, float rotation,
      std::vector<lawnmower::ProjectileState>* projectile_spawns);
  void ProcessPlayerFireStage(
      Scene& scene, double dt_seconds, const CombatTickParams& params,
      std::vector<lawnmower::ProjectileState>* projectile_spawns);
  struct EnemyHitGrid {
    int cells_x = 0;
    int cells_y = 0;
    float cell_size = 0.0f;
    bool enabled = false;
    std::vector<std::vector<EnemyRuntime*>> cells;
  };
  void BuildEnemyHitGridForProjectileStage(Scene& scene,
                                           EnemyHitGrid* out_grid) const;
  bool FindProjectileHitEnemyForStage(
      Scene& scene, const CombatTickParams& params, const EnemyHitGrid& grid,
      float prev_x, float prev_y, float next_x, float next_y,
      EnemyRuntime** hit_enemy, uint32_t* hit_enemy_id, float* out_hit_t) const;
  void ApplyProjectileHitForStage(
      Scene& scene, ProjectileRuntime& proj, EnemyRuntime& hit_enemy,
      std::vector<lawnmower::S2C_EnemyDied>* enemy_dieds,
      std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
      std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
      std::vector<uint32_t>* killed_enemy_ids, bool* has_dirty);
  static bool IsProjectileOutOfBoundsForStage(const ProjectileRuntime& proj,
                                              float map_w, float map_h);
  static void PushProjectileDespawnForStage(
      const ProjectileRuntime& proj, lawnmower::ProjectileDespawnReason reason,
      uint32_t hit_enemy_id,
      std::vector<lawnmower::ProjectileDespawn>* projectile_despawns);
  void ProcessProjectileHitStage(
      Scene& scene, double dt_seconds, const CombatTickParams& params,
      std::vector<lawnmower::S2C_EnemyDied>* enemy_dieds,
      std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
      std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
      std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
      std::vector<uint32_t>* killed_enemy_ids, bool* has_dirty);
  void BuildDropCandidatesForStage(
      std::vector<std::pair<uint32_t, uint32_t>>* drop_candidates,
      uint32_t* drop_weight_total) const;
  uint32_t PickDropTypeIdForStage(
      Scene& scene,
      const std::vector<std::pair<uint32_t, uint32_t>>& drop_candidates,
      uint32_t drop_weight_total) const;
  void SpawnDropItemForStage(Scene& scene, uint32_t type_id, float x, float y,
                             uint32_t max_items_alive,
                             std::vector<lawnmower::ItemState>* dropped_items,
                             bool* has_dirty);
  void ProcessEnemyDropStage(Scene& scene,
                             const std::vector<uint32_t>& killed_enemy_ids,
                             std::vector<lawnmower::ItemState>* dropped_items,
                             bool* has_dirty);
  void ResolveEnemyAttackRadiiForStage(const EnemyTypeConfig& type,
                                       float* enter_radius,
                                       float* exit_radius) const;
  uint32_t SelectEnemyMeleeTargetForStage(const Scene& scene,
                                          const EnemyRuntime& enemy, float ex,
                                          float ey, float enter_sq,
                                          float exit_sq) const;
  void PushEnemyAttackStateForStage(
      uint32_t enemy_id, EnemyRuntime& enemy, bool attacking,
      uint32_t target_id,
      std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states) const;
  void TryApplyEnemyMeleeDamageForStage(
      Scene& scene, uint32_t enemy_id, EnemyRuntime& enemy,
      uint32_t target_player_id, const EnemyTypeConfig& type,
      std::vector<lawnmower::S2C_PlayerHurt>* player_hurts, bool* has_dirty);
  void ProcessEnemyMeleeStage(
      Scene& scene, double dt_seconds,
      std::vector<lawnmower::S2C_PlayerHurt>* player_hurts,
      std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
      bool* has_dirty);
  static std::size_t CountAlivePlayersAfterCombatForStage(const Scene& scene);
  void BuildGameOverMessageForStage(const Scene& scene,
                                    lawnmower::S2C_GameOver* out) const;
  void UpdateGameOverForCombatStage(
      Scene& scene, std::optional<lawnmower::S2C_GameOver>* game_over) const;
  void ProcessCombatAndProjectiles(
      Scene& scene, double dt_seconds,
      std::vector<lawnmower::S2C_PlayerHurt>* player_hurts,
      std::vector<lawnmower::S2C_EnemyDied>* enemy_dieds,
      std::vector<lawnmower::EnemyAttackStateDelta>* enemy_attack_states,
      std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
      std::optional<lawnmower::S2C_GameOver>* game_over,
      std::vector<lawnmower::ProjectileState>* projectile_spawns,
      std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
      std::vector<lawnmower::ItemState>* dropped_items, bool* has_dirty);
  void BuildUpgradeOptionsLocked(Scene& scene);
  bool BeginUpgradeLocked(uint32_t room_id, Scene& scene, uint32_t player_id,
                          lawnmower::UpgradeReason reason,
                          lawnmower::S2C_UpgradeRequest* request);
  void ResetUpgradeLocked(Scene& scene);
  void ApplyUpgradeEffect(PlayerRuntime& runtime,
                          const UpgradeEffectConfig& effect);
  void MarkPlayerDirty(Scene& scene, uint32_t player_id, PlayerRuntime& runtime,
                       bool low_freq);
  void MarkEnemyDirty(Scene& scene, uint32_t enemy_id, EnemyRuntime& runtime);
  void MarkItemDirty(Scene& scene, uint32_t item_id, ItemRuntime& runtime);
  std::size_t GetPredictionHistoryLimit(const Scene& scene) const;
  void RecordPlayerHistoryLocked(Scene& scene);
  static void FillPlayerHighFreq(const PlayerRuntime& runtime,
                                 lawnmower::PlayerState* out);
  static void FillPlayerForSync(PlayerRuntime& runtime,
                                lawnmower::PlayerState* out);
  static bool PositionChanged(const lawnmower::Vector2& current,
                              const lawnmower::Vector2& last);
  static bool PositionChanged(float current_x, float current_y, float last_x,
                              float last_y);
  static void UpdatePlayerLastSync(PlayerRuntime& runtime);
  static void UpdateEnemyLastSync(EnemyRuntime& runtime);
  static void UpdateItemLastSync(ItemRuntime& runtime);
  void BuildSyncPayloadsLocked(uint32_t room_id, Scene& scene,
                               bool force_full_sync,
                               const std::vector<uint32_t>& dirty_player_ids,
                               const std::vector<uint32_t>& dirty_enemy_ids,
                               const std::vector<uint32_t>& dirty_item_ids,
                               lawnmower::S2C_GameStateSync* sync,
                               lawnmower::S2C_GameStateDeltaSync* delta,
                               bool* built_sync, bool* built_delta,
                               uint32_t* perf_delta_items_size,
                               uint32_t* perf_sync_items_size);
  void CollectExpiredPlayersLocked(const Scene& scene, double grace_seconds,
                                   std::vector<uint32_t>* out) const;
  bool HandlePausedTickLocked(
      Scene& scene, double dt_seconds,
      const std::chrono::steady_clock::time_point& perf_start);
  static bool HasPriorityEventsInTick(
      const std::vector<lawnmower::ProjectileState>& projectile_spawns,
      const std::vector<lawnmower::ProjectileDespawn>& projectile_despawns,
      const std::vector<lawnmower::ItemState>& dropped_items,
      const std::vector<lawnmower::S2C_PlayerHurt>& player_hurts,
      const std::vector<lawnmower::EnemyAttackStateDelta>& enemy_attack_states,
      const std::vector<lawnmower::S2C_EnemyDied>& enemy_dieds,
      const std::vector<lawnmower::S2C_PlayerLevelUp>& level_ups,
      const std::optional<lawnmower::S2C_GameOver>& game_over,
      const std::optional<lawnmower::S2C_UpgradeRequest>& upgrade_request);
  void UpdateSyncSchedulingLocked(
      Scene& scene, double dt_seconds, double tick_interval_seconds,
      bool has_priority_events, bool has_dirty_players, bool has_dirty_enemies,
      bool has_dirty_items, bool* should_sync, bool* force_full_sync) const;
  void MaybeLogItemSyncSnapshotLocked(uint32_t room_id, Scene& scene,
                                      std::size_t dropped_events,
                                      bool built_sync, bool built_delta,
                                      uint32_t perf_delta_items_size,
                                      uint32_t perf_sync_items_size);
  void CleanupExpiredPlayers(const std::vector<uint32_t>& expired_players);
  void ResetPerfStats(Scene& scene);
  void RecordPerfSampleLocked(Scene& scene, double elapsed_ms,
                              double dt_seconds, bool is_paused,
                              uint32_t dirty_player_count,
                              uint32_t dirty_enemy_count,
                              uint32_t dirty_item_count,
                              uint32_t delta_items_size,
                              uint32_t sync_items_size);
  void SavePerfStatsToFile(uint32_t room_id, const PerfStats& stats,
                           uint32_t tick_rate, uint32_t sync_rate,
                           double elapsed_seconds);
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
  UpgradeConfig upgrade_config_;
};
