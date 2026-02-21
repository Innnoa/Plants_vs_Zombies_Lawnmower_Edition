#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

#include "game/managers/room_manager.hpp"

lawnmower::S2C_SetReadyResult RoomManager::SetReady(
    uint32_t player_id, const lawnmower::C2S_SetReady& request) {
  lawnmower::S2C_SetReadyResult result;
  RoomUpdate update;
  bool need_broadcast = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto mapping = player_room_.find(player_id);
    if (mapping == player_room_.end()) {
      result.set_success(false);
      result.set_message_ready("玩家未在房间");
      return result;
    }

    auto room_it = rooms_.find(mapping->second);
    if (room_it == rooms_.end()) {
      player_room_.erase(mapping);
      result.set_success(false);
      result.set_message_ready("房间不存在");
      return result;
    }

    Room& room = room_it->second;
    RoomPlayer* player = FindRoomPlayerLocked(room, player_id);
    if (player == nullptr) {
      player_room_.erase(mapping);
      result.set_success(false);
      result.set_message_ready("玩家未在房间");
      return result;
    }

    if (room.is_playing) {
      result.set_success(false);
      result.set_room_id(room.room_id);
      result.set_is_ready(player->is_ready);
      result.set_message_ready("游戏中无法切换准备状态");
      return result;
    }

    player->is_ready = request.is_ready();
    result.set_success(true);
    result.set_room_id(room.room_id);
    result.set_is_ready(player->is_ready);
    result.set_message_ready(player->is_ready ? "已准备" : "已取消准备");

    update = BuildRoomUpdateLocked(room);
    need_broadcast = true;
  }

  if (need_broadcast) {
    SendRoomUpdate(update);
    spdlog::info("玩家 {} {}房间 {}", player_id,
                 request.is_ready() ? "准备" : "取消准备",
                 update.message.room_id());
  }

  return result;
}

std::optional<RoomManager::RoomSnapshot> RoomManager::TryStartGame(
    uint32_t player_id, lawnmower::S2C_GameStart* result) {
  if (result == nullptr) {
    return std::nullopt;
  }

  RoomSnapshot snapshot;

  std::lock_guard<std::mutex> lock(mutex_);
  auto mapping = player_room_.find(player_id);
  if (mapping == player_room_.end()) {
    result->set_success(false);
    result->set_message_start("玩家未在房间中");
    return std::nullopt;
  }

  auto room_it = rooms_.find(mapping->second);
  if (room_it == rooms_.end()) {
    player_room_.erase(mapping);
    result->set_success(false);
    result->set_message_start("房间不存在");
    return std::nullopt;
  }

  Room& room = room_it->second;
  result->set_room_id(room.room_id);

  const RoomPlayer* requester = FindRoomPlayerLocked(room, player_id);
  if (requester == nullptr) {
    player_room_.erase(mapping);
    result->set_success(false);
    result->set_message_start("玩家未在房间中");
    return std::nullopt;
  }

  if (!requester->is_host) {
    result->set_success(false);
    result->set_message_start("只有房主可以开始游戏");
    return std::nullopt;
  }

  if (room.is_playing) {
    result->set_success(false);
    result->set_message_start("房间已在游戏中");
    return std::nullopt;
  }

  const bool all_ready = std::all_of(room.players.begin(), room.players.end(),
                                     [](const RoomPlayer& player) {
                                       return player.is_host || player.is_ready;
                                     });
  if (!all_ready) {
    result->set_success(false);
    result->set_message_start("存在未准备的玩家");
    return std::nullopt;
  }

  room.is_playing = true;
  for (auto& player : room.players) {
    player.is_ready = false;
  }

  snapshot.room_id = room.room_id;
  snapshot.is_playing = room.is_playing;
  snapshot.players.reserve(room.players.size());
  for (const auto& player : room.players) {
    RoomPlayerSnapshot player_snapshot;
    player_snapshot.player_id = player.player_id;
    player_snapshot.player_name = player.player_name;
    player_snapshot.is_host = player.is_host;
    player_snapshot.session = player.session;
    snapshot.players.push_back(std::move(player_snapshot));
  }

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch());
  result->set_start_time(static_cast<uint64_t>(now_ms.count()));
  result->set_success(true);
  result->set_message_start("游戏开始");

  return snapshot;
}

bool RoomManager::FinishGame(uint32_t room_id) {
  RoomUpdate update;
  bool need_broadcast = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
      return false;
    }

    Room& room = room_it->second;
    if (!room.is_playing) {
      return true;
    }

    room.is_playing = false;
    update = BuildRoomUpdateLocked(room);
    need_broadcast = !update.targets.empty();
  }

  if (need_broadcast) {
    SendRoomUpdate(update);
  }

  spdlog::info("房间 {} 游戏结束，已重置 is_playing", room_id);
  return true;
}
