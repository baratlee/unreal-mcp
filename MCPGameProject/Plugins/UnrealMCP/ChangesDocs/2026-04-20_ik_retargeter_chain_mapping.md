# unreal-mcp 扩展：get_ik_retargeter_info 增加 Chain Mapping 详情

**日期**：2026-04-20
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 修改目的

`get_ik_retargeter_info` 的 `retarget_ops` 数组之前只输出每个 Op 的 `struct_name`（如 `IKRetargetFKChainsOp`），无法看到内部的 chain mapping（哪个源链映射到哪个目标链）和 per-chain settings（FK 旋转模式、IK blend 值等）。

## 修改内容

### Op 序列化扩展

每个 Op entry 现在额外输出：

| 字段 | 类型 | 说明 |
|---|---|---|
| `enabled` | bool | Op 是否启用 |
| `chain_pair_count` | int | chain mapping 对数（仅有 mapping 的 Op 输出） |
| `chain_pairs` | array | `{ source_chain, target_chain }` 对数组 |
| `settings` | object | Op 的 per-chain settings（通过 `UStructToJsonObject` 序列化，含所有 EditAnywhere 属性） |

### 支持的 Op 类型

| Op Struct | chain_pairs | settings 关键字段 |
|---|---|---|
| `IKRetargetFKChainsOp` | ✅ | ChainsToRetarget: RotationMode/TranslationMode/Alpha |
| `IKRetargetIKChainsOp` | — | ChainsToRetarget: EnableIK/BlendToSource/StaticOffset/Extension |
| `IKRetargetRunIKRigOp` | ✅ | IKRigAsset/ExcludedGoals |
| `IKRetargetAlignPoleVectorOp` | ✅ | ChainsToAlign: AlignAlpha/StaticAngularOffset |
| `IKRetargetStretchChainOp` | ✅ | ChainsToStretch: MatchSourceLength/ScaleChainLength |
| `IKRetargetPelvisMotionOp` | — | PelvisSettings（各轴权重） |

### 返回示例（FK Chains Op）

```json
{
  "index": 0,
  "struct_name": "IKRetargetFKChainsOp",
  "enabled": true,
  "chain_pair_count": 5,
  "chain_pairs": [
    {"source_chain": "Spine", "target_chain": "Spine"},
    {"source_chain": "LeftArm", "target_chain": "LeftArm"},
    {"source_chain": "RightArm", "target_chain": "RightArm"},
    {"source_chain": "LeftLeg", "target_chain": "LeftLeg"},
    {"source_chain": "RightLeg", "target_chain": "RightLeg"}
  ],
  "settings": {
    "ChainsToRetarget": [
      {"TargetChainName": "Spine", "EnableFK": true, "RotationMode": "Interpolated", "RotationAlpha": 1.0, ...}
    ]
  }
}
```

## 修改的文件

### C++ 端

| 文件 | 改动 |
|---|---|
| `Private/Commands/UnrealMCPAnimationCommands.cpp` | +2 include（IKRetargetOps.h / IKRetargetChainMapping.h）；HandleGetIKRetargeterInfo 的 Op 循环扩展：读 GetChainMapping() chain pairs + GetSettings() + UStructToJsonObject |

### Python 端

| 文件 | 改动 |
|---|---|
| `Python/tools/animation_tools.py` | 更新 `get_ik_retargeter_info` docstring |

### 文档

| 文件 | 改动 |
|---|---|
| `ChangesDocs/ToolList.md` | 更新 `get_ik_retargeter_info` 描述 |
