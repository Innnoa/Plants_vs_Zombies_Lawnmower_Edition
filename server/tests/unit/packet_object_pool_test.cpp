#include <iostream>
#include <stdexcept>
#include <string>

#include "network/shared/packet_object_pool.hpp"

namespace {

[[noreturn]] void Fail(const std::string& msg) {
  throw std::runtime_error(msg);
}

void Expect(bool cond, const std::string& msg) {
  if (!cond) {
    Fail(msg);
  }
}

void TestReusesSameAddressAfterRelease() {
  const void* first_address = nullptr;
  {
    auto packet = network_shared::AcquireReusablePacket();
    packet->set_msg_type(lawnmower::MSG_S2C_GAME_OVER);
    first_address = packet.get();
  }

  auto packet = network_shared::AcquireReusablePacket();
  Expect(packet.get() == first_address,
         "归还后再次获取应复用同一 Packet 地址");
}

void TestClearsFieldsBeforeReuse() {
  {
    auto packet = network_shared::AcquireReusablePacket();
    packet->set_msg_type(lawnmower::MSG_S2C_GAME_OVER);
    packet->set_payload("stale-payload");
  }

  auto packet = network_shared::AcquireReusablePacket();
  Expect(packet->msg_type() == lawnmower::MSG_UNKNOWN,
         "复用 Packet 前应清空旧 msg_type");
  Expect(packet->payload().empty(), "复用 Packet 前应清空旧 payload");
}

}  // namespace

int main() {
  try {
    TestReusesSameAddressAfterRelease();
    TestClearsFieldsBeforeReuse();
    std::cout << "packet_object_pool_test: PASS\n";
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "packet_object_pool_test: FAIL: " << ex.what() << "\n";
    return 1;
  }
}
