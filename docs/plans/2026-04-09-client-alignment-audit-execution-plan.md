# 2026-04-09 Client Alignment Audit Execution Plan

## Internal Grade

`L`

理由：这是一次串行、证据优先的跨端审计任务，不需要多代理写入，但需要分阶段核对协议、服务端发送、客户端消费与测试证据。

## Waves

### Wave 1

读取仓库骨架、`AI_GUIDE.md`、近期 `vibe` 产物与现有网络审计文档，锁定近几轮服务端新增能力与边界。

### Wave 2

逐类核对协议与服务端发送路径，重点覆盖：

- 房间控制消息
- 开局结果
- 重连 ACK 与补全量
- 升级请求/选项/刷新/选择
- full snapshot 分片
- delta / `authoritative_tick`
- tick 事件 batch
- 非关键事件 backlog

### Wave 3

逐类核对客户端消费路径，重点确认：

- `Main.java` 顶层 TCP 分发是否真的能收到对应消息
- `GameScreen.java` 是否真正消费 batch / `authoritative_tick` / backlog 语义
- 房间 UI 是否按服务端结果驱动，而不是乐观切屏
- 初始/重同步请求是否真的存在端到端闭环

### Wave 4

执行最相关 smoke tests，为静态结论补动态证据：

- `server_smoke`
- `udp_sync_smoke`
- `tcp_event_batch_smoke`
- `full_snapshot_chunking_smoke`
- `upgrade_resume_smoke`

### Wave 5

输出最终 Markdown，按 `必须改` / `协同项` / `已对齐` / `无需改` 归类，并写入 `vibe` receipts、`CURRENT_TASK.md` 与 `AI_GUIDE.md`。

## Ownership Boundaries

- 主代理负责全部审计、验证、文档产出与状态回写
- 本次不启用子代理

## Verification Commands

```bash
rg -n "MSG_S2C_|authoritative_tick|snapshot_id|chunk_count|chunk_index|Upgrade|Reconnect|Heartbeat" proto/message.proto client/core/src/main/java server/src
ctest --test-dir server/build-debug --output-on-failure -R "server_smoke|udp_sync_smoke|tcp_event_batch_smoke|full_snapshot_chunking_smoke|upgrade_resume_smoke"
```

说明：

- 本地沙箱若限制创建监听/探测 socket，`ctest` 需要越权重跑

## Delivery Acceptance Plan

- 只有在“服务端已发 + 客户端未消费/错消费”的证据同时存在时，才标记为客户端待对齐项
- 纯服务端内部优化如果没有新增客户端协议/语义负担，统一归到“无需改”
- 客户端单改无法闭环的项，统一单列为“协同项”

## Completion Language Rules

- 不说“客户端全部没问题”或“都需要重做”
- 优先给出“必须改什么”，其次给出“已经对齐了什么”和“哪些其实不用改”

## Rollback Rules

- 本轮只新增/更新文档与 runtime receipts，不改业务代码
- 若用户不需要这些治理产物，可单独删除本轮新增文档与 receipts

## Phase Cleanup Expectations

- 生成 governance / skeleton / intent / stage lineage / execute / cleanup / delivery acceptance receipts
- 更新 `CURRENT_TASK.md`
- 追加 `AI_GUIDE.md` 审计记录
