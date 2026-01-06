#include "game/managers/game_manager.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <numbers>
#include <regex>
#include <span>
#include <spdlog/spdlog.h>
#include <string_view>

#include "network/tcp/tcp_session.hpp"
#include "network/udp/udp_server.hpp"

namespace {
constexpr float kSpawnRadius = 120.0f;
constexpr int32_t kDefaultMaxHealth = 100;
constexpr uint32_t kDefaultAttack = 10;
constexpr uint32_t kDefaultExpToNext = 100;
constexpr std::size_t kMaxPendingInputs = 64;
constexpr float kDirectionEpsilonSq = 1e-6f;
constexpr float kMaxDirectionLengthSq = 1.21f;    // 略放宽，防止浮点误差
constexpr uint32_t kFullSyncIntervalTicks = 300;  // ~5s @60Hz

// 计算朝向
float DegreesFromDirection(float x, float y) {
  if (std::abs(x) < 1e-6f && std::abs(y) < 1e-6f) {
    return 0.0f;
  }
  const float angle_rad = std::atan2(y, x);
  return angle_rad * 180.0f / std::numbers::pi_v<float>;
}

std::chrono::milliseconds NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch());
}

void FillSyncTiming(uint32_t room_id, uint64_t tick,
                    lawnmower::S2C_GameStateSync* sync) {
  if (sync == nullptr) {
    return;
  }

  const auto now_ms = NowMs();
  sync->set_room_id(room_id);
  sync->set_server_time_ms(static_cast<uint64_t>(now_ms.count()));

  auto* ts = sync->mutable_sync_time();
  ts->set_server_time(static_cast<uint64_t>(now_ms.count()));
  ts->set_tick(static_cast<uint32_t>(tick));
}

void SendSyncToSessions(std::span<const std::weak_ptr<TcpSession>> sessions,
                        const lawnmower::S2C_GameStateSync& sync) {
  for (const auto& weak_session : sessions) {
    if (auto session = weak_session.lock()) {
      session->SendProto(lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC, sync);
    }
  }
}
}  // namespace

// 单例构造
GameManager& GameManager::Instance() {
  static GameManager instance;
  return instance;
}

// 构建默认配置
GameManager::SceneConfig GameManager::BuildDefaultConfig() const {
  return LoadConfigFromFile();
}

void GameManager::SetIoContext(asio::io_context* io) { io_context_ = io; }

void GameManager::SetUdpServer(UdpServer* udp) { udp_server_ = udp; }

void GameManager::ScheduleGameTick(
    uint32_t room_id, std::chrono::microseconds interval,
    const std::shared_ptr<asio::steady_timer>& timer,
    double tick_interval_seconds) {
  if (!timer) {
    return;
  }

  timer->expires_after(interval);
  timer->async_wait([this, room_id, interval, timer,
                     tick_interval_seconds](const asio::error_code& ec) {
    if (ec == asio::error::operation_aborted) {
      return;
    }

    ProcessSceneTick(room_id, tick_interval_seconds);
    ScheduleGameTick(room_id, interval, timer, tick_interval_seconds);
  });
}

void GameManager::StartGameLoop(uint32_t room_id) {
  if (io_context_ == nullptr) {
    spdlog::warn("未设置 io_context，无法启动游戏循环");
    return;
  }

  std::shared_ptr<asio::steady_timer> timer;
  uint32_t tick_rate = 60;
  uint32_t state_sync_rate = 20;
  double tick_interval_seconds = 0.0;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      spdlog::warn("房间 {} 未找到场景，无法启动游戏循环", room_id);
      return;
    }

    Scene& scene = scene_it->second;
    tick_rate = std::max<uint32_t>(1, scene.config.tick_rate);
    state_sync_rate = std::max<uint32_t>(1, scene.config.state_sync_rate);
    tick_interval_seconds = 1.0 / static_cast<double>(tick_rate);
    const auto tick_interval =
        std::chrono::duration<double>(tick_interval_seconds);
    scene.tick_interval = tick_interval;
    scene.sync_interval = std::chrono::duration<double>(
        1.0 / static_cast<double>(state_sync_rate));
    scene.full_sync_interval = std::chrono::duration<double>(
        tick_interval_seconds * kFullSyncIntervalTicks);

    if (scene.loop_timer) {
      scene.loop_timer->cancel();
    }
    timer = std::make_shared<asio::steady_timer>(*io_context_);
    scene.loop_timer = timer;
    scene.tick = 0;
    scene.sync_accumulator = 0.0;
    scene.full_sync_elapsed = 0.0;
    scene.last_tick_time = std::chrono::steady_clock::now();
  }

  const auto interval = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::duration<double>(tick_interval_seconds));
  ScheduleGameTick(room_id, interval, timer, tick_interval_seconds);
  spdlog::debug("房间 {} 启动游戏循环，tick_rate={}，state_sync_rate={}",
                room_id, tick_rate, state_sync_rate);
}

