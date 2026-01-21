#include "config/player_roles_config.hpp"

#include <array>
#include <algorithm>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>

namespace {
constexpr std::array<const char*, 3> kConfigPaths = {
    "game_config/player_roles.json", "../game_config/player_roles.json",
    "../../game_config/player_roles.json"};

PlayerRolesConfig BuildDefaultPlayerRolesConfig() {
  PlayerRolesConfig cfg;
  cfg.default_role_id = 1;

  cfg.roles.emplace(1u, PlayerRoleConfig{
                            .role_id = 1,
                            .name = "豌豆射手",
                            .max_health = 100,
                            .attack = 10,
                            .attack_speed = 2,
                            .move_speed = 200.0f,
                            .critical_hit_rate = 50,
                        });
  cfg.roles.emplace(2u, PlayerRoleConfig{
                            .role_id = 2,
                            .name = "坦克",
                            .max_health = 180,
                            .attack = 8,
                            .attack_speed = 1,
                            .move_speed = 170.0f,
                            .critical_hit_rate = 0,
                        });
  cfg.roles.emplace(3u, PlayerRoleConfig{
                            .role_id = 3,
                            .name = "速射手",
                            .max_health = 80,
                            .attack = 6,
                            .attack_speed = 4,
                            .move_speed = 210.0f,
                            .critical_hit_rate = 100,
                        });
  cfg.roles.emplace(4u, PlayerRoleConfig{
                            .role_id = 4,
                            .name = "狙击手",
                            .max_health = 90,
                            .attack = 18,
                            .attack_speed = 1,
                            .move_speed = 190.0f,
                            .critical_hit_rate = 150,
                        });
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

bool LoadPlayerRolesConfig(PlayerRolesConfig* out) {
  if (out == nullptr) {
    return false;
  }

  const PlayerRolesConfig fallback = BuildDefaultPlayerRolesConfig();

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

  PlayerRolesConfig cfg;
  cfg.default_role_id = fallback.default_role_id;
  cfg.roles.clear();

  ExtractUint(content, "default_role_id", &cfg.default_role_id);

  // 粗解析：匹配包含 role_id 的对象（本项目不引入完整 JSON 依赖，避免额外构建成本）
  std::regex role_obj_re("\\{[^\\{\\}]*\"role_id\"\\s*:\\s*(\\d+)[^\\{\\}]*\\}");
  std::size_t parsed = 0;
  for (std::sregex_iterator it(content.begin(), content.end(), role_obj_re),
       end;
       it != end; ++it) {
    const std::string obj = it->str();

    PlayerRoleConfig role;
    ExtractUint(obj, "role_id", &role.role_id);
    ExtractString(obj, "name", &role.name);

    uint32_t max_health_u =
        static_cast<uint32_t>(std::max<int32_t>(1, role.max_health));
    ExtractUint(obj, "max_health", &max_health_u);
    role.max_health = static_cast<int32_t>(ClampUInt32(max_health_u, 1u, 100000u));

    ExtractUint(obj, "attack", &role.attack);
    role.attack = ClampUInt32(role.attack, 0u, 100000u);

    ExtractUint(obj, "attack_speed", &role.attack_speed);
    role.attack_speed = ClampUInt32(role.attack_speed, 1u, 1000u);

    ExtractFloat(obj, "move_speed", &role.move_speed);
    role.move_speed = std::clamp(role.move_speed, 0.0f, 5000.0f);

    ExtractUint(obj, "critical_hit_rate", &role.critical_hit_rate);
    role.critical_hit_rate = ClampUInt32(role.critical_hit_rate, 0u, 1000u);

    if (role.role_id == 0) {
      continue;
    }
    if (role.name.empty()) {
      role.name = "职业" + std::to_string(role.role_id);
    }

    cfg.roles[role.role_id] = std::move(role);
    parsed += 1;
  }

  if (parsed == 0) {
    *out = fallback;
    return false;
  }

  if (cfg.default_role_id == 0) {
    cfg.default_role_id = 1;
  }

  if (cfg.roles.find(cfg.default_role_id) == cfg.roles.end()) {
    uint32_t best_id = 0;
    for (const auto& [role_id, _] : cfg.roles) {
      if (best_id == 0 || role_id < best_id) {
        best_id = role_id;
      }
    }
    cfg.default_role_id = best_id != 0 ? best_id : fallback.default_role_id;
  }

  *out = std::move(cfg);
  return true;
}
