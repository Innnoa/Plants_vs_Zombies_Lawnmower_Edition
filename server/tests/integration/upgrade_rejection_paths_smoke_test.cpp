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
#include <set>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
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
      (fs::temp_directory_path() / "upgrade-rejection-smoke-XXXXXX").string();
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
                    "  \"map_width\": 400,\n"
                    "  \"map_height\": 400,\n"
                    "  \"enemy_spawn_base_per_second\": 0,\n"
                    "  \"enemy_spawn_per_player_per_second\": 0,\n"
                    "  \"enemy_spawn_wave_growth_per_second\": 0,\n"
                    "  \"max_enemies_alive\": 1,\n"
                    "  \"max_enemy_spawn_per_tick\": 1,\n"
                    "  \"max_enemy_replan_per_tick\": 4,\n"
                    "  \"projectile_speed\": 5000,\n"
                    "  \"projectile_radius\": 128,\n"
                    "  \"projectile_ttl_seconds\": 1.0,\n"
                    "  \"projectile_attack_min_interval_seconds\": 0.05,\n"
                    "  \"projectile_attack_max_interval_seconds\": 0.05,\n"
                    "  \"log_level\": \"warn\"\n"
                    "}\n");

  WriteTextFile(cfg_dir / "player_roles.json",
                "{\n"
                "  \"default_role_id\": 1,\n"
                "  \"roles\": [\n"
                "    {\n"
                "      \"role_id\": 1,\n"
                "      \"name\": \"Upgrade Reject Host\",\n"
                "      \"max_health\": 100,\n"
                "      \"attack\": 1,\n"
                "      \"attack_speed\": 20,\n"
                "      \"move_speed\": 0,\n"
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
                "      \"name\": \"Upgrade Reject Zombie\",\n"
                "      \"max_health\": 1,\n"
                "      \"move_speed\": 0,\n"
                "      \"damage\": 0,\n"
                "      \"exp_reward\": 100,\n"
                "      \"drop_chance\": 0,\n"
                "      \"attack_enter_radius\": 32,\n"
                "      \"attack_exit_radius\": 40,\n"
                "      \"attack_interval_seconds\": 1.0\n"
                "    }\n"
                "  ]\n"
                "}\n");

  WriteTextFile(cfg_dir / "items_config.json",
                "{\n"
                "  \"default_type_id\": 1,\n"
                "  \"max_items_alive\": 4,\n"
                "  \"pick_radius\": 16,\n"
                "  \"items\": [\n"
                "    {\n"
                "      \"type_id\": 1,\n"
                "      \"name\": \"placeholder\",\n"
                "      \"effect\": \"none\",\n"
                "      \"value\": 0,\n"
                "      \"drop_weight\": 0\n"
                "    }\n"
                "  ]\n"
                "}\n");

  WriteTextFile(cfg_dir / "upgrade_config.json",
                "{\n"
                "  \"option_count\": 3,\n"
                "  \"refresh_limit\": 1,\n"
                "  \"upgrades\": [\n"
                "    {\"type\": \"attack\", \"level\": \"low\", \"value\": 1, "
                "\"weight\": 100},\n"
                "    {\"type\": \"move_speed\", \"level\": \"low\", \"value\": "
                "1, \"weight\": 100},\n"
                "    {\"type\": \"max_health\", \"level\": \"low\", \"value\": "
                "1, \"weight\": 100}\n"
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
    if (body_len == 0 || body_len > 1024 * 1024) {
      Fail("收到非法 TCP 包长: " + std::to_string(body_len));
    }

    std::string body(body_len, '\0');
    ReadExact(body.data(), body.size());

    lawnmower::Packet packet;
    if (!packet.ParseFromArray(body.data(), static_cast<int>(body.size()))) {
      Fail("TCP Packet 解析失败");
    }
    return packet;
  }

  lawnmower::Packet ReceiveUntil(lawnmower::MessageType expected_type,
                                 int timeout_ms,
                                 const std::string& stage = "") {
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
    const std::string suffix =
        stage.empty() ? std::string() : (" stage=" + stage);
    Fail("等待指定 TCP 消息超时 type=" +
         lawnmower::MessageType_Name(expected_type) + suffix);
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

template <typename T>
T ParsePayload(const lawnmower::Packet& packet) {
  T msg;
  if (!msg.ParseFromString(packet.payload())) {
    Fail("消息解析失败");
  }
  return msg;
}

void RequireChunkedFullSnapshot(TcpClient& client, const std::string& stage,
                                int timeout_ms) {
  std::optional<uint64_t> snapshot_id;
  uint32_t expected_chunk_count = 0;
  std::set<uint32_t> chunk_indexes;
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  while (Clock::now() < deadline) {
    const int left_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                              Clock::now())
            .count());
    if (left_ms <= 0) {
      break;
    }

    auto packet = client.ReceiveOnce(left_ms);
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

    Require(sync.chunk_count() > 1, stage + ": 收到未分片的全量快照");
    if (!snapshot_id.has_value()) {
      snapshot_id = sync.snapshot_id();
      expected_chunk_count = sync.chunk_count();
    }
    Require(sync.snapshot_id() == snapshot_id.value(),
            stage + ": snapshot_id 发生切换");
    Require(sync.chunk_count() == expected_chunk_count,
            stage + ": chunk_count 不一致");
    chunk_indexes.insert(sync.chunk_index());
    if (chunk_indexes.size() >= expected_chunk_count) {
      break;
    }
  }

  Require(snapshot_id.has_value(), stage + ": 未收到全量快照分片");
  Require(expected_chunk_count >= 2, stage + ": chunk_count 未形成多片");
  Require(chunk_indexes.size() >= expected_chunk_count,
          stage + ": 未收齐全量快照分片");
}

