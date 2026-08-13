# AnimGraph 节点 Property Binding 紧凑查询

## 目的

`get_blueprint_function_graph` 只能按顶层函数图名称展开节点，无法直接查询 Linked Anim Graph 等嵌套图中的节点。为避免用全图输出或 Slate UI Snapshot 核对单个 Binding，增加按节点 GUID 查询的只读接口。

## 接口

`get_anim_graph_node_property_bindings(blueprint_path, node_guid?, graph_name?, node_class?)`

接口支持两种查询方式：

- `node_guid`：跨 Blueprint 的顶层图与 `SubGraphs` 精确查找单个节点。
- `graph_name` + 可选 `node_class`：枚举指定嵌套图或 Anim Layer Interface 实现图内匹配的 AnimGraph 节点，同时返回 GUID 与 Binding。

两种方式都返回节点、所属图以及 `Binding->PropertyBindings` 的结构化内容。无 Binding 时返回空数组。

Graph 遍历根包含 Blueprint 的 Ubergraph、函数图、宏图、Delegate 图以及 `ImplementedInterfaces[].Graphs`，再递归进入各自的 `SubGraphs`。

## 修改范围

- `UnrealMCPBridge.cpp`：路由命令。
- `UnrealMCPBlueprintCommands.h/.cpp`：声明、实现与复用 Binding 序列化。
- `Python/tools/blueprint_tools.py`：暴露 MCP 工具。
- `ChangesDocs/ToolList.md`：登记工具。

## 验证

1. 静态检查 C++ 声明、命令路由与实现一致。
2. Python 文件通过语法检查。
3. 编译插件并重启 Editor 后，对嵌套 AnimGraph 节点调用接口，确认 `bindings[].detail.PropertyPath` 与编辑器设置一致。
