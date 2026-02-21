static constexpr int kNavCellSize = 100;  // px
static constexpr float kEnemySpawnInset =
    10.0f;  // 避免精确落在边界导致 clamp 抖动
static constexpr uint32_t kEnemySpawnForceSyncCount =
    6;  // 新刷怪多发几次，降低 UDP 丢包影响
static uint32_t NextRng(uint32_t* state);
static float NextRngUnitFloat(uint32_t* state);

SceneConfig BuildDefaultConfig() const;
void PlacePlayers(const SceneCreateSnapshot& snapshot, Scene* scene);
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

CombatTickParams BuildCombatTickParams(const Scene& scene,
                                       double dt_seconds) const;
void MarkPlayerLowFreqDirtyForCombat(Scene& scene, PlayerRuntime& runtime);
void GrantExpForCombat(Scene& scene, PlayerRuntime& player, uint32_t exp_reward,
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
                             uint32_t projectile_id, const EnemyRuntime& target,
                             float origin_x, float origin_y, float dir_x,
                             float dir_y, float rotation) const;
bool ResolveProjectileDirectionForPlayerFire(const Scene& scene,
                                             PlayerRuntime& player,
                                             const EnemyRuntime& target,
                                             float* out_dir_x, float* out_dir_y,
                                             float* out_rotation) const;
int32_t ComputeProjectileDamageForPlayerFire(Scene& scene,
                                             const PlayerRuntime& player) const;
void SpawnProjectileForPlayerFire(
    Scene& scene, const CombatTickParams& params, uint32_t owner_player_id,
    PlayerRuntime& player, const EnemyRuntime& target, int32_t damage,
    float dir_x, float dir_y, float rotation,
    std::vector<lawnmower::ProjectileState>* projectile_spawns);
void ProcessPlayerFireStage(
    Scene& scene, double dt_seconds, const CombatTickParams& params,
    std::vector<lawnmower::ProjectileState>* projectile_spawns);

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
    uint32_t enemy_id, EnemyRuntime& enemy, bool attacking, uint32_t target_id,
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
void UpdateSyncSchedulingLocked(Scene& scene, double dt_seconds,
                                double tick_interval_seconds,
                                bool has_priority_events,
                                bool has_dirty_players, bool has_dirty_enemies,
                                bool has_dirty_items, bool* should_sync,
                                bool* force_full_sync) const;
void MaybeLogItemSyncSnapshotLocked(uint32_t room_id, Scene& scene,
                                    std::size_t dropped_events, bool built_sync,
                                    bool built_delta,
                                    uint32_t perf_delta_items_size,
                                    uint32_t perf_sync_items_size);
void CleanupExpiredPlayers(const std::vector<uint32_t>& expired_players);
void ResetPerfStats(Scene& scene);
void RecordPerfSampleLocked(Scene& scene, double elapsed_ms, double dt_seconds,
                            bool is_paused, uint32_t dirty_player_count,
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
