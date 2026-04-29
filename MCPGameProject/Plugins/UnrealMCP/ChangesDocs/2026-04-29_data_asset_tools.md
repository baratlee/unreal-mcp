# unreal-mcp 扩展：DataAsset 通用读取 / 写入 / 列举工具（3 条）+ SetObjectProperty ImportText 通用回退

**日期**：2026-04-29
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件 C++ 侧）
- `C:\Workspace\git\unreal-mcp`（Python MCP server 侧）

## 概述

新增 DataAsset（及任意 UObject 资产）的通用 MCP 操作能力：通过 UE 反射读取全部可编辑属性、设置单个属性值、通过 AssetRegistry 枚举资产。

同时为 `SetObjectProperty` 公共工具函数增加 `ImportText_Direct` 通用回退，使所有已有的属性写入工具（`set_blueprint_property`、`set_actor_property`、`set_component_property` 等）也获得对 struct / FName / FText / double / SoftObjectReference 等复杂��型的写入能力。

## 背景

此前 MCP 对特定资产类型（Chooser Table、PoseSearch、StateTree 等）有专门的操作工具，但对通用 `UDataAsset` / `UPrimaryDataAsset` 子类实例没有任何读写能力。用户自定义的 DataAsset（如 `UGC_CameraDataAsset`）只能在编辑器中手动操作。

## 文件变更

### 新增

| 文件 | 说明 |
|---|---|
| `Public/Commands/UnrealMCPDataAssetCommands.h` | DataAsset 命令处理类声明 |
| `Private/Commands/UnrealMCPDataAssetCommands.cpp` | 3 个 handler 实现 + 属性序列化辅助函数（约 320 行） |
| `Python: tools/data_asset_tools.py` | 3 个 Python MCP 工具注册 |

### 修改

| 文件 | 变更 |
|---|---|
| `UnrealMCPBridge.h` | 新增 `#include` + `TSharedPtr<FUnrealMCPDataAssetCommands> DataAssetCommands` 成员 |
| `UnrealMCPBridge.cpp` | 构造 / 析构 / `ExecuteCommand` 路由增加 3 条 DataAsset 命令 |
| `UnrealMCPCommonUtils.cpp` | `SetObjectProperty` 末尾新增 `ImportText_Direct` 通用回退分支（+15 行） |
| `Python: unreal_mcp_server.py` | `import` + `register_data_asset_tools(mcp)` |

## 新增 3 条工具

### `get_data_asset_info`

**参数**：`asset_path`（必），`max_depth`（可选，1-4，默认 2），`category_filter`（可选��。

**行为**：
1. `LoadObject<UObject>(nullptr, *AssetPath)` 加载资产，找不到时自动尝试追加 `.LeafName` 后缀
2. `TFieldIterator<FProperty>` 遍历类层级（IncludeSuper），过滤 `CPF_Edit` 且非 Transient 属性
3. 按属性类型分别序列化：
   - `FSoftObjectProperty` → SoftObjectReference 路径 + object_class
   - `FObjectProperty` → 硬引用路径 + object_class，嵌套对象在 depth 内递归
   - `FArrayProperty` → 对象数组展开元素（含递归），非对象数组 `ExportTextItem_Direct`
   - 其他 → `ExportTextItem_Direct` 导出文本 + 精确类型名（bool/int/float/double/string/FName/FText/enum/struct 名）
4. 支持 `category_filter` 按属性 Category 元数据子串过滤

**返回**：`asset_path / class_name / parent_class / properties[]`

### `set_data_asset_property`

**参数**：`asset_path`（必），`property_name`（必），`property_value`（必）。

**行为**：
1. 加载资产（同上回退逻辑）
2. 委托 `FUnrealMCPCommonUtils::SetObjectProperty()` 设置属性值
3. 成功后调用 `Asset->MarkPackageDirty()` 标脏（编辑器中显示 *，不自动保存）

**支持的属性类型**（通过 CommonUtils）：
- 原生类型：bool / int / float / string
- 枚举：FByteProperty（TEnumAsByte）/ FEnumProperty，支持数值和名称字符串
- **新增 ImportText 回退**：struct / FName / FText / double / FSoftObjectPtr / FObjectProperty 等所有支持 T3D 文本格式的类型

**返回**：`asset_path / property / success`

### `list_data_assets`

**参数**：`class_name`（可选），`search_path`（可选）。

**行为**：
1. `class_name` 为空时默认按 `UDataAsset` 过滤（含所有子类，`bRecursiveClasses = true`）
2. `class_name` 非空时通过 `FindFirstObject<UClass>` 查找，找不到时尝试 `/Script/Engine.ClassName`
3. `search_path` 非空时限制包路径（`bRecursivePaths = true`）
4. 通过 `IAssetRegistry::GetAssets(Filter)` 枚举

**返回**：`count / assets[]{asset_path, asset_name, class_name, package_path}`

## SetObjectProperty ImportText 通用回退

在 `UnrealMCPCommonUtils::SetObjectProperty` 原有的 bool / int / float / string / byte / enum 分支之后、错误返回之前，新增：

```cpp
if (Value->Type == EJson::String)
{
    const TCHAR* Result = Property->ImportText_Direct(Buffer, PropertyAddr, Object, PPF_None);
    ...
}
```

这使得 `get_data_asset_info` 读出的 value 字符串（T3D 格式）可以直接原样回填给 `set_data_asset_property`，因为读写使用同一套序列化格式。

**受益范围**：所有调用 `SetObjectProperty` 的已有工具——`set_blueprint_property`、`set_actor_property`、`set_component_property`、`set_state_tree_node_property` 现在都能写入 struct 类型属性。

## 典型工作流

```
1. list_data_assets(search_path="/Game/Data")
   → 枚举项目中所有 DataAsset
2. list_data_assets(class_name="UGC_CameraDataAsset", search_path="/Game/Examples")
   → 按类名 + 路径精确过滤
3. get_data_asset_info(asset_path="/Game/Examples/Leo/NewDataAsset")
   → 读取全部属性，含 struct 值的 T3D 文本
4. set_data_asset_property(
       asset_path="/Game/Examples/Leo/NewDataAsset",
       property_name="FOVSettings",
       property_value="(MinFOV=75.0,MaxFOV=90.0,...)")
   → 写入 struct 属性，资产标脏
5. get_data_asset_info(..., category_filter="FOV")
   → 回读确认修改生效
```

## 验证记录

2026-04-29 全部 3 条工具经 MCP 实测通过：

| 工具 | 测试内容 | 结果 |
|---|---|---|
| `list_data_assets` | 无参枚举 /Game 下全部资产 | 返回 292 条，格式正确 |
| `list_data_assets` | `search_path="/Game/Leo"` 过滤 | 返回 14 条 PoseSearch 相关资产 |
| `get_data_asset_info` | 读取 `UGC_CameraDataAsset_C`（含 struct/TMap/TArray） | 全部属性正确序列化 |
| `set_data_asset_property` | struct 属性 `FOVSettings` 写入（MinFOV 90→75） | 写入成功，回读确认 |

## 已知限制

1. **属性写入类型覆盖**：`SetObjectProperty` 对 TArray / TMap / TSet 的整体赋值尚未支持，只能写入标量和 struct 类型属性
2. **资产不自动保存**：写入后仅 `MarkPackageDirty()`，需要用户在编辑器中手动保存或后续实现 `save_asset` 工具
3. **class_name 查找**：`list_data_assets` 的 class_name 参数需要传 UClass 短名（如 `DataAsset`），不支持蓝图生成类的 `_C` ���缀查找
