# unreal-mcp 扩展：ChooserTable 只读工具（动画工具第三批）

**日期**：2026-04-15
**涉及仓库**：`C:\Workspace\git\unreal-mcp`
**前置变更**：
- [`2026-04-14_unreal_mcp_animation_tools.md`](./2026-04-14_unreal_mcp_animation_tools.md)（第一批 4 个动画工具）
- [`2026-04-14_unreal_mcp_skeleton_tools.md`](./2026-04-14_unreal_mcp_skeleton_tools.md)（第二批 Skeleton 工具）

## 修改目的

Game Animation Sample 等动画样本里大量使用 `UChooserTable` 作为动画分支选择入口（`CHT_*` 命名族），结构是「列条件 + 行结果」的二维表，每行最终落到一个 AnimSequence/AnimMontage/PoseSearchDatabase/CameraRig 或下一张子表上。在没有只读检查工具之前，Claude 没法搞清楚一个动画系统的整体路由——既不知道项目里有哪些 ChooserTable，也不知道某张表的列条件类型与行结果指向。

新增工具：

| Python 工具 | 对应命令 | 作用 |
|---|---|---|
| `list_chooser_tables` | `list_chooser_tables` | Asset Registry 按类型扫 `UChooserTable::StaticClass()`，支持 `path_filter` 限制扫描范围 |
| `get_chooser_table_info` | `get_chooser_table_info` | 读一张 ChooserTable 的结构骨架：column 列表（struct_name + editor-only `RowValuesPropertyName`） + result 行列表（按 `FObjectChooserBase` 子类分 4 种 kind） |

**取舍**：本批是 structure-only 只读，**不解析每列每行的条件单元格值**（FloatRange / Bool 掩码 / Enum / GameplayTag 等）。每种 column 子类的 RowValues 数组类型不同，逐一支持工作量翻 10 倍，先验证骨架再说。

两个工具均已在 2026-04-15 当日完成编译、重启 UE 与 Claude Code 并在项目内 5 张 ChooserTable 上端到端验证通过。

## 修改文件清单（共 5 个）

### 1. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Public/Commands/UnrealMCPAnimationCommands.h`

private 区追加 2 个 handler 声明：

- `HandleListChooserTables`
- `HandleGetChooserTableInfo`

### 2. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp`

- include 追加 5 个：`StructUtils/InstancedStruct.h` / `Chooser.h` / `IObjectChooser.h` / `IChooserColumn.h` / `ObjectChooser_Asset.h`
- `HandleCommand` 追加 2 个 `if (CommandType == TEXT("..."))` 分发分支
- 文件末尾实现 2 个 handler。核心要点：
  - **`HandleListChooserTables`**：`FAssetRegistryModule::Get().Get().GetAssetsByClass(UChooserTable::StaticClass()->GetClassPathName(), ...)`。`path_filter` 非空时先调 `GetAssetsByPath(path_filter, recursive=true)` 收集全部资产再按类型筛——比 `GetAssetsByClass` 内部的 path 限制更可靠。返回 `{path, class}` 数组
  - **`HandleGetChooserTableInfo`**：`LoadObject<UChooserTable>` → 遍历 `ColumnsStructs`（每项是 `FInstancedStruct` 包 `FChooserColumnBase` 子类）输出 `struct_name`，`#if WITH_EDITOR` 下追加 `RowValuesPropertyName().ToString()`；行数据走 `ResultsStructs` 优先（WITH_EDITORONLY_DATA 带原作者顺序），如果 `IsCookedData()` 为真则 fallback 到 `CookedResults`，用 `TArrayView` 统一访问。每行按 `ScriptStruct` 分 4 种 kind：
    - `FAssetChooser` → `kind: "asset"`，输出 `Asset->GetPathName() + GetClass()->GetName()`
    - `FSoftAssetChooser` → `kind: "soft_asset"`，输出 `Asset.ToString()`
    - `FEvaluateChooser` / `FNestedChooser` → `kind: "evaluate_chooser"` / `"nested_chooser"`，输出 `Chooser->GetPathName()`
    - 其它 → `kind: "unknown"`（只给 struct_name）
    - 未初始化 instance → `kind: "empty"`
  - 错误统一走 `FUnrealMCPCommonUtils::CreateErrorResponse`，找不到资产或 cast 失败返回 `ChooserTable asset not found`

