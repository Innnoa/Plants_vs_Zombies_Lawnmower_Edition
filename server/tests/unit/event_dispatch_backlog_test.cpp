#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "internal/game_manager_event_dispatch.hpp"
#include "network/tcp/tcp_session.hpp"

namespace {

[[noreturn]] void Fail(const std::string& msg) {
  throw std::runtime_error(msg);
}

void Expect(bool cond, const std::string& msg) {
  if (!cond) {
    Fail(msg);
  }
}

std::vector<lawnmower::ProjectileState> MakeProjectileSpawns(uint32_t room_id) {
  (void)room_id;
  std::vector<lawnmower::ProjectileState> spawns;
  lawnmower::ProjectileState projectile;
  projectile.set_projectile_id(1);
  projectile.set_owner_player_id(1);
  projectile.mutable_position()->set_x(100);
  projectile.mutable_position()->set_y(120);
  projectile.set_rotation(0);
  projectile.set_ttl_ms(300);
  auto* meta = projectile.mutable_projectile();
  meta->set_speed(8);
  meta->set_damage(3);
  spawns.push_back(projectile);
  return spawns;
}

void TestDeferredBacklogRoomIsRemovedAfterDrain() {
  constexpr uint32_t kRoomId = 77;
  game_manager_event_dispatch::ClearRoomDispatchState(kRoomId);

  const auto spawns = MakeProjectileSpawns(kRoomId);
  const std::vector<lawnmower::ProjectileDespawn> despawns;
  const std::vector<lawnmower::ItemState> dropped_items;
  const std::vector<lawnmower::EnemyAttackStateDelta> attack_states;
  const std::vector<lawnmower::S2C_PlayerHurt> player_hurts;
  const std::vector<lawnmower::S2C_EnemyDied> enemy_dieds;
  const std::vector<lawnmower::S2C_PlayerLevelUp> level_ups;
  const std::vector<std::weak_ptr<TcpSession>> sessions;
  const std::optional<lawnmower::S2C_GameOver> game_over;
  const std::optional<lawnmower::S2C_UpgradeRequest> upgrade_request;

  game_manager_event_dispatch::DispatchTickEvents(
      kRoomId, 10, 1, spawns, despawns, dropped_items, attack_states,
      player_hurts, enemy_dieds, level_ups, game_over, sessions,
      upgrade_request, 0, 0);

  Expect(game_manager_event_dispatch::HasDeferredBacklogRoom(kRoomId),
         "预算为 0 时应留下 backlog 房间记录");
  Expect(game_manager_event_dispatch::QueuedNonCriticalEventPacketCount(kRoomId) > 0,
         "预算为 0 时应积压非关键事件包");

  game_manager_event_dispatch::DispatchTickEvents(
      kRoomId, 11, 1, {}, despawns, dropped_items, attack_states, player_hurts,
      enemy_dieds, level_ups, game_over, sessions, upgrade_request, 8, 8);

  Expect(game_manager_event_dispatch::QueuedNonCriticalEventPacketCount(kRoomId) == 0,
         "drain 后 backlog 包数应为 0");
  Expect(!game_manager_event_dispatch::HasDeferredBacklogRoom(kRoomId),
         "drain 后应移除空 backlog 房间记录");
}

void TestDeferredBacklogPacketKeepsPreparedBytesAcrossTicks() {
  constexpr uint32_t kRoomId = 88;
  game_manager_event_dispatch::ClearRoomDispatchState(kRoomId);

  const auto spawns = MakeProjectileSpawns(kRoomId);
  const std::vector<lawnmower::ProjectileDespawn> despawns;
  const std::vector<lawnmower::ItemState> dropped_items;
  const std::vector<lawnmower::EnemyAttackStateDelta> attack_states;
  const std::vector<lawnmower::S2C_PlayerHurt> player_hurts;
  const std::vector<lawnmower::S2C_EnemyDied> enemy_dieds;
  const std::vector<lawnmower::S2C_PlayerLevelUp> level_ups;
  const std::vector<std::weak_ptr<TcpSession>> sessions;
  const std::optional<lawnmower::S2C_GameOver> game_over;
  const std::optional<lawnmower::S2C_UpgradeRequest> upgrade_request;

  game_manager_event_dispatch::DispatchTickEvents(
      kRoomId, 20, 1, spawns, despawns, dropped_items, attack_states,
      player_hurts, enemy_dieds, level_ups, game_over, sessions,
      upgrade_request, 0, 0);

  const auto first_identity =
      game_manager_event_dispatch::FirstDeferredBacklogPreparedBytesIdentity(
          kRoomId);
  Expect(first_identity != 0,
         "backlog packet 入队后应立即持有 prepared bytes");

  game_manager_event_dispatch::DispatchTickEvents(
      kRoomId, 21, 1, {}, despawns, dropped_items, attack_states, player_hurts,
      enemy_dieds, level_ups, game_over, sessions, upgrade_request, 0, 0);

  const auto second_identity =
      game_manager_event_dispatch::FirstDeferredBacklogPreparedBytesIdentity(
          kRoomId);
  Expect(second_identity == first_identity,
         "同一 backlog packet 跨 tick 留存时应复用同一份 prepared bytes");

  game_manager_event_dispatch::ClearRoomDispatchState(kRoomId);
}

}  // namespace

void TcpSession::SendFramedPacket(const std::shared_ptr<const std::string>&,
                                  lawnmower::MessageType, std::size_t,
                                  std::size_t) {}

int main() {
  try {
    TestDeferredBacklogRoomIsRemovedAfterDrain();
    TestDeferredBacklogPacketKeepsPreparedBytesAcrossTicks();
    std::cout << "event_dispatch_backlog_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "event_dispatch_backlog_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
