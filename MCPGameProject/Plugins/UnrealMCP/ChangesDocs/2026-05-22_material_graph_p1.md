# 2026-05-22 · Material 节点图 P1

延续同日 P0（`2026-05-22_material_read_p0.md`）。补齐 Material 资产读取的**节点图维度**：每个 expression 节点、所有连线、根 pin 连接、注释节点。

P0 已实测通过，本 P1 在同会话内推进。

---

## 新增工具（1 个）

| 工具 | 入参 | 用途 |
|---|---|---|
| `get_material_graph` | `asset_path` + `pin_payload_mode` + `include_comments` + `include_root_inputs` | 完整节点图：节点 + 连线 + Material root pins + 注释 |

## Schema

**入参：**
| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `asset_path` | string | (required) | `/Game/.../M_Foo` |
| `pin_payload_mode` | string | `"summary"` | `"names_only"` 仅 guid+class；`"summary"` 加位置/输入输出/常量值 UPROPERTY；`"full"` 加全部非-input UPROPERTY（含对象引用） |
| `include_comments` | bool | `true` | 是否输出 UMaterialExpressionComment |
| `include_root_inputs` | bool | `true` | 是否输出 Material root pins（BaseColor/EmissiveColor 等） |

**返回（summary 模式样例）：**
```json
{
  "asset_path": "...",
  "class_name": "Material",
  "node_count": 47,
  "comment_count": 3,
  "connection_count": 65,
  "root_connection_count": 1,
  "nodes": [
    {
      "guid": "1A2B3C4D-...",
      "class": "MaterialExpressionScalarParameter",
      "position": [-1200, 320],
      "outputs": [{"index": 0, "name": ""}],
      "inputs": [],
      "properties": {
        "ParameterName": "RippleSpeed",
        "DefaultValue": "1.000000",
        "Group": "None"
      }
    }
  ],
  "comments": [
    {"guid": "...", "position": [x, y], "size": [w, h], "text": "EdgeMask 段"}
  ],
  "connections": [
    {
      "source_guid": "...",
      "source_output_index": 0,
      "target_guid": "...",
      "target_input_index": 0,
      "target_input_name": "A"
    }
  ],
  "root_inputs": [
    {
      "name": "EmissiveColor",
      "input_struct": "ColorMaterialInput",
      "connected": true,
      "source_guid": "...",
      "source_output_index": 0
    }
  ]
}
```

## 关键实现

- **节点源**：`Material->GetExpressionCollection().Expressions`（`TArray<TObjectPtr<UMaterialExpression>>`），editor-only。
- **每节点 GUID**：`MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens)`；GUID 无效时退回 `Expr->GetName()` 作 ID。
- **outputs**：`Expr->GetOutputs()` → `TArray<FExpressionOutput>`，取 `OutputName` 与索引。
- **inputs**：`Expr->GetInputsView()` → `TArrayView<FExpressionInput*>`；pin 名走 `Expr->GetInputName(i)`（而非 `FExpressionInput::InputName`，后者不总赋值）。
- **连线提取**：遍历每个 expression 的 inputs，`In->Expression != nullptr` 的连线记录一条；`In->OutputIndex` 给源 pin 索引。
- **注释**：`Collection.EditorComments` → `UMaterialExpressionComment`：`SizeX/SizeY/Text`。
- **Root inputs**：`Material->GetEditorOnlyData()`（`UMaterialEditorOnlyData*`）含 BaseColor/EmissiveColor/Roughness/Opacity/Normal/... 一组 `FColorMaterialInput`/`FScalarMaterialInput`/`FVectorMaterialInput`/`FMaterialAttributesInput` 字段。
  - 通过 `TFieldIterator<FProperty>` 遍历 + `FStructProperty::Struct` 沿 super-struct 链匹配 **`ExpressionInput` 或 `MaterialInput`** 名 → 自动覆盖所有 root pin 类型。
  - **5.7 反射坑（首次实测发现并修复）**：UHT noexport 镜像里 `FMaterialInput`（Material.h L155-187）和 `FExpressionInput`（MaterialExpression.h L43）是**两条独立**的 USTRUCT 链，并非 C++ 上的"FMaterialInput<T> 继承 FExpressionInput"那种父子关系。只匹配 `ExpressionInput` 会漏掉 16 个 FColor/FScalar/FVector/FVector2/FShadingModel/FSubstrate MaterialInput 字段，只剩 `FMaterialAttributesInput`（因为它的反射镜像是 `FExpressionInput` 派生）可见。
  - 用 `reinterpret_cast<const FExpressionInput*>(struct_addr)` 取基类视图（两条链的 layout 头部 3 字段 Expression/OutputIndex/InputName 一致，cast 安全）。
  - **未支持**：`UMaterialEditorOnlyData::CustomizedUVs[8]` 是 fixed-size array of `FVector2MaterialInput`（ArrayDim=8 的 FStructProperty），P1 当前只扫单 pin，没处理数组维度。如需 UV root 连接走 P2 时一起补。
