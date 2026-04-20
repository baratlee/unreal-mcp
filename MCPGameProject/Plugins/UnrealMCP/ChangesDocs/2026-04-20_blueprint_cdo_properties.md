# unreal-mcp 扩展：get_blueprint_cdo_properties 新工具

**日期**：2026-04-20
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 修改目的

`get_blueprint_info` 只返回组件/变量/图表结构信息，无法读取 Blueprint CDO（Class Default Object）上 C++ `EditDefaultsOnly` UPROPERTY 的实际值。例如 AnimBP 的 `IdleAnimation`、`WalkMaxSpeed` 等在 Class Defaults 面板中设置的属性值完全不可见。

## 新增功能

### `get_blueprint_cdo_properties`

读取 Blueprint GeneratedClass CDO 上所有带 `CPF_Edit` 标志的属性值。

**参数**：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `blueprint_path` | string | 是 | Blueprint 资产路径 |
| `max_depth` | int | 否 | 递归深度，1-4，默认 2 |
| `category_filter` | string | 否 | 按 Category 元数据过滤（子串匹配，不区分大小写），为空返回全部 |

**属性序列化规则**：
- 基础类型（bool/int/float/string/enum/struct）：通过 `ExportTextItem_Direct` 导出
- `FObjectProperty`：输出路径 + 类名；inner sub-object 递归展开 `sub_properties`
- `FArrayProperty`：
  - 内部是 object → 展开每个元素的属性
  - 其他类型 → 导出为文本字符串
- 跳过 `CPF_Transient | CPF_DuplicateTransient` 属性

**返回示例**（对 AnimBP 使用 category_filter="Quad"）：
```json
{
  "blueprint_path": "/Game/.../Quad_Mover_ABP.Quad_Mover_ABP",
  "class_name": "Quad_Mover_ABP_C",
  "parent_class": "QuadAnimInstance",
  "properties": [
    {"name": "IdleAnimation", "category": "Quad|Animations", "type": "object", "value": "/Game/.../Idle.Idle", "object_class": "AnimSequence"},
    {"name": "WalkAnimation", "category": "Quad|Animations|Locomotion", "type": "object", "value": "/Game/.../Loco_Walk.Loco_Walk", "object_class": "AnimSequence"},
    {"name": "WalkSlowMaxSpeed", "category": "Quad|Config", "type": "float", "value": "150.000000"},
    {"name": "WalkMaxSpeed", "category": "Quad|Config", "type": "float", "value": "350.000000"},
    {"name": "VelocityThreshold", "category": "Quad|Config", "type": "float", "value": "10.000000"}
  ]
}
```

## 修改的文件

### C++ 端（UnrealMCP 插件）

| 文件 | 改动 |
|---|---|
| `Public/Commands/UnrealMCPBlueprintCommands.h` | 声明 `HandleGetBlueprintCDOProperties` |
| `Private/Commands/UnrealMCPBlueprintCommands.cpp` | 实现 `HandleGetBlueprintCDOProperties`；路由表添加 |
| `Private/UnrealMCPBridge.cpp` | 路由表添加 `get_blueprint_cdo_properties` → BlueprintCommands |

### Python 端（unreal-mcp server）

| 文件 | 改动 |
|---|---|
| `Python/tools/blueprint_tools.py` | 注册 `get_blueprint_cdo_properties` 工具 |

## 与 get_component_properties 的区别

| | `get_component_properties` | `get_blueprint_cdo_properties` |
|---|---|---|
| **目标** | BP 上的某个组件实例 | Blueprint CDO 本身 |
| **用途** | 读取 MoverComponent 的 MaxSpeed 等 | 读取 AnimBP 的 IdleAnimation、Config 阈值等 |
| **查找范围** | SCS → 父 BP SCS → 原生 CDO 组件 | GeneratedClass CDO 所有 Edit 属性 |
| **过滤** | 无 | 支持 category_filter |
