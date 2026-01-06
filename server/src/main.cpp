#include <array>
#include <fstream>
#include <regex>
#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"
#include "message.pb.h"
#include "network/tcp/tcp_server.hpp"
#include "network/udp/udp_server.hpp"

namespace {
uint16_t LoadPortFromConfig(std::string_view key, uint16_t fallback) {
  constexpr std::array<std::string_view, 3> kPaths = {
      "config/server_config.json", "../config/server_config.json",
      "server/config/server_config.json"};

  for (const auto path : kPaths) {
    std::ifstream file{std::string(path)};
    if (!file.is_open()) {
      continue;
    }
    const std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    std::regex re(std::string("\"") + std::string(key) + "\"\\s*:\\s*(\\d+)");
    std::smatch match;
    if (std::regex_search(content, match, re) && match.size() > 1) {
      try {
        return static_cast<uint16_t>(std::stoul(match[1].str()));
      } catch (...) {
      }
    }
  }
  return fallback;
}
}  // namespace

int main() {
  try {
    asio::io_context io;
    const uint16_t tcp_port = LoadPortFromConfig("tcp_port", 7777);
    const uint16_t udp_port = LoadPortFromConfig("udp_port", 7778);

    GameManager::Instance().SetIoContext(&io);
    UdpServer udp_server(io, udp_port);
    GameManager::Instance().SetUdpServer(&udp_server);
    TcpServer tcp_server(io, tcp_port);

    // 默认使用 info 级别，避免大量调试日志阻塞 IO 线程
    // spdlog::set_level(spdlog::level::info);
    spdlog::set_level(spdlog::level::debug);
    // Keep the same timestamp/level/message layout, but wrap the level with
    // color markers so the console sink prints it with ANSI colors when the
    // output is a TTY.
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e %^[%l]%$ %v");
    spdlog::info("服务器启动，TCP 端口 {}，UDP 端口 {}", tcp_port, udp_port);
    udp_server.Start();
    tcp_server.start();

    io.run();
  } catch (std::exception& e) {
    spdlog::error("错误: {}", e.what());
  }
  return 0;
}
