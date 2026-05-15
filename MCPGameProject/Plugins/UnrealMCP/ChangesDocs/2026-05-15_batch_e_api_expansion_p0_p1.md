# 2026-05-15 — Batch E: API expansion (P0 + P1 from `UnrealMCP_API_ExpansionRequest.md`)

## 触发

需求来自 `PrjKunlun/Docs/UnrealMCP_API_ExpansionRequest.md`（5.7 项目内的"运行时动态动画重定向"任务）。当前 MCP 跑这条链路约 60–70%，AnimBP 节点写入、派生蓝图创建、磁盘持久化全部需要手动到编辑器里完成。本批次补齐 P0 + P1 共 **14 个工具**，目标把这类"跨骨架适配 / 动态重定向 / Pawn 派生"任务的自动化率拉到 ~100%（视觉迭代之外）。

> P2（PIE 启停、编译错误读取、ConstructionScript 读取、Log tail）留作下批次。
> Bug 5.1（`FindBlueprintByPath` 路径硬编码崩溃）此前已在 `UnrealMCPCommonUtils.cpp:161` 修复，本批次确认仍生效，未再改动。

## 工具总览（14 个新接口）

### P0 — AnimBP 链路彻底打通

| 接口 | 命令名 | 落点 |
|---|---|---|
| 1 | `create_anim_blueprint` | `UnrealMCPAnimationCommands.cpp` |
| 2 | `add_anim_graph_node` | `UnrealMCPBlueprintCommands.cpp` |
| 3 | `connect_anim_graph_nodes` | `UnrealMCPBlueprintCommands.cpp` |
| 4 | `set_anim_graph_node_property` | `UnrealMCPBlueprintCommands.cpp` |
| 5 | `create_blueprint_from_parent_blueprint` | `UnrealMCPBlueprintCommands.cpp` |

### P1 — 闭环：保存、函数图、IK Rig 修复

| 接口 | 命令名 | 落点 |
|---|---|---|
| 6 | `save_dirty_assets` | `UnrealMCPEditorCommands.cpp` |
| 7 | `add_blueprint_function_graph` | `UnrealMCPBlueprintCommands.cpp` |
| 8 | `connect_ik_rig_goal_to_solver` | `UnrealMCPAnimationCommands.cpp` |
| 9 | `set_ik_rig_solver_field` | `UnrealMCPAnimationCommands.cpp` |
| 10 | `delete_ik_rig_chain` | `UnrealMCPAnimationCommands.cpp` |
| 11 | `delete_ik_rig_goal` | `UnrealMCPAnimationCommands.cpp` |
| 12 | `delete_ik_rig_solver` | `UnrealMCPAnimationCommands.cpp` |
| 13 | `update_ik_rig_chain` | `UnrealMCPAnimationCommands.cpp` |
| 14 | `delete_asset` | `UnrealMCPEditorCommands.cpp` |

## 实施摘要

### 1. `create_anim_blueprint`

走 `UAnimBlueprintFactory::FactoryCreateNew`。参数：

- `asset_path`（必填）
- `target_skeleton`（必填，除非 `template=true`）
- `parent_class`（可选；默认 `UAnimInstance`；接受 classpath / GeneratedClass path / AnimBP 资产路径三种形式）
- `preview_skeletal_mesh`（可选）
- `template`（可选，默认 false；对应 Layer Interface / 模板创建模式）

未保存到磁盘（`saved=false`），调用方需 `save_dirty_assets` 落盘。

### 2. AnimGraph 写入三件套

#### `add_anim_graph_node`

- 接受 bare class name (`AnimGraphNode_RetargetPoseFromMesh`) 或全 classpath。
- 必须是 `UAnimGraphNode_Base` 子类。
- 默认放进名为 `AnimGraph` 的图；可通过 `graph_name` 切到子图。
- `NewObject` → `AddNode` → `CreateNewGuid` → `PostPlacedNewNode` → `AllocateDefaultPins` → `ReconstructNode` 标准流程。

#### `connect_anim_graph_nodes`

跨所有图（含 SubGraphs，BFS 查找）按 GUID 找节点。要求源/目标在同一图。复用 `FUnrealMCPCommonUtils::ConnectGraphNodes`。

#### `set_anim_graph_node_property`

三种模式可组合，按 (A) → (B) → (C) 顺序执行：

- **(A)** `field_path + value`：优先写内层 `FAnimNode_*` 结构（`anim_node_properties` 域），失败回退到 `UAnimGraphNode_*` UObject 的 UPROPERTY（`node_object_properties` 域）。`ImportText_Direct` 语义。
- **(B)** `property_binding`：在 `Binding->PropertyBindings` (TMap) 添加或替换一条 `FAnimGraphNodePropertyBinding`。这是关键能力 —— 例如把 `RetargetPoseFromMesh.SourceMeshComponent` 绑到 AnimInstance 变量 `LeaderMeshComponent`，对应于编辑器 Details 面板的拖拽绑定行为。
- **(C)** `clear_binding`：按 key 移除一条 binding。

