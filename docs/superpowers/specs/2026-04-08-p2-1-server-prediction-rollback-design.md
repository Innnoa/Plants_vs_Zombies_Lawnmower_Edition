# 2026-04-08 P2-1 Server Prediction Rollback Design

## Summary

本设计只覆盖服务器侧 `P2-1` 的最小闭环版本，不新增协议字段，不改客户端。

目标是让服务端在预测历史窗口内：

- 接受“窗口内迟到但未处理过”的玩家旧输入
- 不再因为先收到高序号输入就提前确认所有更高序号
- 在缺口补齐后，基于历史快照和已处理输入片段重放玩家移动
- 若重放结果与当前权威状态有偏差，则修正服务器当前玩家状态，并通过现有状态同步链路触发纠偏

## Current State

当前服务器已有以下基础：

- `PlayerRuntime.history` 保存按 tick 记录的玩家状态快照
- `pending_inputs` 保存待处理输入
- `last_input_seq` 与 `last_processed_input_seq` 会进入同步消息
- `prediction_history_seconds` 已提供历史窗口配置

当前缺失点：

- `seq <= last_input_seq` 会被直接丢弃，无法接受迟到旧输入
- 没有“连续确认序号”与“已收到但未连续”的分离
- 没有保存“服务器实际如何消费输入”的历史片段
- 没有在缺口补齐后回放近窗口输入并修正权威状态

## Approach Options

### Option A: Minimal Server Replay On Existing Sync

做法：

- 增加连续确认序号与输入接收窗口
- 为玩家记录“已处理输入片段历史”
- 缺口补齐后，仅对该玩家做近窗口回放
- 纠偏通过现有状态同步返回

优点：

- 改动只在服务器
- 不新增协议
- 风险可控，能直接改善输入确认滞后和位置偏差

缺点：

- 只覆盖玩家移动/朝向，不覆盖战斗回滚
- 客户端仍然只是被动吃权威纠偏

### Option B: Full Deterministic Room Rollback

做法：

- 房间级保存所有实体历史
- 迟到输入到达后回滚整个房间到旧 tick 重跑

优点：

- 理论上最完整

缺点：

- 改动面过大
- 会影响 AI、掉落、伤害、升级等系统
- 不适合作为当前阶段服务器-only 落地

### Option C: Detect Only, No Correction

做法：

- 只记录偏差和缺口
- 不主动修正当前状态

优点：

- 实现最轻

缺点：

- 不能真正完成 P2-1 闭环

## Recommendation

采用 `Option A`。

原因：

- 满足“只改服务器”
- 直接覆盖当前主痛点：输入确认滞后、迟到输入导致的位置偏差
- 可以复用现有同步消息，不扩协议

## Design

### 1. Player Runtime Additions

给 `PlayerRuntime` 增加：

- `last_contiguous_input_seq`
  - 表示已经连续收到并可安全确认给客户端的最高序号
- `received_input_seqs`
  - 保存窗口内已收到但未必连续的输入序号
- `processed_input_segments`
  - 保存服务器实际消费过的输入片段历史
- `needs_prediction_reconciliation`
  - 标记是否需要在本 tick 结束后做服务器侧重放纠偏

每个 `processed_input_segment` 至少记录：

- `input_seq`
- `input_tick`
- `applied_tick`
- `delta_seconds`
- `move_direction`
- `rotation`

### 2. Input Acceptance Rules

输入接收调整为：

- 超出历史窗口的旧输入仍丢弃
- 若 `seq` 已在窗口集合内，丢弃重包
- 若 `seq` 小于等于 `last_contiguous_input_seq`，丢弃已确认输入
- 若 `seq` 合法但存在缺口，先缓存，不提前推进连续确认序号
- 若迟到输入填补了缺口，则推进 `last_contiguous_input_seq`

### 3. Processing Rules

逻辑帧消费输入时：

- 允许从 `pending_inputs` 中挑出当前最小可处理序号
- 对每个实际消费片段记录到 `processed_input_segments`
- 玩家同步里的 `last_processed_input_seq` 改为 `last_contiguous_input_seq`

这样客户端收到的确认序号不再跨越缺口。

### 4. Reconciliation Rules

当出现“迟到输入到达且进入历史窗口”时：

- 标记该玩家 `needs_prediction_reconciliation=true`
- 在 tick 内逻辑处理后，若标记存在：
  - 从“迟到输入发生前”的最近历史快照起点开始
  - 按 `applied_tick + input_seq` 顺序重放该玩家近窗口输入片段
  - 得到新的权威位置/朝向
  - 若与当前状态偏差超过阈值，则直接修正当前玩家状态
  - 同时更新 `authoritative_tick`
  - 标记玩家 dirty，并触发短期强制同步窗口

### 5. Scope Boundaries

本轮只回放：

- 玩家位置
- 玩家朝向
- 输入确认序号

本轮不回放：

- 敌人 AI
- 伤害结算
- 射弹生成/命中
- 道具掉落/拾取
- 升级流程

### 6. Error Handling

若缺少可用起始快照或重放数据不完整：

- 不尝试部分猜测回放
- 直接保守触发玩家强制同步
- 输出 debug 日志

### 7. Testing

先写失败测试：

1. `late_input_reconciliation_smoke_test`
   - 先发 `seq 2/3`，后发迟到 `seq 1`
   - 验证服务器确认序号不会越过缺口
   - 验证补齐后会产生可观测的权威位置纠偏

2. `late_input_window_reject_test`
   - 超出历史窗口的旧输入仍被拒绝

3. `duplicate_old_input_reject_test`
   - 已确认或已接收重复包不会重复进入处理链

## Risks

- 玩家输入片段历史若设计不当，可能造成内存膨胀
- 若起点快照选错，纠偏会放大误差
- 若纠偏过于频繁，可能引发客户端可见抖动

## Mitigations

- 历史与片段统一受 `prediction_history_seconds` 限制
- 只对单玩家回放
- 只在迟到输入真正补齐缺口后触发回放
- 纠偏设最小距离阈值，微小偏差不触发
