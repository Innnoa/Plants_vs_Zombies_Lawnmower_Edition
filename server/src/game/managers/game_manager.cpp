#include "game/managers/game_manager.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>
#include <span>
#include <spdlog/spdlog.h>
#include <string_view>

#include "game/entities/enemy_types.hpp"
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
constexpr uint32_t kFullSyncIntervalTicks = 180;  // ~3s @60Hz

constexpr int kNavCellSize = 100;          // px
constexpr float kEnemySpawnInset = 10.0f;  // 避免精确落在边界导致 clamp 抖动
constexpr std::size_t kMaxEnemiesAlive = 256;
constexpr double kWaveIntervalSeconds = 15.0;
constexpr double kEnemySpawnBasePerSecond = 1.0;
constexpr double kEnemySpawnPerPlayerPerSecond = 0.75;
constexpr double kEnemySpawnWaveGrowthPerSecond = 0.2;
constexpr std::size_t kMaxEnemySpawnPerTick = 4;
constexpr double kEnemyReplanIntervalSeconds = 0.25;
constexpr float kEnemyWaypointReachRadius = 12.0f;
constexpr uint32_t kEnemySpawnForceSyncCount =
    6;  // 新刷怪多发几次，降低 UDP 丢包影响

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
      std::chrono::steady_clock::now().time_since_epoch());
}

uint32_t NextRng(uint32_t* state) {
  if (state == nullptr) {
    return 0;
  }
  // LCG: fast & deterministic for gameplay purposes.
  *state = (*state * 1664525u) + 1013904223u;
  return *state;
}

float NextRngUnitFloat(uint32_t* state) {
  const uint32_t r = NextRng(state);
  // Use high 24 bits to build [0,1) float.
  return static_cast<float>((r >> 8) & 0x00FFFFFF) * (1.0f / 16777216.0f);
}

struct NavGrid {
  int cells_x = 0;
  int cells_y = 0;
  int cell_size = kNavCellSize;
};

int ClampInt(int v, int lo, int hi) { return std::min(std::max(v, lo), hi); }

int ToIndex(const NavGrid& grid, int x, int y) { return y * grid.cells_x + x; }

std::pair<int, int> WorldToCell(const NavGrid& grid, float x, float y) {
  const int cx =
      ClampInt(static_cast<int>(x / static_cast<float>(grid.cell_size)), 0,
               std::max(0, grid.cells_x - 1));
  const int cy =
      ClampInt(static_cast<int>(y / static_cast<float>(grid.cell_size)), 0,
               std::max(0, grid.cells_y - 1));
  return {cx, cy};
}

std::pair<float, float> CellCenterWorld(const NavGrid& grid, int cx, int cy) {
  const float fx =
      (static_cast<float>(cx) + 0.5f) * static_cast<float>(grid.cell_size);
  const float fy =
      (static_cast<float>(cy) + 0.5f) * static_cast<float>(grid.cell_size);
  return {fx, fy};
}

float Heuristic(const std::pair<int, int>& a, const std::pair<int, int>& b) {
  const float dx = static_cast<float>(a.first - b.first);
  const float dy = static_cast<float>(a.second - b.second);
  return std::sqrt(dx * dx + dy * dy);
}

