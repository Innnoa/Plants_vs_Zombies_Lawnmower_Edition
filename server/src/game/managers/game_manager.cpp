#include "game/managers/game_manager.hpp"

#include <algorithm>
#include <thread>

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
  cfg.sync_enemy_near_distance = config_.sync_enemy_near_distance;
  cfg.sync_enemy_far_distance = config_.sync_enemy_far_distance;
  cfg.sync_enemy_medium_stride = config_.sync_enemy_medium_stride;
  cfg.sync_enemy_far_stride = config_.sync_enemy_far_stride;
  cfg.room_sync_object_budget_per_tick = config_.room_sync_object_budget_per_tick;
  cfg.room_event_entry_budget_per_tick = config_.room_event_entry_budget_per_tick;
  cfg.room_packet_budget_per_tick = config_.room_packet_budget_per_tick;
  cfg.move_speed = config_.move_speed;
  return cfg;
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
void GameManager::SetIoContext(asio::io_context* io) {
  io_context_ = io;
  if (io_context_ == nullptr) {
    logic_thread_pool_.reset();
    dispatch_thread_pool_.reset();
    dispatch_strand_.reset();
    return;
  }
  const unsigned int hardware_threads =
      std::max(1u, std::thread::hardware_concurrency());
  const unsigned int logic_workers = std::max(1u, std::min(4u, hardware_threads));
  logic_thread_pool_ = std::make_shared<asio::thread_pool>(logic_workers);
  dispatch_thread_pool_ = std::make_shared<asio::thread_pool>(1);
  dispatch_strand_ =
      std::make_shared<asio::strand<asio::thread_pool::executor_type>>(
          asio::make_strand(*dispatch_thread_pool_));
}

// 设置UDP服务器
void GameManager::SetUdpServer(UdpServer* udp) { udp_server_ = udp; }