所有变更走 `ReconstructNode` + `MarkBlueprintAsModified`。

### 3. `create_blueprint_from_parent_blueprint`

走 `FKismetEditorUtilities::CreateBlueprint(UClass* ParentClass, UPackage* Outer, FName Name, BPType, BPClassType, BPGenClassType)`。父类 = 父蓝图的 `GeneratedClass`；蓝图类 = 父蓝图实际类（保留 AnimBlueprint 等子类身份，使 AnimGraph 等 FunctionGraphs 由引擎正确生成）。

如果父蓝图未编译（`GeneratedClass == nullptr`），直接返回错误而非默默退化到 AActor。

### 4. `save_dirty_assets`

- 不传 `asset_paths`：扫所有已加载 UPackage，对 dirty 且非 Map 的 package 调 `UPackage::Save(SaveArgs)`。
- 传 `asset_paths`：逐个 `FindPackage` → 不存在则 `LoadPackage` → 走相同保存流程。
- **不保存 Map/Level**：风险面（PIE 中保存关卡、覆盖在编辑的关卡）远大于内容资产，且需走 `FEditorFileUtils::SaveDirtyPackages` 不同代码路径。需要保存关卡的场景请走单独脚本。
- 返回 `saved` / `skipped` / `failed` 三个数组 + 计数。`success` 仅当 failed 为空。

### 5. `add_blueprint_function_graph`

`FBlueprintEditorUtils::CreateNewGraph` + `AddFunctionGraph<UClass>(/*SignatureFromClass=*/nullptr)`。自动生成 Entry + Result 节点。重名提前拒绝（避免引擎默默 `_0` 自增）。

### 6. IK Rig 写入补丁（6 项）

| 接口 | 走的 controller API |
|---|---|
| `connect_ik_rig_goal_to_solver` | `UIKRigController::ConnectGoalToSolver(GoalName, SolverIndex)` |
| `set_ik_rig_solver_field` | `GetSolverStructAtIndex(idx)` → `ResolveStructFieldPath` → `ImportText_Direct` |
| `delete_ik_rig_chain` | `RemoveRetargetChain(ChainName)` |
| `delete_ik_rig_goal` | `RemoveGoal(GoalName)`（自动断开所有 solver + 清 chain 引用） |
| `delete_ik_rig_solver` | `RemoveSolver(SolverIndex)` |
| `update_ik_rig_chain` | `SetRetargetChainStartBone/EndBone/Goal` + 可选 `RenameRetargetChain` |

`set_ik_rig_solver_field` 复用既有 `ResolveStructFieldPath` 助手 —— 与 `set_ik_retargeter_op_field` 同源，路径形如 `RootBone` / `GlobalSettings.MassMultiplier`。

`update_ik_rig_chain` 一次调用可同时改 start/end/goal/rename，最少传一个字段；返回 `applied` 数组记录每项是否成功。

### 7. `delete_asset`

走 `UEditorAssetLibrary::DeleteAsset`（未加载 / 有引用就拒绝，更安全），或在 `force=true` 时走 `DeleteLoadedAsset`。这是 Content Browser 删除走的内部路径。

## Bridge 分发

`UnrealMCPBridge.cpp:260+` 三个分支表追加：

- Editor 分支：`save_dirty_assets`, `delete_asset`
- Blueprint 分支：`create_blueprint_from_parent_blueprint`, `add_anim_graph_node`, `connect_anim_graph_nodes`, `set_anim_graph_node_property`, `add_blueprint_function_graph`
- Animation 分支：`create_anim_blueprint`, `connect_ik_rig_goal_to_solver`, `set_ik_rig_solver_field`, `delete_ik_rig_chain`, `delete_ik_rig_goal`, `delete_ik_rig_solver`, `update_ik_rig_chain`

## Python 桥接

- `tools/editor_tools.py`：+`save_dirty_assets`、+`delete_asset`
- `tools/blueprint_tools.py`：+`create_blueprint_from_parent_blueprint`、+`add_anim_graph_node`、+`connect_anim_graph_nodes`、+`set_anim_graph_node_property`、+`add_blueprint_function_graph`
- `tools/animation_tools.py`：+`create_anim_blueprint`、+6 个 IK Rig 工具

每个 Python 函数都带 docstring，覆盖参数语义和坑点（如 binding 三模式、saved=false 需后续 save_dirty_assets）。

## 新依赖头文件

