# unreal-mcp Batch D：Chooser Table 全套读/写扩展

**日期**：2026-04-22
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 对应需求

`project_unreal_mcp_chooser_extension.md`（per-project memory）P0–P3 全套（用户 2026-04-22 批准）。需求驱动：Quad Step C 方案 A（CHT 同时输出 `AnimationAsset` + `FQuadChooserOutputs` struct via Output Column）。

## Batch 拆分

| 子批次 | 目标 |
|---|---|
| D.0 | 扩展 `get_chooser_table_info`（读） |
| D.1 | 顶层配置写（3 + 1） |
| D.2 | 列写（3） |
| D.3 | 行写（2 新） |

## D.0：`get_chooser_table_info` 扩展

新增字段（原有字段兼容保留）：

- `result_type`：`"ObjectResult"` / `"ClassResult"` / `"NoPrimaryResult"`
- `result_class`：`{name, path}` 或 null（`UChooserSignature::OutputObjectType`）
- `parameter_count` / `parameters[]`：每项 `{index, struct_name, kind: "class"/"struct", class_or_struct: {name,path}, direction: "Input"/"Output"/"InputOutput"}`（源码 direction Read/Write/ReadWrite 映射 UI 侧）
- 每个 column 额外：`has_filters` / `has_outputs`（虚函数），`sub_type: "Input"/"Output"/"Mixed"`，`b_disabled`（editor-only），`binding`：`{binding_struct, context_index, is_bound_to_root, property_path[], path_as_text, display_name(editor), enum_type/allowed_class/struct_type (by subclass)}` 或 null
- 每行额外：`column_values[]` —— 反射读该列 `RowValues[row_index]`，走 `UPropertyToJsonValue` 导出（EnumRowData / bool / FloatRange 等都能自然序列化）
- `fallback_result`：与行 Result 同 shape（asset / soft_asset / evaluate_chooser / nested_chooser），或 null

**不纳入的 P0 诉求**：原 memory 说"一行 Result 是 TArray<FObjectChooserBase>"—— 核对 `FAssetChooser::Asset` 源码后确认**UE5.7 每行单资产**，不是 TArray。"多候选"模式本质是多行，每行一个资产，通过列条件区分。`asset_paths[]` 字段取消。

## D.1：顶层配置写工具（4 个）

### `set_chooser_table_result(asset_path, result_type, result_class_path="")`
- 写 `ResultType` + `OutputObjectType`
- result_type 支持多种别名：`ObjectResult`/`Object Of Type`/`Object` 三种都认 ObjectResult；同理 ClassResult/NoPrimaryResult
- 返回里带一句 "Changing ResultType invalidates existing row Results" 警示

### `set_chooser_table_fallback_result(asset_path, result_asset_path="")`
- 空字符串 → `FallbackResult.Reset()` 清空
- 非空 → 装 `FAssetChooser{Asset=result}` 进 FallbackResult

### `add_chooser_table_parameter(asset_path, kind, class_or_struct_path, direction="Input")`
- kind: "class" / "struct"
- direction: "Input"/"Output"/"InputOutput"（映射到 `EContextObjectDirection::Read/Write/ReadWrite`）
- 构造 `FContextObjectTypeClass` 或 `FContextObjectTypeStruct` 追加到 `ContextData`

### `remove_chooser_table_parameter(asset_path, parameter_index)`
- 返回里提示列 binding 可能失效

## D.2：列写工具（3 个）

### `add_chooser_table_column(asset_path, column_type, binding_path_as_text="", context_index=0, enum_path="")`
- column_type 支持裸名（`EnumColumn`）或 `/Script/Chooser.EnumColumn`
- `ResolveColumnStruct` 做两种解析 + 校验 `IsChildOf(FChooserColumnBase)`
- `InitializeAs(ColStruct)` 创建列，CHOOSER_COLUMN_BOILERPLATE 会自动创建 InputValue 的默认 ParameterType
- 额外 fallback：若 InputValue 没初始化，用 `GetInputBaseType()` 初始化
- binding_path_as_text 按 "." 分段写入 `FChooserPropertyBinding::PropertyBindingChain`，同时写 ContextIndex + editor-only DisplayName
- enum_path 对 `FChooserEnumPropertyBinding::Enum` 专门赋值（EnumColumn/MultiEnumColumn 专用）
- 追加后调 `SyncChooserColumnRowCounts` 把新列 RowValues 扩容到当前行数
- 触发 `Compile(bForce=true)`

