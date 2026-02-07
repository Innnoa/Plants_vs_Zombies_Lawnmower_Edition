#include "game/managers/game_manager.hpp"

#include <algorithm>
#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <span>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include "network/tcp/tcp_session.hpp"
#include "network/udp/udp_server.hpp"

namespace {
constexpr float kSpawnRadius = 120.0f;         // 生成半径
constexpr int32_t kDefaultMaxHealth = 100;     // 默认最大血量
constexpr uint32_t kDefaultAttack = 10;        // 默认攻击力
constexpr uint32_t kDefaultExpToNext = 100;    // 默认升级所需经验
constexpr std::size_t kMaxPendingInputs = 64;  // 单个玩家输入队列的最大缓存条数
constexpr float kDirectionEpsilonSq =
    1e-6f;  // 方向向量长度平方的极小阈值，小于此视为无效输入
constexpr float kMaxDirectionLengthSq = 1.21f;    // 方向向量长度平方的上限
constexpr float kDeltaPositionEpsilon = 1e-4f;    // delta 位置/朝向变化阈值
constexpr uint32_t kFullSyncIntervalTicks = 180;  // 全量同步时间间隔
constexpr uint32_t kUpgradeOptionCount = 3;       // 升级选项数量
constexpr const char* kPerfRootDir = "server_metrics";  // 性能数据根目录
constexpr uint64_t kItemLogIntervalSeconds = 2;          // 道具日志输出间隔

void DedupProjectileSpawns(std::vector<lawnmower::ProjectileState>* spawns) {
  if (spawns == nullptr || spawns->size() < 2) {
    return;
  }
  std::unordered_set<uint32_t> seen;
  seen.reserve(spawns->size());
  std::size_t out = 0;
  for (std::size_t i = 0; i < spawns->size(); ++i) {
    auto& spawn = (*spawns)[i];
    const uint32_t id = spawn.projectile_id();
    if (!seen.insert(id).second) {
      continue;
    }
    if (out != i) {
      (*spawns)[out] = std::move(spawn);
    }
    out += 1;
  }
  spawns->resize(out);
}

void DedupProjectileDespawns(
    std::vector<lawnmower::ProjectileDespawn>* despawns) {
  if (despawns == nullptr || despawns->size() < 2) {
    return;
  }
  std::unordered_set<uint32_t> seen;
  seen.reserve(despawns->size());
  std::size_t out = 0;
  for (std::size_t i = 0; i < despawns->size(); ++i) {
    auto& despawn = (*despawns)[i];
    const uint32_t id = despawn.projectile_id();
    if (!seen.insert(id).second) {
      continue;
    }
    if (out != i) {
      (*despawns)[out] = std::move(despawn);
    }
    out += 1;
  }
  despawns->resize(out);
}

lawnmower::ItemEffectType ResolveItemEffectType(std::string_view effect) {
  if (effect == "heal") {
    return lawnmower::ITEM_EFFECT_HEAL;
  }
  if (effect == "exp") {
    return lawnmower::ITEM_EFFECT_EXP;
  }
  if (effect == "speed") {
    return lawnmower::ITEM_EFFECT_SPEED;
  }
  return lawnmower::ITEM_EFFECT_NONE;
}

// 计算朝向
float DegreesFromDirection(float x, float y) {
  if (std::abs(x) < kDirectionEpsilonSq && std::abs(y) < kDirectionEpsilonSq) {
    return 0.0f;
  }
  const float angle_rad = std::atan2(y, x);
  return angle_rad * 180.0f / std::numbers::pi_v<float>;
}

// 当前时间
std::chrono::milliseconds NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch());
}

std::tm ToLocalTm(std::time_t value) {
  std::tm result{};
#if defined(_WIN32)
  localtime_s(&result, &value);
#else
  localtime_r(&value, &result);
#endif
  return result;
}

std::string FormatDate(const std::chrono::system_clock::time_point& tp) {
  const std::time_t time_value = std::chrono::system_clock::to_time_t(tp);
  const std::tm tm_value = ToLocalTm(time_value);
  std::ostringstream oss;
  oss << std::put_time(&tm_value, "%Y-%m-%d");
  return oss.str();
}

std::string FormatDateTime(const std::chrono::system_clock::time_point& tp) {
  const std::time_t time_value = std::chrono::system_clock::to_time_t(tp);
  const std::tm tm_value = ToLocalTm(time_value);
  std::ostringstream oss;
  oss << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

std::filesystem::path GetServerRootDir() {
  std::filesystem::path root = std::filesystem::path(__FILE__).parent_path();
  for (int i = 0; i < 4; ++i) {
    if (!root.has_parent_path()) {
      break;
    }
    root = root.parent_path();
  }
  return root;
}

uint64_t ToEpochMs(const std::chrono::system_clock::time_point& tp) {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          tp.time_since_epoch())
          .count());
}

double ComputePercentile(std::vector<double> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  const double clamped = std::clamp(percentile, 0.0, 1.0);
  const std::size_t index = static_cast<std::size_t>(
      std::ceil(clamped * static_cast<double>(values.size() - 1)));
  std::nth_element(values.begin(), values.begin() + index, values.end());
  return values[index];
}

// 填充同步时间
void FillSyncTiming(uint32_t room_id, uint64_t tick,
                    lawnmower::S2C_GameStateSync* sync) {
  if (sync == nullptr) {
    return;
  }

  const auto now_ms = NowMs();
  sync->set_room_id(room_id);
  sync->set_server_time_ms(static_cast<uint64_t>(now_ms.count()));

  // lawnmower::Timestamp* 类型
  auto* ts = sync->mutable_sync_time();
  ts->set_server_time(static_cast<uint64_t>(now_ms.count()));
  ts->set_tick(static_cast<uint32_t>(tick));
}

// 填充增量同步时间
void FillDeltaTiming(uint32_t room_id, uint64_t tick,
                     lawnmower::S2C_GameStateDeltaSync* sync) {
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

// 向各个会话发送同步消息
struct FramedPacket {
  lawnmower::MessageType type = lawnmower::MessageType::MSG_UNKNOWN;
  std::shared_ptr<const std::string> framed;
  std::size_t payload_len = 0;
  std::size_t body_len = 0;
};

FramedPacket BuildFramedPacket(lawnmower::MessageType type,
                               const google::protobuf::Message& message) {
  lawnmower::Packet packet;
  packet.set_msg_type(type);
  const std::string payload = message.SerializeAsString();
  packet.set_payload(payload);
  const std::string body = packet.SerializeAsString();
  const uint32_t net_len = htonl(static_cast<uint32_t>(body.size()));
  std::string framed;
  framed.resize(sizeof(net_len) + body.size());
  std::memcpy(framed.data(), &net_len, sizeof(net_len));
  std::memcpy(framed.data() + sizeof(net_len), body.data(), body.size());
  return {type, std::make_shared<std::string>(std::move(framed)),
          payload.size(), body.size()};
}

void SendFramedToSessions(std::span<const std::weak_ptr<TcpSession>> sessions,
                          const FramedPacket& packet) {
  for (const auto& weak_session : sessions) {
    if (auto session = weak_session.lock()) {
      session->SendFramedPacket(packet.framed, packet.type, packet.payload_len,
                                packet.body_len);
    }
  }
}

// 向各个会话发送同步消息
void SendSyncToSessions(std::span<const std::weak_ptr<TcpSession>> sessions,
                        const lawnmower::S2C_GameStateSync& sync) {
  const auto packet =
      BuildFramedPacket(lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC, sync);
  SendFramedToSessions(sessions, packet);
}

// 向各个会话发送增量同步消息
void SendDeltaToSessions(std::span<const std::weak_ptr<TcpSession>> sessions,
                         const lawnmower::S2C_GameStateDeltaSync& sync) {
  const auto packet = BuildFramedPacket(
      lawnmower::MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC, sync);
  SendFramedToSessions(sessions, packet);
}

template <typename T>
void BroadcastToRoom(uint32_t room_id, lawnmower::MessageType type,
                     const T& message) {
  const auto sessions = RoomManager::Instance().GetRoomSessions(room_id);
  const auto packet = BuildFramedPacket(type, message);
  SendFramedToSessions(sessions, packet);
}
}  // namespace

// 简单的伪随机数生成器, state 是随机数种子指针
uint32_t GameManager::NextRng(uint32_t* state) {
  if (state == nullptr) {
    return 0;
  }
  // 线性同余法
  // LCG: fast & deterministic for gameplay purposes.
  *state = (*state * 1664525u) + 1013904223u;
  return *state;
}

// 获取一个[0,1）的浮点随机值
float GameManager::NextRngUnitFloat(uint32_t* state) {
  const uint32_t r = NextRng(state);
  // Use high 24 bits to build [0,1) float.
  // 取r的高24位，再乘 2e-24 即可把整数映射为浮点数
  return static_cast<float>((r >> 8) & 0x00FFFFFF) * (1.0f / 16777216.0f);
}

// 单例构造
GameManager& GameManager::Instance() {
  static GameManager instance;
  return instance;
}

// 构建场景默认配置
GameManager::SceneConfig GameManager::BuildDefaultConfig() const {
  SceneConfig cfg;
  cfg.width = config_.map_width;
  cfg.height = config_.map_height;
  cfg.tick_rate = config_.tick_rate;
  cfg.state_sync_rate = config_.state_sync_rate;
  cfg.move_speed = config_.move_speed;
  return cfg;
}

void GameManager::ResetPerfStats(Scene& scene) {
  scene.perf.samples.clear();
  scene.perf.total_ms = 0.0;
  scene.perf.max_ms = 0.0;
  scene.perf.min_ms = 0.0;
  scene.perf.tick_count = 0;
  scene.perf.start_time = std::chrono::system_clock::now();
  scene.perf.end_time = scene.perf.start_time;
}

std::size_t GameManager::GetPredictionHistoryLimit(const Scene& scene) const {
  double tick_interval = scene.tick_interval.count();
  if (tick_interval <= 0.0) {
    tick_interval = config_.tick_rate > 0
                        ? 1.0 / static_cast<double>(config_.tick_rate)
                        : 1.0 / 60.0;
  }
  const double seconds = std::max(0.1f, config_.prediction_history_seconds);
  const std::size_t limit =
      static_cast<std::size_t>(std::ceil(seconds / tick_interval));
  return std::max<std::size_t>(1, limit);
}

