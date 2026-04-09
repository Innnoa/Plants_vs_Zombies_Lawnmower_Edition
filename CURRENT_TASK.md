# CURRENT_TASK.md

本文件是当前任务的唯一权威状态文件。任务切换时应覆盖旧内容，不保留无关历史。

最后更新：2026-04-09 17:02:00 CST

## 目标

- 产出一份基于当前仓库证据的“客户端对齐审计”Markdown，供后续另一位 AI 直接实施

## 范围

- 允许新增/更新文档、runtime receipts、审计状态文件
- 不改客户端业务代码
- 不改服务端业务代码
- 不改协议

## 验收标准

- 审计文档明确区分客户端必改项、协同项、已对齐项、纯服务端无需改项
- 至少覆盖房间控制、开局、状态同步、authoritative_tick、batch 事件、升级、重连、full snapshot 分片
- 至少有一组动态验证结果支撑服务端当前语义

## 相关文件

- docs/client-alignment/2026-04-09-client-alignment-audit.md
- docs/requirements/2026-04-09-client-alignment-audit.md
- docs/plans/2026-04-09-client-alignment-audit-execution-plan.md
- outputs/runtime/vibe-sessions/20260409-165413-client-alignment-audit/
- AI_GUIDE.md

## 当前状态

- 已完成：代码审计、协议核对、服务端 smoke 验证、最终 Markdown 产出
- 待处理：用户确认是否据此继续进入客户端实现阶段
- 阻塞：真实 full resync 若要彻底闭环，需要客户端与协议/服务端协同

## 下一步

1. 由用户审阅 docs/client-alignment/2026-04-09-client-alignment-audit.md
2. 若进入实现阶段，按文档中的 P0 → P1 顺序修客户端
3. 若允许协同改协议/服务端，再补 full resync 控制面
