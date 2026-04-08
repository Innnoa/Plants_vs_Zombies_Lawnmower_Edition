#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cctype>
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
#include <string_view>
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

fs::path CreateTempWorkspace(std::string_view prefix) {
  std::string pattern =
      (fs::temp_directory_path() /
       (std::string(prefix) + "-XXXXXX"))
          .string();
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

void WriteCommonItemsConfig(const fs::path& cfg_dir) {
  WriteTextFile(cfg_dir / "items_config.json",
                "{\n"
                "  \"default_type_id\": 1,\n"
                "  \"max_items_alive\": 8,\n"
                "  \"pick_radius\": 32,\n"
                "  \"items\": [\n"
                "    {\n"
                "      \"type_id\": 1,\n"
                "      \"name\": \"测试道具\",\n"
                "      \"effect\": \"heal\",\n"
                "      \"value\": 1,\n"
                "      \"drop_weight\": 100\n"
                "    }\n"
                "  ]\n"
                "}\n");
}

void WriteHeartbeatUpgradeConfigs(const fs::path& workspace, uint16_t tcp_port,
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
                    "  \"map_width\": 240,\n"
                    "  \"map_height\": 240,\n"
                    "  \"enemy_spawn_base_per_second\": 0,\n"
                    "  \"enemy_spawn_per_player_per_second\": 0,\n"
                    "  \"enemy_spawn_wave_growth_per_second\": 0,\n"
                    "  \"max_enemies_alive\": 1,\n"
                    "  \"projectile_attack_min_interval_seconds\": 0.05,\n"
                    "  \"projectile_attack_max_interval_seconds\": 0.05,\n"
                    "  \"reconnect_grace_seconds\": 1.0,\n"
                    "  \"log_level\": \"warn\"\n"
                    "}\n");

  WriteTextFile(cfg_dir / "player_roles.json",
                "{\n"
                "  \"default_role_id\": 1,\n"
                "  \"roles\": [\n"
                "    {\n"
                "      \"role_id\": 1,\n"
                "      \"name\": \"升级测试角色\",\n"
                "      \"max_health\": 100,\n"
                "      \"attack\": 100,\n"
                "      \"attack_speed\": 20,\n"
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
                "      \"name\": \"升级测试僵尸\",\n"
                "      \"max_health\": 1,\n"
                "      \"move_speed\": 0,\n"
                "      \"damage\": 0,\n"
                "      \"exp_reward\": 200,\n"
                "      \"drop_chance\": 0,\n"
                "      \"attack_enter_radius\": 40,\n"
                "      \"attack_exit_radius\": 40,\n"
                "      \"attack_interval_seconds\": 1.0\n"
                "    }\n"
                "  ]\n"
                "}\n");

  WriteCommonItemsConfig(cfg_dir);

  WriteTextFile(cfg_dir / "upgrade_config.json",
                "{\n"
                "  \"option_count\": 3,\n"
                "  \"refresh_limit\": 1,\n"
                "  \"upgrades\": [\n"
                "    {\n"
                "      \"type\": \"attack\",\n"
                "      \"level\": \"low\",\n"
                "      \"value\": 50,\n"
                "      \"weight\": 1\n"
                "    }\n"
                "  ]\n"
                "}\n");
}

