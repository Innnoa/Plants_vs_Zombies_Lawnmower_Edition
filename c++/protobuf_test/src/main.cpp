#include <iostream>
#include <spdlog/spdlog.h>
#include "messages.pb.h"

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // 测试序列化
    lawnmower::C2S_Login login;
    login.set_player_name("测试玩家");

    std::string data = login.SerializeAsString();
    spdlog::info("序列化大小: {} 字节", data.size());

    // 测试反序列化
    lawnmower::C2S_Login parsed;
    parsed.ParseFromString(data);
    spdlog::info("玩家名: {}", parsed.player_name());

    // 测试 Packet 封装
    lawnmower::Packet packet;
    packet.set_msg_type(1);
    packet.set_payload(data);

    spdlog::info("Packet 大小: {} 字节",
                 packet.SerializeAsString().size());

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
