# State Alias 读取扩展（2026-08-22）

## 概述

扩展 State Machine MCP 对 State Alias 的支持：`get_anim_state_machine` 返回 State Alias 配置，`add_anim_transition` 可将 State Alias 作为 Transition 源或目标。现有 `state_count` 与 `states` 字段继续只统计普通 State 和 Conduit，避免改变既有调用方语义。

## 新增返回字段

| 字段 | 说明 |
|---|---|
| `state_alias_count` | 当前 State Machine 中的 State Alias 数量 |
| `state_aliases` | State Alias 数组 |
| `state_aliases[].name` | Alias 名称 |
| `state_aliases[].global_alias` | 是否为 Global Alias |
| `state_aliases[].aliased_state_count` | Alias 关联的状态数量 |
| `state_aliases[].aliased_states` | 按名称稳定排序的关联状态列表 |

## 设计边界

- 本扩展只读取已有 State Alias，不创建或修改 Graph 节点。
- `add_anim_transition` 的端点解析覆盖普通 State、Conduit 和 State Alias；最终是否合法仍由 Animation State Machine Schema 判定。
- Transition 的 `source` / `target` 继续使用节点的状态名称，因此以 Alias 为端点的 Transition 无需改变格式。
- 无 State Alias 时固定返回 `state_alias_count: 0` 与空 `state_aliases`，便于调用方统一处理。

## 修改文件

- `Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp`
- `ChangesDocs/ToolList.md`