void GameManager::RecordPlayerHistoryLocked(Scene& scene) {
  const std::size_t limit = GetPredictionHistoryLimit(scene);
  for (auto& [_, runtime] : scene.players) {
    PlayerRuntime::HistoryEntry entry;
    entry.tick = scene.tick;
    entry.position = runtime.state.position();
    entry.rotation = runtime.state.rotation();
    entry.health = runtime.state.health();
    entry.is_alive = runtime.state.is_alive();
    entry.last_processed_input_seq = runtime.last_input_seq;
    runtime.history.push_back(entry);
    while (runtime.history.size() > limit) {
      runtime.history.pop_front();
    }
  }
}

void GameManager::FillPlayerHighFreq(const PlayerRuntime& runtime,
                                     lawnmower::PlayerState* out) {
  if (out == nullptr) {
    return;
  }
  out->Clear();
  out->set_player_id(runtime.state.player_id());
  out->set_rotation(runtime.state.rotation());
  out->set_is_alive(runtime.state.is_alive());
  out->set_last_processed_input_seq(runtime.last_input_seq);
  // 因 position 是自定义类型，没有 set 函数，故直接赋值
  *out->mutable_position() = runtime.state.position();
}

void GameManager::FillPlayerForSync(PlayerRuntime& runtime,
                                    lawnmower::PlayerState* out) {
  if (out == nullptr) {
    return;
  }
  // 是否发生低频全量变化
  if (runtime.low_freq_dirty) {
    // 全量状态
    *out = runtime.state;
    out->set_last_processed_input_seq(runtime.last_input_seq);
  } else {
    // 只填充高频字段
    FillPlayerHighFreq(runtime, out);
  }
}

bool GameManager::PositionChanged(const lawnmower::Vector2& current,
                                  const lawnmower::Vector2& last) {
  // 如果当前位移与上次位移的绝对值大于可接受位移最小值，则为位置变化
  return std::abs(current.x() - last.x()) > kDeltaPositionEpsilon ||
         std::abs(current.y() - last.y()) > kDeltaPositionEpsilon;
}

void GameManager::UpdatePlayerLastSync(PlayerRuntime& runtime) {
  runtime.last_sync_position = runtime.state.position();
  runtime.last_sync_rotation = runtime.state.rotation();
  runtime.last_sync_is_alive = runtime.state.is_alive();
  runtime.last_sync_input_seq = runtime.last_input_seq;
}

void GameManager::UpdateEnemyLastSync(EnemyRuntime& runtime) {
  runtime.last_sync_position = runtime.state.position();
  runtime.last_sync_health = runtime.state.health();
  runtime.last_sync_is_alive = runtime.state.is_alive();
}

void GameManager::MarkPlayerDirty(Scene& scene, uint32_t player_id,
                                  PlayerRuntime& runtime, bool low_freq) {
  if (low_freq) {
    runtime.low_freq_dirty = true;
  }
  runtime.dirty = true;
  scene.dirty_player_ids.insert(player_id);
}

void GameManager::MarkEnemyDirty(Scene& scene, uint32_t enemy_id,
                                 EnemyRuntime& runtime) {
  runtime.dirty = true;
  scene.dirty_enemy_ids.insert(enemy_id);
}

void GameManager::MarkItemDirty(Scene& scene, uint32_t item_id,
                                ItemRuntime& runtime) {
  runtime.dirty = true;
  scene.dirty_item_ids.insert(item_id);
}

void GameManager::BuildSyncPayloadsLocked(
    uint32_t room_id, Scene& scene, bool force_full_sync,
    const std::unordered_set<uint32_t>& dirty_player_ids,
    const std::unordered_set<uint32_t>& dirty_enemy_ids,
    const std::unordered_set<uint32_t>& dirty_item_ids,
    lawnmower::S2C_GameStateSync* sync,
    lawnmower::S2C_GameStateDeltaSync* delta, bool* built_sync,
    bool* built_delta, uint32_t* perf_delta_items_size,
    uint32_t* perf_sync_items_size) {
  if (sync == nullptr || delta == nullptr || built_sync == nullptr ||
      built_delta == nullptr || perf_delta_items_size == nullptr ||
      perf_sync_items_size == nullptr) {
    return;
  }

  *perf_delta_items_size = 0;
  *perf_sync_items_size = 0;
  std::vector<uint32_t> items_to_remove;
  items_to_remove.reserve(scene.items.size());
  std::vector<uint32_t> players_to_clear;
  std::vector<uint32_t> enemies_to_clear;
  std::vector<uint32_t> items_to_clear;
  players_to_clear.reserve(dirty_player_ids.size());
  enemies_to_clear.reserve(dirty_enemy_ids.size());
  items_to_clear.reserve(dirty_item_ids.size());
  if (force_full_sync) {
    FillSyncTiming(room_id, scene.tick, sync);
    if (!scene.players.empty()) {
      sync->mutable_players()->Reserve(static_cast<int>(scene.players.size()));
    }
    if (!scene.enemies.empty()) {
      sync->mutable_enemies()->Reserve(static_cast<int>(scene.enemies.size()));
    }
    if (!scene.items.empty()) {
      sync->mutable_items()->Reserve(static_cast<int>(scene.items.size()));
    }
    for (auto& [_, runtime] : scene.players) {
      FillPlayerForSync(runtime, sync->add_players());
      UpdatePlayerLastSync(runtime);
      runtime.dirty = false;
      runtime.low_freq_dirty = false;
    }
    for (auto& [_, enemy] : scene.enemies) {
      auto* out = sync->add_enemies();
      *out = enemy.state;
      UpdateEnemyLastSync(enemy);
      enemy.dirty = false;
      if (enemy.force_sync_left > 0) {
        enemy.force_sync_left -= 1;
      }
    }
    for (auto& [_, item] : scene.items) {
      if (item.is_picked) {
        items_to_remove.push_back(item.item_id);
        continue;
      }
      auto* out = sync->add_items();
      out->set_item_id(item.item_id);
      out->set_type_id(item.type_id);
      out->set_is_picked(item.is_picked);
      out->set_effect_type(item.effect_type);
      out->mutable_position()->set_x(item.x);
      out->mutable_position()->set_y(item.y);
      item.dirty = false;
    }
    *perf_sync_items_size = static_cast<uint32_t>(sync->items_size());
    *built_sync = true;
    scene.full_sync_elapsed = 0.0;
    scene.dirty_player_ids.clear();
    scene.dirty_enemy_ids.clear();
    scene.dirty_item_ids.clear();
  } else {
    bool sync_inited = false;
    bool delta_inited = false;
    if (!scene.players.empty()) {
      delta->mutable_players()->Reserve(static_cast<int>(scene.players.size()));
    }
    if (!scene.enemies.empty()) {
      delta->mutable_enemies()->Reserve(static_cast<int>(scene.enemies.size()));
    }
    if (!scene.items.empty()) {
      delta->mutable_items()->Reserve(static_cast<int>(scene.items.size()));
    }

    for (const auto player_id : dirty_player_ids) {
      auto it = scene.players.find(player_id);
      if (it == scene.players.end()) {
        players_to_clear.push_back(player_id);
        continue;
      }
      PlayerRuntime& runtime = it->second;
      if (!runtime.dirty && !runtime.low_freq_dirty) {
        players_to_clear.push_back(player_id);
        continue;
      }
      if (runtime.low_freq_dirty) {
        if (!sync_inited) {
          FillSyncTiming(room_id, scene.tick, sync);
          sync_inited = true;
        }
        FillPlayerForSync(runtime, sync->add_players());
        *built_sync = true;
        UpdatePlayerLastSync(runtime);
        runtime.dirty = false;
        runtime.low_freq_dirty = false;
        players_to_clear.push_back(player_id);
        continue;
      }
      uint32_t changed_mask = 0;
      const auto& position = runtime.state.position();
      if (PositionChanged(position, runtime.last_sync_position)) {
        changed_mask |= lawnmower::PLAYER_DELTA_POSITION;
      }
      if (std::abs(runtime.state.rotation() - runtime.last_sync_rotation) >
          kDeltaPositionEpsilon) {
        changed_mask |= lawnmower::PLAYER_DELTA_ROTATION;
      }
      if (runtime.state.is_alive() != runtime.last_sync_is_alive) {
        changed_mask |= lawnmower::PLAYER_DELTA_IS_ALIVE;
      }
      if (runtime.last_input_seq != runtime.last_sync_input_seq) {
        changed_mask |= lawnmower::PLAYER_DELTA_LAST_PROCESSED_INPUT_SEQ;
      }
      if (changed_mask == 0) {
        runtime.dirty = false;
        players_to_clear.push_back(player_id);
        continue;
      }
      if (!delta_inited) {
        FillDeltaTiming(room_id, scene.tick, delta);
        delta_inited = true;
      }
      auto* out = delta->add_players();
      out->set_player_id(runtime.state.player_id());
      out->set_changed_mask(changed_mask);
      if ((changed_mask & lawnmower::PLAYER_DELTA_POSITION) != 0) {
        *out->mutable_position() = position;
      }
      if ((changed_mask & lawnmower::PLAYER_DELTA_ROTATION) != 0) {
        out->set_rotation(runtime.state.rotation());
      }
      if ((changed_mask & lawnmower::PLAYER_DELTA_IS_ALIVE) != 0) {
        out->set_is_alive(runtime.state.is_alive());
      }
      if ((changed_mask & lawnmower::PLAYER_DELTA_LAST_PROCESSED_INPUT_SEQ) !=
          0) {
        out->set_last_processed_input_seq(
            static_cast<int32_t>(runtime.last_input_seq));
      }
      *built_delta = true;
      UpdatePlayerLastSync(runtime);
      runtime.dirty = false;
      players_to_clear.push_back(player_id);
    }

    for (const auto enemy_id : dirty_enemy_ids) {
      auto it = scene.enemies.find(enemy_id);
      if (it == scene.enemies.end()) {
        enemies_to_clear.push_back(enemy_id);
        continue;
      }
      EnemyRuntime& enemy = it->second;
      if (!enemy.dirty && enemy.force_sync_left == 0) {
        enemies_to_clear.push_back(enemy_id);
        continue;
      }
      if (enemy.force_sync_left > 0) {
        if (!sync_inited) {
          FillSyncTiming(room_id, scene.tick, sync);
          sync_inited = true;
        }
        auto* out = sync->add_enemies();
        *out = enemy.state;
        *built_sync = true;
        UpdateEnemyLastSync(enemy);
        enemy.dirty = false;
        enemy.force_sync_left -= 1;
        if (enemy.force_sync_left == 0) {
          enemies_to_clear.push_back(enemy_id);
        }
        continue;
      }
      uint32_t changed_mask = 0;
      const auto& position = enemy.state.position();
      if (PositionChanged(position, enemy.last_sync_position)) {
        changed_mask |= lawnmower::ENEMY_DELTA_POSITION;
      }
      if (enemy.state.health() != enemy.last_sync_health) {
        changed_mask |= lawnmower::ENEMY_DELTA_HEALTH;
      }
      if (enemy.state.is_alive() != enemy.last_sync_is_alive) {
        changed_mask |= lawnmower::ENEMY_DELTA_IS_ALIVE;
      }
      if (changed_mask == 0) {
        enemy.dirty = false;
        enemies_to_clear.push_back(enemy_id);
        continue;
      }
      if (!delta_inited) {
        FillDeltaTiming(room_id, scene.tick, delta);
        delta_inited = true;
      }
      auto* out = delta->add_enemies();
      out->set_enemy_id(enemy.state.enemy_id());
      out->set_changed_mask(changed_mask);
      if ((changed_mask & lawnmower::ENEMY_DELTA_POSITION) != 0) {
        *out->mutable_position() = position;
      }
      if ((changed_mask & lawnmower::ENEMY_DELTA_HEALTH) != 0) {
        out->set_health(enemy.state.health());
      }
      if ((changed_mask & lawnmower::ENEMY_DELTA_IS_ALIVE) != 0) {
        out->set_is_alive(enemy.state.is_alive());
      }
      *built_delta = true;
      UpdateEnemyLastSync(enemy);
      enemy.dirty = false;
      enemies_to_clear.push_back(enemy_id);
    }

    for (const auto item_id : dirty_item_ids) {
      auto it = scene.items.find(item_id);
      if (it == scene.items.end()) {
        items_to_clear.push_back(item_id);
        continue;
      }
      ItemRuntime& item = it->second;
      if (!item.dirty) {
        items_to_clear.push_back(item_id);
        continue;
      }
      if (!delta_inited) {
        FillDeltaTiming(room_id, scene.tick, delta);
        delta_inited = true;
      }
      auto* out = delta->add_items();
      out->set_item_id(item.item_id);
      out->set_type_id(item.type_id);
      out->set_is_picked(item.is_picked);
      out->set_effect_type(item.effect_type);
      out->mutable_position()->set_x(item.x);
      out->mutable_position()->set_y(item.y);
      *built_delta = true;
      item.dirty = false;
      items_to_clear.push_back(item_id);
      if (item.is_picked) {
        items_to_remove.push_back(item.item_id);
      }
    }
    if (delta_inited) {
      *perf_delta_items_size = static_cast<uint32_t>(delta->items_size());
    }
  }

  if (!items_to_remove.empty()) {
    for (const auto item_id : items_to_remove) {
      auto item_it = scene.items.find(item_id);
      if (item_it == scene.items.end()) {
        continue;
      }
      scene.item_pool.push_back(std::move(item_it->second));
      scene.items.erase(item_it);
      scene.dirty_item_ids.erase(item_id);
    }
  }
  if (!players_to_clear.empty()) {
    for (const auto player_id : players_to_clear) {
      scene.dirty_player_ids.erase(player_id);
    }
  }
  if (!enemies_to_clear.empty()) {
    for (const auto enemy_id : enemies_to_clear) {
      scene.dirty_enemy_ids.erase(enemy_id);
    }
  }
  if (!items_to_clear.empty()) {
    for (const auto item_id : items_to_clear) {
      scene.dirty_item_ids.erase(item_id);
    }
  }
}

