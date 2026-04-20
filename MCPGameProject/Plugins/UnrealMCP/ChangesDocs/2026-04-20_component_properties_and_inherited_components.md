# unreal-mcp 扩展：get_component_properties 新工具 + get_blueprint_info 返回继承组件

**日期**：2026-04-20
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 修改目的

两个痛点促成此次扩展：

1. **看不到继承组件**：`get_blueprint_info` 只返回 BP 自身 SCS 中添加的组件，父类继承的组件（如 `BaseAnimatedMannyPawn` 的 SkeletalMeshComponent、CapsuleComponent）完全不可见。导致无法通过 MCP 验证 AnimClass 是否正确设置。

2. **看不到嵌套子对象属性**：`CharacterMoverComponent` 的 `SharedSettings` 是 `TArray<UObject*>`，其中 `CommonLegacyMovementSettings` 的 `MaxSpeed`、`Acceleration` 等关键属性无法读取。现有工具只能看到数组元素的资产路径字符串，看不到内部属性值。

## 新增功能

### 1. `get_component_properties`（新工具）

读取 BP 上指定组件的所有可编辑属性，支持递归展开嵌套子对象。

**参数**：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `blueprint_path` | string | 是 | Blueprint 资产路径 |
| `component_name` | string | 是 | 组件名称（SCS 变量名或 CDO 组件名） |
| `max_depth` | int | 否 | 递归深度，1-4，默认 2 |

**组件查找顺序**：
1. 当前 BP 的 SimpleConstructionScript
2. 沿父类 BP 链向上查找父 BP 的 SCS（继承的 BP 组件）
3. 到达 C++ 类时查找 CDO 的原生组件

**属性序列化规则**：
- 基础类型（bool/int/float/string/enum/struct）：通过 `ExportTextItem_Direct` 导出为字符串
- `FObjectProperty`（单个子对象）：输出路径 + 类名；如果是 inner sub-object 且未超深度限制，递归展开 `sub_properties`
- `FArrayProperty`（数组）：
  - 内部是 object 类型 → 展开每个元素的属性为 `elements` 数组
  - 其他类型 → 导出为文本字符串

**返回示例**（关键字段）：
```json
{
  "component_name": "CharacterMover",
  "component_class": "CharacterMoverComponent",
  "source": "scs",
  "properties": [
    {"name": "MaxSpeed", "type": "float", "value": "800.000000"},
    {
      "name": "SharedSettings",
      "type": "TArray",
      "elements": [{
        "class": "CommonLegacyMovementSettings",
        "properties": [
          {"name": "MaxSpeed", "type": "float", "value": "800.000000"},
          {"name": "Acceleration", "type": "float", "value": "4000.000000"}
        ]
      }]
    }
  ]
}
```

### 2. `get_blueprint_info` 增加 `inherited_components` 字段

在现有 `components` 数组之后新增 `inherited_components` 数组，列出所有从父类继承的组件（不含详细属性，仅 name/class/inherited_from）。

**查找逻辑**：
- 沿父类 BP 链遍历每个父 BP 的 SCS 节点
- 到达 C++ 类时通过 CDO `GetComponents()` 获取原生组件

**返回示例**：
```json
"inherited_components": [
  {"name": "Capsule", "class": "CapsuleComponent", "inherited_from": "BaseAnimatedMannyPawn"},
  {"name": "SK_Mannequin", "class": "SkeletalMeshComponent", "inherited_from": "BaseAnimatedMannyPawn"},
  {"name": "SpringArm", "class": "SpringArmComponent", "inherited_from": "BaseAnimatedMannyPawn"},
  {"name": "Camera", "class": "CameraComponent", "inherited_from": "BaseAnimatedMannyPawn"}
]
```

## 修改的文件

### C++ 端（UnrealMCP 插件）

| 文件 | 改动 |
|---|---|
| `Public/Commands/UnrealMCPBlueprintCommands.h` | 声明 `HandleGetComponentProperties`、`FindComponentTemplate`、`SerializePropertiesToJson` |
| `Private/Commands/UnrealMCPBlueprintCommands.cpp` | 实现三个新函数；`HandleGetBlueprintInfo` 增加 `inherited_components` 收集逻辑 |
| `Private/UnrealMCPBridge.cpp` | 路由表添加 `get_component_properties` → BlueprintCommands |

### Python 端（unreal-mcp server）

| 文件 | 改动 |
|---|---|
| `Python/tools/blueprint_tools.py` | 注册 `get_component_properties` 工具（docstring + send_command） |

## 验证结果

1. **`get_blueprint_info`**：对 `BP_Quad_Character` 调用后，`inherited_components` 正确返回 4 个继承组件（Capsule / SK_Mannequin / SpringArm / Camera），均来自 `BaseAnimatedMannyPawn`
2. **`get_component_properties`**：对 `CharacterMover` 组件调用（max_depth=3），成功读到嵌套的 `CommonLegacyMovementSettings` 内部所有属性，包括 MaxSpeed=800、Acceleration=4000、TurningRate=500 等
