# unreal-mcp 扩展：set_animation_properties 新工具

**日期**：2026-04-20
**涉及仓库**：
- `C:\Workspace\git\LeoPractice`（UnrealMCP 插件）
- `C:\Workspace\git\unreal-mcp`（Python MCP server）

## 修改目的

之前只能读取动画 Root Motion 属性（通过 `get_animation_info`），无法修改。Quad 四足项目中发现第三方动画包（Animalia Leopard）的 locomotion 动画全部开启了 Root Motion，需要批量关闭才能配合 Mover 驱动的角色移动。

## 新增功能

### `set_animation_properties`

设置 UAnimSequence 的 Root Motion 相关属性。只接受 AnimSequence（非 AnimMontage）。

**参数**（全部可选，只修改提供的字段）：

| 参数 | 类型 | 说明 |
|---|---|---|
| `asset_path` | string | AnimSequence 资产路径（必填） |
| `b_enable_root_motion` | bool | 启用/禁用 Root Motion |
| `root_motion_root_lock` | string | Root Lock 模式：`RefPose` / `AnimFirstFrame` / `Zero` |
| `b_force_root_lock` | bool | 强制 Root Lock |
| `b_use_normalized_root_motion_scale` | bool | 使用归一化 Root Motion 缩放 |

**行为**：
- 直接赋值 public 字段（UE5.7 中这些都是 public UPROPERTY）
- 修改后调用 `MarkPackageDirty()`（用户需在编辑器中 Ctrl+S 保存）
- 返回修改后的所有 RM 属性当前值

**返回示例**：
```json
{
  "success": true,
  "asset_path": "/Game/.../Loco_Walk.Loco_Walk",
  "modified_count": 1,
  "modified_fields": ["bEnableRootMotion"],
  "b_enable_root_motion": false,
  "root_motion_root_lock": "RefPose",
  "b_force_root_lock": false,
  "b_use_normalized_root_motion_scale": true
}
```

## 修改的文件

### C++ 端

| 文件 | 改动 |
|---|---|
| `Public/Commands/UnrealMCPAnimationCommands.h` | 声明 `HandleSetAnimationProperties` |
| `Private/Commands/UnrealMCPAnimationCommands.cpp` | 实现 handler + 路由 |
| `Private/UnrealMCPBridge.cpp` | 路由表添加 `set_animation_properties` |

### Python 端

| 文件 | 改动 |
|---|---|
| `Python/tools/animation_tools.py` | 注册 `set_animation_properties` 工具 |

## 使用场景

批量关闭 locomotion 动画的 Root Motion（Mover 驱动的角色不应使用 RM 动画）：
```
set_animation_properties("/Game/.../Loco_Walk", b_enable_root_motion=false)
set_animation_properties("/Game/.../Loco_WalkSlow", b_enable_root_motion=false)
...
```
