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

struct EnemyHitGrid {
  int cells_x = 0;
  int cells_y = 0;
  float cell_size = 0.0f;
  bool enabled = false;
  std::vector<std::vector<EnemyRuntime*>> cells;
};
