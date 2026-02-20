#include <spdlog/spdlog.h>

#include "game/managers/room_manager.hpp"
#include "network/tcp/tcp_session.hpp"
#include "network/tcp/tcp_session_internal.hpp"

namespace {

template <typename Request, typename Result, typename BuildFn>
void HandleRequestWithResult(TcpSession* session, const std::string& payload,
                             const char* parse_warn_message,
                             lawnmower::MessageType reply_type,
                             BuildFn&& build_result) {
  if (session == nullptr) {
    return;
  }
  Request request;
  if (!tcp_session_internal::ParsePayload(payload, &request,
                                          parse_warn_message)) {
    return;
  }
  Result result = build_result(request);
  session->SendProto(reply_type, result);
}

void FillLoginRequiredResult(lawnmower::S2C_CreateRoomResult* result) {
  if (result == nullptr) {
    return;
  }
  result->set_success(false);
  result->set_message_create("请先登录");
}

void FillLoginRequiredResult(lawnmower::S2C_JoinRoomResult* result) {
  if (result == nullptr) {
    return;
  }
  result->set_success(false);
  result->set_message_join("请先登录");
}

void FillLoginRequiredResult(lawnmower::S2C_LeaveRoomResult* result) {
  if (result == nullptr) {
    return;
  }
  result->set_success(false);
  result->set_message_leave("请先登录");
}

void FillLoginRequiredResult(lawnmower::S2C_SetReadyResult* result) {
  if (result == nullptr) {
    return;
  }
  result->set_success(false);
  result->set_message_ready("请先登录");
}

}  // namespace

// 处理创建房间请求
void TcpSession::HandleCreateRoom(const std::string& payload) {
  HandleRequestWithResult<lawnmower::C2S_CreateRoom,
                          lawnmower::S2C_CreateRoomResult>(
      this, payload, "解析创建房间包体失败",
      lawnmower::MessageType::MSG_S2C_CREATE_ROOM_RESULT,
      [this](const lawnmower::C2S_CreateRoom& request) {
        lawnmower::S2C_CreateRoomResult result;
        if (player_id_ == 0) {
          FillLoginRequiredResult(&result);
          return result;
        }
        return RoomManager::Instance().CreateRoom(player_id_, player_name_,
                                                  weak_from_this(), request);
      });
}

// 处理获取房间列表请求
void TcpSession::HandleGetRoomList(const std::string& payload) {
  HandleRequestWithResult<lawnmower::C2S_GetRoomList, lawnmower::S2C_RoomList>(
      this, payload, "解析房间列表请求失败",
      lawnmower::MessageType::MSG_S2C_ROOM_LIST,
      [this](const lawnmower::C2S_GetRoomList&) {
        lawnmower::S2C_RoomList list;
        if (player_id_ != 0) {
          list = RoomManager::Instance().GetRoomList();
        }
        spdlog::debug("发送房间列表给玩家 {}", player_id_);
        return list;
      });
}

// 处理加入房间请求
void TcpSession::HandleJoinRoom(const std::string& payload) {
  HandleRequestWithResult<lawnmower::C2S_JoinRoom,
                          lawnmower::S2C_JoinRoomResult>(
      this, payload, "解析加入房间包体失败",
      lawnmower::MessageType::MSG_S2C_JOIN_ROOM_RESULT,
      [this](const lawnmower::C2S_JoinRoom& request) {
        lawnmower::S2C_JoinRoomResult result;
        if (player_id_ == 0) {
          FillLoginRequiredResult(&result);
          return result;
        }
        return RoomManager::Instance().JoinRoom(player_id_, player_name_,
                                                weak_from_this(), request);
      });
}

// 处理离开房间请求
void TcpSession::HandleLeaveRoom(const std::string& payload) {
  HandleRequestWithResult<lawnmower::C2S_LeaveRoom,
                          lawnmower::S2C_LeaveRoomResult>(
      this, payload, "解析离开房间包体失败",
      lawnmower::MessageType::MSG_S2C_LEAVE_ROOM_RESULT,
      [this](const lawnmower::C2S_LeaveRoom&) {
        lawnmower::S2C_LeaveRoomResult result;
        if (player_id_ == 0) {
          FillLoginRequiredResult(&result);
          return result;
        }
        return RoomManager::Instance().LeaveRoom(player_id_);
      });
}

// 处理设置准备状态请求
void TcpSession::HandleSetReady(const std::string& payload) {
  HandleRequestWithResult<lawnmower::C2S_SetReady,
                          lawnmower::S2C_SetReadyResult>(
      this, payload, "解析设置准备状态包体失败",
      lawnmower::MessageType::MSG_S2C_SET_READY_RESULT,
      [this](const lawnmower::C2S_SetReady& request) {
        lawnmower::S2C_SetReadyResult result;
        if (player_id_ == 0) {
          FillLoginRequiredResult(&result);
          return result;
        }
        return RoomManager::Instance().SetReady(player_id_, request);
      });
}

// 处理断开连接请求
void TcpSession::HandleRequestQuit() {
  CloseSession(SessionCloseReason::kClientRequest);
}
