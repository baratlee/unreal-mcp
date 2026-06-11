# SetObjectProperty FObjectProperty Load Fix (2026-06-11)

`FUnrealMCPCommonUtils::SetObjectProperty` was silently writing **nullptr** to every `FObjectProperty` (TObjectPtr<UObject>) when the value was a JSON string, regardless of whether the string was a valid asset path. Symptom found while landing the Material write batch — `set_material_expression_property(..., property_name="Texture", value="/Game/.../T_bodi_Normal")` reported `status=success` but the Texture field on the resulting MaterialExpression was `None`.

## Root cause

The pre-fix generic fallback at the end of `SetObjectProperty` (just before this fix) was:

```cpp
const TCHAR* Result = Property->ImportText_Direct(Buffer, PropertyAddr, Object, PPF_None);
if (Result) { bWritten = true; break; }
```

`FObjectProperty::ImportText_Direct` under `PPF_None` does **not** invoke `LoadObject` for unloaded assets. When the asset isn't already in memory:

1. ImportText parses the T3D-form string (e.g. `Texture2D'/Game/.../T_bodi_Normal.T_bodi_Normal'`).
2. Looks up the object via `StaticFindObject` (in-memory only).
3. Returns `nullptr` to the property write.
4. Still returns a non-null `Result` (advanced parse cursor), so the caller falsely concludes "wrote successfully".

`PPF_None` is correct for safety on transient writes, but for explicit MCP asset references the intent is always "load the named asset and bind it".

## Fix

Added an explicit `FObjectProperty` branch **before** the generic ImportText fallback:

- Strip T3D class prefix if present: `Texture2D'.../X.X'` or `/Script/Engine.Texture2D'.../X.X'` → keep the path between the first and last `'`.
- Accept plain object path too: `/Game/.../X.X`.
- Accept `"None"` and empty string → write nullptr.
- Otherwise call `StaticLoadObject(ObjProp->PropertyClass, nullptr, *PathStr)`. If load fails, surface the error (no silent nullptr).
- `ObjProp->SetObjectPropertyValue(PropertyAddr, Asset)`.

The branch only runs when `Value->Type == EJson::String`. Numeric / bool / array inputs still go through earlier specific branches; struct + name + soft object + other text-importable types still go through the generic ImportText fallback below.

## Affected commands

Any `set_*` tool that writes through `FUnrealMCPCommonUtils::SetObjectProperty` and the target property is a `TObjectPtr<UObject>` asset reference:

- `set_material_expression_property` (Texture Sample's `Texture`, MaterialFunctionCall's `MaterialFunction`, etc.)
- `set_blueprint_property` (any TObjectPtr field on a BP CDO)
- `set_component_property` (any TObjectPtr field on a component)
- `set_actor_property` (any TObjectPtr field on an actor)
- `set_data_asset_property`
- `set_animation_notify_property` (notify object refs)
- `set_anim_graph_node_property` (anim node asset refs)

`FSoftObjectProperty` is **not** affected by this fix (still goes through ImportText fallback, which works correctly for SoftObjectPath since it doesn't require pre-loading).

## Files

- `Private/Commands/UnrealMCPCommonUtils.cpp` — new FObjectProperty branch around line ~852 (before generic fallback).

## Build

Requires VS rebuild of UnrealEditor.exe + editor restart, same as any UnrealMCP C++ change.

## Verification

After rebuild:
1. `set_material_expression_property` on a Texture Sample with `value="/Game/.../Texture.Texture"` (plain path).
2. `get_material_graph` should show the new path under `properties.Texture`, not `"None"`.
3. Repeat with full T3D form `"Texture2D'/Game/.../Texture.Texture'"` — same result.
4. Negative test: pass a non-existent path → expect error `"failed to load asset"`, not silent success.