bool FindPathAstar(const NavGrid& grid, const std::pair<int, int>& start,
                   const std::pair<int, int>& goal,
                   std::vector<std::pair<int, int>>* out_path,
                   std::vector<int>& came_from, std::vector<float>& g_score,
                   std::vector<uint8_t>& closed) {
  if (out_path == nullptr) {
    return false;
  }
  out_path->clear();
  if (grid.cells_x <= 0 || grid.cells_y <= 0) {
    return false;
  }

  const int total = grid.cells_x * grid.cells_y;
  if (total <= 0) {
    return false;
  }

  came_from.assign(static_cast<std::size_t>(total), -1);
  g_score.assign(static_cast<std::size_t>(total),
                 std::numeric_limits<float>::infinity());
  closed.assign(static_cast<std::size_t>(total), 0);

  const int start_idx = ToIndex(grid, start.first, start.second);
  const int goal_idx = ToIndex(grid, goal.first, goal.second);
  if (start_idx < 0 || start_idx >= total || goal_idx < 0 ||
      goal_idx >= total) {
    return false;
  }

  struct OpenNode {
    int idx = 0;
    float f = 0.0f;
    float g = 0.0f;
  };
  auto cmp = [](const OpenNode& a, const OpenNode& b) { return a.f > b.f; };
  std::priority_queue<OpenNode, std::vector<OpenNode>, decltype(cmp)> open(cmp);

  g_score[static_cast<std::size_t>(start_idx)] = 0.0f;
  open.push(OpenNode{start_idx, Heuristic(start, goal), 0.0f});

  constexpr std::array<std::pair<int, int>, 8> kDirs = {{
      {1, 0},
      {-1, 0},
      {0, 1},
      {0, -1},
      {1, 1},
      {1, -1},
      {-1, 1},
      {-1, -1},
  }};

  bool found = false;
  while (!open.empty()) {
    const OpenNode cur = open.top();
    open.pop();
    if (cur.idx == goal_idx) {
      found = true;
      break;
    }
    if (closed[static_cast<std::size_t>(cur.idx)] != 0) {
      continue;
    }
    closed[static_cast<std::size_t>(cur.idx)] = 1;

    const int cx = cur.idx % grid.cells_x;
    const int cy = cur.idx / grid.cells_x;

    for (const auto& [dx, dy] : kDirs) {
      const int nx = cx + dx;
      const int ny = cy + dy;
      if (nx < 0 || ny < 0 || nx >= grid.cells_x || ny >= grid.cells_y) {
        continue;
      }
      const int nidx = ToIndex(grid, nx, ny);
      if (closed[static_cast<std::size_t>(nidx)] != 0) {
        continue;
      }

      const float step_cost =
          (dx == 0 || dy == 0) ? 1.0f : std::numbers::sqrt2_v<float>;
      const float tentative_g =
          g_score[static_cast<std::size_t>(cur.idx)] + step_cost;
      if (tentative_g < g_score[static_cast<std::size_t>(nidx)]) {
        came_from[static_cast<std::size_t>(nidx)] = cur.idx;
        g_score[static_cast<std::size_t>(nidx)] = tentative_g;
        const std::pair<int, int> ncell{nx, ny};
        const float f = tentative_g + Heuristic(ncell, goal);
        open.push(OpenNode{nidx, f, tentative_g});
      }
    }
  }

  if (!found) {
    return false;
  }

  // Reconstruct path (goal -> start).
  std::vector<int> rev;
  rev.reserve(32);
  int cur = goal_idx;
  while (cur >= 0 && cur < total) {
    rev.push_back(cur);
    if (cur == start_idx) {
      break;
    }
    cur = came_from[static_cast<std::size_t>(cur)];
  }

  if (rev.empty() || rev.back() != start_idx) {
    return false;
  }

  out_path->reserve(rev.size());
  for (auto it = rev.rbegin(); it != rev.rend(); ++it) {
    const int idx = *it;
    const int x = idx % grid.cells_x;
    const int y = idx / grid.cells_x;
    out_path->push_back({x, y});
  }
  return true;
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
  SceneConfig cfg;
  cfg.width = config_.map_width;
  cfg.height = config_.map_height;
  cfg.tick_rate = config_.tick_rate;
  cfg.state_sync_rate = config_.state_sync_rate;
  cfg.move_speed = config_.move_speed;
  return cfg;
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
  scene.config = BuildDefaultConfig();  // 构建默认配置
  scene.next_enemy_id = 1;
  scene.elapsed = 0.0;
  scene.spawn_elapsed = 0.0;
  scene.wave_id = 1;
  scene.rng_state = snapshot.room_id ^ static_cast<uint32_t>(NowMs().count());
  if (scene.rng_state == 0) {
    scene.rng_state = 1;
  }

  scene.nav_cells_x = std::max(
      1,
      static_cast<int>((scene.config.width + kNavCellSize - 1) / kNavCellSize));
  scene.nav_cells_y =
      std::max(1, static_cast<int>((scene.config.height + kNavCellSize - 1) /
                                   kNavCellSize));
  const std::size_t nav_cells =
      static_cast<std::size_t>(scene.nav_cells_x * scene.nav_cells_y);
  scene.nav_came_from.assign(nav_cells, -1);
  scene.nav_g_score.assign(nav_cells, 0.0f);
  scene.nav_closed.assign(nav_cells, 0);

  PlacePlayers(snapshot, &scene);  // 放置玩家？

  auto spawn_enemy = [&](uint32_t type_id) {
    if (scene.enemies.size() >= kMaxEnemiesAlive) {
      return;
    }
    const EnemyType* type = FindEnemyType(type_id);
    if (type == nullptr) {
      type = &DefaultEnemyType();
    }

    const float map_w = static_cast<float>(scene.config.width);
    const float map_h = static_cast<float>(scene.config.height);
    const float t = NextRngUnitFloat(&scene.rng_state);
    const uint32_t edge = NextRng(&scene.rng_state) % 4u;

    float x = 0.0f;
    float y = 0.0f;
    switch (edge) {
      case 0:  // left
        x = kEnemySpawnInset;
        y = t * map_h;
        break;
      case 1:  // right
        x = std::max(0.0f, map_w - kEnemySpawnInset);
        y = t * map_h;
        break;
      case 2:  // bottom
        x = t * map_w;
        y = kEnemySpawnInset;
        break;
      default:  // top
        x = t * map_w;
        y = std::max(0.0f, map_h - kEnemySpawnInset);
        break;
    }

    EnemyRuntime runtime;
    runtime.state.set_enemy_id(scene.next_enemy_id++);
    runtime.state.set_type_id(type->type_id);
    const auto clamped_pos = ClampToMap(scene.config, x, y);
    runtime.state.mutable_position()->set_x(clamped_pos.x());
    runtime.state.mutable_position()->set_y(clamped_pos.y());
    runtime.state.set_health(type->max_health);
    runtime.state.set_max_health(type->max_health);
    runtime.state.set_is_alive(true);
    runtime.state.set_wave_id(scene.wave_id);
    runtime.state.set_is_friendly(false);
    runtime.force_sync_left = kEnemySpawnForceSyncCount;
    runtime.dirty = true;
    scene.enemies.emplace(runtime.state.enemy_id(), std::move(runtime));
  };

  const std::size_t initial_enemy_count =
      std::max<std::size_t>(1, snapshot.players.size() * 2);
  for (std::size_t i = 0; i < initial_enemy_count; ++i) {
    const std::size_t idx =
        static_cast<std::size_t>(NextRng(&scene.rng_state)) %
        kEnemyTypes.size();
    spawn_enemy(kEnemyTypes[idx].type_id);
  }
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
  for (const auto& [_, runtime] : scene.enemies) {
    auto* enemy_state = sync->add_enemies();
    *enemy_state = runtime.state;
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
    auto fill_player_high_freq = [](const PlayerRuntime& runtime,
                                    lawnmower::PlayerState* out) {
      if (out == nullptr) {
        return;
      }
      out->Clear();
      out->set_player_id(runtime.state.player_id());
      *out->mutable_position() = runtime.state.position();
      out->set_rotation(runtime.state.rotation());
      out->set_is_alive(runtime.state.is_alive());
      out->set_last_processed_input_seq(runtime.last_input_seq);
    };

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

      // 消耗输入队列，尽量在当前 tick 内吃掉完整的输入 delta（上限
      // kMaxTickDeltaSeconds）
      while (!runtime.pending_inputs.empty() &&
             processed_seconds < kMaxTickDeltaSeconds) {
        auto& input = runtime.pending_inputs.front();
        const float dx_raw = input.move_direction().x();
        const float dy_raw = input.move_direction().y();
        const float len_sq = dx_raw * dx_raw + dy_raw * dy_raw;

        const double reported_dt = input.delta_ms() > 0
                                       ? std::clamp(input.delta_ms() / 1000.0,
                                                    0.0, kMaxInputDeltaSeconds)
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
          const uint32_t remaining_ms = static_cast<uint32_t>(std::clamp(
              std::llround(remaining_dt * 1000.0), 1LL,
              static_cast<long long>(kMaxInputDeltaSeconds * 1000.0)));
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

    scene.elapsed += dt_seconds;
    scene.wave_id = std::max<uint32_t>(
        1, 1u + static_cast<uint32_t>(scene.elapsed / kWaveIntervalSeconds));

    const std::size_t alive_players = std::count_if(
        scene.players.begin(), scene.players.end(),
        [](const auto& kv) { return kv.second.state.is_alive(); });

    auto spawn_enemy = [&](uint32_t type_id) {
      if (scene.enemies.size() >= kMaxEnemiesAlive) {
        return false;
      }
      if (alive_players == 0) {
        return false;
      }

      const EnemyType* type = FindEnemyType(type_id);
      if (type == nullptr) {
        type = &DefaultEnemyType();
      }

      const float map_w = static_cast<float>(scene.config.width);
      const float map_h = static_cast<float>(scene.config.height);
      const float t = NextRngUnitFloat(&scene.rng_state);
      const uint32_t edge = NextRng(&scene.rng_state) % 4u;

      float x = 0.0f;
      float y = 0.0f;
      switch (edge) {
        case 0:
          x = kEnemySpawnInset;
          y = t * map_h;
          break;
        case 1:
          x = std::max(0.0f, map_w - kEnemySpawnInset);
          y = t * map_h;
          break;
        case 2:
          x = t * map_w;
          y = kEnemySpawnInset;
          break;
        default:
          x = t * map_w;
          y = std::max(0.0f, map_h - kEnemySpawnInset);
          break;
      }

      EnemyRuntime runtime;
      runtime.state.set_enemy_id(scene.next_enemy_id++);
      runtime.state.set_type_id(type->type_id);
      const auto clamped_pos = ClampToMap(scene.config, x, y);
      runtime.state.mutable_position()->set_x(clamped_pos.x());
      runtime.state.mutable_position()->set_y(clamped_pos.y());
      runtime.state.set_health(type->max_health);
      runtime.state.set_max_health(type->max_health);
      runtime.state.set_is_alive(true);
      runtime.state.set_wave_id(scene.wave_id);
      runtime.state.set_is_friendly(false);
      runtime.force_sync_left = kEnemySpawnForceSyncCount;
      runtime.dirty = true;
      scene.enemies.emplace(runtime.state.enemy_id(), std::move(runtime));
      return true;
    };

    if (alive_players > 0) {
      const double wave_boost =
          static_cast<double>(scene.wave_id > 0 ? scene.wave_id - 1 : 0);
      const double spawn_rate =
          std::clamp(kEnemySpawnBasePerSecond +
                         kEnemySpawnPerPlayerPerSecond *
                             static_cast<double>(alive_players) +
                         kEnemySpawnWaveGrowthPerSecond * wave_boost,
                     0.0, 30.0);
      const double spawn_interval = spawn_rate > 1e-6 ? 1.0 / spawn_rate : 0.0;

      scene.spawn_elapsed += dt_seconds;
      std::size_t spawned = 0;
      while (spawn_interval > 0.0 && scene.spawn_elapsed >= spawn_interval &&
             scene.enemies.size() < kMaxEnemiesAlive &&
             spawned < kMaxEnemySpawnPerTick) {
        scene.spawn_elapsed -= spawn_interval;
        const std::size_t idx =
            static_cast<std::size_t>(NextRng(&scene.rng_state)) %
            kEnemyTypes.size();
        if (spawn_enemy(kEnemyTypes[idx].type_id)) {
          spawned += 1;
          has_dirty = true;
        } else {
          break;
        }
      }
    }

    const NavGrid nav{scene.nav_cells_x, scene.nav_cells_y, kNavCellSize};
    const float reach_sq =
        kEnemyWaypointReachRadius * kEnemyWaypointReachRadius;

    auto nearest_player_id = [&](float x, float y) -> uint32_t {
      uint32_t best_id = 0;
      float best_dist_sq = std::numeric_limits<float>::infinity();
      for (const auto& [pid, pr] : scene.players) {
        if (!pr.state.is_alive()) {
          continue;
        }
        const float dx = pr.state.position().x() - x;
        const float dy = pr.state.position().y() - y;
        const float dist_sq = dx * dx + dy * dy;
        if (dist_sq < best_dist_sq) {
          best_dist_sq = dist_sq;
          best_id = pid;
        }
      }
      return best_id;
    };

    for (auto& [_, enemy] : scene.enemies) {
      if (!enemy.state.is_alive()) {
        continue;
      }

      auto* pos = enemy.state.mutable_position();
      const float prev_x = pos->x();
      const float prev_y = pos->y();

      const uint32_t target_id = nearest_player_id(prev_x, prev_y);
      if (target_id == 0) {
        continue;
      }

      const auto target_it = scene.players.find(target_id);
      if (target_it == scene.players.end()) {
        continue;
      }

      const float target_x = target_it->second.state.position().x();
      const float target_y = target_it->second.state.position().y();

      const bool target_changed = (enemy.target_player_id != target_id);
      enemy.replan_elapsed += dt_seconds;

      if (target_changed ||
          enemy.replan_elapsed >= kEnemyReplanIntervalSeconds) {
        enemy.target_player_id = target_id;
        enemy.replan_elapsed = 0.0;

        const auto start_cell = WorldToCell(nav, prev_x, prev_y);
        const auto goal_cell = WorldToCell(nav, target_x, target_y);
        if (start_cell == goal_cell) {
          enemy.path.clear();
          enemy.path_index = 0;
        } else {
          std::vector<std::pair<int, int>> new_path;
          if (FindPathAstar(nav, start_cell, goal_cell, &new_path,
                            scene.nav_came_from, scene.nav_g_score,
                            scene.nav_closed) &&
              new_path.size() > 1) {
            enemy.path = std::move(new_path);
            enemy.path_index = 1;  // 跳过起点格
          } else {
            enemy.path.clear();
            enemy.path_index = 0;
          }
        }
      }

      auto select_goal = [&]() -> std::pair<float, float> {
        if (enemy.path_index < enemy.path.size()) {
          const auto [cx, cy] = enemy.path[enemy.path_index];
          const auto [wx, wy] = CellCenterWorld(nav, cx, cy);
          const auto clamped = ClampToMap(scene.config, wx, wy);
          return {clamped.x(), clamped.y()};
        }
        return {target_x, target_y};
      };

      std::pair<float, float> goal = select_goal();
      for (int iter = 0; iter < 4; ++iter) {
        const float dx = goal.first - prev_x;
        const float dy = goal.second - prev_y;
        const float dist_sq = dx * dx + dy * dy;
        if (enemy.path_index < enemy.path.size() && dist_sq <= reach_sq) {
          enemy.path_index += 1;
          goal = select_goal();
          continue;
        }
        break;
      }

      const float dx = goal.first - prev_x;
      const float dy = goal.second - prev_y;
      const float dist_sq = dx * dx + dy * dy;
      if (dist_sq > 1e-6f) {
        const float inv_len = 1.0f / std::sqrt(dist_sq);
        const float dir_x = dx * inv_len;
        const float dir_y = dy * inv_len;

        const EnemyType* type = FindEnemyType(enemy.state.type_id());
        if (type == nullptr) {
          type = &DefaultEnemyType();
        }
        const float speed = type->move_speed > 0.0f ? type->move_speed : 60.0f;

        const auto new_pos =
            ClampToMap(scene.config,
                       prev_x + dir_x * speed * static_cast<float>(dt_seconds),
                       prev_y + dir_y * speed * static_cast<float>(dt_seconds));
        const float new_x = new_pos.x();
        const float new_y = new_pos.y();
        if (std::abs(new_x - prev_x) > 1e-4f ||
            std::abs(new_y - prev_y) > 1e-4f) {
          pos->set_x(new_x);
          pos->set_y(new_y);
          enemy.dirty = true;
        }
      }

      if (enemy.dirty || enemy.force_sync_left > 0) {
        has_dirty = true;
      }
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
        fill_player_high_freq(runtime, sync.add_players());
        runtime.dirty = false;
      }
      for (auto& [_, enemy] : scene.enemies) {
        auto* out = sync.add_enemies();
        *out = enemy.state;
        enemy.dirty = false;
        if (enemy.force_sync_left > 0) {
          enemy.force_sync_left -= 1;
        }
      }
      scene.full_sync_elapsed = 0.0;
    } else {
      for (auto& [_, runtime] : scene.players) {
        if (!runtime.dirty) {
          continue;
        }
        fill_player_high_freq(runtime, sync.add_players());
        runtime.dirty = false;
      }
      for (auto& [_, enemy] : scene.enemies) {
        if (!enemy.dirty && enemy.force_sync_left == 0) {
          continue;
        }
        auto* out = sync.add_enemies();
        *out = enemy.state;
        enemy.dirty = false;
        if (enemy.force_sync_left > 0) {
          enemy.force_sync_left -= 1;
        }
      }
    }
  }

  if (sync.players_size() == 0 && sync.enemies_size() == 0 &&
      sync.items_size() == 0) {
    return;
  }

  // Full sync 往往包含完整敌人列表，UDP 易发生分片丢包；优先走
  // TCP（可靠）作为兜底快照。
  if (!force_full_sync) {
    if (udp_server_ != nullptr &&
        udp_server_->BroadcastState(room_id, sync) > 0) {
      return;
    }
  }

  const auto sessions = RoomManager::Instance().GetRoomSessions(room_id);
  if (!sessions.empty()) {
    SendSyncToSessions(sessions, sync);
  } else {
    spdlog::debug("房间 {} 无可用会话，跳过 TCP 同步兜底", room_id);
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
    const uint32_t prev_seq = runtime.last_input_seq;
    runtime.last_input_seq =
        std::max(runtime.last_input_seq, input.input_seq());
    if (runtime.last_input_seq != prev_seq) {
      // 需要尽快把输入确认序号同步回客户端，避免客户端预测队列长期堆积。
      runtime.dirty = true;
    }
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
