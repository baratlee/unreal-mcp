# unreal-mcp 扩展：设置 Graph 节点输入 Pin 默认值

## 目标

新增 `set_graph_node_pin_default_value`，用于修改 Blueprint 或 AnimBlueprint 图中现有节点的未连接输入 Pin 默认值。典型场景是把 AnimGraph 内 `Safe Divide` 节点的 `B` 从 `0.0` 改为 `100.0`。

## 接口

参数：

- `blueprint_path`：Blueprint / AnimBlueprint 资产路径。
- `node_guid`：`get_blueprint_function_graph` 返回的节点 GUID。
- `pin_name`：内部输入 Pin 名称。
- `value`：编辑器格式的目标值；Python 层接受字符串、整数、浮点数和布尔值。

返回节点、Graph、Pin、请求值、旧默认值以及 Schema 规范化后的默认值。

## 安全边界

- 仅处理输入 Pin。
- 拒绝已连接、孤立或默认值只读的 Pin。
- 仅处理 K2 兼容 Graph Schema。
- 写入前调用 K2 Schema 解析与 `IsPinDefaultValid` 校验。
- 成功后标记 Blueprint 已修改，但不自动编译或保存资产。

## 修改范围

- `Python/tools/blueprint_tools.py`
- `UnrealMCPBlueprintCommands.h/.cpp`
- `UnrealMCPBridge.cpp`
- `ChangesDocs/ToolList.md`

## 验证方式

1. 静态检查 Python 文件可解析。
2. 编译 UnrealMCP Editor 模块。
3. 重启 Unreal Editor 与 MCP 会话。
4. 对未连接的 `Safe Divide.B` 调用工具并回读 `get_blueprint_function_graph`，期望默认值为 `100.0` 的 Schema 规范化表示。
5. 分别验证不存在的节点、输出 Pin、已连接 Pin、只读 Pin 和非法数值返回错误且不修改资产。
