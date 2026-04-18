# World Partition Actor Listing Fix

**Date**: 2026-04-17
**Scope**: `get_actors_in_level` / `find_actors_by_name` — World Partition + One File Per Actor 支持

## Problem

在启用 World Partition + OFPA 的地图中，`get_actors_in_level` 和 `find_actors_by_name` 无法返回未加载 cell 中的 Actor。

根因：两个 handler 均使用 `UGameplayStatics::GetAllActorsOfClass(GWorld, ...)` 遍历 Actor，该 API 只能访问当前已加载到内存的 Actor。World Partition 按 cell 流式加载，编辑器视口范围外的 Actor 不在内存中，因此被遗漏。

## Solution

当检测到地图启用 World Partition 时，改用 `FWorldPartitionHelpers::ForEachActorDescInstance()` 遍历所有 Actor 描述符（包括未加载的）：

1. **已加载 Actor**（`IsLoaded() == true`）：通过 `GetActor()` 获取 `AActor*`，走原有的 `ActorToJson()` 路径，输出完整数据
2. **未加载 Actor**：从 `FWorldPartitionActorDescInstance` 提取元数据（name/class/transform），额外输出 `"is_wp_unloaded": true` 标记
3. **系统 Actor 兜底**：WP 遍历后再用 `UGameplayStatics::GetAllActorsOfClass()` 补充非 WP 管理的 Actor（WorldSettings、DefaultPhysicsVolume 等），通过 `TSet<FName>` 去重

非 World Partition 地图保持原有行为不变。

## Changed Files (2)

### `Private/Commands/UnrealMCPEditorCommands.cpp`
- **includes 追加 3 个**：`WorldPartition/WorldPartition.h` / `WorldPartition/WorldPartitionHelpers.h` / `WorldPartition/WorldPartitionActorDescInstance.h`
- **`HandleGetActorsInLevel`**：���写，WP 地图走 `ForEachActorDescInstance` + 系统 Actor 兜底���非 WP 走原路径
- **`HandleFindActorsByName`**：重写，同上策略；名称匹配同时检查 `GetActorLabelOrName()`（编辑器标签）和 `GetActorName()`（内部名），解决已加载 WP Actor 通过标签或内部名均可���索的问题

### `Private/Commands/UnrealMCPCommonUtils.cpp`
- **`ActorToJson`**：追加 `label` 字段（`WITH_EDITOR`），输出 `Actor->GetActorLabel()`。仅当 label 非空且与内部名不同时输出。WP+OFPA 地图中内部名是 GUID 哈希，label 是编辑器里看到的名字（如 "Cube"）

## New Dependencies

无新模块依赖。`WorldPartition` 相关头文件属于 `Engine` 模块（已在 Build.cs 依赖中）。

## Output Format Change

### 已加载 Actor — 新增 `label` 字���
```json
{
  "name": "StaticMeshActor_UAID_C895CE49BE5B72D202_1546930319",
  "class": "StaticMeshActor",
  "label": "Cube",
  "location": [-1340, 20, 260],
  "rotation": [0, 0, 0],
  "scale": [1, 1, 1]
}
```
`label` 仅在 label 与 name 不同时出现（非 WP 地图中两者通常相同，不会输出 label）。

### 未加载 WP Actor — `is_wp_unloaded` 标记
```json
{
  "name": "Cube",
  "class": "StaticMeshActor",
  "location": [100, 200, 50],
  "rotation": [0, 0, 0],
  "scale": [1, 1, 1],
  "is_wp_unloaded": true
}
```

## Verification ✅ (2026-04-18)

在 OpenWorld1 地图上完成端到端验证：

- `find_actors_by_name("Cube")`：返回 2 个结果
  - **Cube**（已加载，视口附近 [-1340,20,260]）：有 `label` 字段，无 `is_wp_unloaded` ✅
  - **Cube2**（未加载，远处 [200000,200000,260]）：带 `is_wp_unloaded: true` ✅
- `get_actors_in_level`：完整列出所有 WP Actor（HLOD、LandscapeStreamingProxy、灯光、Cube 等）
  - 未加载 Actor 全部带 `is_wp_unloaded: true` ✅
  - 已加载 Actor 不带该标记 ✅
  - 系统 Actor（WorldSettings、DefaultPhysicsVolume 等）正常出现 ✅
