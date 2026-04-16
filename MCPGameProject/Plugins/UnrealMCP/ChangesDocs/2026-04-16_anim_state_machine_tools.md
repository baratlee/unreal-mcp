# State Machine 读取工具（2026-04-16）

## 概述

新增 3 个 MCP 工具，用于读取 Animation Blueprint 中 State Machine 的内部结构：状态列表、转换线拓扑和转换条件图。

## 新增工具

| Python 工具 | 对应命令 | 作用 |
|---|---|---|
| `get_anim_state_machine` | `get_anim_state_machine` | 获取 State Machine 的完整结构：入口状态、所有状态（含 Conduit）、所有转换线（源/目标/优先级/crossfade/blend mode 等） |
| `get_anim_state_graph` | `get_anim_state_graph` | 获取某个状态内部的动画节点图（State 的 BoundGraph），输出格式与 `get_blueprint_function_graph` 一致 |
| `get_anim_transition_graph` | `get_anim_transition_graph` | 获取某条转换线的条件图 + 转换元数据（crossfade / blend mode / priority / automatic rule 等） |

## 改动文件

### 1. `Public/Commands/UnrealMCPBlueprintCommands.h`
- private 区追加 3 个 handler 声明

### 2. `Private/Commands/UnrealMCPBlueprintCommands.cpp`
- include 追加：`AnimGraphNode_StateMachineBase.h` / `AnimationStateMachineGraph.h` / `AnimStateNode.h` / `AnimStateTransitionNode.h` / `AnimStateConduitNode.h` / `AnimStateEntryNode.h` / `AnimationTransitionGraph.h` / `AnimGraphNode_TransitionResult.h`
- `HandleCommand` 追加 3 个 `else if` 分发分支
- 新增匿名命名空间 helper：`FindStateMachineGraph()` / `TransitionLogicTypeToString()` / `BlendOptionToString()`
- 文件末尾实现 3 个 handler

### 3. `Private/UnrealMCPBridge.cpp`
- Blueprint Commands 分支的 if 条件追加 3 个命令字符串

### 4. `UnrealMCP.Build.cs`
- Editor 分支 `PrivateDependencyModuleNames` 追加 `"AnimGraph"`

### 5. `Python/tools/animation_tools.py`（unreal-mcp 仓库）
- `register_animation_tools` 内追加 3 个 `@mcp.tool()` 函数

## 设计决策

- **3 个 handler 放在 BlueprintCommands 而非 AnimationCommands**：因为它们读的是 UEdGraph 蓝图图表数据，与 `get_blueprint_function_graph` 同类，可以直接复用匿名命名空间中的 `SerializeGraphNodeToJson()` 函数，不需要提取到公共头文件。
- **State 和 Transition 的 BoundGraph 直接复用现有序列化函数**：BoundGraph 是标准 UEdGraph，`SerializeGraphNodeToJson()` 完全适用。
- **state_machine_name 参数可选**：不传时自动取 AnimGraph 中的第一个 State Machine 节点，简化常见场景。

## 当前限制与未来扩展计划

### 只读一层 State Machine（当前）
- 本次实现**不处理嵌套 State Machine**（State 内部再放一个 State Machine 节点的情况）
- `get_anim_state_graph` 返回的是状态内部的直接 BoundGraph，如果里面有嵌套 SM 节点，它会作为普通节点出现在返回结果中，但不会递归展开

### 未来可能的扩展
1. **嵌套 State Machine 支持**：递归展开 State 内部的 SM 节点
2. **Custom Transition Graph**：读取自定义混合图（`CustomTransitionGraph`），目前只检测其存在（`has_custom_blend_graph` 字段）
3. **LinkedAnimLayer 读取**：通过 Layer 名查找实现类并读取实现图
4. **State Machine 写入工具**：创建状态、添加转换线等（当前只做只读）

## 遍历路径参考

```
AnimGraph 中的 StateMachine 节点 (UAnimGraphNode_StateMachineBase)
  └→ EditorStateMachineGraph (UAnimationStateMachineGraph)
       ├→ EntryNode (UAnimStateEntryNode) → 指向初始状态
       ├→ UAnimStateNode[] → 每个有 BoundGraph (状态内动画图)
       ├→ UAnimStateTransitionNode[] → 每个有:
       │    ├→ GetPreviousState() / GetNextState()
       │    ├→ CrossfadeDuration / BlendMode / PriorityOrder
       │    └→ BoundGraph (转换条件图，返回 bool)
       └→ UAnimStateConduitNode[] → 共享条件中间节点
```

## 新模块依赖

`AnimGraph`（Editor 模块）—— 提供 `UAnimGraphNode_StateMachineBase` / `UAnimStateNode` / `UAnimStateTransitionNode` / `UAnimStateConduitNode` / `UAnimStateEntryNode` / `UAnimationStateMachineGraph` 等编辑器类。
