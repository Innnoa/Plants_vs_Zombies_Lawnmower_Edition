#include "config/upgrade_config.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <limits>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>

namespace {
constexpr std::array<const char*, 3> kConfigPaths = {
    "game_config/upgrade_config.json", "../game_config/upgrade_config.json",
    "../../game_config/upgrade_config.json"};

UpgradeEffectConfig MakeEffect(lawnmower::UpgradeType type,
                               lawnmower::UpgradeLevel level, float value,
                               uint32_t weight) {
  UpgradeEffectConfig effect;
  effect.type = type;
  effect.level = level;
  effect.value = value;
  effect.weight = std::max<uint32_t>(1, weight);
  return effect;
}

UpgradeConfig BuildDefaultUpgradeConfig() {
  UpgradeConfig cfg;
  cfg.option_count = 3;
  cfg.refresh_limit = 1;
  cfg.effects = {
      MakeEffect(lawnmower::UPGRADE_TYPE_MOVE_SPEED,
                 lawnmower::UPGRADE_LEVEL_LOW, 10.0f, 100),
      MakeEffect(lawnmower::UPGRADE_TYPE_MOVE_SPEED,
                 lawnmower::UPGRADE_LEVEL_MEDIUM, 20.0f, 60),
      MakeEffect(lawnmower::UPGRADE_TYPE_MOVE_SPEED,
                 lawnmower::UPGRADE_LEVEL_HIGH, 35.0f, 30),
      MakeEffect(lawnmower::UPGRADE_TYPE_ATTACK, lawnmower::UPGRADE_LEVEL_LOW,
                 2.0f, 100),
      MakeEffect(lawnmower::UPGRADE_TYPE_ATTACK,
                 lawnmower::UPGRADE_LEVEL_MEDIUM, 4.0f, 60),
      MakeEffect(lawnmower::UPGRADE_TYPE_ATTACK, lawnmower::UPGRADE_LEVEL_HIGH,
                 7.0f, 30),
      MakeEffect(lawnmower::UPGRADE_TYPE_ATTACK_SPEED,
                 lawnmower::UPGRADE_LEVEL_LOW, 1.0f, 100),
      MakeEffect(lawnmower::UPGRADE_TYPE_ATTACK_SPEED,
                 lawnmower::UPGRADE_LEVEL_MEDIUM, 2.0f, 60),
      MakeEffect(lawnmower::UPGRADE_TYPE_ATTACK_SPEED,
                 lawnmower::UPGRADE_LEVEL_HIGH, 3.0f, 30),
      MakeEffect(lawnmower::UPGRADE_TYPE_MAX_HEALTH,
                 lawnmower::UPGRADE_LEVEL_LOW, 10.0f, 100),
      MakeEffect(lawnmower::UPGRADE_TYPE_MAX_HEALTH,
                 lawnmower::UPGRADE_LEVEL_MEDIUM, 20.0f, 60),
      MakeEffect(lawnmower::UPGRADE_TYPE_MAX_HEALTH,
                 lawnmower::UPGRADE_LEVEL_HIGH, 35.0f, 30),
      MakeEffect(lawnmower::UPGRADE_TYPE_CRITICAL_RATE,
                 lawnmower::UPGRADE_LEVEL_LOW, 10.0f, 100),
      MakeEffect(lawnmower::UPGRADE_TYPE_CRITICAL_RATE,
                 lawnmower::UPGRADE_LEVEL_MEDIUM, 20.0f, 60),
      MakeEffect(lawnmower::UPGRADE_TYPE_CRITICAL_RATE,
                 lawnmower::UPGRADE_LEVEL_HIGH, 30.0f, 30),
  };
  return cfg;
}

const google::protobuf::Value* FindField(const google::protobuf::Struct& root,
                                         std::string_view key) {
  const auto it = root.fields().find(std::string(key));
  if (it == root.fields().end()) {
    return nullptr;
  }
  return &it->second;
}

bool TryGetNumber(const google::protobuf::Struct& root, std::string_view key,
                  double* out) {
  if (out == nullptr) {
    return false;
  }
  const google::protobuf::Value* field = FindField(root, key);
  if (field == nullptr) {
    return false;
  }
  if (field->kind_case() != google::protobuf::Value::kNumberValue) {
    spdlog::warn("配置项 {} 类型错误，期望 number，保持默认值", key);
    return false;
  }
  *out = field->number_value();
  return true;
}

template <typename T>
void ExtractUint(const google::protobuf::Struct& root, std::string_view key,
                 T* out) {
  if (out == nullptr) {
    return;
  }
  double value = 0.0;
  if (!TryGetNumber(root, key, &value)) {
    return;
  }
  if (!std::isfinite(value)) {
    spdlog::warn("配置项 {} 非有限数值，保持默认值", key);
    return;
  }
  const double integral = std::floor(value);
  if (std::fabs(value - integral) > 1e-6) {
    spdlog::warn("配置项 {} 需要整数，当前值={}，保持默认值", key, value);
    return;
  }
  if (integral < 0.0 ||
      integral > static_cast<double>(std::numeric_limits<T>::max())) {
    spdlog::warn("配置项 {} 超出范围，当前值={}，保持默认值", key, value);
    return;
  }
  *out = static_cast<T>(integral);
}

void ExtractFloat(const google::protobuf::Struct& root, std::string_view key,
                  float* out) {
  if (out == nullptr) {
    return;
  }
  double value = 0.0;
  if (!TryGetNumber(root, key, &value)) {
    return;
  }
  if (!std::isfinite(value)) {
    spdlog::warn("配置项 {} 非有限数值，保持默认值", key);
    return;
  }
  if (value > static_cast<double>(std::numeric_limits<float>::max()) ||
      value < static_cast<double>(std::numeric_limits<float>::lowest())) {
    spdlog::warn("配置项 {} 超出 float 范围，当前值={}，保持默认值", key,
                 value);
    return;
  }
  *out = static_cast<float>(value);
}

void ExtractString(const google::protobuf::Struct& root, std::string_view key,
                   std::string* out) {
  if (out == nullptr) {
    return;
  }
  const google::protobuf::Value* field = FindField(root, key);
  if (field == nullptr) {
    return;
  }
  if (field->kind_case() != google::protobuf::Value::kStringValue) {
    spdlog::warn("配置项 {} 类型错误，期望 string，保持默认值", key);
    return;
  }
  *out = field->string_value();
}

uint32_t ClampUInt32(uint32_t v, uint32_t lo, uint32_t hi) {
  return std::min(std::max(v, lo), hi);
}

std::string ToLower(std::string_view raw) {
  std::string out(raw);
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

lawnmower::UpgradeType ParseUpgradeType(std::string_view raw) {
  const std::string key = ToLower(raw);
  if (key == "move_speed" || key == "movespeed") {
    return lawnmower::UPGRADE_TYPE_MOVE_SPEED;
  }
  if (key == "attack") {
    return lawnmower::UPGRADE_TYPE_ATTACK;
  }
  if (key == "attack_speed" || key == "attackspeed") {
    return lawnmower::UPGRADE_TYPE_ATTACK_SPEED;
  }
  if (key == "max_health" || key == "maxhealth") {
    return lawnmower::UPGRADE_TYPE_MAX_HEALTH;
  }
  if (key == "critical_rate" || key == "criticalrate") {
    return lawnmower::UPGRADE_TYPE_CRITICAL_RATE;
  }
  return lawnmower::UPGRADE_TYPE_UNKNOWN;
}

lawnmower::UpgradeLevel ParseUpgradeLevel(std::string_view raw) {
  const std::string key = ToLower(raw);
  if (key == "low") {
    return lawnmower::UPGRADE_LEVEL_LOW;
  }
  if (key == "mid" || key == "medium") {
    return lawnmower::UPGRADE_LEVEL_MEDIUM;
  }
  if (key == "high") {
    return lawnmower::UPGRADE_LEVEL_HIGH;
  }
  return lawnmower::UPGRADE_LEVEL_UNKNOWN;
}
}  // namespace

bool LoadUpgradeConfig(UpgradeConfig* out) {
  if (out == nullptr) {
    return false;
  }

  const UpgradeConfig fallback = BuildDefaultUpgradeConfig();

  std::ifstream file;
  for (const auto* path : kConfigPaths) {
    file = std::ifstream(path);
    if (file.is_open()) {
      break;
    }
  }

  if (!file.is_open()) {
    *out = fallback;
    return false;
  }

  const std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

  google::protobuf::Struct root;
  const auto status =
      google::protobuf::util::JsonStringToMessage(content, &root);
  if (!status.ok()) {
    spdlog::warn("upgrade_config.json 解析失败：{}，使用默认配置",
                 status.ToString());
    *out = fallback;
    return false;
  }

  UpgradeConfig cfg;
  cfg.option_count = fallback.option_count;
  cfg.refresh_limit = fallback.refresh_limit;
  cfg.effects.clear();

  ExtractUint(root, "option_count", &cfg.option_count);
  ExtractUint(root, "refresh_limit", &cfg.refresh_limit);

  // 当前设计固定 3 选 1，保留原有行为
  cfg.option_count = 3;
  cfg.refresh_limit = ClampUInt32(cfg.refresh_limit, 0u, 999u);

  const google::protobuf::Value* upgrades_field = FindField(root, "upgrades");
  if (upgrades_field != nullptr &&
      upgrades_field->kind_case() != google::protobuf::Value::kListValue) {
    spdlog::warn("配置项 upgrades 类型错误，期望 array，使用默认配置");
    *out = fallback;
    return false;
  }

  std::size_t parsed = 0;
  if (upgrades_field != nullptr) {
    for (const auto& value : upgrades_field->list_value().values()) {
      if (value.kind_case() != google::protobuf::Value::kStructValue) {
        spdlog::warn("upgrades 数组存在非 object 元素，已忽略");
        continue;
      }

      const auto& obj = value.struct_value();
      std::string type_str;
      std::string level_str;
      ExtractString(obj, "type", &type_str);
      ExtractString(obj, "level", &level_str);
      if (type_str.empty() || level_str.empty()) {
        continue;
      }

      const lawnmower::UpgradeType type = ParseUpgradeType(type_str);
      const lawnmower::UpgradeLevel level = ParseUpgradeLevel(level_str);
      if (type == lawnmower::UPGRADE_TYPE_UNKNOWN ||
          level == lawnmower::UPGRADE_LEVEL_UNKNOWN) {
        continue;
      }

      float value_num = 0.0f;
      ExtractFloat(obj, "value", &value_num);
      value_num = std::clamp(value_num, -100000.0f, 100000.0f);

      uint32_t weight = 1;
      ExtractUint(obj, "weight", &weight);
      weight = ClampUInt32(weight, 1u, 100000u);

      cfg.effects.push_back(MakeEffect(type, level, value_num, weight));
      parsed += 1;
    }
  }

  if (parsed == 0) {
    *out = fallback;
    return false;
  }

  *out = std::move(cfg);
  return true;
}