### 3. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp`

`ExecuteCommand` 里 Animation 命令分支的 if 条件从原来 6 个命令字符串扩成 8 个（追加 `list_chooser_tables` / `get_chooser_table_info`，仍用 `||` 连接）。命中后转发给同一个 `AnimationCommands->HandleCommand(...)`，构造函数无需改动。漏改这里新命令会落到 "Unknown command"。

### 4. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/UnrealMCP.Build.cs`

Editor 分支的 `PrivateDependencyModuleNames` 追加 `"Chooser"`——本批**唯一**的新模块依赖。注释写明 "For UChooserTable read-only inspection"。`StructUtils/InstancedStruct.h` 不需要新依赖（UE 5.5+ 已合进 `CoreUObject`）。

### 5. `Python/tools/animation_tools.py`

`register_animation_tools` 里追加 2 个 `@mcp.tool()` 函数，照前两批的模板走 `get_unreal_connection → send_command`，负责打包参数、透传返回结构、异常捕获回报错误。`list_chooser_tables` 的 `path_filter` 默认空字符串。

### 不需要改动的文件

- **`Python/unreal_mcp_server.py`**：第一批已经调过 `register_animation_tools(mcp)`，新工具在同一个 register 函数里被一并注册，server 入口完全不动

## 关键技术细节

- UE5.7 Chooser 插件源码：`C:\Program Files\Epic Games\UE_5.7\Engine\Plugins\Chooser\Source\Chooser\`
- `UChooserTable` 定义：`Internal/Chooser.h:21`，类继承 `UChooserSignature`
- `ColumnsStructs` (Chooser.h:191) = `TArray<FInstancedStruct>`，每项是 `FChooserColumnBase` 子类实例
- `ResultsStructs` (Chooser.h:153, **WITH_EDITORONLY_DATA**) = `TArray<FInstancedStruct>`，每项是 `FObjectChooserBase` 子类实例；row 数量就是这个数组的大小
- `CookedResults` (Chooser.h:187) = 运行时版本，`IsCookedData()` 用 `!CookedResults.IsEmpty()` 判断
- `Chooser.Build.cs:10` 有 Epic 留的 `PublicIncludePaths.Add(... "Internal")`，所以 `Internal/Chooser.h` 对外部模块可见，可直接 `#include "Chooser.h"`
- `FChooserColumnBase::RowValuesPropertyName()` 是 `WITH_EDITOR` virtual，BoolColumn 返回 `RowValuesWithAny`、FloatRangeColumn / EnumColumn 返回 `RowValues`——不同列子类成员名不同，这个 API 正好能暴露反射路径
- `FObjectChooserBase::GetReferencedObject()` (`IObjectChooser.h:157`) 是 `WITH_EDITOR` virtual，FAssetChooser/FSoftAssetChooser 已实现，但 FEvaluateChooser/FNestedChooser 没实现，**所以统一走 ScriptStruct switch 更稳**
- `FInstancedStruct` 在 `StructUtils/InstancedStruct.h`，UE 5.5+ 已合进 `CoreUObject` 模块，零额外依赖
- **Chooser 插件必须在 `.uproject` 中启用**，Game Animation Sample 默认启用，裸项目要手动开 Plugins > Chooser

## 验证摘要

