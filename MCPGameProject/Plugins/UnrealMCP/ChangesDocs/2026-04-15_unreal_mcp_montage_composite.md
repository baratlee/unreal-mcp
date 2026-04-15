# unreal-mcp 扩展：Montage 组合结构只读工具（动画工具第四批）

**日期**：2026-04-15
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

**前置变更**：
- [`2026-04-14_unreal_mcp_animation_tools.md`](./2026-04-14_unreal_mcp_animation_tools.md)（第一批 4 个动画工具）
- [`2026-04-14_unreal_mcp_skeleton_tools.md`](./2026-04-14_unreal_mcp_skeleton_tools.md)（第二批 Skeleton 工具）
- [`2026-04-15_unreal_mcp_chooser_tools.md`](./2026-04-15_unreal_mcp_chooser_tools.md)（第三批 ChooserTable 只读工具）

## 修改目的

第一批的 `get_animation_bone_track_names` 已经穿透 `SlotAnimTracks` 聚合骨骼轨道，但只输出聚合后的 track 列表，丢失了 Montage 自身的时间轴结构（Sections / 各 Slot 的 segment 时间排布）。要理解一段 Montage 是怎么被 section 切成多个分支、每个 segment 起止时间、播放速率、循环次数，必须再加一个工具。这是动画工具未决问题清单里"是否要加 Montage composite 信息单独 command"的明确落地。

新增工具：

| Python 工具 | 对应命令 | 作用 |
|---|---|---|
| `get_montage_composite_info` | `get_montage_composite_info` | 读 `UAnimMontage` 的 composite 结构骨架：sections / SlotAnimTracks / AnimSegments / blend in/out / notify tracks |

**取舍**：
- 仅接受 `UAnimMontage`（喂 AnimSequence 报错）。
- 不输出 BranchingPoints 详情（branching point 的 trigger 已经由 `get_animation_notifies` 通过 `is_branching_point` 字段覆盖）。
- 不输出 NotifyTracks 上的 notify 列表（同样由 `get_animation_notifies` 提供）；只输出 notify track 名清单（提供 track 名 → notify 的反查依据）。
- 不解析 BlendProfile / TimeStretchCurve / SyncSlotName 等高级字段——一期只覆盖最常用的时间轴结构与 blend 时间。

## 修改文件清单（共 4 个）

### 1. `Plugins/UnrealMCP/Source/UnrealMCP/Public/Commands/UnrealMCPAnimationCommands.h`

private 区追加 1 个 handler 声明：

- `HandleGetMontageCompositeInfo`

### 2. `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPAnimationCommands.cpp`

- include **无追加**——`Animation/AnimMontage.h` / `Animation/AnimCompositeBase.h` / `Animation/AnimTypes.h` 已经在第一批引入
- `HandleCommand` 追加 1 个 `if (CommandType == TEXT("get_montage_composite_info"))` 分发分支
- 文件末尾实现 1 个 handler。核心要点：
  - `LoadObject<UAnimMontage>` 而不是 `UAnimSequenceBase`——直接拒绝非 Montage 路径，错误返回 `"AnimMontage asset not found: <path>"`
  - **顶层字段**：`play_length / is_loop（继承自 UAnimSequenceBase::bLoop）/ blend_in_time（BlendIn.GetBlendTime()）/ blend_out_time / blend_out_trigger_time / enable_auto_blend_out / skeleton`
  - **Sections**：遍历 `Montage->CompositeSections`，每项输出 `section_index / section_name / start_time（FAnimLinkableElement::GetTime()）/ segment_length（Montage->GetSectionLength(idx)）/ next_section_name`。**注意**：`LinkValue` 是 `FAnimLinkableElement` 的 protected 成员，外部不可直接访问；绝对秒数走 `GetTime()` 已经够用
  - **SlotAnimTracks**：遍历 `Montage->SlotAnimTracks`，每个 slot 输出 `slot_index / slot_name / segment_count / segments[]`
  - **Segments**（每个 slot 内）：`segment_index / anim_path / anim_class / start_pos / end_pos（GetEndPos()）/ length（GetLength()）/ anim_start_time / anim_end_time / play_rate（AnimPlayRate）/ loop_count（LoopingCount）`
  - **Notify tracks**：`#if WITH_EDITORONLY_DATA` guard 下读 `Montage->AnimNotifyTracks`，每项输出 `track_index / track_name`。插件是 editor target，guard 一定为 true，但保留 guard 以贴合 UE 编码规范

### 3. `Plugins/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp`

Animation 分支的 if 条件追加一个 `|| CommandType == TEXT("get_montage_composite_info")`。命令字符串总数 13 → 14。

### 4. `C:\Workspace\git\unreal-mcp\Python\tools\animation_tools.py`

`register_animation_tools` 内追加 1 个 `@mcp.tool()` 函数 `get_montage_composite_info(ctx, asset_path)`。模板照搬第一批：`from unreal_mcp_server import get_unreal_connection` → `unreal.send_command(...)` → 错误处理。`unreal_mcp_server.py` 不动。

## 数据结构参考