void GameManager::StopGameLoop(uint32_t room_id) {
  std::shared_ptr<asio::steady_timer> timer;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      return;
    }
    timer = scene_it->second.loop_timer;
    scene_it->second.loop_timer.reset();
  }

  if (timer) {
    timer->cancel();
  }
}

// 将坐标限制在地图边界内
lawnmower::Vector2 GameManager::ClampToMap(const SceneConfig& cfg, float x,
                                           float y) const {
  lawnmower::Vector2 pos;
  pos.set_x(std::clamp(x, 0.0f, static_cast<float>(cfg.width)));
  pos.set_y(std::clamp(y, 0.0f, static_cast<float>(cfg.height)));
  return pos;
}

// 放置玩家
void GameManager::PlacePlayers(const RoomManager::RoomSnapshot& snapshot,
                               Scene* scene) {
  if (scene == nullptr) {
    return;
  }

  const std::size_t count = snapshot.players.size();  // 玩家数量
  if (count == 0) {
    return;
  }

  const float center_x =
      static_cast<float>(scene->config.width) * 0.5f;  // 计算中心x
  const float center_y =
      static_cast<float>(scene->config.height) * 0.5f;  // 计算中心y

  // 遍历每一个玩家
  for (std::size_t i = 0; i < count; ++i) {
    const auto& player = snapshot.players[i];
    const float angle =
        (2.0f * std::numbers::pi_v<float> * static_cast<float>(i)) /
        static_cast<float>(count);

    // 计算实际x/y的位置
    const float x = center_x + std::cos(angle) * kSpawnRadius;
    const float y = center_y + std::sin(angle) * kSpawnRadius;
    const auto clamped_pos = ClampToMap(scene->config, x, y);

    // 设置基本信息
    PlayerRuntime runtime;
    runtime.state.set_player_id(player.player_id);
    runtime.state.mutable_position()->set_x(clamped_pos.x());
    runtime.state.mutable_position()->set_y(clamped_pos.y());
    runtime.state.set_rotation(angle * 180.0f / std::numbers::pi_v<float>);
    runtime.state.set_health(kDefaultMaxHealth);
    runtime.state.set_max_health(kDefaultMaxHealth);
    runtime.state.set_level(1);
    runtime.state.set_exp(0);
    runtime.state.set_exp_to_next(kDefaultExpToNext);
    runtime.state.set_is_alive(true);
    runtime.state.set_attack(kDefaultAttack);
    runtime.state.set_is_friendly(true);
    runtime.state.set_role_id(0);
    runtime.state.set_critical_hit_rate(0);
    runtime.state.set_has_buff(false);
    runtime.state.set_buff_id(0);
    runtime.state.set_attack_speed(1);
    runtime.state.set_move_speed(scene->config.move_speed);
    runtime.state.set_last_processed_input_seq(0);

    // 将玩家对应玩家信息插入会话
    scene->players.emplace(player.player_id, std::move(runtime));
    player_scene_[player.player_id] = snapshot.room_id;  // 增加玩家对应房间
  }
}

// 创建场景
lawnmower::SceneInfo GameManager::CreateScene(
    const RoomManager::RoomSnapshot& snapshot) {
  StopGameLoop(snapshot.room_id);            // 清理旧的同步定时器
  std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁

  // 清理旧场景（防止重复开始游戏导致映射残留）
  auto existing = scenes_.find(snapshot.room_id);  // 房间对应会话map
  if (existing != scenes_.end()) {                 // 存在该会话
    for (const auto& [player_id, _] : existing->second.players) {
      player_scene_.erase(player_id);  // 玩家对应房间map
    }
    scenes_.erase(existing);  // 删除该会话
  }

  Scene scene;
  scene.config = BuildDefaultConfig();           // 构建默认配置
  PlacePlayers(snapshot, &scene);                // 放置玩家？
  scenes_[snapshot.room_id] = std::move(scene);  // 房间对应会话map

  lawnmower::SceneInfo scene_info;  // 场景信息
  // 设置必要信息
  scene_info.set_scene_id(snapshot.room_id);
  scene_info.set_width(scenes_[snapshot.room_id].config.width);
  scene_info.set_height(scenes_[snapshot.room_id].config.height);
  scene_info.set_tick_rate(scenes_[snapshot.room_id].config.tick_rate);
  scene_info.set_state_sync_rate(
      scenes_[snapshot.room_id].config.state_sync_rate);

  spdlog::info("创建场景: room_id={}, players={}", snapshot.room_id,
               snapshot.players.size());
  return scene_info;
}

