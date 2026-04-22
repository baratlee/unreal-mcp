# unreal-mcp 扩展：Batch B（IKRig solver 细节读取）+ Batch C.1（IKRig 创建与配置）

**日期**：2026-04-22
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 对应需求清单

`Docs/UnrealMCP_扩展需求清单.md` 的：
- Item 6（P1 读）—— 扩展 `get_ik_rig_info` 返回 solver 完整字段
- Item 8（P0 写）—— `create_ik_rig`
- Item 9（P0 写）—— 4 条 IKRig 配置子命令：`set_ik_rig_retarget_root` / `add_ik_rig_retarget_chain` / `add_ik_rig_goal` / `add_ik_rig_solver`

## Batch B：扩展 `get_ik_rig_info`

原输出 `solvers[]` 每条只给 `struct_name`，无法对照 solver 参数。本次在每条 solver 条目里追加 `fields` 字段（来自 `FJsonObjectConverter::UStructToJsonObject`），包含该 solver 的全部 UPROPERTY。

**兼容性**：仍保留 `struct_name`，`fields` 是**新增**字段，不破坏既有调用方。

> 需求清单提了独立工具 `get_ik_rig_solver_details(asset_path, solver_index)`。考虑到单个 IKRig 通常只有 1–3 个 solver，直接把 full 字段合并到 `get_ik_rig_info` 比多一次 round trip 更简单，维护面也小。保留清单原方案作为未来可选项（若真有 solver 体量过大需求）。

## Batch C.1：IKRig 写工具

### `create_ik_rig`

**参数**：`asset_path`（必），`preview_skeletal_mesh_path`（可选）。

**行为**：
1. 在指定包路径下 `CreatePackage` + `NewObject<UIKRigDefinition>`，`RF_Public | RF_Standalone`
2. `FAssetRegistryModule::AssetCreated` + `MarkPackageDirty`（**不自动保存** —— 需要用户编辑器保存或未来 `save_asset`）
3. 若传了 preview SKM：通过 `UIKRigController::GetController(NewAsset)->SetSkeletalMesh(Mesh)` 载入骨骼层级；失败或不兼容时在返回里写 `skeletal_mesh_warning`
4. 资产已存在时报错、不覆盖

**返回**：`success / asset_path / skeletal_mesh_assigned / skeletal_mesh_warning? / saved=false`

### `set_ik_rig_retarget_root`

**参数**：`asset_path`、`bone_name`。

**底层**：`UIKRigController::SetRetargetRoot(FName)`。成功时 `MarkPackageDirty`。

### `add_ik_rig_retarget_chain`

**参数**：`asset_path`、`chain_name`、`start_bone`、`end_bone`、`goal_name`（可选，空为无 goal）。

**底层**：`UIKRigController::AddRetargetChain(ChainName, Start, End, Goal)`。返回实际分配名（UE 可能加数字后缀避重名）。

### `add_ik_rig_goal`

**参数**：`asset_path`、`goal_name`、`bone_name`。

**底层**：`UIKRigController::AddNewGoal(GoalName, BoneName)`。返回 `assigned_goal_name`（可能被 uniquify）。

### `add_ik_rig_solver`

**参数**：`asset_path`、`solver_struct_name`（支持两种形式）：
- 全路径：`/Script/IKRig.IKRigFBIKSolver` → 直接走 `AddSolver(FString)`
- 裸 struct 名：`IKRigFBIKSolver` → 通过 `FindFirstObject<UScriptStruct>` 解析后走 `AddSolver(UScriptStruct*)`

**返回**：`solver_index`（在 stack 中的位置）。

**不涵盖**：solver 的内部参数（Root Bone / iteration count / preferred angles 等）—— 这些要走未来的 op-field 写工具（类似 Batch C.2 要做的 `set_ik_retargeter_op_field`）。

## 修改的文件

### C++ 端（UnrealMCP 插件）

| 文件 | 改动 |
|---|---|
| `UnrealMCP.Build.cs` | 新增 `IKRigEditor` 私有依赖（UIKRigController/UIKRetargeterController 所在模块） |
| `Public/Commands/UnrealMCPAnimationCommands.h` | 声明 5 个新 handler（`HandleCreateIKRig` 等） |
| `Private/Commands/UnrealMCPAnimationCommands.cpp` | 新增 include `RigEditor/IKRigController.h`；路由 5 条；新匿名命名空间 `SplitAssetPath` + `GetIKRigControllerForPath`；5 个 handler 实现；`HandleGetIKRigInfo` 的 solver 循环加 `fields` JSON 输出（Batch B） |
| `Private/UnrealMCPBridge.cpp` | Animation 路由段追加 5 条命令 |

### Python 端（unreal-mcp server）

| 文件 | 改动 |
|---|---|
| `Python/tools/animation_tools.py` | 注册 5 个新 MCP tool |

## 验证顺序（编译后）

1. **Batch B 回归**：重跑 `get_ik_rig_info` 对任一现有 IKRig（需先有一个可用的 IKRig 资产），确认 solver 条目里多了 `fields` blob
2. **create_ik_rig** → 测试资产 `/Game/Tests/IK_Test_Create`，传 `preview_skeletal_mesh_path=/Game/.../SKM_UEFN_Mannequin`，确认返回 `success=true / skeletal_mesh_assigned=true`
3. **set_ik_rig_retarget_root** → 在上面新建的 rig 上设 `bone_name="pelvis"`
4. **add_ik_rig_goal** → 添加 `goal_name="LeftHandIK"`、`bone_name="hand_l"`
5. **add_ik_rig_retarget_chain** → 添加 `chain_name="LeftArm"`、`start_bone="upperarm_l"`、`end_bone="hand_l"`、`goal_name="LeftHandIK"`
6. **add_ik_rig_solver** → 先试 `solver_struct_name="IKRigFBIKSolver"`（裸名），再试 `/Script/IKRig.IKRigFBIKSolver`（全路径），两者都应 OK
7. **回归读**：用扩展后的 `get_ik_rig_info` 读刚建的 rig，确认 root_bone / goals / retarget_chains / solvers 都在

## 不在本批次

- Batch C.2（下次编译周期）：Items 10 / 11 / 12 —— IKRetargeter 创建 + 6 条 op 子命令 + retarget pose 偏移
- solver 内部字段写（类似 `set_ik_retargeter_op_field`）—— 可能作为 C.2 或之后的独立 item

## 依赖与兼容性

- `IKRigEditor` 模块仅 editor 构建可用；`UnrealMCP.Build.cs` 原本已有 editor-only 块，这里在块内追加，与现有 `IKRig` 运行时依赖并存
- `UIKRigController::GetController` 是 `UFUNCTION` + `BlueprintCallable`，线程安全无锁，同一资产多次调用返回同一个 per-asset controller 单例
