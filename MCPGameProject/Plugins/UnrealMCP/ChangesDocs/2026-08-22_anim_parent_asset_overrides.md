# AnimBlueprint Parent Asset Override MCP

## 目标

为子 AnimBlueprint 的 Parent Asset Override 提供可审计的读取、设置和删除接口，不创建或移动 Graph 节点。

## 命令

### `get_anim_parent_asset_overrides`

- 参数：`blueprint_path`
- 返回所有可覆盖父动画节点，而不只返回已有本地 Override。
- 每项包含父节点 GUID、节点名 / 标题 / 类型、Graph 名、父图资源、继承 Override、本地 Override 和最终生效资源。

### `set_anim_parent_asset_override`

- 参数：`blueprint_path`、`parent_node_guid`、`animation_asset_path`
- 校验目标是拥有父 AnimBlueprint 的子 AnimBP。
- 校验 GUID 属于可覆盖动画节点、资源类型受节点支持、Skeleton 与目标 AnimBP 兼容。
- 复用 `UEditorParentPlayerListObj::ApplyOverrideToBlueprint`，保持与编辑器 Details 面板一致的事务、通知、去重和 Blueprint 标脏语义。
- 当目标资源等于父级回退资源时不创建冗余 Override；已有本地 Override 会被规范化移除。
- 返回 `saved=false`，由调用者显式执行 `save_dirty_assets`。

### `remove_anim_parent_asset_override`

- 参数：`blueprint_path`、`parent_node_guid`
- 删除本地 Override，回退到最近的继承 Override；没有继承项时回退到父 Graph 节点资源。
- 删除不存在的本地 Override 是成功的无操作。

## 传输稳定性

Python 连接层不再提前建立空闲 TCP 连接，也不再发送 `NUL` 健康检查。每条命令会在连接建立后立即发送已经编码好的 JSON 请求，避免 Unreal 桥接线程在请求数据抵达前把连接判为空。

## 引擎依据

- `Engine/Source/Runtime/Engine/Classes/Animation/AnimBlueprint.h`
  - `FAnimParentNodeAssetOverride`
  - `UAnimBlueprint::ParentAssetOverrides`
  - `GetAssetOverrideForNode` / `GetAssetOverrides` / `NotifyOverrideChange`
- `Engine/Source/Editor/UnrealEd/Private/Animation/EditorParentPlayerListObj.cpp`
  - `InitialiseFromBlueprint`
  - `ApplyOverrideToBlueprint`
- `Engine/Source/Editor/Persona/Private/AnimGraphNodeDetails.cpp`
  - 使用 `USkeleton::IsCompatibleForEditor` 过滤不兼容资源

## 文件

- `Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp`
- `Source/UnrealMCP/Public/Commands/UnrealMCPAnimationCommands.h`
- `Source/UnrealMCP/Private/UnrealMCPBridge.cpp`
- `C:/GitHub/unreal-mcp/Python/tools/animation_tools.py`
- `C:/GitHub/unreal-mcp/Python/unreal_mcp_server.py`

## 验证边界

- 当前完成源码与 Python 语法静态检查。
- C++ 编译、编辑器重启和真实 AnimBP 读写回归需要在本次修改后执行。
- Parent Asset Override 不会给父 AnimBP 中未赋值的 Sequence Player 自动补默认资源。
