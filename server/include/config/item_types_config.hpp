#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 道具类型配置：用于生成/拾取/效果结算。
// - type_id 会同步给客户端（ItemState.type_id）
// - 配置来自 game_config/items_config.json（缺失则使用默认值）
struct ItemTypeConfig {
  uint32_t type_id = 0;
  std::string name;
  std::string effect;        // 道具效果类型（如 "heal"）
  int32_t value = 0;         // 效果数值（如回血量）
  uint32_t drop_weight = 0;  // 掉落权重（0 表示不参与掉落）
};

struct ItemsConfig {
  uint32_t default_type_id = 1;
  uint32_t max_items_alive = 6;  // 同时存在的道具上限
  float pick_radius = 24.0f;     // 拾取半径（像素）
  std::unordered_map<uint32_t, ItemTypeConfig> items;
};

// 从配置文件加载道具配置；若未找到文件或解析失败，返回 false 并保留默认值
bool LoadItemsConfig(ItemsConfig* out);
