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

#include "config/server_config.hpp"
#include "game/managers/room_manager.hpp"
#include "message.pb.h"

// 游戏管理器：负责场景初始化、玩家状态更新与同步
class UdpServer;

class GameManager {
 public:
  static GameManager& Instance();

  GameManager(const GameManager&) = delete;
  GameManager& operator=(const GameManager&) = delete;

  // 为指定房间创建场景并生成初始 SceneInfo（覆盖已存在的同房间场景）
  lawnmower::SceneInfo CreateScene(const RoomManager::RoomSnapshot& snapshot);

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

  // 在游戏开始后为房间启动固定逻辑帧循环与状态同步
  void StartGameLoop(uint32_t room_id);

  // 处理玩家输入，将其入队等待逻辑帧处理；返回 false 表示未找到玩家或场景
  [[nodiscard]] bool HandlePlayerInput(uint32_t player_id,
                                       const lawnmower::C2S_PlayerInput& input,
                                       uint32_t* room_id);

  // 判断给定坐标是否在指定房间的地图边界内（基于场景宽高）
  [[nodiscard]] bool IsInsideMap(uint32_t room_id,
                                 const lawnmower::Vector2& position) const;

  // 玩家断线/离开时清理场景信息
  void RemovePlayer(uint32_t player_id);

 private:
  GameManager() = default;

  struct SceneConfig {  // 默认场景配置
    uint32_t width = 2000;
    uint32_t height = 2000;
    uint32_t tick_rate = 60;
    uint32_t state_sync_rate = 30;
    float move_speed = 200.0f;
  };

  struct PlayerRuntime {
    lawnmower::PlayerState state;
    std::string player_name;
    uint32_t last_input_seq = 0;
    std::deque<lawnmower::C2S_PlayerInput> pending_inputs;
    bool wants_attacking = false;
    double attack_cooldown_seconds = 0.0;
    int32_t kill_count = 0;
    int32_t damage_dealt = 0;
    bool low_freq_dirty = false;
    bool dirty = false;
  };

  struct EnemyRuntime {
    lawnmower::EnemyState state;
    uint32_t target_player_id = 0;
    std::vector<std::pair<int, int>> path;
    std::size_t path_index = 0;
    double replan_elapsed = 0.0;
    double attack_cooldown_seconds = 0.0;
    double dead_elapsed_seconds = 0.0;
    uint32_t force_sync_left = 0;
    bool dirty = false;
  };

  struct ProjectileRuntime {
    uint32_t projectile_id = 0;
    uint32_t owner_player_id = 0;
    float x = 0.0f;
    float y = 0.0f;
    float dir_x = 1.0f;
    float dir_y = 0.0f;
    float rotation = 0.0f;
    float speed = 0.0f;
    int32_t damage = 0;
    bool has_buff = false;
    uint32_t buff_id = 0;
    bool is_friendly = true;
    double remaining_seconds = 0.0;
  };

  struct Scene {
    SceneConfig config;
    std::unordered_map<uint32_t, PlayerRuntime>
        players;  // 玩家对应玩家运行状态
    std::unordered_map<uint32_t, EnemyRuntime> enemies;
    std::unordered_map<uint32_t, ProjectileRuntime> projectiles;
    uint32_t next_enemy_id = 1;
    uint32_t next_projectile_id = 1;
    uint32_t wave_id = 0;
    double elapsed = 0.0;
    double spawn_elapsed = 0.0;
    uint32_t rng_state = 1;
    bool game_over = false;
    int nav_cells_x = 0;
    int nav_cells_y = 0;
    std::vector<int> nav_came_from;
    std::vector<float> nav_g_score;
    std::vector<uint8_t> nav_closed;
    uint64_t tick = 0;
    double sync_accumulator = 0.0;  // 以秒计
    double full_sync_elapsed = 0.0;
    std::chrono::steady_clock::time_point last_tick_time;
    std::chrono::duration<double> tick_interval;
    std::chrono::duration<double> sync_interval;
    std::chrono::duration<double> full_sync_interval;
    std::shared_ptr<asio::steady_timer> loop_timer;
  };

  static constexpr uint32_t kEnemySpawnForceSyncCount =
      6;  // 新刷怪多发几次，降低 UDP 丢包影响
  static uint32_t NextRng(uint32_t* state);
  static float NextRngUnitFloat(uint32_t* state);

  SceneConfig BuildDefaultConfig() const;
  void PlacePlayers(const RoomManager::RoomSnapshot& snapshot, Scene* scene);
  void ProcessSceneTick(uint32_t room_id, double tick_interval_seconds);
  void ProcessCombatAndProjectiles(
      Scene& scene, double dt_seconds,
      std::vector<lawnmower::S2C_PlayerHurt>* player_hurts,
      std::vector<lawnmower::S2C_EnemyDied>* enemy_dieds,
      std::vector<lawnmower::S2C_PlayerLevelUp>* level_ups,
      std::optional<lawnmower::S2C_GameOver>* game_over,
      std::vector<lawnmower::ProjectileState>* projectile_spawns,
      std::vector<lawnmower::ProjectileDespawn>* projectile_despawns,
      bool* has_dirty);
  void ScheduleGameTick(uint32_t room_id, std::chrono::microseconds interval,
                        const std::shared_ptr<asio::steady_timer>& timer,
                        double tick_interval_seconds);
  [[nodiscard]] bool ShouldRescheduleTick(
      uint32_t room_id, const std::shared_ptr<asio::steady_timer>& timer) const;
  void StopGameLoop(uint32_t room_id);
  lawnmower::Vector2 ClampToMap(const SceneConfig& cfg, float x, float y) const;

  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, Scene> scenes_;           // room_id -> scene
  std::unordered_map<uint32_t, uint32_t> player_scene_;  // player_id -> room_id

  asio::io_context* io_context_ = nullptr;
  UdpServer* udp_server_ = nullptr;
  ServerConfig config_;
};
