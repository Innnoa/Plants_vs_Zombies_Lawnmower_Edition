#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"
#include "network/tcp/tcp_session.hpp"

namespace {
constexpr uint32_t kUpgradeOptionCount = 3;

template <typename TMessage>
void BroadcastToRoom(uint32_t room_id, lawnmower::MessageType type,
                     const TMessage& message) {
  const auto sessions = RoomManager::Instance().GetRoomSessions(room_id);
  for (const auto& weak_session : sessions) {
    if (auto session = weak_session.lock()) {
      session->SendProto(type, message);
    }
  }
}

void SendFullSyncToRoom(uint32_t room_id,
                        const lawnmower::S2C_GameStateSync& sync) {
  const auto sessions = RoomManager::Instance().GetRoomSessions(room_id);
  for (const auto& weak_session : sessions) {
    if (auto session = weak_session.lock()) {
      session->SendProto(lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC, sync);
    }
  }
}
}  // namespace

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
      SendFullSyncToRoom(room_id, full_sync);
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
