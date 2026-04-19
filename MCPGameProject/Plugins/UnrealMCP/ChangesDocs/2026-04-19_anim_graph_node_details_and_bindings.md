# unreal-mcp 扩展：get_blueprint_function_graph 暴露 AnimGraph 节点的 Details 属性 + PropertyBindings

**日期**：2026-04-19
**涉及仓库**：
- `D:\Workspace\GitHub\LeoPractice`（UnrealMCP 插件）
- `D:\Workspace\GitHub\unreal-mcp`（Python MCP server）

## 修改目的

`get_blueprint_function_graph` 之前只能返回 AnimGraph 节点的 pin 信息（name/type/links/default_value），看不到两类关键数据：

1. **节点的 Details 面板属性**（EditAnywhere UPROPERTY，非 Pin）。例：Two Way Blend 的 `AlphaInputType`、`bAlphaBoolEnabled`、`bAlwaysUpdateChildren`；Blend Stack 的 `MaxActiveBlends`、`SampleRate`；Pose History 的 `bGenerateTrajectory`、`TrajectorySpeedMultiplier` 等。这些全在 Details 面板可见但不在图上。
2. **AnimGraph 节点的 Property Bindings**。Details 面板里每个属性旁有个"binding"小按钮，点开可直接把属性绑到 AnimInstance 的变量或函数上（替代画布拖 Get 节点）。在 UE5.x 里，这些 binding 已从 `UAnimGraphNode_Base::PropertyBindings_DEPRECATED` 搬到 Instanced 子对象 `UAnimGraphNode_Base::Binding` 的 `PropertyBindings` TMap。

没有这两块数据，AnimGraph 的状态根本无法从 MCP 侧完整核对：Alpha Input Type 是 Bool 还是 Float 看不到；某个 pin 是靠连线还是靠 binding 提供数据看不到。直接后果是 Quad ABP Phase 2 搭建时无法自动化核对，只能口头确认。

## 新字段（仅在 AnimGraph 节点上出现）

每个 `UAnimGraphNode_*` 的 JSON 对象在原有 `guid / class / title / pins / ...` 基础上追加：

| 字段 | 类型 | 语义 |
|---|---|---|
| `anim_node_struct` | string | 内嵌 FAnimNode 结构体名，如 `"AnimNode_TwoWayBlend"` |
| `anim_node_properties` | object | 该结构体所有 EditAnywhere UPROPERTY 的当前值（用 `FJsonObjectConverter::UStructToJsonObject` + `CheckFlags=CPF_Edit` 过滤，只保留用户在 Details 面板能改的那批） |
| `property_bindings` | array | `Binding->PropertyBindings` TMap 的展开；每项 `{ property_name, detail: { PathAsText, PropertyPath[], Type, bIsBound, bIsPromotion, PinType, ... } }` |

约束：

- `pin_payload_mode="names_only"` 时这两个字段都被跳过（NamesOnly 的目标是极简）
- 非 AnimGraph 节点（EventGraph 里的 K2Node_*）不会出现这些字段 —— 代码通过 `Cast<UAnimGraphNode_Base>` 严格过滤
- 如果节点的 Binding 子对象不存在或 PropertyBindings map 为空，`property_bindings` 仍输出为空数组（不是字段缺失），方便客户端断言

## 修改文件清单（共 2 个）

### 1. `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp`

- 顶部新增两个 include：
  - `#include "AnimGraphNode_Base.h"` — for `UAnimGraphNode_Base` + `FAnimGraphNodePropertyBinding` (public 头，AnimGraph 模块，Build.cs 已有依赖)
  - `#include "JsonObjectConverter.h"` — for `FJsonObjectConverter::UStructToJsonObject` (JsonUtilities 模块，Build.cs 已有依赖)
