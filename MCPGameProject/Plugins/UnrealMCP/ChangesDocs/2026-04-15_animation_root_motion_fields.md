# unreal-mcp 动画工具：补充 Root Motion 字段

**日期**：2026-04-15
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件 C++）
- `C:\Workspace\git\unreal-mcp`（Python 工具 docstring）

## 修改目的

之前 `get_animation_info` 只返回 `play_length` / `num_frames` / `skeleton`，没有 root motion 信息。在解读 `Variant_Combat::AM_ComboAttack` 时遇到的需求是：定位"哪一段 AnimSequence 启用了 Root Motion"。

UE5.7 中 Montage 上的 `bEnableRootMotionTranslation` / `bEnableRootMotionRotation` 已 deprecated（`AnimMontage.h:696-702` 注释 `// DEPRECATED in 4.5 root motion is controlled by anim sequences`），编辑器面板里也不显示。真正生效的开关在 `UAnimSequence::bEnableRootMotion`（`AnimSequence.h:318-320`）。Montage 的 `HasRootMotion()` 是把 `SlotAnimTracks` 里所有底层 `AnimSequence` 的 `bEnableRootMotion` 做 OR 聚合（`AnimMontage.cpp:918-928`）—— 任意一段开了，整段攻击播放期间 CMC 都会用根骨速度覆盖玩家输入算出的 Velocity，所以"想让攻击中能移动"必须把所有引用段都关掉。

为了能直接通过 MCP 定位是哪一段触发的聚合，本次：
1. 在 `get_animation_info` 上加 root motion 字段
2. 在 `get_montage_composite_info` 的每个 segment 上加 root motion 字段（这样不用对每段再调一次 `get_animation_info`）
3. 在 `get_montage_composite_info` 顶层加聚合后的 `has_root_motion`

## 新增字段

**`get_animation_info`**：
- `has_root_motion` —— 调用虚函数 `UAnimSequenceBase::HasRootMotion()`。AnimSequence 直接返回 `bEnableRootMotion`；AnimMontage 走 OR 聚合
- `b_enable_root_motion` —— `UAnimSequence::bEnableRootMotion`（仅 AnimSequence）
- `root_motion_root_lock` —— `RefPose` / `AnimFirstFrame` / `Zero`（仅 AnimSequence）
- `b_force_root_lock` —— `UAnimSequence::bForceRootLock`（仅 AnimSequence）
- `b_use_normalized_root_motion_scale` —— `UAnimSequence::bUseNormalizedRootMotionScale`（仅 AnimSequence）

**`get_montage_composite_info`**：
- 顶层新增 `has_root_motion`（OR 聚合结果）
- 每个 `segments[i]` 新增同一组 5 个字段（仅在 segment 引用的资产是 `UAnimSequence` 时输出）

AnimMontage 自身不输出 `b_enable_root_motion` 等"per-sequence"字段 —— 因为它们在 4.5 已 deprecated，避免误导。

## 修改文件清单

### 1. `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp`

文件顶部新增匿名命名空间，包含两个 helper：

```cpp
namespace
{
    FString RootMotionRootLockToString(ERootMotionRootLock::Type Lock)
    {
        switch (Lock)
        {
        case ERootMotionRootLock::RefPose:        return TEXT("RefPose");
        case ERootMotionRootLock::AnimFirstFrame: return TEXT("AnimFirstFrame");
        case ERootMotionRootLock::Zero:           return TEXT("Zero");
        default:                                   return TEXT("Unknown");
        }
    }

    void AddRootMotionFieldsForSequence(const TSharedPtr<FJsonObject>& Out, const UAnimSequence* AnimSeq)
    {
        Out->SetBoolField(TEXT("b_enable_root_motion"), AnimSeq->bEnableRootMotion);
        Out->SetStringField(TEXT("root_motion_root_lock"), RootMotionRootLockToString(AnimSeq->RootMotionRootLock.GetValue()));
        Out->SetBoolField(TEXT("b_force_root_lock"), AnimSeq->bForceRootLock);
        Out->SetBoolField(TEXT("b_use_normalized_root_motion_scale"), AnimSeq->bUseNormalizedRootMotionScale);
    }
}
```