void WriteGameOverConfigs(const fs::path& workspace, uint16_t tcp_port,
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
                    "  \"map_width\": 240,\n"
                    "  \"map_height\": 240,\n"
                    "  \"enemy_spawn_base_per_second\": 0,\n"
                    "  \"enemy_spawn_per_player_per_second\": 0,\n"
                    "  \"enemy_spawn_wave_growth_per_second\": 0,\n"
                    "  \"max_enemies_alive\": 1,\n"
                    "  \"projectile_attack_min_interval_seconds\": 0.05,\n"
                    "  \"projectile_attack_max_interval_seconds\": 0.05,\n"
                    "  \"perf_sample_stride\": 1,\n"
                    "  \"reconnect_grace_seconds\": 1.0,\n"
                    "  \"log_level\": \"warn\"\n"
                    "}\n");

  WriteTextFile(cfg_dir / "player_roles.json",
                "{\n"
                "  \"default_role_id\": 1,\n"
                "  \"roles\": [\n"
                "    {\n"
                "      \"role_id\": 1,\n"
                "      \"name\": \"结算测试角色\",\n"
                "      \"max_health\": 1,\n"
                "      \"attack\": 1,\n"
                "      \"attack_speed\": 1,\n"
                "      \"move_speed\": 100,\n"
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
                "      \"name\": \"结算测试僵尸\",\n"
                "      \"max_health\": 1000,\n"
                "      \"move_speed\": 0,\n"
                "      \"damage\": 5,\n"
                "      \"exp_reward\": 0,\n"
                "      \"drop_chance\": 0,\n"
                "      \"attack_enter_radius\": 1000,\n"
                "      \"attack_exit_radius\": 1000,\n"
                "      \"attack_interval_seconds\": 0.05\n"
                "    }\n"
                "  ]\n"
                "}\n");

  WriteCommonItemsConfig(cfg_dir);

  WriteTextFile(cfg_dir / "upgrade_config.json",
                "{\n"
                "  \"option_count\": 3,\n"
                "  \"refresh_limit\": 0,\n"
                "  \"upgrades\": [\n"
                "    {\n"
                "      \"type\": \"attack\",\n"
                "      \"level\": \"low\",\n"
                "      \"value\": 1,\n"
                "      \"weight\": 1\n"
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

class ScopedRemoveAll final {
 public:
  explicit ScopedRemoveAll(fs::path path) : path_(std::move(path)) {}

  ~ScopedRemoveAll() {
    if (path_.empty()) {
      return;
    }
    std::error_code ec;
    fs::remove_all(path_, ec);
  }

 private:
  fs::path path_;
};

class TcpClient final {
 public:
  TcpClient(std::string host, uint16_t port, int connect_timeout_ms) {
    const auto deadline =
        Clock::now() + std::chrono::milliseconds(connect_timeout_ms);
    while (true) {
      const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
      if (fd < 0) {
        Fail("创建客户端 socket 失败");
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
      Fail("收到非法包长: " + std::to_string(body_len));
    }

    std::string body(body_len, '\0');
    if (!ReadExact(body.data(), body.size(), timeout_ms)) {
      return std::nullopt;
    }

    lawnmower::Packet packet;
    if (!packet.ParseFromString(body)) {
      Fail("解析 Packet 失败");
    }
    return packet;
  }

  lawnmower::Packet ReceiveUntil(lawnmower::MessageType type,
                                 int timeout_ms) const {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    while (Clock::now() < deadline) {
      const auto left_ms = static_cast<int>(
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
    Fail("等待消息超时: " + lawnmower::MessageType_Name(type));
  }

 private:
  void WriteExact(const char* data, std::size_t size) const {
    std::size_t sent = 0;
    while (sent < size) {
      const ssize_t n = ::send(fd_, data + sent, size - sent, MSG_NOSIGNAL);
      if (n <= 0) {
        const int err = errno;
        Fail("发送失败: " + std::string(std::strerror(err)));
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
      const ssize_t n = ::recv(fd_, data + got, size - got, 0);
      if (n == 0) {
        return false;
      }
      if (n < 0) {
        if (errno == EINTR) {
          continue;
        }
        Fail("recv 失败: " + std::string(std::strerror(errno)));
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

bool HasRoom(const lawnmower::S2C_RoomList& list, uint32_t room_id,
             bool* out_is_playing) {
  for (const auto& room : list.rooms()) {
    if (room.room_id() == room_id) {
      if (out_is_playing != nullptr) {
        *out_is_playing = room.is_playing();
      }
      return true;
    }
  }
  return false;
}

const lawnmower::PlayerState& FindPlayerState(const lawnmower::S2C_GameStateSync& sync,
                                              uint32_t player_id) {
  for (const auto& player : sync.players()) {
    if (player.player_id() == player_id) {
      return player;
    }
  }
  Fail("全量同步中未找到玩家状态");
}

fs::path FindRepoRootFromServerBinary(const std::string& server_binary) {
  fs::path cur = fs::absolute(server_binary);
  if (fs::is_regular_file(cur)) {
    cur = cur.parent_path();
  }
  for (int depth = 0; depth < 8; ++depth) {
    if (fs::exists(cur / "proto" / "message.proto") &&
        fs::exists(cur / "server" / "CMakeLists.txt")) {
      return cur;
    }
    if (!cur.has_parent_path()) {
      break;
    }
    cur = cur.parent_path();
  }
  Fail("无法从 server binary 推断仓库根目录");
}

std::set<fs::path> CollectMetricsFiles(const fs::path& repo_root) {
  std::set<fs::path> files;
  const fs::path metrics_root = repo_root / "server_metrics";
  if (!fs::exists(metrics_root)) {
    return files;
  }
  for (const auto& entry : fs::recursive_directory_iterator(metrics_root)) {
    if (entry.is_regular_file()) {
      files.insert(entry.path());
    }
  }
  return files;
}

std::vector<fs::path> DiffMetricsFiles(const std::set<fs::path>& before,
                                       const std::set<fs::path>& after) {
  std::vector<fs::path> diff;
  for (const auto& path : after) {
    if (!before.contains(path)) {
      diff.push_back(path);
    }
  }
  return diff;
}

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    Fail("读取文件失败: " + path.string());
  }
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

uint64_t ExtractJsonUIntField(const std::string& content, std::string_view key) {
  const std::string quoted_key = "\"" + std::string(key) + "\"";
  std::size_t pos = content.find(quoted_key);
  if (pos == std::string::npos) {
    Fail("JSON 中未找到字段: " + std::string(key));
  }
  pos = content.find(':', pos);
  if (pos == std::string::npos) {
    Fail("JSON 字段缺少冒号: " + std::string(key));
  }
  ++pos;
  while (pos < content.size() &&
         std::isspace(static_cast<unsigned char>(content[pos])) != 0) {
    ++pos;
  }
  std::size_t end = pos;
  while (end < content.size() &&
         std::isdigit(static_cast<unsigned char>(content[end])) != 0) {
    ++end;
  }
  if (end == pos) {
    Fail("JSON 字段不是无符号整数: " + std::string(key));
  }
  return std::stoull(content.substr(pos, end - pos));
}

void CleanupMetricsFiles(const std::vector<fs::path>& files,
                         const fs::path& repo_root) {
  std::error_code ec;
  for (const auto& path : files) {
    fs::remove(path, ec);
    ec.clear();
    fs::path parent = path.parent_path();
    while (!parent.empty() && parent != repo_root && parent != parent.root_path()) {
      if (!fs::is_directory(parent)) {
        break;
      }
      if (!fs::is_empty(parent, ec)) {
        ec.clear();
        break;
      }
      fs::remove(parent, ec);
      if (ec) {
        ec.clear();
        break;
      }
      parent = parent.parent_path();
    }
  }
}

class MetricsCleanupGuard final {
 public:
  explicit MetricsCleanupGuard(fs::path repo_root)
      : repo_root_(std::move(repo_root)) {}

  ~MetricsCleanupGuard() { CleanupMetricsFiles(files_, repo_root_); }

  std::vector<fs::path>& Files() { return files_; }

 private:
  std::vector<fs::path> files_;
  fs::path repo_root_;
};

void SendAttackInput(UdpClient& client, uint32_t player_id,
                     const std::string& session_token, uint32_t input_seq) {
  lawnmower::C2S_PlayerInput input;
  input.set_player_id(player_id);
  input.mutable_move_direction()->set_x(0.0f);
  input.mutable_move_direction()->set_y(0.0f);
  input.set_is_attacking(true);
  input.set_input_seq(input_seq);
  input.set_delta_ms(33);
  input.set_session_token(session_token);
  client.Send(lawnmower::MSG_C2S_PLAYER_INPUT, input);
}

void RunHeartbeatAndUpgradeSmoke(const std::string& server_binary) {
  const uint16_t tcp_port = ReservePort(SOCK_STREAM);
  const uint16_t udp_port = ReservePort(SOCK_DGRAM);
  const fs::path workspace = CreateTempWorkspace("server-net-fix-upgrade");
  ScopedRemoveAll workspace_cleanup(workspace);
  WriteHeartbeatUpgradeConfigs(workspace, tcp_port, udp_port);

  ServerProcess server(server_binary, workspace);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  TcpClient host("127.0.0.1", tcp_port, 5000);
  lawnmower::C2S_Login login;
  login.set_player_name("net_fix_host");
  host.Send(lawnmower::MSG_C2S_LOGIN, login);
  const auto login_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_LOGIN_RESULT, 3000);
  const auto login_result =
      ParsePayload<lawnmower::S2C_LoginResult>(login_packet);
  Require(login_result.success(), "升级 smoke: 登录失败");

  lawnmower::C2S_Heartbeat heartbeat;
  heartbeat.set_timestamp(123456);
  host.Send(lawnmower::MSG_C2S_HEARTBEAT, heartbeat);
  const auto heartbeat_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_HEARTBEAT, 3000);
  const auto heartbeat_reply =
      ParsePayload<lawnmower::S2C_Heartbeat>(heartbeat_packet);
  Require(heartbeat_reply.timestamp() > 0,
          "升级 smoke: heartbeat 时间戳无效");
  Require(heartbeat_reply.online_players() == 1,
          "升级 smoke: heartbeat 在线连接数应为 1");

  lawnmower::C2S_CreateRoom create_room;
  create_room.set_room_name("net_fix_upgrade_room");
  create_room.set_max_players(1);
  host.Send(lawnmower::MSG_C2S_CREATE_ROOM, create_room);
  const auto create_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_CREATE_ROOM_RESULT, 3000);
  const auto create_result =
      ParsePayload<lawnmower::S2C_CreateRoomResult>(create_packet);
  Require(create_result.success(), "升级 smoke: 建房失败");
  const uint32_t room_id = create_result.room_id();
  const uint32_t player_id = login_result.player_id();

  lawnmower::C2S_StartGame start_game;
  host.Send(lawnmower::MSG_C2S_START_GAME, start_game);
  const auto game_start_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_GAME_START, 3000);
  const auto game_start =
      ParsePayload<lawnmower::S2C_GameStart>(game_start_packet);
  Require(game_start.success(), "升级 smoke: 开局失败");
  Require(game_start.room_id() == room_id, "升级 smoke: room_id 不匹配");

  const auto initial_sync_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_GAME_STATE_SYNC, 3000);
  const auto initial_sync =
      ParsePayload<lawnmower::S2C_GameStateSync>(initial_sync_packet);
  Require(initial_sync.room_id() == room_id,
          "升级 smoke: 初始全量同步 room_id 不匹配");
  Require(initial_sync.enemies_size() >= 1,
          "升级 smoke: 初始全量同步中应至少有 1 个敌人");

  UdpClient udp("127.0.0.1", udp_port);
  std::optional<lawnmower::S2C_UpgradeRequest> upgrade_request;
  auto deadline = Clock::now() + std::chrono::seconds(8);
  uint32_t input_seq = 1;
  while (Clock::now() < deadline && !upgrade_request.has_value()) {
    SendAttackInput(udp, player_id, login_result.session_token(), input_seq++);
    while (Clock::now() < deadline) {
      auto packet = host.ReceiveOnce(30);
      if (!packet.has_value()) {
        break;
      }
      if (packet->msg_type() != lawnmower::MSG_S2C_UPGRADE_REQUEST) {
        continue;
      }
      upgrade_request =
          ParsePayload<lawnmower::S2C_UpgradeRequest>(*packet);
      break;
    }
  }
  Require(upgrade_request.has_value(), "升级 smoke: 未收到升级请求");
  Require(upgrade_request->room_id() == room_id,
          "升级 smoke: 升级请求 room_id 不匹配");
  Require(upgrade_request->player_id() == player_id,
          "升级 smoke: 升级请求 player_id 不匹配");
  Require(upgrade_request->reason() == lawnmower::UPGRADE_REASON_LEVEL_UP,
          "升级 smoke: 首次升级原因应为 LEVEL_UP");

  lawnmower::C2S_UpgradeRequestAck request_ack;
  request_ack.set_room_id(room_id);
  request_ack.set_player_id(player_id);
  host.Send(lawnmower::MSG_C2S_UPGRADE_REQUEST_ACK, request_ack);

  const auto options_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_UPGRADE_OPTIONS, 3000);
  const auto options =
      ParsePayload<lawnmower::S2C_UpgradeOptions>(options_packet);
  Require(options.room_id() == room_id,
          "升级 smoke: 升级选项 room_id 不匹配");
  Require(options.player_id() == player_id,
          "升级 smoke: 升级选项 player_id 不匹配");
  Require(options.options_size() == 3,
          "升级 smoke: 升级选项数量应固定为 3");
  Require(options.refresh_remaining() == 1,
          "升级 smoke: 首轮剩余刷新次数应为 1");
  Require(options.options(0).effects_size() == 1,
          "升级 smoke: 升级效果数量异常");
  Require(options.options(0).effects(0).type() ==
              lawnmower::UPGRADE_TYPE_ATTACK &&
              options.options(0).effects(0).value() == 50,
          "升级 smoke: 升级效果不符合测试配置");

  lawnmower::C2S_UpgradeRefreshRequest refresh;
  refresh.set_room_id(room_id);
  refresh.set_player_id(player_id);
  host.Send(lawnmower::MSG_C2S_UPGRADE_REFRESH_REQUEST, refresh);

  const auto refresh_request_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_UPGRADE_REQUEST, 3000);
  const auto refresh_request =
      ParsePayload<lawnmower::S2C_UpgradeRequest>(refresh_request_packet);
  Require(refresh_request.reason() == lawnmower::UPGRADE_REASON_REFRESH,
          "升级 smoke: 刷新后的升级原因应为 REFRESH");

  host.Send(lawnmower::MSG_C2S_UPGRADE_REQUEST_ACK, request_ack);
  const auto refreshed_options_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_UPGRADE_OPTIONS, 3000);
  const auto refreshed_options =
      ParsePayload<lawnmower::S2C_UpgradeOptions>(refreshed_options_packet);
  Require(refreshed_options.refresh_remaining() == 0,
          "升级 smoke: 刷新后剩余次数应归零");

  lawnmower::C2S_UpgradeOptionsAck options_ack;
  options_ack.set_room_id(room_id);
  options_ack.set_player_id(player_id);
  host.Send(lawnmower::MSG_C2S_UPGRADE_OPTIONS_ACK, options_ack);

  lawnmower::C2S_UpgradeSelect select;
  select.set_room_id(room_id);
  select.set_player_id(player_id);
  select.set_option_index(0);
  host.Send(lawnmower::MSG_C2S_UPGRADE_SELECT, select);

  const auto select_ack_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_UPGRADE_SELECT_ACK, 3000);
  const auto select_ack =
      ParsePayload<lawnmower::S2C_UpgradeSelectAck>(select_ack_packet);
  Require(select_ack.room_id() == room_id,
          "升级 smoke: 选择确认 room_id 不匹配");
  Require(select_ack.player_id() == player_id,
          "升级 smoke: 选择确认 player_id 不匹配");
  Require(select_ack.option_index() == 0,
          "升级 smoke: 选择确认 option_index 不匹配");

  const auto full_sync_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_GAME_STATE_SYNC, 3000);
  const auto full_sync =
      ParsePayload<lawnmower::S2C_GameStateSync>(full_sync_packet);
  const auto& player_state = FindPlayerState(full_sync, player_id);
  Require(player_state.level() == 2,
          "升级 smoke: 升级后等级应为 2");
  Require(player_state.attack() == 150,
          "升级 smoke: 升级后攻击力应增加到 150");

  server.Stop();
}

