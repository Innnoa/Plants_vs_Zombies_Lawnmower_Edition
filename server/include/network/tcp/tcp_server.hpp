#pragma once

#include <array>
#include <asio.hpp>
#include <functional>
#include <memory>
#include <string>

using asio::ip::tcp;

class TcpSession : public std::enable_shared_from_this<TcpSession> {
 public:
  explicit TcpSession(tcp::socket socket);
  void start();
  void send(const std::string& data);

 private:
  void do_read();
  void do_write();

  tcp::socket socket_;
  std::array<char, 1024> buffer_{};
  std::string write_data_;
};

class TcpServer {
 public:
  TcpServer(asio::io_context& io, uint16_t port);
  void start();

 private:
  void do_accept();

  asio::io_context& io_context_;
  tcp::acceptor acceptor_;
};
