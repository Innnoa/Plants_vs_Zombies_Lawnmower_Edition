#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "message.pb.h"

namespace network_shared {

class PacketObjectPool;

class ReusablePacketHandle {
 public:
  ReusablePacketHandle() = default;

  ReusablePacketHandle(const ReusablePacketHandle&) = delete;
  ReusablePacketHandle& operator=(const ReusablePacketHandle&) = delete;

  ReusablePacketHandle(ReusablePacketHandle&& other) noexcept
      : pool_(other.pool_), packet_(std::move(other.packet_)) {
    other.pool_ = nullptr;
  }

  ReusablePacketHandle& operator=(ReusablePacketHandle&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Reset();
    pool_ = other.pool_;
    packet_ = std::move(other.packet_);
    other.pool_ = nullptr;
    return *this;
  }

  ~ReusablePacketHandle() { Reset(); }

  lawnmower::Packet* get() { return packet_.get(); }
  const lawnmower::Packet* get() const { return packet_.get(); }

  lawnmower::Packet& operator*() { return *packet_; }
  const lawnmower::Packet& operator*() const { return *packet_; }

  lawnmower::Packet* operator->() { return packet_.get(); }
  const lawnmower::Packet* operator->() const { return packet_.get(); }

 private:
  friend class PacketObjectPool;

  ReusablePacketHandle(PacketObjectPool* pool, std::unique_ptr<lawnmower::Packet> packet)
      : pool_(pool), packet_(std::move(packet)) {}

  void Reset();

  PacketObjectPool* pool_ = nullptr;
  std::unique_ptr<lawnmower::Packet> packet_;
};

class PacketObjectPool {
 public:
  ReusablePacketHandle Acquire() {
    std::unique_ptr<lawnmower::Packet> packet;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!free_list_.empty()) {
        packet = std::move(free_list_.back());
        free_list_.pop_back();
      }
    }

    if (!packet) {
      packet = std::make_unique<lawnmower::Packet>();
    }
    packet->Clear();
    return ReusablePacketHandle(this, std::move(packet));
  }

 private:
  friend class ReusablePacketHandle;

  void Release(std::unique_ptr<lawnmower::Packet> packet) {
    if (!packet) {
      return;
    }
    packet->Clear();
    std::lock_guard<std::mutex> lock(mutex_);
    constexpr std::size_t kMaxRetainedPackets = 256;
    if (free_list_.size() >= kMaxRetainedPackets) {
      return;
    }
    free_list_.push_back(std::move(packet));
  }

  std::mutex mutex_;
  std::vector<std::unique_ptr<lawnmower::Packet>> free_list_;
};

inline void ReusablePacketHandle::Reset() {
  if (pool_ == nullptr || !packet_) {
    return;
  }
  pool_->Release(std::move(packet_));
  pool_ = nullptr;
}

inline PacketObjectPool& GlobalPacketObjectPool() {
  static auto* pool = new PacketObjectPool();
  return *pool;
}

inline ReusablePacketHandle AcquireReusablePacket() {
  return GlobalPacketObjectPool().Acquire();
}

}  // namespace network_shared
