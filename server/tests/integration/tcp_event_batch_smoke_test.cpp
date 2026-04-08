#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "message.pb.h"

namespace {
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

[[noreturn]] void Fail(const std::string& msg) {
  throw std::runtime_error(msg);
}

void Require(bool cond, const std::string& msg) {
  if (!cond) {
    Fail(msg);
  }
}

uint16_t ReservePort(int socket_type) {
  const int fd = ::socket(AF_INET, socket_type, 0);
  if (fd < 0) {
    Fail("创建端口探测 socket 失败");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    const int err = errno;
    ::close(fd);
    Fail("端口探测 bind 失败: " + std::string(std::strerror(err)));
  }

  socklen_t len = sizeof(addr);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    const int err = errno;
    ::close(fd);
    Fail("端口探测 getsockname 失败: " + std::string(std::strerror(err)));
  }
  const uint16_t port = ntohs(addr.sin_port);
  ::close(fd);
  return port;
}

fs::path CreateTempWorkspace() {
  std::string pattern =
      (fs::temp_directory_path() / "tcp-event-batch-XXXXXX").string();
  std::vector<char> buf(pattern.begin(), pattern.end());
  buf.push_back('\0');
  char* created = ::mkdtemp(buf.data());
  if (created == nullptr) {
    Fail("创建临时目录失败");
  }
  return fs::path(created);
}

void WriteTextFile(const fs::path& path, const std::string& content) {
  std::ofstream out(path);
  if (!out.is_open()) {
    Fail("写入文件失败: " + path.string());
  }
  out << content;
}

void WriteTestConfigs(const fs::path& workspace, uint16_t tcp_port,
                      uint16_t udp_port) {
  const fs::path cfg_dir = workspace / "game_config";
  std::error_code ec;
  fs::create_directories(cfg_dir, ec);
  if (ec) {
    Fail("创建 game_config 目录失败: " + ec.message());
  }

  WriteTextFile(cfg_dir / "server_config.json",
                "{\n"
                "  \"tcp_port\": " +
                    std::to_string(tcp_port) +
                    ",\n"
                    "  \"udp_port\": " +
                    std::to_string(udp_port) +
                    ",\n"
                    "  \"max_players_per_room\": 1,\n"
                    "  \"tick_rate\": 30,\n"
                    "  \"state_sync_rate\": 20,\n"
                    "  \"map_width\": 1200,\n"
                    "  \"map_height\": 1200,\n"
                    "  \"enemy_spawn_base_per_second\": 30,\n"
                    "  \"enemy_spawn_per_player_per_second\": 0,\n"
                    "  \"enemy_spawn_wave_growth_per_second\": 0,\n"
                    "  \"max_enemies_alive\": 64,\n"
                    "  \"max_enemy_spawn_per_tick\": 16,\n"
                    "  \"max_enemy_replan_per_tick\": 32,\n"
                    "  \"projectile_attack_min_interval_seconds\": 0.5,\n"
                    "  \"projectile_attack_max_interval_seconds\": 1.0,\n"
                    "  \"log_level\": \"warn\"\n"
                    "}\n");

  WriteTextFile(cfg_dir / "player_roles.json",
                "{\n"
                "  \"default_role_id\": 1,\n"
                "  \"roles\": [\n"
                "    {\n"
                "      \"role_id\": 1,\n"
                "      \"name\": \"TCP batch tester\",\n"
                "      \"max_health\": 200,\n"
                "      \"attack\": 0,\n"
                "      \"attack_speed\": 1,\n"
                "      \"move_speed\": 250,\n"
                "      \"critical_hit_rate\": 0\n"
                "    }\n"
                "  ]\n"
                "}\n");

  WriteTextFile(cfg_dir / "enemy_types.json",
                "{\n"
                "  \"default_type_id\": 1,\n"
                "  \"enemies\": [\n"
                "    {\n"
                "      \"type_id\": 1,\n"
                "      \"name\": \"Batch Zombie\",\n"
                "      \"max_health\": 40,\n"
                "      \"move_speed\": 0,\n"
                "      \"damage\": 10,\n"
                "      \"exp_reward\": 5,\n"
                "      \"drop_chance\": 0,\n"
                "      \"attack_enter_radius\": 3000,\n"
                "      \"attack_exit_radius\": 3000,\n"
                "      \"attack_interval_seconds\": 1.0\n"
                "    }\n"
                "  ]\n"
                "}\n");

  WriteTextFile(cfg_dir / "items_config.json",
                "{\n"
                "  \"default_type_id\": 1,\n"
                "  \"max_items_alive\": 32,\n"
                "  \"pick_radius\": 32,\n"
                "  \"items\": [\n"
                "    {\n"
                "      \"type_id\": 1,\n"
                "      \"name\": \"batch drop\",\n"
                "      \"effect\": \"heal\",\n"
                "      \"value\": 10,\n"
                "      \"drop_weight\": 100\n"
                "    }\n"
                "  ]\n"
                "}\n");
}