| 工具调用 | 测试结果 |
|---|---|
| `list_chooser_tables()` | total_count=7 ✓，全部以 `CHT_` 命名（不含 "Chooser" 字样，**修正了之前用 glob 找不到的盲点**） |
| `get_chooser_table_info(CHT_TraversalMontages_Mover)` | 6 列（3 BoolColumn / 2 FloatRangeColumn / 1 OutputEnumColumn）4 行 全 nested_chooser ✓，BoolColumn 反射出 `RowValuesWithAny` ✓，FloatRange 是 `RowValues` ✓ |
| `get_chooser_table_info(CHT_PoseSearchDatabases_Relaxed)` | 4 EnumColumn / 8 行 全 nested_chooser，路径形如 `...:Stand Idles` ✓ |
| `get_chooser_table_info(CHT_CameraRig)` | 2 EnumColumn / 11 行 全 asset (`CameraRigAsset`) ✓ |
| `get_chooser_table_info(CHT_PoseSearchQuad)` | 1 MultiEnumColumn / 4 行 全 asset (`PoseSearchDatabase`) ✓ |
| `get_chooser_table_info(CHT_RotationOffsetCurve)` | 2 EnumColumn / 8 行 全 asset (`CurveFloat`) ✓ |
| `get_chooser_table_info(<AnimSequence path>)` | 错误路径返回 `{"success": false, "message": "ChooserTable asset not found"}` ✓ |

**附加观察**：
- 5 张表全部 `is_cooked_data=false` / `used_cooked_results=false`，证明走的是 `ResultsStructs` 编辑器分支，CookedResults fallback 路径未被触发（编辑器资产符合预期）
- 4 种 kind 中本次命中 `asset` 与 `nested_chooser` 两种，`evaluate_chooser` / `soft_asset` 项目里没出现，但实现位与 nested_chooser/asset 同位等长，无额外风险
- NestedChooser 的 path 形如 `<table_path>:Hurdles` `<table_path>:Mantles`——同一 ChooserTable 资产内的子表（hierarchy 嵌套），这个细节方便后续读懂 `CHT_TraversalMontages_Mover` 的内部组织

## 未决问题（推迟到下一期）

- **单元格值解析**（FloatRange / Bool 掩码 / Enum / GameplayTag / Object / Output...）：本期明确不做。若要做，从 column 的 `RowValuesPropertyName()` 拿到 FName，`StructUtils::FindPropertyByName` 拿到 RowValues UProperty，再按 column 子类分支解析每种数据形态——每种 column 单独写 setter
- **`evaluate_chooser` / `soft_asset` 两种 kind 的实测样本缺失**：项目内 7 张表都没出现，等遇到具体样本再补一次回归测试
- 是否需要 `find_chooser_tables_by_referenced_asset`（反查某个动画/资产被哪些 ChooserTable 引用）：等具体需求出现再说

## 2026-04-15 回归复测

代码无改动，对当日落地的两个工具做了一次回归运行，覆盖项目里新增的 Kunlun 系列表。

| 工具调用 | 结果 |
|---|---|
| `list_chooser_tables()` | total_count=**10**（比首次验证的 7 张多 3 张 Kunlun 表：`CHT_KL_RotationOffsetCurve` / `CHT_KL_CameraData` / `CHT_KL_MoverCharacterAnimations` / `CHT_KL_PoseSearchDatabases_Relaxed`，全部命中 `CHT_` 命名习惯） |
| `get_chooser_table_info(CHT_TraversalMontages_Mover)` | 6 列 / 4 行 nested_chooser，与首次结果完全一致（BoolColumn → `RowValuesWithAny`，FloatRangeColumn / OutputEnumColumn → `RowValues`） |
| `get_chooser_table_info(CHT_KL_MoverCharacterAnimations)` | **新表样本**：3 列（1 MultiEnumColumn + 2 EnumColumn）/ 8 行 全 nested_chooser，子表名为 `Stand Stopped` / `Stand Walks` / `Stand Runs` / `Stand Sprints` / `Crouch Stopped` / `Crouch Walks` / `In Air` / `Slide`，证明 MultiEnumColumn 列类型也能正常反射出 `RowValues` |
| `get_chooser_table_info(<AnimSequence path>)` | 错误路径仍返回 `ChooserTable asset not found` |

所有表 `is_cooked_data=false` / `used_cooked_results=false`，行为与首次验证完全一致。本次回归未发现回归问题，三批 8 个工具状态确认稳定。
