#include <iostream>
#include <stdexcept>
#include <string>

#include "network/channel_policy.hpp"

namespace {

using network_channel_policy::DeliveryTransport;
using network_channel_policy::DeliveryScope;
using network_channel_policy::MessageChannel;
using network_channel_policy::ResolveMessageChannel;
using network_channel_policy::ResolveMessageChannelPolicy;

[[noreturn]] void Fail(const std::string& msg) {
  throw std::runtime_error(msg);
}

void Expect(bool cond, const std::string& msg) {
  if (!cond) {
    Fail(msg);
  }
}

void ExpectChannel(lawnmower::MessageType type, MessageChannel expected,
                   const std::string& msg) {
  const auto actual = ResolveMessageChannel(type);
  Expect(actual == expected,
         msg + " actual=" + std::to_string(static_cast<int>(actual)) +
             " expected=" + std::to_string(static_cast<int>(expected)));
}

void TestKnownMappings() {
  using lawnmower::MessageType;

  ExpectChannel(MessageType::MSG_C2S_PLAYER_INPUT,
                MessageChannel::UnreliableRealtimeState,
                "MSG_C2S_PLAYER_INPUT should map to UnreliableRealtimeState");
  ExpectChannel(
      MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC,
      MessageChannel::UnreliableRealtimeState,
      "MSG_S2C_GAME_STATE_DELTA_SYNC should map to UnreliableRealtimeState");
  ExpectChannel(MessageType::MSG_S2C_GAME_STATE_SYNC,
                MessageChannel::ReliableControl,
                "MSG_S2C_GAME_STATE_SYNC should map to ReliableControl");
  ExpectChannel(MessageType::MSG_S2C_PLAYER_HURT_BATCH,
                MessageChannel::ReliableCriticalEvent,
                "MSG_S2C_PLAYER_HURT_BATCH should map to ReliableCriticalEvent");
  ExpectChannel(MessageType::MSG_S2C_GAME_OVER,
                MessageChannel::ReliableCriticalEvent,
                "MSG_S2C_GAME_OVER should map to ReliableCriticalEvent");
  ExpectChannel(MessageType::MSG_S2C_UPGRADE_REQUEST,
                MessageChannel::ReliableControl,
                "MSG_S2C_UPGRADE_REQUEST should map to ReliableControl");
}

void ExpectPolicy(lawnmower::MessageType type, const MessageChannel expected_channel,
                  const DeliveryTransport expected_transport,
                  const DeliveryScope expected_scope,
                  bool expected_allow_fallback, bool expected_allow_backlog,
                  bool expected_packet_budget, bool expected_event_entry_budget,
                  const std::string& msg_prefix) {
  const auto policy = ResolveMessageChannelPolicy(type);
  Expect(policy.channel == expected_channel, msg_prefix + " 频道不符合预期");
  Expect(policy.default_transport == expected_transport,
         msg_prefix + " 默认传输策略不符合预期");
  Expect(policy.delivery_scope == expected_scope,
         msg_prefix + " 投递范围不符合预期");
  Expect(policy.allow_fallback == expected_allow_fallback,
         msg_prefix + " allow_fallback 不符合预期");
  Expect(policy.allow_backlog == expected_allow_backlog,
         msg_prefix + " allow_backlog 不符合预期");
  Expect(policy.subject_to_packet_budget == expected_packet_budget,
         msg_prefix + " subject_to_packet_budget 不符合预期");
  Expect(policy.subject_to_event_entry_budget == expected_event_entry_budget,
         msg_prefix + " subject_to_event_entry_budget 不符合预期");
}

void TestPolicyFieldsForRealtimeAndCriticalMessages() {
  using lawnmower::MessageType;

  ExpectPolicy(MessageType::MSG_C2S_PLAYER_INPUT,
               MessageChannel::UnreliableRealtimeState,
               DeliveryTransport::UdpPreferredTcpFallback,
               DeliveryScope::SingleSession,
               true, false, false, false,
               "MSG_C2S_PLAYER_INPUT");

  ExpectPolicy(MessageType::MSG_S2C_GAME_STATE_DELTA_SYNC,
               MessageChannel::UnreliableRealtimeState,
               DeliveryTransport::UdpPreferredTcpFallback,
               DeliveryScope::RoomBroadcastWithFallback,
               true, true, true, false,
               "MSG_S2C_GAME_STATE_DELTA_SYNC");

  ExpectPolicy(MessageType::MSG_S2C_PLAYER_HURT_BATCH,
               MessageChannel::ReliableCriticalEvent,
               DeliveryTransport::TcpOnly,
               DeliveryScope::RoomBroadcast,
               false, false, false, false,
               "MSG_S2C_PLAYER_HURT_BATCH");

  ExpectPolicy(MessageType::MSG_S2C_PROJECTILE_SPAWN,
               MessageChannel::ReliableControl,
               DeliveryTransport::TcpOnly,
               DeliveryScope::RoomBroadcast,
               false, true, true, true,
               "MSG_S2C_PROJECTILE_SPAWN");

  ExpectPolicy(MessageType::MSG_S2C_UPGRADE_REQUEST,
               MessageChannel::ReliableControl,
               DeliveryTransport::TcpOnly,
               DeliveryScope::RoomBroadcast,
               false, false, false, false,
               "MSG_S2C_UPGRADE_REQUEST");
}

void TestConservativeDefaultForUnmappedMessages() {
  ExpectPolicy(lawnmower::MessageType::MSG_UNKNOWN,
               MessageChannel::ReliableControl,
               DeliveryTransport::TcpOnly,
               DeliveryScope::SingleSession,
               false, false, false, false,
               "MSG_UNKNOWN 保守默认策略");
}

}  // namespace

int main() {
  try {
    TestKnownMappings();
    TestPolicyFieldsForRealtimeAndCriticalMessages();
    TestConservativeDefaultForUnmappedMessages();
    std::cout << "channel_policy_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "channel_policy_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
