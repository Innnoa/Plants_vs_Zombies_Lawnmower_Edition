#include "internal/game_manager_event_dispatch.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <algorithm>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <span>
#include <spdlog/spdlog.h>

#include "internal/game_manager_internal_utils.hpp"
#include "network/tcp/tcp_session.hpp"
#include "network/shared/shared_string_pool.hpp"

namespace {
using game_manager_internal::NowMs;

struct PreparedPacket {
  lawnmower::MessageType type = lawnmower::MessageType::MSG_UNKNOWN;
  std::shared_ptr<const std::string> framed;
  std::size_t payload_len = 0;
  std::size_t body_len = 0;
  uint32_t entry_count = 0;
};

PreparedPacket BuildPreparedPacket(lawnmower::MessageType type,
                                   const google::protobuf::Message& message) {
  lawnmower::Packet packet;
  packet.set_msg_type(type);
  const auto payload_len = static_cast<std::size_t>(message.ByteSizeLong());
  auto* payload = packet.mutable_payload();
  payload->resize(payload_len);
  if (payload_len > 0 &&
      !message.SerializeToArray(payload->data(), static_cast<int>(payload_len))) {
    return {type, nullptr, 0, 0};
  }
  const auto body_len = static_cast<std::size_t>(packet.ByteSizeLong());
  const uint32_t net_len = htonl(static_cast<uint32_t>(body_len));
  auto framed = network_shared::AcquireSharedString(sizeof(net_len) + body_len);
  framed->resize(sizeof(net_len) + body_len);
  std::memcpy(framed->data(), &net_len, sizeof(net_len));
  if (body_len > 0 &&
      !packet.SerializeToArray(framed->data() + sizeof(net_len),
                               static_cast<int>(body_len))) {
    return {type, nullptr, 0, 0};
  }
  return {type, framed, payload_len, body_len, 0};
}

void AppendPreparedPacket(std::vector<PreparedPacket>* out,
                          lawnmower::MessageType type,
                          const google::protobuf::Message& message,
                          uint32_t entry_count = 0) {
  if (out == nullptr) {
    return;
  }
  auto packet = BuildPreparedPacket(type, message);
  if (packet.framed == nullptr) {
    spdlog::warn("构造事件包失败 type={}", lawnmower::MessageType_Name(type));
    return;
  }
  packet.entry_count = entry_count;
  out->push_back(std::move(packet));
}

struct TickEventMessages {
  bool has_projectile_spawn = false;
  bool has_projectile_despawn = false;
  bool has_dropped_items = false;
  bool has_enemy_attack_state = false;
  bool has_player_hurt_batch = false;
  bool has_enemy_died_batch = false;
  bool has_level_up_batch = false;
  bool has_upgrade_request = false;
  bool has_game_over = false;
  lawnmower::S2C_ProjectileSpawn projectile_spawn_msg;
  lawnmower::S2C_ProjectileDespawn projectile_despawn_msg;
  lawnmower::S2C_DroppedItem dropped_item_msg;
  lawnmower::S2C_EnemyAttackStateSync enemy_attack_state_msg;
  lawnmower::S2C_PlayerHurtBatch player_hurt_batch_msg;
  lawnmower::S2C_EnemyDiedBatch enemy_died_batch_msg;
  lawnmower::S2C_PlayerLevelUpBatch level_up_batch_msg;
  lawnmower::S2C_UpgradeRequest upgrade_request_msg;
  lawnmower::S2C_GameOver game_over_msg;
};

struct DeferredNonCriticalEventBacklog {
  std::deque<PreparedPacket> projectile_spawns;
  std::deque<PreparedPacket> projectile_despawns;
  std::deque<PreparedPacket> dropped_items;
  std::deque<PreparedPacket> enemy_attack_states;
};

std::unordered_map<uint32_t, DeferredNonCriticalEventBacklog>& DeferredEventBacklogs() {
  static auto* backlogs =
      new std::unordered_map<uint32_t, DeferredNonCriticalEventBacklog>();
  return *backlogs;
}

DeferredNonCriticalEventBacklog& DeferredBacklogForRoom(uint32_t room_id) {
  return DeferredEventBacklogs()[room_id];
}

void ClearDeferredBacklog(uint32_t room_id) {
  DeferredEventBacklogs().erase(room_id);
}

uint32_t CountEntries(const lawnmower::S2C_ProjectileSpawn& msg) {
  return static_cast<uint32_t>(msg.projectiles_size());
}

uint32_t CountEntries(const lawnmower::S2C_ProjectileDespawn& msg) {
  return static_cast<uint32_t>(msg.projectiles_size());
}

uint32_t CountEntries(const lawnmower::S2C_DroppedItem& msg) {
  return static_cast<uint32_t>(msg.items_size());
}

uint32_t CountEntries(const lawnmower::S2C_EnemyAttackStateSync& msg) {
  return static_cast<uint32_t>(msg.enemies_size());
}

bool IsMessageEmpty(const lawnmower::S2C_ProjectileSpawn& msg) {
  return msg.projectiles_size() == 0;
}

bool IsMessageEmpty(const lawnmower::S2C_ProjectileDespawn& msg) {
  return msg.projectiles_size() == 0;
}

bool IsMessageEmpty(const lawnmower::S2C_DroppedItem& msg) {
  return msg.items_size() == 0;
}

bool IsMessageEmpty(const lawnmower::S2C_EnemyAttackStateSync& msg) {
  return msg.enemies_size() == 0;
}

lawnmower::S2C_ProjectileSpawn ExtractLeadingEntries(
    lawnmower::S2C_ProjectileSpawn* msg, uint32_t count) {
  lawnmower::S2C_ProjectileSpawn out;
  if (msg == nullptr || count == 0 || msg->projectiles_size() == 0) {
    return out;
  }
  out.set_room_id(msg->room_id());
  if (msg->has_sync_time()) {
    *out.mutable_sync_time() = msg->sync_time();
  }
  const int take = std::min<int>(static_cast<int>(count), msg->projectiles_size());
  for (int i = 0; i < take; ++i) {
    *out.add_projectiles() = msg->projectiles(i);
  }
  msg->mutable_projectiles()->DeleteSubrange(0, take);
  return out;
}

lawnmower::S2C_ProjectileDespawn ExtractLeadingEntries(
    lawnmower::S2C_ProjectileDespawn* msg, uint32_t count) {
  lawnmower::S2C_ProjectileDespawn out;
  if (msg == nullptr || count == 0 || msg->projectiles_size() == 0) {
    return out;
  }
  out.set_room_id(msg->room_id());
  if (msg->has_sync_time()) {
    *out.mutable_sync_time() = msg->sync_time();
  }
  const int take = std::min<int>(static_cast<int>(count), msg->projectiles_size());
  for (int i = 0; i < take; ++i) {
    *out.add_projectiles() = msg->projectiles(i);
  }
  msg->mutable_projectiles()->DeleteSubrange(0, take);
  return out;
}

lawnmower::S2C_DroppedItem ExtractLeadingEntries(
    lawnmower::S2C_DroppedItem* msg, uint32_t count) {
  lawnmower::S2C_DroppedItem out;
  if (msg == nullptr || count == 0 || msg->items_size() == 0) {
    return out;
  }
  out.set_room_id(msg->room_id());
  out.set_source_enemy_id(msg->source_enemy_id());
  out.set_wave_id(msg->wave_id());
  if (msg->has_sync_time()) {
    *out.mutable_sync_time() = msg->sync_time();
  }
  const int take = std::min<int>(static_cast<int>(count), msg->items_size());
  for (int i = 0; i < take; ++i) {
    *out.add_items() = msg->items(i);
  }
  msg->mutable_items()->DeleteSubrange(0, take);
  return out;
}

lawnmower::S2C_EnemyAttackStateSync ExtractLeadingEntries(
    lawnmower::S2C_EnemyAttackStateSync* msg, uint32_t count) {
  lawnmower::S2C_EnemyAttackStateSync out;
  if (msg == nullptr || count == 0 || msg->enemies_size() == 0) {
    return out;
  }
  out.set_room_id(msg->room_id());
  if (msg->has_sync_time()) {
    *out.mutable_sync_time() = msg->sync_time();
  }
  const int take = std::min<int>(static_cast<int>(count), msg->enemies_size());
  for (int i = 0; i < take; ++i) {
    *out.add_enemies() = msg->enemies(i);
  }
  msg->mutable_enemies()->DeleteSubrange(0, take);
  return out;
}

template <typename TMessage>
void BuildPreparedNonCriticalChunks(lawnmower::MessageType type,
                                    const TMessage& message,
                                    uint32_t max_entries_per_chunk,
                                    std::deque<PreparedPacket>* out) {
  if (out == nullptr || IsMessageEmpty(message)) {
    return;
  }
  TMessage remaining = message;
  const uint32_t chunk_entries = std::max<uint32_t>(1, max_entries_per_chunk);
  while (!IsMessageEmpty(remaining)) {
    TMessage head = ExtractLeadingEntries(&remaining, chunk_entries);
    if (IsMessageEmpty(head)) {
      break;
    }
    std::vector<PreparedPacket> prepared;
    prepared.reserve(1);
    AppendPreparedPacket(&prepared, type, head, CountEntries(head));
    if (!prepared.empty()) {
      out->push_back(std::move(prepared.front()));
    }
  }
}

template <typename TMessage>
void CollectPreparedMessages(const std::vector<TMessage>& messages,
                             lawnmower::MessageType type,
                             std::vector<PreparedPacket>* out) {
  if (out == nullptr) {
    return;
  }
  for (const auto& message : messages) {
    AppendPreparedPacket(out, type, message, CountEntries(message));
  }
}

template <typename TPacket>
void DrainDeferredMessages(std::deque<TPacket>* backlog,
                           std::vector<PreparedPacket>* ready,
                           uint32_t* remaining_entries,
                           uint32_t* remaining_messages) {
  if (backlog == nullptr || ready == nullptr || remaining_entries == nullptr ||
      remaining_messages == nullptr) {
    return;
  }

  while (!backlog->empty() && *remaining_messages > 0) {
    const auto& current = backlog->front();
    if (current.entry_count > *remaining_entries) {
      break;
    }
    *remaining_entries -= current.entry_count;
    *remaining_messages -= 1;
    ready->push_back(current);
    backlog->pop_front();
  }
}

template <typename TMessage>
void QueueCurrentMessage(const TMessage& current,
                         lawnmower::MessageType type,
                         uint32_t max_entries_per_chunk,
                         std::deque<PreparedPacket>* backlog,
                         std::vector<PreparedPacket>* ready,
                         uint32_t* remaining_entries,
                         uint32_t* remaining_messages) {
  if (backlog == nullptr || ready == nullptr || remaining_entries == nullptr ||
      remaining_messages == nullptr || IsMessageEmpty(current)) {
    return;
  }

  std::deque<PreparedPacket> prepared_chunks;
  BuildPreparedNonCriticalChunks(type, current, max_entries_per_chunk,
                                 &prepared_chunks);
  while (!prepared_chunks.empty()) {
    auto packet = std::move(prepared_chunks.front());
    prepared_chunks.pop_front();
    if (*remaining_messages > 0 && packet.entry_count <= *remaining_entries) {
      *remaining_entries -= packet.entry_count;
      *remaining_messages -= 1;
      ready->push_back(std::move(packet));
      continue;
    }
    backlog->push_back(std::move(packet));
  }
}

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
    const std::vector<lawnmower::S2C_PlayerHurt>& player_hurts,
    const std::vector<lawnmower::S2C_EnemyDied>& enemy_dieds,
    const std::vector<lawnmower::S2C_PlayerLevelUp>& level_ups,
    const std::optional<lawnmower::S2C_GameOver>& game_over,
    const std::optional<lawnmower::S2C_UpgradeRequest>& upgrade_request,
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

