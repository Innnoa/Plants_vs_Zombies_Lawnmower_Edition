#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <random>
#include <sstream>

#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "network/tcp/tcp_session.hpp"
#include "network/tcp/tcp_session_internal.hpp"

// 生成Token
std::string TcpSession::GenerateToken() {
  std::array<uint8_t, tcp_session_internal::kTokenBytes> buf{};
  // 生成静态随机数使用梅森旋转算法，大小为unsigned longlong,
  // rng重载了（）运算符，random_device{}()为种子
  static thread_local std::mt19937_64 rng(std::random_device{}());
  // 每轮写入 8 字节（uint64_t），两轮即可填满 16 字节（128bit token）。
  for (std::size_t i = 0; i < tcp_session_internal::kTokenBytes;
       i += sizeof(uint64_t)) {
    const uint64_t v = rng();  // 获取随机数

    // 把随机数v的原始字节拷贝至buf的第i段
    // std::min的意义是让代码对任意token长度都安全，如果将来kTokenBytes不是8的倍数，
    // 则最后一轮只拷贝剩余的字节数，避免越界
    std::memcpy(
        buf.data() + i, &v,
        std::min(sizeof(uint64_t), tcp_session_internal::kTokenBytes - i));
  }
  std::ostringstream oss;  // 用于将各种数据类型转化为string格式
  oss << std::hex;  // ostringstream重载了 << 用于将数据存至对象中/设置进制格式
  for (auto b : buf) {
    // 输出为两位 hex（如 0x0A -> "0a"）；width
    // 仅对下一次插入生效，因此放在循环内。
    oss.width(2);
    oss.fill('0');
    oss << static_cast<int>(b);  // 避免按字符输出
  }
  return oss.str();  // 返回oss内容
}

// 注册Token
void TcpSession::RegisterToken(uint32_t player_id, std::string token) {
  std::lock_guard<std::mutex> lock(token_mutex_);  // 互斥锁-上锁
  session_tokens_[player_id] = std::move(token);   // 添加该节点
}

// 验证Token
bool TcpSession::VerifyToken(uint32_t player_id, std::string_view token) {
  std::lock_guard<std::mutex> lock(token_mutex_);   // 互斥锁-上锁
  const auto it = session_tokens_.find(player_id);  // 寻找该节点
  return it != session_tokens_.end() && it->second == token;
}

// 撤销Token
void TcpSession::RevokeToken(uint32_t player_id) {
  std::lock_guard<std::mutex> lock(token_mutex_);  // 互斥锁-上锁
  session_tokens_.erase(player_id);                // 擦除该节点
}

void TcpSession::SetPacketDebugLogStride(uint32_t stride) {
  packet_debug_log_stride_.store(std::max<uint32_t>(1, stride),
                                 std::memory_order_relaxed);
}

bool TcpSession::ShouldLogPacketDebug() {
  const uint32_t stride = std::max<uint32_t>(
      1, packet_debug_log_stride_.load(std::memory_order_relaxed));
  const uint64_t index =
      packet_debug_log_counter_.fetch_add(1, std::memory_order_relaxed);
  return (index % stride) == 0;
}

// 处理登录请求
void TcpSession::HandleLogin(const std::string& payload) {
  lawnmower::C2S_Login login;
  if (!tcp_session_internal::ParsePayload(payload, &login,
                                          "解析登录包体失败")) {
    return;
  }

  if (player_id_ != 0) {
    lawnmower::S2C_LoginResult result;
    result.set_success(false);
    result.set_player_id(player_id_);
    result.set_message_login("重复登录");
    SendProto(lawnmower::MessageType::MSG_S2C_LOGIN_RESULT, result);
    return;
  }

  player_id_ = next_player_id_.fetch_add(1);  // 原子加1
  // 设置玩家名，若未输入玩家名则为玩家+id,否则则为玩家名
  player_name_ = login.player_name().empty()
                     ? ("玩家" + std::to_string(player_id_))
                     : login.player_name();
  // 获取Token
  session_token_ = GenerateToken();
  // 注册Token
  RegisterToken(player_id_, session_token_);

  lawnmower::S2C_LoginResult result;
  result.set_success(true);
  result.set_player_id(player_id_);
  result.set_message_login("login success");
  result.set_session_token(session_token_);

  SendProto(lawnmower::MessageType::MSG_S2C_LOGIN_RESULT, result);
  spdlog::info("玩家登录: {} (id={})", player_name_, player_id_);
}

