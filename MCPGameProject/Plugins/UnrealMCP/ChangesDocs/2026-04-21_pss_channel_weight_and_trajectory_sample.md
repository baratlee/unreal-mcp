# PSS Channel Weight + Trajectory Sample 写工具（2026-04-21）

## 目标

扩展 MCP 支持修改 PoseSearchSchema 内部的 Channel Weight 和 Trajectory Channel 的 Samples 配置，解决 Quad ABP 中 Trajectory 权重过高（7 vs Dog 的 1）导致走直线闪烁 _L/_R 的问题。

## 新增工具

### 1. `set_pose_search_schema_channel_weight`

设置 PSS channel 顶层的 Weight UPROPERTY。适用于任何有 Weight 字段的 channel 类（Trajectory / Position / Velocity / Heading 等）。

**参数**：
- `asset_path`: PSS object path
- `channel_index`: Channels 数组索引（Trajectory 一般在 0）
- `weight`: 新权重值

### 2. `set_pose_search_schema_trajectory_sample`

修改 Trajectory channel 的 Samples[sample_index] 配置。每个字段可选。

**参数**：
- `asset_path`, `channel_index`, `sample_index`
- `offset`（float）: 时间偏移（秒，负 = 过去，正 = 未来）
- `weight`（float）: Sample 权重
- `flags`（int）: EPoseSearchTrajectoryFlags bitmask（Velocity=1, Position=2, ...）

## 改动文件

### C++ 插件
- `Plugins/UnrealMCP/Source/UnrealMCP/Public/Commands/UnrealMCPAnimationCommands.h`
  - +2 handler 声明
- `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp`
  - +2 dispatch 分支
  - +2 handler 实现（约 150 行）
- `Plugins/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp`
  - +2 命令字符串路由

### Python 侧
- `C:\Workspace\git\unreal-mcp\Python\tools\animation_tools.py`
  - +2 `@mcp.tool()` 函数

## 实现要点

### Channel Weight 反射路径
- PSS 的 `Channels` 是 `TArray<TObjectPtr<UPoseSearchFeatureChannel>>`（Instanced UObject）
- FArrayProperty → FObjectProperty::GetObjectPropertyValue 拿到 channel UObject
- `ChannelObject->GetClass()->FindPropertyByName("Weight")` 反射 Weight 字段
- 兼容任何子类（Trajectory / Position / Velocity 等都有 Weight UPROPERTY）

### Trajectory Sample 反射路径
- `ChannelObject` 的 `Samples` 是 `TArray<FPoseSearchTrajectorySample>`
- FArrayProperty → FStructProperty::Struct 拿 struct 定义
- `FindPropertyByName("Offset" / "Weight" / "Flags")` 改字段
- Weight 在 WITH_EDITORONLY_DATA guard 下（插件是 editor target，始终生效）

## 测试计划

编译重启后：

1. **读 PSS 当前 Trajectory weight**（get_pose_search_schema_info）
2. **降低顶层 Weight**
   ```
   set_pose_search_schema_channel_weight(
       "/Game/Leo/.../PSS_Quad_Default.PSS_Quad_Default",
       channel_index=0,
       weight=1.0
   )
   ```
3. **调整 Samples**（按 Dog 结构）
   - offsets: `-0.05, 0, 0.1, 0.3, 0.5`
   - weights: `0.4, 1.0, 1.0, 1.0, 1.5`
   - flags: `32, 144, 160, 176, 4`
4. 读回验证

## 生效步骤（用户执行）

1. 关 UE 编辑器
2. 重编 UnrealMCP 插件
3. 启 UE
4. `/exit` 退出 Claude Code → 重启 `claude`
5. 继续 Quad 调试（调 PSS_Quad_Default 权重）

## 工具总数变化

66 工具 → 68 工具（+2）。
