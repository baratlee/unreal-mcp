# 修 FindBlueprintByPath 无限递归（Editor 崩溃）

**日期**：2026-04-15
**涉及仓库**：`C:\Workspace\git\LeoPractice`（UnrealMCP 插件 C++）

## 现象

调用任何接受裸 blueprint 名（不带 `/Game/...` 前缀）的工具时，UE Editor 立刻 stack overflow 崩溃。

崩溃报告：`Saved/Crashes/UECC-Windows-F34784F945E8D8F0F0CC0AB1DC726D5C_0000`

- `ErrorMessage`: `Unhandled Exception: EXCEPTION_STACK_OVERFLOW`
- `CallStack`: 数千层 `FUnrealMCPCommonUtils::FindBlueprintByPath` 自递归（实际上是和 `FindBlueprintByName` 互递归，但符号都打到了同一行）
- 最后一条 MCP 请求：`{"type": "get_blueprint_function_graph", "params": {"blueprint_path": "SandboxCharacter_Mover", ...}}` —— 触发 trigger 是这个**裸名字**输入

## 根因

`UnrealMCPCommonUtils.cpp` 里 `FindBlueprintByName` 和 `FindBlueprintByPath` 互调形成无限循环：

```cpp
UBlueprint* FindBlueprintByName(const FString& BlueprintName)
{
    return FindBlueprintByPath(BlueprintName);   // 行 158
}

UBlueprint* FindBlueprintByPath(const FString& BlueprintPath)
{
    if (BlueprintPath.IsEmpty()) return nullptr;
    if (BlueprintPath.StartsWith(TEXT("/"))) { ... return ...; }

    // 不带 '/' 前缀的裸名字走老的 /Game/Blueprints/ 快捷回退
    return FindBlueprintByName(BlueprintPath);   // 行 189 ← 死循环！
}
```

之前一次重写把 `FindBlueprintByName` 里**实际的** `/Game/Blueprints/<name>` 拼接逻辑删掉了，只留下 `return FindBlueprintByPath(name)` 的 passthrough。`FindBlueprintByPath` 又把不带 `/` 的入参回调给 `FindBlueprintByName`，于是两个函数互相 tail call → stack overflow。

注释里写着"走老的 /Game/Blueprints/ 快捷回退"，但代码并没有真的做拼接——这是 review 时漏掉的语义裂口。

裸名字调用点遍布 `UnrealMCPBlueprintCommands.cpp` / `UnrealMCPBlueprintNodeCommands.cpp`（`FindBlueprint(BlueprintName)` 共 17+ 处），所以这是个高频踩雷面，不是边缘 case。

## 修复

抽 `TryLoadByObjectPath` lambda 复用 `LoadObject(Path)` + `LoadObject(Path + "." + AssetName)` 两次尝试，然后把裸名字分支改成**非递归**地依次尝试两条 `/Game/...` 候选路径：

```cpp
auto TryLoadByObjectPath = [](const FString& Path) -> UBlueprint*
{
    if (UBlueprint* Direct = LoadObject<UBlueprint>(nullptr, *Path))
    {
        return Direct;
    }
    const FString AssetName = FPaths::GetBaseFilename(Path);
    if (AssetName.IsEmpty()) return nullptr;
    return LoadObject<UBlueprint>(nullptr, *(Path + TEXT(".") + AssetName));
};

if (BlueprintPath.StartsWith(TEXT("/")))
{
    return TryLoadByObjectPath(BlueprintPath);
}

// 裸名字 → 先 /Game/Blueprints/<name>（兑现旧注释承诺）
const FString GameBlueprintsPath = FString(TEXT("/Game/Blueprints/")) + BlueprintPath;
if (UBlueprint* Found = TryLoadByObjectPath(GameBlueprintsPath))
{
    return Found;
}

// 二级回退：/Game/<name>，覆盖资产不在 Blueprints 子目录的情况
const FString GameRootPath = FString(TEXT("/Game/")) + BlueprintPath;
return TryLoadByObjectPath(GameRootPath);
```

要点：
- **没有任何函数自调 / 互调**，无论输入是什么都 O(1) 步数返回
- 找不到就返回 nullptr（caller 已经处理 nullptr → 返回 "blueprint not found"），不再让 Editor 崩
- 仍然兜底支持两种常见快捷写法：`/Game/Blueprints/<n>` 和 `/Game/<n>`
- 真正想精确指定的用户依然可以传 `/Game/ThirdParty/.../<name>` 完整路径走第一个 if 分支

对触发本次崩溃的 `"SandboxCharacter_Mover"`：实际位置在 `/Game/ThirdParty/GameAnimationSample/Blueprints/SandboxCharacter_Mover`，两条快捷回退都找不到，会**优雅返回 nullptr** 而不是崩溃。要读这个 BP，调用方必须传完整路径。

## 修改文件清单

- `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPCommonUtils.cpp` — `FindBlueprintByPath` 函数体重写，消除递归

## 验证步骤（重启后跑）

1. 关 UE → 重编 UnrealMCP 插件 → 启 UE
2. **回归测试 1（崩溃复现）**：用 unreal-mcp 调 `get_blueprint_function_graph(blueprint_path="SandboxCharacter_Mover", function_name="Get_MoveInput")`，期望返回 "blueprint not found"，**不崩溃**
3. **回归测试 2（裸名字 + 存在）**：构造一个真的位于 `/Game/Blueprints/` 下的测试 BP，用裸名字调用，期望成功返回
4. **回归测试 3（完整路径，主路径）**：用 `blueprint_path="/Game/ThirdParty/GameAnimationSample/Blueprints/SandboxCharacter_Mover"` 重试 step 2 的请求，期望成功返回 `Get_MoveInput` 函数图

## 相关源码引用

- `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPCommonUtils.cpp:154-200` —— 修复后的实现
- `Saved/Crashes/UECC-Windows-F34784F945E8D8F0F0CC0AB1DC726D5C_0000/CrashContext.runtime-xml` —— 原始崩溃栈
- `Saved/Crashes/UECC-Windows-F34784F945E8D8F0F0CC0AB1DC726D5C_0000/LeoPractice.log:1354` —— 触发崩溃的 MCP 请求
