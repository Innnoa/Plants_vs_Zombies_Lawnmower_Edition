#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "network/tcp/tcp_session.hpp"
#include "network/tcp/tcp_session_internal.hpp"
#include "network/udp/udp_server.hpp"

template <typename Request, typename Handler>
void TcpSession::HandleLoggedInRequest(const std::string& payload,
                                       const char* parse_warn_message,
                                       const char* login_warn_message,
                                       Handler&& handler) {
  Request request;
  if (!tcp_session_internal::ParsePayload(payload, &request,
                                          parse_warn_message)) {
    return;
  }
  if (!EnsureLoggedInOrWarn(login_warn_message)) {
    return;
  }
  handler(request);
}

// 处理开始游戏请求
void TcpSession::HandleStartGame(const std::string& payload) {
  lawnmower::C2S_StartGame request;
  if (!tcp_session_internal::ParsePayload(payload, &request,
                                          "解析开始游戏请求失败")) {
    return;
  }

  lawnmower::S2C_GameStart result;
  auto snapshot = RoomManager::Instance().TryStartGame(player_id_, &result);
  if (!result.success()) {
    SendProto(lawnmower::MessageType::MSG_S2C_GAME_START, result);
    return;
  }

  const lawnmower::SceneInfo scene_info =
      GameManager::Instance().CreateScene(*snapshot);

  // mutable_scene() 返回 SceneInfo*（指向 result
  // 内部的子消息），解引用后整体赋值。
  // 等价写法：result.mutable_scene()->CopyFrom(scene_info);
  *result.mutable_scene() = scene_info;

  const auto sessions =
      RoomManager::Instance().GetRoomSessions(snapshot->room_id);
  // 广播
  tcp_session_internal::BroadcastToRoom(
      sessions, lawnmower::MessageType::MSG_S2C_GAME_START, result);

  if (SendFullSyncToRoom(snapshot->room_id, sessions,
                         scene_info.state_sync_rate())) {
    GameManager::Instance().StartGameLoop(snapshot->room_id);
  }
  spdlog::info("房间 {} 游戏开始", snapshot->room_id);
}

bool TcpSession::EnsureLoggedInOrWarn(const char* warn_message) const {
  if (player_id_ != 0) {
    return true;
  }
  if (warn_message != nullptr) {
    spdlog::warn("{}", warn_message);
  }
  return false;
}

// 处理玩家输入请求
void TcpSession::HandlePlayerInput(const std::string& payload) {
  HandleLoggedInRequest<lawnmower::C2S_PlayerInput>(
      payload, "解析玩家输入失败", "未登录玩家发送移动输入",
      [this](lawnmower::C2S_PlayerInput& input) {
        if (!input.session_token().empty() &&
            !VerifyToken(player_id_, input.session_token())) {
          spdlog::warn("玩家 {} 输入令牌校验失败", player_id_);
          return;
        }

        input.set_player_id(player_id_);

        uint32_t room_id = 0;
        if (!GameManager::Instance().HandlePlayerInput(player_id_, input,
                                                       &room_id)) {
          spdlog::debug("玩家 {} 输入被拒绝或未找到场景", player_id_);
        }
      });
}

// 处理升级请求确认
void TcpSession::HandleUpgradeRequestAck(const std::string& payload) {
  HandleLoggedInRequest<lawnmower::C2S_UpgradeRequestAck>(
      payload, "解析升级请求确认失败", "未登录玩家发送升级请求确认",
      [this](lawnmower::C2S_UpgradeRequestAck& ack) {
        ack.set_player_id(player_id_);
        if (!GameManager::Instance().HandleUpgradeRequestAck(player_id_, ack)) {
          spdlog::debug("玩家 {} 升级请求确认被拒绝", player_id_);
        }
      });
}