void RunGameOverAndMetricsSmoke(const std::string& server_binary) {
  const fs::path repo_root = FindRepoRootFromServerBinary(server_binary);
  const auto metrics_before = CollectMetricsFiles(repo_root);
  MetricsCleanupGuard metrics_cleanup(repo_root);

  const uint16_t tcp_port = ReservePort(SOCK_STREAM);
  const uint16_t udp_port = ReservePort(SOCK_DGRAM);
  const fs::path workspace = CreateTempWorkspace("server-net-fix-gameover");
  ScopedRemoveAll workspace_cleanup(workspace);
  WriteGameOverConfigs(workspace, tcp_port, udp_port);

  {
    ServerProcess server(server_binary, workspace);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    TcpClient host("127.0.0.1", tcp_port, 5000);
    lawnmower::C2S_Login login;
    login.set_player_name("net_fix_gameover");
    host.Send(lawnmower::MSG_C2S_LOGIN, login);
    const auto login_packet =
        host.ReceiveUntil(lawnmower::MSG_S2C_LOGIN_RESULT, 3000);
    const auto login_result =
        ParsePayload<lawnmower::S2C_LoginResult>(login_packet);
    Require(login_result.success(), "结算 smoke: 登录失败");
    const uint32_t player_id = login_result.player_id();

    lawnmower::C2S_CreateRoom create_room;
    create_room.set_room_name("net_fix_gameover_room");
    create_room.set_max_players(1);
    host.Send(lawnmower::MSG_C2S_CREATE_ROOM, create_room);
    const auto create_packet =
        host.ReceiveUntil(lawnmower::MSG_S2C_CREATE_ROOM_RESULT, 3000);
    const auto create_result =
        ParsePayload<lawnmower::S2C_CreateRoomResult>(create_packet);
    Require(create_result.success(), "结算 smoke: 建房失败");
    const uint32_t room_id = create_result.room_id();

    lawnmower::C2S_StartGame start_game;
    host.Send(lawnmower::MSG_C2S_START_GAME, start_game);
    const auto game_start_packet =
        host.ReceiveUntil(lawnmower::MSG_S2C_GAME_START, 3000);
    const auto game_start =
        ParsePayload<lawnmower::S2C_GameStart>(game_start_packet);
    Require(game_start.success(), "结算 smoke: 开局失败");

    const auto game_over_packet =
        host.ReceiveUntil(lawnmower::MSG_S2C_GAME_OVER, 5000);
    const auto game_over =
        ParsePayload<lawnmower::S2C_GameOver>(game_over_packet);
    Require(!game_over.victory(), "结算 smoke: 单人死亡应为失败结算");
    Require(game_over.scores_size() == 1,
            "结算 smoke: 分数列表应只包含当前玩家");
    Require(game_over.scores(0).player_id() == player_id,
            "结算 smoke: GameOver 分数 player_id 不匹配");

    const auto room_update_packet =
        host.ReceiveUntil(lawnmower::MSG_S2C_ROOM_UPDATE, 3000);
    const auto room_update =
        ParsePayload<lawnmower::S2C_RoomUpdate>(room_update_packet);
    Require(room_update.room_id() == room_id,
            "结算 smoke: room_update room_id 不匹配");
    Require(room_update.players_size() == 1,
            "结算 smoke: 结束后房间成员数量异常");

    lawnmower::C2S_GetRoomList room_list_req;
    host.Send(lawnmower::MSG_C2S_GET_ROOM_LIST, room_list_req);
    const auto room_list_packet =
        host.ReceiveUntil(lawnmower::MSG_S2C_ROOM_LIST, 3000);
    const auto room_list =
        ParsePayload<lawnmower::S2C_RoomList>(room_list_packet);
    bool is_playing = true;
    Require(HasRoom(room_list, room_id, &is_playing),
            "结算 smoke: 房间列表中未找到目标房间");
    Require(!is_playing, "结算 smoke: GameOver 后房间应回到非游戏态");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto metrics_after = CollectMetricsFiles(repo_root);
    metrics_cleanup.Files() = DiffMetricsFiles(metrics_before, metrics_after);
    Require(!metrics_cleanup.Files().empty(),
            "结算 smoke: 未生成新的性能指标文件");

    const std::string metrics_content =
        ReadFile(metrics_cleanup.Files().front());
    Require(ExtractJsonUIntField(metrics_content, "room_id") == room_id,
            "结算 smoke: 性能指标 room_id 不匹配");
    Require(ExtractJsonUIntField(metrics_content, "tick_count") > 0,
            "结算 smoke: 性能指标 tick_count 应大于 0");
    Require(metrics_content.find("\"samples\": [") != std::string::npos,
            "结算 smoke: 性能指标缺少 samples");
  }
}

void RunSmoke(const std::string& server_binary) {
  RunHeartbeatAndUpgradeSmoke(server_binary);
  RunGameOverAndMetricsSmoke(server_binary);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "用法: server_net_fix_smoke_test <server_binary>\n";
    return 2;
  }

  try {
    RunSmoke(argv[1]);
    std::cout << "server_net_fix_smoke_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "server_net_fix_smoke_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
