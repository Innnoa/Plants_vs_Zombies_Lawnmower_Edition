#include <arpa/inet.h>
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
    Fail("创建端口探测 socket 失败: " + std::string(std::strerror(errno)));
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
      (fs::temp_directory_path() / "late-input-reconcile-XXXXXX").string();
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
                    "  \"tick_rate\": 20,\n"
                    "  \"state_sync_rate\": 20,\n"
                    "  \"map_width\": 400,\n"
                    "  \"map_height\": 400,\n"
                    "  \"enemy_spawn_base_per_second\": 0,\n"
                    "  \"enemy_spawn_per_player_per_second\": 0,\n"
                    "  \"enemy_spawn_wave_growth_per_second\": 0,\n"
                    "  \"max_enemies_alive\": 0,\n"
                    "  \"prediction_history_seconds\": 2.0,\n"
                    "  \"log_level\": \"warn\"\n"
                    "}\n");

  WriteTextFile(cfg_dir / "player_roles.json",
                "{\n"
                "  \"default_role_id\": 1,\n"
                "  \"roles\": [\n"
                "    {\n"
                "      \"role_id\": 1,\n"
                "      \"name\": \"Rollback Host\",\n"
                "      \"max_health\": 100,\n"
                "      \"attack\": 1,\n"
                "      \"attack_speed\": 20,\n"
                "      \"move_speed\": 200,\n"
                "      \"critical_hit_rate\": 0\n"
                "    }\n"
                "  ]\n"
                "}\n");

  WriteTextFile(cfg_dir / "enemy_types.json",
                "{\n"
                "  \"default_type_id\": 1,\n"
                "  \"enemies\": []\n"
                "}\n");

  WriteTextFile(cfg_dir / "items_config.json",
                "{\n"
                "  \"default_type_id\": 1,\n"
                "  \"max_items_alive\": 0,\n"
                "  \"pick_radius\": 24,\n"
                "  \"items\": [\n"
                "    {\n"
                "      \"type_id\": 1,\n"
                "      \"name\": \"unused\",\n"
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
        Fail("poll 失败: " + std::string(std::strerror(errno)));
      }
      if ((pfd.revents & POLLIN) == 0) {
        return false;
      }
      const ssize_t n = ::recv(fd_, data + got, size - got, 0);
      if (n == 0) {
        return false;
      }
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        Fail("TCP 接收失败: " + std::string(std::strerror(errno)));
      }
      got += static_cast<std::size_t>(n);
    }
    return true;
  }

  int fd_ = -1;
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

struct PlayerObservation {
  std::optional<uint32_t> confirmed_seq;
  std::optional<float> x;
  std::optional<float> y;
  std::optional<float> rotation;
  uint32_t packet_tick = 0;
};

std::optional<PlayerObservation> ExtractPlayerObservation(
    const lawnmower::Packet& packet, uint32_t player_id) {
  if (packet.msg_type() == lawnmower::MSG_S2C_GAME_STATE_SYNC) {
    const auto sync = ParsePayload<lawnmower::S2C_GameStateSync>(packet);
    for (const auto& player : sync.players()) {
      if (player.player_id() != player_id) {
        continue;
      }
      PlayerObservation obs;
      obs.confirmed_seq = static_cast<uint32_t>(player.last_processed_input_seq());
      if (player.has_position()) {
        obs.x = player.position().x();
        obs.y = player.position().y();
      }
      obs.rotation = player.rotation();
      if (sync.has_sync_time()) {
        obs.packet_tick = sync.sync_time().tick();
      }
      return obs;
    }
    return std::nullopt;
  }

  if (packet.msg_type() == lawnmower::MSG_S2C_GAME_STATE_DELTA_SYNC) {
    const auto delta = ParsePayload<lawnmower::S2C_GameStateDeltaSync>(packet);
    for (const auto& player : delta.players()) {
      if (player.player_id() != player_id) {
        continue;
      }
      PlayerObservation obs;
      if (player.has_last_processed_input_seq()) {
        obs.confirmed_seq =
            static_cast<uint32_t>(player.last_processed_input_seq());
      }
      if (player.has_position()) {
        obs.x = player.position().x();
        obs.y = player.position().y();
      }
      if (player.has_rotation()) {
        obs.rotation = player.rotation();
      }
      if (delta.has_sync_time()) {
        obs.packet_tick = delta.sync_time().tick();
      }
      return obs;
    }
  }

  return std::nullopt;
}

void SendMoveInput(const TcpClient& client, uint32_t seq, uint32_t input_tick,
                   uint32_t delta_ms, float dx, float dy) {
  lawnmower::C2S_PlayerInput input;
  input.mutable_move_direction()->set_x(dx);
  input.mutable_move_direction()->set_y(dy);
  input.set_is_attacking(false);
  input.mutable_input_time()->set_tick(input_tick);
  input.set_input_seq(seq);
  input.set_delta_ms(delta_ms);
  client.Send(lawnmower::MSG_C2S_PLAYER_INPUT, input);
}

