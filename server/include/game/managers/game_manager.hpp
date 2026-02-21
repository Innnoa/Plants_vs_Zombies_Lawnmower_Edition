#pragma once

#include <asio/steady_timer.hpp>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config/enemy_types_config.hpp"
#include "config/item_types_config.hpp"
#include "config/player_roles_config.hpp"
#include "config/server_config.hpp"
#include "config/upgrade_config.hpp"
#include "message.pb.h"

// 游戏管理器：负责场景初始化、玩家状态更新与同步
class UdpServer;
class TcpSession;

class GameManager {
 public:
  static GameManager& Instance();

  GameManager(const GameManager&) = delete;
  GameManager& operator=(const GameManager&) = delete;

  struct SceneCreatePlayer {
    uint32_t player_id = 0;
    std::string player_name;
    bool is_host = false;
    std::weak_ptr<TcpSession> session;
  };

  struct SceneCreateSnapshot {
    uint32_t room_id = 0;
    bool is_playing = false;
    std::vector<SceneCreatePlayer> players;
  };

  // 为指定房间创建场景并生成初始 SceneInfo（覆盖已存在的同房间场景）
  lawnmower::SceneInfo CreateScene(const SceneCreateSnapshot& snapshot);

  // 构造完整的状态同步（通常用于游戏开始时的全量同步）
  [[nodiscard]] bool BuildFullState(uint32_t room_id,
                                    lawnmower::S2C_GameStateSync* sync);

  // 注册 io_context（用于定时广播状态同步）
  void SetIoContext(asio::io_context* io);

  // 注册 UDP 服务（用于高频同步）
  void SetUdpServer(UdpServer* udp);
  [[nodiscard]] UdpServer* GetUdpServer() const { return udp_server_; }
  [[nodiscard]] asio::io_context* GetIoContext() const { return io_context_; }
  void SetConfig(const ServerConfig& cfg) { config_ = cfg; }
  void SetPlayerRolesConfig(const PlayerRolesConfig& cfg) {
    player_roles_config_ = cfg;
  }
  void SetEnemyTypesConfig(const EnemyTypesConfig& cfg) {
    enemy_types_config_ = cfg;
  }
  void SetItemsConfig(const ItemsConfig& cfg) { items_config_ = cfg; }
  void SetUpgradeConfig(const UpgradeConfig& cfg) { upgrade_config_ = cfg; }

  // 在游戏开始后为房间启动固定逻辑帧循环与状态同步
  void StartGameLoop(uint32_t room_id);

  // 处理玩家输入，将其入队等待逻辑帧处理；返回 false 表示未找到玩家或场景
  [[nodiscard]] bool HandlePlayerInput(uint32_t player_id,
                                       const lawnmower::C2S_PlayerInput& input,
                                       uint32_t* room_id);
  [[nodiscard]] bool HandleUpgradeRequestAck(
      uint32_t player_id, const lawnmower::C2S_UpgradeRequestAck& request);
  [[nodiscard]] bool HandleUpgradeOptionsAck(
      uint32_t player_id, const lawnmower::C2S_UpgradeOptionsAck& request);
  [[nodiscard]] bool HandleUpgradeSelect(
      uint32_t player_id, const lawnmower::C2S_UpgradeSelect& request);
  [[nodiscard]] bool HandleUpgradeRefreshRequest(
      uint32_t player_id, const lawnmower::C2S_UpgradeRefreshRequest& request);

  // 判断给定坐标是否在指定房间的地图边界内（基于场景宽高）
  [[nodiscard]] bool IsInsideMap(uint32_t room_id,
                                 const lawnmower::Vector2& position) const;

  // 玩家断线/离开时清理场景信息
  void RemovePlayer(uint32_t player_id);
  // 标记玩家断线（保留运行时状态）
  bool MarkPlayerDisconnected(uint32_t player_id);
  struct ReconnectSnapshot {
    uint32_t room_id = 0;      // 房间ID
    uint64_t server_tick = 0;  // 服务器当前tick
    bool is_paused = false;    // 是否暂停
    std::string player_name;   // 玩家名
  };
  // 重连后恢复玩家状态（重置输入基线等）
  [[nodiscard]] bool TryReconnectPlayer(uint32_t player_id, uint32_t room_id,
                                        uint32_t last_input_seq,
                                        uint32_t last_server_tick,
                                        ReconnectSnapshot* out);

 private:
  GameManager() = default;

  // clang-format off
#include "game/managers/internal/game_manager_private_types.inc"
#include "game/managers/internal/game_manager_private_tick_types.inc"
#include "game/managers/internal/game_manager_private_combat_types.inc"
#include "game/managers/internal/game_manager_private_methods.inc"
#include "game/managers/internal/game_manager_private_members.inc"
  // clang-format on
};
