#include <algorithm>
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
      (fs::temp_directory_path() / "server-smoke-XXXXXX").string();
  std::vector<char> buf(pattern.begin(), pattern.end());
  buf.push_back('\0');
  char* created = ::mkdtemp(buf.data());
  if (created == nullptr) {
    Fail("创建临时目录失败");
  }
  return fs::path(created);
}

void WriteServerConfig(const fs::path& workspace, uint16_t tcp_port,
                       uint16_t udp_port) {
  const fs::path cfg_dir = workspace / "game_config";
  std::error_code ec;
  fs::create_directories(cfg_dir, ec);
  if (ec) {
    Fail("创建 game_config 目录失败: " + ec.message());
  }

  const fs::path cfg = cfg_dir / "server_config.json";
  std::ofstream out(cfg);
  if (!out.is_open()) {
    Fail("写入 server_config.json 失败");
  }

  out << "{\n";
  out << "  \"tcp_port\": " << tcp_port << ",\n";
  out << "  \"udp_port\": " << udp_port << ",\n";
  out << "  \"max_players_per_room\": 4,\n";
  out << "  \"tick_rate\": 30,\n";
  out << "  \"state_sync_rate\": 10,\n";
  out << "  \"reconnect_grace_seconds\": 1.0,\n";
  out << "  \"log_level\": \"warn\"\n";
  out << "}\n";
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
            const google::protobuf::Message& payload) {
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

bool RoomUpdateHasPlayerReady(const lawnmower::S2C_RoomUpdate& update,
                              uint32_t player_id, bool is_ready) {
  for (const auto& player : update.players()) {
    if (player.player_id() == player_id && player.is_ready() == is_ready) {
      return true;
    }
  }
  return false;
}

template <typename Request, typename Response, typename MessageGetter>
void ExpectLoginRequiredFailure(TcpClient& client,
                                lawnmower::MessageType request_type,
                                lawnmower::MessageType response_type,
                                const Request& request,
                                MessageGetter&& message_getter,
                                const std::string& scenario) {
  client.Send(request_type, request);
  const auto packet = client.ReceiveUntil(response_type, 3000);
  const auto response = ParsePayload<Response>(packet);
  Require(!response.success(), scenario + " 未登录时应返回 success=false");
  const std::string message = message_getter(response);
  Require(message.find("请先登录") != std::string::npos,
          scenario + " 未登录提示不正确: " + message);
}

void RunSmoke(const std::string& server_binary) {
  const uint16_t tcp_port = ReservePort(SOCK_STREAM);
  const uint16_t udp_port = ReservePort(SOCK_DGRAM);
  const fs::path workspace = CreateTempWorkspace();
  WriteServerConfig(workspace, tcp_port, udp_port);

  ServerProcess server(server_binary, workspace);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  TcpClient unauth("127.0.0.1", tcp_port, 5000);
  lawnmower::C2S_CreateRoom unauth_create_room;
  unauth_create_room.set_room_name("unauth_room");
  unauth_create_room.set_max_players(2);
  ExpectLoginRequiredFailure<lawnmower::C2S_CreateRoom,
                             lawnmower::S2C_CreateRoomResult>(
      unauth, lawnmower::MSG_C2S_CREATE_ROOM,
      lawnmower::MSG_S2C_CREATE_ROOM_RESULT, unauth_create_room,
      [](const lawnmower::S2C_CreateRoomResult& result) {
        return result.message_create();
      },
      "CreateRoom");

  lawnmower::C2S_JoinRoom unauth_join_room;
  unauth_join_room.set_room_id(1);
  ExpectLoginRequiredFailure<lawnmower::C2S_JoinRoom,
                             lawnmower::S2C_JoinRoomResult>(
      unauth, lawnmower::MSG_C2S_JOIN_ROOM, lawnmower::MSG_S2C_JOIN_ROOM_RESULT,
      unauth_join_room,
      [](const lawnmower::S2C_JoinRoomResult& result) {
        return result.message_join();
      },
      "JoinRoom");

  lawnmower::C2S_LeaveRoom unauth_leave_room;
  ExpectLoginRequiredFailure<lawnmower::C2S_LeaveRoom,
                             lawnmower::S2C_LeaveRoomResult>(
      unauth, lawnmower::MSG_C2S_LEAVE_ROOM,
      lawnmower::MSG_S2C_LEAVE_ROOM_RESULT, unauth_leave_room,
      [](const lawnmower::S2C_LeaveRoomResult& result) {
        return result.message_leave();
      },
      "LeaveRoom");

  lawnmower::C2S_SetReady unauth_set_ready;
  unauth_set_ready.set_is_ready(true);
  ExpectLoginRequiredFailure<lawnmower::C2S_SetReady,
                             lawnmower::S2C_SetReadyResult>(
      unauth, lawnmower::MSG_C2S_SET_READY, lawnmower::MSG_S2C_SET_READY_RESULT,
      unauth_set_ready,
      [](const lawnmower::S2C_SetReadyResult& result) {
        return result.message_ready();
      },
      "SetReady");
  unauth.Close();

  TcpClient host("127.0.0.1", tcp_port, 5000);
  lawnmower::C2S_Login host_login;
  host_login.set_player_name("smoke_host");
  host.Send(lawnmower::MSG_C2S_LOGIN, host_login);

  const auto host_login_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_LOGIN_RESULT, 3000);
  const auto host_login_result =
      ParsePayload<lawnmower::S2C_LoginResult>(host_login_packet);
  Require(host_login_result.success(), "房主登录失败");
  Require(host_login_result.player_id() > 0, "房主 player_id 非法");
  Require(!host_login_result.session_token().empty(),
          "房主 session_token 为空");

  lawnmower::C2S_CreateRoom create_room;
  create_room.set_room_name("smoke_room");
  create_room.set_max_players(2);
  host.Send(lawnmower::MSG_C2S_CREATE_ROOM, create_room);
  const auto create_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_CREATE_ROOM_RESULT, 3000);
  const auto create_result =
      ParsePayload<lawnmower::S2C_CreateRoomResult>(create_packet);
  Require(create_result.success(), "建房失败");
  Require(create_result.room_id() > 0, "room_id 非法");
  const uint32_t room_id = create_result.room_id();
  const uint32_t host_player_id = host_login_result.player_id();

  TcpClient guest("127.0.0.1", tcp_port, 5000);
  lawnmower::C2S_Login guest_login;
  guest_login.set_player_name("smoke_guest");
  guest.Send(lawnmower::MSG_C2S_LOGIN, guest_login);
  const auto guest_login_packet =
      guest.ReceiveUntil(lawnmower::MSG_S2C_LOGIN_RESULT, 3000);
  const auto guest_login_result =
      ParsePayload<lawnmower::S2C_LoginResult>(guest_login_packet);
  Require(guest_login_result.success(), "访客登录失败");
  Require(!guest_login_result.session_token().empty(),
          "访客 session_token 为空");

  lawnmower::C2S_GetRoomList room_list_req;
  guest.Send(lawnmower::MSG_C2S_GET_ROOM_LIST, room_list_req);
  const auto room_list_packet =
      guest.ReceiveUntil(lawnmower::MSG_S2C_ROOM_LIST, 3000);
  const auto room_list =
      ParsePayload<lawnmower::S2C_RoomList>(room_list_packet);

  Require(HasRoom(room_list, room_id, nullptr), "房间列表中未找到刚创建的房间");

  lawnmower::C2S_JoinRoom join_room;
  join_room.set_room_id(room_id);
  guest.Send(lawnmower::MSG_C2S_JOIN_ROOM, join_room);

  const auto host_join_update_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_ROOM_UPDATE, 3000);
  const auto host_join_update =
      ParsePayload<lawnmower::S2C_RoomUpdate>(host_join_update_packet);
  Require(host_join_update.room_id() == room_id,
          "房主收到的 room_update 房间号不匹配");
  Require(host_join_update.players_size() == 2,
          "加入后房主看到的玩家数量不是2");

  const auto guest_join_update_packet =
      guest.ReceiveUntil(lawnmower::MSG_S2C_ROOM_UPDATE, 3000);
  const auto guest_join_update =
      ParsePayload<lawnmower::S2C_RoomUpdate>(guest_join_update_packet);
  Require(guest_join_update.room_id() == room_id,
          "访客收到的 room_update 房间号不匹配");
  Require(guest_join_update.players_size() == 2,
          "加入后访客看到的玩家数量不是2");

  const auto join_result_packet =
      guest.ReceiveUntil(lawnmower::MSG_S2C_JOIN_ROOM_RESULT, 3000);
  const auto join_result =
      ParsePayload<lawnmower::S2C_JoinRoomResult>(join_result_packet);
  Require(join_result.success(), "加入房间失败");

  lawnmower::C2S_StartGame start_game;
  guest.Send(lawnmower::MSG_C2S_START_GAME, start_game);
  const auto non_host_start_packet =
      guest.ReceiveUntil(lawnmower::MSG_S2C_GAME_START, 3000);
  const auto non_host_start =
      ParsePayload<lawnmower::S2C_GameStart>(non_host_start_packet);
  Require(!non_host_start.success(), "非房主开局应失败");
  Require(non_host_start.message_start().find("只有房主") != std::string::npos,
          "非房主开局失败文案不正确: " + non_host_start.message_start());

  host.Send(lawnmower::MSG_C2S_START_GAME, start_game);
  const auto start_fail_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_GAME_START, 3000);
  const auto start_fail =
      ParsePayload<lawnmower::S2C_GameStart>(start_fail_packet);
  Require(!start_fail.success(), "存在未 ready 玩家时开局应失败");
  Require(start_fail.message_start().find("未准备") != std::string::npos,
          "未准备开局失败文案不正确: " + start_fail.message_start());

  lawnmower::C2S_SetReady set_ready;
  set_ready.set_is_ready(true);
  guest.Send(lawnmower::MSG_C2S_SET_READY, set_ready);

  const auto host_ready_update_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_ROOM_UPDATE, 3000);
  const auto host_ready_update =
      ParsePayload<lawnmower::S2C_RoomUpdate>(host_ready_update_packet);
  Require(host_ready_update.room_id() == room_id,
          "准备后房主收到 room_update 房间号不匹配");
  Require(RoomUpdateHasPlayerReady(host_ready_update,
                                   guest_login_result.player_id(), true),
          "准备后房主视角未看到访客 ready=true");

  const auto set_ready_result_packet =
      guest.ReceiveUntil(lawnmower::MSG_S2C_SET_READY_RESULT, 3000);
  const auto set_ready_result =
      ParsePayload<lawnmower::S2C_SetReadyResult>(set_ready_result_packet);
  Require(set_ready_result.success(), "设置准备状态失败");
  Require(set_ready_result.is_ready(), "设置准备状态后返回 is_ready=false");

  host.Send(lawnmower::MSG_C2S_START_GAME, start_game);
  const auto host_game_start_packet =
      host.ReceiveUntil(lawnmower::MSG_S2C_GAME_START, 3000);
  const auto host_game_start =
      ParsePayload<lawnmower::S2C_GameStart>(host_game_start_packet);
  Require(host_game_start.success(), "房主开始游戏失败");
  Require(host_game_start.room_id() == room_id,
          "房主收到 game_start 房间号不匹配");

  const auto guest_game_start_packet =
      guest.ReceiveUntil(lawnmower::MSG_S2C_GAME_START, 3000);
  const auto guest_game_start =
      ParsePayload<lawnmower::S2C_GameStart>(guest_game_start_packet);
  Require(guest_game_start.success(), "访客收到 game_start 失败");
  Require(guest_game_start.room_id() == room_id,
          "访客收到 game_start 房间号不匹配");

  TcpClient observer("127.0.0.1", tcp_port, 5000);
  lawnmower::C2S_Login observer_login;
  observer_login.set_player_name("smoke_observer");
  observer.Send(lawnmower::MSG_C2S_LOGIN, observer_login);
  const auto observer_login_packet =
      observer.ReceiveUntil(lawnmower::MSG_S2C_LOGIN_RESULT, 3000);
  const auto observer_login_result =
      ParsePayload<lawnmower::S2C_LoginResult>(observer_login_packet);
  Require(observer_login_result.success(), "观察者登录失败");

  observer.Send(lawnmower::MSG_C2S_GET_ROOM_LIST, room_list_req);
  const auto observer_room_list_packet =
      observer.ReceiveUntil(lawnmower::MSG_S2C_ROOM_LIST, 3000);
  const auto observer_room_list =
      ParsePayload<lawnmower::S2C_RoomList>(observer_room_list_packet);
  bool is_playing = false;
  Require(HasRoom(observer_room_list, room_id, &is_playing),
          "开局后房间列表未找到目标房间");
  Require(is_playing, "开局后房间列表中 is_playing 不是 true");

  TcpClient bad_token_reconnect_client("127.0.0.1", tcp_port, 5000);
  lawnmower::C2S_ReconnectRequest bad_token_reconnect;
  bad_token_reconnect.set_player_id(guest_login_result.player_id());
  bad_token_reconnect.set_room_id(room_id);
  bad_token_reconnect.set_session_token("invalid-token-for-smoke");
  bad_token_reconnect.set_last_input_seq(0);
  bad_token_reconnect.set_last_server_tick(0);
  bad_token_reconnect_client.Send(lawnmower::MSG_C2S_RECONNECT_REQUEST,
                                  bad_token_reconnect);
  const auto bad_token_ack_packet = bad_token_reconnect_client.ReceiveUntil(
      lawnmower::MSG_S2C_RECONNECT_ACK, 3000);
  const auto bad_token_ack =
      ParsePayload<lawnmower::S2C_ReconnectAck>(bad_token_ack_packet);
  Require(!bad_token_ack.success(), "错误 token 重连应失败");
  Require(bad_token_ack.message().find("令牌") != std::string::npos,
          "错误 token 重连失败文案不正确: " + bad_token_ack.message());

  host.Close();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));

  TcpClient reconnect_client("127.0.0.1", tcp_port, 5000);
  lawnmower::C2S_ReconnectRequest reconnect;
  reconnect.set_player_id(host_player_id);
  reconnect.set_room_id(room_id);
  reconnect.set_session_token(host_login_result.session_token());
  reconnect.set_last_input_seq(0);
  reconnect.set_last_server_tick(0);
  reconnect_client.Send(lawnmower::MSG_C2S_RECONNECT_REQUEST, reconnect);
  const auto reconnect_packet =
      reconnect_client.ReceiveUntil(lawnmower::MSG_S2C_RECONNECT_ACK, 3000);
  const auto reconnect_ack =
      ParsePayload<lawnmower::S2C_ReconnectAck>(reconnect_packet);

  Require(reconnect_ack.success(), "重连 ACK 失败");
  Require(reconnect_ack.player_id() == host_player_id, "重连 player_id 不匹配");
  Require(reconnect_ack.room_id() == room_id, "重连 room_id 不匹配");
  Require(!reconnect_ack.session_token().empty(),
          "重连 ACK session_token 为空");

  guest.Close();
  std::this_thread::sleep_for(std::chrono::milliseconds(2200));

  TcpClient guest_reconnect_client("127.0.0.1", tcp_port, 5000);
  lawnmower::C2S_ReconnectRequest guest_reconnect;
  guest_reconnect.set_player_id(guest_login_result.player_id());
  guest_reconnect.set_room_id(room_id);
  guest_reconnect.set_session_token(guest_login_result.session_token());
  guest_reconnect.set_last_input_seq(0);
  guest_reconnect.set_last_server_tick(0);
  guest_reconnect_client.Send(lawnmower::MSG_C2S_RECONNECT_REQUEST,
                              guest_reconnect);
  const auto guest_reconnect_packet = guest_reconnect_client.ReceiveUntil(
      lawnmower::MSG_S2C_RECONNECT_ACK, 3000);
  const auto guest_reconnect_ack =
      ParsePayload<lawnmower::S2C_ReconnectAck>(guest_reconnect_packet);
  Require(!guest_reconnect_ack.success(), "重连超过宽限期应失败");
  Require(guest_reconnect_ack.message().find("不在房间") != std::string::npos,
          "重连超时失败文案不正确: " + guest_reconnect_ack.message());

  server.Stop();
  std::error_code ec;
  fs::remove_all(workspace, ec);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "用法: server_smoke_test <server_binary>\n";
    return 2;
  }

  try {
    RunSmoke(argv[1]);
    std::cout << "server_smoke_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "server_smoke_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
