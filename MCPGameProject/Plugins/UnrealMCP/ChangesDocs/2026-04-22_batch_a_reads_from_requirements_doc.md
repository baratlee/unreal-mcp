# unreal-mcp 扩展：依 `Docs/UnrealMCP_扩展需求清单.md` 的 Batch A 读类工具

**日期**：2026-04-22
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 背景

项目文档 `Docs/UnrealMCP_扩展需求清单.md` 共列 18 项扩展诉求。按"先做读类，写类单独批准"的原则，本次（Batch A）实现**纯读**的 6 项（清单编号 1 / 2 / 3 / 4 扩展 / 7 / 18）。

同一份清单中：
- Item 5 `get_blueprint_function_graph` 已存在，无需再加
- Item 6 IKRig Solver 细节留 Batch B
- 写类 Items 8 / 9 / 10 / 11 / 12 已被用户批准、留 Batch C 逐条实施
- Items 13 / 14 / 15 / 16 / 17 用户明确不做

## 本批次新增 / 扩展

### 1. `get_skeleton_reference_pose`（清单 Item 1，P0）

读 USkeleton 的 Bind Pose / Reference Pose。

**参数**：`skeleton_path`（必）、`space`（可选，`Local` / `Component`，默认 `Local`）。

**返回**：每根骨骼的 `{ index, name, parent_index, translation[x,y,z], rotation_quat[x,y,z,w], rotation_euler_pyr[pitch,yaw,roll], scale[x,y,z] }`。

**底层**：`FReferenceSkeleton::GetRawRefBonePose()`；Component space 自下往上累乘（父索引天然小于子索引）。

### 2. `get_skeletal_mesh_info`（清单 Item 2，P0）

读 USkeletalMesh 的尺寸 / 绑定 / LOD / 材质 / Socket 等。

**参数**：`mesh_path`（必）。

**返回**：`skeleton_path`、`default_physics_asset_path`、`post_process_anim_blueprint_class`、`bounding_box { min, max }`、`bounds_origin`、`bounds_extent`、`approximate_height_cm`（= extent.Z × 2）、`lod_count`、`material_slots[]`（含 slot_name + material_path）、`socket_names[]`（mesh-only，不含 skeleton socket）。

**注**：需求文档把 `post_process_anim_blueprint_path` 归到 `get_animation_blueprint_info` 的输出；此属性实际属于 `USkeletalMesh::PostProcessAnimBlueprint`，因此正确归属在本工具（`get_anim_blueprint_info` 的 docstring 已指向此处）。

### 3. `get_physics_asset_info`（清单 Item 3，P0）

读 UPhysicsAsset 的 Body / Constraint 结构。

**参数**：`asset_path`（必）。

**返回**：`preview_skeletal_mesh`、`body_count` / `bodies[]`（每 body：`bone_name`、`physics_type`（Default / Kinematic / Simulated）、`simulate_physics`、`mass_override`、`linear_damping`、`angular_damping`、`collision_response`、`num_sphere` / `num_box` / `num_capsule` / `num_convex` / `num_tapered_capsule`）、`constraint_count` / `constraints[]`（每 constraint：`constraint_name`、`bone1`、`bone2`、`linear_limit_size`、`angular_swing1_limit_deg`、`angular_swing2_limit_deg`、`angular_twist_limit_deg`）。

**底层**：遍历 `UPhysicsAsset::SkeletalBodySetups` 和 `ConstraintSetup`，读 `FBodyInstance::DefaultInstance` 和 `FConstraintInstance`。

### 4. 扩展 `get_anim_blueprint_info`（清单 Item 4 补完，P0）

在 2026-04-22 首版基础上增补两个字段：

- `state_machines[]`：AnimGraph 内顶层 `UAnimGraphNode_StateMachineBase` 节点；每条 `{ node_name, sub_graph_name, state_count }`。state_count 通过类名前缀 `AnimState*` 匹配，避免直接 include `AnimationStateMachineGraph` 私有头
- `used_animation_assets[]`：遍历所有 `FunctionGraphs` 的 `UAnimGraphNode_Base` 节点，调 `GetAnimationAsset()` 收集非 null 资产；**不覆盖** Chooser 运行时挑选 / 属性 binding 动态设定 / PoseSearchDatabase 内部动画（按 Q2 约定）

