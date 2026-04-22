# PSD CostBiases + Animation Flags 写工具（2026-04-21）

## 目标

给 UnrealMCP 加两个工具，支持修改 PoseSearchDatabase 里现有的 cost bias 字段和动画条目的 flags。

原 PSD 写工具只能：
- `set_pose_search_database_schema` — 改 Schema 引用
- `add_pose_search_database_animation` — 添加动画条目（只能设 enabled / sampling_range）
- `remove_pose_search_database_animation` — 删除动画条目

**缺少**：修改 PSD 级 cost bias，修改已有动画条目的 DisableReselection / MirrorOption。

## 新增工具

### 1. `set_pose_search_database_cost_biases`

修改 PSD 的 cost bias 字段（PSD 本身的 UPROPERTY）：
- `ContinuingPoseCostBias`（float）
- `BaseCostBias`（float）
- `LoopingCostBias`（float）

每个参数可选，只修改提供的字段。

### 2. `set_pose_search_database_animation_flags`

修改 PSD 里指定 index 的动画条目的 flags：
- `bEnabled`（bool）
- `bDisableReselection`（bool）
- `MirrorOption`（enum 字符串：UnmirroredOnly / MirroredOnly / UnmirroredAndMirrored）
- `SamplingRange.Min / Max`（float）

每个参数可选，只修改提供的字段。

## 改动文件

### C++ 插件（LeoPractice 工程内）
- `Plugins/UnrealMCP/Source/UnrealMCP/Public/Commands/UnrealMCPAnimationCommands.h`
  - 新增 2 个 handler 声明
- `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp`
  - `HandleCommand` 新增 2 个 `if` 分发分支
  - 文件末尾新增 2 个 handler 实现（HandleSetPoseSearchDatabaseCostBiases 约 55 行、HandleSetPoseSearchDatabaseAnimationFlags 约 95 行）
- `Plugins/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp`
  - 命令字符串路由新增 2 条

### Python 侧（外部仓 unreal-mcp）
- `C:\Workspace\git\unreal-mcp\Python\tools\animation_tools.py`
  - 新增 2 个 `@mcp.tool()` 函数

## 实现要点

### CostBiases 用 `FFloatProperty` 反射
所有 3 个 cost bias 都是 `float`，通过 `Database->GetClass()->FindPropertyByName` + `FFloatProperty::SetPropertyValue_InContainer` 写入。

### AnimationFlags 走 `FScriptArrayHelper`
- 用 `DatabaseAnimationAssets` 的 `FArrayProperty` 拿 ArrayHelper
- 按 index 拿 element 指针（`GetRawPtr`）
- `FStructProperty::Struct` 拿 inner struct（FPoseSearchDatabaseAnimationAssetBase 派生类）
- 反射查找 `bEnabled / bDisableReselection / MirrorOption / SamplingRange`

### MirrorOption 枚举字符串容错
- 先尝试 `GetValueByNameString("UnmirroredOnly")`
- 失败则尝试 `GetValueByNameString("EPoseSearchMirrorOption::UnmirroredOnly")`
- 两种写法都能接受

### WITH_EDITORONLY_DATA guard
- 动画条目的 `bEnabled / bDisableReselection / MirrorOption / SamplingRange` 都是 editor-only 字段
- Guard 保留贴合 UE 编码规范

## 测试计划

编译重启后：

1. **CostBiases 改写**
   ```
   set_pose_search_database_cost_biases(
       "/Game/Leo/Blueprints/Character/Quad/PSD_Quad_Walk.PSD_Quad_Walk",
       continuing_pose_cost_bias=-0.1,
       looping_cost_bias=-0.005
   )
   ```
   用 `get_pose_search_database_info` 读回验证

2. **AnimationFlags 批量改**
   对每个 PSD 的每条动画循环调：
   ```
   set_pose_search_database_animation_flags(
       "/Game/.../PSD_Quad_Walk.PSD_Quad_Walk",
       index=0,
       disable_reselection=True
   )
   ```

3. **错误路径**
   - 无效 asset_path → `PoseSearchDatabase not found`
   - index 越界 → `Index N out of range (0..M-1)`
   - 无效 mirror_option 字符串 → 不触发修改（ModifiedFields 里没有 MirrorOption）

## 生效步骤（用户执行）

1. 关 UE 编辑器
2. 重编 UnrealMCP 插件
3. 启 UE
4. `/exit` 退出 Claude Code
5. 重新 `claude` 启动
6. 继续 Quad 调试任务（改 13 个 PSD 的参数）

## 工具总数变化

原 64 工具 → 66 工具（+2）。
