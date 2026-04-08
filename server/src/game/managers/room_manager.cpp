#include "game/managers/room_manager.hpp"

RoomManager& RoomManager::Instance() {
  static RoomManager instance;
  return instance;
}

std::vector<std::weak_ptr<TcpSession>> RoomManager::GetRoomSessions(
    uint32_t room_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto room_it = rooms_.find(room_id);
  if (room_it == rooms_.end()) {
    return {};
  }
  return room_it->second.cached_sessions;
}

std::optional<uint32_t> RoomManager::GetPlayerRoom(uint32_t player_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) {
    return std::nullopt;
  }
  return it->second;
}