class ServerProcess final {
 public:
  ServerProcess(std::string server_path, fs::path workspace)
      : workspace_(std::move(workspace)) {
    pid_ = ::fork();
    if (pid_ < 0) {
      Fail("fork 失败");
    }
    if (pid_ == 0) {
      if (::chdir(workspace_.c_str()) != 0) {
        std::fprintf(stderr, "chdir 失败: %s\n", std::strerror(errno));
        std::_Exit(127);
      }
      ::execl(server_path.c_str(), server_path.c_str(),
              static_cast<char*>(nullptr));
      std::fprintf(stderr, "exec 失败: %s\n", std::strerror(errno));
      std::_Exit(127);
    }
  }

  ~ServerProcess() { Stop(); }

  void Stop() {
    if (pid_ <= 0) {
      return;
    }
    ::kill(pid_, SIGTERM);
    int status = 0;
    ::waitpid(pid_, &status, 0);
    pid_ = -1;
  }

 private:
  pid_t pid_ = -1;
  fs::path workspace_;
};

class TcpClient final {
 public:
  TcpClient(std::string host, uint16_t port, int connect_timeout_ms) {
    const auto deadline =
        Clock::now() + std::chrono::milliseconds(connect_timeout_ms);
    while (true) {
      const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
      if (fd < 0) {
        Fail("创建 TCP 客户端 socket 失败");
      }

      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(port);
      if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        Fail("非法 IP 地址: " + host);
      }

      if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr),
                    sizeof(addr)) == 0) {
        fd_ = fd;
        return;
      }

      ::close(fd);
      if (Clock::now() >= deadline) {
        Fail("连接服务器超时");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  ~TcpClient() { Close(); }

  void Close() {
    if (fd_ >= 0) {
      ::shutdown(fd_, SHUT_RDWR);
      ::close(fd_);
      fd_ = -1;
    }
  }

  void Send(lawnmower::MessageType type,
            const google::protobuf::Message& payload) const {
    lawnmower::Packet packet;
    packet.set_msg_type(type);
    packet.set_payload(payload.SerializeAsString());

    const std::string body = packet.SerializeAsString();
    const uint32_t body_len = static_cast<uint32_t>(body.size());
    const uint32_t net_len = htonl(body_len);

    WriteExact(reinterpret_cast<const char*>(&net_len), sizeof(net_len));
    WriteExact(body.data(), body.size());
  }

  std::optional<lawnmower::Packet> ReceiveOnce(int timeout_ms) const {
    if (fd_ < 0) {
      return std::nullopt;
    }

    uint32_t net_len = 0;
    if (!ReadExact(reinterpret_cast<char*>(&net_len), sizeof(net_len),
                   timeout_ms)) {
      return std::nullopt;
    }

    const uint32_t body_len = ntohl(net_len);
    if (body_len == 0 || body_len > 1024 * 1024) {
      Fail("收到非法 TCP 包长: " + std::to_string(body_len));
    }

    std::string body(body_len, '\0');
    if (!ReadExact(body.data(), body.size(), timeout_ms)) {
      return std::nullopt;
    }

    lawnmower::Packet packet;
    if (!packet.ParseFromString(body)) {
      Fail("解析 TCP Packet 失败");
    }
    return packet;
  }

  void Drain(int entries) const {
    int drained = 0;
    while (drained < entries) {
      auto packet = ReceiveOnce(1);
      if (!packet.has_value()) {
        break;
      }
      drained += 1;
    }
  }

  lawnmower::Packet ReceiveUntil(lawnmower::MessageType type,
                                 int timeout_ms) const {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (Clock::now() < deadline) {
      const int left_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                                Clock::now())
              .count());
      if (left_ms <= 0) {
        break;
      }
      auto packet = ReceiveOnce(left_ms);
      if (!packet.has_value()) {
        continue;
      }
      if (packet->msg_type() == type) {
        return *packet;
      }
    }
    Fail("等待 TCP 消息超时: " + lawnmower::MessageType_Name(type));
  }

 private:
  void WriteExact(const char* data, std::size_t size) const {
    std::size_t sent = 0;
    while (sent < size) {
      const ssize_t n = ::send(fd_, data + sent, size - sent, MSG_NOSIGNAL);
      if (n <= 0) {
        const int err = errno;
        Fail("TCP 发送失败: " + std::string(std::strerror(err)));
      }
      sent += static_cast<std::size_t>(n);
    }
  }

  bool ReadExact(char* data, std::size_t size, int timeout_ms) const {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    std::size_t got = 0;
    while (got < size) {
      const auto left = deadline - Clock::now();
      if (left <= std::chrono::milliseconds(0)) {
        return false;
      }
      const int wait_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(left).count());
      pollfd pfd{};
      pfd.fd = fd_;
      pfd.events = POLLIN;
      const int ret = ::poll(&pfd, 1, std::max(1, wait_ms));
      if (ret == 0) {
        return false;
      }
      if (ret < 0) {
        if (errno == EINTR) {
          continue;
        }
        Fail("TCP poll 失败: " + std::string(std::strerror(errno)));
      }
      const ssize_t n = ::recv(fd_, data + got, size - got, 0);
      if (n == 0) {
        return false;
      }
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        Fail("TCP recv 失败: " + std::string(std::strerror(errno)));
      }
      got += static_cast<std::size_t>(n);
    }
    return true;
  }

  int fd_ = -1;
};