// 处理心跳请求
void TcpSession::HandleHeartbeat(const std::string& payload) {
  lawnmower::C2S_Heartbeat heartbeat;
  if (!tcp_session_internal::ParsePayload(payload, &heartbeat,
                                          "解析心跳包失败")) {
    return;
  }

  lawnmower::S2C_Heartbeat reply;
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch());
  reply.set_timestamp(static_cast<uint64_t>(now_ms.count()));
  reply.set_online_players(active_sessions_.load(std::memory_order_relaxed));

  SendProto(lawnmower::MessageType::MSG_S2C_HEARTBEAT, reply);
}

// 处理重连请求
void TcpSession::HandleReconnectRequest(const std::string& payload) {
  lawnmower::C2S_ReconnectRequest request;
  if (!tcp_session_internal::ParsePayload(payload, &request,
                                          "解析重连请求包失败")) {
    return;
  }

  lawnmower::S2C_ReconnectAck ack;
  ack.set_player_id(request.player_id());
  ack.set_room_id(request.room_id());
  const auto log_fail = [&](const char* reason) {
    spdlog::info("[reconnect] fail player_id={} room_id={} reason={}",
                 request.player_id(), request.room_id(),
                 reason != nullptr ? reason : "");
  };

  if (player_id_ != 0) {
    ack.set_success(false);
    ack.set_message("当前会话已登录");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    log_fail("session already logged in");
    return;
  }
  if (request.player_id() == 0) {
    ack.set_success(false);
    ack.set_message("缺少玩家ID");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    log_fail("missing player id");
    return;
  }

  const auto room_opt =
      RoomManager::Instance().GetPlayerRoom(request.player_id());
  if (!room_opt.has_value()) {
    ack.set_success(false);
    ack.set_message("玩家不在房间");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    log_fail("player not in room");
    return;
  }
  const uint32_t target_room_id = *room_opt;
  if (request.room_id() != 0 && request.room_id() != target_room_id) {
    ack.set_success(false);
    ack.set_message("房间不匹配");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    log_fail("room mismatch");
    return;
  }
  if (!request.session_token().empty() &&
      !VerifyToken(request.player_id(), request.session_token())) {
    ack.set_success(false);
    ack.set_message("会话令牌无效");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    log_fail("invalid session token");
    return;
  }

  bool is_playing = false;
  std::string player_name;
  if (!RoomManager::Instance().AttachSession(request.player_id(),
                                             target_room_id, weak_from_this(),
                                             &is_playing, &player_name)) {
    ack.set_success(false);
    ack.set_message("重连失败");
    SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
    log_fail("attach session failed");
    return;
  }

  if (is_playing) {
    GameManager::ReconnectSnapshot snapshot;
    if (!GameManager::Instance().TryReconnectPlayer(
            request.player_id(), target_room_id, request.last_input_seq(),
            request.last_server_tick(), &snapshot)) {
      RoomManager::Instance().MarkPlayerDisconnected(request.player_id());
      ack.set_success(false);
      ack.set_message("场景不存在");
      SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
      log_fail("scene missing");
      return;
    }
    ack.set_server_tick(static_cast<uint32_t>(snapshot.server_tick));
    ack.set_is_paused(snapshot.is_paused);
    if (player_name.empty()) {
      player_name = snapshot.player_name;
    }
  }

  ack.set_room_id(target_room_id);
  ack.set_is_playing(is_playing);

  std::string token = request.session_token();
  if (token.empty()) {
    token = GenerateToken();
  }
  RegisterToken(request.player_id(), token);
  session_token_ = token;
  ack.set_session_token(token);

  player_id_ = request.player_id();
  if (!player_name.empty()) {
    player_name_ = player_name;
  } else {
    player_name_ = "玩家" + std::to_string(player_id_);
  }

  ack.set_success(true);
  ack.set_message("reconnect success");
  SendProto(lawnmower::MessageType::MSG_S2C_RECONNECT_ACK, ack);
  spdlog::info("[reconnect] success player_id={} room_id={} is_playing={}",
               request.player_id(), target_room_id,
               is_playing ? "true" : "false");

  if (is_playing) {
    SendFullSyncToSession(target_room_id);
  }
}
