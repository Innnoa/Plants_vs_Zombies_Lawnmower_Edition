#include "game/managers/game_manager.hpp"

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
