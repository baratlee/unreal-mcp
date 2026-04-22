# unreal-mcp 扩展：Batch C.2（IKRetargeter 创建 + Op Stack 配置 + Retarget Pose）

**日期**：2026-04-22
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 对应需求清单

`Docs/UnrealMCP_扩展需求清单.md` 的：
- Item 10（P0 写）—— `create_ik_retargeter`
- Item 11（P0 写）—— 6 条 op stack 子命令：`set_op_enabled` / `set_op_field` / `add_op` / `add_pin_bones_entry` / `set_chain_mapping` / `auto_map_chains`
- Item 12（P1 写）—— `set_ik_retargeter_retarget_pose`

## 新增 8 条工具

### `create_ik_retargeter`

**参数**：`asset_path`（必），`source_ik_rig_path`（可选），`target_ik_rig_path`（可选）。

**行为**：
1. `CreatePackage` + `NewObject<UIKRetargeter>`，`RF_Public | RF_Standalone`
2. `FAssetRegistryModule::AssetCreated` + `MarkPackageDirty`（**不自动保存**）
3. 通过 `UIKRetargeterController::SetIKRig(ERetargetSourceOrTarget::Source/Target, IKRig)` 装配源/目标 rig；任一找不到 → 在返回 JSON 写 `source_warning` / `target_warning`，不算总失败
4. 资产已存在时报错、不覆盖

**返回**：`success / asset_path / source_assigned / target_assigned / source_warning? / target_warning? / saved=false`

### `set_ik_retargeter_op_enabled`

**参数**：`asset_path`、`op_index`、`enabled`。
**底层**：`UIKRetargeterController::SetRetargetOpEnabled(int32, bool)`。

### `set_ik_retargeter_op_field`（通用反射 setter）

**参数**：`asset_path`、`op_index`、`field_path`（点分路径，如 `Settings.bCopyTranslation`）、`value`（T3D 文本）。

**行为**：
1. `GetRetargetOpStructAtIndex(op_index)` → `FInstancedStruct*`
2. `ResolveStructFieldPath` 按点分路径走 `FindPropertyByName`，逐级下钻 `FStructProperty`（不允许跨非-struct 节点）
3. 末端 `FProperty::ImportText_Direct` 解析 `value`：覆盖 bool / int / float / FName / FString / enum / FVector / FRotator / FQuat / 嵌套 struct / array（用 UE T3D 文本格式，例如 `(Pitch=0,Yaw=90,Roll=0)`）

**返回**：`success / asset_path / op_index / field_path / op_struct`

**配套写法举例**（修改 Pin Bones op 的 translation 模式）：
- `field_path="Settings.bCopyTranslation"`、`value="false"`
- `field_path="Settings.TranslationMode"`、`value="CopyLocalPositionRelativeOffset"`
- `field_path="Settings.SkeletonToCopyFrom"`、`value="Source"`

### `add_ik_retargeter_op`

**参数**：`asset_path`、`op_struct_name`（全路径或裸名）、`insert_after_index`（可选）。

**底层**：`AddRetargetOp(UScriptStruct*)` → 末尾追加 → 若 `insert_after_index` ≥ 0，调 `MoveRetargetOpInStack(NewIndex, insert_after_index+1)`。UE 因执行顺序约束可能再次调整最终位置；通过 op 名重新解析 `final_index`。

**返回**：`success / asset_path / op_index（最终位置）/ reordered`

### `add_ik_retargeter_pin_bones_entry`

**参数**：`asset_path`、`op_index`、`bone_to_copy_from`、`bone_to_copy_to`。

**行为**：op 必须 `IsChildOf(FIKRetargetPinBoneOp::StaticStruct())`；构造 `FPinBoneData{BoneToCopyFrom.BoneName, BoneToCopyTo.BoneName}`，`Add` 到 `Settings.BonesToPin`。

**注**：per-op 的 translation/rotation 模式 / copy 标志属于 op 级，**不在 entry 上** —— 用 `set_ik_retargeter_op_field` 改 `Settings.bCopy*` / `Settings.*Mode`。

### `set_ik_retargeter_chain_mapping`

**参数**：`asset_path`、`op_index`、`target_chain_name`（必）、`source_chain_name`（可选，空字符串 → 清空映射）。

**底层**：`SetSourceChain(SourceFName, TargetFName, OpName)`，op_index → op_name 通过 `GetOpName(int32)`。

### `ik_retargeter_auto_map_chains`

**参数**：`asset_path`、`op_index`、`mode`（5 选 1：`MapAllExact` / `MapOnlyEmptyExact` / `MapAllFuzzy` / `MapOnlyEmptyFuzzy` / `ClearAll`）。