void RunLateInputReconciliationSmoke(const std::string& server_binary) {
  const uint16_t tcp_port = ReservePort(SOCK_STREAM);
  const uint16_t udp_port = ReservePort(SOCK_DGRAM);
  const fs::path workspace = CreateTempWorkspace();
  WriteTestConfigs(workspace, tcp_port, udp_port);

  ServerProcess server(server_binary, workspace);
  std::this_thread::sleep_for(std::chrono::milliseconds(250));

  TcpClient host("127.0.0.1", tcp_port, 5000);

  lawnmower::C2S_Login login;
  login.set_player_name("rollback_host");
  host.Send(lawnmower::MSG_C2S_LOGIN, login);
  const auto login_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_LOGIN_RESULT, 3000);
  const auto login_result =
      ParsePayload<lawnmower::S2C_LoginResult>(login_packet);
  Require(login_result.success(), "late input reconcile smoke: 登录失败");
  const uint32_t player_id = login_result.player_id();
  Require(player_id > 0, "late input reconcile smoke: player_id 非法");

  lawnmower::C2S_CreateRoom create_room;
  create_room.set_room_name("rollback_room");
  create_room.set_max_players(1);
  host.Send(lawnmower::MSG_C2S_CREATE_ROOM, create_room);
  const auto create_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_CREATE_ROOM_RESULT, 3000);
  const auto create_result =
      ParsePayload<lawnmower::S2C_CreateRoomResult>(create_packet);
  Require(create_result.success(), "late input reconcile smoke: 建房失败");

  lawnmower::C2S_StartGame start_game;
  host.Send(lawnmower::MSG_C2S_START_GAME, start_game);
  const auto game_start_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_GAME_START, 3000);
  const auto game_start =
      ParsePayload<lawnmower::S2C_GameStart>(game_start_packet);
  Require(game_start.success(), "late input reconcile smoke: 开局失败");

  const auto full_sync_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_GAME_STATE_SYNC, 3000);
  const auto initial_sync =
      ParsePayload<lawnmower::S2C_GameStateSync>(full_sync_packet);
  Require(initial_sync.players_size() == 1,
          "late input reconcile smoke: 初始 full sync 玩家数量不正确");
  const auto& initial_player = initial_sync.players(0);
  Require(initial_player.player_id() == player_id,
          "late input reconcile smoke: 初始玩家ID不匹配");
  const float initial_x = initial_player.position().x();
  const float initial_y = initial_player.position().y();
  const uint32_t initial_confirmed_seq =
      static_cast<uint32_t>(initial_player.last_processed_input_seq());
  Require(initial_confirmed_seq == 0,
          "late input reconcile smoke: 初始确认序号应为 0");

  host.Drain(8);

  SendMoveInput(host, 2, 2, 100, 1.0f, 0.0f);
  SendMoveInput(host, 3, 3, 100, 1.0f, 0.0f);

  uint32_t max_confirmed_before_fill = 0;
  float before_fill_x = initial_x;
  float before_fill_y = initial_y;
  bool saw_gap_movement = false;
  auto before_deadline = Clock::now() + std::chrono::milliseconds(1200);
  while (Clock::now() < before_deadline) {
    auto packet = host.ReceiveOnce(200);
    if (!packet.has_value()) {
      continue;
    }
    const auto obs = ExtractPlayerObservation(*packet, player_id);
    if (!obs.has_value()) {
      continue;
    }
    if (obs->confirmed_seq.has_value()) {
      max_confirmed_before_fill =
          std::max(max_confirmed_before_fill, *obs->confirmed_seq);
    }
    if (obs->x.has_value()) {
      before_fill_x = std::max(before_fill_x, *obs->x);
      if (*obs->x > initial_x + 5.0f) {
        saw_gap_movement = true;
      }
    }
    if (obs->y.has_value()) {
      before_fill_y = std::max(before_fill_y, *obs->y);
    }
  }

  Require(saw_gap_movement,
          "late input reconcile smoke: 未观察到缺口期间的玩家移动");
  Require(max_confirmed_before_fill == 0,
          "late input reconcile smoke: 缺口未补齐前确认序号不应推进");

  SendMoveInput(host, 1, 1, 100, 0.0f, 1.0f);

  uint32_t confirmed_after_fill = max_confirmed_before_fill;
  float after_fill_x = before_fill_x;
  float after_fill_y = before_fill_y;
  bool saw_reconciled_position = false;
  bool saw_replayed_rotation = false;
  auto after_deadline = Clock::now() + std::chrono::milliseconds(2000);
  while (Clock::now() < after_deadline) {
    auto packet = host.ReceiveOnce(200);
    if (!packet.has_value()) {
      continue;
    }
    const auto obs = ExtractPlayerObservation(*packet, player_id);
    if (!obs.has_value()) {
      continue;
    }
    if (obs->confirmed_seq.has_value()) {
      confirmed_after_fill = std::max(confirmed_after_fill, *obs->confirmed_seq);
    }
    if (obs->x.has_value()) {
      after_fill_x = std::max(after_fill_x, *obs->x);
    }
    if (obs->y.has_value()) {
      after_fill_y = std::max(after_fill_y, *obs->y);
      if (*obs->y > before_fill_y + 5.0f) {
        saw_reconciled_position = true;
      }
    }
    if (obs->rotation.has_value() && std::abs(*obs->rotation) < 15.0f) {
      saw_replayed_rotation = true;
    }
    if (confirmed_after_fill >= 3 && saw_reconciled_position &&
        saw_replayed_rotation) {
      break;
    }
  }

  Require(confirmed_after_fill >= 3,
          "late input reconcile smoke: 补洞后确认序号未推进到 3");
  Require(saw_reconciled_position,
          "late input reconcile smoke: 补洞后未观察到权威位置纠偏");
  Require(saw_replayed_rotation,
          "late input reconcile smoke: 补洞后最终朝向未保持逻辑末尾输入方向");

  server.Stop();
  std::error_code ec;
  fs::remove_all(workspace, ec);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "用法: late_input_reconciliation_smoke_test <server_binary>\n";
    return 2;
  }

  try {
    RunLateInputReconciliationSmoke(argv[1]);
    std::cout << "late_input_reconciliation_smoke_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "late_input_reconciliation_smoke_test: FAIL: " << ex.what()
              << "\n";
    return 1;
  }
}