class UdpClient final {
 public:
  UdpClient(std::string host, uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
      Fail("创建 UDP 客户端 socket 失败");
    }

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(0);
    bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd_, reinterpret_cast<const sockaddr*>(&bind_addr),
               sizeof(bind_addr)) != 0) {
      const int err = errno;
      ::close(fd_);
      fd_ = -1;
      Fail("UDP bind 失败: " + std::string(std::strerror(err)));
    }

    std::memset(&server_addr_, 0, sizeof(server_addr_));
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &server_addr_.sin_addr) != 1) {
      ::close(fd_);
      fd_ = -1;
      Fail("非法 UDP 地址: " + host);
    }
  }

  ~UdpClient() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  void Send(lawnmower::MessageType type,
            const google::protobuf::Message& payload) const {
    lawnmower::Packet packet;
    packet.set_msg_type(type);
    packet.set_payload(payload.SerializeAsString());
    const std::string body = packet.SerializeAsString();
    const ssize_t sent = ::sendto(
        fd_, body.data(), body.size(), 0,
        reinterpret_cast<const sockaddr*>(&server_addr_), sizeof(server_addr_));
    if (sent < 0 || static_cast<std::size_t>(sent) != body.size()) {
      const int err = errno;
      Fail("UDP sendto 失败: " + std::string(std::strerror(err)));
    }
  }

  std::optional<lawnmower::Packet> ReceiveOnce(int timeout_ms) const {
    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    const int ret = ::poll(&pfd, 1, std::max(1, timeout_ms));
    if (ret == 0) {
      return std::nullopt;
    }
    if (ret < 0) {
      if (errno == EINTR) {
        return std::nullopt;
      }
      Fail("UDP poll 失败: " + std::string(std::strerror(errno)));
    }
    std::array<char, 65536> buf{};
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    const ssize_t bytes =
        ::recvfrom(fd_, buf.data(), buf.size(), 0,
                   reinterpret_cast<sockaddr*>(&from), &from_len);
    if (bytes <= 0) {
      return std::nullopt;
    }

    lawnmower::Packet packet;
    if (!packet.ParseFromArray(buf.data(), static_cast<int>(bytes))) {
      return std::nullopt;
    }
    return packet;
  }

 private:
  int fd_ = -1;
  sockaddr_in server_addr_{};
};