- `UnrealMCPAnimationCommands.cpp`: `+#include "Factories/AnimBlueprintFactory.h"`
- `UnrealMCPBlueprintCommands.cpp`: `+Animation/AnimBlueprint.h`, `+Animation/AnimInstance.h`, `+AnimationGraph.h`, `+AnimationGraphSchema.h`, `+EdGraphSchema_K2.h`, `+K2Node_FunctionEntry.h`, `+K2Node_FunctionResult.h`
- `UnrealMCPEditorCommands.cpp`: `+EditorAssetLibrary.h`, `+Misc/PackageName.h`, `+UObject/SavePackage.h`, `+UObject/UObjectIterator.h`, `+UObject/Package.h`

`UnrealMCP.Build.cs` 现有依赖已覆盖（`UnrealEd`、`Kismet`、`KismetCompiler`、`BlueprintGraph`、`AnimGraph`、`IKRig`、`IKRigEditor`、`EditorScriptingUtilities`）。无需修改。

## 验证步骤（建议手工跑一遍）

> 本批次代码量 ~600 行 C++ + ~400 行 Python。编辑器侧编译由开发者手动触发（per memory: `feedback_no_auto_build`）。

按依赖顺序：

1. **`create_anim_blueprint`** → 新建一个 `/Game/Test/ABP_MCPSmoke.ABP_MCPSmoke`，target_skeleton 用项目内任一 SK 骨架。验证：编辑器里能打开、AnimGraph 为空白默认 OutputPose。
2. **`add_anim_graph_node`** → 在 ABP_MCPSmoke 的 AnimGraph 里加一个 `AnimGraphNode_RetargetPoseFromMesh`。验证：节点出现、有默认引脚。
3. **`connect_anim_graph_nodes`** → 把上一步新增节点的 Pose 输出引脚连到 OutputPose 节点的输入。验证：连线显示。
4. **`set_anim_graph_node_property`** 三模式：
   - (A) `field_path="iKRetargeterAsset"`, `value="/Game/SomeRTG.SomeRTG"` → 节点 Details 面板显示该 RTG。
   - (B) `property_binding={"property_name":"SourceMeshComponent","property_path":["LeaderMeshComponent"]}` → Details 面板该字段显示绑定指示符。
   - (C) `clear_binding="SourceMeshComponent"` → 绑定消失。
5. **`add_blueprint_function_graph`** → 给一个测试蓝图加 `Init` 函数。验证：My Blueprint 面板里出现 Init 节点。
6. **`create_blueprint_from_parent_blueprint`** → 用 BP_KLPawn_Human 派生 BP_KLPawn_StoneGolem。验证：编译通过、继承链正确。
7. **IK Rig 补丁** —— 用一个已存在的 IK Rig：
   - `add_ik_rig_goal` 加一个孤儿 goal → `connect_ik_rig_goal_to_solver` → 编辑器警告消失。
   - `set_ik_rig_solver_field` 改 FBIK 的 `RootBone` / `Iterations`。
   - `delete_ik_rig_chain` / `delete_ik_rig_goal` / `delete_ik_rig_solver` / `update_ik_rig_chain` 每个跑一次。
8. **`save_dirty_assets`**（不传参）→ 上面所有 dirty 资产落盘，关闭编辑器再开应看到改动持久化。
9. **`delete_asset`** → 删除 step 1 的 ABP_MCPSmoke。验证：Content Browser 里消失。

## 已知边界

- **`save_dirty_assets` 不保存 Map**：见上文说明。
- **`set_anim_graph_node_property` 的 binding 写入**：跳过 `PinType` / `PromotedPinType` 等运行时编译期才填的字段。引擎编译 AnimBP 时会按 `PropertyName` 在 AnimInstance 类上反查实际 pin 类型；如果该变量在 AnimBP 上不存在，编译会报"Cannot resolve binding"。
- **`create_blueprint_from_parent_blueprint`** 要求父蓝图已编译过一次（有 `GeneratedClass`）。父蓝图刚被新建但未编译时会拒绝。
- **`add_anim_graph_node` 仅支持 `UAnimGraphNode_Base` 子类**。普通 K2Node 还是走原有 `add_blueprint_function_node` 等接口。

## 参考源码

- `Engine/Source/Editor/UnrealEd/Classes/Factories/AnimBlueprintFactory.h` — AnimBP 工厂参数
- `Engine/Source/Editor/UnrealEd/Public/Kismet2/KismetEditorUtilities.h:110-124` — `FKismetEditorUtilities::CreateBlueprint` 完整签名
- `Engine/Source/Editor/AnimGraph/Public/AnimGraphNode_Base.h:138-191` — `FAnimGraphNodePropertyBinding` 结构定义
- `Engine/Plugins/Animation/IKRig/Source/IKRigEditor/Public/RigEditor/IKRigController.h` — IK Rig 写入控制器（详见 `PrjKunlun/Docs/UESource/IKRigEditor/IKRigController.md`）
- `Engine/Source/Editor/UnrealEd/Public/Subsystems/EditorAssetSubsystem.h` 与 `EditorAssetLibrary.h` — 资产 save/delete API
