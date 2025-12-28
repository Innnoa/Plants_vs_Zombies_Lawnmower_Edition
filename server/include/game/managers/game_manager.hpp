#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "message.pb.h"
#include "game/managers/room_manager.hpp"

// 游戏管理器：负责场景初始化、玩家状态更新与同步
class GameManager {
 public:
  static GameManager& Instance();

  GameManager(const GameManager&) = delete;
  GameManager& operator=(const GameManager&) = delete;

  // 为指定房间创建场景并生成初始 SceneInfo（覆盖已存在的同房间场景）
  lawnmower::SceneInfo CreateScene(const RoomManager::RoomSnapshot& snapshot);

  // 构造完整的状态同步（通常用于游戏开始时的全量同步）
  bool BuildFullState(uint32_t room_id, lawnmower::S2C_GameStateSync* sync);

  // 处理玩家输入并返回需要广播的增量状态；返回 false 表示未找到玩家或场景
  bool HandlePlayerInput(uint32_t player_id, const lawnmower::C2S_PlayerInput& input,
                         lawnmower::S2C_GameStateSync* sync, uint32_t* room_id);

  // 玩家断线/离开时清理场景信息
  void RemovePlayer(uint32_t player_id);

 private:
  GameManager() = default;

  struct SceneConfig {
    uint32_t width = 2000;
    uint32_t height = 2000;
    uint32_t tick_rate = 60;
    uint32_t state_sync_rate = 20;
    float move_speed = 280.0f;
  };

  struct PlayerRuntime {
    lawnmower::PlayerState state;
    uint32_t last_input_seq = 0;
  };

  struct Scene {
    SceneConfig config;
    std::unordered_map<uint32_t, PlayerRuntime> players;
  };

  SceneConfig BuildDefaultConfig() const;
  void PlacePlayers(const RoomManager::RoomSnapshot& snapshot, Scene* scene);
  lawnmower::Timestamp BuildTimestamp();

  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, Scene> scenes_;       // room_id -> scene
  std::unordered_map<uint32_t, uint32_t> player_scene_;  // player_id -> room_id
  uint32_t tick_counter_ = 0;
};
