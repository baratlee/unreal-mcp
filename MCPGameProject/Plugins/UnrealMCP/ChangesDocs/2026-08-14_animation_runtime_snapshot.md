# Animation Runtime Snapshot

## 目的

动画调试需要区分蓝图静态配置与 PIE 中真正运行的实例。`get_animation_runtime_snapshot` 以角色的 `SkeletalMeshComponent` 为边界，一次返回主 AnimInstance、Linked AnimInstance、Post Process AnimInstance、Linked Anim Layer 实际目标，以及各实例的状态机运行状态。

接口保持引擎通用，不依赖 Sampler 的类型、属性或调试显示。

## 接口

`get_animation_runtime_snapshot(pie_instance_id=None, player_index=0, actor_path="", component_name="")`

- `pie_instance_id`：可选。存在多个 PIE/Game World 时必须指定。
- `player_index`：未指定 `actor_path` 时用于选择本地 Player Pawn，默认 `0`。
- `actor_path`：可选。支持 World 内 Actor 的完整对象路径或对象名；设置后优先于 `player_index`。
- `component_name`：可选。Actor 存在多个 SkeletalMeshComponent 时必须指定，可传组件对象名或完整对象路径。

返回内容包括：

- `frame_counter`、`pie_instance_id`、World、Actor 与 MeshComponent 路径。
- `main_instance`、`linked_instances`、`post_process_instance`。
- 每个实例的类、对象路径，以及所有 Baked State Machine 的当前状态、持续时间、Machine Weight 和各 State Weight。
- `linked_layer_nodes`：节点属性名、Layer、Interface、动态链接函数，以及当前实际 Target Class / Target Instance。

选择条件不唯一时接口返回候选列表并要求调用方显式收窄，避免静默读取错误的 PIE World 或 Mesh。

## 修改范围

- `Source/UnrealMCP/Public/Commands/UnrealMCPAnimationCommands.h`
- `Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp`
- `Source/UnrealMCP/Private/UnrealMCPBridge.cpp`
- `Python/tools/animation_tools.py`
- `ChangesDocs/ToolList.md`

## 验证

1. 静态检查 C++ 声明、命令分发、Bridge 路由和 Python 工具名一致。
2. 对 Python 工具文件执行语法检查。
3. 编译 MCP 插件并重启 Editor 与 MCP Server。
4. 在 PIE 中查询 Player Pawn，确认 Free/Locked 切换时 Linked Layer 的 Target Instance 与状态机当前状态同步变化。
