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
  lawnmower::Vector2 last_sync_position;                  // delta 同步基线位置
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