### 7. `get_asset_references`（清单 Item 7，P2）

AssetRegistry 反向依赖查询。

**参数**：`asset_path`（必，全对象路径或短名皆可）。

**返回**：`package_name`、`referencer_count`、`referencers[]`（每条 `{ package_name, asset_path, asset_class }`）。

**底层**：`IAssetRegistry::GetReferencers` + `GetAssetsByPackageName` 富化类型信息。

### 18. `list_animation_blueprints_for_skeleton`（清单 Item 18，P2）

对称于已有的 `find_animations_for_skeleton`，但过滤 UAnimBlueprint 资产、匹配 `TargetSkeleton` tag。

**参数**：`skeleton_path`（必）、`path_filter`（可选）。

**返回**：`total_count`、`anim_blueprints[]`（每条 `{ path, name, class, is_template }`）。

## 修改的文件

### C++ 端（UnrealMCP 插件）

| 文件 | 改动 |
|---|---|
| `Public/Commands/UnrealMCPAnimationCommands.h` | 声明 `HandleGetSkeletonReferencePose` / `HandleGetSkeletalMeshInfo` / `HandleGetPhysicsAssetInfo` / `HandleGetAssetReferences` / `HandleListAnimationBlueprintsForSkeleton` 共 5 条 |
| `Private/Commands/UnrealMCPAnimationCommands.cpp` | 新 include（Animation/AnimInstance.h、Engine/SkeletalMesh.h + SkeletalMeshSocket.h、PhysicsEngine/PhysicsAsset.h + SkeletalBodySetup.h + ConstraintInstance.h + PhysicsConstraintTemplate.h、AnimGraphNode_Base.h + AnimGraphNode_StateMachineBase.h、AnimationGraph.h、AssetRegistry/AssetData.h）；路由表 6 条；新匿名命名空间 `TransformToJson`；5 个新 handler 实现；`HandleGetAnimBlueprintInfo` 扩展出 `state_machines` / `used_animation_assets` 两字段 |
| `Private/UnrealMCPBridge.cpp` | Animation 路由段追加 5 条（`list_animation_blueprints_for_skeleton` / `get_skeleton_reference_pose` / `get_skeletal_mesh_info` / `get_physics_asset_info` / `get_asset_references`） |

### Python 端（unreal-mcp server）

| 文件 | 改动 |
|---|---|
| `Python/tools/animation_tools.py` | 注册 5 个新 MCP tool；`get_anim_blueprint_info` docstring 增补 `state_machines` / `used_animation_assets` 说明 + `post_process_anim_blueprint` 归属提示 |

## 验证顺序（编译后跑）

1. `get_skeleton_reference_pose` → 对 `SK_UEFN_Mannequin` 取 Local + Component 两份，bone_count 应一致
2. `get_skeletal_mesh_info` → 对 `SKM_UEFN_Mannequin`（如有）或其他 SKM 资产，确认 skeleton_path 正确
3. `get_physics_asset_info` → 选个有 PhysicsAsset 的角色 SKM 的默认 PA 资产
4. `get_anim_blueprint_info` 重跑 `SandboxCharacter_Mover_ABP`，确认 `state_machines` 出现（GASP 的 State Machine (Experimental)）、`used_animation_assets` 非空
5. `list_animation_blueprints_for_skeleton` 对 `SK_UEFN_Mannequin` → 至少能看到 `SandboxCharacter_Mover_ABP`
6. `get_asset_references` 对某个动画资产 → 返回引用它的 PSD / AnimBP 等

## 待办 / 跟进

- Batch B：Item 6 扩展 `get_ik_rig_info` 返回 solver 完整字段
- Batch C（写类已批准）：Items 8 / 9 / 10 / 11 / 12 逐条实施（每条建议一次编译周期，避免批次性风险）
