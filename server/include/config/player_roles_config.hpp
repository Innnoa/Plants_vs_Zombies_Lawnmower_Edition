#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

// 玩家职业配置：用于初始化 PlayerState 的基础属性。
// - role_id 会同步给客户端（PlayerState.role_id），用于客户端资源/UI 映射
// - 该配置来自 server/config/player_roles.json（缺失则使用内置默认值）
struct PlayerRoleConfig {
  uint32_t role_id = 0;
  std::string name;
  int32_t max_health = 100;
  uint32_t attack = 10;
  uint32_t attack_speed = 1;
  float move_speed = 0.0f;  // <=0 表示使用 server_config.json 的 move_speed
  uint32_t critical_hit_rate = 0;  // 单位：‰（0~1000）
};

struct PlayerRolesConfig {
  uint32_t default_role_id = 1;
  std::unordered_map<uint32_t, PlayerRoleConfig> roles;
};

// 从配置文件加载玩家职业配置；若未找到文件或解析失败，返回 false 并保留默认值
bool LoadPlayerRolesConfig(PlayerRolesConfig* out);