void GameManager::CollectExpiredPlayersLocked(
    const Scene& scene, double grace_seconds,
    std::vector<uint32_t>* out) const {
  if (out == nullptr) {
    return;
  }
  if (grace_seconds < 0.0) {
    return;
  }
  const auto now_steady = std::chrono::steady_clock::now();
  for (const auto& [player_id, runtime] : scene.players) {
    if (!runtime.is_connected) {
      const double disconnected_seconds =
          std::chrono::duration<double>(now_steady - runtime.disconnected_at)
              .count();
      if (disconnected_seconds >= grace_seconds) {
        out->push_back(player_id);
      }
    }
  }
}

bool GameManager::HandlePausedTickLocked(
    Scene& scene, double dt_seconds,
    const std::chrono::steady_clock::time_point& perf_start) {
  if (!scene.is_paused) {
    return false;
  }
  scene.tick += 1;
  const auto perf_end = std::chrono::steady_clock::now();
  const double perf_ms =
      std::chrono::duration<double, std::milli>(perf_end - perf_start).count();
  RecordPerfSampleLocked(scene, perf_ms, dt_seconds, true,
                         static_cast<uint32_t>(scene.dirty_player_ids.size()),
                         static_cast<uint32_t>(scene.dirty_enemy_ids.size()),
                         static_cast<uint32_t>(scene.dirty_item_ids.size()), 0,
                         0);
  return true;
}

void GameManager::CleanupExpiredPlayers(
    const std::vector<uint32_t>& expired_players) {
  for (const uint32_t player_id : expired_players) {
    spdlog::info("[disconnect] timeout player_id={}", player_id);
    RoomManager::Instance().RemovePlayer(player_id);
    RemovePlayer(player_id);
    TcpSession::RevokeToken(player_id);
  }
}

void GameManager::RecordPerfSampleLocked(Scene& scene, double elapsed_ms,
                                         double dt_seconds, bool is_paused,
                                         uint32_t dirty_player_count,
                                         uint32_t dirty_enemy_count,
                                         uint32_t dirty_item_count,
                                         uint32_t delta_items_size,
                                         uint32_t sync_items_size) {
  scene.perf.tick_count += 1;
  scene.perf.total_ms += elapsed_ms;
  if (scene.perf.tick_count == 1) {
    scene.perf.min_ms = elapsed_ms;
    scene.perf.max_ms = elapsed_ms;
  } else {
    scene.perf.min_ms = std::min(scene.perf.min_ms, elapsed_ms);
    scene.perf.max_ms = std::max(scene.perf.max_ms, elapsed_ms);
  }

  const uint32_t stride = std::max<uint32_t>(1, config_.perf_sample_stride);
  if (stride > 1 && (scene.tick % stride) != 0) {
    return;
  }

  PerfSample sample;
  sample.tick = scene.tick;
  sample.logic_ms = elapsed_ms;
  sample.dt_seconds = dt_seconds;
  sample.player_count = static_cast<uint32_t>(scene.players.size());
  sample.enemy_count = static_cast<uint32_t>(scene.enemies.size());
  sample.projectile_count = static_cast<uint32_t>(scene.projectiles.size());
  sample.item_count = static_cast<uint32_t>(scene.items.size());
  sample.dirty_player_count = dirty_player_count;
  sample.dirty_enemy_count = dirty_enemy_count;
  sample.dirty_item_count = dirty_item_count;
  sample.is_paused = is_paused;
  sample.delta_items_size = delta_items_size;
  sample.sync_items_size = sync_items_size;

  scene.perf.samples.push_back(sample);
}

