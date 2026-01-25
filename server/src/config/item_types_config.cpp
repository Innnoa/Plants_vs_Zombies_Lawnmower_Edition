#include "config/item_types_config.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>

namespace {
constexpr std::array<const char*, 3> kConfigPaths = {
    "game_config/items_config.json", "../game_config/items_config.json",
    "../../game_config/items_config.json"};

ItemsConfig BuildDefaultItemsConfig() {
  ItemsConfig cfg;
  cfg.default_type_id = 1;
  cfg.spawn_interval_seconds = 8.0f;
  cfg.max_items_alive = 6;
  cfg.pick_radius = 24.0f;

  cfg.items.emplace(1u, ItemTypeConfig{
                            .type_id = 1,
                            .name = "回血道具",
                            .effect = "heal",
                            .value = 30,
                            .can_spawn = true,
                        });
  cfg.items.emplace(2u, ItemTypeConfig{
                            .type_id = 2,
                            .name = "经验道具",
                            .effect = "exp",
                            .value = 10,
                            .can_spawn = false,
                        });
  cfg.items.emplace(3u, ItemTypeConfig{
                            .type_id = 3,
                            .name = "加速道具",
                            .effect = "speed",
                            .value = 5,
                            .can_spawn = false,
                        });

  cfg.spawn_type_ids = {1};
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

void ExtractBool(const std::string& content, std::string_view key, bool* out) {
  if (out == nullptr) {
    return;
  }
  std::regex re(std::string("\"") + std::string(key) +
                "\"\\s*:\\s*(true|false)");
  std::smatch match;
  if (std::regex_search(content, match, re) && match.size() > 1) {
    *out = match[1].str() == "true";
  }
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

  ItemsConfig cfg;
  cfg.default_type_id = fallback.default_type_id;
  cfg.spawn_interval_seconds = fallback.spawn_interval_seconds;
  cfg.max_items_alive = fallback.max_items_alive;
  cfg.pick_radius = fallback.pick_radius;
  cfg.items.clear();
  cfg.spawn_type_ids.clear();

  ExtractUint(content, "default_type_id", &cfg.default_type_id);
  ExtractFloat(content, "spawn_interval_seconds", &cfg.spawn_interval_seconds);
  ExtractUint(content, "max_items_alive", &cfg.max_items_alive);
  ExtractFloat(content, "pick_radius", &cfg.pick_radius);

  cfg.spawn_interval_seconds =
      std::clamp(cfg.spawn_interval_seconds, 0.1f, 300.0f);
  cfg.max_items_alive =
      ClampUInt32(cfg.max_items_alive, 1u, 1000u);
  cfg.pick_radius = std::clamp(cfg.pick_radius, 1.0f, 500.0f);

  std::regex obj_re(
      "\\{[^\\{\\}]*\"type_id\"\\s*:\\s*(\\d+)[^\\{\\}]*\\}");
  std::size_t parsed = 0;
  for (std::sregex_iterator it(content.begin(), content.end(), obj_re), end;
       it != end; ++it) {
    const std::string obj = it->str();

    ItemTypeConfig item;
    ExtractUint(obj, "type_id", &item.type_id);
    ExtractString(obj, "name", &item.name);
    ExtractString(obj, "effect", &item.effect);
    ExtractBool(obj, "can_spawn", &item.can_spawn);

    uint32_t value_u =
        static_cast<uint32_t>(std::max<int32_t>(0, item.value));
    ExtractUint(obj, "value", &value_u);
    item.value = static_cast<int32_t>(ClampUInt32(value_u, 0u, 100000u));

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

  if (parsed == 0) {
    *out = fallback;
    return false;
  }

  if (cfg.default_type_id == 0) {
    cfg.default_type_id = fallback.default_type_id;
  }

  for (const auto& [type_id, item] : cfg.items) {
    if (item.can_spawn) {
      cfg.spawn_type_ids.push_back(type_id);
    }
  }
  std::sort(cfg.spawn_type_ids.begin(), cfg.spawn_type_ids.end());

  if (cfg.items.find(cfg.default_type_id) == cfg.items.end()) {
    cfg.default_type_id =
        !cfg.spawn_type_ids.empty() ? cfg.spawn_type_ids.front()
                                    : fallback.default_type_id;
  }

  *out = std::move(cfg);
  return true;
}
