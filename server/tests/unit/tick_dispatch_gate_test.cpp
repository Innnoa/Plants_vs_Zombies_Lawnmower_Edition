#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "game/managers/internal/tick_dispatch_gate.hpp"

namespace {

[[noreturn]] void Fail(const std::string& msg) {
  throw std::runtime_error(msg);
}

void Expect(bool cond, const std::string& msg) {
  if (!cond) {
    Fail(msg);
  }
}

void ExpectNear(double actual, double expected, double eps,
                const std::string& msg) {
  if (std::fabs(actual - expected) > eps) {
    Fail(msg + " actual=" + std::to_string(actual) +
         " expected=" + std::to_string(expected));
  }
}

void TestIdleRoomDispatchesImmediately() {
  game_manager_tick_dispatch_gate::RoomTickDispatchGate gate;
  Expect(game_manager_tick_dispatch_gate::TryBeginDispatch(&gate, 0.033),
         "空闲房间首次调度应立即派发");
  Expect(gate.tick_in_flight, "首次调度后 tick_in_flight 应为 true");
  Expect(!gate.tick_pending, "首次调度后 tick_pending 应为 false");
}

void TestInFlightRoomCoalescesPendingTick() {
  game_manager_tick_dispatch_gate::RoomTickDispatchGate gate;
  Expect(game_manager_tick_dispatch_gate::TryBeginDispatch(&gate, 0.033),
         "首次调度应成功");
  Expect(!game_manager_tick_dispatch_gate::TryBeginDispatch(&gate, 0.040),
         "in-flight 房间不应并发派发第二个 tick");
  Expect(gate.tick_in_flight, "coalesce 后 tick_in_flight 应保持 true");
  Expect(gate.tick_pending, "coalesce 后 tick_pending 应为 true");
  ExpectNear(gate.pending_tick_interval_seconds, 0.040, 1e-9,
             "pending tick 间隔应记录最新值");
  Expect(!game_manager_tick_dispatch_gate::TryBeginDispatch(&gate, 0.050),
         "重复 coalesce 仍不应并发派发");
  ExpectNear(gate.pending_tick_interval_seconds, 0.050, 1e-9,
             "重复 coalesce 应覆盖为最新 tick 间隔");
}

void TestFinishDispatchSchedulesOnePendingTick() {
  game_manager_tick_dispatch_gate::RoomTickDispatchGate gate;
  Expect(game_manager_tick_dispatch_gate::TryBeginDispatch(&gate, 0.033),
         "首次调度应成功");
  Expect(!game_manager_tick_dispatch_gate::TryBeginDispatch(&gate, 0.040),
         "第二次调度应被合并");

  const auto next =
      game_manager_tick_dispatch_gate::FinishDispatchAndTakePending(&gate);
  Expect(next.has_value(), "完成当前 tick 后应取出 pending tick");
  ExpectNear(*next, 0.040, 1e-9, "pending tick 间隔不正确");
  Expect(gate.tick_in_flight, "取出 pending tick 后应继续保持 in-flight");
  Expect(!gate.tick_pending, "取出 pending tick 后应清空 pending 标记");

  const auto none =
      game_manager_tick_dispatch_gate::FinishDispatchAndTakePending(&gate);
  Expect(!none.has_value(), "无 pending tick 时不应返回新调度");
  Expect(!gate.tick_in_flight, "完全完成后 tick_in_flight 应清空");
}

}  // namespace

int main() {
  try {
    TestIdleRoomDispatchesImmediately();
    TestInFlightRoomCoalescesPendingTick();
    TestFinishDispatchSchedulesOnePendingTick();
    std::cout << "tick_dispatch_gate_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "tick_dispatch_gate_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