void GameManager::SavePerfStatsToFile(uint32_t room_id, const PerfStats& stats,
                                      uint32_t tick_rate, uint32_t sync_rate,
                                      double elapsed_seconds) {
  const std::string date_dir = FormatDate(stats.end_time);
  const std::filesystem::path root_dir = GetServerRootDir();
  std::filesystem::path output_dir = root_dir / kPerfRootDir / date_dir;
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    spdlog::warn("房间 {} 性能数据目录创建失败: {}", room_id, ec.message());
    return;
  }

  const uint64_t epoch_ms = ToEpochMs(stats.end_time);
  std::ostringstream file_name;
  file_name << "room_" << room_id << "_run_" << epoch_ms << ".json";
  const std::filesystem::path output_file = output_dir / file_name.str();

  std::ofstream out(output_file, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    spdlog::warn("房间 {} 性能数据文件打开失败: {}", room_id,
                 output_file.string());
    return;
  }

  const double avg_ms =
      stats.tick_count > 0 ? stats.total_ms / stats.tick_count : 0.0;
  std::vector<double> ms_values;
  ms_values.reserve(stats.samples.size());
  uint64_t sum_players = 0;
  uint64_t sum_enemies = 0;
  uint64_t sum_items = 0;
  uint64_t sum_dirty_players = 0;
  uint64_t sum_dirty_enemies = 0;
  uint64_t sum_dirty_items = 0;
  for (const auto& sample : stats.samples) {
    ms_values.push_back(sample.logic_ms);
    sum_players += sample.player_count;
    sum_enemies += sample.enemy_count;
    sum_items += sample.item_count;
    sum_dirty_players += sample.dirty_player_count;
    sum_dirty_enemies += sample.dirty_enemy_count;
    sum_dirty_items += sample.dirty_item_count;
  }
  const double p95_ms = ComputePercentile(ms_values, 0.95);
  const double dirty_player_ratio =
      sum_players > 0 ? static_cast<double>(sum_dirty_players) /
                            static_cast<double>(sum_players)
                      : 0.0;
  const double dirty_enemy_ratio =
      sum_enemies > 0 ? static_cast<double>(sum_dirty_enemies) /
                            static_cast<double>(sum_enemies)
                      : 0.0;
  const double dirty_item_ratio = sum_items > 0
                                      ? static_cast<double>(sum_dirty_items) /
                                            static_cast<double>(sum_items)
                                      : 0.0;

  out << "{\n";
  out << "  \"room_id\": " << room_id << ",\n";
  out << "  \"start_time\": \"" << FormatDateTime(stats.start_time) << "\",\n";
  out << "  \"end_time\": \"" << FormatDateTime(stats.end_time) << "\",\n";
  out << "  \"elapsed_seconds\": " << std::fixed << std::setprecision(3)
      << elapsed_seconds << ",\n";
  out << "  \"tick_rate\": " << tick_rate << ",\n";
  out << "  \"sync_rate\": " << sync_rate << ",\n";
  out << "  \"tick_count\": " << stats.tick_count << ",\n";
  out << "  \"avg_ms\": " << std::fixed << std::setprecision(3) << avg_ms
      << ",\n";
  out << "  \"min_ms\": " << std::fixed << std::setprecision(3) << stats.min_ms
      << ",\n";
  out << "  \"max_ms\": " << std::fixed << std::setprecision(3) << stats.max_ms
      << ",\n";
  out << "  \"p95_ms\": " << std::fixed << std::setprecision(3) << p95_ms
      << ",\n";
  out << "  \"dirty_player_ratio\": " << std::fixed << std::setprecision(6)
      << dirty_player_ratio << ",\n";
  out << "  \"dirty_enemy_ratio\": " << std::fixed << std::setprecision(6)
      << dirty_enemy_ratio << ",\n";
  out << "  \"dirty_item_ratio\": " << std::fixed << std::setprecision(6)
      << dirty_item_ratio << ",\n";
  out << "  \"samples\": [\n";
  for (std::size_t i = 0; i < stats.samples.size(); ++i) {
    const auto& sample = stats.samples[i];
    const double sample_dirty_player_ratio =
        sample.player_count > 0
            ? static_cast<double>(sample.dirty_player_count) /
                  static_cast<double>(sample.player_count)
            : 0.0;
    const double sample_dirty_enemy_ratio =
        sample.enemy_count > 0 ? static_cast<double>(sample.dirty_enemy_count) /
                                     static_cast<double>(sample.enemy_count)
                               : 0.0;
    const double sample_dirty_item_ratio =
        sample.item_count > 0 ? static_cast<double>(sample.dirty_item_count) /
                                    static_cast<double>(sample.item_count)
                              : 0.0;
    out << "    {\"tick\": " << sample.tick << ", \"logic_ms\": " << std::fixed
        << std::setprecision(3) << sample.logic_ms
        << ", \"dt_seconds\": " << std::fixed << std::setprecision(6)
        << sample.dt_seconds << ", \"players\": " << sample.player_count
        << ", \"enemies\": " << sample.enemy_count
        << ", \"projectiles\": " << sample.projectile_count
        << ", \"items\": " << sample.item_count
        << ", \"dirty_players\": " << sample.dirty_player_count
        << ", \"dirty_enemies\": " << sample.dirty_enemy_count
        << ", \"dirty_items\": " << sample.dirty_item_count
        << ", \"dirty_player_ratio\": " << std::fixed << std::setprecision(6)
        << sample_dirty_player_ratio
        << ", \"dirty_enemy_ratio\": " << std::fixed << std::setprecision(6)
        << sample_dirty_enemy_ratio << ", \"dirty_item_ratio\": " << std::fixed
        << std::setprecision(6) << sample_dirty_item_ratio
        << ", \"delta_items\": " << sample.delta_items_size
        << ", \"sync_items\": " << sample.sync_items_size
        << ", \"paused\": " << (sample.is_paused ? "true" : "false") << "}";
    if (i + 1 < stats.samples.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
  out.close();

  spdlog::info("房间 {} 性能数据已保存: {}", room_id, output_file.string());
}

// 解析道具类型
const ItemTypeConfig& GameManager::ResolveItemType(uint32_t type_id) const {
  // 后备配置
  static const ItemTypeConfig kFallback{
      .type_id = 1,
      .name = "默认道具",
      .effect = "none",
      .value = 0,
      .drop_weight = 0,
  };

  if (type_id != 0) {
    auto it = items_config_.items.find(type_id);
    if (it != items_config_.items.end()) {
      return it->second;
    }
  }

  const uint32_t default_id = items_config_.default_type_id > 0
                                  ? items_config_.default_type_id
                                  : kFallback.type_id;
  auto it = items_config_.items.find(default_id);
  if (it != items_config_.items.end()) {
    return it->second;
  }

  if (!items_config_.items.empty()) {
    return items_config_.items.begin()->second;
  }

  return kFallback;
}

// 设置io上下文
void GameManager::SetIoContext(asio::io_context* io) { io_context_ = io; }

// 设置UDP服务器
void GameManager::SetUdpServer(UdpServer* udp) { udp_server_ = udp; }

// 游戏逻辑帧的定时调度器
void GameManager::ScheduleGameTick(
    uint32_t room_id, std::chrono::microseconds interval,
    const std::shared_ptr<asio::steady_timer>& timer,
    double tick_interval_seconds) {
  if (!timer) {
    return;
  }

  std::chrono::steady_clock::time_point deadline;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      return;
    }
    Scene& scene = scene_it->second;
    if (scene.loop_timer != timer) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (scene.next_tick_time.time_since_epoch().count() == 0) {
      scene.next_tick_time = now + interval;
    }
    deadline = scene.next_tick_time;
    scene.next_tick_time += interval;
    if (deadline + interval < now) {
      deadline = now;
      scene.next_tick_time = now + interval;
    }
  }

  timer->expires_at(deadline);
  timer->async_wait([this, room_id, interval, timer,
                     tick_interval_seconds](const asio::error_code& ec) {
    if (ec == asio::error::operation_aborted) {
      return;
    }

    ProcessSceneTick(room_id, tick_interval_seconds);
    if (!ShouldRescheduleTick(room_id, timer)) {
      return;
    }
    // 递归调用，如果游戏还在运行就继续调用
    ScheduleGameTick(room_id, interval, timer, tick_interval_seconds);
  });
}

// 判断是否需要重启
bool GameManager::ShouldRescheduleTick(
    uint32_t room_id, const std::shared_ptr<asio::steady_timer>& timer) const {
  std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁
  const auto it = scenes_.find(room_id);
  if (it == scenes_.end()) {
    return false;
  }
  const Scene& scene = it->second;
  if (scene.game_over) {
    return false;
  }
  return scene.loop_timer == timer;
}

void GameManager::StartGameLoop(uint32_t room_id) {
  if (io_context_ == nullptr) {
    spdlog::warn("未设置 io_context，无法启动游戏循环");
    return;
  }

  std::shared_ptr<asio::steady_timer> timer;
  uint32_t tick_rate = 60;             // 默认tick_rate
  uint32_t state_sync_rate = 20;       // 默认state_sync_rate
  double tick_interval_seconds = 0.0;  // 默认tick_intelval_seconds

  {
    std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      spdlog::warn("房间 {} 未找到场景，无法启动游戏循环", room_id);
      return;
    }

    Scene& scene = scene_it->second;
    // 提取配置
    tick_rate = std::max<uint32_t>(1, scene.config.tick_rate);
    state_sync_rate = std::max<uint32_t>(1, scene.config.state_sync_rate);
    tick_interval_seconds = 1.0 / static_cast<double>(tick_rate);
    const auto tick_interval =
        std::chrono::duration<double>(tick_interval_seconds);
    //  设置scene的配置
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
    scene.sync_idle_elapsed = 0.0;
    scene.full_sync_elapsed = 0.0;
    scene.last_tick_time = std::chrono::steady_clock::now();
    scene.next_tick_time = std::chrono::steady_clock::time_point{};
    scene.dynamic_sync_interval = scene.sync_interval;
    ResetPerfStats(scene);
  }

  const auto interval = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::duration<double>(tick_interval_seconds));
  ScheduleGameTick(room_id, interval, timer, tick_interval_seconds);
  spdlog::debug("房间 {} 启动游戏循环，tick_rate={}，state_sync_rate={}",
                room_id, tick_rate, state_sync_rate);
}

// 停止游戏循环
void GameManager::StopGameLoop(uint32_t room_id) {
  std::shared_ptr<asio::steady_timer> timer;
  {
    std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      return;
    }
    timer = scene_it->second.loop_timer;
    scene_it->second.loop_timer.reset();  // 置空
  }

  if (timer) {
    timer->cancel();  // 因cansel会触发回调，故不在锁内执行
  }
  // 锁内摘掉timer,锁外cansel,避免死锁并正确停止循环
}