- 匿名 namespace 内新增 helper `SerializeAnimGraphNodeExtras(UAnimGraphNode_Base* AnimNode, TSharedPtr<FJsonObject> NodeObj, EPinPayloadMode Mode)`：
  - 反射遍历 AnimNode 的 `TFieldIterator<FStructProperty>`，找出名字以 `AnimNode_` 开头的 UScriptStruct 成员（一个就退出），调用 `FJsonObjectConverter::UStructToJsonObject` 输出 `anim_node_properties`，额外写一个 `anim_node_struct` 字段标识是哪个 FAnimNode 子类
  - 反射拿 `Binding` 成员（`FindPropertyByName(TEXT("Binding"))` → `CastField<FObjectProperty>`），解引用到 UObject 实例后再反射找 `PropertyBindings` 的 `FMapProperty`，用 `FScriptMapHelper::GetMaxIndex()` + `IsValidIndex()` 遍历 map（版本稳定写法）
  - map 的 key 是 FName，直接 `reinterpret_cast` 读；value 是 `FAnimGraphNodePropertyBinding` 结构体，用 `FAnimGraphNodePropertyBinding::StaticStruct()` + `UStructToJsonObject` 展开（CheckFlags=0 因为结构体字段是裸 UPROPERTY() 不带 Edit）
- `SerializeGraphNodeToJson` 末尾（`SetArrayField("pins", ...)` 之后、`return` 之前）加一个 `Cast<UAnimGraphNode_Base>` 分支，命中就调 helper，不命中什么也不做

### 2. `D:/Workspace/GitHub/unreal-mcp/Python/tools/blueprint_tools.py`

- `get_blueprint_function_graph` 的 docstring 在 Returns 段追加三个新字段的描述（`anim_node_struct` / `anim_node_properties` / `property_bindings`），说明只在 AnimGraph 节点上出现、`names_only` 模式下省略
- 函数签名和调用参数不变

## 关键技术细节

### 为什么用反射，不用静态 cast

每个 `UAnimGraphNode_*` 子类的内嵌 FAnimNode 成员**命名不统一**：多数叫 `Node`（ApplyAdditive / BlendListByInt / CCDIK），少数叫 `BlendNode`（TwoWayBlend、BlendBoneByChannel）。如果硬编码"反射查找名为 Node 的字段"就会漏掉 TwoWayBlend —— 这个恰好是 Quad ABP 用到的节点，等于没做。改用"查找类型名以 `AnimNode_` 开头的 FStructProperty"是稳定写法。

### 为什么 PropertyBindings 要走反射

`UAnimGraphNodeBinding_Base::PropertyBindings` 所在的头文件 `AnimGraphNodeBinding_Base.h` 在 `AnimGraph/Private/` 目录，UE 标记为 editor-private。虽然通过 `PrivateIncludePathModuleNames` 也能 include，但那会让 UnrealMCP 插件和 AnimGraph 编辑器内部实现紧耦合，UE 升级时极易破。改用 `FindPropertyByName + CastField<FMapProperty>` 反射访问，对 UE 版本解耦。

`FAnimGraphNodePropertyBinding` 的结构体本身在 `AnimGraphNode_Base.h`（public 头），include 是稳定的。

### CPF_Edit + CPF_Transient 的组合

`FJsonObjectConverter::UStructToJsonObject` 的第四个参数 `CheckFlags` 是"只包含带这些 flag 的属性"：

- **FAnimNode_* 的 inner struct**：用 `CheckFlags=CPF_Edit`，只保留 `EditAnywhere/EditDefaultsOnly/EditInstanceOnly` 属性。这精确匹配"用户在 Details 面板能看到的"范围，不会 dump 出 `bAIsRelevant` / `InternalBlendAlpha` 这类 protected 的运行时状态（它们根本没有 UPROPERTY flag，本来就不会被反射）
- **FAnimGraphNodePropertyBinding**：用 `CheckFlags=0`（不过滤），因为结构体里的字段全部是裸 `UPROPERTY()`（`PropertyName`、`PathAsText`、`PropertyPath[]`、`Type`、`bIsBound` 等），不带 Edit flag。过滤了就全空了

`SkipFlags=CPF_Transient | CPF_DuplicateTransient` 两者都加，统一跳过 transient（如 UE 内部 compile cache、debug scratch）。

### 性能 / 响应体积影响