void ExpectNoPackets(TcpClient& client, int timeout_ms,
                     const std::string& stage) {
  auto packet = client.ReceiveOnce(timeout_ms);
  if (packet.has_value()) {
    Fail(stage + ": 收到意外消息 type=" +
         lawnmower::MessageType_Name(packet->msg_type()));
  }
}

uint32_t SendAttackBurst(TcpClient& client, uint32_t player_id,
                         const std::string& session_token,
                         uint32_t start_seq) {
  uint32_t seq = start_seq;
  for (uint32_t i = 0; i < 5; ++i, ++seq) {
    lawnmower::C2S_PlayerInput input;
    input.set_player_id(player_id);
    input.set_is_attacking(true);
    input.set_input_seq(seq);
    input.set_delta_ms(33);
    input.set_session_token(session_token);
    input.mutable_input_time()->set_tick(seq);
    input.mutable_move_direction()->set_x(0.0f);
    input.mutable_move_direction()->set_y(0.0f);
    client.Send(lawnmower::MSG_C2S_PLAYER_INPUT, input);
  }
  return seq;
}

lawnmower::S2C_UpgradeRequest WaitForInitialUpgradeRequest(
    TcpClient& client, uint32_t player_id, const std::string& session_token,
    int timeout_ms, const std::string& stage) {
  uint32_t next_seq = 1;
  auto next_attack_time = Clock::now();
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  while (Clock::now() < deadline) {
    if (Clock::now() >= next_attack_time) {
      next_seq =
          SendAttackBurst(client, player_id, session_token, next_seq);
      next_attack_time = Clock::now() + std::chrono::milliseconds(100);
    }

    const int left_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                              Clock::now())
            .count());
    if (left_ms <= 0) {
      break;
    }

    auto packet = client.ReceiveOnce(std::min(100, left_ms));
    if (!packet.has_value()) {
      continue;
    }
    if (packet->msg_type() == lawnmower::MSG_S2C_UPGRADE_REQUEST) {
      return ParsePayload<lawnmower::S2C_UpgradeRequest>(*packet);
    }
  }
  Fail("等待升级请求超时 stage=" + stage);
}

void SendUpgradeRequestAck(TcpClient& client, uint32_t room_id,
                           uint32_t player_id) {
  lawnmower::C2S_UpgradeRequestAck ack;
  ack.set_room_id(room_id);
  ack.set_player_id(player_id);
  client.Send(lawnmower::MSG_C2S_UPGRADE_REQUEST_ACK, ack);
}

void SendUpgradeOptionsAck(TcpClient& client, uint32_t room_id,
                           uint32_t player_id) {
  lawnmower::C2S_UpgradeOptionsAck ack;
  ack.set_room_id(room_id);
  ack.set_player_id(player_id);
  client.Send(lawnmower::MSG_C2S_UPGRADE_OPTIONS_ACK, ack);
}

void SendUpgradeRefreshRequest(TcpClient& client, uint32_t room_id,
                               uint32_t player_id) {
  lawnmower::C2S_UpgradeRefreshRequest request;
  request.set_room_id(room_id);
  request.set_player_id(player_id);
  client.Send(lawnmower::MSG_C2S_UPGRADE_REFRESH_REQUEST, request);
}

void SendUpgradeSelect(TcpClient& client, uint32_t room_id, uint32_t player_id,
                       uint32_t option_index) {
  lawnmower::C2S_UpgradeSelect select;
  select.set_room_id(room_id);
  select.set_player_id(player_id);
  select.set_option_index(option_index);
  client.Send(lawnmower::MSG_C2S_UPGRADE_SELECT, select);
}

void RequireSelectAck(TcpClient& client, uint32_t room_id, uint32_t player_id,
                      uint32_t option_index, const std::string& stage) {
  const auto packet =
      client.ReceiveUntil(lawnmower::MSG_S2C_UPGRADE_SELECT_ACK, 2000, stage);
  const auto ack = ParsePayload<lawnmower::S2C_UpgradeSelectAck>(packet);
  Require(ack.room_id() == room_id, stage + ": room_id 不匹配");
  Require(ack.player_id() == player_id, stage + ": player_id 不匹配");
  Require(ack.option_index() == option_index,
          stage + ": option_index 不匹配");
}

