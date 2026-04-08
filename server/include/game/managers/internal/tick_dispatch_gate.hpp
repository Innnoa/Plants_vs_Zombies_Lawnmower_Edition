#pragma once

#include <optional>

namespace game_manager_tick_dispatch_gate {

struct RoomTickDispatchGate {
  bool tick_in_flight = false;
  bool tick_pending = false;
  double pending_tick_interval_seconds = 0.0;
};

inline bool TryBeginDispatch(RoomTickDispatchGate* gate,
                             double tick_interval_seconds) {
  if (gate == nullptr) {
    return false;
  }
  if (!gate->tick_in_flight) {
    gate->tick_in_flight = true;
    gate->tick_pending = false;
    gate->pending_tick_interval_seconds = tick_interval_seconds;
    return true;
  }
  gate->tick_pending = true;
  gate->pending_tick_interval_seconds = tick_interval_seconds;
  return false;
}

inline std::optional<double> FinishDispatchAndTakePending(
    RoomTickDispatchGate* gate) {
  if (gate == nullptr) {
    return std::nullopt;
  }
  if (gate->tick_pending) {
    gate->tick_pending = false;
    gate->tick_in_flight = true;
    return gate->pending_tick_interval_seconds;
  }
  gate->tick_in_flight = false;
  return std::nullopt;
}

}  // namespace game_manager_tick_dispatch_gate
