#include "config/enemy_types_config.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>

namespace {
constexpr std::array<const char*, 3> kConfigPaths = {
    "game_config/enemy_types.json", "../game_config/enemy_types.json",
    "../../game_config/enemy_types.json"};

EnemyTypesConfig BuildDefaultEnemyTypesConfig() {
  EnemyTypesConfig cfg;
  cfg.default_type_id = 1;

  cfg.enemies.emplace(1u, EnemyTypeConfig{
                              .type_id = 1,
                              .name = "普通僵尸",
                              .max_health = 30,
                              .move_speed = 60.0f,
                              .damage = 0,
                              .exp_reward = 10,
                              .attack_enter_radius = 34.0f,
                              .attack_exit_radius = 40.0f,
                              .attack_interval_seconds = 0.8f,
                          });
  cfg.enemies.emplace(2u, EnemyTypeConfig{
                              .type_id = 2,
                              .name = "路障僵尸",
                              .max_health = 60,
                              .move_speed = 50.0f,
                              .damage = 0,
                              .exp_reward = 20,
                              .attack_enter_radius = 34.0f,
                              .attack_exit_radius = 40.0f,
                              .attack_interval_seconds = 0.8f,
                          });
  cfg.enemies.emplace(3u, EnemyTypeConfig{
                              .type_id = 3,
                              .name = "铁桶僵尸",
                              .max_health = 120,
                              .move_speed = 40.0f,
                              .damage = 0,
                              .exp_reward = 40,
                              .attack_enter_radius = 34.0f,
                              .attack_exit_radius = 40.0f,
                              .attack_interval_seconds = 0.8f,
                          });
  cfg.enemies.emplace(4u, EnemyTypeConfig{
                              .type_id = 4,
                              .name = "橄榄球僵尸",
                              .max_health = 80,
                              .move_speed = 100.0f,
                              .damage = 0,
                              .exp_reward = 50,
                              .attack_enter_radius = 34.0f,
                              .attack_exit_radius = 40.0f,
                              .attack_interval_seconds = 0.8f,
                          });

  cfg.spawn_type_ids = {1, 2, 3, 4};
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

void ExtractFloat(const std::string& content, std::string_view key, float* out) {
  if (out == nullptr) {
    return;
  }
  std::regex re(std::string("\"") + std::string(key) +
                "\"\\s*:\\s*(\\d+\\.?\\d*)");
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
}  // namespace

bool LoadEnemyTypesConfig(EnemyTypesConfig* out) {
  if (out == nullptr) {
    return false;
  }

  const EnemyTypesConfig fallback = BuildDefaultEnemyTypesConfig();

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

  EnemyTypesConfig cfg;
  cfg.default_type_id = fallback.default_type_id;
  cfg.enemies.clear();
  cfg.spawn_type_ids.clear();

  ExtractUint(content, "default_type_id", &cfg.default_type_id);

  // 粗解析：匹配包含 type_id 的对象（本项目不引入完整 JSON 依赖，避免额外构建成本）
  std::regex enemy_obj_re("\\{[^\\{\\}]*\"type_id\"\\s*:\\s*(\\d+)[^\\{\\}]*\\}");
  std::size_t parsed = 0;
  for (std::sregex_iterator it(content.begin(), content.end(), enemy_obj_re),
       end;
       it != end; ++it) {
    const std::string obj = it->str();

    EnemyTypeConfig enemy;
    ExtractUint(obj, "type_id", &enemy.type_id);
    ExtractString(obj, "name", &enemy.name);

    uint32_t max_health_u =
        static_cast<uint32_t>(std::max<int32_t>(1, enemy.max_health));
    ExtractUint(obj, "max_health", &max_health_u);
    enemy.max_health =
        static_cast<int32_t>(ClampUInt32(max_health_u, 1u, 1000000u));

    ExtractFloat(obj, "move_speed", &enemy.move_speed);
    enemy.move_speed = std::clamp(enemy.move_speed, 0.0f, 5000.0f);

    uint32_t damage_u = static_cast<uint32_t>(std::max<int32_t>(0, enemy.damage));
    ExtractUint(obj, "damage", &damage_u);
    enemy.damage = static_cast<int32_t>(ClampUInt32(damage_u, 0u, 100000u));

    uint32_t exp_u =
        static_cast<uint32_t>(std::max<int32_t>(0, enemy.exp_reward));
    ExtractUint(obj, "exp_reward", &exp_u);
    enemy.exp_reward = static_cast<int32_t>(ClampUInt32(exp_u, 0u, 1000000u));

    float attack_enter = enemy.attack_enter_radius;
    ExtractFloat(obj, "attack_enter_radius", &attack_enter);
    attack_enter = std::clamp(attack_enter, 0.0f, 1000.0f);

    float attack_exit = enemy.attack_exit_radius;
    ExtractFloat(obj, "attack_exit_radius", &attack_exit);
    attack_exit = std::clamp(attack_exit, 0.0f, 1000.0f);

    if (attack_enter <= 0.0f) {
      attack_enter = enemy.attack_enter_radius;
    }
    if (attack_exit <= 0.0f) {
      attack_exit = attack_enter;
    }
    if (attack_exit < attack_enter) {
      attack_exit = attack_enter;
    }
    enemy.attack_enter_radius = attack_enter;
    enemy.attack_exit_radius = attack_exit;

    float attack_interval = enemy.attack_interval_seconds;
    ExtractFloat(obj, "attack_interval_seconds", &attack_interval);
    attack_interval = std::clamp(attack_interval, 0.05f, 10.0f);
    if (attack_interval <= 0.0f) {
      attack_interval = enemy.attack_interval_seconds;
    }
    enemy.attack_interval_seconds = attack_interval;

    if (enemy.type_id == 0) {
      continue;
    }
    if (enemy.name.empty()) {
      enemy.name = "敌人" + std::to_string(enemy.type_id);
    }

    cfg.enemies[enemy.type_id] = std::move(enemy);
    parsed += 1;
  }

  if (parsed == 0) {
    *out = fallback;
    return false;
  }

  if (cfg.default_type_id == 0) {
    cfg.default_type_id = 1;
  }

  cfg.spawn_type_ids.reserve(cfg.enemies.size());
  for (const auto& [type_id, _] : cfg.enemies) {
    cfg.spawn_type_ids.push_back(type_id);
  }
  std::sort(cfg.spawn_type_ids.begin(), cfg.spawn_type_ids.end());

  if (cfg.enemies.find(cfg.default_type_id) == cfg.enemies.end()) {
    cfg.default_type_id =
        !cfg.spawn_type_ids.empty() ? cfg.spawn_type_ids.front()
                                    : fallback.default_type_id;
  }

  *out = std::move(cfg);
  return true;
}
