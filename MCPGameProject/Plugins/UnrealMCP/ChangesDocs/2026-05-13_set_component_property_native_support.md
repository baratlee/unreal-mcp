# unreal-mcp 修复：set_component_property 支持原生继承组件 + get_component_properties 读取蓝图覆盖值

**日期**：2026-05-13
**涉及文件**：
- `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp`

## 问题描述

两个关联缺陷导致 MCP 无法对原生 C++ 继承组件进行读写：

1. **`set_component_property` 找不到原生组件**：该函数只搜索 `Blueprint->SimpleConstructionScript` 的 SCS 节点。当组件由 C++ 父类定义（如 `KLPawnMoverBase` 的 `SkeletalMesh`、`CharacterMover`）时，SCS 中不存在对应节点，直接返回 `"Component not found"` 错误。

2. **`get_component_properties` 读不到蓝图覆盖值**：该函数通过 `FindComponentTemplate()` 查找组件，对 native 组件返回的是**父类 CDO 的组件实例**（默认值），而非蓝图 GeneratedClass CDO 上的覆盖值。用户在蓝图编辑器中修改的属性（如 SkeletalMeshAsset、AnimClass）通过 MCP 读取仍显示为 `None`。

## 修改内容

### 1. `HandleSetComponentProperty`（写入端）

**原逻辑**（行 669-714）：
```cpp
USCS_Node* ComponentNode = nullptr;
for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
{
    if (Node->GetVariableName().ToString() == ComponentName)
    {
        ComponentNode = Node;
        break;
    }
}
if (!ComponentNode) return Error("Component not found");
UObject* ComponentTemplate = ComponentNode->ComponentTemplate;
```

**新逻辑**：两级搜索
```cpp
UObject* ComponentTemplate = nullptr;

// 1. 先搜当前 BP 自身的 SCS
if (Blueprint->SimpleConstructionScript)
{
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->ComponentTemplate &&
            Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentTemplate = Node->ComponentTemplate;
            break;
        }
    }
}

// 2. SCS 找不到 → 从 GeneratedClass CDO 获取继承/原生组件
if (!ComponentTemplate && Blueprint->GeneratedClass)
{
    AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
    if (CDO)
    {
        TInlineComponentArray<UActorComponent*> Components;
        CDO->GetComponents(Components);
        for (UActorComponent* Comp : Components)
        {
            if (Comp && Comp->GetName() == ComponentName)
            {
                ComponentTemplate = Comp;
                break;
            }
        }
    }
}
```

关键点：使用 `Blueprint->GeneratedClass->GetDefaultObject()` 而非父类 CDO。直接修改生成类 CDO 上的组件实例，这与蓝图编辑器的行为一致——覆盖值序列化在蓝图资产中，不影响父类默认值。

### 2. `HandleGetComponentProperties`（读取端）

在 `FindComponentTemplate()` 返回后，增加对 native 组件的重定向：

```cpp
// 对 native 组件，优先从生成类 CDO 读取蓝图覆盖值
if (Source.StartsWith(TEXT("native:")) && Blueprint->GeneratedClass)
{
    if (AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
    {
        TInlineComponentArray<UActorComponent*> Components;
        CDO->GetComponents(Components);
        for (UActorComponent* Comp : Components)
        {
            if (Comp && Comp->GetName() == ComponentName)
            {
                ComponentObj = Comp;
                break;
            }
        }
    }
}
```

`FindComponentTemplate()` 本身不修改，保持向后兼容。

## 影响范围

- `set_component_property`：新增支持对原生/继承组件的属性写入，SCS 组件行为不变
- `get_component_properties`：native 组件现在返回蓝图覆盖值而非父类默认值，SCS 组件行为不变
- 无 Python 端修改，无新参数，完全向后兼容

## 典型使用场景

```
# 之前会返回 "Component not found"
set_component_property(
    blueprint_name="/Game/BP_MyCharacter.BP_MyCharacter",
    component_name="SkeletalMesh",       # C++ 父类定义的原生组件
    property_name="AnimClass",
    property_value="/Game/ABP_MyAnim.ABP_MyAnim_C"
)

# 之前返回父类默认值 None，现在返回蓝图覆盖值
get_component_properties(
    blueprint_path="/Game/BP_MyCharacter",
    component_name="SkeletalMesh"
)
```
