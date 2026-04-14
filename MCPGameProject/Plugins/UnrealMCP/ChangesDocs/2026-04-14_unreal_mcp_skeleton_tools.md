# unreal-mcp 扩展：Skeleton 数据读取工具（动画工具第二批）

**日期**：2026-04-14
**涉及仓库**：`C:\Workspace\git\unreal-mcp`
**前置变更**：[`2026-04-14_unreal_mcp_animation_tools.md`](./2026-04-14_unreal_mcp_animation_tools.md)（第一批 4 个动画工具）

## 修改目的

第一批 4 个动画工具能读单个 AnimSequence/AnimMontage 的内部数据，但仍缺两块能力：

1. **从 Skeleton 反查动画**：白盒动画工作流里经常需要先确定一根骨架，再列出所有挂在它上面的 AnimSequence/AnimMontage。靠 Asset Registry 扫一遍才能避免逐个 Load。
2. **读 Skeleton 的骨骼层级**：动画 retarget、IK 挂点检查、自动生成姿态都需要先知道骨架的骨骼名/index/parent 关系，包括虚拟骨骼。

新增工具：

| Python 工具 | 对应命令 | 作用 |
|---|---|---|
| `find_animations_for_skeleton` | `find_animations_for_skeleton` | Asset Registry 按 Skeleton tag 扫 AnimSequence/AnimMontage，支持 `include_montages` 与 `path_filter`，自动补齐 `.LeafName` |
| `get_skeleton_bone_hierarchy` | `get_skeleton_bone_hierarchy` | 读 `USkeleton::GetReferenceSkeleton().GetRawRefBoneInfo()`，输出每根骨骼的 index/name/parent_index/parent_name/children_indices；虚拟骨骼通过 `GetVirtualBones()` 单独输出 |

两个工具均已于当日完成编译、重启 Claude Code 并在 `SK_UEFN_Mannequin` 上实测通过，包含错误路径分支。

## 修改文件清单（共 4 个，全部为修改，无新增文件）

第一批已经把 Animation 命令类骨架建好，本批只需要在已有文件里追加。

### 1. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Public/Commands/UnrealMCPAnimationCommands.h`

private 区追加 2 个 handler 声明：

- `HandleFindAnimationsForSkeleton`
- `HandleGetSkeletonBoneHierarchy`

### 2. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp`

- include 追加：`AssetRegistry/AssetRegistryModule.h` / `AssetRegistry/IAssetRegistry.h` / `Animation/Skeleton.h`
- `HandleCommand` 追加 2 个 `if (CommandType == TEXT("..."))` 分发分支
- 文件末尾实现 2 个 handler。核心要点：
  - **`HandleFindAnimationsForSkeleton`**：通过 `FAssetRegistryModule::Get().Get().GetAssetsByClass` 扫 `UAnimSequence::StaticClass()->GetClassPathName()`，然后按需追加 `UAnimMontage`。逐个比对 `FAssetData::GetTagValueRef<FString>(USkeleton::GetSkeletonMemberName())`，匹配的写入返回列表。`skeleton_path` 入参自动补 `.LeafName` 后缀，避免用户传 `/Game/.../SK_X` 短形式时匹配失败。`path_filter` 直接传给 `GetAssetsByClass` 的 `PathName` 参数限制扫描范围
  - **`HandleGetSkeletonBoneHierarchy`**：`LoadObject<USkeleton>` 拿到资产后，先遍历 `RefSkel.GetRawRefBoneInfo()` 收集 index/name/parent_index/parent_name；再做一次 O(n) 遍历构建 `children_indices`（每个 bone 把自己 push 到父节点的 children 数组里）。虚拟骨骼走 `Skeleton->GetVirtualBones()`，输出 `virtual_name / source_bone / target_bone`，与原始骨骼分两个数组返回不混淆 index 空间
- 错误统一走 `FUnrealMCPCommonUtils::CreateErrorResponse`，路径找不到 / cast 失败都返回 `Skeleton asset not found`

### 3. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp`

`ExecuteCommand` 里 Animation 命令分支的 if 条件从原来 4 个命令字符串扩成 6 个（追加 `find_animations_for_skeleton` / `get_skeleton_bone_hierarchy`，仍用 `||` 连接）。命中后转发给同一个 `AnimationCommands->HandleCommand(...)`，构造函数无需改动。漏改这里新命令会落到 "Unknown command"。

### 4. `Python/tools/animation_tools.py`

`register_animation_tools` 里追加 2 个 `@mcp.tool()` 函数，照第一批的模板走 `get_unreal_connection → send_command`，负责打包参数、透传返回结构、异常捕获回报错误。`find_animations_for_skeleton` 的 `include_montages` 默认 `True`、`path_filter` 默认空字符串。

### 不需要改动的文件

- **`UnrealMCP.Build.cs`**：AssetRegistry 模块在第一批已经在依赖列表里（其它命令也用），Skeleton 头文件属于 Engine 模块也已挂上，本批零新增依赖
- **`Python/unreal_mcp_server.py`**：第一批已经调过 `register_animation_tools(mcp)`，新工具在同一个 register 函数里被一并注册，server 入口完全不动

## 验证摘要

测试目标资产：`/Game/ThirdParty/GameAnimationSample/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin.SK_UEFN_Mannequin`

| 工具调用 | 测试结果 |
|---|---|
| `get_skeleton_bone_hierarchy(SK_UEFN_Mannequin)` | bone_count=88 ✓，root→pelvis→spine_01..05→四肢/手指/IK 链完整层级，virtual_bone_count=0 ✓ |
| `get_skeleton_bone_hierarchy(<AnimSequence path>)` | 错误路径返回 `{"success": false, "message": "Skeleton asset not found"}` ✓ |
| `find_animations_for_skeleton(SK_UEFN_Mannequin, path_filter="/Game/.../Animations/Jump")` | sequence_count=122 / montage_count=0 ✓，`include_montages=true` |
| 同上但 `include_montages=false` | 返回 122 条 AnimSequence ✓（该子目录无 Montage，旗标行为正确） |

**附加收获**：本次验证一并确认 `SK_UEFN_Mannequin` 资产**就是 USkeleton**（不是 SkeletalMesh），项目里 "SK_" 前缀只是命名习惯不是引擎类型约定。该结论在前置文档"未决问题"里被打开，至此关闭。
