# unreal-mcp 扩展：StateTree 创建 / 读取 / 写入工具（10 条）

**日期**：2026-04-28
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件 C++ 侧）
- `C:\Workspace\git\unreal-mcp`（Python MCP server 侧）

## 概述

新增 StateTree 资产的完整 MCP 操作能力：从零创建 StateTree、递归读取树结构和节点 detail 面板属性、增删改状态 / Task / Transition、以及编译。

## 文件变更

### 新增

| 文件 | 说明 |
|---|---|
| `Public/Commands/UnrealMCPStateTreeCommands.h` | StateTree 命令处理类声明 |
| `Private/Commands/UnrealMCPStateTreeCommands.cpp` | 10 个 handler 实现（约 800 行） |
| `Python: tools/statetree_tools.py` | 10 个 Python MCP 工具注册 |

### 修改

| 文件 | 变更 |
|---|---|
| `UnrealMCPBridge.h` | 新增 `#include` + `TSharedPtr<FUnrealMCPStateTreeCommands> StateTreeCommands` 成员 |
| `UnrealMCPBridge.cpp` | 构造 / 析构 / `ExecuteCommand` 路由增加 10 条 StateTree 命令 |
| `UnrealMCP.Build.cs` | `PrivateDependencyModuleNames` 增加 `StateTreeModule`、`StateTreeEditorModule`、`GameplayTags` |
| `UnrealMCP.uplugin` | `Plugins` 增加 `StateTree` |
| `Python: unreal_mcp_server.py` | `import` + `register_statetree_tools(mcp)` |

## 新增 10 条工具

### `create_state_tree`

**参数**：`asset_path`（必），`schema_class`（可选，如 `StateTreeComponentSchema`）。

**行为**：
1. `NewObject<UStateTreeFactory>()` + `SetSchemaClass()` 设置 Schema
2. `FactoryCreateNew` 创建 UStateTree，Factory 负责初始化 EditorData
3. `FAssetRegistryModule::AssetCreated` + `MarkPackageDirty`（不自动保存）
4. 资产已存在时报错、不覆盖

**返回**：`success / asset_path / schema / saved=false`

### `get_state_tree_info`

**参数**：`asset_path`。

**行为**：递归遍历 `UStateTreeEditorData::SubTrees`，对每个 `UStateTreeState` 输出：
- 基本信息：`name / id / type / selection_behavior / tasks_completion / tag`
- 子节点：`tasks[]`（id + name + node_struct_type + instance_struct_type）
- `enter_conditions[]`、`transitions[]`（id + trigger + type + target + priority + conditions）
- `children[]`（递归）
- 顶层同时输出 `evaluators[]` 和 `global_tasks[]`

**返回**：完整 JSON 树结构

### `get_state_tree_node_properties`

**参数**：`asset_path`、`node_id`（GUID）。

**行为**：通过 GUID 在整棵树中定位 `FStateTreeEditorNode`，用 UE 反射（`TFieldIterator<FProperty>`）遍历 `Node` 和 `Instance` 两个 `FInstancedStruct` 的所有 `CPF_Edit | CPF_BlueprintVisible` 属性，按类型序列化为 JSON：
- bool / int / float / double / FString / FName / FText → 原生 JSON 类型
- enum（FEnumProperty / FByteProperty with Enum） → 枚举值名字符串
- FGameplayTag → tag 字符串
- FGameplayTagContainer → 字符串数组
- UObject* / TSoftObjectPtr → 资产路径
- 其他 struct → `ExportTextItem_Direct` 导出文本

**返回**：`success / node_id / name / node{struct_type, properties{}} / instance{struct_type, properties{}}`

### `add_state_tree_state`

**参数**：`asset_path`、`state_name`（必），`parent_state_name`（可选），`state_type`（可选，默认 `State`）。

**行为**：
- `parent_state_name` 为空 → `EditorData->AddSubTree()`（根状态）
- 否则 → 递归查找父状态 → `ParentState->AddChildState()`
- 支持 `state_type`：State / Group / Linked / LinkedAsset / Subtree

**返回**：`success / state_name / state_id`

### `remove_state_tree_state`

**参数**：`asset_path`、`state_name`。

**行为**：递归查找状态，通过 `UStateTreeState::Parent` 判断层级，从 `Parent->Children` 或 `EditorData->SubTrees` 中移除。

**返回**：`success / removed_state`

### `set_state_tree_state_property`

**参数**：`asset_path`、`state_name`（必），以下均可选：`name`、`type`、`selection_behavior`、`tasks_completion`、`tag`、`enabled`。

**行为**：只修改提供了值的字段，通过 `StaticEnum` 解析枚举名。

