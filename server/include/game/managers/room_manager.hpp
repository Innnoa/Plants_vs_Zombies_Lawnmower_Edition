#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "message.pb.h"

class TcpSession;

// 房间管理器：负责创建/加入/离开房间以及广播房间成员变化
class RoomManager {
 public:
  static RoomManager& Instance();  // 单例模式，只有一个房间管理器

  RoomManager(const RoomManager&) = delete;
  RoomManager& operator=(const RoomManager&) = delete;

  lawnmower::S2C_CreateRoomResult CreateRoom(
      uint32_t player_id, const std::string& player_name,
      std::weak_ptr<TcpSession> session,
      const lawnmower::C2S_CreateRoom& request);

  lawnmower::S2C_JoinRoomResult JoinRoom(
      uint32_t player_id, const std::string& player_name,
      std::weak_ptr<TcpSession> session,
      const lawnmower::C2S_JoinRoom& request);

  lawnmower::S2C_LeaveRoomResult LeaveRoom(uint32_t player_id);
  lawnmower::S2C_RoomList GetRoomList() const;
  lawnmower::S2C_SetReadyResult SetReady(
      uint32_t player_id, const lawnmower::C2S_SetReady& request);

  struct RoomPlayerSnapshot {
    uint32_t player_id = 0;
    std::string player_name;
    bool is_host = false;
    std::weak_ptr<TcpSession> session;
  };

  struct RoomSnapshot {
    uint32_t room_id = 0;
    bool is_playing = false;
    std::vector<RoomPlayerSnapshot> players;
  };

  // 房主开始游戏：检查房间状态、准备状态并设置 is_playing
  // 成功返回房间快照（包含成员会话，用于后续广播）；失败返回 nullopt，同时填充
  // result
  std::optional<RoomSnapshot> TryStartGame(uint32_t player_id,
                                           lawnmower::S2C_GameStart* result);

  // 获取房间内所有成员会话（用于广播）
  std::vector<std::weak_ptr<TcpSession>> GetRoomSessions(
      uint32_t room_id) const;

  // 断线清理，不返回离开结果
  void RemovePlayer(uint32_t player_id);

 private:
  RoomManager() = default;

  struct RoomPlayer {
    uint32_t player_id = 0;
    std::string player_name;
    bool is_ready = false;
    bool is_host = false;
    std::weak_ptr<TcpSession> session;
  };

  struct Room {
    uint32_t room_id = 0;
    std::string name;
    uint32_t max_players = 0;
    bool is_playing = false;
    std::vector<RoomPlayer> players;
  };

  struct RoomUpdate {
    lawnmower::S2C_RoomUpdate message;  // 房间信息结构： room_id , player
    std::vector<std::weak_ptr<TcpSession>>
        targets;  // 保存房间里其他成员的weak_ptr供广播使用
  };

  RoomUpdate BuildRoomUpdateLocked(const Room& room) const;
  void SendRoomUpdate(const RoomUpdate& update);
  bool DetachPlayerLocked(uint32_t player_id, RoomUpdate* update);
  void EnsureHost(Room& room);

  mutable std::mutex mutex_;
  uint32_t next_room_id_ = 1;
  std::unordered_map<uint32_t, Room>
      rooms_;  // room_id - Room(room基本信息) （房间id对应房间信息)
  std::unordered_map<uint32_t, uint32_t>
      player_room_;  // player_id - room_id （玩家id对应房间id）
};
