#include "config/server_config.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <google/protobuf/struct.pb.h>
#include <google/protobuf/util/json_util.h>
#include <limits>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>

// 为什么提取函数要放在namespace里
namespace {
// 搜寻config文件
constexpr std::array<const char*, 3> kConfigPaths = {
    "game_config/server_config.json", "../game_config/server_config.json",
    "../../game_config/server_config.json"};

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
}  // namespace

// 加载服务器配置
bool LoadServerConfig(ServerConfig* out) {
  if (out == nullptr) {
    return false;
  }

  ServerConfig cfg;
  std::ifstream file;
  // 查找可用配置路径
  for (const auto* path : kConfigPaths) {
    file = std::ifstream(path);
    if (file.is_open()) {
      break;
    }
  }
  if (!file.is_open()) {
    *out = cfg;
    return false;
  }

  // string特殊构造，接受两个迭代器，迭代器活动并将内容存至content
  const std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

  google::protobuf::Struct root;
  const auto status =
      google::protobuf::util::JsonStringToMessage(content, &root);
  if (!status.ok()) {
    spdlog::warn("server_config.json 解析失败：{}，使用默认配置",
                 status.ToString());
    *out = cfg;
    return false;
  }

  // 提取各配置
  ExtractUint(root, "tcp_port", &cfg.tcp_port);
  ExtractUint(root, "udp_port", &cfg.udp_port);
  ExtractUint(root, "max_players_per_room", &cfg.max_players_per_room);
  ExtractUint(root, "tick_rate", &cfg.tick_rate);
  ExtractUint(root, "state_sync_rate", &cfg.state_sync_rate);
  ExtractFloat(root, "sync_idle_light_seconds", &cfg.sync_idle_light_seconds);
  ExtractFloat(root, "sync_idle_heavy_seconds", &cfg.sync_idle_heavy_seconds);
  ExtractFloat(root, "sync_scale_light", &cfg.sync_scale_light);
  ExtractFloat(root, "sync_scale_medium", &cfg.sync_scale_medium);
  ExtractFloat(root, "sync_scale_idle", &cfg.sync_scale_idle);
  ExtractUint(root, "map_width", &cfg.map_width);
  ExtractUint(root, "map_height", &cfg.map_height);
  ExtractFloat(root, "move_speed", &cfg.move_speed);
  ExtractFloat(root, "prediction_history_seconds",
               &cfg.prediction_history_seconds);
  ExtractFloat(root, "wave_interval_seconds", &cfg.wave_interval_seconds);
  ExtractFloat(root, "enemy_spawn_base_per_second",
               &cfg.enemy_spawn_base_per_second);
  ExtractFloat(root, "enemy_spawn_per_player_per_second",
               &cfg.enemy_spawn_per_player_per_second);
  ExtractFloat(root, "enemy_spawn_wave_growth_per_second",
               &cfg.enemy_spawn_wave_growth_per_second);
  ExtractUint(root, "max_enemies_alive", &cfg.max_enemies_alive);
  ExtractUint(root, "max_enemy_spawn_per_tick", &cfg.max_enemy_spawn_per_tick);
  ExtractUint(root, "max_enemy_replan_per_tick",
              &cfg.max_enemy_replan_per_tick);
  ExtractFloat(root, "projectile_speed", &cfg.projectile_speed);
  ExtractFloat(root, "projectile_radius", &cfg.projectile_radius);
  ExtractFloat(root, "projectile_muzzle_offset", &cfg.projectile_muzzle_offset);
  ExtractFloat(root, "projectile_ttl_seconds", &cfg.projectile_ttl_seconds);
  ExtractUint(root, "projectile_max_shots_per_tick",
              &cfg.projectile_max_shots_per_tick);
  ExtractFloat(root, "projectile_attack_min_interval_seconds",
               &cfg.projectile_attack_min_interval_seconds);
  ExtractFloat(root, "projectile_attack_max_interval_seconds",
               &cfg.projectile_attack_max_interval_seconds);
  ExtractFloat(root, "reconnect_grace_seconds", &cfg.reconnect_grace_seconds);
  ExtractUint(root, "perf_sample_stride", &cfg.perf_sample_stride);
  ExtractUint(root, "tcp_packet_debug_log_stride",
              &cfg.tcp_packet_debug_log_stride);
  ExtractString(root, "log_level", &cfg.log_level);

  cfg.prediction_history_seconds =
      std::clamp(cfg.prediction_history_seconds, 0.1f, 30.0f);

  cfg.sync_idle_light_seconds =
      std::clamp(cfg.sync_idle_light_seconds, 0.0f, 120.0f);
  cfg.sync_idle_heavy_seconds = std::clamp(cfg.sync_idle_heavy_seconds,
                                           cfg.sync_idle_light_seconds, 300.0f);
  cfg.sync_scale_light = std::clamp(cfg.sync_scale_light, 1.0f, 20.0f);
  cfg.sync_scale_medium =
      std::clamp(cfg.sync_scale_medium, cfg.sync_scale_light, 20.0f);
  cfg.sync_scale_idle =
      std::clamp(cfg.sync_scale_idle, cfg.sync_scale_medium, 30.0f);
  cfg.reconnect_grace_seconds =
      std::clamp(cfg.reconnect_grace_seconds, 1.0f, 600.0f);
  cfg.max_enemy_replan_per_tick =
      std::max<uint32_t>(1, cfg.max_enemy_replan_per_tick);
  cfg.perf_sample_stride = std::max<uint32_t>(1, cfg.perf_sample_stride);
  cfg.tcp_packet_debug_log_stride =
      std::max<uint32_t>(1, cfg.tcp_packet_debug_log_stride);

  *out = cfg;
  return true;
}
