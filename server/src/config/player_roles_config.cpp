#include "config/player_roles_config.hpp"

#include <algorithm>
#include <array>
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

  google::protobuf::Struct root;
  const auto status =
      google::protobuf::util::JsonStringToMessage(content, &root);
  if (!status.ok()) {
    spdlog::warn("player_roles.json 解析失败：{}，使用默认配置",
                 status.ToString());
    *out = fallback;
    return false;
  }

  PlayerRolesConfig cfg;
  cfg.default_role_id = fallback.default_role_id;
  cfg.roles.clear();

  ExtractUint(root, "default_role_id", &cfg.default_role_id);

  const google::protobuf::Value* roles_field = FindField(root, "roles");
  if (roles_field != nullptr &&
      roles_field->kind_case() != google::protobuf::Value::kListValue) {
    spdlog::warn("配置项 roles 类型错误，期望 array，使用默认配置");
    *out = fallback;
    return false;
  }

  std::size_t parsed = 0;
  if (roles_field != nullptr) {
    for (const auto& value : roles_field->list_value().values()) {
      if (value.kind_case() != google::protobuf::Value::kStructValue) {
        spdlog::warn("roles 数组存在非 object 元素，已忽略");
        continue;
      }

      const auto& obj = value.struct_value();
      PlayerRoleConfig role;
      ExtractUint(obj, "role_id", &role.role_id);
      ExtractString(obj, "name", &role.name);

      uint32_t max_health_u =
          static_cast<uint32_t>(std::max<int32_t>(1, role.max_health));
      ExtractUint(obj, "max_health", &max_health_u);
      role.max_health =
          static_cast<int32_t>(ClampUInt32(max_health_u, 1u, 100000u));

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
