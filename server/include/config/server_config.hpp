#pragma once

#include <cstdint>
#include <string>

// 服务器整体配置（由 JSON 加载，若读取失败则保持默认值）
struct ServerConfig {
  uint16_t tcp_port = 7777;
  uint16_t udp_port = 7778;
  uint32_t max_players_per_room = 4;
  uint32_t tick_rate = 60;
  uint32_t state_sync_rate = 30;
  uint32_t map_width = 2000;
  uint32_t map_height = 2000;
  float move_speed = 200.0f;
  // 刷怪/难度参数（用于快速调参，不用重新编译）
  float wave_interval_seconds = 15.0f;             // 波次增长间隔（秒）
  float enemy_spawn_base_per_second = 1.0f;        // 基础刷怪速率（只要有人存活）
  float enemy_spawn_per_player_per_second = 0.75f; // 每个存活玩家增加的刷怪速率
  float enemy_spawn_wave_growth_per_second = 0.2f; // 每波次额外增长（随 wave_id 增大）
  uint32_t max_enemies_alive = 256;                // 同时存活敌人上限
  uint32_t max_enemy_spawn_per_tick = 4;           // 单 tick 最大刷怪数量（防止卡顿）
  // 射弹/战斗参数（用于快速调参，不用重新编译）
  float projectile_speed = 420.0f;                      // 射弹速度（像素/秒）
  float projectile_radius = 6.0f;                       // 射弹碰撞半径（像素）
  float projectile_muzzle_offset = 22.0f;               // 出膛偏移（像素，沿 rotation 方向）
  float projectile_ttl_seconds = 2.5f;                  // 射弹生存时间（秒）
  uint32_t projectile_max_shots_per_tick = 4;           // 单 tick 最多补发射弹数（防止卡顿/爆发）
  float projectile_attack_min_interval_seconds = 0.05f; // 射速上限（最小间隔，秒）
  float projectile_attack_max_interval_seconds = 2.0f;  // 射速下限（最大间隔，秒）
  std::string log_level = "info";
};

// 从配置文件加载配置；若未找到文件或解析失败，返回 false 并保留默认值
bool LoadServerConfig(ServerConfig* out);
