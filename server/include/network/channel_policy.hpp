#pragma once

#include "message.pb.h"

namespace network_channel_policy {

enum class MessageChannel {
  ReliableControl = 0,
  ReliableCriticalEvent = 1,
  UnreliableRealtimeState = 2,
};

enum class DeliveryTransport {
  TcpOnly = 0,
  UdpOnly = 1,
  UdpPreferredTcpFallback = 2,
};

enum class DeliveryScope {
  SingleSession = 0,
  RoomBroadcast = 1,
  RoomBroadcastWithFallback = 2,
};

struct ChannelPolicy {
  MessageChannel channel = MessageChannel::ReliableControl;
  DeliveryTransport default_transport = DeliveryTransport::TcpOnly;
  DeliveryScope delivery_scope = DeliveryScope::SingleSession;
  bool allow_fallback = false;
  bool allow_backlog = false;
  bool subject_to_packet_budget = false;
  bool subject_to_event_entry_budget = false;
};

ChannelPolicy ResolveMessageChannelPolicy(lawnmower::MessageType type);
MessageChannel ResolveMessageChannel(lawnmower::MessageType type);

}  // namespace network_channel_policy
