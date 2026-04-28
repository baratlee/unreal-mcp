# unreal-mcp 扩展：Enhanced Input 写入工具（7 条）

**日期**：2026-04-28
**涉及仓库**：
- `C:\Workspace\git\unreal-mcp`（C++ 插件 + Python MCP server）

## 概述

补全 Enhanced Input 的 MCP 写入能力。此前仅有 `get_input_action_info` 和 `get_input_mapping_context_info` 两个只读工具；现在可以从零创建 IA / IMC 资产、添加 key mapping、逐条叠加 Modifier 和 Trigger 及其子属性，实现与 Editor 手工编辑完全等价的资产输出。

旧版 `create_input_mapping`（走 `UInputSettings::AddActionMapping` 的 Legacy Input System）保持不变但不推荐使用。

## 新增工具

| 工具 | 说明 |
|---|---|
| `create_input_action` | 创建 UInputAction 资产（指定 ValueType、description、consume_input 等） |
| `set_input_action_property` | 修改已有 IA 的属性（value_type / description / consume_input / trigger_when_paused / reserve_all_mappings） |
| `create_input_mapping_context` | 创建 UInputMappingContext 资产（可选 description） |
| `add_imc_mapping` | 向 IMC 添加一条 Action + Key 映射，返回 mapping_index |
| `remove_imc_mapping` | 按 mapping_index 精确删除，或按 action_path + key 删除 |
| `add_imc_mapping_modifier` | 给指定 mapping 追加 Modifier（支持 DeadZone / Negate / SwizzleAxis / Scalar / FOVScaling / ResponseCurveExponential，含全部子属性） |
| `add_imc_mapping_trigger` | 给指定 mapping 追加 Trigger（支持 Down / Pressed / Released / Hold / HoldAndRelease / Tap / Pulse / ChordAction，含全部子属性） |

## 实现要点

- **资产创建**：沿用 `CreatePackage` + `NewObject` + `FAssetRegistryModule::AssetCreated` + `MarkPackageDirty` 模式（同 IKRig/IKRetargeter）。
- **Mapping 添加**：调用 `UInputMappingContext::MapKey()` 公开 API，返回新 mapping 的数组索引。
- **Mapping 修改**：`GetMappings()` 返回 const 引用，通过 `const_cast` 获取可写引用（Editor 工具标准做法），直接操作 `Modifiers` / `Triggers` 数组。
- **Modifier / Trigger 创建**：`NewObject<UInputModifierXxx>(IMC)` / `NewObject<UInputTriggerXxx>(IMC)`，Outer 设为 IMC 保证序列化归属正确。
- **Helper 函数**：`CreateModifierFromJson` / `CreateTriggerFromJson` 封装 class 名→UObject 分派和子属性赋值，与现有 `SerializeModifier` / `SerializeTrigger` 读序列化对称。

## 文件变更

### 修改

| 文件 | 变更 |
|---|---|
| `Public/Commands/UnrealMCPAnimationCommands.h` | +7 个 handler 声明 |
| `Private/Commands/UnrealMCPAnimationCommands.cpp` | +3 个 helper（StringToInputActionValueType / CreateModifierFromJson / CreateTriggerFromJson）+ 7 个 handler 实现（约 400 行），HandleCommand 路由 +7 条 |
| `Private/UnrealMCPBridge.cpp` | Animation 命令路由块 +7 条命令字符串 |
| `Python/tools/input_tools.py` | 重写：提取 `_send` 公共函数，2 个 reader + 7 个 writer 共 9 个工具 |

### 未变更

- `UnrealMCP.Build.cs` — EnhancedInput 模块依赖已在 2026-04-16 reader 阶段添加
- `Python/unreal_mcp_server.py` — `register_input_tools` 已在 reader 阶段注册

## 端到端测试结果

在 UE 5.7 Editor 中全流程验证通过：

1. `create_input_action` — 创建 IA_TestMCP_Move (Axis2D) + IA_TestMCP_Jump (Boolean)
2. `create_input_mapping_context` — 创建 IMC_TestMCP
3. `add_imc_mapping` — 添加 W / S / Gamepad_Left2D / SpaceBar 四条映射
4. `add_imc_mapping_modifier` — W→SwizzleAxis(YXZ)、S→SwizzleAxis(YXZ)+Negate(xyz)、Gamepad→DeadZone(0.3/1/Radial)
5. `add_imc_mapping_trigger` — SpaceBar→Pressed(threshold=0.5)
6. `set_input_action_property` — 修改 description + consume_input
7. `remove_imc_mapping` — 按 index 删除后 count 4→3
8. `get_input_mapping_context_info` 读回所有数据，与 GASP `IMC_Sandbox` 同类型映射结构完全一致
