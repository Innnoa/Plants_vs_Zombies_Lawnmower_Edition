#pragma once

#include <asio.hpp>
#include <cstdint>

using tcp = asio::ip::tcp;

class TcpSession;

class TcpServer {
 public:
  TcpServer(asio::io_context& io, uint16_t port);
  void start();

 private:
  void do_accept();

  asio::io_context& io_context_;
  tcp::acceptor acceptor_;
};
