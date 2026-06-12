# IMC Chord Blocker Trigger Support (2026-06-12)

## What

`add_imc_mapping_trigger` 现在支持 `trigger_class="ChordBlocker"`。
对应 UE 引擎类 `UInputTriggerChordBlocker`（继承自 `UInputTriggerChordAction`），
用途是「按住 chord IA 时阻止本 mapping 触发」——`Q` 单按瞄准，`Alt+Q` 不瞄准的典型场景。

字段与 `ChordAction` 完全相同：
- `actuation_threshold: float`
- `chord_action_path: str`（chord IA 资产路径）

## Why

之前只能用 `ChordAction`（chord 满足才触发）。互斥需求（chord 满足时屏蔽）
原本需要进 Editor 手动加 trigger，MCP 流水线断点。

## Changed Files

### C++ (Plugins/UnrealMCP/)
- `Private/Commands/UnrealMCPAnimationCommands.cpp`
  - `CreateTriggerByName`: 新增 `ChordBlocker` 分支（NewObject `UInputTriggerChordBlocker`）
  - `HandleAddIMCMappingTrigger` 错误信息列表追加 `ChordBlocker`

> 读路径（`SerializeTrigger`）和写路径（`ApplyTriggerProperties`）无需修改：
> `UInputTriggerChordBlocker` 继承 `UInputTriggerChordAction`，`Cast<UInputTriggerChordAction>`
> 已覆盖 ChordBlocker 实例；`class` 字段用 `GetClass()->GetName()` 上报，自动区分。

### Python (unreal-mcp/)
- `Python/tools/input_tools.py`
  - `add_imc_mapping_trigger` docstring: trigger_class 列表追加 `ChordBlocker`，
    新增 ChordBlocker params 段
  - `set_imc_mapping_trigger` docstring: ChordAction 段改为 `ChordAction / ChordBlocker`

## Verify

```
# Q→Aim mapping 加 Alt chord blocker，按下 Q 单触发 Aim；按下 Alt+Q 不触发
add_imc_mapping_trigger(
    asset_path="/Game/Kunlun/Input/IMC_KLGameplay",
    mapping_index=47,
    trigger_class="ChordBlocker",
    chord_action_path="/Game/Kunlun/Input/Debug/IA_Debug_AltModifier.IA_Debug_AltModifier",
)
```

随后 `get_input_mapping_context_info` 应在该 mapping 上看到
`"class": "InputTriggerChordBlocker"` + `"chord_action": ".../IA_Debug_AltModifier..."`