### `remove_chooser_table_column(asset_path, column_index)`
- 从 ColumnsStructs 移除，触发 Compile

### `set_chooser_table_column_binding(asset_path, column_index, binding_path_as_text, context_index=0, enum_path="")`
- 不改列类型，只重写该列的 Binding 字段
- 用于修复 Parameter 顺序变动后的失效 binding

## D.3：行写工具（2 个新）

原有的 `add_chooser_table_row` / `remove_chooser_table_row` 保留不扩展（用户已定方案 B 每行单资产）。新增：

### `set_chooser_table_row_result(asset_path, row_index, result_asset_path)`
- 替换行 Result，重新装 `FAssetChooser`

### `set_chooser_table_row_column_value(asset_path, row_index, column_index, value)`
- 按 column_index + row_index 定位 RowValues 元素
- 走 `FProperty::ImportText_Direct` 解析 T3D 文本值
- Python wrapper 做 Union[str,bool,int,float] → str coerce 预处理（复用 `set_ik_retargeter_op_field` 的防 pydantic bug 模式）

## 核心实现要点

- **通用 helper** 在匿名命名空间：
  - `LoadChooserTableOrError` —— 加载 + 友好报错
  - `ParseChooserResultType` / `ParseContextObjectDirection` —— 字符串 → 枚举
  - `ResolveColumnStruct` —— 两种形式的 column_type 解析
  - `ApplyBindingToColumn` —— 反射写 Binding（支持 Enum sub-type）
  - `PostWriteChooserCompile` —— 写后触发 `Compile(bForce=true)`
  - `SyncChooserColumnRowCounts` —— 保持每列 RowValues 长度与行数对齐
- **读扩展** helper：
  - `ChooserResultTypeToString` / `ContextObjectDirectionToString` —— 枚举 → UI/源码字符串
  - `SerializeChooserResult` / `SerializeChooserParameter` / `SerializeChooserColumnBinding` —— 各单元的 JSON 化

## 修改的文件

### C++（LeoPractice）
| 文件 | 改动 |
|---|---|
| `Public/Commands/UnrealMCPAnimationCommands.h` | 声明 9 个新 handler |
| `Private/Commands/UnrealMCPAnimationCommands.cpp` | 新 include：`IChooserParameterBase.h` / `ChooserPropertyAccess.h` / `IHasContext.h`；重写 `HandleGetChooserTableInfo`（新增 7 组字段）；末尾追加 9 个 handler 实现 + 通用 helper；`HandleCommand` 路由追加 9 条 |
| `Private/UnrealMCPBridge.cpp` | Animation 路由段追加 9 条命令 |

无 Build.cs 改动（Chooser 模块依赖之前已有）。

### Python（unreal-mcp）
| 文件 | 改动 |
|---|---|
| `Python/tools/animation_tools.py` | `get_chooser_table_info` docstring 升级反映新字段；追加 9 个新 MCP tool wrapper |

## 验收（按 memory 的 9 步流程）

1. 手动建空 CHT（非 MCP 范围）
2. `set_chooser_table_result(..., "ObjectResult", "/Script/Engine.AnimationAsset")`
3. `add_chooser_table_parameter(..., "class", "<AnimInstance 子类 _C>", "Input")`
4. `add_chooser_table_parameter(..., "struct", "/Script/LeoPractice.QuadChooserOutputs", "Output")`
5. `add_chooser_table_column(..., "EnumColumn", "MovementState", 0, enum_path="EQuadStateMachineState")`
6. `add_chooser_table_column(..., "EnumColumn", "CurrentGait", 0, enum_path="EQuadGait")`
7. 6 × `add_chooser_table_column(..., "OutputXxxColumn", "QuadChooserOutputs.<field>", 1)`
8. 若干 × `add_chooser_table_row(..., <single asset>, column_values=[...])` + 相应 × `set_chooser_table_row_column_value(..., <output col index>, <value>)`
9. `get_chooser_table_info(...)` 所有字段与 Path 1 设计对齐

## 生效方式

**两步都要**：
1. 关 UE → 编译 UnrealMCP 插件（C++ 变更）
2. 重启 Claude Code 或 `/mcp` reload（Python wrapper 新增需要 MCP schema 重刷）

## 废弃条件

按 `project_unreal_mcp_chooser_extension.md` 最后的约定：验收 9 步全部通过 + `project_unreal_mcp_tools.md` 里程碑表加"Chooser 顶层配置/列/行扩展 (D 批) 已验证"之后，删除 `project_unreal_mcp_chooser_extension.md` 这条 per-project memory。
