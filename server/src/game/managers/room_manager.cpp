#include "game/managers/room_manager.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "network/tcp/tcp_session.hpp"

namespace {
std::string ResolvePlayerName(uint32_t player_id,
                              const std::string& player_name) {
  if (!player_name.empty()) {
    return player_name;
  }
  return "玩家" + std::to_string(player_id);
}
}  // namespace

RoomManager& RoomManager::Instance() {  // 单例房间管理器
  static RoomManager instance;
  return instance;
}

lawnmower::S2C_CreateRoomResult RoomManager::CreateRoom(  // 创建房间
    uint32_t player_id, const std::string& player_name,
    std::weak_ptr<TcpSession> session,
    const lawnmower::C2S_CreateRoom& request) {
  lawnmower::S2C_CreateRoomResult result;  // 创建房间反馈
  RoomUpdate update;
  bool need_broadcast = false;  // 设定是否需要广播变量

  if (player_id == 0) {
    result.set_success(false);
    result.set_message_create("未登录，无法创建房间");
    return result;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁
    if (player_room_.count(player_id)) {
      result.set_success(false);
      result.set_message_create("请先离开当前房间");
      return result;
    }

    // 房间基本信息
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

    // 玩家玩家基本信息
    RoomPlayer host;
    host.player_id = player_id;
    host.player_name = ResolvePlayerName(player_id, player_name);
    host.is_ready = false;
    host.is_host = true;
    host.session = std::move(session);
    room.players.push_back(host);  // players容器
    room.player_index_by_id.emplace(host.player_id, room.players.size() - 1);

    // 插入房间id-房间信息结构
    auto [iter, inserted] = rooms_.emplace(room.room_id, std::move(room));
    // 添加player_id-房间id结构
    player_room_[player_id] = iter->first;

    result.set_success(true);
    result.set_room_id(iter->first);
    result.set_message_create("房间创建成功");

    update =
        BuildRoomUpdateLocked(iter->second);  // 房间发生变化,更新房间基本信息
    need_broadcast = true;                    // 需要广播
  }

  if (need_broadcast) {
    SendRoomUpdate(update);
  }

  spdlog::info("玩家 {} 创建房间 {}", player_id, result.room_id());
  return result;
}

lawnmower::S2C_JoinRoomResult RoomManager::JoinRoom(  // 加入房间
    uint32_t player_id, const std::string& player_name,
    std::weak_ptr<TcpSession> session, const lawnmower::C2S_JoinRoom& request) {
  lawnmower::S2C_JoinRoomResult result;  // 加入房间反馈
  RoomUpdate update;
  bool need_broadcast = false;  // 设定是否需要广播变量

  if (player_id == 0) {
    result.set_success(false);
    result.set_message_join("请先登录");
    return result;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁
    if (player_room_.count(player_id)) {
      result.set_success(false);
      result.set_message_join("已在房间中");
      return result;
    }

    auto room_it = rooms_.find(request.room_id());  // 查找房间
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

    // 加入玩家基本信息
    RoomPlayer player;
    player.player_id = player_id;
    player.player_name = ResolvePlayerName(player_id, player_name);
    player.is_ready = false;
    player.is_host = false;
    player.session = std::move(session);

    // 添加/补充对应结构
    room.players.push_back(std::move(player));
    room.player_index_by_id.emplace(player_id, room.players.size() - 1);
    player_room_[player_id] = room.room_id;

    result.set_success(true);
    result.set_message_join("加入房间成功");

    update = BuildRoomUpdateLocked(room);  // 房间人数发生变化，更新房间基本信息
    need_broadcast = true;                 // 基本信息发生变化，需要广播
  }

  if (need_broadcast) {
    SendRoomUpdate(update);
  }

  spdlog::info("玩家 {} 加入房间 {}", player_id, request.room_id());
  return result;
}

lawnmower::S2C_LeaveRoomResult RoomManager::LeaveRoom(
    uint32_t player_id) {                 // 离开房间
  lawnmower::S2C_LeaveRoomResult result;  // 离开房间反馈
  RoomUpdate update;
  bool need_broadcast = false;  // 设定是否需要广播变量

  {
    std::lock_guard<std::mutex> lock(mutex_);       // 互斥锁
    if (!DetachPlayerLocked(player_id, &update)) {  // 检查玩家是否在房间中
      result.set_success(false);
      result.set_message_leave("玩家未在任何房间");
      return result;
    }

    result.set_success(true);
    result.set_message_leave("已离开房间");
    need_broadcast = !update.targets.empty();  // 是否还有成员需要通知
  }

  if (need_broadcast) {  // 房间会话存在
    SendRoomUpdate(update);
  }

  spdlog::info("玩家 {} 离开房间", player_id);
  return result;
}

// 获取房间列表
lawnmower::S2C_RoomList RoomManager::GetRoomList() const {
  lawnmower::S2C_RoomList list;
  std::lock_guard<std::mutex> lock(mutex_);     // 互斥锁
  for (const auto& [room_id, room] : rooms_) {  // 生成房间列表
    auto* info = list.add_rooms();
    info->set_room_id(room_id);
    info->set_room_name(room.name);
    info->set_current_players(static_cast<uint32_t>(room.players.size()));
    info->set_max_players(room.max_players);
    info->set_is_playing(room.is_playing);
    // 获取房主信息
    const auto host_it =
        std::find_if(room.players.begin(), room.players.end(),
                     [](const RoomPlayer& player) { return player.is_host; });
    if (host_it != room.players.end()) {
      info->set_host_name(host_it->player_name);
    }
  }
  return list;  // 返回房间列表
}

// 移除加入房间的用户,主要用于当用户主动与服务器断开连接而非主动退出房间
void RoomManager::RemovePlayer(uint32_t player_id) {
  RoomUpdate update;
  bool need_broadcast = false;  // 设定是否需要广播变量
  {
    std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁

    // 判断是否需要广播
    need_broadcast = DetachPlayerLocked(player_id, &update)  // 房间是否发生变化
                     && !update.targets.empty();  // 是否还有成员需要通知
  }

  if (need_broadcast) {
    SendRoomUpdate(update);  // 广播更新
  }
}

std::vector<std::weak_ptr<TcpSession>> RoomManager::GetRoomSessions(
    uint32_t room_id) const {
  std::vector<std::weak_ptr<TcpSession>> sessions;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto room_it = rooms_.find(room_id);
  if (room_it == rooms_.end()) {
    return sessions;
  }

  sessions.reserve(room_it->second.players.size());
  for (const auto& player : room_it->second.players) {
    sessions.push_back(player.session);
  }
  return sessions;
}

std::optional<uint32_t> RoomManager::GetPlayerRoom(uint32_t player_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) {
    return std::nullopt;
  }
  return it->second;
}
