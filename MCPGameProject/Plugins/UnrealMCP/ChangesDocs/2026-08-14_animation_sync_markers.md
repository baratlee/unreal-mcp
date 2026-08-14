# Animation Sync Marker 查询

## 目的

动画状态机调试需要区分资源没有配置 Sync Marker、Marker 时间不正确，以及运行时 Sync Group 没有形成有效同步位置。为避免依赖动画编辑器界面或输出完整资产信息，新增只读接口，直接返回 `AnimSequence` 中的 Sync Marker 数据。

## 接口

`get_animation_sync_markers(asset_path)`

- `asset_path`：必填，`AnimSequence` 资产路径。
- 接口只读取资产，不修改动画或 Blueprint。
- 非 `AnimSequence` 路径或资产不存在时返回错误。

返回内容：

- `asset_path`：解析后的完整对象路径。
- `play_length`：动画时长，单位为秒。
- `marker_count`：Marker 数量。
- `marker_names`：去重并排序后的 Marker 名称列表。
- `markers`：按时间升序排列的 Marker 列表。
  - `name`：Marker 名称。
  - `time`：Marker 在动画中的时间，单位为秒。
  - `normalized_time`：`time / play_length`；动画时长为零时返回 `0`。
  - `track_index`：Marker 所属 Notify Track 索引。
  - `track_name`：对应 Notify Track 名称；索引无效时为空字符串。

## 修改范围

- `Source/UnrealMCP/Public/Commands/UnrealMCPAnimationCommands.h`：声明命令处理函数。
- `Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp`：加载 `AnimSequence`，读取、排序并序列化 Sync Marker。
- `Source/UnrealMCP/Private/UnrealMCPBridge.cpp`：将命令路由至 Animation Commands。
- `Python/tools/animation_tools.py`：暴露 Python MCP 工具并转发 `asset_path`。
- `ChangesDocs/ToolList.md`：登记新工具。

## UE 5.8 兼容处理

`UnrealMCPAnimationCommands.cpp` 的 Enhanced Input 序列化仍需识别已有的 `UInputTriggerCombo` 资产。UE 5.8 已弃用该类型，但未提供等价替代类型，因此保留只读兼容分支，并按照引擎自身的处理方式，仅在该分支周围使用 `PRAGMA_DISABLE_DEPRECATION_WARNINGS` / `PRAGMA_ENABLE_DEPRECATION_WARNINGS`。其他代码仍保留弃用警告检查。

## 验证

1. 静态检查 C++ 声明、命令分发、Bridge 路由和 Python 工具名称一致。
2. 对 `Python/tools/animation_tools.py` 执行语法检查。
3. 编译 MCP 插件并重启 Editor。
4. 分别查询包含 Marker 和不包含 Marker 的 `AnimSequence`，核对数量、名称、时间、归一化时间与轨道信息。
5. 确认编译不再从 `UInputTriggerCombo` 兼容分支产生 C4996，同时没有扩大弃用警告的屏蔽范围。
