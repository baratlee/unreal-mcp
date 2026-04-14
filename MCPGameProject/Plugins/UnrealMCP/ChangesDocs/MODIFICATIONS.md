# UnrealMCP Plugin Modifications

本文档记录对 UnrealMCP 插件的修改，主要为兼容 UE 5.7。

原始项目：https://github.com/chongdashu/unreal-mcp（基于 UE 5.5）

---

## 2026-04-11: 兼容 UE 5.7 — 移除 ANY_PACKAGE

`ANY_PACKAGE` 宏在 UE 5.1 中已被移除，所有 `FindObject<UClass>(ANY_PACKAGE, ...)` 调用替换为 `FindObject<UClass>(nullptr, ...)`，语义不变（`nullptr` 表示在所有包中搜索）。

**涉及文件：**

- `Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp` — 4 处
- `Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintNodeCommands.cpp` — 5 处

---

## 2026-04-11: 兼容 UE 5.7 — 禁用 C4459 Shadow Variable 警告

UE 5.7 引擎头文件 `StringConv.h` 触发 C4459 警告（局部变量隐藏全局声明），默认作为错误处理导致编译失败。由于无法修改引擎代码，创建插件私有 PCH 文件，在 include 引擎头文件前通过 `#pragma warning(disable: 4459)` 禁用该警告。

**涉及文件：**

- `Source/UnrealMCP/UnrealMCP.Build.cs` — 添加 `PrivatePCHHeaderFile = "UnrealMCPPCH.h";`
- `Source/UnrealMCP/Public/UnrealMCPPCH.h` — 新建，插件私有 PCH

---

## 2026-04-11: 兼容 UE 5.7 — 补充缺失的头文件 include

UE 5.7 的 IWYU (Include What You Use) 更严格，不再通过其他头文件间接包含某些类型定义，需要显式 include。

**修改：**

- `Source/UnrealMCP/Public/Commands/UnrealMCPCommonUtils.h` — 添加 `#include "EdGraph/EdGraphPin.h"`，引入 `EEdGraphPinDirection` 类型定义
- `Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp` — 添加 `#include "Editor.h"`（引入 `GEditor`）和 `#include "Materials/MaterialInterface.h"`（引入 `UMaterialInterface`）

---

## 2026-04-11: 兼容 UE 5.7 — 更新废弃 API

**FImageUtils::CompressImageArray → PNGCompressImageArray：**

`CompressImageArray` 已被标记为废弃，替换为 `PNGCompressImageArray`。新 API 的输出参数类型从 `TArray<uint8>` 变更为 `TArray64<uint8>`（即 `TArray<uint8, FDefaultAllocator64>`），同步修改了局部变量类型。

**涉及文件：**

- `Source/UnrealMCP/Private/Commands/UnrealMCPEditorCommands.cpp`

---

## 其他操作

- 删除了原项目预编译的 `Binaries/` 和 `Intermediate/` 目录，使用 UE 5.7 重新编译
