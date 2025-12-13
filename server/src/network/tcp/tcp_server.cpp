#include "network/tcp/tcp_server.hpp"

TcpSession::TcpSession(tcp::socket socket) : socket_(std::move(socket)) {}

void TcpSession::start() { do_read(); }

void TcpSession::send(const std::string& data) {
  write_data_ = data;
  do_write();
}

void TcpSession::do_read() {
  auto self = shared_from_this();
  socket_.async_read_some(asio::buffer(buffer_),
    [this, self](const asio::error_code& ec,
    std::size_t bytes_transferred) {
      if (ec) {
        return;
      }
    write_data_.assign(buffer_.data(), bytes_transferred);
    do_write();
    }
  );
}

void TcpSession::do_write() {
  auto self = shared_from_this();
  asio::async_write(socket_, asio::buffer(write_data_),
    [this, self](const asio::error_code& ec, std::size_t) {
      if (ec) {
        return;
      }
      do_read();
    }
  );
}

TcpServer::TcpServer(asio::io_context& io, uint16_t port)
    : io_context_(io),
      acceptor_(io_context_, tcp::endpoint(tcp::v4(), port)) {}

void TcpServer::start() { do_accept(); }

void TcpServer::do_accept() {
  acceptor_.async_accept([this](const asio::error_code& ec, tcp::socket socket) {
    if (!ec) {
      std::make_shared<TcpSession>(std::move(socket))->start();
    }
    do_accept();
  });
}