- `anim_node_properties` 每个节点 JSON 膨胀量：Two Way Blend 多 ~8 字段、BlendStack 多 ~12 字段、Pose History 多 ~15 字段。对于 Quad ABP 5-10 节点量级基本无感；对于 GASP 23 节点 ABP 预计增加 < 20 KB，仍在 MCP socket 响应窗口内
- `property_bindings` 通常每个节点 0-5 条 binding（大多数节点 0 条），每条约 300 字节。膨胀量更小
- `names_only` 模式下两者都跳过，和扩展前响应体积完全一致

### 响应格式兼容性

- 对老客户端零破坏：新字段在 JSON 里追加，旧字段（guid/class/title/pins 等）结构完全不变
- 对非 AnimGraph 节点零影响：K2Node_* 不会进 cast 分支，返回 JSON 和扩展前完全一致

## 测试用例（7 个场景）

| # | 场景 | 预期结果 | 目的 |
|---|---|---|---|
| 1 | `get_blueprint_function_graph("/Game/Leo/Blueprints/Character/Quad/Quad_Mover_ABP", "AnimGraph")` | Two Way Blend 节点带 `anim_node_struct="AnimNode_TwoWayBlend"`，`anim_node_properties.AlphaInputType="Bool"`、`.bAlphaBoolEnabled=true`、`.bAlwaysUpdateChildren=true`；Pose History 节点的 `property_bindings` 数组包含 `{ property_name: "TransformTrajectory", detail: { PathAsText: "Transform Trajectory", ... } }` | 主场景：验证 Quad ABP 的 Details 属性 + Property Binding 两条盲区都能读到 |
| 2 | `get_blueprint_function_graph("/Game/ThirdParty/GameAnimationSample/.../SandboxCharacter_Mover_ABP", "AnimGraph", "summary")` | 返回成功、不超时、每个 AnimGraph 节点都带 `anim_node_properties` 和 `property_bindings` 字段（后者可能为空数组） | 回归：更复杂的 ABP（23 节点）不会因新字段体积爆炸 |
| 3 | `get_blueprint_function_graph("/Game/.../Quad_Mover_ABP", "AnimGraph", "names_only")` | 所有节点都**没有** `anim_node_properties` / `property_bindings` / `anim_node_struct` 字段 | 验证 NamesOnly 模式完全跳过新字段 |
| 4 | 用 Blend Two Way 手动连线模式（没做 binding） | Two Way Blend 节点的 `property_bindings` 为 `[]`（空数组），不是字段缺失 | 验证无 binding 场景不崩，返回稳定结构 |
| 5 | `get_blueprint_function_graph(任意 BP, "EventGraph")` | EventGraph 里的 K2Node_* 节点**没有** `anim_node_properties` / `property_bindings` 字段，输出结构与扩展前完全相同 | 验证非 AnimGraph 节点零影响 |
| 6 | `get_blueprint_function_graph("/Game/.../Quad_Mover_ABP", "NonExistentGraph")` | 返回 error，信息含现有 graph 名列表 | 验证错误分支不受影响（原有行为） |
| 7 | 故意在 ABP 里创建一个 binding，绑到已删除变量（mock 方式：保存后删变量） | `property_bindings` 返回该项，`detail.bIsBound=false` 或对应字段反映"已失效"，不 crash | 边界：残留 binding 不崩 |

### 测试执行步骤

1. `tools\build_unrealmcp.bat` 编译插件
2. 用户重启 UE 编辑器（加载新插件 DLL）
3. 确保 unreal-mcp Python server 是最新版本（docstring 更新已同步到 `blueprint_tools.py`）
4. 按上表顺序调用，每一项人工核对预期结果

测试 #1 是主验证，成功即证明方案 A 落地。#2-#7 依次回归。

## 关联

- 关联 memory：`project_unreal_mcp_method_a.md`（方案 A 本次实施）、`project_quad_abp_implementation.md`（下游依赖：Quad ABP Phase 2 搭状态机前需要用新字段核对 AnimGraph）
- 未实施的姐妹方案 B（AnimGraph 写入工具：`add_anim_graph_node` / `connect_pins` / `set_anim_graph_node_property` / `add_state` / `add_transition`）暂缓，等 Quad ABP 跑通后再决定
