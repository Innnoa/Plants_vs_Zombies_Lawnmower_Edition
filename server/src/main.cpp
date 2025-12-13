#include <spdlog/spdlog.h>
#include "network/tcp/tcp_server.hpp"

int main() {
    try {
        asio::io_context io;
        TcpServer server(io, 7777);
        
        spdlog::info("服务器启动，监听端口 7777");
        server.start();
        
        io.run();
    } catch (std::exception& e) {
        spdlog::error("错误: {}", e.what());
    }
    return 0;
}
