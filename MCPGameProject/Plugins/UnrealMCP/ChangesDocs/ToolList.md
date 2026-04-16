# UnrealMCP Tool List

> 51 tools total, organized by category.

---

## Editor Tools (8)

| Tool | Description |
|---|---|
| `get_actors_in_level` | 获取当前关卡中所有 Actor 列表 |
| `find_actors_by_name` | 按名称模式搜索 Actor |
| `spawn_actor` | 在当前关卡中创建新 Actor |
| `delete_actor` | 按名称删除 Actor |
| `set_actor_transform` | 设置 Actor 的 Transform（位置/旋转/缩放） |
| `get_actor_properties` | 获取 Actor 的所有属性 |
| `set_actor_property` | 设置 Actor 上的指定属性 |
| `spawn_blueprint_actor` | 从 Blueprint 资产生成 Actor 实例 |

## Blueprint Tools (9)

| Tool | Description |
|---|---|
| `create_blueprint` | 创建新 Blueprint 类 |
| `add_component_to_blueprint` | 为 Blueprint 添加组件 |
| `set_component_property` | 设置 Blueprint 中组件的属性 |
| `set_static_mesh_properties` | 设置 StaticMeshComponent 的网格体属性 |
| `set_physics_properties` | 设置组件的物理属性 |
| `set_blueprint_property` | 设置 Blueprint CDO（Class Default Object）上的属性 |
| `compile_blueprint` | 编译 Blueprint |
| `get_blueprint_info` | 获取 Blueprint 资产详情：组件、变量、函数列表、事件图节点 |
| `get_blueprint_function_graph` | 展开单个函数/AnimGraph 的完整节点图（支持 `pin_payload_mode` 控制响应体积） |

## Blueprint Node Tools (8)

| Tool | Description |
|---|---|
| `add_blueprint_event_node` | 在事件图中添加事件节点 |
| `add_blueprint_input_action_node` | 在事件图中添加 Input Action 事件节点 |
| `add_blueprint_function_node` | 在事件图中添加函数调用节点 |
| `connect_blueprint_nodes` | 连接事件图中的两个节点 |
| `add_blueprint_variable` | 为 Blueprint 添加变量 |
| `add_blueprint_get_self_component_reference` | 添加获取自身组件引用的节点 |
| `add_blueprint_self_reference` | 添加 Get Self 节点（返回当前 Actor 引用） |
| `find_blueprint_nodes` | 在事件图中搜索节点 |

## UMG Widget Tools (6)

| Tool | Description |
|---|---|
| `create_umg_widget_blueprint` | 创建新 UMG Widget Blueprint |
| `add_text_block_to_widget` | 向 Widget Blueprint 添加 Text Block 控件 |
| `add_button_to_widget` | 向 Widget Blueprint 添加 Button 控件 |
| `bind_widget_event` | 将控件事件绑定到函数 |
| `set_text_block_binding` | 为 Text Block 设置属性绑定 |
| `add_widget_to_viewport` | 将 Widget Blueprint 实例添加到 Viewport |

## Project Tools (1)

| Tool | Description |
|---|---|
| `create_input_mapping` | 创建项目输入映射 |

## Animation Tools (9)

| Tool | Description |
|---|---|
| `get_animation_info` | 获取 AnimSequence/AnimMontage 基本信息（时长、帧数、骨骼、Root Motion 字段） |
| `get_animation_notifies` | 获取动画通知事件列表（事件型 + NotifyState 型） |
| `get_animation_curve_names` | 获取动画中的 Float 曲线名列表 |
| `get_animation_bone_track_names` | 获取骨骼轨道名列表（Montage 自动穿透 Segment 聚合） |
| `get_montage_composite_info` | 获取 AnimMontage 组合结构：Sections / SlotAnimTracks / Segments / Blend 参数 |
| `find_animations_for_skeleton` | 通过 Asset Registry 按 Skeleton 反查所有关联的 AnimSequence/AnimMontage |
| `get_skeleton_bone_hierarchy` | 获取 USkeleton 的完整骨骼层级（含虚拟骨骼） |
| `get_skeleton_retarget_modes` | 获取 USkeleton 每根骨骼的 TranslationRetargetingMode |
| `list_chooser_tables` | 通过 Asset Registry 列出项目中所有 UChooserTable 资产 |

## Chooser Table Tools (1)

| Tool | Description |
|---|---|
| `get_chooser_table_info` | 读取 UChooserTable 的结构骨架：列定义 + 结果行（asset/nested_chooser 等 4 种 kind） |

## IK Tools (4)

| Tool | Description |
|---|---|
| `list_ik_rigs` | 通过 Asset Registry 列出项目中所有 UIKRigDefinition 资产 |
| `get_ik_rig_info` | 读取 IKRig 的静态结构（骨骼链、Solver、Goal） |
| `list_ik_retargeters` | 通过 Asset Registry 列出项目中所有 UIKRetargeter 资产 |
| `get_ik_retargeter_info` | 读取 IKRetargeter 的静态结构（源/目标 IKRig、链映射） |

## State Machine Tools (3)

| Tool | Description |
|---|---|
| `get_anim_state_machine` | 获取 AnimBP 中状态机的结构：状态列表 + 转换拓扑 + 入口状态 |
| `get_anim_state_graph` | 获取状态机单个状态内部的动画节点图 |
| `get_anim_transition_graph` | 获取状态转换的条件图 + 元数据（crossfade、blend_mode 等） |

## Enhanced Input Tools (2)

| Tool | Description |
|---|---|
| `get_input_action_info` | 读取 InputAction（IA）资产：ValueType、Triggers、Modifiers、标志位 |
| `get_input_mapping_context_info` | 读取 InputMappingContext（IMC）资产：所有按键映射及其 Action/Triggers/Modifiers |
