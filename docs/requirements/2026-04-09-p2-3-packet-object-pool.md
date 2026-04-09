# 2026-04-09 P2-3 Packet Object Pool Requirement

## Goal

完成服务器侧 `P2-3` 的最后一块轻量对象池化范围：为发送热路径里的 `lawnmower::Packet` 引入对象池，减少构包阶段反复创建 / 销毁临时 `Packet` 实例。

## Deliverable

- 服务器代码改动
- 至少一个新增 failing-first 测试，覆盖 `Packet` 归还后复用同一地址与 `Clear()` 语义
- 更新后的构建与测试结果

## Constraints

- 只改服务器
- 不改客户端
- 不改 `proto/message.proto` 外层包结构
- 只处理发送热路径
- 不扩展到业务 message 对象池化
- 不使用 protobuf arena

## Acceptance Criteria

- 存在一个可复用的 `Packet` 对象池入口
- 发送热路径里的临时 `lawnmower::Packet` 改为从池里获取并归还
- 归还后的 `Packet` 再次取出时不会残留旧 `msg_type / payload`
- 关键回归 smoke 仍通过

## Manual Spot Checks

- `server/include/network/shared/packet_object_pool.hpp`
- `server/src/network/tcp/tcp_session.cpp`
- `server/src/network/udp/udp_server.cpp`
- `server/src/game/managers/game_manager_dispatch_cache.cpp`
- `server/src/game/managers/game_manager_sync_dispatch.cpp`
- `server/tests/unit/packet_object_pool_test.cpp`

## Non-Goals

- 业务 message 对象池化
- 接收链路 `Packet` 解析
- 客户端改动
- protobuf 对象池化
