#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 敌人类型配置：用于刷怪/移动/伤害/经验等结算。
// - type_id 会同步给客户端（EnemyState.type_id），用于客户端资源映射
// - 该配置来自 server/config/enemy_types.json（缺失则使用内置默认值）
struct EnemyTypeConfig {
  uint32_t type_id = 0;
  std::string name;
  int32_t max_health = 30;
  float move_speed = 60.0f;
  int32_t damage = 0;
  int32_t exp_reward = 10;
  uint32_t drop_chance = 30;          // 掉落概率（0-100）
  float attack_enter_radius = 34.0f;  // 进入攻击状态的距离阈值（像素）
  float attack_exit_radius =
      40.0f;  // 退出攻击状态的距离阈值（像素，需 >= enter）
  float attack_interval_seconds = 0.8f;  // 近战攻击间隔（秒）
};

struct EnemyTypesConfig {
  uint32_t default_type_id = 1;  // type_id -> config
  std::unordered_map<uint32_t, EnemyTypeConfig> enemies;
  // 用于随机刷怪的候选 type_id（排序后，保证选择稳定/可复现）
  std::vector<uint32_t> spawn_type_ids;
};

// 从配置文件加载敌人类型配置；若未找到文件或解析失败，返回 false 并保留默认值
bool LoadEnemyTypesConfig(EnemyTypesConfig* out);