// 构建完整的游戏状态
bool GameManager::BuildFullState(uint32_t room_id,
                                 lawnmower::S2C_GameStateSync* sync) {
  if (sync == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);     // 互斥锁
  const auto scene_it = scenes_.find(room_id);  // 房间对应会话map
  if (scene_it == scenes_.end()) {
    return false;
  }

  sync->Clear();
  FillSyncTiming(room_id, scene_it->second.tick, sync);

  const Scene& scene = scene_it->second;
  for (const auto& [_, runtime] : scene.players) {
    auto* player_state = sync->add_players();
    *player_state = runtime.state;
    player_state->set_last_processed_input_seq(runtime.last_input_seq);
  }
  return true;
}

namespace {
constexpr double kMaxTickDeltaSeconds = 0.1;  // clamp 极端卡顿
constexpr double kMaxInputDeltaSeconds = 0.1;
}  // namespace

void GameManager::ProcessSceneTick(uint32_t room_id,
                                   double tick_interval_seconds) {
  lawnmower::S2C_GameStateSync sync;
  bool force_full_sync = false;
  bool should_sync = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      return;
    }

    Scene& scene = scene_it->second;

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = scene.last_tick_time.time_since_epoch().count() == 0
                             ? scene.tick_interval
                             : now - scene.last_tick_time;
    scene.last_tick_time = now;

    const double elapsed_seconds =
        std::clamp(std::chrono::duration<double>(elapsed).count(), 0.0,
                   kMaxTickDeltaSeconds);
    const double dt_seconds =
        elapsed_seconds > 0.0 ? elapsed_seconds : tick_interval_seconds;

    bool has_dirty = false;

    for (auto& [_, runtime] : scene.players) {
      bool moved = false;
      bool consumed_input = false;
      double processed_seconds = 0.0;

      // 消耗输入队列，尽量在当前 tick 内吃掉完整的输入 delta（上限 kMaxTickDeltaSeconds）
      while (!runtime.pending_inputs.empty() &&
             processed_seconds < kMaxTickDeltaSeconds) {
        auto& input = runtime.pending_inputs.front();
        const float dx_raw = input.move_direction().x();
        const float dy_raw = input.move_direction().y();
        const float len_sq = dx_raw * dx_raw + dy_raw * dy_raw;

        const double reported_dt =
            input.delta_ms() > 0
                ? std::clamp(input.delta_ms() / 1000.0, 0.0,
                             kMaxInputDeltaSeconds)
                : tick_interval_seconds;
        const double remaining_budget =
            kMaxTickDeltaSeconds - processed_seconds;
        const double input_dt = std::min(reported_dt, remaining_budget);

        if (len_sq >= kDirectionEpsilonSq && len_sq <= kMaxDirectionLengthSq &&
            input_dt > 0.0) {
          const float len = std::sqrt(len_sq);
          const float dx = dx_raw / len;
          const float dy = dy_raw / len;

          const float speed = runtime.state.move_speed() > 0.0f
                                  ? runtime.state.move_speed()
                                  : scene.config.move_speed;

          auto* position = runtime.state.mutable_position();
          const auto new_pos = ClampToMap(
              scene.config,
              position->x() + dx * speed * static_cast<float>(input_dt),
              position->y() + dy * speed * static_cast<float>(input_dt));
          const float new_x = new_pos.x();
          const float new_y = new_pos.y();

          if (std::abs(new_x - position->x()) > 1e-4f ||
              std::abs(new_y - position->y()) > 1e-4f) {
            moved = true;
          }

          position->set_x(new_x);
          position->set_y(new_y);
          runtime.state.set_rotation(DegreesFromDirection(dx, dy));
          processed_seconds += input_dt;
          consumed_input = true;
        } else {
          // 无效方向也要前进时间，防止队列阻塞
          processed_seconds += input_dt;
          consumed_input = true;
        }

        // 更新序号（即便被拆分）
        if (input.input_seq() > runtime.last_input_seq) {
          runtime.last_input_seq = input.input_seq();
        }

        const double remaining_dt = reported_dt - input_dt;
        if (remaining_dt > 1e-5) {
          // 当前 tick 只消耗了一部分，保留剩余 delta_ms 在队首
          const uint32_t remaining_ms = static_cast<uint32_t>(
              std::clamp(std::llround(remaining_dt * 1000.0), 1LL,
                         static_cast<long long>(kMaxInputDeltaSeconds *
                                                 1000.0)));
          input.set_delta_ms(remaining_ms);
          break;
        } else {
          runtime.pending_inputs.pop_front();
        }
      }

      if (moved || consumed_input) {
        runtime.dirty = true;
      }
      has_dirty = has_dirty || runtime.dirty;
    }

    scene.tick += 1;
    scene.sync_accumulator += dt_seconds;
    scene.full_sync_elapsed += dt_seconds;

    const double sync_interval = scene.sync_interval.count() > 0.0
                                     ? scene.sync_interval.count()
                                     : tick_interval_seconds;

    while (scene.sync_accumulator >= sync_interval) {
      scene.sync_accumulator -= sync_interval;
      should_sync = true;
    }

    const double full_sync_interval_seconds =
        scene.full_sync_interval.count() > 0.0
            ? scene.full_sync_interval.count()
            : tick_interval_seconds *
                  static_cast<double>(kFullSyncIntervalTicks);

    force_full_sync = full_sync_interval_seconds > 0.0 &&
                      scene.full_sync_elapsed >= full_sync_interval_seconds;

    if (!should_sync && !force_full_sync) {
      return;
    }

    if (!force_full_sync && !has_dirty) {
      return;
    }

    FillSyncTiming(room_id, scene.tick, &sync);

    if (force_full_sync) {
      for (auto& [_, runtime] : scene.players) {
        runtime.state.set_last_processed_input_seq(runtime.last_input_seq);
        *sync.add_players() = runtime.state;
        runtime.dirty = false;
      }
      scene.full_sync_elapsed = 0.0;
    } else {
      for (auto& [_, runtime] : scene.players) {
        if (!runtime.dirty) {
          continue;
        }
        runtime.state.set_last_processed_input_seq(runtime.last_input_seq);
        *sync.add_players() = runtime.state;
        runtime.dirty = false;
      }
    }
  }

  if (sync.players_size() == 0) {
    return;
  }

  const auto sessions = RoomManager::Instance().GetRoomSessions(room_id);
  SendSyncToSessions(sessions, sync);
  if (udp_server_ != nullptr) {
    udp_server_->BroadcastState(room_id, sync);
  }
}

