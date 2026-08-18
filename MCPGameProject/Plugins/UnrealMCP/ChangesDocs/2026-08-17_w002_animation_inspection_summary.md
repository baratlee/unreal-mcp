# W002 动画检查相关 UnrealMCP 插件修改汇总

## 1. 目的

W002 重构 Humanoid 移动动画时，需要同时检查 AnimBP 静态结构、动画资源 Sync Marker，以及 PIE 中真正运行的 AnimInstance 和 Linked Anim Layer。原有接口在嵌套 Anim Layer Graph、响应体积和运行时状态读取方面不足，因此扩展了 UnrealMCP 的动画只读检查能力。

本文只记录 `Plugins/UnrealMCP` 内的修改。Sampler 玩法代码、动画资产以及工程外 Python MCP Server 不属于本文修改范围。

## 2. 功能变化

| 功能 | 接口 | 变化 |
|---|---|---|
| AnimGraph Property Binding 查询 | `get_anim_graph_node_property_bindings` | 支持按节点 GUID 精确查询，也支持按嵌套 Graph 名称和节点类型枚举 Binding。 |
| Anim Layer 内状态机读取 | `get_anim_state_machine`、`get_anim_state_graph`、`get_anim_transition_graph` | Graph 查找扩展到 Anim Layer Interface 实现图及递归 SubGraph，不再局限于顶层 Function Graph。 |
| 紧凑 Graph 输出 | `get_blueprint_function_graph`、`get_anim_state_graph`、`get_anim_transition_graph` | 新增默认开启的紧凑输出和拓扑模式，减少坐标、空容器和冗长属性带来的响应体积。 |
| Sync Marker 查询 | `get_animation_sync_markers` | 返回 AnimSequence 的 Marker 名称、时间、归一化时间和 Notify Track。 |
| 运行时动画快照 | `get_animation_runtime_snapshot` | 返回指定角色 Mesh 的主、Linked、Post Process AnimInstance，状态机当前状态及 Linked Layer 实际目标。 |

以上能力均为只读检查，不创建、删除或修改 Blueprint Graph 节点和连线。

## 3. Graph 读取调整

### 3.1 统一 Graph 遍历

Blueprint Graph 收集逻辑统一为递归遍历：

- Ubergraph。
- Function、Macro、Delegate Graph。
- `ImplementedInterfaces[].Graphs`。
- 上述 Graph 的递归 `SubGraphs`。

遍历使用已访问集合避免重复访问和循环引用。该逻辑由 `get_blueprint_function_graph`、状态机查找、Graph 名称查找、节点 GUID 查找和 Property Binding 枚举共同使用。

### 3.2 Property Binding 查询

`get_anim_graph_node_property_bindings` 支持两种模式：

- `node_guid`：跨顶层 Graph 与 SubGraph 查找一个 AnimGraph 节点。
- `graph_name` + 可选 `node_class`：枚举指定嵌套 Graph 中的匹配节点。

响应包含节点 GUID、节点类型、所属 Graph 和结构化 Binding；节点没有 Binding 时返回空数组。

### 3.3 紧凑与拓扑输出

Graph 读取接口支持：

- `compact_output=true`：默认模式，保留节点语义、Pin 和连接关系，省略编辑器坐标、空容器和冗长属性。
- `compact_output=false` + `pin_payload_mode="full"`：完整诊断回退，保持旧响应结构。
- `topology_only=true`：只保留语义节点，并以 Graph 级 `edges` 数组返回每条连接一次。

优先级为：

```text
topology_only
  > compact_output
      > output_profile（旧兼容字段）
```

`topology_only` 生效时，响应使用 `output_profile="topology"`，Pin Payload 固定为 `names_only`。

## 4. 动画资源与运行时读取

### 4.1 Sync Marker

`get_animation_sync_markers(asset_path)` 只接受 AnimSequence，返回：

- 动画完整路径与时长。
- Marker 总数与去重名称。
- 按时间排序的 Marker。
- 每个 Marker 的秒数、归一化时间、Track Index 和 Track Name。

该接口用于区分资源未配置 Marker、Marker 时间错误和运行时 Sync Group 无有效位置三类问题。

### 4.2 Runtime Snapshot

`get_animation_runtime_snapshot(...)` 以 SkeletalMeshComponent 为检查边界，可通过 PIE Instance、Player Index、Actor Path 和 Component Name 精确选择目标。

响应包含：

- World、Actor、MeshComponent 和帧号。
- Main、Linked、Post Process AnimInstance。
- 每个实例的 Baked State Machine 当前状态、持续时间和权重。
- Linked Anim Layer 节点的 Layer、Interface、动态链接函数、实际 Target Class 和 Target Instance。

存在多个候选 World、Actor 或 Mesh 时返回候选列表，不静默选择不明确目标。

## 5. 插件内修改文件

| 文件 | 修改内容 |
|---|---|
| `Source/UnrealMCP/Public/Commands/UnrealMCPBlueprintCommands.h` | 声明 AnimGraph Binding 和扩展 Graph 读取接口。 |
| `Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp` | 统一 Graph 遍历、Property Binding 序列化、紧凑节点输出和拓扑 Edge 输出。 |
| `Source/UnrealMCP/Public/Commands/UnrealMCPAnimationCommands.h` | 声明 Sync Marker 与 Runtime Snapshot 命令处理函数。 |
| `Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp` | 实现动画资源 Marker 序列化和运行时 AnimInstance/State Machine/Linked Layer 快照。 |
| `Source/UnrealMCP/Private/UnrealMCPBridge.cpp` | 注册并路由新增命令。 |
| `ChangesDocs/ToolList.md` | 登记新增接口。 |
| `ChangesDocs/2026-08-13_*.md`、`2026-08-14_*.md` | 记录各项接口的独立设计与验证说明。 |

## 6. 插件外配套依赖

工程外 Python MCP Server 需要同步暴露参数与工具 Schema，例如：

- `Python/tools/blueprint_tools.py`
- `Python/tools/animation_tools.py`

这些文件不位于 `Plugins/UnrealMCP`，不属于本插件文档的修改清单，但 C++ 命令字段变更后必须同步更新，并重启 Python MCP/Codex stdio 会话才能加载新 Schema。

## 7. 验证状态

### 当前静态确认

- 插件源码中存在新增命令处理、Bridge 路由和 ToolList 登记。
- Graph 遍历已覆盖 Interface Graph 与递归 SubGraph。
- Sync Marker 与 Runtime Snapshot 均保持只读。

### 已有运行验证记录

- `compact_output=true` 与完整输出回退已完成实时调用验证。
- `topology_only`、Sync Marker、Runtime Snapshot 以及 Anim Layer 状态机读取仍应以当前插件重新编译、重启 Editor 和 MCP Server 后的实际调用结果为准。

插件或 Schema 变化后的验证顺序：

1. 编译 UnrealMCP 插件。
2. 重启 Unreal Editor。
3. 重启 Python MCP Server。
4. 重启 Codex/MCP stdio 会话。
5. 分别验证紧凑、完整、拓扑、Marker 和 PIE Runtime Snapshot 响应。

## 8. 边界

- 不包含 Sampler 的 Humanoid、Mover、Equipment 或测试代码。
- 不包含 W002 AnimBP、Animation Sequence 和 Blend Space 资产。
- 不包含任何 Blueprint Graph 写入。
- 不把工具接口存在视为 Editor 在线或运行时验证已经完成。
