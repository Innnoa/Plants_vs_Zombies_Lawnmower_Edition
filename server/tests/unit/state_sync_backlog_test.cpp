#include <arpa/inet.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "game/managers/game_manager.hpp"
#include "game/managers/room_manager.hpp"
#include "game/managers/internal/game_manager_sync_dispatch.hpp"
#include "network/tcp/tcp_session.hpp"
#include "network/udp/udp_server.hpp"

namespace {

std::vector<lawnmower::Packet> g_sent_packets;

[[noreturn]] void Fail(const std::string& msg) {
  throw std::runtime_error(msg);
}

void Expect(bool cond, const std::string& msg) {
  if (!cond) {
    Fail(msg);
  }
}

void ClearSentPackets() { g_sent_packets.clear(); }

lawnmower::S2C_GameStateDeltaSync MakeDelta(uint32_t room_id) {
  lawnmower::S2C_GameStateDeltaSync delta;
  delta.set_room_id(room_id);
  auto* player = delta.add_players();
  player->set_player_id(1);
  player->set_changed_mask(1);
  player->mutable_position()->set_x(120);
  player->mutable_position()->set_y(240);
  return delta;
}

lawnmower::S2C_GameStateSync MakeFullSync(uint32_t room_id) {
  lawnmower::S2C_GameStateSync sync;
  sync.set_room_id(room_id);
  sync.set_is_full_snapshot(true);
  auto* player = sync.add_players();
  player->set_player_id(1);
  player->mutable_position()->set_x(100);
  player->mutable_position()->set_y(200);
  player->set_rotation(0);
  player->set_is_alive(true);
  return sync;
}

void TestStateBacklogKeepsBothPreparedBytesAndFrozenTiming() {
  constexpr uint32_t kRoomId = 101;
  game_manager_sync_dispatch::ClearRoomDispatchState(kRoomId);

  const std::vector<std::weak_ptr<TcpSession>> sessions;
  const lawnmower::S2C_GameStateSync empty_sync;

  game_manager_sync_dispatch::DispatchStateSyncPayloads(
      kRoomId, 50, nullptr, sessions, false, false, true, empty_sync,
      MakeDelta(kRoomId), 0);

  const auto first_tcp =
      game_manager_sync_dispatch::FirstDeferredStatePreparedTcpIdentity(kRoomId);
  const auto first_udp =
      game_manager_sync_dispatch::FirstDeferredStatePreparedUdpIdentity(kRoomId);
  const auto first_tick =
      game_manager_sync_dispatch::FirstDeferredStateFrozenTick(kRoomId);
  const auto first_time =
      game_manager_sync_dispatch::FirstDeferredStateFrozenServerTime(kRoomId);

  Expect(game_manager_sync_dispatch::QueuedStatePacketCount(kRoomId) > 0,
         "状态 backlog 应已有积压状态包");
  Expect(first_tcp != 0, "进入 backlog 的状态包应持有 TCP prepared bytes");
  Expect(first_udp != 0, "进入 backlog 的状态包应持有 UDP prepared bytes");
  Expect(first_tick == 50, "frozen tick 应等于首次入 backlog 的 tick");
  Expect(first_time != 0, "frozen server_time 应被写入");

  game_manager_sync_dispatch::DispatchStateSyncPayloads(
      kRoomId, 51, nullptr, sessions, false, false, false, empty_sync,
      lawnmower::S2C_GameStateDeltaSync{}, 0);

  Expect(game_manager_sync_dispatch::FirstDeferredStatePreparedTcpIdentity(
             kRoomId) == first_tcp,
         "状态 backlog 跨 tick 应复用同一份 TCP prepared bytes");
  Expect(game_manager_sync_dispatch::FirstDeferredStatePreparedUdpIdentity(
             kRoomId) == first_udp,
         "状态 backlog 跨 tick 应复用同一份 UDP prepared bytes");
  Expect(game_manager_sync_dispatch::FirstDeferredStateFrozenTick(kRoomId) ==
             first_tick,
         "状态 backlog 跨 tick 不应改写 frozen tick");
  Expect(game_manager_sync_dispatch::FirstDeferredStateFrozenServerTime(
             kRoomId) == first_time,
         "状态 backlog 跨 tick 不应改写 frozen server_time");

  game_manager_sync_dispatch::ClearRoomDispatchState(kRoomId);
}

void TestFullSnapshotBacklogUpdatesTickButReusesTemplate() {
  constexpr uint32_t kRoomId = 103;
  game_manager_sync_dispatch::ClearRoomDispatchState(kRoomId);
  ClearSentPackets();

  asio::io_context io_context;
  auto session = std::make_shared<TcpSession>(tcp::socket(io_context));
  const std::vector<std::weak_ptr<TcpSession>> sessions{session};
  const lawnmower::S2C_GameStateDeltaSync empty_delta;

  game_manager_sync_dispatch::DispatchStateSyncPayloads(
      kRoomId, 70, nullptr, sessions, true, true, false, MakeFullSync(kRoomId),
      empty_delta, 0);

  const auto first_template =
      game_manager_sync_dispatch::FirstDeferredStateTemplateIdentity(kRoomId);
  const auto first_tick =
      game_manager_sync_dispatch::FirstDeferredStateFrozenTick(kRoomId);
  const auto first_time =
      game_manager_sync_dispatch::FirstDeferredStateFrozenServerTime(kRoomId);

  Expect(game_manager_sync_dispatch::QueuedStatePacketCount(kRoomId) > 0,
         "full snapshot backlog 应已有积压状态包");
  Expect(first_template != 0,
         "full snapshot backlog chunk 应持有可复用模板 identity");
  Expect(first_tick == 70, "首次 backlog 观察到的 tick 应等于首个 dispatch tick");
  Expect(first_time != 0, "首次 backlog 观察到的 server_time 应非 0");

  game_manager_sync_dispatch::DispatchStateSyncPayloads(
      kRoomId, 71, nullptr, sessions, false, false, false,
      lawnmower::S2C_GameStateSync{}, lawnmower::S2C_GameStateDeltaSync{}, 0);

  Expect(game_manager_sync_dispatch::FirstDeferredStateTemplateIdentity(kRoomId) ==
             first_template,
         "full snapshot backlog 跨 tick 应复用同一份模板");
  Expect(game_manager_sync_dispatch::FirstDeferredStateFrozenTick(kRoomId) ==
             first_tick,
         "未实际发送前不应提前改写 backlog 里的观察 tick");

  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  game_manager_sync_dispatch::DispatchStateSyncPayloads(
      kRoomId, 72, nullptr, sessions, false, false, false,
      lawnmower::S2C_GameStateSync{}, lawnmower::S2C_GameStateDeltaSync{}, 1);

  Expect(!g_sent_packets.empty(), "full snapshot backlog 应在预算允许时发送");
  const auto& sent_sync =
      g_sent_packets.back();
  lawnmower::S2C_GameStateSync sent_payload;
  Expect(sent_payload.ParseFromString(sent_sync.payload()),
         "应能解析发送出的 full snapshot payload");
  Expect(sent_payload.sync_time().tick() == 72,
         "full snapshot backlog 真正发送时应更新为实际发送 tick");
  Expect(sent_payload.sync_time().server_time() != first_time,
         "full snapshot backlog 真正发送时应刷新 server_time");

  game_manager_sync_dispatch::ClearRoomDispatchState(kRoomId);
}

}  // namespace