// 将坐标限制在地图边界内
lawnmower::Vector2 GameManager::ClampToMap(const SceneConfig& cfg, float x,
                                           float y) const {
  lawnmower::Vector2 pos;
  // clamp 功能是把第一个参数限制在[第二个参数，第三个参数]
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

  const auto resolve_default_role = [&]() -> const PlayerRoleConfig* {
    const uint32_t desired_role_id = player_roles_config_.default_role_id > 0
                                         ? player_roles_config_.default_role_id
                                         : 1u;

    auto it = player_roles_config_.roles.find(desired_role_id);
    if (it != player_roles_config_.roles.end()) {
      // 找到了
      return &it->second;
    }

    const PlayerRoleConfig* best = nullptr;
    uint32_t best_id = 0;
    // 找最小的role_id
    for (const auto& [role_id, cfg] : player_roles_config_.roles) {
      if (best == nullptr || role_id < best_id) {
        best = &cfg;
        best_id = role_id;
      }
    }
    return best;
  };

  const PlayerRoleConfig* default_role = resolve_default_role();

  // 遍历每一个玩家, 计算初始位置
  for (std::size_t i = 0; i < count; ++i) {
    const auto& player = snapshot.players[i];
    const float angle =
        (2.0f * std::numbers::pi_v<float> * static_cast<float>(i)) /
        static_cast<float>(count);

    // 计算实际x/y的位置
    const float x = center_x + std::cos(angle) * kSpawnRadius;
    const float y = center_y + std::sin(angle) * kSpawnRadius;
    const auto clamped_pos = ClampToMap(scene->config, x, y);  // 限制边界

    // 设置基本信息
    PlayerRuntime runtime;
    runtime.state.set_player_id(player.player_id);
    runtime.player_name = player.player_name.empty()
                              ? ("玩家" + std::to_string(player.player_id))
                              : player.player_name;
    runtime.state.mutable_position()->set_x(clamped_pos.x());
    runtime.state.mutable_position()->set_y(clamped_pos.y());
    runtime.state.set_rotation(angle * 180.0f / std::numbers::pi_v<float>);

    const int32_t max_health =
        default_role != nullptr ? std::max<int32_t>(1, default_role->max_health)
                                : kDefaultMaxHealth;
    const uint32_t attack =
        default_role != nullptr ? default_role->attack : kDefaultAttack;
    const uint32_t attack_speed =
        default_role != nullptr
            ? std::max<uint32_t>(1, default_role->attack_speed)
            : 1u;
    const float move_speed =
        default_role != nullptr && default_role->move_speed > 0.0f
            ? default_role->move_speed
            : scene->config.move_speed;
    const uint32_t crit =
        default_role != nullptr ? default_role->critical_hit_rate : 0u;
    const uint32_t role_id =
        default_role != nullptr ? default_role->role_id : 0u;

    runtime.state.set_health(max_health);
    runtime.state.set_max_health(max_health);
    runtime.state.set_level(1);
    runtime.state.set_exp(0);
    runtime.state.set_exp_to_next(kDefaultExpToNext);
    runtime.state.set_is_alive(true);
    runtime.state.set_attack(attack);
    runtime.state.set_is_friendly(true);
    runtime.state.set_role_id(role_id);
    runtime.state.set_critical_hit_rate(crit);
    runtime.state.set_has_buff(false);
    runtime.state.set_buff_id(0);
    runtime.state.set_attack_speed(attack_speed);
    runtime.state.set_move_speed(move_speed);
    runtime.state.set_last_processed_input_seq(0);
    runtime.pending_upgrade_count = 0;
    runtime.refresh_remaining = upgrade_config_.refresh_limit;
    // 初始化 delta 同步基线
    runtime.last_sync_position = runtime.state.position();
    runtime.last_sync_rotation = runtime.state.rotation();
    runtime.last_sync_is_alive = runtime.state.is_alive();
    runtime.last_sync_input_seq = runtime.last_input_seq;

    // 将玩家对应玩家信息插入会话
    scene->players.emplace(player.player_id, std::move(runtime));
    player_scene_[player.player_id] = snapshot.room_id;  // 增加玩家对应房间结构
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
  scene.next_projectile_id = 1;
  scene.next_item_id = 1;
  scene.elapsed = 0.0;
  scene.spawn_elapsed = 0.0;
  scene.wave_id = 1;
  scene.game_over = false;
  scene.rng_state = snapshot.room_id ^ static_cast<uint32_t>(NowMs().count());
  if (scene.rng_state == 0) {
    scene.rng_state = 1;
  }
  ResetPerfStats(scene);

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

  const std::size_t max_enemies_alive =
      config_.max_enemies_alive > 0 ? config_.max_enemies_alive : 256;
  scene.players.reserve(snapshot.players.size());
  scene.enemies.reserve(max_enemies_alive);
  scene.enemy_pool.reserve(max_enemies_alive);
  scene.projectiles.reserve(max_enemies_alive);
  scene.projectile_pool.reserve(max_enemies_alive);
  const std::size_t max_items_alive =
      items_config_.max_items_alive > 0 ? items_config_.max_items_alive : 64;
  scene.items.reserve(max_items_alive);
  scene.item_pool.reserve(max_items_alive);
  scene.dirty_player_ids.reserve(snapshot.players.size());
  scene.dirty_enemy_ids.reserve(max_enemies_alive);
  scene.dirty_item_ids.reserve(max_items_alive);

  PlacePlayers(snapshot, &scene);  // 放置玩家

  // 生成敌人lambda
  auto spawn_enemy = [&](uint32_t type_id) {
    if (scene.enemies.size() >= max_enemies_alive) {
      return;
    }

    // 解析敌人类型
    const EnemyTypeConfig& type = ResolveEnemyType(type_id);

    const float map_w = static_cast<float>(scene.config.width);
    const float map_h = static_cast<float>(scene.config.height);
    const float t =
        NextRngUnitFloat(&scene.rng_state);  // 获取一个[0,1)的浮点随机值
    const uint32_t edge =
        NextRng(&scene.rng_state) % 4u;  // 获取一个0-3的随机值

    float x = 0.0f;
    float y = 0.0f;
    // 根据随机选中的地图边界生成敌人
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
      default:  // 3 - top
        x = t * map_w;
        y = std::max(0.0f, map_h - kEnemySpawnInset);
        break;
    }

    EnemyRuntime runtime;
    // 敌人运行时状态配置信息
    runtime.state.set_enemy_id(scene.next_enemy_id++);
    runtime.state.set_type_id(type.type_id);
    const auto clamped_pos = ClampToMap(scene.config, x, y);  // 限制边界
    runtime.state.mutable_position()->set_x(clamped_pos.x());
    runtime.state.mutable_position()->set_y(clamped_pos.y());
    runtime.state.set_health(type.max_health);
    runtime.state.set_max_health(type.max_health);
    runtime.state.set_is_alive(true);
    runtime.state.set_wave_id(scene.wave_id);
    runtime.state.set_is_friendly(false);
    // 初始化 delta 同步基线
    runtime.last_sync_position = runtime.state.position();
    runtime.last_sync_health = runtime.state.health();
    runtime.last_sync_is_alive = runtime.state.is_alive();
    runtime.force_sync_left = kEnemySpawnForceSyncCount;
    runtime.dirty = false;
    auto [it, _] =
        scene.enemies.emplace(runtime.state.enemy_id(), std::move(runtime));
    MarkEnemyDirty(scene, it->first, it->second);
  };

  // 初始敌人数量
  const std::size_t initial_enemy_count = std::min<std::size_t>(
      max_enemies_alive, std::max<std::size_t>(1, snapshot.players.size() * 2));
  for (std::size_t i = 0; i < initial_enemy_count; ++i) {
    // 生成敌人
    spawn_enemy(PickSpawnEnemyTypeId(&scene.rng_state));
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
  // 填充同步时间
  FillSyncTiming(room_id, scene_it->second.tick, sync);

  const Scene& scene = scene_it->second;
  if (!scene.players.empty()) {
    sync->mutable_players()->Reserve(static_cast<int>(scene.players.size()));
  }
  if (!scene.enemies.empty()) {
    sync->mutable_enemies()->Reserve(static_cast<int>(scene.enemies.size()));
  }
  if (!scene.items.empty()) {
    sync->mutable_items()->Reserve(static_cast<int>(scene.items.size()));
  }
  // 全量同步包的玩家信息的state赋值
  for (const auto& [_, runtime] : scene.players) {
    auto* player_state = sync->add_players();
    *player_state = runtime.state;
    player_state->set_last_processed_input_seq(runtime.last_input_seq);
  }
  // 全量同步包的敌人信息的state赋值
  for (const auto& [_, runtime] : scene.enemies) {
    auto* enemy_state = sync->add_enemies();
    *enemy_state = runtime.state;
  }
  // 全量同步包的道具信息的state赋值
  for (const auto& [_, item] : scene.items) {
    if (item.is_picked) {
      continue;
    }
    auto* item_state = sync->add_items();
    item_state->set_item_id(item.item_id);
    item_state->set_type_id(item.type_id);
    item_state->set_is_picked(item.is_picked);
    item_state->set_effect_type(item.effect_type);
    item_state->mutable_position()->set_x(item.x);
    item_state->mutable_position()->set_y(item.y);
  }
  return true;
}

void GameManager::ProcessItems(Scene& scene, bool* has_dirty) {
  if (has_dirty == nullptr) {
    return;
  }

  const std::size_t alive_players =
      std::count_if(scene.players.begin(), scene.players.end(),
                    [](const auto& kv) { return kv.second.state.is_alive(); });
  if (alive_players == 0) {
    return;
  }

  auto mark_player_low_freq_dirty = [&](PlayerRuntime& runtime) {
    MarkPlayerDirty(scene, runtime.state.player_id(), runtime, true);
  };

  const float pick_radius =
      items_config_.pick_radius > 0.0f ? items_config_.pick_radius : 24.0f;
  const float pick_radius_sq = pick_radius * pick_radius;

  for (auto& [_, item] : scene.items) {
    if (item.is_picked) {
      continue;
    }
    for (auto& [_, player] : scene.players) {
      if (!player.state.is_alive()) {
        continue;
      }
      const float dx = player.state.position().x() - item.x;
      const float dy = player.state.position().y() - item.y;
      const float dist_sq = dx * dx + dy * dy;
      if (dist_sq > pick_radius_sq) {
        continue;
      }

      item.is_picked = true;
      MarkItemDirty(scene, item.item_id, item);
      *has_dirty = true;

      if (item.effect_type == lawnmower::ITEM_EFFECT_HEAL) {
        const ItemTypeConfig& type = ResolveItemType(item.type_id);
        const int32_t heal_value = std::max<int32_t>(0, type.value);
        if (heal_value > 0) {
          const int32_t prev_hp = player.state.health();
          const int32_t max_hp = player.state.max_health();
          const int32_t next_hp = std::min(max_hp, prev_hp + heal_value);
          if (next_hp != prev_hp) {
            player.state.set_health(next_hp);
            mark_player_low_freq_dirty(player);
          }
        }
      }

      break;
    }
  }
}

namespace {
constexpr double kMaxTickDeltaSeconds = 0.1;  // clamp 极端卡顿
constexpr double kMaxInputDeltaSeconds = 0.1;
}  // namespace

// 进程场景计时器
void GameManager::ProcessSceneTick(uint32_t room_id,
                                   double tick_interval_seconds) {
  lawnmower::S2C_GameStateSync sync;
  lawnmower::S2C_GameStateDeltaSync delta;
  bool force_full_sync = false;  // 强制全量同步
  bool should_sync = false;      // 需要同步
  bool built_sync = false;
  bool built_delta = false;

  std::vector<lawnmower::S2C_PlayerHurt> player_hurts;
  std::vector<lawnmower::S2C_EnemyDied> enemy_dieds;
  std::vector<lawnmower::EnemyAttackStateDelta> enemy_attack_states;
  std::vector<lawnmower::S2C_PlayerLevelUp> level_ups;
  std::optional<lawnmower::S2C_GameOver> game_over;
  std::optional<lawnmower::S2C_UpgradeRequest> upgrade_request;
  std::vector<lawnmower::ProjectileState> projectile_spawns;
  std::vector<lawnmower::ProjectileDespawn> projectile_despawns;
  std::vector<lawnmower::ItemState> dropped_items;
  std::vector<uint32_t> expired_players;
  bool paused_only = false;
  std::optional<PerfStats> perf_to_save;
  uint32_t perf_tick_rate = 0;
  uint32_t perf_sync_rate = 0;
  double perf_elapsed_seconds = 0.0;
  uint32_t perf_delta_items_size = 0;
  uint32_t perf_sync_items_size = 0;
  uint64_t event_tick = 0;
  uint32_t event_wave_id = 0;

  {
    std::lock_guard<std::mutex> lock(mutex_);  // 互斥锁
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      return;
    }

    Scene& scene = scene_it->second;
    if (scene.game_over) {
      return;
    }
    if (!scene.players.empty()) {
      player_hurts.reserve(scene.players.size());
      level_ups.reserve(scene.players.size());
      expired_players.reserve(scene.players.size());
    }
    if (!scene.enemies.empty()) {
      enemy_dieds.reserve(scene.enemies.size());
      enemy_attack_states.reserve(scene.enemies.size());
    }
    if (!scene.projectiles.empty()) {
      projectile_spawns.reserve(scene.projectiles.size());
      projectile_despawns.reserve(scene.projectiles.size());
    }
    if (!scene.items.empty()) {
      dropped_items.reserve(scene.items.size());
    }
    const auto perf_start = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    // 没看明白
    const auto elapsed = scene.last_tick_time.time_since_epoch().count() == 0
                             ? scene.tick_interval
                             : now - scene.last_tick_time;
    scene.last_tick_time = now;
    // 看不懂
    const double elapsed_seconds =
        std::clamp(std::chrono::duration<double>(elapsed).count(), 0.0,
                   kMaxTickDeltaSeconds);
    // 看不懂
    const double dt_seconds =
        elapsed_seconds > 0.0 ? elapsed_seconds : tick_interval_seconds;

    const double grace_seconds =
        std::max(0.0, static_cast<double>(config_.reconnect_grace_seconds));
    CollectExpiredPlayersLocked(scene, grace_seconds, &expired_players);

    if (HandlePausedTickLocked(scene, dt_seconds, perf_start)) {
      paused_only = true;
    } else {
      bool has_dirty = false;

      for (auto& [_, runtime] : scene.players) {
        runtime.attack_cooldown_seconds -= dt_seconds;
        if (!runtime.is_connected) {
          runtime.pending_inputs.clear();
          runtime.wants_attacking = false;
          runtime.has_attack_dir = false;
          continue;
        }
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

          const double reported_dt =
              input.delta_ms() > 0 ? std::clamp(input.delta_ms() / 1000.0, 0.0,
                                                kMaxInputDeltaSeconds)
                                   : tick_interval_seconds;
          const double remaining_budget =
              kMaxTickDeltaSeconds - processed_seconds;
          const double input_dt = std::min(reported_dt, remaining_budget);

          const bool can_move = runtime.state.is_alive();
          if (len_sq >= kDirectionEpsilonSq &&
              len_sq <= kMaxDirectionLengthSq && input_dt > 0.0 && can_move) {
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

        if (moved || consumed_input || runtime.low_freq_dirty) {
          MarkPlayerDirty(scene, runtime.state.player_id(), runtime, false);
          has_dirty = true;
        }
      }

      scene.elapsed += dt_seconds;
      ProcessEnemies(scene, dt_seconds, &has_dirty);
      ProcessItems(scene, &has_dirty);

      // -----------------
      // 战斗：玩家攻击 + 敌人接触伤害
      // -----------------

      ProcessCombatAndProjectiles(
          scene, dt_seconds, &player_hurts, &enemy_dieds, &enemy_attack_states,
          &level_ups, &game_over, &projectile_spawns, &projectile_despawns,
          &dropped_items, &has_dirty);
      if (scene.upgrade_stage == UpgradeStage::kNone) {
        uint32_t candidate_player_id = 0;
        for (const auto& [player_id, runtime] : scene.players) {
          if (runtime.pending_upgrade_count > 0) {
            candidate_player_id = player_id;
            break;
          }
        }
        if (candidate_player_id != 0) {
          lawnmower::S2C_UpgradeRequest request;
          if (BeginUpgradeLocked(room_id, scene, candidate_player_id,
                                 lawnmower::UPGRADE_REASON_LEVEL_UP,
                                 &request)) {
            upgrade_request = request;
          }
        }
      }

      RecordPlayerHistoryLocked(scene);

      const bool has_dirty_players = !scene.dirty_player_ids.empty();
      const bool has_dirty_enemies = !scene.dirty_enemy_ids.empty();
      const bool has_dirty_items = !scene.dirty_item_ids.empty();
      has_dirty = has_dirty_players || has_dirty_enemies || has_dirty_items;

      const bool has_priority_events =
          !projectile_spawns.empty() || !projectile_despawns.empty() ||
          !dropped_items.empty() || !player_hurts.empty() ||
          !enemy_attack_states.empty() || !enemy_dieds.empty() ||
          !level_ups.empty() || game_over.has_value() ||
          upgrade_request.has_value();

      scene.tick += 1;
      scene.sync_accumulator += dt_seconds;
      scene.full_sync_elapsed += dt_seconds;
      const double base_sync_interval = scene.sync_interval.count() > 0.0
                                            ? scene.sync_interval.count()
                                            : tick_interval_seconds;
      const double idle_light_seconds =
          std::max(0.0f, config_.sync_idle_light_seconds);
      const double idle_heavy_seconds =
          std::max(idle_light_seconds,
                   static_cast<double>(config_.sync_idle_heavy_seconds));
      const double scale_light = std::max(1.0f, config_.sync_scale_light);
      const double scale_medium =
          std::max(scale_light, static_cast<double>(config_.sync_scale_medium));
      const double scale_idle =
          std::max(scale_medium, static_cast<double>(config_.sync_scale_idle));
      if (has_priority_events || has_dirty_players) {
        scene.sync_idle_elapsed = 0.0;
        scene.dynamic_sync_interval =
            std::chrono::duration<double>(base_sync_interval);
      } else {
        scene.sync_idle_elapsed += dt_seconds;
        double scale = 1.0;
        if (has_dirty_enemies || has_dirty_items) {
          scale = scene.sync_idle_elapsed >= idle_light_seconds ? scale_medium
                                                                : scale_light;
        } else {
          scale = scene.sync_idle_elapsed >= idle_heavy_seconds ? scale_idle
                                                                : scale_medium;
        }
        scene.dynamic_sync_interval =
            std::chrono::duration<double>(base_sync_interval * scale);
      }

      const double sync_interval = scene.dynamic_sync_interval.count() > 0.0
                                       ? scene.dynamic_sync_interval.count()
                                       : base_sync_interval;

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

      const bool want_sync = should_sync || force_full_sync;
      const bool need_sync = want_sync && (force_full_sync || has_dirty);
      if (need_sync) {
        BuildSyncPayloadsLocked(room_id, scene, force_full_sync,
                                scene.dirty_player_ids, scene.dirty_enemy_ids,
                                scene.dirty_item_ids, &sync, &delta,
                                &built_sync, &built_delta,
                                &perf_delta_items_size, &perf_sync_items_size);
      }

      const auto perf_end = std::chrono::steady_clock::now();
      const double perf_ms =
          std::chrono::duration<double, std::milli>(perf_end - perf_start)
              .count();
      RecordPerfSampleLocked(
          scene, perf_ms, dt_seconds, false,
          static_cast<uint32_t>(scene.dirty_player_ids.size()),
          static_cast<uint32_t>(scene.dirty_enemy_ids.size()),
          static_cast<uint32_t>(scene.dirty_item_ids.size()),
          perf_delta_items_size, perf_sync_items_size);

      if (game_over.has_value()) {
        scene.perf.end_time = std::chrono::system_clock::now();
        perf_tick_rate = scene.config.tick_rate;
        perf_sync_rate = scene.config.state_sync_rate;
        perf_elapsed_seconds = scene.elapsed;
        perf_to_save = std::move(scene.perf);
      }

      event_wave_id = scene.wave_id;
      event_tick = scene.tick;

      const uint64_t log_interval_ticks = std::max<uint64_t>(
          1, static_cast<uint64_t>(scene.config.tick_rate) *
                 kItemLogIntervalSeconds);
      if (scene.tick >= scene.last_item_log_tick + log_interval_ticks) {
        scene.last_item_log_tick = scene.tick;
        spdlog::info(
            "[item] room={} tick={} items={} dirty_items={} "
            "dropped_events={} built_sync={} built_delta={} delta_items={} "
            "sync_items={}",
            room_id, scene.tick, scene.items.size(),
            scene.dirty_item_ids.size(), dropped_items.size(),
            built_sync ? "true" : "false", built_delta ? "true" : "false",
            perf_delta_items_size, perf_sync_items_size);
      }
    }
  }

  CleanupExpiredPlayers(expired_players);

  if (paused_only) {
    return;
  }

  DedupProjectileSpawns(&projectile_spawns);
  DedupProjectileDespawns(&projectile_despawns);

  const auto event_now_ms = NowMs();
  const uint64_t event_now_count = static_cast<uint64_t>(event_now_ms.count());

  lawnmower::S2C_ProjectileSpawn projectile_spawn_msg;
  const bool has_projectile_spawn = !projectile_spawns.empty();
  if (has_projectile_spawn) {
    projectile_spawn_msg.set_room_id(room_id);
    projectile_spawn_msg.set_server_time_ms(event_now_count);
    projectile_spawn_msg.mutable_sync_time()->set_server_time(event_now_count);
    projectile_spawn_msg.mutable_sync_time()->set_tick(
        static_cast<uint32_t>(event_tick));
    projectile_spawn_msg.mutable_projectiles()->Reserve(
        static_cast<int>(projectile_spawns.size()));
    for (const auto& spawn : projectile_spawns) {
      *projectile_spawn_msg.add_projectiles() = spawn;
    }
  }

  lawnmower::S2C_ProjectileDespawn projectile_despawn_msg;
  const bool has_projectile_despawn = !projectile_despawns.empty();
  if (has_projectile_despawn) {
    projectile_despawn_msg.set_room_id(room_id);
    projectile_despawn_msg.set_server_time_ms(event_now_count);
    projectile_despawn_msg.mutable_sync_time()->set_server_time(
        event_now_count);
    projectile_despawn_msg.mutable_sync_time()->set_tick(
        static_cast<uint32_t>(event_tick));
    projectile_despawn_msg.mutable_projectiles()->Reserve(
        static_cast<int>(projectile_despawns.size()));
    for (const auto& despawn : projectile_despawns) {
      *projectile_despawn_msg.add_projectiles() = despawn;
    }
  }

  lawnmower::S2C_DroppedItem dropped_item_msg;
  const bool has_dropped_items = !dropped_items.empty();
  if (has_dropped_items) {
    dropped_item_msg.set_source_enemy_id(0);
    dropped_item_msg.set_wave_id(event_wave_id);
    dropped_item_msg.mutable_items()->Reserve(
        static_cast<int>(dropped_items.size()));
    for (const auto& item : dropped_items) {
      *dropped_item_msg.add_items() = item;
    }
  }

  lawnmower::S2C_EnemyAttackStateSync enemy_attack_state_msg;
  const bool has_enemy_attack_state = !enemy_attack_states.empty();
  if (has_enemy_attack_state) {
    enemy_attack_state_msg.set_room_id(room_id);
    enemy_attack_state_msg.set_server_time_ms(event_now_count);
    enemy_attack_state_msg.mutable_sync_time()->set_server_time(
        event_now_count);
    enemy_attack_state_msg.mutable_sync_time()->set_tick(
        static_cast<uint32_t>(event_tick));
    enemy_attack_state_msg.mutable_enemies()->Reserve(
        static_cast<int>(enemy_attack_states.size()));
    for (const auto& delta : enemy_attack_states) {
      *enemy_attack_state_msg.add_enemies() = delta;
    }
  }

  if (game_over.has_value()) {
    spdlog::info("房间 {} 游戏结束，survive_time={}s，scores={}", room_id,
                 game_over->survive_time(), game_over->scores_size());
    spdlog::info("房间 {} GameOver 详情: victory={}", room_id,
                 game_over->victory() ? "true" : "false");
    for (const auto& score : game_over->scores()) {
      spdlog::info(
          "房间 {} 分数: player_id={} name={} level={} kills={} damage={}",
          room_id, score.player_id(), score.player_name(), score.final_level(),
          score.kill_count(), score.damage_dealt());
    }
  }

  if (has_projectile_spawn || has_projectile_despawn || has_dropped_items ||
      !player_hurts.empty() || has_enemy_attack_state || !enemy_dieds.empty() ||
      !level_ups.empty() || game_over.has_value() ||
      upgrade_request.has_value()) {
    const auto sessions = RoomManager::Instance().GetRoomSessions(room_id);
    for (const auto& weak_session : sessions) {
      auto session = weak_session.lock();
      if (!session) {
        continue;
      }
      if (has_projectile_spawn) {
        session->SendProto(lawnmower::MessageType::MSG_S2C_PROJECTILE_SPAWN,
                           projectile_spawn_msg);
      }
      if (has_projectile_despawn) {
        session->SendProto(lawnmower::MessageType::MSG_S2C_PROJECTILE_DESPAWN,
                           projectile_despawn_msg);
      }
      if (has_dropped_items) {
        session->SendProto(lawnmower::MessageType::MSG_S2C_DROPPED_ITEM,
                           dropped_item_msg);
      }
      if (has_enemy_attack_state) {
        session->SendProto(
            lawnmower::MessageType::MSG_S2C_ENEMY_ATTACK_STATE_SYNC,
            enemy_attack_state_msg);
      }
      for (const auto& hurt : player_hurts) {
        session->SendProto(lawnmower::MessageType::MSG_S2C_PLAYER_HURT, hurt);
      }
      for (const auto& died : enemy_dieds) {
        session->SendProto(lawnmower::MessageType::MSG_S2C_ENEMY_DIED, died);
      }
      for (const auto& level_up : level_ups) {
        session->SendProto(lawnmower::MessageType::MSG_S2C_PLAYER_LEVEL_UP,
                           level_up);
      }
      if (upgrade_request.has_value()) {
        session->SendProto(lawnmower::MessageType::MSG_S2C_UPGRADE_REQUEST,
                           *upgrade_request);
      }
      if (game_over.has_value()) {
        session->SendProto(lawnmower::MessageType::MSG_S2C_GAME_OVER,
                           *game_over);
      }
    }
  }

  if (game_over.has_value()) {
    // 等 GameOver 消息发送完再重置房间状态，避免客户端被 ROOM_UPDATE 提前切屏。
    if (!RoomManager::Instance().FinishGame(room_id)) {
      spdlog::warn("房间 {} 未找到，无法重置游戏状态", room_id);
    }
  }

  if (perf_to_save.has_value()) {
    SavePerfStatsToFile(room_id, *perf_to_save, perf_tick_rate, perf_sync_rate,
                        perf_elapsed_seconds);
  }

  const bool has_sync_payload =
      built_sync && (sync.players_size() > 0 || sync.enemies_size() > 0 ||
                     sync.items_size() > 0);
  const bool has_delta_payload =
      built_delta && (delta.players_size() > 0 || delta.enemies_size() > 0 ||
                      delta.items_size() > 0);

  if (!has_sync_payload && !has_delta_payload) {
    return;
  }

  std::vector<std::weak_ptr<TcpSession>> sessions;
  bool sessions_ready = false;
  auto get_sessions = [&]() -> const std::vector<std::weak_ptr<TcpSession>>& {
    if (!sessions_ready) {
      sessions = RoomManager::Instance().GetRoomSessions(room_id);
      sessions_ready = true;
    }
    return sessions;
  };

  // 优先尝试 UDP 发送增量；若无 UDP 则走 TCP 兜底。
  if (has_delta_payload) {
    bool delta_sent_udp = false;
    if (udp_server_ != nullptr) {
      delta_sent_udp = udp_server_->BroadcastDeltaState(room_id, delta) > 0;
    }
    if (!delta_sent_udp) {
      const auto& targets = get_sessions();
      if (!targets.empty()) {
        SendDeltaToSessions(targets, delta);
      } else {
        spdlog::debug("房间 {} 无可用会话，跳过 TCP 增量同步兜底", room_id);
      }
    }
  }

  if (has_sync_payload) {
    bool sync_sent_udp = false;
    // Full sync 往往包含完整敌人列表，UDP 易发生分片丢包；优先走 TCP 兜底快照。
    // 若已发送增量，同一 tick 不再走 UDP，避免客户端判重丢包。
    const bool allow_udp_sync = !force_full_sync && !has_delta_payload;
    if (allow_udp_sync && udp_server_ != nullptr) {
      sync_sent_udp = udp_server_->BroadcastState(room_id, sync) > 0;
    }
    if (!sync_sent_udp) {
      const auto& targets = get_sessions();
      if (!targets.empty()) {
        SendSyncToSessions(targets, sync);
      } else {
        spdlog::debug("房间 {} 无可用会话，跳过 TCP 同步兜底", room_id);
      }
    }
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

  const uint32_t input_tick = input.input_time().tick();
  if (input_tick > 0) {
    const uint64_t scene_tick = scene.tick;
    const std::size_t history_limit = GetPredictionHistoryLimit(scene);
    if (scene_tick > input_tick && (scene_tick - input_tick) > history_limit) {
      spdlog::debug(
          "HandlePlayerInput: player {} 输入过期 input_tick={} scene_tick={} "
          "window={}",
          player_id, input_tick, scene_tick, history_limit);
      return false;
    }
  }

  const uint32_t seq = input.input_seq();  // 输入序号（客户端递增）
  if (seq != 0 && seq <= runtime.last_input_seq) {
    spdlog::debug("HandlePlayerInput: player {} 输入序号回退 seq={} last={}",
                  player_id, seq, runtime.last_input_seq);
    return false;
  }

  if (scene.is_paused) {
    const uint32_t prev_seq = runtime.last_input_seq;
    runtime.last_input_seq =
        std::max(runtime.last_input_seq, input.input_seq());
    if (runtime.last_input_seq != prev_seq) {
      MarkPlayerDirty(scene, player_id, runtime, false);
    }
    runtime.wants_attacking = false;
    runtime.pending_inputs.clear();
    *room_id = target_room_id;
    return true;
  }

  // 战斗相关：即便不移动也要同步攻击意图（例如原地攻击/抬手取消）。
  runtime.wants_attacking = input.is_attacking();

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
      MarkPlayerDirty(scene, player_id, runtime, false);
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

void GameManager::BuildUpgradeOptionsLocked(Scene& scene) {
  scene.upgrade_options.clear();
  if (upgrade_config_.effects.empty()) {
    return;
  }

  std::vector<std::size_t> candidates;
  candidates.reserve(upgrade_config_.effects.size());
  for (std::size_t i = 0; i < upgrade_config_.effects.size(); ++i) {
    candidates.push_back(i);
  }

  for (uint32_t i = 0; i < kUpgradeOptionCount; ++i) {
    if (candidates.empty()) {
      for (std::size_t j = 0; j < upgrade_config_.effects.size(); ++j) {
        candidates.push_back(j);
      }
    }

    uint64_t total_weight = 0;
    for (std::size_t idx : candidates) {
      total_weight +=
          std::max<uint32_t>(1, upgrade_config_.effects[idx].weight);
    }
    if (total_weight == 0) {
      break;
    }

    uint64_t roll = GameManager::NextRng(&scene.rng_state) % total_weight;
    std::size_t chosen_pos = 0;
    for (; chosen_pos < candidates.size(); ++chosen_pos) {
      const auto idx = candidates[chosen_pos];
      const uint64_t weight =
          std::max<uint32_t>(1, upgrade_config_.effects[idx].weight);
      if (roll < weight) {
        break;
      }
      roll -= weight;
    }
    if (chosen_pos >= candidates.size()) {
      chosen_pos = 0;
    }

    scene.upgrade_options.push_back(
        upgrade_config_.effects[candidates[chosen_pos]]);
    candidates.erase(candidates.begin() +
                     static_cast<std::ptrdiff_t>(chosen_pos));
  }
}

bool GameManager::BeginUpgradeLocked(uint32_t room_id, Scene& scene,
                                     uint32_t player_id,
                                     lawnmower::UpgradeReason reason,
                                     lawnmower::S2C_UpgradeRequest* request) {
  if (request == nullptr) {
    return false;
  }
  scene.is_paused = true;
  scene.upgrade_player_id = player_id;
  scene.upgrade_stage = UpgradeStage::kRequestSent;
  scene.upgrade_reason = reason;
  scene.upgrade_options.clear();
  for (auto& [_, runtime] : scene.players) {
    runtime.pending_inputs.clear();
    runtime.wants_attacking = false;
  }

  request->set_room_id(room_id);
  request->set_player_id(player_id);
  request->set_reason(reason);
  return true;
}

void GameManager::ResetUpgradeLocked(Scene& scene) {
  scene.is_paused = false;
  scene.upgrade_player_id = 0;
  scene.upgrade_stage = UpgradeStage::kNone;
  scene.upgrade_reason = lawnmower::UPGRADE_REASON_UNKNOWN;
  scene.upgrade_options.clear();
}

void GameManager::ApplyUpgradeEffect(PlayerRuntime& runtime,
                                     const UpgradeEffectConfig& effect) {
  const int64_t delta = static_cast<int64_t>(std::llround(effect.value));
  switch (effect.type) {
    case lawnmower::UPGRADE_TYPE_MOVE_SPEED: {
      const float delta_speed = static_cast<float>(delta);
      const float next =
          std::clamp(runtime.state.move_speed() + delta_speed, 0.0f, 5000.0f);
      runtime.state.set_move_speed(next);
      break;
    }
    case lawnmower::UPGRADE_TYPE_ATTACK: {
      const int64_t next = std::clamp<int64_t>(
          static_cast<int64_t>(runtime.state.attack()) + delta, 0, 100000);
      runtime.state.set_attack(static_cast<uint32_t>(next));
      break;
    }
    case lawnmower::UPGRADE_TYPE_ATTACK_SPEED: {
      const int64_t next = std::clamp<int64_t>(
          static_cast<int64_t>(runtime.state.attack_speed()) + delta, 1, 1000);
      runtime.state.set_attack_speed(static_cast<uint32_t>(next));
      break;
    }
    case lawnmower::UPGRADE_TYPE_MAX_HEALTH: {
      const int64_t next = std::clamp<int64_t>(
          static_cast<int64_t>(runtime.state.max_health()) + delta, 1, 100000);
      runtime.state.set_max_health(static_cast<int32_t>(next));
      if (runtime.state.health() > next) {
        runtime.state.set_health(static_cast<int32_t>(next));
      }
      break;
    }
    case lawnmower::UPGRADE_TYPE_CRITICAL_RATE: {
      const int64_t next = std::clamp<int64_t>(
          static_cast<int64_t>(runtime.state.critical_hit_rate()) + delta, 0,
          10000);
      runtime.state.set_critical_hit_rate(static_cast<uint32_t>(next));
      break;
    }
    default:
      break;
  }
}

bool GameManager::HandleUpgradeRequestAck(
    uint32_t player_id, const lawnmower::C2S_UpgradeRequestAck& request) {
  static_cast<void>(request);
  uint32_t room_id = 0;
  lawnmower::S2C_UpgradeOptions options_msg;
  bool should_send = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto mapping = player_scene_.find(player_id);
    if (mapping == player_scene_.end()) {
      spdlog::debug("HandleUpgradeRequestAck: player {} 未映射到场景",
                    player_id);
      return false;
    }
    room_id = mapping->second;
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      spdlog::debug("HandleUpgradeRequestAck: room {} 未找到场景", room_id);
      return false;
    }
    Scene& scene = scene_it->second;
    if (scene.upgrade_stage != UpgradeStage::kRequestSent ||
        scene.upgrade_player_id != player_id) {
      spdlog::debug("HandleUpgradeRequestAck: room {} 升级阶段不匹配 player={}",
                    room_id, player_id);
      return false;
    }
    auto player_it = scene.players.find(player_id);
    if (player_it == scene.players.end()) {
      return false;
    }

    BuildUpgradeOptionsLocked(scene);
    if (scene.upgrade_options.empty()) {
      spdlog::warn("房间 {} 升级选项为空，取消升级流程", room_id);
      ResetUpgradeLocked(scene);
      return false;
    }

    scene.upgrade_stage = UpgradeStage::kOptionsSent;
    options_msg.set_room_id(room_id);
    options_msg.set_player_id(player_id);
    options_msg.set_reason(scene.upgrade_reason);
    options_msg.set_refresh_remaining(player_it->second.refresh_remaining);
    for (std::size_t i = 0; i < scene.upgrade_options.size(); ++i) {
      const auto& effect = scene.upgrade_options[i];
      auto* option = options_msg.add_options();
      option->set_option_index(static_cast<uint32_t>(i));
      auto* out_effect = option->add_effects();
      out_effect->set_type(effect.type);
      out_effect->set_level(effect.level);
      out_effect->set_value(static_cast<int32_t>(std::llround(effect.value)));
    }
    should_send = true;
  }

  if (should_send) {
    BroadcastToRoom(room_id, lawnmower::MessageType::MSG_S2C_UPGRADE_OPTIONS,
                    options_msg);
  }
  return should_send;
}

bool GameManager::HandleUpgradeOptionsAck(
    uint32_t player_id, const lawnmower::C2S_UpgradeOptionsAck& request) {
  static_cast<void>(request);
  std::lock_guard<std::mutex> lock(mutex_);
  const auto mapping = player_scene_.find(player_id);
  if (mapping == player_scene_.end()) {
    spdlog::debug("HandleUpgradeOptionsAck: player {} 未映射到场景", player_id);
    return false;
  }
  const uint32_t room_id = mapping->second;
  auto scene_it = scenes_.find(room_id);
  if (scene_it == scenes_.end()) {
    spdlog::debug("HandleUpgradeOptionsAck: room {} 未找到场景", room_id);
    return false;
  }
  Scene& scene = scene_it->second;
  if (scene.upgrade_stage != UpgradeStage::kOptionsSent ||
      scene.upgrade_player_id != player_id) {
    spdlog::debug("HandleUpgradeOptionsAck: room {} 升级阶段不匹配 player={}",
                  room_id, player_id);
    return false;
  }
  scene.upgrade_stage = UpgradeStage::kWaitingSelect;
  return true;
}

bool GameManager::HandleUpgradeSelect(
    uint32_t player_id, const lawnmower::C2S_UpgradeSelect& request) {
  uint32_t room_id = 0;
  std::optional<lawnmower::S2C_UpgradeRequest> next_request;
  lawnmower::S2C_UpgradeSelectAck ack;
  bool should_send_ack = false;
  bool should_resume = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto mapping = player_scene_.find(player_id);
    if (mapping == player_scene_.end()) {
      spdlog::debug("HandleUpgradeSelect: player {} 未映射到场景", player_id);
      return false;
    }
    room_id = mapping->second;
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      spdlog::debug("HandleUpgradeSelect: room {} 未找到场景", room_id);
      return false;
    }
    Scene& scene = scene_it->second;
    if (scene.upgrade_stage != UpgradeStage::kWaitingSelect ||
        scene.upgrade_player_id != player_id) {
      spdlog::debug("HandleUpgradeSelect: room {} 升级阶段不匹配 player={}",
                    room_id, player_id);
      return false;
    }
    if (scene.upgrade_options.empty()) {
      spdlog::warn("房间 {} 升级选项为空，忽略选择", room_id);
      return false;
    }
    const uint32_t option_index = request.option_index();
    if (option_index >= scene.upgrade_options.size()) {
      spdlog::warn("房间 {} 升级选择索引越界 index={}", room_id, option_index);
      return false;
    }
    auto player_it = scene.players.find(player_id);
    if (player_it == scene.players.end()) {
      return false;
    }

    ApplyUpgradeEffect(player_it->second, scene.upgrade_options[option_index]);
    MarkPlayerDirty(scene, player_id, player_it->second, true);

    if (player_it->second.pending_upgrade_count > 0) {
      player_it->second.pending_upgrade_count -= 1;
    }

    ack.set_room_id(room_id);
    ack.set_player_id(player_id);
    ack.set_option_index(option_index);
    should_send_ack = true;

    if (player_it->second.pending_upgrade_count > 0) {
      lawnmower::S2C_UpgradeRequest request_msg;
      if (BeginUpgradeLocked(room_id, scene, player_id,
                             lawnmower::UPGRADE_REASON_LEVEL_UP,
                             &request_msg)) {
        next_request = request_msg;
      }
    } else {
      ResetUpgradeLocked(scene);
      should_resume = true;
    }
  }

  if (should_send_ack) {
    BroadcastToRoom(room_id, lawnmower::MessageType::MSG_S2C_UPGRADE_SELECT_ACK,
                    ack);
  }
  if (next_request.has_value()) {
    BroadcastToRoom(room_id, lawnmower::MessageType::MSG_S2C_UPGRADE_REQUEST,
                    *next_request);
  }
  if (should_resume) {
    lawnmower::S2C_GameStateSync full_sync;
    if (BuildFullState(room_id, &full_sync)) {
      const auto sessions = RoomManager::Instance().GetRoomSessions(room_id);
      if (!sessions.empty()) {
        SendSyncToSessions(sessions, full_sync);
      }
    }
  }
  return should_send_ack;
}

