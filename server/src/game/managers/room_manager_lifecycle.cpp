#include <algorithm>
#include <spdlog/spdlog.h>

#include "game/managers/room_manager.hpp"

namespace {
std::string ResolvePlayerName(uint32_t player_id,
                              const std::string& player_name) {
  if (!player_name.empty()) {
    return player_name;
  }
  return "玩家" + std::to_string(player_id);
}
}  // namespace

lawnmower::S2C_CreateRoomResult RoomManager::CreateRoom(
    uint32_t player_id, const std::string& player_name,
    std::weak_ptr<TcpSession> session,
    const lawnmower::C2S_CreateRoom& request) {
  lawnmower::S2C_CreateRoomResult result;
  RoomUpdate update;
  bool need_broadcast = false;

  if (player_id == 0) {
    result.set_success(false);
    result.set_message_create("未登录，无法创建房间");
    return result;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (player_room_.count(player_id)) {
      result.set_success(false);
      result.set_message_create("请先离开当前房间");
      return result;
    }

    Room room;
    room.room_id = next_room_id_++;
    room.name = request.room_name().empty()
                    ? ("房间" + std::to_string(room.room_id))
                    : request.room_name();
    const uint32_t configured_max =
        config_.max_players_per_room > 0 ? config_.max_players_per_room : 4;
    if (request.max_players() == 0) {
      room.max_players = configured_max;
    } else {
      room.max_players = std::max<uint32_t>(
          1, std::min(request.max_players(), configured_max));
    }
    room.is_playing = false;

    RoomPlayer host;
    host.player_id = player_id;
    host.player_name = ResolvePlayerName(player_id, player_name);
    host.is_ready = false;
    host.is_host = true;
    host.session = std::move(session);
    room.players.push_back(host);
    room.player_index_by_id.emplace(host.player_id, room.players.size() - 1);

    auto [iter, inserted] = rooms_.emplace(room.room_id, std::move(room));
    player_room_[player_id] = iter->first;

    result.set_success(true);
    result.set_room_id(iter->first);
    result.set_message_create("房间创建成功");

    update = BuildRoomUpdateLocked(iter->second);
    need_broadcast = true;
  }

  if (need_broadcast) {
    SendRoomUpdate(update);
  }

  spdlog::info("玩家 {} 创建房间 {}", player_id, result.room_id());
  return result;
}

lawnmower::S2C_JoinRoomResult RoomManager::JoinRoom(
    uint32_t player_id, const std::string& player_name,
    std::weak_ptr<TcpSession> session, const lawnmower::C2S_JoinRoom& request) {
  lawnmower::S2C_JoinRoomResult result;
  RoomUpdate update;
  bool need_broadcast = false;

  if (player_id == 0) {
    result.set_success(false);
    result.set_message_join("请先登录");
    return result;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (player_room_.count(player_id)) {
      result.set_success(false);
      result.set_message_join("已在房间中");
      return result;
    }

    auto room_it = rooms_.find(request.room_id());
    if (room_it == rooms_.end()) {
      result.set_success(false);
      result.set_message_join("房间不存在");
      return result;
    }

    Room& room = room_it->second;
    if (room.is_playing) {
      result.set_success(false);
      result.set_message_join("房间已开始游戏");
      return result;
    }

    if (room.max_players > 0 && room.players.size() >= room.max_players) {
      result.set_success(false);
      result.set_message_join("房间已满");
      return result;
    }

    RoomPlayer player;
    player.player_id = player_id;
    player.player_name = ResolvePlayerName(player_id, player_name);
    player.is_ready = false;
    player.is_host = false;
    player.session = std::move(session);

    room.players.push_back(std::move(player));
    room.player_index_by_id.emplace(player_id, room.players.size() - 1);
    player_room_[player_id] = room.room_id;

    result.set_success(true);
    result.set_message_join("加入房间成功");

    update = BuildRoomUpdateLocked(room);
    need_broadcast = true;
  }

  if (need_broadcast) {
    SendRoomUpdate(update);
  }

  spdlog::info("玩家 {} 加入房间 {}", player_id, request.room_id());
  return result;
}

lawnmower::S2C_LeaveRoomResult RoomManager::LeaveRoom(uint32_t player_id) {
  lawnmower::S2C_LeaveRoomResult result;
  RoomUpdate update;
  bool need_broadcast = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!DetachPlayerLocked(player_id, &update)) {
      result.set_success(false);
      result.set_message_leave("玩家未在任何房间");
      return result;
    }

    result.set_success(true);
    result.set_message_leave("已离开房间");
    need_broadcast = !update.targets.empty();
  }

  if (need_broadcast) {
    SendRoomUpdate(update);
  }

  spdlog::info("玩家 {} 离开房间", player_id);
  return result;
}

lawnmower::S2C_RoomList RoomManager::GetRoomList() const {
  lawnmower::S2C_RoomList list;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& [room_id, room] : rooms_) {
    auto* info = list.add_rooms();
    info->set_room_id(room_id);
    info->set_room_name(room.name);
    info->set_current_players(static_cast<uint32_t>(room.players.size()));
    info->set_max_players(room.max_players);
    info->set_is_playing(room.is_playing);
    const auto host_it =
        std::find_if(room.players.begin(), room.players.end(),
                     [](const RoomPlayer& player) { return player.is_host; });
    if (host_it != room.players.end()) {
      info->set_host_name(host_it->player_name);
    }
  }
  return list;
}

void RoomManager::RemovePlayer(uint32_t player_id) {
  RoomUpdate update;
  bool need_broadcast = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    need_broadcast =
        DetachPlayerLocked(player_id, &update) && !update.targets.empty();
  }

  if (need_broadcast) {
    SendRoomUpdate(update);
  }
}