// 操纵玩家输入：只入队，逻辑帧内处理
bool GameManager::HandlePlayerInput(uint32_t player_id,
                                    const lawnmower::C2S_PlayerInput& input,
                                    uint32_t* room_id) {
  if (room_id == nullptr) {
    spdlog::debug("HandlePlayerInput: room_id 指针为空");
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);            // 互斥锁
  const auto mapping = player_scene_.find(player_id);  // 玩家对应房间map
  if (mapping == player_scene_.end()) {
    spdlog::debug("HandlePlayerInput: player {} 未映射到任何场景", player_id);
    return false;
  }

  const uint32_t target_room_id = mapping->second;  // 房间对应会话map
  auto scene_it = scenes_.find(target_room_id);
  if (scene_it == scenes_.end()) {
    player_scene_.erase(mapping);
    spdlog::debug("HandlePlayerInput: room {} 未找到场景，移除 player {} 映射",
                  target_room_id, player_id);
    return false;
  }

  Scene& scene = scene_it->second;
  auto player_it = scene.players.find(player_id);  // 会话对应玩家map
  if (player_it == scene.players.end()) {
    player_scene_.erase(mapping);
    spdlog::debug("HandlePlayerInput: player {} 不在场景玩家列表，移除映射",
                  player_id);
    return false;
  }

  PlayerRuntime& runtime = player_it->second;

  const uint32_t seq = input.input_seq();  // 输入序号（客户端递增）
  if (seq != 0 && seq <= runtime.last_input_seq) {
    spdlog::debug("HandlePlayerInput: player {} 输入序号回退 seq={} last={}",
                  player_id, seq, runtime.last_input_seq);
    return false;
  }

  const float dx_raw = input.move_direction().x();  // 获取x轴向量
  const float dy_raw = input.move_direction().y();  // 获取y轴向量
  const float len_sq = dx_raw * dx_raw + dy_raw * dy_raw;
  if (len_sq < kDirectionEpsilonSq) {
    // 零向量视作“无移动”，仅更新序号防止排队阻塞
    runtime.last_input_seq =
        std::max(runtime.last_input_seq, input.input_seq());
    return true;
  }
  if (len_sq > kMaxDirectionLengthSq) {
    spdlog::debug("HandlePlayerInput: player {} 方向过大 len_sq={}", player_id,
                  len_sq);
    return false;
  }

  if (runtime.pending_inputs.size() >= kMaxPendingInputs) {
    runtime.pending_inputs.pop_front();  // 丢弃最旧输入，防止队列过长
  }

  runtime.pending_inputs.push_back(input);
  *room_id = target_room_id;
  return true;
}