**`HandleGetAnimationInfo`** 在 skeleton 字段后追加：

```cpp
Result->SetBoolField(TEXT("has_root_motion"), AnimBase->HasRootMotion());
if (const UAnimSequence* AnimSeq = Cast<UAnimSequence>(AnimBase))
{
    AddRootMotionFieldsForSequence(Result, AnimSeq);
}
```

**`HandleGetMontageCompositeInfo`** 顶层在 `enable_auto_blend_out` 后追加：

```cpp
Result->SetBoolField(TEXT("has_root_motion"), Montage->HasRootMotion());
```

并在 segment 循环 `loop_count` 后追加：

```cpp
if (const UAnimSequence* SegSeq = Cast<UAnimSequence>(SegAnim))
{
    SegEntry->SetBoolField(TEXT("has_root_motion"), SegSeq->HasRootMotion());
    AddRootMotionFieldsForSequence(SegEntry, SegSeq);
}
```

### 2. `C:\Workspace\git\unreal-mcp\Python\tools\animation_tools.py`

只更新 `get_animation_info` 与 `get_montage_composite_info` 的 docstring，把新字段写进 Returns 段。Python 侧不需要改逻辑 —— 工具本来就是 pass-through，UE 端返回什么字段就回什么字段。

## 验证步骤（重启后跑）

1. 关 UE → 重编 UnrealMCP 插件 → 启 UE
2. 重启 Claude Code 或在新会话里重载 MCP（让 Python 那边的 docstring 更新生效）
3. **`get_animation_info` 回归测试**：对一个普通 AnimSequence（例如 `MM_Attack_01`）调用，期望返回里出现 `has_root_motion` / `b_enable_root_motion` / `root_motion_root_lock` 等字段
4. **`get_animation_info` 兼容性测试**：对一个 AnimMontage（例如 `AM_ComboAttack`）调用，期望只看到 `has_root_motion`，没有 `b_enable_root_motion` 等 sequence-only 字段
5. **`get_montage_composite_info` 主验证**：对 `AM_ComboAttack` 调用，期望：
   - 顶层 `has_root_motion` = true
   - `slot_anim_tracks[0].segments[]` 里每段都有 `has_root_motion` / `b_enable_root_motion` 等字段
   - 三段 `MM_Attack_01..03` 至少有一段 `b_enable_root_motion = true`（这正好对得上"想让攻击中能动必须全关"的结论）

## 相关源码引用

- `Engine/Source/Runtime/Engine/Classes/Animation/AnimSequence.h:318-332` —— `bEnableRootMotion` / `RootMotionRootLock` / `bForceRootLock` / `bUseNormalizedRootMotionScale` 字段定义
- `Engine/Source/Runtime/Engine/Classes/Animation/AnimSequence.h:406` —— `HasRootMotion() const override { return bEnableRootMotion; }`
- `Engine/Source/Runtime/Engine/Classes/Animation/AnimMontage.h:696-702,856` —— Montage 上同名字段已 deprecated；`HasRootMotion()` 是 override
- `Engine/Source/Runtime/Engine/Private/Animation/AnimMontage.cpp:918-928` —— `UAnimMontage::HasRootMotion()` 的 OR 聚合实现
- `Engine/Source/Runtime/Engine/Classes/Animation/AnimEnums.h:9-24` —— `ERootMotionRootLock` 枚举三个值
- `Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp:2876-2899` —— `Velocity = ConstrainAnimRootMotionVelocity(...)` 覆盖玩家输入算出的 Velocity 的位置（这是"为什么任意一段开 RM 都会锁定移动"的根本原因，关 Q2 的解答见 `Docs/Variant_Combat/Variant_Combat_攻击系统解读.md`）
