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
