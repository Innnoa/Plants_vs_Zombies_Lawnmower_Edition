#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace network_shared {

class SharedStringPool {
 public:
  std::shared_ptr<std::string> Acquire(std::size_t reserve_hint) {
    std::string* buffer = nullptr;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!free_list_.empty()) {
        buffer = free_list_.back().release();
        free_list_.pop_back();
      }
    }

    if (buffer == nullptr) {
      buffer = new std::string();
    }
    buffer->clear();
    if (buffer->capacity() < reserve_hint) {
      buffer->reserve(reserve_hint);
    }

    return std::shared_ptr<std::string>(
        buffer, [this](std::string* value) { Release(value); });
  }

 private:
  void Release(std::string* value) {
    if (value == nullptr) {
      return;
    }
    value->clear();
    constexpr std::size_t kMaxRetainedCapacity = 256 * 1024;
    if (value->capacity() > kMaxRetainedCapacity) {
      delete value;
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    constexpr std::size_t kMaxRetainedBuffers = 256;
    if (free_list_.size() >= kMaxRetainedBuffers) {
      delete value;
      return;
    }
    free_list_.emplace_back(value);
  }

  std::mutex mutex_;
  std::vector<std::unique_ptr<std::string>> free_list_;
};

inline SharedStringPool& GlobalSharedStringPool() {
  static auto* pool = new SharedStringPool();
  return *pool;
}

inline std::shared_ptr<std::string> AcquireSharedString(std::size_t reserve_hint) {
  return GlobalSharedStringPool().Acquire(reserve_hint);
}

}  // namespace network_shared
