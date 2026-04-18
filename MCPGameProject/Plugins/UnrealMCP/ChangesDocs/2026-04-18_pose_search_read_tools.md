# 2026-04-18: Pose Search 只读工具（PSD + PSS）

## 新增工具

| Python 工具 | 对应命令 | 作用 |
|---|---|---|
| `get_pose_search_database_info` | `get_pose_search_database_info` | 读取 `UPoseSearchDatabase` 结构：Schema 引用、动画资产列表（路径/类/启用/镜像/采样范围）、Tags、成本偏置、搜索模式 |
| `get_pose_search_schema_info` | `get_pose_search_schema_info` | 读取 `UPoseSearchSchema` 结构：采样率、骨骼引用、数据预处理、完整通道层级（递归序列化子通道） |

## 改动文件清单

### 新增模块依赖
- `UnrealMCP.Build.cs` — Editor 分支追加 `"PoseSearch"` 依赖

### C++ 改动（3 个文件）
1. **`Public/Commands/UnrealMCPAnimationCommands.h`** — 追加 2 个 handler 声明
2. **`Private/Commands/UnrealMCPAnimationCommands.cpp`**:
   - include 追加 3 个：`PoseSearch/PoseSearchDatabase.h`、`PoseSearch/PoseSearchSchema.h`、`PoseSearch/PoseSearchFeatureChannel.h`
   - 匿名命名空间新增 `SerializeChannelRecursive()` 递归序列化通道
   - `HandleCommand` 追加 2 个 `if` 分发分支
   - 文件末尾实现 2 个 handler
3. **`Private/UnrealMCPBridge.cpp`** — Animation 分支 if 条件追加 2 个命令字符串

### Python 改动（1 个文件）
4. **`Python/tools/animation_tools.py`** — `register_animation_tools` 内追加 2 个 `@mcp.tool()` 函数

## 技术要点

### Schema 字段访问
- `Skeletons` 是 private，通过 public getter `GetRoledSkeletons()` 访问
- `Channels` 是 private，通过 UE 反射 `FindPropertyByName("Channels")` 访问原始用户配置数组
- 不使用 `GetChannels()`（返回 FinalizedChannels，运行时填充，编辑器下可能为空）

### 通道递归序列化
- `SerializeChannelRecursive()` 处理所有通道子类（Trajectory/Pose/Position/Velocity/Curve/Group 等）
- 通过 `GetClass()->GetName()` 输出通道类名
- 通过 UE 反射读取各子类特有字段（Weight/Bone/SampleRole/SampleTimeOffset 等）
- Trajectory 通道：序列化 Samples 数组（offset/flags/weight）
- Pose 通道：序列化 SampledBones 数组（bone_name/flags/weight）
- Group 通道：递归 `GetSubChannels()` 序列化子通道

### Database 动画资产访问
- `DatabaseAnimationAssets` 是 private UPROPERTY，通过反射访问
- 每个条目序列化：AnimAsset 路径/类、bEnabled、MirrorOption、SamplingRange、bUseSingleSample

### PoseSearch 模块
- 模块名：`PoseSearch`
- 头文件路径：`PoseSearch/PoseSearchDatabase.h` 等（Runtime/Public 下）
- PoseSearch 插件位于 `Engine/Plugins/Animation/PoseSearch/`

## 端到端验证 ✅ (2026-04-18)

- **PSD_QuadIdle**：animation_asset_count=2、schema→PSS_QuadDefault、continuing_pose_cost_bias=-0.05、pose_search_mode="PCAKDTree"、每条 asset 有 anim_asset_path/anim_asset_class/enabled/mirror_option/sampling_range ✅
- **PSD_QuadWalk**：animation_asset_count=3、字段完整 ✅
- **PSS_QuadDefault**：sample_rate=30、schema_cardinality=27、data_preprocessor="NormalizeWithCommonSchema"、skeleton_count=1→GermanShepherd_Skeleton、channel_count=2（Trajectory 含 5 samples + 9 sub_channels、Group 含 3 sub_channels 引用 RigLFLegAnkle/RigRFLegAnkle/RigPelvis）、递归序列化完整 ✅
- **错误路径**：ChooserTable 喂 PSD → "PoseSearchDatabase asset not found" ✅；AnimSequence 喂 PSS → "PoseSearchSchema asset not found" ✅
