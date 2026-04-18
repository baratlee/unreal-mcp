# 2026-04-18: PSD/PSS/ChooserTable 读写工具

## 新增工具（10 个）

### 只读工具（2 个）
| Python 工具 | 作用 |
|---|---|
| `get_pose_search_database_info` | 读 PSD：Schema、动画列表、Tags、成本偏置、搜索模式 |
| `get_pose_search_schema_info` | 读 PSS：采样率、骨骼、通道层级递归序列化 |

### PSD 写入工具（3 个）
| Python 工具 | 作用 |
|---|---|
| `set_pose_search_database_schema` | 设置 PSD 的 Schema 引用 |
| `add_pose_search_database_animation` | 添加动画条目（路径 + enabled + sampling range） |
| `remove_pose_search_database_animation` | 按 index 移除动画条目 |

### PSS 写入工具（3 个）
| Python 工具 | 作用 |
|---|---|
| `set_pose_search_schema_settings` | 设置采样率、骨骼引用 |
| `add_pose_search_schema_channel` | 添加通道（Trajectory/Pose/Position/Velocity/Heading/Curve） |
| `remove_pose_search_schema_channel` | 按 index 移除通道 |

### ChooserTable 写入工具（2 个）
| Python 工具 | 作用 |
|---|---|
| `add_chooser_table_row` | 添加行（asset result + 列值，支持 Enum/Bool 列） |
| `remove_chooser_table_row` | 按 index 移除行（同步移除所有列的行值） |

## 改动文件清单

### 新增模块依赖
- `UnrealMCP.Build.cs` — `"PoseSearch"` 依赖

### C++ 改动
1. **`UnrealMCPAnimationCommands.h`** — +10 handler 声明
2. **`UnrealMCPAnimationCommands.cpp`**:
   - +8 include（PoseSearch 通道子类 + Chooser 列类型）
   - +递归序列化 helper（SerializeChannelRecursive）
   - +10 分发分支
   - +10 handler 实现
3. **`UnrealMCPBridge.cpp`** — +10 命令字符串

### Python 改动
4. **`animation_tools.py`** — +10 个 `@mcp.tool()` 函数

## 技术要点

### PSD 写入
- `DatabaseAnimationAssets` 是 private，通过反射 `FScriptArrayHelper` 操作
- `Schema` 字段是 `TObjectPtr<const UPoseSearchSchema>`，通过 `FObjectProperty::SetObjectPropertyValue_InContainer` 设置

### PSS 写入
- 通道通过 `NewObject<Channel>(Schema)` 创建，Schema 作为 Outer
- 支持 6 种通道类型：Trajectory（含 Samples 数组）、Pose（含 SampledBones 数组）、Position、Velocity、Heading、Curve
- `Channels` 数组通过反射写入

### ChooserTable 写入
- Result 通过 `FInstancedStruct::InitializeAs<FAssetChooser>()` 创建
- 每添加/删除一行，同步操作所有列的 RowValues 数组
- 支持 FEnumColumn（通过枚举名查找值）和 FBoolColumn
- 未知列类型通过反射 `RowValuesPropertyName()` 添加默认值
- 所有写入操作调用 `Modify()` + `MarkPackageDirty()`

## 端到端验证 ✅ (2026-04-18)

### PSS 写入
- `set_pose_search_schema_settings`：PSS_Test 设 sample_rate=30 + GermanShepherd_Skeleton → 读回确认 ✅
- `add_pose_search_schema_channel`：加 Trajectory 通道（3 samples）→ new_index=2 ✅；加 Pose 通道（weight=0.8, RigLFLegAnkle+RigRFLegAnkle）→ new_index=3 ✅；读回 channel_count=4 结构完整
- `remove_pose_search_schema_channel`：删 index 2 → channel_count 降到 3 ✅
- 注意：空 PSS 创建时自带 2 个默认通道（Trajectory+Pose），所以新增通道 index 从 2 开始

### PSD 写入
- `set_pose_search_database_schema`：PSD_Test 设 schema→PSS_Test ✅
- `add_pose_search_database_animation`：加 Stand_01→index 0 ✅；加 Loco_Walk→index 1 ✅；读回 animation_asset_count=2 ✅
- `remove_pose_search_database_animation`：删 index 0 → animation_asset_count=1，仅剩 Loco_Walk ✅

### ChooserTable 写入
- `add_chooser_table_row`：CHT_PoseSearchQuad 原 4 行，加 PSD_Test（column_values=["Idle"]）→ new_row_index=4 ✅；读回 row_count=5 ✅
- `remove_chooser_table_row`：删 index 4 → row_count 恢复 4，原始数据不变 ✅