// 处理升级选项确认
void TcpSession::HandleUpgradeOptionsAck(const std::string& payload) {
  HandleLoggedInRequest<lawnmower::C2S_UpgradeOptionsAck>(
      payload, "解析升级选项确认失败", "未登录玩家发送升级选项确认",
      [this](lawnmower::C2S_UpgradeOptionsAck& ack) {
        ack.set_player_id(player_id_);
        if (!GameManager::Instance().HandleUpgradeOptionsAck(player_id_, ack)) {
          spdlog::debug("玩家 {} 升级选项确认被拒绝", player_id_);
        }
      });
}

// 处理升级选择
void TcpSession::HandleUpgradeSelect(const std::string& payload) {
  HandleLoggedInRequest<lawnmower::C2S_UpgradeSelect>(
      payload, "解析升级选择失败", "未登录玩家发送升级选择",
      [this](lawnmower::C2S_UpgradeSelect& select) {
        select.set_player_id(player_id_);
        if (!GameManager::Instance().HandleUpgradeSelect(player_id_, select)) {
          spdlog::debug("玩家 {} 升级选择被拒绝", player_id_);
        }
      });
}

// 处理刷新升级请求
void TcpSession::HandleUpgradeRefreshRequest(const std::string& payload) {
  HandleLoggedInRequest<lawnmower::C2S_UpgradeRefreshRequest>(
      payload, "解析刷新升级请求失败", "未登录玩家发送刷新升级请求",
      [this](lawnmower::C2S_UpgradeRefreshRequest& refresh) {
        refresh.set_player_id(player_id_);
        if (!GameManager::Instance().HandleUpgradeRefreshRequest(player_id_,
                                                                 refresh)) {
          spdlog::debug("玩家 {} 刷新升级请求被拒绝", player_id_);
        }
      });
}

bool TcpSession::SendFullSyncToRoom(
    uint32_t room_id, const std::vector<std::weak_ptr<TcpSession>>& sessions,
    uint32_t state_sync_rate) {
  lawnmower::S2C_GameStateSync sync;
  if (!GameManager::Instance().BuildFullState(room_id, &sync)) {
    return false;
  }

  bool sent_udp = false;
  if (auto udp = GameManager::Instance().GetUdpServer()) {
    sent_udp = udp->BroadcastState(room_id, sync) > 0;
  }
  if (!sent_udp) {
    // 尚未收到客户端 UDP，首帧用 TCP 兜底
    tcp_session_internal::BroadcastToRoom(
        sessions, lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC, sync);
  }

  const uint32_t sync_rate = std::max<uint32_t>(1, state_sync_rate);
  if (auto io = GameManager::Instance().GetIoContext()) {
    auto timer = std::make_shared<asio::steady_timer>(*io);
    const int interval_ms =
        std::max(1, static_cast<int>(1000.0 / static_cast<double>(sync_rate)));
    timer->expires_after(std::chrono::milliseconds(interval_ms));
    timer->async_wait([room_id, timer](const asio::error_code& ec) {
      if (ec == asio::error::operation_aborted) {
        return;
      }
      lawnmower::S2C_GameStateSync retry_sync;
      if (GameManager::Instance().BuildFullState(room_id, &retry_sync)) {
        bool sent_retry_udp = false;
        if (auto udp = GameManager::Instance().GetUdpServer()) {
          sent_retry_udp = udp->BroadcastState(room_id, retry_sync) > 0;
        }
        if (!sent_retry_udp) {
          const auto retry_sessions =
              RoomManager::Instance().GetRoomSessions(room_id);
          tcp_session_internal::BroadcastToRoom(
              retry_sessions, lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC,
              retry_sync);
        }
      }
    });
  }

  spdlog::debug("全量同步发送 room_id={} target=room udp={}", room_id,
                sent_udp ? "true" : "false");
  return true;
}

void TcpSession::SendFullSyncToSession(uint32_t room_id) {
  lawnmower::S2C_GameStateSync sync;
  if (!GameManager::Instance().BuildFullState(room_id, &sync)) {
    return;
  }
  SendProto(lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC, sync);
  spdlog::debug("全量同步发送 room_id={} target=session", room_id);
}
