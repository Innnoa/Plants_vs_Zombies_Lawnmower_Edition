#include "game/managers/room_manager.hpp"

#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

#include "network/tcp/tcp_session.hpp"

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
    room.max_players = request.max_players() == 0 ? 4 : request.max_players();
    room.is_playing = false;

    // 玩家玩家基本信息
    RoomPlayer host;
    host.player_id = player_id;
    host.player_name = player_name.empty()
                           ? ("玩家" + std::to_string(player_id))
                           : player_name;
    host.is_ready = false;
    host.is_host = true;
    host.session = std::move(session);
    room.players.push_back(host);  // players容器

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
    player.player_name = player_name.empty()
                             ? ("玩家" + std::to_string(player_id))
                             : player_name;
    player.is_ready = false;
    player.is_host = false;
    player.session = std::move(session);

    // 添加/补充对应结构
    room.players.push_back(std::move(player));
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

// 设置玩家准备状态
lawnmower::S2C_SetReadyResult RoomManager::SetReady(
    uint32_t player_id, const lawnmower::C2S_SetReady& request) {
  lawnmower::S2C_SetReadyResult result;  // 准备状态反馈
  RoomUpdate update;
  bool need_broadcast = false;  // 设定是否需要广播变量

  {
    std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁
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
    auto player_it = std::find_if(room.players.begin(), room.players.end(),
                                  [player_id](const RoomPlayer& player) {
                                    return player.player_id == player_id;
                                  });
    if (player_it == room.players.end()) {
      player_room_.erase(mapping);
      result.set_success(false);
      result.set_message_ready("玩家未在房间");
      return result;
    }

    if (room.is_playing) {
      result.set_success(false);
      result.set_room_id(room.room_id);
      result.set_is_ready(player_it->is_ready);
      result.set_message_ready("游戏中无法切换准备状态");
      return result;
    }

    player_it->is_ready = request.is_ready();
    result.set_success(true);
    result.set_room_id(room.room_id);
    result.set_is_ready(player_it->is_ready);
    result.set_message_ready(player_it->is_ready ? "已准备" : "已取消准备");

    update = BuildRoomUpdateLocked(room);  // 房间信息变化
    need_broadcast = true;                 // 需要广播
  }

  if (need_broadcast) {
    SendRoomUpdate(update);  // 广播更新后的房间信息
    spdlog::info("玩家 {} {}房间 {}", player_id,
                 request.is_ready() ? "准备" : "取消准备",
                 update.message.room_id());
  }

  return result;
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

  const auto requester_it =
      std::find_if(room.players.begin(), room.players.end(),
                   [player_id](const RoomPlayer& player) {
                     return player.player_id == player_id;
                   });
  if (requester_it == room.players.end()) {
    player_room_.erase(mapping);
    result->set_success(false);
    result->set_message_start("玩家未在房间中");
    return std::nullopt;
  }

  if (!requester_it->is_host) {
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

// 更新房间基本信息
RoomManager::RoomUpdate RoomManager::BuildRoomUpdateLocked(
    const Room& room) const {
  RoomUpdate update;
  update.message.set_room_id(room.room_id);  // room_id 不变
  for (const auto& player : room.players) {  // 提取Room中player容器中的各player
    auto* info = update.message.add_players();  // 新增一个玩家消息并获取指针
    info->set_player_id(player.player_id);      // 设置id
    info->set_player_name(player.player_name);  // 设置name
    info->set_is_ready(player.is_ready);        // 设置准备状态
    info->set_is_host(player.is_host);          // 设置是否为房主
    update.targets.push_back(player.session);  // 保存player的RoomPlayer会话状态
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

// 从房间数据结构中移除玩家并可选生成更新
bool RoomManager::DetachPlayerLocked(uint32_t player_id, RoomUpdate* update) {
  auto mapping =
      player_room_.find(player_id);  // 通过player_id 查找 room_id 是否存在
  if (mapping == player_room_.end()) {
    return false;  // 玩家不在房间中，不发生变化，返回false
  }

  auto room_it = rooms_.find(mapping->second);  // 通过room_id 查找其基本信息
  if (room_it == rooms_.end()) {                // 该room不存在基本信息
    player_room_.erase(mapping);                // 删除该玩家对应房间
    return false;  // 房间不存在信息，不发生变化，返回false
  }

  Room& room = room_it->second;               // 房间基本信息
  const auto old_size = room.players.size();  // 旧大小
  // 删除该房间内的玩家并调整players容器大小
  room.players.erase(std::remove_if(room.players.begin(), room.players.end(),
                                    [player_id](const RoomPlayer& p) {
                                      return p.player_id == player_id;
                                    }),
                     room.players.end());
  // 判断是否有玩家被删除，房间大小是否变化
  const bool removed = old_size != room.players.size();
  if (!removed) {
    return false;  // 没有被删除，不发生变化，返回false
  }

  // 如果发生变化，则移除该玩家对应房间的索引，说明该玩家已被移除
  player_room_.erase(mapping);

  if (room.players.empty()) {  // 如果此时的房间信息中人数为0
    rooms_.erase(room_it);     // 说明这个房间为空，移除这个房间
    return true;               // 房间人数为空，房间被删除，发生变化,返回true
  }

  // 确认确实删掉玩家
  if (old_size != room.players.size()) {
    EnsureHost(room);  // 确保存在房主,因为有可能是房主退出房间
    if (update) {      // update存在
      *update = BuildRoomUpdateLocked(room);  // 更新room_update
    }
  }
  return true;  // 房间人数不为空并且人数发生变化，发生变化，返回true
}

// 确保存在房主
void RoomManager::EnsureHost(Room& room) {
  // 检查是否有房主，直到返回true,否则返回false
  const bool has_host =
      std::any_of(room.players.begin(), room.players.end(),
                  [](const RoomPlayer& player) { return player.is_host; });
  // 如果room的玩家容器为非空并且不存在房主
  if (!room.players.empty() && !has_host) {
    room.players.front().is_host = true;  // 则令第一个玩家为房主
  }
}