bool GameManager::HandleUpgradeRefreshRequest(
    uint32_t player_id, const lawnmower::C2S_UpgradeRefreshRequest& request) {
  static_cast<void>(request);
  uint32_t room_id = 0;
  std::optional<lawnmower::S2C_UpgradeRequest> request_msg;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto mapping = player_scene_.find(player_id);
    if (mapping == player_scene_.end()) {
      spdlog::debug("HandleUpgradeRefreshRequest: player {} 未映射到场景",
                    player_id);
      return false;
    }
    room_id = mapping->second;
    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      spdlog::debug("HandleUpgradeRefreshRequest: room {} 未找到场景", room_id);
      return false;
    }
    Scene& scene = scene_it->second;
    if (scene.upgrade_stage == UpgradeStage::kNone ||
        scene.upgrade_player_id != player_id) {
      spdlog::debug(
          "HandleUpgradeRefreshRequest: room {} 升级阶段不匹配 player={}",
          room_id, player_id);
      return false;
    }
    auto player_it = scene.players.find(player_id);
    if (player_it == scene.players.end()) {
      return false;
    }
    if (player_it->second.refresh_remaining == 0) {
      spdlog::debug("房间 {} 玩家 {} 刷新次数耗尽", room_id, player_id);
      return false;
    }

    player_it->second.refresh_remaining -= 1;
    lawnmower::S2C_UpgradeRequest out;
    if (BeginUpgradeLocked(room_id, scene, player_id,
                           lawnmower::UPGRADE_REASON_REFRESH, &out)) {
      request_msg = out;
    }
  }

  if (request_msg.has_value()) {
    BroadcastToRoom(room_id, lawnmower::MessageType::MSG_S2C_UPGRADE_REQUEST,
                    *request_msg);
    return true;
  }
  return false;
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

