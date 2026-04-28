# unreal-mcp 扩展：IMC Modifier / Trigger 管理工具（4 条）

**日期**：2026-04-28
**涉及仓库**：
- `C:\Workspace\git\unreal-mcp`（C++ 插件 + Python MCP server）

## 概述

补全 IMC（InputMappingContext）对 Modifier 和 Trigger 的细粒度管理能力。此前只能通过 `add_imc_mapping_modifier` / `add_imc_mapping_trigger` 追加，若要改属性或删除单条则必须整条 mapping remove 再重建。现在支持按索引直接删除或就地修改已有 Modifier / Trigger。

## 新增工具

| 工具 | 必选参数 | 说明 |
|---|---|---|
| `remove_imc_mapping_modifier` | asset_path, mapping_index, modifier_index | 按索引删除指定 mapping 上的单个 Modifier |
| `set_imc_mapping_modifier` | asset_path, mapping_index, modifier_index + 属性参数 | 就地修改已有 Modifier 的属性（自动识别类型，只改传入的字段） |
| `remove_imc_mapping_trigger` | asset_path, mapping_index, trigger_index | 按索引删除指定 mapping 上的单个 Trigger |
| `set_imc_mapping_trigger` | asset_path, mapping_index, trigger_index + 属性参数 | 就地修改已有 Trigger 的属性（自动识别类型，只改传入的字段） |

### set 命令支持的属性

与 `add_imc_mapping_modifier` / `add_imc_mapping_trigger` 完全一致：

- **Modifier**: DeadZone（lower_threshold / upper_threshold / dead_zone_type）、Negate（negate_x/y/z）、SwizzleAxis（order）、Scalar（scalar_x/y/z）、FOVScaling（fov_scale）、ResponseCurveExponential（curve_exponent_x/y/z）
- **Trigger**: actuation_threshold（通用）、hold_time_threshold / is_one_shot（Hold）、hold_time_threshold（HoldAndRelease）、tap_release_time_threshold（Tap）、trigger_on_start / interval / trigger_limit（Pulse）、chord_action_path（ChordAction）

## 实现要点

- **辅助函数**：新增 `ApplyModifierProperties` / `ApplyTriggerProperties`，通过 `Cast<>` 识别已有对象的具体子类并就地赋值。与 `CreateModifierFromJson` / `CreateTriggerFromJson`（创建新对象）分工明确。
- **索引定位**：remove 和 set 均通过 mapping_index + modifier_index / trigger_index 双层索引定位，索引值来自 `get_input_mapping_context_info` 返回的数组顺序。
- **TObjectPtr 兼容**：UE5.7 的 `FEnhancedActionKeyMapping::Modifiers` / `Triggers` 数组类型为 `TArray<TObjectPtr<...>>`，remove handler 使用 `auto&` 推导避免与裸指针 TArray 的类型不匹配。

## 文件变更

### 修改

| 文件 | 变更 |
|---|---|
| `Public/Commands/UnrealMCPAnimationCommands.h` | +4 个 handler 声明 |
| `Private/Commands/UnrealMCPAnimationCommands.cpp` | +2 个 helper（ApplyModifierProperties / ApplyTriggerProperties）+ 4 个 handler 实现 + HandleCommand 路由 +4 条 |
| `Private/UnrealMCPBridge.cpp` | Animation 命令路由块 +4 条命令字符串 |
| `Python/tools/input_tools.py` | +4 个 @mcp.tool() 函数 |

## 端到端测试结果

在 UE 5.7 Editor 中对 GASP `IMC_Sandbox` 验证通过（测后已还原）：

1. `set_imc_mapping_modifier` — mapping 4 (Gamepad_Left2D) DeadZone lower_threshold 0.3 → 0.25 → 还原 0.3
2. `remove_imc_mapping_modifier` — mapping 1 (S) 删除 Negate，remaining=1 → add 加回还原
3. `set_imc_mapping_trigger` — mapping 15 (SpaceBar/Jump) Hold hold_time_threshold 0.5 → 1.0
4. `remove_imc_mapping_trigger` — mapping 15 删除 Hold，remaining=0（测试 trigger 全部清理完毕）
