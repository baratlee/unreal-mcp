# unreal-mcp 扩展：get_anim_blueprint_info 新工具

**日期**：2026-04-22
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 修改目的

`get_blueprint_info` 返回的是 AnimInstance generated class 视角（变量 / 事件图 / 函数图），**读不到 UAnimBlueprint 资产级属性**。典型场景：

- 想知道某个 AnimBP 的 `TargetSkeleton` 是哪个 —— 看不见
- 想确认 AnimBP 的预览 Mesh / 预览 AnimBP 设置 —— 看不见
- 想判断 AnimBP 是 template 还是普通 BP —— 看不见
- 想追 AnimBP 的继承链（parent / root AnimBP）—— 看不见

用 `get_blueprint_cdo_properties` 也不行 —— CDO 是 AnimInstance 子类的默认对象，UAnimBlueprint 资产自身的 UPROPERTY 不在 CDO 上。

## 新增功能

### `get_anim_blueprint_info`

专门针对 UAnimBlueprint 资产的头部视图，读取 `UAnimBlueprint` 直接暴露的字段（见 `Engine/Source/Runtime/Engine/Classes/Animation/AnimBlueprint.h`）。

**参数**：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `blueprint_path` | string | 是 | AnimBP 资产路径（`/Game/...` 或短名均可） |

**返回字段**：

| 字段 | 类型 | 说明 |
|---|---|---|
| `name` / `path` | string | AnimBP 名称 / 全路径 |
| `parent_class` | string | AnimInstance 基类名 |
| `blueprint_type` | string | Normal / Interface / FunctionLibrary / MacroLibrary |
| `is_template` | bool | 模板 AnimBP（无 TargetSkeleton）|
| `target_skeleton` | {name,path} 或 null | 模板时为 null |
| `preview_skeletal_mesh` | {name,path} 或 null | Persona 预览 Mesh；仅读显式设置，不回退到 Skeleton 默认 |
| `preview_animation_blueprint` | {name,path} 或 null | 预览用 overlay/linked AnimBP |
| `preview_animation_blueprint_application_method` | string | `LinkedLayers` 或 `LinkedAnimGraph` |
| `preview_animation_blueprint_tag` | string | LinkedAnimGraph 方式下的 tag |
| `default_binding_class` | string 或 null | 新增节点默认使用的 `UAnimGraphNodeBinding` 子类名 |
| `use_multi_threaded_animation_update` | bool | 工作线程更新动画（还需项目设置配合） |
| `warn_about_blueprint_usage` | bool | 编译器在 AnimGraph 调用 BP 时产生警告 |
| `enable_linked_anim_layer_instance_sharing` | bool | LinkedLayer 实例共享 |
| `anim_groups` | [string, ...] | 所有同步组名字 |
| `num_parent_asset_overrides` | int | 父类节点资产 override 数量 |
| `num_pose_watches` | int | 激活的 pose watch 数 |
| `parent_anim_blueprint` | {name,path} 或 null | 继承自哪个 AnimBP（非原生类时才非 null）|
| `root_anim_blueprint` | {name,path} 或 null | 链上最根的 AnimBP（自己就是根时为 null）|

**返回示例**（对 `SandboxCharacter_Mover_ABP`）：
```json
{
  "name": "SandboxCharacter_Mover_ABP",
  "path": "/Game/ThirdParty/GameAnimationSample/Blueprints/SandboxCharacter_Mover_ABP.SandboxCharacter_Mover_ABP",
  "parent_class": "AnimInstance",
  "blueprint_type": "Normal",
  "is_template": false,
  "target_skeleton": {
    "name": "SK_UEFN_Mannequin",
    "path": "/Game/.../SK_UEFN_Mannequin.SK_UEFN_Mannequin"
  },
  "preview_skeletal_mesh": {...},
  "preview_animation_blueprint": null,
  "preview_animation_blueprint_application_method": "LinkedLayers",
  "preview_animation_blueprint_tag": "",
  "default_binding_class": "AnimGraphNodeBinding_Base",
  "use_multi_threaded_animation_update": true,
  "warn_about_blueprint_usage": false,
  "enable_linked_anim_layer_instance_sharing": false,
  "anim_groups": [],
  "num_parent_asset_overrides": 0,
  "num_pose_watches": 0,
  "parent_anim_blueprint": null,
  "root_anim_blueprint": null
}
```

## 与既有工具的分工

| 工具 | 视角 |
|---|---|
| `get_blueprint_info` | AnimInstance generated class（变量 / 事件图 / 函数图 / 组件 / 继承组件） |
| `get_blueprint_cdo_properties` | AnimInstance CDO 上 Edit UPROPERTY（C++ 父类 EditDefaultsOnly + Class Defaults 面板改动） |
| **`get_anim_blueprint_info`**（新） | UAnimBlueprint 资产自身（TargetSkeleton / 预览设置 / 继承链） |

三者互补，不重叠。典型工作流：

1. 先 `get_anim_blueprint_info` 拿 TargetSkeleton → 再 `find_animations_for_skeleton` 找动画 / 未来的 `find_skeletal_meshes_for_skeleton` 找 SKM
2. 查 AnimGraph 行为仍走 `get_blueprint_info` + `get_blueprint_function_graph`

## 修改的文件

### C++ 端（UnrealMCP 插件）

| 文件 | 改动 |
|---|---|
| `Public/Commands/UnrealMCPAnimationCommands.h` | 声明 `HandleGetAnimBlueprintInfo` |
| `Private/Commands/UnrealMCPAnimationCommands.cpp` | 添加 `#include "Animation/AnimBlueprint.h"` / `#include "Engine/SkeletalMesh.h"`；新辅助匿名命名空间（`MakeAssetRefJson` + `PreviewAppMethodToString`）；实现 `HandleGetAnimBlueprintInfo`；路由表追加 |
| `Private/UnrealMCPBridge.cpp` | Animation 路由段追加 `get_anim_blueprint_info` |

### Python 端（unreal-mcp server）

| 文件 | 改动 |
|---|---|
| `Python/tools/animation_tools.py` | 注册 `get_anim_blueprint_info` MCP tool |

## 依赖与编辑器约束

- `UAnimBlueprint::GetPreviewAnimationBlueprint()` / `GetPreviewAnimationBlueprintApplicationMethod()` / `GetPreviewAnimationBlueprintTag()` 为非 WITH_EDITOR 声明，可直接调用
- `GetParentAnimBlueprint` / `FindRootAnimBlueprint` 位于 `#if WITH_EDITOR` 块内，实现已用该宏保护（本插件仅 Editor 构建）
- `DefaultBindingClass` / `PreviewSkeletalMesh` / `PreviewAnimationBlueprint` 属性本身位于 `WITH_EDITORONLY_DATA` 块；前者通过 `GetDefaultBindingClass()` 访问，后两者通过公开 getter 访问

## 待办 / 跟进

- 配套 `find_skeletal_meshes_for_skeleton` 工具尚未实现（AssetRegistry 查 SKM 按 `Skeleton` tag 过滤），下次扩展时加上