```json
{
  "asset_path": "/Game/Animations/MM_Pistol_Fire_Montage.MM_Pistol_Fire_Montage",
  "asset_class": "AnimMontage",
  "skeleton": "/Game/Characters/.../SK_UEFN_Mannequin.SK_UEFN_Mannequin",
  "play_length": 1.234,
  "is_loop": false,
  "blend_in_time": 0.25,
  "blend_out_time": 0.25,
  "blend_out_trigger_time": -1.0,
  "enable_auto_blend_out": true,
  "section_count": 2,
  "sections": [
    {
      "section_index": 0,
      "section_name": "Default",
      "start_time": 0.0,
      "segment_length": 0.8,
      "next_section_name": "Loop"
    }
  ],
  "slot_count": 1,
  "slot_anim_tracks": [
    {
      "slot_index": 0,
      "slot_name": "Arms",
      "segment_count": 1,
      "segments": [
        {
          "segment_index": 0,
          "anim_path": "/Game/.../MM_Pistol_Fire.MM_Pistol_Fire",
          "anim_class": "AnimSequence",
          "start_pos": 0.0,
          "end_pos": 1.234,
          "length": 1.234,
          "anim_start_time": 0.0,
          "anim_end_time": 1.234,
          "play_rate": 1.0,
          "loop_count": 1
        }
      ]
    }
  ],
  "notify_track_count": 2,
  "notify_tracks": [
    { "track_index": 0, "track_name": "Default" },
    { "track_index": 1, "track_name": "Foley" }
  ]
}
```

## 关键技术细节

- `UAnimMontage` 继承自 `UAnimCompositeBase`，再上一层是 `UAnimSequenceBase`。`bLoop` 是 UAnimSequenceBase 的成员（`AnimSequenceBase.h:68`），**不在 UAnimMontage 自己的字段里**——但通过派生类指针直接 `Montage->bLoop` 即可访问
- `FCompositeSection`（`AnimMontage.h:37`）继承 `FAnimLinkableElement`，`StartTime` 走基类的 `GetTime()`（`AnimLinkableElement.h:94`），不要直接读 `LinkValue`——`LinkValue` 只是 link method 的内部表示，绝对时间用 `GetTime()`
- `Montage->GetSectionLength(int32)` 是 ENGINE_API（`AnimMontage.h:804`），可以直接调，不用自己算
- `FAnimSegment`（`AnimCompositeBase.h:67`）的关键字段：`StartPos / AnimStartTime / AnimEndTime / AnimPlayRate / LoopingCount`，方法 `GetEndPos() / GetLength() / GetAnimReference()`。`GetLength()` 内部公式 `(LoopingCount * (AnimEndTime - AnimStartTime)) / |AnimPlayRate|`，已经包含 loop 与 play rate 的影响
- `BlendIn / BlendOut` 类型是 `FAlphaBlend`，调 `GetBlendTime()` 拿浮点秒数；`Montage` 还有 `GetDefaultBlendInTime() / GetDefaultBlendOutTime()` 包装方法（`AnimMontage.h:666/669`），等价
- `BlendOutTriggerTime`（`AnimMontage.h:657`）：负值表示从尾端往前推 BlendOut 时长开始 blend out；非负值表示绝对时间触发
- `AnimNotifyTracks` 是 `WITH_EDITORONLY_DATA`（`AnimSequenceBase.h:71`），编辑器构建总是有；这个字段只存 track 元数据（name + color），具体 notify 引用通过 `Notifies` 数组反向关联——**不要**通过 `AnimNotifyTracks[i].Notifies` 取 notify，那里面是 raw pointer 列表会绕一圈，已经有 `get_animation_notifies` 走 `UAnimationBlueprintLibrary::GetAnimationNotifyEvents` 的稳定路径
- Build.cs **不用动**——本批没有任何新模块依赖，所有 type 都已在第一批/AnimationBlueprintLibrary 模块中可见

## 生效步骤（用户执行）

1. 关 UE 编辑器
2. 重编 UnrealMCP 插件（Rider / VS 的 Build，或命令行 UBT）
3. 启 UE
4. `/exit` 退出 Claude Code，重新 `claude` 启动（让 MCP 注册新工具）
5. 跑端到端验证：
   - 用 `find_animations_for_skeleton` 找一张已知的 AnimMontage 路径（比如 `MM_Pistol_Fire_Montage` 或 Game Animation Sample 里 Traversal 系列的 Montage）
   - 跑 `get_montage_composite_info(asset_path)`，检查 sections / slot_anim_tracks / notify_tracks 三个数组都非空
   - 错误路径：把 AnimSequence 路径喂给它应该返回 `AnimMontage asset not found`

## 已知限制 / 后续可能扩展

- BranchingPoints 详情未单独输出（当前由 `get_animation_notifies` 的 `is_branching_point` 字段间接覆盖）
- TimeStretchCurve / BlendProfile / SyncSlotIndex 未输出
- BlendIn / BlendOut 只输出时间，不输出 BlendOption / CurveCustom 形态
- 如果用户需要某个 Section 的运行时绝对结束时间，可以自己用 `start_time + segment_length` 算——已经够用
