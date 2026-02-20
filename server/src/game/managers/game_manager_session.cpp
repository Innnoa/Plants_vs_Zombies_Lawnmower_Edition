#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"

namespace {
constexpr std::size_t kMaxPendingInputs = 64;  // 单个玩家输入队列的最大缓存条数
constexpr float kDirectionEpsilonSq =
    1e-6f;  // 方向向量长度平方的极小阈值，小于此视为无效输入
constexpr float kMaxDirectionLengthSq = 1.21f;  // 方向向量长度平方的上限
}  // namespace

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

void GameManager::RemovePlayer(uint32_t player_id) {
  bool scene_removed = false;
  uint32_t room_id = 0;
  std::shared_ptr<asio::steady_timer> timer;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto mapping = player_scene_.find(player_id);
    if (mapping == player_scene_.end()) {
      return;
    }

    room_id = mapping->second;
    player_scene_.erase(mapping);

    auto scene_it = scenes_.find(room_id);
    if (scene_it == scenes_.end()) {
      return;
    }

    auto player_it = scene_it->second.players.find(player_id);
    if (player_it != scene_it->second.players.end()) {
      player_it->second.dirty_queued = false;
      scene_it->second.players.erase(player_it);
    }
    if (scene_it->second.players.empty()) {
      timer = scene_it->second.loop_timer;
      scenes_.erase(scene_it);
      scene_removed = true;
    }
  }

  if (scene_removed) {
    if (timer) {
      timer->cancel();
    }
  }
}