template <typename T>
T ParsePayload(const lawnmower::Packet& packet) {
  T msg;
  if (!msg.ParseFromString(packet.payload())) {
    Fail("解析 payload 失败，消息类型: " +
         lawnmower::MessageType_Name(packet.msg_type()));
  }
  return msg;
}

void RunTcpEventBatchSmoke(const std::string& server_binary) {
  const uint16_t tcp_port = ReservePort(SOCK_STREAM);
  const uint16_t udp_port = ReservePort(SOCK_DGRAM);
  const fs::path workspace = CreateTempWorkspace();
  WriteTestConfigs(workspace, tcp_port, udp_port);

  ServerProcess server(server_binary, workspace);
  std::this_thread::sleep_for(std::chrono::milliseconds(250));

  TcpClient host("127.0.0.1", tcp_port, 5000);
  lawnmower::C2S_Login login;
  login.set_player_name("event_batch_host");
  host.Send(lawnmower::MSG_C2S_LOGIN, login);
  const auto login_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_LOGIN_RESULT, 3000);
  auto login_result = ParsePayload<lawnmower::S2C_LoginResult>(login_packet);
  Require(login_result.success(), "TCP batch smoke: 登录失败");
  Require(login_result.player_id() > 0, "TCP batch smoke: player_id 非法");
  Require(!login_result.session_token().empty(),
          "TCP batch smoke: session_token 为空");

  lawnmower::C2S_CreateRoom create_room;
  create_room.set_room_name("batch_room");
  create_room.set_max_players(1);
  host.Send(lawnmower::MSG_C2S_CREATE_ROOM, create_room);
  const auto create_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_CREATE_ROOM_RESULT, 3000);
  auto create_result =
      ParsePayload<lawnmower::S2C_CreateRoomResult>(create_packet);
  Require(create_result.success(), "TCP batch smoke: 建房失败");
  const uint32_t room_id = create_result.room_id();
  Require(room_id > 0, "TCP batch smoke: room_id 非法");

  lawnmower::C2S_StartGame start_game;
  host.Send(lawnmower::MSG_C2S_START_GAME, start_game);
  auto game_start_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_GAME_START, 3000);
  auto game_start = ParsePayload<lawnmower::S2C_GameStart>(game_start_packet);
  Require(game_start.success(), "TCP batch smoke: 开局失败");
  Require(game_start.room_id() == room_id,
          "TCP batch smoke: game_start room_id 不匹配");

  bool saw_batch = false;
  uint32_t last_tick = 0;
  uint32_t batch_packets = 0;
  uint32_t max_events_in_batch = 0;

  host.Drain(8);
  auto deadline = Clock::now() + std::chrono::seconds(12);
  while (Clock::now() < deadline) {
    auto packet = host.ReceiveOnce(200);
    if (!packet.has_value()) {
      continue;
    }
    if (packet->msg_type() != lawnmower::MSG_S2C_PLAYER_HURT_BATCH) {
      continue;
    }
    const auto hurt_batch =
        ParsePayload<lawnmower::S2C_PlayerHurtBatch>(*packet);
    const auto tick = hurt_batch.has_sync_time()
                          ? hurt_batch.sync_time().tick()
                          : last_tick;
    Require(tick >= last_tick, "TCP batch smoke: tick 发生倒退");
    last_tick = tick;

    batch_packets += 1;
    max_events_in_batch = std::max<uint32_t>(
        max_events_in_batch,
        static_cast<uint32_t>(hurt_batch.events_size()));
    if (hurt_batch.events_size() >= 2) {
      saw_batch = true;
      break;
    }
  }

  Require(batch_packets >= 1, "TCP batch smoke: 未收到任何 hurt batch 包");
  Require(max_events_in_batch >= 2,
          "TCP batch smoke: 未形成同 tick 多条受伤事件");
  Require(saw_batch, "TCP batch smoke: 未观察到 batched PlayerHurt");

  server.Stop();
  std::error_code ec;
  fs::remove_all(workspace, ec);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "用法: tcp_event_batch_smoke_test <server_binary>\n";
    return 2;
  }

  try {
    RunTcpEventBatchSmoke(argv[1]);
    std::cout << "tcp_event_batch_smoke_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "tcp_event_batch_smoke_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
