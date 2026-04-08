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
      (fs::temp_directory_path() / "state-packet-budget-smoke-XXXXXX").string();
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
                    "  \"state_sync_rate\": 1,\n"
                    "  \"room_sync_object_budget_per_tick\": 1,\n"
                    "  \"room_event_entry_budget_per_tick\": 256,\n"
                    "  \"room_packet_budget_per_tick\": 1,\n"
                    "  \"map_width\": 2000,\n"
                    "  \"map_height\": 2000,\n"
                    "  \"enemy_spawn_base_per_second\": 32,\n"
                    "  \"enemy_spawn_per_player_per_second\": 0,\n"
                    "  \"enemy_spawn_wave_growth_per_second\": 0,\n"
                    "  \"max_enemies_alive\": 24,\n"
                    "  \"max_enemy_spawn_per_tick\": 12,\n"
                    "  \"max_enemy_replan_per_tick\": 32,\n"
                    "  \"projectile_attack_min_interval_seconds\": 1.0,\n"
                    "  \"projectile_attack_max_interval_seconds\": 1.0,\n"
                    "  \"reconnect_grace_seconds\": 10.0,\n"
                    "  \"log_level\": \"warn\"\n"
                    "}\n");

  WriteTextFile(cfg_dir / "player_roles.json",
                "{\n"
                "  \"default_role_id\": 1,\n"
                "  \"roles\": [\n"
                "    {\n"
                "      \"role_id\": 1,\n"
                "      \"name\": \"State Budget Tester\",\n"
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
                "      \"name\": \"State Budget Zombie\",\n"
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
      ::close(fd_);
      fd_ = -1;
    }
  }

  void Send(lawnmower::MessageType type, const google::protobuf::Message& msg) {
    lawnmower::Packet packet;
    packet.set_msg_type(type);
    packet.set_payload(msg.SerializeAsString());
    const std::string body = packet.SerializeAsString();
    const uint32_t net_len = htonl(static_cast<uint32_t>(body.size()));
    WriteAll(reinterpret_cast<const char*>(&net_len), sizeof(net_len));
    WriteAll(body.data(), body.size());
  }

  std::optional<lawnmower::Packet> ReceiveOnce(int timeout_ms) {
    pollfd pfd{fd_, POLLIN, 0};
    const int ready = ::poll(&pfd, 1, timeout_ms);
    if (ready < 0) {
      Fail("TCP poll 失败: " + std::string(std::strerror(errno)));
    }
    if (ready == 0 || (pfd.revents & POLLIN) == 0) {
      return std::nullopt;
    }

    uint32_t net_len = 0;
    ReadExact(reinterpret_cast<char*>(&net_len), sizeof(net_len));
    const uint32_t body_len = ntohl(net_len);
    std::string body(body_len, '\0');
    ReadExact(body.data(), body.size());

    lawnmower::Packet packet;
    if (!packet.ParseFromArray(body.data(), static_cast<int>(body.size()))) {
      Fail("TCP Packet 解析失败");
    }
    return packet;
  }

  lawnmower::Packet ReceiveUntil(lawnmower::MessageType expected_type,
                                 int timeout_ms) {
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
      if (packet->msg_type() == expected_type) {
        return *packet;
      }
    }
    Fail("等待指定 TCP 消息超时");
  }

  void Drain(int idle_timeout_ms, int max_packets = 64) {
    for (int i = 0; i < max_packets; ++i) {
      auto packet = ReceiveOnce(idle_timeout_ms);
      if (!packet.has_value()) {
        return;
      }
    }
  }

 private:
  void WriteAll(const char* data, std::size_t len) {
    std::size_t offset = 0;
    while (offset < len) {
      const ssize_t written =
          ::send(fd_, data + offset, len - offset, MSG_NOSIGNAL);
      if (written < 0) {
        if (errno == EINTR) {
          continue;
        }
        Fail("TCP send 失败: " + std::string(std::strerror(errno)));
      }
      offset += static_cast<std::size_t>(written);
    }
  }

  void ReadExact(char* data, std::size_t len) {
    std::size_t offset = 0;
    while (offset < len) {
      const ssize_t bytes = ::recv(fd_, data + offset, len - offset, 0);
      if (bytes < 0) {
        if (errno == EINTR) {
          continue;
        }
        Fail("TCP recv 失败: " + std::string(std::strerror(errno)));
      }
      if (bytes == 0) {
        Fail("TCP 连接被关闭");
      }
      offset += static_cast<std::size_t>(bytes);
    }
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
  login.set_player_name("state_budget_host");
  host.Send(lawnmower::MSG_C2S_LOGIN, login);
  const auto login_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_LOGIN_RESULT, 3000);
  const auto login_result =
      ParsePayload<lawnmower::S2C_LoginResult>(login_packet);
  Require(login_result.success(), "state packet budget smoke: 登录失败");

  lawnmower::C2S_CreateRoom create_room;
  create_room.set_room_name("state_budget_room");
  host.Send(lawnmower::MSG_C2S_CREATE_ROOM, create_room);
  const auto create_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_CREATE_ROOM_RESULT, 3000);
  const auto create_result =
      ParsePayload<lawnmower::S2C_CreateRoomResult>(create_packet);
  Require(create_result.success(), "state packet budget smoke: 建房失败");
  const uint32_t room_id = create_result.room_id();

  lawnmower::C2S_StartGame start_game;
  host.Send(lawnmower::MSG_C2S_START_GAME, start_game);
  bool saw_game_start = false;
  std::unordered_map<uint32_t, uint32_t> packets_per_tick;
  std::optional<uint64_t> snapshot_id;
  uint32_t expected_chunk_count = 0;
  std::set<uint32_t> chunk_indexes;
  auto deadline = Clock::now() + std::chrono::seconds(3);
  while (Clock::now() < deadline) {
    auto packet = host.ReceiveOnce(200);
    if (!packet.has_value()) {
      continue;
    }
    if (packet->msg_type() == lawnmower::MSG_S2C_GAME_START) {
      const auto game_start = ParsePayload<lawnmower::S2C_GameStart>(*packet);
      Require(game_start.success(), "state packet budget smoke: 开局失败");
      saw_game_start = true;
      continue;
    }
    if (packet->msg_type() != lawnmower::MSG_S2C_GAME_STATE_SYNC) {
      continue;
    }

    const auto sync = ParsePayload<lawnmower::S2C_GameStateSync>(*packet);
    if (!sync.is_full_snapshot()) {
      continue;
    }
    if (sync.chunk_count() <= 1) {
      continue;
    }

    packets_per_tick[sync.sync_time().tick()] += 1;
    if (!snapshot_id.has_value()) {
      snapshot_id = sync.snapshot_id();
      expected_chunk_count = sync.chunk_count();
    }
    if (sync.snapshot_id() != snapshot_id.value()) {
      continue;
    }
    chunk_indexes.insert(sync.chunk_index());
    if (chunk_indexes.size() >= expected_chunk_count) {
      break;
    }
  }

  Require(saw_game_start, "state packet budget smoke: 未收到 GAME_START");
  Require(snapshot_id.has_value(),
          "state packet budget smoke: 未观察到分片全量快照");
  Require(expected_chunk_count >= 2,
          "state packet budget smoke: chunk_count 未形成多片");
  Require(chunk_indexes.size() >= expected_chunk_count,
          "state packet budget smoke: 未收齐全量快照分片");

  uint32_t max_packets_same_tick = 0;
  for (const auto& [_, count] : packets_per_tick) {
    max_packets_same_tick = std::max(max_packets_same_tick, count);
  }
  Require(max_packets_same_tick <= 1,
          "state packet budget smoke: 同一 tick 全量分片包数超预算");

  host.Close();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  TcpClient reconnect_client("127.0.0.1", tcp_port, 5000);
  lawnmower::C2S_ReconnectRequest reconnect;
  reconnect.set_player_id(login_result.player_id());
  reconnect.set_room_id(room_id);
  reconnect.set_session_token(login_result.session_token());
  reconnect.set_last_input_seq(0);
  reconnect.set_last_server_tick(0);
  reconnect_client.Send(lawnmower::MSG_C2S_RECONNECT_REQUEST, reconnect);

  const auto reconnect_packet =
      reconnect_client.ReceiveUntil(lawnmower::MSG_S2C_RECONNECT_ACK, 3000);
  const auto reconnect_ack =
      ParsePayload<lawnmower::S2C_ReconnectAck>(reconnect_packet);
  Require(reconnect_ack.success(),
          "state packet budget smoke: 重连失败");

  packets_per_tick.clear();
  snapshot_id.reset();
  expected_chunk_count = 0;
  chunk_indexes.clear();
  deadline = Clock::now() + std::chrono::seconds(3);
  while (Clock::now() < deadline) {
    auto packet = reconnect_client.ReceiveOnce(200);
    if (!packet.has_value()) {
      continue;
    }
    if (packet->msg_type() != lawnmower::MSG_S2C_GAME_STATE_SYNC) {
      continue;
    }

    const auto sync = ParsePayload<lawnmower::S2C_GameStateSync>(*packet);
    if (!sync.is_full_snapshot()) {
      continue;
    }
    if (sync.chunk_count() <= 1) {
      continue;
    }

    packets_per_tick[sync.sync_time().tick()] += 1;
    if (!snapshot_id.has_value()) {
      snapshot_id = sync.snapshot_id();
      expected_chunk_count = sync.chunk_count();
    }
    if (sync.snapshot_id() != snapshot_id.value()) {
      continue;
    }
    chunk_indexes.insert(sync.chunk_index());
    if (chunk_indexes.size() >= expected_chunk_count) {
      break;
    }
  }

  Require(snapshot_id.has_value(),
          "state packet budget smoke: 重连未观察到分片全量快照");
  Require(expected_chunk_count >= 2,
          "state packet budget smoke: 重连 chunk_count 未形成多片");
  Require(chunk_indexes.size() >= expected_chunk_count,
          "state packet budget smoke: 重连未收齐全量快照分片");

  max_packets_same_tick = 0;
  for (const auto& [_, count] : packets_per_tick) {
    max_packets_same_tick = std::max(max_packets_same_tick, count);
  }
  Require(max_packets_same_tick <= 1,
          "state packet budget smoke: 重连单会话补包同一 tick 分片包数超预算");

  server.Stop();
  std::error_code ec;
  fs::remove_all(workspace, ec);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "用法: state_packet_budget_smoke_test <server_binary>\n";
    return 2;
  }

  try {
    RunSmoke(argv[1]);
    std::cout << "state_packet_budget_smoke_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "state_packet_budget_smoke_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
