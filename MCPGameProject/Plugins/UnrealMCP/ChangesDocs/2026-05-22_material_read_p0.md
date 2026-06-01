# 2026-05-22 · Material 读取 P0

为 UnrealMCP 增加 Material 系列资产的只读检视能力，弥补此前 `❌ 不能做 / 其它资产读取仍缺 · Material 资产读取` 项空缺。

本批仅 **P0 读取**：UMaterial / UMaterialInstance / UMaterialParameterCollection 三类资产的 settings + 参数清单。节点图（per-node graph traversal）放到 **P1**。详见 `Docs/Todo/UnrealMCP_Material_Extension_Task.md`。

---

## 新增工具（3 个）

| 工具 | 入参 | 用途 |
|---|---|---|
| `get_material_info` | `asset_path` | 读 UMaterial：domain/blend/shading model + post-process 子对象 + usage 开关组 + 全量参数清单（scalar/vector/texture/static switch 含默认值）+ parameter_counts |
| `get_material_instance_info` | `asset_path` | 读 UMaterialInstance(Constant/Dynamic)：parent_path / base_material_path + 三类参数 **覆写列表**（仅本实例存储的值，不与 parent 求并）+ FMaterialInstanceBasePropertyOverrides 全字段 |
| `get_material_parameter_collection_info` | `asset_path` | 读 UMaterialParameterCollection：ScalarParameters / VectorParameters 声明（name + default_value + GUID） |

## 关键实现细节

- **Enum→字符串**：用 `StaticEnum<TEnum>()->GetNameStringByValue(...)` + 自写 `StripEnumPrefix`，输出去掉 `MD_/BLEND_/BL_/MSM_` 前缀的可读名。
- **ShadingModels**：UE5 是 `FMaterialShadingModelField`（多模型 bitfield），用 `HasShadingModel(EMaterialShadingModel)` 逐个枚举展开，结果是字符串数组。
- **PostProcess 字段**：仅当 `material_domain == "PostProcess"` 才输出 `post_process` 子对象（blendable_location / blendable_priority / blendable_output_alpha），其它 domain 没有这块字段。
- **参数清单源**：用 `UMaterialInterface::GetAll{Scalar,Vector,Texture,StaticSwitch}ParameterInfo` 统一拿信息，再用 `Get*ParameterDefaultValue` 取默认值。这个接口同时适用于 UMaterial 和 UMaterialInstance，且会包含嵌套 Material Function 的参数。
- **Instance 覆写列表**：直接遍历 `MI->ScalarParameterValues` / `VectorParameterValues` / `TextureParameterValues` 三个 UPROPERTY 数组。这些数组就是 instance 显式覆写的部分（base 不会写进这里）。
- **base_property_overrides**：直接读 `MI->BasePropertyOverrides`（`FMaterialInstanceBasePropertyOverrides`），逐个 `bOverride_*` + 对应值字段输出。
- **base_material_path**：`MI->GetMaterial()` 自动沿 parent 链回溯到顶层 UMaterial。
- **路径回退**：与 `get_data_asset_info` 等保持一致 —— 给 `/Game/.../M_Foo` 会自动补 `.M_Foo` 后缀重试。
- **Static switch override**：P0 暂不返回 instance 的 static switch 覆写值（5.7 `StaticParameters_DEPRECATED` 已弃用，新位置在 `StaticParametersRuntime` + editor-only data，提取链路较绕）。Base material 上的 static switch declaration 仍正常返回。延到 P1。

## 涉及文件

- 新增 `Source/UnrealMCP/Public/Commands/UnrealMCPMaterialCommands.h`
- 新增 `Source/UnrealMCP/Private/Commands/UnrealMCPMaterialCommands.cpp`
- 改 `Source/UnrealMCP/Public/UnrealMCPBridge.h` — include + `MaterialCommands` 成员
- 改 `Source/UnrealMCP/Private/UnrealMCPBridge.cpp` — include + 构造/析构 + 命令分支
- Python 侧（在 `C:/Workspace/git/unreal-mcp/Python/`）：
  - 新增 `tools/material_tools.py`
  - 改 `unreal_mcp_server.py` — `register_material_tools(mcp)`
- Build.cs **未改**：UMaterial / UMaterialInstance / UMaterialParameterCollection 都在 Engine 模块，已有依赖足够。

## UE 5.7 字段依据

源码已登记到 `.claude/memory/project_ue_source_priority_list.md`（4 条 2026-05-22 条目）：
- `Material.h` — MaterialDomain / BlendMode / ShadingModels / TwoSided / bIsThinSurface / bUsedWith* / BlendableLocation / BlendableOutputAlpha / BlendablePriority
- `MaterialInstance.h` — Parent / ScalarParameterValues / VectorParameterValues / TextureParameterValues / BasePropertyOverrides
- `MaterialInterface.h` — GetAll*ParameterInfo / Get*ParameterDefaultValue ENGINE_API
- `MaterialParameterCollection.h` — ScalarParameters / VectorParameters + FCollectionScalarParameter / FCollectionVectorParameter 字段
- `MaterialParameters.h` — `FHashedMaterialParameterInfo = FMemoryImageMaterialParameterInfo` 别名 + 从 FMaterialParameterInfo 隐式转换构造

## 端到端验证步骤

1. 在 unreal-mcp 工程（`C:\Workspace\git\unreal-mcp\MCPGameProject\`）打开 VS，Development Editor | Win64 编译插件
2. 编译产物（Binaries/Win64/UnrealEditor-UnrealMCP.dll）自动落进 symlink 目标，PrjKunlun 下次启动即生效
3. 重启 PrjKunlun 编辑器
4. 用新工具检视样本：
   - `get_material_info("/Game/Examples/Leo/M_PP_DreamRipple")` — 验证 PostProcess 域识别 + post_process 子对象 + 参数清单完整性
   - `get_material_instance_info(...)` — 在某个 MI 上跑一次
   - `get_material_parameter_collection_info(...)` — 在某个 MPC 上跑一次

## 后续

- **P1 节点图**：`get_material_graph` —— 完整节点列表 + 连线 + 每节点参数值，类比 `get_anim_state_graph`。预计 1-1.5 天。
- **P2 高级**：`get_material_function_info`（Material Function 内部图）+ `list_materials`（按目录扫描，复用 list_data_assets 模式）。预计半天。
