#include <iostream>
#include <stdexcept>
#include <string>

#include "game/managers/internal/game_manager_dispatch_cache.hpp"

namespace {

using game_manager_dispatch_cache::CacheFamily;
using game_manager_dispatch_cache::ClearAllDispatchCachesForTests;
using game_manager_dispatch_cache::ClearRoomDispatchCache;
using game_manager_dispatch_cache::MaterializeOrReusePacket;
using game_manager_dispatch_cache::PreparedTransport;

[[noreturn]] void Fail(const std::string& msg) {
  throw std::runtime_error(msg);
}

void Expect(bool cond, const std::string& msg) {
  if (!cond) {
    Fail(msg);
  }
}

lawnmower::S2C_GameOver MakeGameOver(uint32_t room_id, bool victory) {
  lawnmower::S2C_GameOver game_over;
  game_over.set_room_id(room_id);
  game_over.set_victory(victory);
  game_over.set_survive_time(42);
  auto* score = game_over.add_scores();
  score->set_player_id(1);
  score->set_player_name("cache-tester");
  score->set_final_level(3);
  score->set_kill_count(9);
  score->set_damage_dealt(120);
  return game_over;
}

void TestReuseWithinSameRoomTickAndSlot() {
  ClearAllDispatchCachesForTests();
  const auto message = MakeGameOver(7, true);
  const auto first = MaterializeOrReusePacket(
      7, 100, CacheFamily::kTickEvents, 0,
      lawnmower::MessageType::MSG_S2C_GAME_OVER,
      PreparedTransport::kTcpFramed, message);
  const auto second = MaterializeOrReusePacket(
      7, 100, CacheFamily::kTickEvents, 0,
      lawnmower::MessageType::MSG_S2C_GAME_OVER,
      PreparedTransport::kTcpFramed, message);
  Expect(first.bytes != nullptr, "第一次 materialize 应产生 bytes");
  Expect(first.bytes == second.bytes,
         "同房间同 tick 同 slot 应复用同一份 prepared bytes");
}

void TestNewTickDoesNotReuseOldBytes() {
  ClearAllDispatchCachesForTests();
  const auto message = MakeGameOver(8, true);
  const auto old_tick = MaterializeOrReusePacket(
      8, 100, CacheFamily::kTickEvents, 0,
      lawnmower::MessageType::MSG_S2C_GAME_OVER,
      PreparedTransport::kTcpFramed, message);
  const auto new_tick = MaterializeOrReusePacket(
      8, 101, CacheFamily::kTickEvents, 0,
      lawnmower::MessageType::MSG_S2C_GAME_OVER,
      PreparedTransport::kTcpFramed, message);
  Expect(old_tick.bytes != nullptr && new_tick.bytes != nullptr,
         "新旧 tick 都应产生 bytes");
  Expect(old_tick.bytes != new_tick.bytes, "新 tick 不应继续复用旧 tick bytes");
}

void TestClearRoomInvalidatesCachedBytes() {
  ClearAllDispatchCachesForTests();
  const auto message = MakeGameOver(9, false);
  const auto cached = MaterializeOrReusePacket(
      9, 200, CacheFamily::kTickEvents, 0,
      lawnmower::MessageType::MSG_S2C_GAME_OVER,
      PreparedTransport::kTcpFramed, message);
  ClearRoomDispatchCache(9);
  const auto rebuilt = MaterializeOrReusePacket(
      9, 200, CacheFamily::kTickEvents, 0,
      lawnmower::MessageType::MSG_S2C_GAME_OVER,
      PreparedTransport::kTcpFramed, message);
  Expect(cached.bytes != nullptr && rebuilt.bytes != nullptr,
         "清理前后都应产生 bytes");
  Expect(cached.bytes != rebuilt.bytes, "ClearRoomDispatchCache 后不应继续命中旧 cache");
}

void TestDifferentMessageTypeDoesNotReuseSameSlot() {
  ClearAllDispatchCachesForTests();
  const auto game_over = MakeGameOver(10, true);

  lawnmower::S2C_PlayerLevelUpBatch level_up_batch;
  level_up_batch.set_room_id(10);
  auto* event = level_up_batch.add_events();
  event->set_player_id(1);
  event->set_new_level(2);

  const auto first = MaterializeOrReusePacket(
      10, 300, CacheFamily::kTickEvents, 0,
      lawnmower::MessageType::MSG_S2C_GAME_OVER,
      PreparedTransport::kTcpFramed, game_over);
  const auto second = MaterializeOrReusePacket(
      10, 300, CacheFamily::kTickEvents, 0,
      lawnmower::MessageType::MSG_S2C_PLAYER_LEVEL_UP_BATCH,
      PreparedTransport::kTcpFramed, level_up_batch);

  Expect(first.bytes != nullptr && second.bytes != nullptr,
         "不同 message_type 都应产生 bytes");
  Expect(first.type == lawnmower::MessageType::MSG_S2C_GAME_OVER,
         "第一次缓存类型应保留原始 type");
  Expect(second.type == lawnmower::MessageType::MSG_S2C_PLAYER_LEVEL_UP_BATCH,
         "不同 message_type 不应复用到旧缓存条目");
  Expect(first.bytes != second.bytes,
         "同 room/tick/family/slot 但不同 message_type 不应复用同一份 bytes");
}

}  // namespace

int main() {
  try {
    TestReuseWithinSameRoomTickAndSlot();
    TestNewTickDoesNotReuseOldBytes();
    TestClearRoomInvalidatesCachedBytes();
    TestDifferentMessageTypeDoesNotReuseSameSlot();
    std::cout << "dispatch_cache_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "dispatch_cache_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
