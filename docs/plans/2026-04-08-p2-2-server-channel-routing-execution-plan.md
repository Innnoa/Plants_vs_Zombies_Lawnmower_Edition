# 2026-04-08 P2-2 Server Channel Routing Execution Plan

## Internal Grade

`XL`

说明：

- 这是多阶段结构改造，但当前未获得多代理授权，所以采用 `XL` 的波次化串行执行，不使用 `spawn_agent`。

## Waves

### Wave 1

冻结频道模型与策略对象，新增 `channel_policy` 头/源文件。

### Wave 2

先写失败测试：

- policy 分类单元测试
- 至少一个集成测试或现有 smoke 扩展，验证关键事件/高频状态仍走正确链路

### Wave 3

把状态同步分发接到统一策略表。

### Wave 4

把事件分发接到统一策略表，明确关键/非关键事件的频道与 backlog 能力。

### Wave 5

把输入上行与基础 TCP 发送入口接到统一策略查询层，完成服务器内部语义收口。

### Wave 6

构建、跑聚焦回归、写 cleanup receipt。

## Verification Commands

```bash
env CCACHE_DISABLE=1 script/build_server.sh --debug
ctest --test-dir server/build-debug --output-on-failure -R "channel_policy|server_smoke|udp_sync_smoke|tcp_event_batch_smoke"
```

## Delivery Acceptance

- 只有在新增测试先失败后转绿，且关键回归 smoke 仍通过时，才允许声称 P2-2 第一阶段完成。

## Rollback Rules

- 若实现过程中发现必须改外层协议或客户端，立即停止并回到用户确认。