  if (!player_hurts.empty()) {
    out->has_player_hurt_batch = true;
    auto& msg = out->player_hurt_batch_msg;
    msg.set_room_id(room_id);
    FillTickEventSyncTime(&msg, event_now_count, event_tick);
    msg.mutable_events()->Reserve(static_cast<int>(player_hurts.size()));
    for (const auto& hurt : player_hurts) {
      *msg.add_events() = hurt;
    }
  }

  if (!enemy_dieds.empty()) {
    out->has_enemy_died_batch = true;
    auto& msg = out->enemy_died_batch_msg;
    msg.set_room_id(room_id);
    FillTickEventSyncTime(&msg, event_now_count, event_tick);
    msg.mutable_events()->Reserve(static_cast<int>(enemy_dieds.size()));
    for (const auto& died : enemy_dieds) {
      *msg.add_events() = died;
    }
  }

  if (!level_ups.empty()) {
    out->has_level_up_batch = true;
    auto& msg = out->level_up_batch_msg;
    msg.set_room_id(room_id);
    FillTickEventSyncTime(&msg, event_now_count, event_tick);
    msg.mutable_events()->Reserve(static_cast<int>(level_ups.size()));
    for (const auto& level_up : level_ups) {
      *msg.add_events() = level_up;
    }
  }

