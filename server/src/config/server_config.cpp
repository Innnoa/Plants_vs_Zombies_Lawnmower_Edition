#include "config/server_config.hpp"

#include <array>
#include <fstream>
#include <regex>
#include <string>

namespace {
constexpr std::array<const char*, 3> kConfigPaths = {
    "config/server_config.json", "../config/server_config.json",
    "server/config/server_config.json"};

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
}  // namespace

bool LoadServerConfig(ServerConfig* out) {
  if (out == nullptr) {
    return false;
  }

  ServerConfig cfg;
  std::ifstream file;
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

  const std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

  ExtractUint(content, "tcp_port", &cfg.tcp_port);
  ExtractUint(content, "udp_port", &cfg.udp_port);
  ExtractUint(content, "max_players_per_room", &cfg.max_players_per_room);
  ExtractUint(content, "tick_rate", &cfg.tick_rate);
  ExtractUint(content, "state_sync_rate", &cfg.state_sync_rate);
  ExtractUint(content, "map_width", &cfg.map_width);
  ExtractUint(content, "map_height", &cfg.map_height);
  ExtractFloat(content, "move_speed", &cfg.move_speed);
  ExtractString(content, "log_level", &cfg.log_level);

  *out = cfg;
  return true;
}
