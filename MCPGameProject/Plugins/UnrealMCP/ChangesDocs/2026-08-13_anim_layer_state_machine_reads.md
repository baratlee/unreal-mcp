# Anim Layer State Machine 读取支持

## 目的

`get_anim_state_machine`、`get_anim_state_graph` 和 `get_anim_transition_graph` 原先只在 Blueprint 的 `FunctionGraphs` 中查找 State Machine，因此无法读取 Anim Layer Interface 实现图里的嵌套状态机。

## 修改

三个接口共用的 `FindStateMachineGraph` 现在遍历：

- Ubergraph、函数图、宏图和 Delegate 图。
- `ImplementedInterfaces[].Graphs`。
- 上述 Graph 的递归 `SubGraphs`。

遍历使用已访问集合避免重复图或循环引用。

随后将相同的 Graph 收集逻辑抽取为 `CollectBlueprintGraphs`，并复用于 `get_blueprint_function_graph`、State Machine 查找、Graph 名称查找、按 GUID 查找节点及 Property Binding 枚举。`get_blueprint_function_graph` 因此可以直接返回 Anim Layer Graph 的节点、Pin 与连线。

## 验证

编译插件并重启 Editor 后，使用 `get_anim_state_machine` 查询 `ABP_Humanoid_Locked` 中 `LockedMoving` 内的 `Locked Directional States`，随后用 State/Transition Graph 接口核对节点与规则。