bool GameManager::MarkPlayerDisconnected(uint32_t player_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto mapping = player_scene_.find(player_id);
  if (mapping == player_scene_.end()) {
    return false;
  }

  auto scene_it = scenes_.find(mapping->second);
  if (scene_it == scenes_.end()) {
    player_scene_.erase(mapping);
    return false;
  }

  Scene& scene = scene_it->second;
  auto player_it = scene.players.find(player_id);
  if (player_it == scene.players.end()) {
    player_scene_.erase(mapping);
    return false;
  }

  PlayerRuntime& runtime = player_it->second;
  if (!runtime.is_connected) {
    return true;
  }
  runtime.is_connected = false;
  runtime.disconnected_at = std::chrono::steady_clock::now();
  runtime.pending_inputs.clear();
  runtime.wants_attacking = false;
  runtime.has_attack_dir = false;
  runtime.attack_cooldown_seconds = 0.0;
  spdlog::info("玩家 {} 断线，进入重连宽限期", player_id);
  return true;
}

bool GameManager::TryReconnectPlayer(uint32_t player_id, uint32_t room_id,
                                     uint32_t last_input_seq,
                                     uint32_t last_server_tick,
                                     ReconnectSnapshot* out) {
  if (out == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto mapping = player_scene_.find(player_id);
  if (mapping == player_scene_.end()) {
    return false;
  }
  if (room_id != 0 && mapping->second != room_id) {
    return false;
  }

  auto scene_it = scenes_.find(mapping->second);
  if (scene_it == scenes_.end()) {
    return false;
  }

  Scene& scene = scene_it->second;
  auto player_it = scene.players.find(player_id);
  if (player_it == scene.players.end()) {
    return false;
  }

  PlayerRuntime& runtime = player_it->second;
  runtime.is_connected = true;
  runtime.disconnected_at = {};
  runtime.pending_inputs.clear();
  runtime.wants_attacking = false;
  runtime.has_attack_dir = false;
  runtime.attack_cooldown_seconds = 0.0;
  runtime.last_input_seq = last_input_seq;
  runtime.last_sync_input_seq = last_input_seq;

  out->room_id = mapping->second;
  out->server_tick = scene.tick;
  out->is_paused = scene.is_paused;
  out->player_name = runtime.player_name;

  spdlog::info(
      "玩家 {} 重连成功 room={} last_input_seq={} client_tick={} "
      "server_tick={}",
      player_id, out->room_id, last_input_seq, last_server_tick,
      out->server_tick);
  return true;
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
    scene_it->second.dirty_player_ids.erase(player_id);
    if (scene_it->second.players.empty()) {  // 会话中玩家数量为0
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