  if (upgrade_request.has_value()) {
    out->has_upgrade_request = true;
    out->upgrade_request_msg = *upgrade_request;
    FillTickEventSyncTime(&out->upgrade_request_msg, event_now_count,
                          event_tick);
  }

  if (game_over.has_value()) {
    out->has_game_over = true;
    out->game_over_msg = *game_over;
    out->game_over_msg.set_room_id(room_id);
    FillTickEventSyncTime(&out->game_over_msg, event_now_count, event_tick);
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
    const std::vector<lawnmower::S2C_ProjectileSpawn>& projectile_spawns,
    const std::vector<lawnmower::S2C_ProjectileDespawn>& projectile_despawns,
    const std::vector<lawnmower::S2C_DroppedItem>& dropped_items,
    const std::vector<lawnmower::S2C_EnemyAttackStateSync>& enemy_attack_states) {
  return messages.has_player_hurt_batch || messages.has_enemy_died_batch ||
         messages.has_level_up_batch || messages.has_game_over ||
         messages.has_upgrade_request || !projectile_spawns.empty() ||
         !projectile_despawns.empty() || !dropped_items.empty() ||
         !enemy_attack_states.empty();
}

void SendTickEventsToSessions(
    std::span<const std::weak_ptr<TcpSession>> sessions,
    const std::vector<PreparedPacket>& prepared_packets) {
  for (const auto& weak_session : sessions) {
    auto session = weak_session.lock();
    if (!session) {
      continue;
    }
    for (const auto& packet : prepared_packets) {
      session->SendFramedPacket(packet.framed, packet.type, packet.payload_len,
                                packet.body_len);
    }
  }
}

void ScheduleDeferredNonCriticalEvents(
    uint32_t room_id, const TickEventMessages& messages,
    uint32_t noncritical_event_entries_budget,
    uint32_t noncritical_event_messages_budget,
    uint32_t max_entries_per_chunk,
    std::vector<PreparedPacket>* prepared_packets) {
  if (prepared_packets == nullptr) {
    return;
  }

  auto& backlog = DeferredBacklogForRoom(room_id);
  uint32_t remaining_entries = noncritical_event_entries_budget;
  uint32_t remaining_messages = noncritical_event_messages_budget;

  DrainDeferredMessages(&backlog.projectile_spawns, prepared_packets,
                        &remaining_entries, &remaining_messages);
  DrainDeferredMessages(&backlog.projectile_despawns, prepared_packets,
                        &remaining_entries, &remaining_messages);
  DrainDeferredMessages(&backlog.dropped_items, prepared_packets,
                        &remaining_entries, &remaining_messages);
  DrainDeferredMessages(&backlog.enemy_attack_states, prepared_packets,
                        &remaining_entries, &remaining_messages);

  if (messages.has_projectile_spawn) {
    QueueCurrentMessage(messages.projectile_spawn_msg,
                        lawnmower::MessageType::MSG_S2C_PROJECTILE_SPAWN,
                        max_entries_per_chunk, &backlog.projectile_spawns,
                        prepared_packets, &remaining_entries,
                        &remaining_messages);
  }
  if (messages.has_projectile_despawn) {
    QueueCurrentMessage(messages.projectile_despawn_msg,
                        lawnmower::MessageType::MSG_S2C_PROJECTILE_DESPAWN,
                        max_entries_per_chunk, &backlog.projectile_despawns,
                        prepared_packets, &remaining_entries,
                        &remaining_messages);
  }
  if (messages.has_dropped_items) {
    QueueCurrentMessage(messages.dropped_item_msg,
                        lawnmower::MessageType::MSG_S2C_DROPPED_ITEM,
                        max_entries_per_chunk, &backlog.dropped_items,
                        prepared_packets, &remaining_entries,
                        &remaining_messages);
  }
  if (messages.has_enemy_attack_state) {
    QueueCurrentMessage(messages.enemy_attack_state_msg,
                        lawnmower::MessageType::MSG_S2C_ENEMY_ATTACK_STATE_SYNC,
                        max_entries_per_chunk, &backlog.enemy_attack_states,
                        prepared_packets, &remaining_entries,
                        &remaining_messages);
  }
}
}  // namespace

