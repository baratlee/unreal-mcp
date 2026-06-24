# create_anim_montage（从单条 AnimSequence 创建 Montage）

日期：2026-06-18
分支主题：补 MCP「建不了 Montage」缺口（攻击 GA 流程被卡在此处）

## 背景
攻击 GA 创建流程（见项目 skill `create-attack-ga`）需要先把攻击动画序列做成 Montage 再铺 notify。此前 MCP 只有 Montage 的**读**（`get_montage_composite_info`）和 notify 增删，**没有创建 Montage 的命令**，导致每次都要人去编辑器右键 Create AnimMontage，自动化断链。

## 改动
新增 1 个工具 `create_anim_montage`，复刻编辑器右键 "Create > Create AnimMontage"。

**C++**（`Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp` + 头 `Public/Commands/UnrealMCPAnimationCommands.h`）
- 新增 include `Factories/AnimMontageFactory.h`
- `HandleCommand` 派发新增 `create_anim_montage` → `HandleCreateAnimMontage`
- ⚠️ **两个注册点**：除 handler 内 `HandleCommand`，还必须在 `UnrealMCPBridge.cpp` 顶层路由表（routing 到 `AnimationCommands->HandleCommand` 的那串 `CommandType == ...` 条件）登记新命令名，否则 bridge 直接返回 `Unknown command`，根本到不了 handler。本次首遍漏了 bridge 这点，实测报 "Unknown command: create_anim_montage" 才补上。
- 新增 `HandleCreateAnimMontage`：
  - 入参 `asset_path`(必), `source_animation`(必), `slot_name`(选，默认 `DefaultSlot`), `blend_in`/`blend_out`(选，默认 0.25)
  - 路径拆包、重名拒绝、`source_animation` 必须是 `UAnimSequence`
  - 走 `UAnimMontageFactory`：设 `SourceAnimation` 后 `FactoryCreateNew` 自动派生 TargetSkeleton、建 `DefaultSlot` 单段、`SetCompositeLength`、补 "Default" section（依据引擎 `Editor/UnrealEd/Private/Factories/AnimMontageFactory.cpp` `FactoryCreateNew`）
  - 后处理暴露旋钮：`BlendIn/BlendOut.SetBlendTime()`、`SlotAnimTracks[0].SlotName`
  - `FAssetRegistryModule::AssetCreated` + `MarkPackageDirty`，**不自动 save**（调用方 `save_dirty_assets`）
  - 返回 asset_path/source_animation/skeleton/slot_name/play_length/blend_in_time/blend_out_time/saved

**Python**（`Python/tools/animation_tools.py`）
- 新增 `create_anim_montage` 工具，参数同上；`ast.parse` 自检通过
- 在已注册的 animation tools register 函数内，**无需改 `unreal_mcp_server.py`**

**模块依赖**：未改 Build.cs / .uplugin —— `UAnimMontageFactory` 在 `UnrealEd` 模块，该依赖随 Batch E `create_anim_blueprint`（同用 UnrealEd 工厂）已存在。

## 覆盖矩阵
| 场景 | 支持 |
|---|---|
| 单序列 → 单 slot 单段 Montage | ✅ |
| 自定义 slot 名 / blend in-out | ✅ |
| 多段拼接 / 多 section / Composite / 多动画合成 | ❌（按需另起） |
| notify 铺设 | ❌ 用现有 `add_animation_notify` 创建后铺 |

## 风险
- 低：纯新增命令，未碰任何现有路径/反射 setter。
- 工厂 `check(TargetSkeleton == NULL || TargetSkeleton == SourceSkeleton)`：我们不传 TargetSkeleton，只设 SourceAnimation，不会触发该 check。
- slot 名设成 AnimBP 未定义的 slot 时运行时不会播到该 slot——属调用方配置问题，非本命令缺陷。

## 调用范围（受影响命令）
仅新增 `create_anim_montage`。其余命令一字未改。

## 验证步骤（用户重编 + 重启编辑器 + 退 PIE 后）
1. **新功能 round-trip**：`create_anim_montage(asset_path=/Game/.../AM_DaulSword_LightAttack1, source_animation=/Game/Examples/RetargetAnim/Dual_Sword/.../AS_Combo_Attack_01_01_Seq)` → 成功；`get_montage_composite_info` 回读：slot=DefaultSlot、单段 anim_path=源序列、section "Default"、play_length≈1.367、blend 0.25/0.25。
2. **自定义参数**：带 `slot_name`/`blend_in=0.1` 再建一个 → 回读字段匹配。
3. **错误路径**：`source_animation` 传一个非 AnimSequence 或不存在路径 → 返清晰 error，不 crash；重名 asset_path → 返 already exists。
4. `save_dirty_assets` 后 .uasset 落盘，重开编辑器仍在。

## 后续
- 验证通过后回写 [[reference_mcp_capability_boundary]] 与 [[reference_unreal_mcp_commands]]。
- 引擎源码 `AnimMontageFactory.h/.cpp` 已按 read-ue-source 协议追加进 UE 源码优先解读列表。
