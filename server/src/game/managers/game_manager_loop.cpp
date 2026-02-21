#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>

#include "game/managers/game_manager.hpp"

namespace {
constexpr uint32_t kFullSyncIntervalTicks = 180;  // 全量同步时间间隔
}  // namespace

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
