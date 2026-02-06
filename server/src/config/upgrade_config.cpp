#include "config/upgrade_config.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <regex>
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

template <typename T>
void ExtractUint(const std::string& content, std::string_view key, T* out) {
  if (out == nullptr) {
    return;
  }
  std::regex re(std::string("\"") + std::string(key) + "\"\\s*:\\s*(\\d+)");
  std::smatch match;
  if (std::regex_search(content, match, re) && match.size() > 1) {
    try {
      *out = static_cast<T>(std::stoull(match[1].str()));
    } catch (...) {
    }
  }
}

void ExtractFloat(const std::string& content, std::string_view key,
                  float* out) {
  if (out == nullptr) {
    return;
  }
  std::regex re(std::string("\"") + std::string(key) +
                "\"\\s*:\\s*(-?\\d+\\.?\\d*)");
  std::smatch match;
  if (std::regex_search(content, match, re) && match.size() > 1) {
    try {
      *out = std::stof(match[1].str());
    } catch (...) {
    }
  }
}

void ExtractString(const std::string& content, std::string_view key,
                   std::string* out) {
  if (out == nullptr) {
    return;
  }
  std::regex re(std::string("\"") + std::string(key) +
                "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  if (std::regex_search(content, match, re) && match.size() > 1) {
    *out = match[1].str();
  }
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

  UpgradeConfig cfg;
  cfg.option_count = fallback.option_count;
  cfg.refresh_limit = fallback.refresh_limit;
  cfg.effects.clear();

  ExtractUint(content, "option_count", &cfg.option_count);
  ExtractUint(content, "refresh_limit", &cfg.refresh_limit);

  cfg.option_count = 3;
  cfg.refresh_limit = ClampUInt32(cfg.refresh_limit, 0u, 999u);

  std::regex obj_re("\\{[^\\{\\}]*\"type\"\\s*:\\s*\"([^\"]+)\"[^\\{\\}]*\\}");
  std::size_t parsed = 0;
  for (std::sregex_iterator it(content.begin(), content.end(), obj_re), end;
       it != end; ++it) {
    const std::string obj = it->str();
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

    float value = 0.0f;
    ExtractFloat(obj, "value", &value);
    value = std::clamp(value, -100000.0f, 100000.0f);

    uint32_t weight = 1;
    ExtractUint(obj, "weight", &weight);
    weight = ClampUInt32(weight, 1u, 100000u);

    cfg.effects.push_back(MakeEffect(type, level, value, weight));
    parsed += 1;
  }

  if (parsed == 0) {
    *out = fallback;
    return false;
  }

  *out = std::move(cfg);
  return true;
}