namespace game_manager_event_dispatch {

uint32_t DispatchTickEvents(
    uint32_t room_id, uint64_t event_tick, uint32_t event_wave_id,
    const std::vector<lawnmower::ProjectileState>& projectile_spawns,
    const std::vector<lawnmower::ProjectileDespawn>& projectile_despawns,
    const std::vector<lawnmower::ItemState>& dropped_items,
    const std::vector<lawnmower::EnemyAttackStateDelta>& enemy_attack_states,
    const std::vector<lawnmower::S2C_PlayerHurt>& player_hurts,
    const std::vector<lawnmower::S2C_EnemyDied>& enemy_dieds,
    const std::vector<lawnmower::S2C_PlayerLevelUp>& level_ups,
    const std::optional<lawnmower::S2C_GameOver>& game_over,
    const std::vector<std::weak_ptr<TcpSession>>& sessions,
    const std::optional<lawnmower::S2C_UpgradeRequest>& upgrade_request,
    uint32_t noncritical_event_entries_budget,
    uint32_t noncritical_event_messages_budget) {
  const uint64_t event_now_count = static_cast<uint64_t>(NowMs().count());
  TickEventMessages tick_event_messages;
  BuildTickEventMessages(room_id, event_tick, event_wave_id, event_now_count,
                         projectile_spawns, projectile_despawns, dropped_items,
                         enemy_attack_states, player_hurts, enemy_dieds,
                         level_ups, game_over, upgrade_request,
                         &tick_event_messages);

  std::vector<PreparedPacket> prepared_packets;
  prepared_packets.reserve(9);
  ScheduleDeferredNonCriticalEvents(
      room_id, tick_event_messages, noncritical_event_entries_budget,
      noncritical_event_messages_budget,
      std::max<uint32_t>(1, noncritical_event_entries_budget),
      &prepared_packets);
  if (tick_event_messages.has_player_hurt_batch) {
    AppendPreparedPacket(&prepared_packets,
                         lawnmower::MessageType::MSG_S2C_PLAYER_HURT_BATCH,
                         tick_event_messages.player_hurt_batch_msg,
                         static_cast<uint32_t>(tick_event_messages.player_hurt_batch_msg.events_size()));
  }
  if (tick_event_messages.has_enemy_died_batch) {
    AppendPreparedPacket(&prepared_packets,
                         lawnmower::MessageType::MSG_S2C_ENEMY_DIED_BATCH,
                         tick_event_messages.enemy_died_batch_msg,
                         static_cast<uint32_t>(tick_event_messages.enemy_died_batch_msg.events_size()));
  }
  if (tick_event_messages.has_level_up_batch) {
    AppendPreparedPacket(&prepared_packets,
                         lawnmower::MessageType::MSG_S2C_PLAYER_LEVEL_UP_BATCH,
                         tick_event_messages.level_up_batch_msg,
                         static_cast<uint32_t>(tick_event_messages.level_up_batch_msg.events_size()));
  }
  if (tick_event_messages.has_upgrade_request) {
    AppendPreparedPacket(&prepared_packets,
                         lawnmower::MessageType::MSG_S2C_UPGRADE_REQUEST,
                         tick_event_messages.upgrade_request_msg, 1);
  }
  if (tick_event_messages.has_game_over) {
    AppendPreparedPacket(&prepared_packets,
                         lawnmower::MessageType::MSG_S2C_GAME_OVER,
                         tick_event_messages.game_over_msg, 1);
  }

  LogGameOverSummary(room_id, game_over);

  const bool has_noncritical_packets = !prepared_packets.empty() &&
      std::any_of(prepared_packets.begin(), prepared_packets.end(),
                  [](const PreparedPacket& packet) {
                    return packet.type != lawnmower::MessageType::MSG_S2C_PLAYER_HURT_BATCH &&
                           packet.type != lawnmower::MessageType::MSG_S2C_ENEMY_DIED_BATCH &&
                           packet.type != lawnmower::MessageType::MSG_S2C_PLAYER_LEVEL_UP_BATCH &&
                           packet.type != lawnmower::MessageType::MSG_S2C_UPGRADE_REQUEST &&
                           packet.type != lawnmower::MessageType::MSG_S2C_GAME_OVER;
                  });
  if (tick_event_messages.has_player_hurt_batch ||
      tick_event_messages.has_enemy_died_batch ||
      tick_event_messages.has_level_up_batch ||
      tick_event_messages.has_game_over ||
      tick_event_messages.has_upgrade_request || has_noncritical_packets) {
    SendTickEventsToSessions(sessions, prepared_packets);
  }

  if (game_over.has_value()) {
    ClearDeferredBacklog(room_id);
  }

  return static_cast<uint32_t>(prepared_packets.size());
}

}  // namespace game_manager_event_dispatch
