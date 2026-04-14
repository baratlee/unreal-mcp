# unreal-mcp 扩展：动画数据读取工具

**日期**：2026-04-14
**涉及仓库**：`C:\Workspace\git\unreal-mcp`

## 修改目的

为 unreal-mcp 插件新增 4 个 Editor-only 动画数据读取工具，让 Claude Code 能直接查询 `UAnimSequence` / `UAnimMontage` 的基础信息、通知、曲线与骨骼轨道。原插件只有 editor/blueprint/node/project/umg 五类工具，完全没有动画相关能力，白盒动画工作流无法落地。

新增工具：

| Python 工具 | 对应命令 | 作用 |
|---|---|---|
| `get_animation_info` | `get_animation_info` | 读取 play_length / num_frames / skeleton |
| `get_animation_notifies` | `get_animation_notifies` | 读取全部 notify 与 notifyState（含时间、轨道、类路径） |
| `get_animation_curve_names` | `get_animation_curve_names` | 读取所有 Float 曲线名 |
| `get_animation_bone_track_names` | `get_animation_bone_track_names` | 读取骨骼轨道；Montage 自动穿透 `SlotAnimTracks → AnimSegments` 聚合底层 AnimSequence 轨道 |

全部 4 个工具已于当日完成编译、重启 Claude Code 并在 UEFN Mannequin 动画资产上实测通过（包含空数据与非空数据两条分支，以及错误资产路径的 fail 分支）。

## 修改文件清单（共 7 个）

### 新增文件（3 个）

#### 1. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Public/Commands/UnrealMCPAnimationCommands.h`
新建 Animation 命令类的头文件。声明 `FUnrealMCPAnimationCommands` 类与 4 个 private handler（`HandleGetAnimationInfo` / `HandleGetAnimationNotifies` / `HandleGetAnimationCurveNames` / `HandleGetAnimationBoneTrackNames`），公开 `HandleCommand` 入口用于 Bridge 分发。

#### 2. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp`
4 个 handler 的 C++ 实现。核心要点：
- 使用 `UAnimationBlueprintLibrary` 读取 notifies / curves / bone tracks；`PlayLength` 走 `AnimSequenceBase->GetPlayLength()`
- **Montage 轨道穿透逻辑**：`UAnimMontage` 本身没有 DataModel，必须遍历 `Montage->SlotAnimTracks[i].AnimTrack.AnimSegments[j].GetAnimReference()` 递归取每段底层 AnimSequence 的轨道名，再用 `TSet<FName>` 去重聚合，同时输出每段的 segment 详情
- Notify 返回同时覆盖 event 型（`Notify`）与 state 型（`NotifyStateClass`），并带上 `is_state` / `is_branching_point` / `notify_class` 便于调用方区分
- 曲线当前只读 `ERawCurveTrackTypes::RCT_Float`
- 错误统一走 `FUnrealMCPCommonUtils::CreateErrorResponse`

#### 3. `Python/tools/animation_tools.py`
MCP 侧 Python 工具包装。`register_animation_tools(mcp)` 内定义 4 个 `@mcp.tool()` 函数，按 `get_animation_info` 的模板走 `get_unreal_connection → send_command`，负责把 `asset_path` 参数打包、返回结构透传、异常捕获后回报错误。

### 修改文件（4 个）

#### 4. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Public/UnrealMCPBridge.h`
- include 新头：`Commands/UnrealMCPAnimationCommands.h`
- private 区追加成员：`TSharedPtr<FUnrealMCPAnimationCommands> AnimationCommands;`

#### 5. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp`
在 `ExecuteCommand` 的字符串 if/else 分发里加一条 Animation 命令分支，条件覆盖全部 4 个命令字符串（用 `||` 连接），命中后转发给 `AnimationCommands->HandleCommand(...)`。构造函数里补 `AnimationCommands = MakeShared<...>()` 初始化。未加这一步，新命令会永远落到 "Unknown command"。

#### 6. `MCPGameProject/Plugins/UnrealMCP/Source/UnrealMCP/UnrealMCP.Build.cs`
`PrivateDependencyModuleNames` 的 Editor 分支追加 `"AnimationBlueprintLibrary"` 模块依赖。没有它 `UAnimationBlueprintLibrary::Get*` 全部无法链接。

#### 7. `Python/unreal_mcp_server.py`
- `from tools.animation_tools import register_animation_tools`
- 调用链末尾追加 `register_animation_tools(mcp)`，让 MCP 服务器启动时把新工具挂到 tool 列表里

## 验证摘要

| 资产 | 测试结果 |
|---|---|
| `M_Relaxed_Jump_Loop_Fall` (AnimSequence) | info ✓、bone_tracks 88 条 ✓；notifies/curves 为空（资产本身无数据） |
| `MM_Pistol_Fire_Montage` (AnimMontage) | bone_tracks 89 条 + 1 段 Arms slot segment，穿透逻辑 ✓ |
| `M_Neutral_Run_Loop_F` (AnimSequence) | notifies 9 条（1 条 PoseSearchBranchIn state + 8 条 FoleyEvent event）✓；curves 5 条（contact_l/r、movedata_speed、enable_warping、Phase）✓ |
| 错误路径（非动画资产） | 返回 `{"success": false, "message": "Animation asset not found"}` ✓ |
