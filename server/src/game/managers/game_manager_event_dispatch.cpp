#include "internal/game_manager_event_dispatch.hpp"

#include <span>
#include <spdlog/spdlog.h>

#include "game/managers/room_manager.hpp"
#include "internal/game_manager_internal_utils.hpp"
#include "network/tcp/tcp_session.hpp"

namespace {
using game_manager_internal::NowMs;

struct TickEventMessages {
  bool has_projectile_spawn = false;
  bool has_projectile_despawn = false;
  bool has_dropped_items = false;
  bool has_enemy_attack_state = false;
  lawnmower::S2C_ProjectileSpawn projectile_spawn_msg;
  lawnmower::S2C_ProjectileDespawn projectile_despawn_msg;
  lawnmower::S2C_DroppedItem dropped_item_msg;
  lawnmower::S2C_EnemyAttackStateSync enemy_attack_state_msg;
};

template <typename TMessage>
void FillTickEventSyncTime(TMessage* message, uint64_t event_now_count,
                           uint64_t event_tick) {
  if (message == nullptr) {
    return;
  }
  message->mutable_sync_time()->set_server_time(event_now_count);
  message->mutable_sync_time()->set_tick(static_cast<uint32_t>(event_tick));
}

void BuildTickEventMessages(
    uint32_t room_id, uint64_t event_tick, uint32_t event_wave_id,
    uint64_t event_now_count,
    const std::vector<lawnmower::ProjectileState>& projectile_spawns,
    const std::vector<lawnmower::ProjectileDespawn>& projectile_despawns,
    const std::vector<lawnmower::ItemState>& dropped_items,
    const std::vector<lawnmower::EnemyAttackStateDelta>& enemy_attack_states,
    TickEventMessages* out) {
  if (out == nullptr) {
    return;
  }

  if (!projectile_spawns.empty()) {
    out->has_projectile_spawn = true;
    auto& msg = out->projectile_spawn_msg;
    msg.set_room_id(room_id);
    FillTickEventSyncTime(&msg, event_now_count, event_tick);
    msg.mutable_projectiles()->Reserve(
        static_cast<int>(projectile_spawns.size()));
    for (const auto& spawn : projectile_spawns) {
      *msg.add_projectiles() = spawn;
    }
  }

  if (!projectile_despawns.empty()) {
    out->has_projectile_despawn = true;
    auto& msg = out->projectile_despawn_msg;
    msg.set_room_id(room_id);
    FillTickEventSyncTime(&msg, event_now_count, event_tick);
    msg.mutable_projectiles()->Reserve(
        static_cast<int>(projectile_despawns.size()));
    for (const auto& despawn : projectile_despawns) {
      *msg.add_projectiles() = despawn;
    }
  }

  if (!dropped_items.empty()) {
    out->has_dropped_items = true;
    auto& msg = out->dropped_item_msg;
    msg.set_room_id(room_id);
    FillTickEventSyncTime(&msg, event_now_count, event_tick);
    msg.set_source_enemy_id(0);
    msg.set_wave_id(event_wave_id);
    msg.mutable_items()->Reserve(static_cast<int>(dropped_items.size()));
    for (const auto& item : dropped_items) {
      *msg.add_items() = item;
    }
  }

  if (!enemy_attack_states.empty()) {
    out->has_enemy_attack_state = true;
    auto& msg = out->enemy_attack_state_msg;
    msg.set_room_id(room_id);
    FillTickEventSyncTime(&msg, event_now_count, event_tick);
    msg.mutable_enemies()->Reserve(
        static_cast<int>(enemy_attack_states.size()));
    for (const auto& delta : enemy_attack_states) {
      *msg.add_enemies() = delta;
    }
  }
}

void LogGameOverSummary(
    uint32_t room_id, const std::optional<lawnmower::S2C_GameOver>& game_over) {
  if (!game_over.has_value()) {
    return;
  }
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

bool HasTickEventsToBroadcast(
    const TickEventMessages& messages,
    const std::vector<lawnmower::S2C_PlayerHurt>& player_hurts,
    const std::vector<lawnmower::S2C_EnemyDied>& enemy_dieds,
    const std::vector<lawnmower::S2C_PlayerLevelUp>& level_ups,
    const std::optional<lawnmower::S2C_GameOver>& game_over,
    const std::optional<lawnmower::S2C_UpgradeRequest>& upgrade_request) {
  return messages.has_projectile_spawn || messages.has_projectile_despawn ||
         messages.has_dropped_items || messages.has_enemy_attack_state ||
         !player_hurts.empty() || !enemy_dieds.empty() || !level_ups.empty() ||
         game_over.has_value() || upgrade_request.has_value();
}

void SendTickEventsToSessions(
    std::span<const std::weak_ptr<TcpSession>> sessions,
    const TickEventMessages& messages,
    const std::vector<lawnmower::S2C_PlayerHurt>& player_hurts,
    const std::vector<lawnmower::S2C_EnemyDied>& enemy_dieds,
    const std::vector<lawnmower::S2C_PlayerLevelUp>& level_ups,
    const std::optional<lawnmower::S2C_GameOver>& game_over,
    const std::optional<lawnmower::S2C_UpgradeRequest>& upgrade_request) {
  for (const auto& weak_session : sessions) {
    auto session = weak_session.lock();
    if (!session) {
      continue;
    }
    if (messages.has_projectile_spawn) {
      session->SendProto(lawnmower::MessageType::MSG_S2C_PROJECTILE_SPAWN,
                         messages.projectile_spawn_msg);
    }
    if (messages.has_projectile_despawn) {
      session->SendProto(lawnmower::MessageType::MSG_S2C_PROJECTILE_DESPAWN,
                         messages.projectile_despawn_msg);
    }
    if (messages.has_dropped_items) {
      session->SendProto(lawnmower::MessageType::MSG_S2C_DROPPED_ITEM,
                         messages.dropped_item_msg);
    }
    if (messages.has_enemy_attack_state) {
      session->SendProto(
          lawnmower::MessageType::MSG_S2C_ENEMY_ATTACK_STATE_SYNC,
          messages.enemy_attack_state_msg);
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
      session->SendProto(lawnmower::MessageType::MSG_S2C_GAME_OVER, *game_over);
    }
  }
}
}  // namespace

namespace game_manager_event_dispatch {

void DispatchTickEvents(
    uint32_t room_id, uint64_t event_tick, uint32_t event_wave_id,
    const std::vector<lawnmower::ProjectileState>& projectile_spawns,
    const std::vector<lawnmower::ProjectileDespawn>& projectile_despawns,
    const std::vector<lawnmower::ItemState>& dropped_items,
    const std::vector<lawnmower::EnemyAttackStateDelta>& enemy_attack_states,
    const std::vector<lawnmower::S2C_PlayerHurt>& player_hurts,
    const std::vector<lawnmower::S2C_EnemyDied>& enemy_dieds,
    const std::vector<lawnmower::S2C_PlayerLevelUp>& level_ups,
    const std::optional<lawnmower::S2C_GameOver>& game_over,
    const std::optional<lawnmower::S2C_UpgradeRequest>& upgrade_request) {
  const uint64_t event_now_count = static_cast<uint64_t>(NowMs().count());
  TickEventMessages tick_event_messages;
  BuildTickEventMessages(room_id, event_tick, event_wave_id, event_now_count,
                         projectile_spawns, projectile_despawns, dropped_items,
                         enemy_attack_states, &tick_event_messages);

  LogGameOverSummary(room_id, game_over);

  if (HasTickEventsToBroadcast(tick_event_messages, player_hurts, enemy_dieds,
                               level_ups, game_over, upgrade_request)) {
    const auto sessions = RoomManager::Instance().GetRoomSessions(room_id);
    SendTickEventsToSessions(sessions, tick_event_messages, player_hurts,
                             enemy_dieds, level_ups, game_over,
                             upgrade_request);
  }
}

}  // namespace game_manager_event_dispatch