**映射逻辑**：UE5.7 的 `EAutoMapChainType` 实际上只有 `Exact / Fuzzy / Clear` 三值；需求清单的 5 种 mode 是 `(EAutoMapChainType, bForceRemap)` 二维笛卡尔积。

| mode | EAutoMapChainType | bForceRemap |
|---|---|---|
| MapAllExact | Exact | true |
| MapOnlyEmptyExact | Exact | false |
| MapAllFuzzy | Fuzzy | true |
| MapOnlyEmptyFuzzy | Fuzzy | false |
| ClearAll | Clear | true |

### `set_ik_retargeter_retarget_pose`

**参数**：`asset_path`、`side`（`Source` / `Target`）、`bone_name`、`rotation_quat`（首选 `[x,y,z,w]`）或 `rotation_euler`（备选 `[pitch,yaw,roll]`，单位度）。

**底层**：`SetRotationOffsetForRetargetPoseBone(BoneName, FQuat, ERetargetSourceOrTarget)`。**只作用于当前激活 pose**；要切到别的 pose 需先调 `SetCurrentRetargetPose`（暂未单独导出）。

## 修改的文件

### C++ 端（UnrealMCP 插件）

| 文件 | 改动 |
|---|---|
| `Public/Commands/UnrealMCPAnimationCommands.h` | 声明 8 个新 handler |
| `Private/Commands/UnrealMCPAnimationCommands.cpp` | 新 include 4 条（`Retargeter/IKRetargetChainMapping.h`、`Retargeter/RetargetOps/PinBoneOp.h`、`RetargetEditor/IKRetargeterController.h`，已有的 `IKRetargeter.h` / `IKRigController.h` 复用）；路由 8 条；新匿名命名空间辅助 `GetIKRetargeterControllerForPath` / `ParseSourceOrTarget` / `ResolveStructFieldPath`；8 个 handler 实现 |
| `Private/UnrealMCPBridge.cpp` | Animation 路由段追加 8 条 |

### Python 端（unreal-mcp server）

| 文件 | 改动 |
|---|---|
| `Python/tools/animation_tools.py` | 注册 8 个新 MCP tool；顶部 import 增加 `Optional, List` |

## 验证顺序（编译后）

1. **create_ik_retargeter**：建 `/Game/Tests/RTG_Test`，传 `source_ik_rig_path` 和 `target_ik_rig_path` 为现有 IK_UEFN_Mannequin（同一个，做最小可用测试）
2. 用 `get_ik_retargeter_info`（已有）确认 source / target 都装上
3. `add_ik_retargeter_op` 加一个 `IKRetargetPinBoneOp`（裸名）
4. `set_ik_retargeter_op_enabled` 关闭再打开
5. `set_ik_retargeter_op_field`：在 op 0 上设 `Settings.bCopyTranslation` = `false`（值得到 op 的 settings 写入验证 ImportText 工作）
6. `add_ik_retargeter_pin_bones_entry`：往 PinBoneOp 里追加 `bone_to_copy_from=spine_05` `bone_to_copy_to=clavicle_l`
7. `ik_retargeter_auto_map_chains` mode=`MapAllExact`（先看现有 PelvisMotion / FK Chains op，需要先用 `add_ik_retargeter_op` 加一个 `IKRetargetFKChainsOp`）
8. `set_ik_retargeter_chain_mapping` 在 FKChains op 上单独覆盖一条
9. `set_ik_retargeter_retarget_pose` side=Target / bone_name=pelvis / rotation_euler=[0,90,0]
10. 全程用 `get_ik_retargeter_info` 回归

## 不在本批次

- `SetCurrentRetargetPose` / `CreateRetargetPose` / `RemoveRetargetPose` 等 retarget pose 集合管理（清单 Item 12 仅要求 rotation offset；其余等用户后续要求）
- `save_asset` 持久化命令（所有 create_* 都不自动保存，目前需要 UE 编辑器手动 Ctrl+S）
- 单独的 op-controller 暴露（GetOpController 返回 UIKRetargetOpControllerBase*，针对每个 op 类型有专属 controller；Item 11b 的通用反射 setter 已能覆盖大部分场景）

## 依赖与边界

- `IKRigEditor` 模块依赖（Batch C.1 已加），本批次复用
- `FInstancedStruct::GetMutableMemory()` / `GetMutablePtr<T>()` 都依赖 op 已 IsValid，handler 都做了校验
- `ImportText_Direct` 失败时返回 nullptr，handler 据此报错并把字段 CPP 类型回带给调用方排查