GameManager& GameManager::Instance() {
  static GameManager instance;
  return instance;
}

RoomManager& RoomManager::Instance() {
  static RoomManager instance;
  return instance;
}

TcpSession::TcpSession(tcp::socket socket) : socket_(std::move(socket)) {}

std::vector<std::weak_ptr<TcpSession>> RoomManager::GetRoomSessions(
    uint32_t) const {
  return {};
}

void TcpSession::SendFramedPacket(const std::shared_ptr<const std::string>& framed,
                                  lawnmower::MessageType, std::size_t,
                                  std::size_t) {
  if (framed == nullptr || framed->size() < sizeof(uint32_t)) {
    return;
  }
  uint32_t net_len = 0;
  std::memcpy(&net_len, framed->data(), sizeof(net_len));
  const auto body_len = ntohl(net_len);
  if (framed->size() < sizeof(net_len) + body_len) {
    return;
  }
  lawnmower::Packet packet;
  if (packet.ParseFromArray(framed->data() + sizeof(net_len),
                            static_cast<int>(body_len))) {
    g_sent_packets.push_back(std::move(packet));
  }
}

std::size_t UdpServer::BroadcastPreparedPacket(
    uint32_t, const std::shared_ptr<const std::string>&) {
  return 1;
}

int main() {
  try {
    TestStateBacklogKeepsBothPreparedBytesAndFrozenTiming();
    TestFullSnapshotBacklogUpdatesTickButReusesTemplate();
    std::cout << "state_sync_backlog_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "state_sync_backlog_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