- **`pin_payload_mode`**：与 `get_blueprint_function_graph` 命名对齐。`summary` 默认只 dump simple-value UPROPERTY（bool/int/float/string/name/enum/LinearColor/Vector/Color/Rotator 等），避开 object reference 和复杂 struct 防止响应爆炸。
- **嵌套 Material Function 不展开**：`UMaterialExpressionMaterialFunctionCall` 以单节点形式返回，`MaterialFunction` 引用保留在 `properties` 里（full 模式可见）。函数内部图属于 **P2** `get_material_function_info`。
- **跳过的 UPROPERTY**：MaterialExpressionGuid/EditorX/EditorY/Outputs/DesiredOutputs/GraphNode/SubgraphExpression/Function/Material（基类字段或单独输出）。
- **WITH_EDITOR 守卫**：整个 `HandleGetMaterialGraph` 包在 `#if WITH_EDITOR`（`UMaterial::GetExpressionCollection` 等都是 editor-only）；非 editor build 返回错误。

## 涉及文件

- 改 `Source/UnrealMCP/Public/Commands/UnrealMCPMaterialCommands.h` — 加 `HandleGetMaterialGraph` 声明
- 改 `Source/UnrealMCP/Private/Commands/UnrealMCPMaterialCommands.cpp`：
  - include `Materials/MaterialExpression.h` / `Materials/MaterialExpressionComment.h` / `MaterialExpressionIO.h`
  - 加 anonymous namespace helper：`EMaterialPayloadMode` / `ParsePayloadMode` / `MakeNodeId` / `IsExpressionInputStruct` / `IsSkippableExpressionProperty` / `IsSimpleValueProperty` / `SerializeExpressionProperties` / `SerializeExpressionNode` / `CollectConnectionsFromExpression` / `CollectRootInputs`
  - 加 `HandleGetMaterialGraph` 实现
  - HandleCommand 加 `get_material_graph` 路由
- 改 `Source/UnrealMCP/Private/UnrealMCPBridge.cpp` — 命令分支加 `get_material_graph`
- 改 Python `tools/material_tools.py` — 加 `get_material_graph` tool
- Build.cs **未改**（FMaterialExpressionCollection / UMaterialExpression / UMaterialExpressionComment 都在 Engine 模块，editor-only API 在 `bBuildEditor` 段已启用）

## UE 5.7 字段依据

源码已登记到 `.claude/memory/project_ue_source_priority_list.md`（3 条 2026-05-22 P1 条目）：
- `MaterialExpression.h` — `UMaterialExpression::MaterialExpressionGuid/EditorX/EditorY` + `GetInputsView/GetInputName/GetOutputs`；`FMaterialExpressionCollection.Expressions/EditorComments`
- `MaterialExpressionIO.h` — `FExpressionInput.Expression/OutputIndex/InputName`；`FExpressionOutput.OutputName/Mask`；`FMaterialInput<T>` 派生体系
- `MaterialExpressionComment.h` — SizeX/SizeY/Text

## 端到端验证

1. `get_material_graph("/Game/Examples/Leo/M_PP_DreamRipple")` 默认 summary 模式：
   - 期望 ~47 个节点（含 6 个 SceneTexture + 5 个 ScalarParameter + ...）
   - 期望 `root_connection_count: 1`（仅 EmissiveColor 连了）
   - 期望某个 SceneTexture 节点的 `properties.SceneTextureId == "PPI_PostProcessInput0"`（验证 P0 编译报错的修正点）
2. `pin_payload_mode: "names_only"` 拿全节点 ID 列表（用于快速概览）
3. `pin_payload_mode: "full"` 拿完整 UPROPERTY（用于深度审计）

## 后续

- **P2**：
  - `get_material_function_info` — Material Function 内部图（`UMaterialFunction::GetExpressionCollection()`，结构对称，复用本批 helper 几乎可零改动接入）
  - `list_materials` — AssetRegistry 按目录扫，复用 `list_data_assets` 模式
- **P3**（按需）：写入工具，目前无明确需求驱动
