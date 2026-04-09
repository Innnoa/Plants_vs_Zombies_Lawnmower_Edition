#include "network/channel_policy.hpp"

namespace network_channel_policy {

namespace {

constexpr ChannelPolicy kConservativeDefaultPolicy{
    .channel = MessageChannel::ReliableControl,
    .default_transport = DeliveryTransport::TcpOnly,
    .delivery_scope = DeliveryScope::SingleSession,
    .allow_fallback = false,
    .allow_backlog = false,
    .subject_to_packet_budget = false,
    .subject_to_event_entry_budget = false,
};

constexpr ChannelPolicy kReliableCriticalEventPolicy{
    .channel = MessageChannel::ReliableCriticalEvent,
    .default_transport = DeliveryTransport::TcpOnly,
    .delivery_scope = DeliveryScope::RoomBroadcast,
    .allow_fallback = false,
    .allow_backlog = false,
    .subject_to_packet_budget = false,
    .subject_to_event_entry_budget = false,
};

constexpr ChannelPolicy kReliableRoomControlPolicy{
    .channel = MessageChannel::ReliableControl,
    .default_transport = DeliveryTransport::TcpOnly,
    .delivery_scope = DeliveryScope::RoomBroadcast,
    .allow_fallback = false,
    .allow_backlog = false,
    .subject_to_packet_budget = false,
    .subject_to_event_entry_budget = false,
};

constexpr ChannelPolicy kReliableBackloggedRoomEventPolicy{
    .channel = MessageChannel::ReliableControl,
    .default_transport = DeliveryTransport::TcpOnly,
    .delivery_scope = DeliveryScope::RoomBroadcast,
    .allow_fallback = false,
    .allow_backlog = true,
    .subject_to_packet_budget = true,
    .subject_to_event_entry_budget = true,
};

constexpr ChannelPolicy kUnreliableRealtimeBroadcastPolicy{
    .channel = MessageChannel::UnreliableRealtimeState,
    .default_transport = DeliveryTransport::UdpPreferredTcpFallback,
    .delivery_scope = DeliveryScope::RoomBroadcastWithFallback,
    .allow_fallback = true,
    .allow_backlog = true,
    .subject_to_packet_budget = true,
    .subject_to_event_entry_budget = false,
};

constexpr ChannelPolicy kUnreliableRealtimeSingleSessionPolicy{
    .channel = MessageChannel::UnreliableRealtimeState,
    .default_transport = DeliveryTransport::UdpPreferredTcpFallback,
    .delivery_scope = DeliveryScope::SingleSession,
    .allow_fallback = true,
    .allow_backlog = false,
    .subject_to_packet_budget = false,
    .subject_to_event_entry_budget = false,
};

}  // namespace

ChannelPolicy ResolveMessageChannelPolicy(lawnmower::MessageType type) {
  switch (type) {
    case lawnmower::MessageType::MSG_C2S_PLAYER_INPUT:
      return kUnreliableRealtimeSingleSessionPolicy;

    case lawnmower::MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC:
      return kUnreliableRealtimeBroadcastPolicy;

    case lawnmower::MessageType::MSG_S2C_PLAYER_HURT_BATCH:
    case lawnmower::MessageType::MSG_S2C_ENEMY_DIED_BATCH:
    case lawnmower::MessageType::MSG_S2C_PLAYER_LEVEL_UP_BATCH:
    case lawnmower::MessageType::MSG_S2C_GAME_OVER:
      return kReliableCriticalEventPolicy;

    case lawnmower::MessageType::MSG_S2C_GAME_STATE_SYNC:
      return kConservativeDefaultPolicy;

    case lawnmower::MessageType::MSG_S2C_UPGRADE_REQUEST:
      return kReliableRoomControlPolicy;

    case lawnmower::MessageType::MSG_S2C_PROJECTILE_SPAWN:
    case lawnmower::MessageType::MSG_S2C_PROJECTILE_DESPAWN:
    case lawnmower::MessageType::MSG_S2C_DROPPED_ITEM:
    case lawnmower::MessageType::MSG_S2C_ENEMY_ATTACK_STATE_SYNC:
      return kReliableBackloggedRoomEventPolicy;

    default:
      return kConservativeDefaultPolicy;
  }
}

MessageChannel ResolveMessageChannel(lawnmower::MessageType type) {
  return ResolveMessageChannelPolicy(type).channel;
}

}  // namespace network_channel_policy
