#include "network/tcp/tcp_server.hpp"

#include <memory>
#include <spdlog/spdlog.h>

#include "network/tcp/tcp_session.hpp"

TcpServer::TcpServer(asio::io_context& io, uint16_t port)
    : io_context_(io),
      acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)) {}

void TcpServer::start() {  // public入口函数
  do_accept();
}

void TcpServer::do_accept() {  // 创建异步非阻塞连接
  acceptor_.async_accept(
      [this](const asio::error_code& ec,
             tcp::socket socket) {  // 异步接受连接，注册回调函数
        if (ec) {
          spdlog::warn("接受连接失败: {}", ec.message());
        } else {
          std::make_shared<TcpSession>(std::move(socket))->start();
        }
        do_accept();
      });
}
