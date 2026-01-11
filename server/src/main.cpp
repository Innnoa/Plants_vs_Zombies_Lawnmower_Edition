#include <spdlog/spdlog.h>

#include "config/server_config.hpp"
#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "message.pb.h"
#include "network/tcp/tcp_server.hpp"
#include "network/udp/udp_server.hpp"

int main() {
  try {
    ServerConfig config;
    // 加载配置
    const bool loaded = LoadServerConfig(&config);
    if (!loaded) {
      spdlog::warn("未找到配置文件，使用默认配置");
    }

    // 设置io上下文
    asio::io_context io;
    // 设置游戏管理与房间管理基本配置
    GameManager::Instance().SetConfig(config);
    RoomManager::Instance().SetConfig(config);

    // 单例设置游戏管理io上下文
    GameManager::Instance().SetIoContext(&io);
    // 设置udp服务器io上下文与端口
    UdpServer udp_server(io, config.udp_port);
    // 单例设置游戏管理udp服务器
    GameManager::Instance().SetUdpServer(&udp_server);
    // 设置tcp服务器io上下文与端口
    TcpServer tcp_server(io, config.tcp_port);

    // 设置日志默认等级
    spdlog::level::level_enum level = spdlog::level::info;
    try {
      // 获取配置中日志等级
      level = spdlog::level::from_str(config.log_level);
    } catch (...) {
      spdlog::warn("日志等级 {} 不合法，使用 info", config.log_level);
    }
    spdlog::set_level(level);
    // Keep the same timestamp/level/message layout, but wrap the level with
    // color markers so the console sink prints it with ANSI colors when the
    // output is a TTY.
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e %^[%l]%$ %v");
    spdlog::info("服务器启动，TCP 端口 {}，UDP 端口 {}", config.tcp_port,
                 config.udp_port);
    udp_server.Start();
    tcp_server.start();

    // 不知道干什么用的
    io.run();
  } catch (std::exception& e) {
    spdlog::error("错误: {}", e.what());
  }
  return 0;
}
