# unreal-mcp 扩展：AnimGraph 节点 UObject 端 UPROPERTY 读取（payload 3）

**日期**：2026-04-22
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 触发问题

用户在 `Quad_Mover_ABP` 的 BlendStack 节点上看到 Details 面板有个 `Tag` 字段（用于运行时按 tag 反查节点），但 `get_blueprint_function_graph` 返回的 `anim_node_properties` 里完全没有这个字段。

## 根因

现有 `SerializeAnimGraphNodeExtras`（`UnrealMCPBlueprintCommands.cpp`）给 AnimGraph 节点输出两类 payload：
1. `anim_node_properties` —— 运行时 `FAnimNode_*` struct 上的 EditAnywhere UPROPERTY
2. `property_bindings` —— `UAnimGraphNodeBinding_Base::PropertyBindings` TMap

**漏掉了第三类**：editor 端 `UAnimGraphNode_Base`（及其子类）UObject 自身的 UPROPERTY。`Tag` 就在这里：

```cpp
// Engine/Source/Editor/AnimGraph/Public/AnimGraphNode_Base.h, line 235-237
private:
    UPROPERTY(EditAnywhere, Category = Tag)
    FName Tag;
```

`UAnimGraphNode_Base` 还有几个其他 EditAnywhere UPROPERTY 都被漏：`ShowPinForProperties`（per-property pin 可见性数组）、`InitialUpdateFunction` / `BecomeRelevantFunction` / `UpdateFunction`（FMemberReference 函数回调）。

`UAnimGraphNode_BlendStack` 自身没新加 editor UPROPERTY（只有 `Node` 字段，已被 payload 1 覆盖），所以 BlendStack 的 Tag 完全来自基类。

## 修改

`SerializeAnimGraphNodeExtras` 末尾追加 **payload (3) `node_object_properties`**：

- 用 `TFieldIterator<FProperty>(AnimNode->GetClass())` 遍历整个继承链上的 UPROPERTY
- 过滤条件：必须 `CPF_Edit`（EditAnywhere/EditDefaultsOnly/EditInstanceOnly），跳过 `CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated`
- 显式跳过：内层运行时 `FAnimNode_*` FStructProperty（已在 `anim_node_properties`）+ `Binding` ObjectProperty（已在 `property_bindings`）
- 每个属性走 `FJsonObjectConverter::UPropertyToJsonValue`

返回示例（BlendStack 节点）：

```json
{
  "guid": "D4D5E42D...",
  "class": "AnimGraphNode_BlendStack",
  "title": "Blend Stack",
  "anim_node_struct": "AnimNode_BlendStack",
  "anim_node_properties": { /* 30 个运行时字段 */ },
  "property_bindings": [ /* 5 条 binding */ ],
  "node_object_properties": {
    "Tag": "BlendStackMain",        // ← 用户要的字段
    "ShowPinForProperties": [ {"PropertyName":"AnimationAsset","bShowPin":true,...}, ... ],
    "InitialUpdateFunction": { ... },
    "BecomeRelevantFunction": { ... },
    "UpdateFunction": { ... }
  }
}
```

## 修改的文件

### C++ 端

| 文件 | 改动 |
|---|---|
| `Private/Commands/UnrealMCPBlueprintCommands.cpp` | `SerializeAnimGraphNodeExtras` 顶部注释从"两类 payload"改"三类 payload" + 新增 payload (3) 的实现块（~30 行） |

无新 include / 无 Build.cs 改动 / 无 Bridge 改动 / 无新命令注册（仅扩展现有 `get_blueprint_function_graph` 的输出）。

### Python 端

| 文件 | 改动 |
|---|---|
| `Python/tools/blueprint_tools.py` | `get_blueprint_function_graph` 的 docstring 从"两个 Details-only 字段"改"三个"，新增 `node_object_properties` 字段说明 |

## 兼容性

- 完全向后兼容：仅在响应里**新增**一个 key，已有调用方不受影响
- `pin_payload_mode="names_only"` 仍跳过整个 `SerializeAnimGraphNodeExtras`（含新 payload），与原行为一致

## 验证（编译后）

1. 在 `Quad_Mover_ABP` 的 AnimGraph BlendStack 节点上设一个 `Tag`，比如 "BlendStackMain"，编译保存
2. `get_blueprint_function_graph(blueprint_path="/Game/Leo/Blueprints/Character/Quad/Quad_Mover_ABP", function_name="AnimGraph", pin_payload_mode="summary")`
3. 找到 `AnimGraphNode_BlendStack` 节点，确认 `node_object_properties.Tag == "BlendStackMain"`
4. 顺便核对 `ShowPinForProperties` / 三个 Function 字段是否也都出现

## 后续可能的扩展

- StateMachine / TwoWayBlend / Inertialization 等其他 AnimGraphNode 子类天然受益（同样基类继承 Tag）—— 不需任何额外代码
- 如果将来想 dump 非 AnimGraph 节点（K2Node_*）的 UObject UPROPERTY，可以把这套抽出来通用化。当前只针对 UAnimGraphNode_Base 子类
