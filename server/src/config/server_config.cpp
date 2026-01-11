#include "config/server_config.hpp"

#include <array>
#include <fstream>
#include <regex>
#include <string>

// 为什么提取函数要放在namespace里
namespace {
  // 搜寻config文件
constexpr std::array<const char*, 3> kConfigPaths = {
    "config/server_config.json", "../config/server_config.json",
    "server/config/server_config.json"};

// T可能为uint16_t/uint32_t，故需模板
// 该函数用于提取配置中的int类值
template <typename T>
void ExtractUint(const std::string& content, std::string_view key, T* out) {
  if (out == nullptr) {
    return;
  }
  // 用于在文件中(server_config.json)找对应字符(tcp_port和udp_port)的属性，这是一个正则表达式匹配规则
  // \s* 的意思是若干个填充符，()代表一个捕获组[],\d+(代表至少一个数字)
  std::regex re(std::string("\"") + std::string(key) + "\"\\s*:\\s*(\\d+)");
  // 用于存储字符串匹配结果
  std::smatch match;
  // 根据re规则查询保存文件内容的content的匹配项，并将其存至match
  if (std::regex_search(content, match, re) && match.size() > 1) {
    try {
      // stoull --> (string -> unsigned longlong)
      // static_cast --> (ull -> T)
      // 因传入的是引用（地址），即会同步修改实体值
      *out = static_cast<T>(std::stoull(match[1].str()));
    } catch (...) {
    }
  }
}

// 该函数用于提取配置中的float类值
void ExtractFloat(const std::string& content, std::string_view key,
                  float* out) {
  if (out == nullptr) {
    return;
  }
  // 这是一个正则表达式匹配规则
  std::regex re(std::string("\"") + std::string(key) +
                "\"\\s*:\\s*(\\d+\\.?\\d*)");
  // 用于存储字符串匹配结果
  std::smatch match;
  if (std::regex_search(content, match, re) && match.size() > 1) {
    try {
      *out = std::stof(match[1].str());
    } catch (...) {
    }
  }
}

// 该函数用于提取配置中的string类值
void ExtractString(const std::string& content, std::string_view key,
                   std::string* out) {
  if (out == nullptr) {
    return;
  }
  // 这是一个正则表达式匹配规则
  std::regex re(std::string("\"") + std::string(key) +
                "\"\\s*:\\s*\"([^\"]*)\"");
  // 用于存储字符串匹配结果
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
