# 2026-04-08 P2-3 Server Serialization Cache Execution Plan

## Internal Grade

`XL`

说明：

- 这是多阶段服务器结构改造，但当前没有用户明确的多代理授权，所以采用 `XL` 的波次化串行执行，不使用 `spawn_agent`。

## Waves

### Wave 1

冻结房间级 dispatch cache 模型与治理产物，明确只做服务器侧同房间同 tick prepared bytes 复用。

### Wave 2

先写失败测试：

- dispatch cache 单元测试，覆盖命中复用
- dispatch cache 单元测试，覆盖新 tick 覆盖旧 cache
- dispatch cache 单元测试，覆盖房间清理失效

### Wave 3

新增 `game_manager_dispatch_cache` 模块，提供统一的逻辑 packet -> prepared bytes materialize / reuse 入口。

### Wave 4

把状态同步分发接到 dispatch cache，保持现有 chunk 拆分、timing 刷新和 route 决策边界不变。

### Wave 5

把事件分发接到 dispatch cache，并补房间删除 / `game_over` 对应的 cache 清理点。

### Wave 6

构建、跑新增测试与聚焦回归、写 cleanup receipt。

## Verification Commands

```bash
env CCACHE_DISABLE=1 script/build_server.sh --debug
ctest --test-dir server/build-debug --output-on-failure -R "dispatch_cache|channel_policy|server_smoke|udp_sync_smoke|tcp_event_batch_smoke"
```

## Delivery Acceptance

- 只有在新增测试先失败后转绿，且关键回归 smoke 仍通过时，才允许声称 P2-3 服务器侧本轮范围完成。

## Rollback Rules

- 若实现过程中发现必须改客户端或协议外层结构，立即停止并回到用户确认。
- 若实现过程中发现需要跨 tick 长期缓存才能成立，立即停止并回到设计层重审。
