#pragma once

#include <cstdint>
#include <vector>

#include "message.pb.h"

struct UpgradeEffectConfig {
  lawnmower::UpgradeType type = lawnmower::UPGRADE_TYPE_UNKNOWN;
  lawnmower::UpgradeLevel level = lawnmower::UPGRADE_LEVEL_UNKNOWN;
  float value = 0.0f;
  uint32_t weight = 1;
};

struct UpgradeConfig {
  uint32_t option_count = 3;
  uint32_t refresh_limit = 0;
  std::vector<UpgradeEffectConfig> effects;
};

// 从配置文件加载升级配置；若未找到文件或解析失败，返回 false 并保留默认值
bool LoadUpgradeConfig(UpgradeConfig* out);
