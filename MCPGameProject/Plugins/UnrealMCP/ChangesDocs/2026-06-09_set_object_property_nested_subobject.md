# Nested Instanced Sub-Object Path in SetObjectProperty

Date: 2026-06-09
Author: Claude Code (LEOCC)

## TL;DR

`FUnrealMCPCommonUtils::SetObjectProperty` now accepts dot-separated property paths to drill into instanced `UObject*` sub-objects. This unblocks writes on `AnimNotifyState_MotionWarping.RootMotionModifier.{WarpTargetName,bWarpTranslation,...}` and any other `EditInlineNew` sub-object property exposed by MCP setter tools.

Before: `RootMotionModifier.WarpTargetName` → `Property not found`. The ImportText fallback would parse the whole `RootMotionModifier=(WarpTargetName=...)` string and silently destroy the original sub-object (replace with `None`).

After: the dot path is split, the head property is resolved as an `FObjectProperty`, the contained `UObject*` is dereferenced, and `SetObjectProperty` recurses with the tail. The outer object is also notified via `NotifyPropertyChanged` so package-dirty / BP-modified propagation still happens.

## How the bug surfaced (AM_Combo_Attack1)

`AM_Combo_Attack1` montage had 4 `AnimNS_KLAttackWarping` notify states without paired `AnimNotifyState_MotionWarping` companions. After adding 4 `AnimNotifyState_MotionWarping` notify states via `add_animation_notify`, each one auto-created a default `RootMotionModifier_SkewWarp` sub-object — but `set_animation_notify_property` could not set its `WarpTargetName` or `bWarpTranslation`:

- `property_name = "WarpTargetName"` → not on notify object (it lives on the inner modifier)
- `property_name = "RootMotionModifier.WarpTargetName"` → `Property not found` (the dot-split was never attempted)
- `property_name = "RootMotionModifier:WarpTargetName"` → same
- `property_name = "RootMotionModifier"`, `value = "(WarpTargetName=AttackTarget)"` → `success: true` returned, but the side effect was destructive: the original `RootMotionModifier_SkewWarp` instance was replaced by `None`, leaving the notify functionally inert.

Same shape blocks Niagara modifier writes, any other `EditInlineNew` sub-object exposed by MCP.

## Root cause

`SetObjectProperty` calls `Object->GetClass()->FindPropertyByName(*PropertyName)` directly — no path traversal. The first miss falls through to the ImportText branch which parses the value into the outer property, and for instanced `FObjectProperty` that means "construct/parse a new instance from the text", which yields `None` when the input is a property-list fragment rather than an asset path.

## Fix

A short block at the top of `SetObjectProperty` detects a `.` in `PropertyName`, splits head/tail, resolves the head property, and recurses into the inner `UObject` if the head is an `FObjectProperty`:

```cpp
int32 DotIdx = INDEX_NONE;
if (PropertyName.FindChar(TEXT('.'), DotIdx))
{
    const FString HeadName = PropertyName.Left(DotIdx);
    const FString TailName = PropertyName.Mid(DotIdx + 1);

    FProperty* HeadProperty = Object->GetClass()->FindPropertyByName(*HeadName);
    if (!HeadProperty)
    {
        OutErrorMessage = FString::Printf(TEXT("Nested path head property not found: %s"), *HeadName);
        return false;
    }

    if (FObjectProperty* HeadObjProp = CastField<FObjectProperty>(HeadProperty))
    {
        void* HeadAddr = HeadObjProp->ContainerPtrToValuePtr<void>(Object);
        UObject* InnerObject = HeadObjProp->GetObjectPropertyValue(HeadAddr);
        if (!InnerObject)
        {
            OutErrorMessage = FString::Printf(TEXT("Nested subobject is null at: %s"), *HeadName);
            return false;
        }

        if (SetObjectProperty(InnerObject, TailName, Value, OutErrorMessage))
        {
            NotifyPropertyChanged(Object, HeadObjProp);
            return true;
        }
        return false;
    }

    OutErrorMessage = FString::Printf(
        TEXT("Nested path through non-object property not supported (head: %s, type: %s). FStructProperty (e.g. BodyInstance) is a known gap, see ChangesDocs."),
        *HeadName, *HeadProperty->GetClass()->GetName());
    return false;
}
```

