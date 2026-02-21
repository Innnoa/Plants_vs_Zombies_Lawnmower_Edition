#include <algorithm>
#include <cstddef>

#include "game/managers/room_manager.hpp"
#include "network/tcp/tcp_session.hpp"

RoomManager::RoomPlayer* RoomManager::FindRoomPlayerLocked(Room& room,
                                                           uint32_t player_id) {
  const auto it = room.player_index_by_id.find(player_id);
  if (it == room.player_index_by_id.end()) {
    return nullptr;
  }
  const std::size_t index = it->second;
  if (index >= room.players.size()) {
    return nullptr;
  }
  RoomPlayer& player = room.players[index];
  return player.player_id == player_id ? &player : nullptr;
}

const RoomManager::RoomPlayer* RoomManager::FindRoomPlayerLocked(
    const Room& room, uint32_t player_id) const {
  const auto it = room.player_index_by_id.find(player_id);
  if (it == room.player_index_by_id.end()) {
    return nullptr;
  }
  const std::size_t index = it->second;
  if (index >= room.players.size()) {
    return nullptr;
  }
  const RoomPlayer& player = room.players[index];
  return player.player_id == player_id ? &player : nullptr;
}

bool RoomManager::MarkPlayerDisconnected(uint32_t player_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto mapping = player_room_.find(player_id);
  if (mapping == player_room_.end()) {
    return false;
  }

  auto room_it = rooms_.find(mapping->second);
  if (room_it == rooms_.end()) {
    player_room_.erase(mapping);
    return false;
  }

  Room& room = room_it->second;
  RoomPlayer* player = FindRoomPlayerLocked(room, player_id);
  if (player == nullptr) {
    return false;
  }

  player->session.reset();
  return true;
}

bool RoomManager::AttachSession(uint32_t player_id, uint32_t room_id,
                                std::weak_ptr<TcpSession> session,
                                bool* out_is_playing,
                                std::string* out_player_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto mapping = player_room_.find(player_id);
  if (mapping == player_room_.end()) {
    return false;
  }
  if (room_id != 0 && mapping->second != room_id) {
    return false;
  }

  auto room_it = rooms_.find(mapping->second);
  if (room_it == rooms_.end()) {
    player_room_.erase(mapping);
    return false;
  }

  Room& room = room_it->second;
  RoomPlayer* player = FindRoomPlayerLocked(room, player_id);
  if (player == nullptr) {
    return false;
  }

  player->session = std::move(session);
  if (out_is_playing != nullptr) {
    *out_is_playing = room.is_playing;
  }
  if (out_player_name != nullptr) {
    *out_player_name = player->player_name;
  }
  return true;
}

RoomManager::RoomUpdate RoomManager::BuildRoomUpdateLocked(
    const Room& room) const {
  RoomUpdate update;
  update.message.set_room_id(room.room_id);
  for (const auto& player : room.players) {
    auto* info = update.message.add_players();
    info->set_player_id(player.player_id);
    info->set_player_name(player.player_name);
    info->set_is_ready(player.is_ready);
    info->set_is_host(player.is_host);
    update.targets.push_back(player.session);
  }
  return update;
}

void RoomManager::SendRoomUpdate(const RoomUpdate& update) {
  for (const auto& weak_session : update.targets) {
    if (auto session = weak_session.lock()) {
      session->SendProto(lawnmower::MessageType::MSG_S2C_ROOM_UPDATE,
                         update.message);
    }
  }
}

bool RoomManager::DetachPlayerLocked(uint32_t player_id, RoomUpdate* update) {
  auto mapping = player_room_.find(player_id);
  if (mapping == player_room_.end()) {
    return false;
  }

  auto room_it = rooms_.find(mapping->second);
  if (room_it == rooms_.end()) {
    player_room_.erase(mapping);
    return false;
  }

  Room& room = room_it->second;
  const auto old_size = room.players.size();
  auto index_it = room.player_index_by_id.find(player_id);
  if (index_it == room.player_index_by_id.end()) {
    return false;
  }
  std::size_t remove_index = index_it->second;
  if (remove_index >= room.players.size() ||
      room.players[remove_index].player_id != player_id) {
    return false;
  }

  room.players.erase(room.players.begin() +
                     static_cast<std::ptrdiff_t>(remove_index));
  room.player_index_by_id.erase(index_it);
  for (std::size_t index = remove_index; index < room.players.size(); ++index) {
    room.player_index_by_id[room.players[index].player_id] = index;
  }

  player_room_.erase(mapping);

  if (room.players.empty()) {
    rooms_.erase(room_it);
    return true;
  }

  if (old_size != room.players.size()) {
    EnsureHost(room);
    if (update != nullptr) {
      *update = BuildRoomUpdateLocked(room);
    }
  }
  return true;
}

void RoomManager::EnsureHost(Room& room) {
  const bool has_host =
      std::any_of(room.players.begin(), room.players.end(),
                  [](const RoomPlayer& player) { return player.is_host; });
  if (!room.players.empty() && !has_host) {
    room.players.front().is_host = true;
  }
}