bool GameManager::IsInsideMap(uint32_t room_id,
                              const lawnmower::Vector2& position) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto scene_it = scenes_.find(room_id);
  if (scene_it == scenes_.end()) {
    return false;
  }

  const SceneConfig& cfg = scene_it->second.config;
  const float x = position.x();
  const float y = position.y();

  return x >= 0.0f && x <= static_cast<float>(cfg.width) && y >= 0.0f &&
         y <= static_cast<float>(cfg.height);
}

// 移除玩家
void GameManager::RemovePlayer(uint32_t player_id) {
  bool scene_removed = false;
  uint32_t room_id = 0;
  std::shared_ptr<asio::steady_timer> timer;
  {
    std::lock_guard<std::mutex> lock(mutex_);            // 互斥锁
    const auto mapping = player_scene_.find(player_id);  // 玩家对应房间map
    if (mapping == player_scene_.end()) {                // 没找到
      return;
    }

    room_id = mapping->second;     // 获取房间id
    player_scene_.erase(mapping);  // 移除玩家对应的房间id

    auto scene_it = scenes_.find(room_id);  // 房间对应会话map
    if (scene_it == scenes_.end()) {        // 没找到
      return;
    }

    scene_it->second.players.erase(player_id);  // 移除玩家对应会话中的玩家信息
    if (scene_it->second.players.empty()) {     // 会话中玩家数量为0
      timer = scene_it->second.loop_timer;
      scenes_.erase(scene_it);  // 移除该会话
      scene_removed = true;
    }
  }

  if (scene_removed) {
    if (timer) {
      timer->cancel();
    }
  }
}
GameManager::SceneConfig GameManager::LoadConfigFromFile() const {
  SceneConfig cfg;
  constexpr std::array<std::string_view, 3> kConfigPaths = {
      "config/server_config.json", "../config/server_config.json",
      "server/config/server_config.json"};

  std::ifstream file;
  for (const auto path : kConfigPaths) {
    file = std::ifstream(std::string(path));
    if (file.is_open()) {
      break;
    }
  }
  if (!file.is_open()) {
    return cfg;
  }

  const std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

  const auto extract_uint = [&content](std::string_view key, uint32_t* out) {
    std::regex re(std::string("\"") + std::string(key) + "\"\\s*:\\s*(\\d+)");
    std::smatch match;
    if (std::regex_search(content, match, re) && match.size() > 1) {
      try {
        *out = static_cast<uint32_t>(std::stoul(match[1].str()));
      } catch (...) {
      }
    }
  };

  extract_uint("map_width", &cfg.width);
  extract_uint("map_height", &cfg.height);
  extract_uint("tick_rate", &cfg.tick_rate);
  extract_uint("state_sync_rate", &cfg.state_sync_rate);

  std::regex speed_re("\"move_speed\"\\s*:\\s*(\\d+\\.?\\d*)");
  std::smatch speed_match;
  if (std::regex_search(content, speed_match, speed_re) &&
      speed_match.size() > 1) {
    try {
      cfg.move_speed = std::stof(speed_match[1].str());
    } catch (...) {
    }
  }

  return cfg;
}
