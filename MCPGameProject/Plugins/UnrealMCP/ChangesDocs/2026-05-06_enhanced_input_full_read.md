# Enhanced Input 完全读取增强 (2026-05-06)

**日期**：2026-05-06
**涉及仓库**：`C:\Workspace\git\unreal-mcp`（C++ 插件 + Python MCP server）

## 概述

将 `get_input_mapping_context_info` 和 `get_input_action_info` 从"基础读取"升级为"完全读取"，覆盖 UE5.7 Enhanced Input 系统的全部可序列化数据。此前这两个工具仅返回核心字段（Action/Key/少量 Trigger-Modifier），现在返回 IMC 级元数据、Profile Overrides、玩家可映射设置、内联 Action 详情，以及所有 Trigger/Modifier 子类的完整属性。

无新增工具，无 API 破坏性变更——所有原有字段保持不变，仅追加新字段。

## 变更明细

### 1. Trigger 序列化增强 — `SerializeTrigger()`

| 变更 | 说明 |
|---|---|
| 基类新字段 | `should_always_tick`（bool） |
| TimedBase 新字段 | `affected_by_time_dilation`（bool）—— Hold / HoldAndRelease / Tap / Pulse / RepeatedTap 均输出 |
| 新增 `UInputTriggerRepeatedTap` | `repeat_delay`、`number_of_taps_which_trigger_repeat`、`tap_release_time_threshold`（protected 字段，通过 UE 反射读取） |
| 新增 `UInputTriggerCombo` | `combo_actions[]`（每步含 action/action_name/completion_states/time_to_press_key）、`cancel_actions[]`（每条含 action/action_name/cancellation_states） |
| `UInputTriggerChordBlocker` | 由 ChordAction 分支自动覆盖（Cast 继承链），class 字段可区分 |

### 2. Modifier 序列化增强 — `SerializeModifier()`

| 变更 | 说明 |
|---|---|
| `UInputModifierFOVScaling` | 新增 `fov_scaling_type`（Standard / UE4_BackCompat） |
| 新增 `UInputModifierSmoothDelta` | `smoothing_method`（枚举名字符串，通过 StaticEnum 反射）、`speed`、`easing_exponent` |
| 新增 `UInputModifierResponseCurveUser` | `response_x` / `response_y` / `response_z`（UCurveFloat 资产路径） |
| 无额外属性的子类 | `ScaleByDeltaTime` / `Smooth` / `ToWorldSpace` —— class 字段已自动序列化 |

### 3. IMC 读取增强 — `HandleGetInputMappingContextInfo()`

#### IMC 级别新字段

| 字段 | 类型 | 来源 |
|---|---|---|
| `registration_tracking_mode` | string (Untracked / CountRegistrations) | `GetRegistrationTrackingMode()` |
| `should_filter_by_input_mode` | bool | `ShouldFilterMappingByInputMode()` |
| `input_mode_query` | string（仅当过滤激活时出现） | `GetInputModeQuery().GetDescription()` |
| `profile_ids` | string[]（仅当存在 Override 时出现） | `GetProfilesWithOverridenMappings()` |
| `profile_overrides` | object{profile_id → mappings[]}（同上） | `GetMappingsForProfile()` |

#### 每条 Mapping 新字段

| 字段 | 说明 |
|---|---|
| `action_description` | IA 上的 ActionDescription |
| `action_consume_input` | IA.bConsumeInput |
| `action_consumes_action_and_axis_mappings` | IA.bConsumesActionAndAxisMappings |
| `action_trigger_when_paused` | IA.bTriggerWhenPaused |
| `action_reserve_all_mappings` | IA.bReserveAllMappings |
| `action_accumulation_behavior` | TakeHighestAbsoluteValue / Cumulative |
| `is_player_mappable` | `FEnhancedActionKeyMapping::IsPlayerMappable()` |
| `mapping_name` | 仅当 is_player_mappable 时出现 |
| `display_name` | 同上 |
| `display_category` | 同上 |

#### 架构优化

原先 mapping 序列化是内联在 handler 中的循环，现提取为匿名命名空间中的 `SerializeMappingArray()` 辅助函数，同时供默认映射和 Profile Override 映射复用。

### 4. IA 读取增强 — `HandleGetInputActionInfo()`

| 新字段 | 类型 | 说明 |
|---|---|---|
| `consumes_action_and_axis_mappings` | bool | 是否消耗 Legacy Input 映射 |
| `accumulation_behavior` | string | TakeHighestAbsoluteValue / Cumulative |
| `trigger_events_that_consume_legacy_keys` | int (bitmask) | 触发哪些事件时消耗 Legacy Key |

## 文件变更

### 修改

| 文件 | 变更 |
|---|---|
| `Private/Commands/UnrealMCPAnimationCommands.cpp` | +2 includes (`PlayerMappableKeySettings.h`, `Curves/CurveFloat.h`)；SerializeTrigger 增强（+Combo/RepeatedTap/基类字段）；SerializeModifier 增强（+SmoothDelta/ResponseCurveUser/FOVScalingType）；+2 辅助函数（`AccumulationBehaviorToString`、`SerializeMappingArray`）；HandleGetInputActionInfo +3 字段；HandleGetInputMappingContextInfo 重写（IMC 元数据 + Profile Overrides + 内联 Action 详情 + PlayerMappable 设置） |
| `Python/tools/input_tools.py` | `get_input_action_info` 和 `get_input_mapping_context_info` 文档字符串更新以反映全部新返回字段 |

### 未变更

- `UnrealMCPAnimationCommands.h` — 无新 handler，签名不变
- `UnrealMCPBridge.cpp` — 命令路由不变（`get_input_action_info` / `get_input_mapping_context_info` 路由已存在）
- `UnrealMCP.Build.cs` — EnhancedInput 依赖已在 2026-04-16 添加
- `unreal_mcp_server.py` — input_tools 注册已存在

## 技术说明

- `UInputTriggerRepeatedTap` 的 `RepeatDelay` / `NumberOfTapsWhichTriggerRepeat` / `TapReleaseTimeThreshold` 三个字段为 protected，通过 `FindPropertyByName` + `CastField<FDoubleProperty/FIntProperty/FFloatProperty>` 反射读取。
- `UInputModifierSmoothDelta::SmoothingMethod` 枚举值通过 `StaticEnum<ENormalizeInputSmoothingType>()->GetNameStringByValue()` 转为字符串，避免硬编码 17 个枚举项。
- Profile Overrides 使用 UE5.7 新增的 `GetProfilesWithOverridenMappings()` + `GetMappingsForProfile()` 公开 API，不访问 protected 成员。