**返回**：`success / state_name / changed`

### `add_state_tree_task`

**参数**：`asset_path`、`state_name`、`task_type`（struct 名，如 `FStateTreeDelayTask`）。

**行为**：
1. 自动去掉 F 前缀后通过 `FindFirstObject<UScriptStruct>` 查找
2. `State->Tasks.AddDefaulted_GetRef()` 创建 `FStateTreeEditorNode`
3. `Node.InitializeAs(TaskStruct)` 初始化节点
4. 从 `FStateTreeNodeBase::GetInstanceDataType()` / `GetExecutionRuntimeDataType()` 自动初始化 Instance 和 RuntimeData

**返回**：`success / node_id（GUID，供后续属性编辑）/ task_type / state_name`

### `add_state_tree_transition`

**参数**：`asset_path`、`state_name`、`trigger`（必）、`transition_type`（必），可选：`target_state_name`、`event_tag`、`priority`、`delay_transition`、`delay_duration`。

**行为**：调用 `UStateTreeState::AddTransition()`，支持两种重载（带 / 不带 EventTag）。GotoState 类型时必须指定 `target_state_name`。

**trigger 枚举**：OnStateCompleted / OnStateSucceeded / OnStateFailed / OnTick / OnEvent / OnDelegate
**transition_type 枚举**：None / Succeeded / Failed / GotoState / NextState / NextSelectableState

**返回**：`success / transition_id / state_name / trigger / transition_type`

### `set_state_tree_node_property`

**参数**：`asset_path`、`node_id`（GUID）、`property_name`、`property_value`、`target`（`instance` 或 `node`，默认 `instance`）。

**行为**：通过 GUID 查找节点 → 定位 `FInstancedStruct`（Instance 或 Node）→ `FindPropertyByName` → 按类型写入：
- bool / int / float / double / FString / FName → 原生类型转换
- enum → `GetValueByNameString` 解析
- FGameplayTag → `RequestGameplayTag`
- UObject* → `LoadObject` + `SetObjectPropertyValue`
- 其他 → `ImportText_Direct` 兜底

**返回**：`success / node_id / property_name / target`

### `compile_state_tree`

**参数**：`asset_path`。

**行为**：`StateTree->Modify()` + `PostEditChangeProperty()` 触发编辑器重编译流程，将 EditorData 烘焙为运行时格式。

**返回**：`success / message / asset_path`

## 典型工作流

```
1. create_state_tree("/Game/AI/ST_Test", "StateTreeComponentSchema")
2. add_state_tree_state(state_name="Root")
3. add_state_tree_state(state_name="Idle", parent_state_name="Root")
4. add_state_tree_state(state_name="Combat", parent_state_name="Root")
5. add_state_tree_task(state_name="Idle", task_type="FStateTreeDelayTask")
   → 返回 node_id
6. get_state_tree_node_properties(node_id=...)
   → 查看 Duration / RandomDeviation / bRunForever
7. set_state_tree_node_property(node_id=..., property_name="Duration", property_value=5)
8. add_state_tree_transition(state_name="Idle", trigger="OnStateCompleted",
      transition_type="GotoState", target_state_name="Combat")
9. compile_state_tree(...)
```

## 验证记录

2026-04-28 全部 10 条工具经 MCP 实测通过：

| 工具 | 结果 |
|---|---|
| `create_state_tree` | 创建成功，Schema = StateTreeComponentSchema |
| `get_state_tree_info` | 递归返回 Root → [Idle, Combat] 结构 |
| `add_state_tree_state` | 根状态 + 子状态均正常 |
| `add_state_tree_task` | FStateTreeDelayTask 添加，返回 node_id |
| `get_state_tree_node_properties` | 读到 Duration=1, RandomDeviation=0, bRunForever=false |
| `set_state_tree_node_property` | Duration 写为 5，回读确认 |
| `set_state_tree_state_property` | Idle 的 selection_behavior 改为 TryEnterState |
| `add_state_tree_transition` | OnStateCompleted → GotoState Combat |
| `remove_state_tree_state` | Combat 已移除 |
| `compile_state_tree` | 编译触发成功 |

## 已知限制

1. **Task struct 查找**：仅支持引擎已加载模块中注册的 USTRUCT；蓝图 Task 需传完整路径
2. **compile_state_tree**：通过 `PostEditChangeProperty` 间接触发编译，无法返回编译器详细日志
3. **同名状态**：`FindStateByName` 返回第一个匹配项；树中存在同名状态时建议通过 state_id 操作（当前 API 暂只按名字查找）
4. **Factory 默认创建 Root**：`UStateTreeFactory::FactoryCreateNew` 会自动生成一个名为 "Root" 的初始状态
