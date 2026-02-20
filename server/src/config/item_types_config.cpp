#include "config/item_types_config.hpp"

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
    "game_config/items_config.json", "../game_config/items_config.json",
    "../../game_config/items_config.json"};

ItemsConfig BuildDefaultItemsConfig() {
  ItemsConfig cfg;
  cfg.default_type_id = 1;
  cfg.max_items_alive = 6;
  cfg.pick_radius = 24.0f;

  cfg.items.emplace(1u, ItemTypeConfig{
                            .type_id = 1,
                            .name = "回血道具",
                            .effect = "heal",
                            .value = 30,
                            .drop_weight = 100,
                        });
  cfg.items.emplace(2u, ItemTypeConfig{
                            .type_id = 2,
                            .name = "经验道具",
                            .effect = "exp",
                            .value = 10,
                            .drop_weight = 60,
                        });
  cfg.items.emplace(3u, ItemTypeConfig{
                            .type_id = 3,
                            .name = "加速道具",
                            .effect = "speed",
                            .value = 5,
                            .drop_weight = 40,
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

bool LoadItemsConfig(ItemsConfig* out) {
  if (out == nullptr) {
    return false;
  }

  const ItemsConfig fallback = BuildDefaultItemsConfig();

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
    spdlog::warn("items_config.json 解析失败：{}，使用默认配置",
                 status.ToString());
    *out = fallback;
    return false;
  }

  ItemsConfig cfg;
  cfg.default_type_id = fallback.default_type_id;
  cfg.max_items_alive = fallback.max_items_alive;
  cfg.pick_radius = fallback.pick_radius;
  cfg.items.clear();

  ExtractUint(root, "default_type_id", &cfg.default_type_id);
  ExtractUint(root, "max_items_alive", &cfg.max_items_alive);
  ExtractFloat(root, "pick_radius", &cfg.pick_radius);

  cfg.max_items_alive = ClampUInt32(cfg.max_items_alive, 1u, 1000u);
  cfg.pick_radius = std::clamp(cfg.pick_radius, 1.0f, 500.0f);

  const google::protobuf::Value* items_field = FindField(root, "items");
  if (items_field != nullptr &&
      items_field->kind_case() != google::protobuf::Value::kListValue) {
    spdlog::warn("配置项 items 类型错误，期望 array，使用默认配置");
    *out = fallback;
    return false;
  }

  std::size_t parsed = 0;
  if (items_field != nullptr) {
    for (const auto& value : items_field->list_value().values()) {
      if (value.kind_case() != google::protobuf::Value::kStructValue) {
        spdlog::warn("items 数组存在非 object 元素，已忽略");
        continue;
      }

      const auto& obj = value.struct_value();
      ItemTypeConfig item;
      ExtractUint(obj, "type_id", &item.type_id);
      ExtractString(obj, "name", &item.name);
      ExtractString(obj, "effect", &item.effect);

      uint32_t value_u =
          static_cast<uint32_t>(std::max<int32_t>(0, item.value));
      ExtractUint(obj, "value", &value_u);
      item.value = static_cast<int32_t>(ClampUInt32(value_u, 0u, 100000u));

      uint32_t weight_u = item.drop_weight;
      ExtractUint(obj, "drop_weight", &weight_u);
      item.drop_weight = ClampUInt32(weight_u, 0u, 100000u);

      if (item.type_id == 0) {
        continue;
      }
      if (item.name.empty()) {
        item.name = "道具" + std::to_string(item.type_id);
      }
      if (item.effect.empty()) {
        item.effect = "none";
      }

      cfg.items[item.type_id] = std::move(item);
      parsed += 1;
    }
  }

  if (parsed == 0) {
    *out = fallback;
    return false;
  }

  if (cfg.default_type_id == 0) {
    cfg.default_type_id = fallback.default_type_id;
  }

  if (cfg.items.find(cfg.default_type_id) == cfg.items.end()) {
    cfg.default_type_id = !cfg.items.empty() ? cfg.items.begin()->first
                                             : fallback.default_type_id;
  }

  *out = std::move(cfg);
  return true;
}