Behavior:
- `Foo` → unchanged from before, single property write.
- `Foo.Bar` → if `Foo` is `FObjectProperty` and the value is non-null, recurse into that inner UObject and set `Bar`. Recursion is open-ended (`Foo.Bar.Baz` works too).
- `Foo.Bar` with `Foo` not an `FObjectProperty` → explicit error; struct path (`FStructProperty`, `FBodyInstance` style) is **not** handled and is logged as a known gap (P2).
- `Foo.Bar` with `Foo` being a null sub-object → explicit error, no silent overwrite.

The outer object's `NotifyPropertyChanged(Object, HeadObjProp)` is invoked after a successful inner write so PostEditChange propagates: package dirty marking, BP-modified marking for CDO writes, and the actor/component PostEditChange chain that protects instance overrides (see `2026-06-08_post_edit_change_fix.md`).

## Coverage

`SetObjectProperty` is the central reflection-based setter consumed by:

- `set_actor_property` (UnrealMCPEditorCommands.cpp)
- `set_blueprint_property` (UnrealMCPBlueprintCommands.cpp)
- `set_data_asset_property` (UnrealMCPDataAssetCommands.cpp)
- `set_animation_notify_property` (UnrealMCPAnimationCommands.cpp)
- `set_state_tree_node_property` / related (UnrealMCPStateTreeCommands.cpp)
- Several command-internal call-sites in CommonUtils

All of them automatically gain nested-path support without any further change.

## What still doesn't work

- **FStructProperty nested paths** (e.g. `BodyInstance.CollisionResponses.Pawn`, `BodyInstance.ResponseToChannels.Pawn`). The address calculation walks `FProperty::ContainerPtrToValuePtr<void>(Object)` which assumes `Object` is the outer; for struct addresses you'd resolve `StructPtr` first and call `ContainerPtrToValuePtr<void>(StructPtr)`. Different code path. Left as P2 with an explicit error message instead of silently returning success.
- **Indexed paths** like `Foo[0].Bar` or `Foo("Key").Bar`. Not needed by the immediate use case; can be added later via a small parser.

## Files modified

- `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPCommonUtils.cpp` — top of `FUnrealMCPCommonUtils::SetObjectProperty`.

No Python-side changes. The MCP tool schemas already accept `property_name` as an arbitrary string; the dot syntax is transparent to the wire protocol.

## Verification result

✅ **2026-06-09 实测通过**（AM_Combo_Attack1, notify_index=1 即 raw `Notifies[1]`，对应 trigger_time 0.2546 的 MotionWarping）：

```
set_animation_notify_property(1, "RootMotionModifier.WarpTargetName", "MCPProbe") → success
set_animation_notify_property(1, "RootMotionModifier.bWarpTranslation", false)   → success
set_animation_notify_property(1, "RootMotionModifier.WarpTargetName", "AttackTarget") → success（还原）
```

错误路径同样实测：
- `set(..., "RootMotionModifier.NonExistent", ...)` → 报 `Nested path set failed: Property not found: NonExistent`
- `set(..., "DoesNotExist.Bar", ...)` → 报 `Nested path set failed: Nested path head property not found: DoesNotExist`
- `set(..., "NotifyColor.R", ...)` → 报 `Nested path set failed: Nested path through non-object property not supported (head: NotifyColor, type: StructProperty)`

`get_animation_notify_details` 仍只暴露顶层属性，子对象 `WarpTargetName` 现值需手动 Editor 验证或后续加 `get_animation_notify_subobject_properties` 工具。

## Companion fix: HandleSetAnimationNotifyProperty error transmission

`set_animation_notify_property` 路径的 ImportText fallback 在路径含 `.` 时永远 fail（因为 FindPropertyByName 不识别点分），且会把 `SetObjectProperty` 的真实错误信息吃掉返回通用 "Property not found on notify object"。同 commit 加了短路：

```cpp
if (PropertyName.Contains(TEXT(".")))
{
    return FUnrealMCPCommonUtils::CreateErrorResponse(
        FString::Printf(TEXT("Nested path set failed: %s"), *ErrorMessage));
}
```

这样嵌套路径写失败时，调用方能看到 head not found / non-object / null subobject 等具体原因，便于排查（典型场景：notify_index 取错时立刻能看出 Object 类型不对）。

## Capability boundary update

`memory/reference_mcp_capability_boundary.md` entry #5 (MotionWarping `RootMotionModifier` not writable) is downgraded: nested path syntax now works for `EditInlineNew` `UObject` sub-objects across all setter tools. The `FStructProperty` nested write gap (BodyInstance) remains and stays in the boundary table.