void RunSmoke(const std::string& server_path) {
  const uint16_t tcp_port = ReservePort(SOCK_STREAM);
  const uint16_t udp_port = ReservePort(SOCK_DGRAM);
  const fs::path workspace = CreateTempWorkspace();
  WriteTestConfigs(workspace, tcp_port, udp_port);

  ServerProcess server(server_path, workspace);
  TcpClient host("127.0.0.1", tcp_port, 5000);

  lawnmower::C2S_Login login;
  login.set_player_name("upgrade_reject_host");
  host.Send(lawnmower::MSG_C2S_LOGIN, login);
  const auto login_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_LOGIN_RESULT, 3000, "login");
  const auto login_result = ParsePayload<lawnmower::S2C_LoginResult>(login_packet);
  Require(login_result.success(), "upgrade reject smoke: 登录失败");
  Require(login_result.player_id() > 0,
          "upgrade reject smoke: player_id 非法");
  Require(!login_result.session_token().empty(),
          "upgrade reject smoke: session_token 为空");

  lawnmower::C2S_CreateRoom create_room;
  create_room.set_room_name("upgrade_reject_room");
  host.Send(lawnmower::MSG_C2S_CREATE_ROOM, create_room);
  const auto create_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_CREATE_ROOM_RESULT, 3000,
                        "create_room");
  const auto create_result =
      ParsePayload<lawnmower::S2C_CreateRoomResult>(create_packet);
  Require(create_result.success(), "upgrade reject smoke: 建房失败");
  const uint32_t room_id = create_result.room_id();

  lawnmower::C2S_StartGame start_game;
  host.Send(lawnmower::MSG_C2S_START_GAME, start_game);
  const auto game_start_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_GAME_START, 3000, "start_game");
  const auto game_start =
      ParsePayload<lawnmower::S2C_GameStart>(game_start_packet);
  Require(game_start.success(), "upgrade reject smoke: 开局失败");

  host.Drain(20, 16);
  const auto upgrade_request = WaitForInitialUpgradeRequest(
      host, login_result.player_id(), login_result.session_token(), 15000,
      "upgrade_request_1");
  Require(upgrade_request.reason() == lawnmower::UPGRADE_REASON_LEVEL_UP,
          "upgrade reject smoke: 首次升级原因不是 level_up");

  SendUpgradeOptionsAck(host, room_id, login_result.player_id());
  ExpectNoPackets(host, 150, "wrong_stage_options_ack");

  SendUpgradeSelect(host, room_id, login_result.player_id(), 0);
  ExpectNoPackets(host, 150, "wrong_stage_select");

  SendUpgradeRequestAck(host, room_id, login_result.player_id());
  const auto options_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_UPGRADE_OPTIONS, 2000,
                        "upgrade_options");
  const auto options =
      ParsePayload<lawnmower::S2C_UpgradeOptions>(options_packet);
  Require(options.options_size() >= 1,
          "upgrade reject smoke: 升级选项为空");
  Require(options.refresh_remaining() == 1,
          "upgrade reject smoke: refresh_remaining 初值不是 1");

  SendUpgradeRequestAck(host, room_id, login_result.player_id());
  ExpectNoPackets(host, 150, "wrong_stage_request_ack");

  SendUpgradeRefreshRequest(host, room_id, login_result.player_id());
  const auto refresh_request =
      host.ReceiveUntil(lawnmower::MSG_S2C_UPGRADE_REQUEST, 2000,
                        "refresh_upgrade_request");
  const auto refresh_msg =
      ParsePayload<lawnmower::S2C_UpgradeRequest>(refresh_request);
  Require(refresh_msg.reason() == lawnmower::UPGRADE_REASON_REFRESH,
          "upgrade reject smoke: refresh 后原因不是 refresh");

  SendUpgradeRefreshRequest(host, room_id, login_result.player_id());
  ExpectNoPackets(host, 150, "refresh_exhausted");

  SendUpgradeRequestAck(host, room_id, login_result.player_id());
  const auto refresh_options_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_UPGRADE_OPTIONS, 2000,
                        "refresh_options");
  const auto refresh_options =
      ParsePayload<lawnmower::S2C_UpgradeOptions>(refresh_options_packet);
  Require(refresh_options.options_size() >= 1,
          "upgrade reject smoke: refresh 后选项为空");
  Require(refresh_options.refresh_remaining() == 0,
          "upgrade reject smoke: refresh 后 refresh_remaining 不是 0");

  SendUpgradeOptionsAck(host, room_id, login_result.player_id());
  SendUpgradeSelect(host, room_id, login_result.player_id(), 99);
  ExpectNoPackets(host, 150, "invalid_option_index");

  SendUpgradeSelect(host, room_id, login_result.player_id(), 0);
  RequireSelectAck(host, room_id, login_result.player_id(), 0,
                   "valid_upgrade_select_ack");
  RequireChunkedFullSnapshot(host,
                             "upgrade reject smoke: 负向路径后最终恢复全量快照",
                             300);

  server.Stop();
  std::error_code ec;
  fs::remove_all(workspace, ec);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "用法: upgrade_rejection_paths_smoke_test <server_binary>\n";
    return 2;
  }

  try {
    RunSmoke(argv[1]);
    std::cout << "upgrade_rejection_paths_smoke_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "upgrade_rejection_paths_smoke_test: FAIL: " << ex.what()
              << "\n";
    return 1;
  }
}
