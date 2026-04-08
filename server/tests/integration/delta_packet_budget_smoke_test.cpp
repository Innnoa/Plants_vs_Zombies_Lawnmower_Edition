#include <algorithm>
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
      (fs::temp_directory_path() / "delta-packet-budget-smoke-XXXXXX").string();
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
                    "  \"room_sync_object_budget_per_tick\": 256,\n"
                    "  \"room_event_entry_budget_per_tick\": 256,\n"
                    "  \"room_packet_budget_per_tick\": 1,\n"
                    "  \"map_width\": 2000,\n"
                    "  \"map_height\": 2000,\n"
                    "  \"enemy_spawn_base_per_second\": 240,\n"
                    "  \"enemy_spawn_per_player_per_second\": 0,\n"
                    "  \"enemy_spawn_wave_growth_per_second\": 0,\n"
                    "  \"max_enemies_alive\": 192,\n"
                    "  \"max_enemy_spawn_per_tick\": 48,\n"
                    "  \"max_enemy_replan_per_tick\": 64,\n"
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
                "      \"name\": \"Delta Budget Tester\",\n"
                "      \"max_health\": 100,\n"
                "      \"attack\": 0,\n"
                "      \"attack_speed\": 1,\n"
                "      \"move_speed\": 200,\n"
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
                "      \"name\": \"Delta Budget Zombie\",\n"
                "      \"max_health\": 100,\n"
                "      \"move_speed\": 90,\n"
                "      \"damage\": 0,\n"
                "      \"exp_reward\": 0,\n"
                "      \"drop_chance\": 0,\n"
                "      \"attack_enter_radius\": 34,\n"
                "      \"attack_exit_radius\": 40,\n"
                "      \"attack_interval_seconds\": 1.0\n"
                "    }\n"
                "  ]\n"
                "}\n");

  WriteTextFile(cfg_dir / "items_config.json",
                "{\n"
                "  \"default_type_id\": 1,\n"
                "  \"max_items_alive\": 8,\n"
                "  \"pick_radius\": 24,\n"
                "  \"items\": [\n"
                "    {\n"
                "      \"type_id\": 1,\n"
                "      \"name\": \"placeholder\",\n"
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

  void Drain(int idle_timeout_ms, int max_packets = 64) const {
    for (int i = 0; i < max_packets; ++i) {
      auto packet = ReceiveOnce(idle_timeout_ms);
      if (!packet.has_value()) {
        return;
      }
    }
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
    pollfd pfd{fd_, POLLIN, 0};
    const int ready = ::poll(&pfd, 1, timeout_ms);
    if (ready < 0) {
      Fail("UDP poll 失败: " + std::string(std::strerror(errno)));
    }
    if (ready == 0 || (pfd.revents & POLLIN) == 0) {
      return std::nullopt;
    }

    std::array<char, 64 * 1024> buf{};
    const ssize_t bytes = ::recv(fd_, buf.data(), buf.size(), 0);
    if (bytes < 0) {
      Fail("UDP recv 失败: " + std::string(std::strerror(errno)));
    }

    lawnmower::Packet packet;
    if (!packet.ParseFromArray(buf.data(), static_cast<int>(bytes))) {
      Fail("UDP Packet 解析失败");
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
    Fail("消息解析失败");
  }
  return msg;
}

void RunSmoke(const std::string& server_path) {
  const uint16_t tcp_port = ReservePort(SOCK_STREAM);
  const uint16_t udp_port = ReservePort(SOCK_DGRAM);
  const fs::path workspace = CreateTempWorkspace();
  WriteTestConfigs(workspace, tcp_port, udp_port);

  ServerProcess server(server_path, workspace);
  TcpClient host("127.0.0.1", tcp_port, 5000);

  lawnmower::C2S_Login login;
  login.set_player_name("delta_budget_host");
  host.Send(lawnmower::MSG_C2S_LOGIN, login);
  const auto login_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_LOGIN_RESULT, 3000);
  const auto login_result =
      ParsePayload<lawnmower::S2C_LoginResult>(login_packet);
  Require(login_result.success(), "delta packet budget smoke: 登录失败");

  lawnmower::C2S_CreateRoom create_room;
  create_room.set_room_name("delta_budget_room");
  host.Send(lawnmower::MSG_C2S_CREATE_ROOM, create_room);
  const auto create_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_CREATE_ROOM_RESULT, 3000);
  const auto create_result =
      ParsePayload<lawnmower::S2C_CreateRoomResult>(create_packet);
  Require(create_result.success(), "delta packet budget smoke: 建房失败");
  const uint32_t room_id = create_result.room_id();

  lawnmower::C2S_StartGame start_game;
  host.Send(lawnmower::MSG_C2S_START_GAME, start_game);
  const auto game_start_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_GAME_START, 3000);
  const auto game_start =
      ParsePayload<lawnmower::S2C_GameStart>(game_start_packet);
  Require(game_start.success(), "delta packet budget smoke: 开局失败");

  UdpClient udp("127.0.0.1", udp_port);
  uint32_t input_seq = 1;
  auto send_udp_hello = [&]() {
    lawnmower::C2S_PlayerInput input;
    input.set_player_id(login_result.player_id());
    input.set_is_attacking(false);
    input.set_input_seq(input_seq++);
    input.set_delta_ms(40);
    input.set_session_token(login_result.session_token());
    udp.Send(lawnmower::MSG_C2S_PLAYER_INPUT, input);
  };

  host.Drain(8);

  std::unordered_map<uint32_t, uint32_t> packets_per_tick;
  uint32_t total_delta_packets = 0;
  uint32_t total_enemy_entries = 0;
  uint32_t distinct_ticks = 0;
  uint32_t last_tick = 0;
  bool has_last_tick = false;
  auto deadline = Clock::now() + std::chrono::seconds(15);
  auto next_send = Clock::now();
  while (Clock::now() < deadline) {
    if (Clock::now() >= next_send) {
      send_udp_hello();
      next_send += std::chrono::milliseconds(40);
    }

    host.Drain(4);

    auto packet = udp.ReceiveOnce(80);
    if (!packet.has_value()) {
      continue;
    }
    if (packet->msg_type() != lawnmower::MSG_S2C_GAME_STATE_DELTA_SYNC) {
      continue;
    }

    const auto delta = ParsePayload<lawnmower::S2C_GameStateDeltaSync>(*packet);
    if (delta.room_id() != room_id) {
      continue;
    }

    const uint32_t tick = delta.sync_time().tick();
    if (!has_last_tick || tick > last_tick) {
      distinct_ticks += 1;
    }
    has_last_tick = true;
    last_tick = tick;

    packets_per_tick[tick] += 1;
    total_delta_packets += 1;
    total_enemy_entries += static_cast<uint32_t>(delta.enemies_size());

    if (distinct_ticks >= 3 && total_delta_packets >= 4 &&
        total_enemy_entries >= 48) {
      break;
    }
  }

  Require(total_delta_packets >= 3,
          "delta packet budget smoke: delta 包数量过少");
  Require(total_enemy_entries >= 32,
          "delta packet budget smoke: enemy delta 样本不足");
  Require(distinct_ticks >= 2,
          "delta packet budget smoke: 未观察到 backlog 跨 tick 续发");

  uint32_t max_packets_same_tick = 0;
  for (const auto& [_, count] : packets_per_tick) {
    max_packets_same_tick = std::max(max_packets_same_tick, count);
  }
  Require(max_packets_same_tick <= 1,
          "delta packet budget smoke: 同一 tick delta 分片包数超预算");

  server.Stop();
  std::error_code ec;
  fs::remove_all(workspace, ec);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "用法: delta_packet_budget_smoke_test <server_binary>\n";
    return 2;
  }

  try {
    RunSmoke(argv[1]);
    std::cout << "delta_packet_budget_smoke_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "delta_packet_budget_smoke_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
