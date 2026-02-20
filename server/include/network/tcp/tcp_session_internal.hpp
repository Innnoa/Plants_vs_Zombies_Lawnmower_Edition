#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <spdlog/spdlog.h>
#include <string>

#include "network/tcp/tcp_session.hpp"

namespace tcp_session_internal {

inline constexpr std::size_t kMaxPacketSize = 64 * 1024;  // 设定最大包大小
inline constexpr std::size_t kMaxWriteQueueSize = 1024;   // 防止慢连接无限堆积
inline constexpr std::size_t kTokenBytes = 16;            // 128bit 令牌
inline constexpr uint64_t kPacketDebugLogStride = 60;     // 高频日志限流步长

inline std::string MessageTypeToString(lawnmower::MessageType type) {
  const std::string name = lawnmower::MessageType_Name(type);
  if (!name.empty()) {
    return name + "(" + std::to_string(static_cast<int>(type)) + ")";
  }
  return "UNKNOWN(" + std::to_string(static_cast<int>(type)) + ")";
}

inline void BroadcastToRoom(std::span<const std::weak_ptr<TcpSession>> sessions,
                            lawnmower::MessageType type,
                            const google::protobuf::Message& message) {
  for (const auto& weak_session : sessions) {
    if (auto session = weak_session.lock()) {
      session->SendProto(type, message);
    }
  }
}

template <typename T>
bool ParsePayload(const std::string& payload, T* out,
                  const char* warn_message) {
  if (out == nullptr) {
    return false;
  }
  if (!out->ParseFromString(payload)) {
    if (warn_message != nullptr) {
      spdlog::warn("{}", warn_message);
    }
    return false;
  }
  return true;
}

}  // namespace tcp_session_internal
