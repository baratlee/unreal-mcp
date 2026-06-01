# Spline Component Editing (LT9 P0)

**Date:** 2026-06-01
**Scope:** Edit USplineComponent point data on Blueprint templates via MCP

## Why

Spline point data lives in `FSplineCurves` (deeply nested `FInterpCurveVector` etc.) which `set_component_property`'s reflection walker doesn't descend (same limitation as `BodyInstance.CollisionResponses`). Until now anyone wanting to author a Spline-driven asset (HitDetect lines, whip bones, patrol paths, trigger volumes) had to either drop into the Viewport and hand-place points, or write `ClearSplinePoints + AddSplinePoint` into the owning C++ constructor and recompile.

PrjKunlun's `AKLWeapon_Claw` (Quad double-claw HitDetect) hit exactly this — workaround was a C++ constructor that resets two Splines to `(0,0,0) → (20,0,0)` on every recompile. Not sustainable for future Spline-shaped melee weapons.

## New Tools (4)

| Tool | Required Params | Optional Params |
|---|---|---|
| `get_spline_info` | blueprint_path, component_name | coordinate_space ("Local"/"World", default Local) |
| `set_spline_points` | blueprint_path, component_name, points[] | coordinate_space, closed_loop |
| `set_spline_point` | blueprint_path, component_name, index | location, arrive_tangent, leave_tangent, rotation, scale, type, coordinate_space |
| `clear_spline_points` | blueprint_path, component_name | — |

**Point object schema** (for `set_spline_points` array entries and `set_spline_point`):
- `location: [x, y, z]` — required for `set_spline_points` entries; optional for `set_spline_point`
- `arrive_tangent: [x, y, z]` — optional
- `leave_tangent: [x, y, z]` — optional. If only one tangent given, both set to it
- `rotation: [pitch, yaw, roll]` — optional
- `scale: [x, y, z]` — optional
- `type: "Linear"/"Curve"/"Constant"/"CurveClamped"/"CurveCustomTangent"` — optional (default `Curve`); `CurveAuto` aliases to `Curve`

## Implementation Notes

### Component Lookup
Reuses the same SCS → inherited SCS → native CDO chain as `set_component_property` / `get_component_properties`. Helper `FindSplineComponentTemplate` (in anonymous namespace in `UnrealMCPSplineCommands.cpp`) collapses the search and `Cast<USplineComponent>` filters non-spline matches. The `inherited_or_native` Source label is less granular than the BlueprintCommands version (which distinguishes the two via parent BP walk) — sufficient for spline ops, not worth duplicating the parent walk.

### Writes
Every write path calls `UpdateSpline()` after batched modifications (per-point `bUpdateSpline=false`, single end-of-call `UpdateSpline`), then `FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint)`. Caller still has to issue `compile_blueprint` + `save_dirty_assets` to persist — matches Material extension contract.

### Tangent Semantics
- Both `arrive_tangent` and `leave_tangent` → `SetTangentsAtSplinePoint` (asymmetric set)
- Only `arrive_tangent` → `SetTangentAtSplinePoint` (engine sets both to this value)
- Only `leave_tangent` (set_spline_point only) → also `SetTangentAtSplinePoint` to that value (mirrors UE's single-tangent setter behavior — no separate "leave-only" API)
- Neither → leave existing tangents untouched (curve-auto recomputation happens on UpdateSpline)

### Rotation Convention
`[pitch, yaw, roll]` array order matches `FRotator` constructor / `UE` ToString — different from some external conventions that use `[roll, pitch, yaw]`. Documented in tool docstrings to avoid surprise.

### Build.cs
Not modified — `USplineComponent` is in `Engine` module, `FBlueprintEditorUtils` is in `UnrealEd` (already linked by the plugin via existing Kismet2 includes).

## Files

**New:**
- `Plugins/UnrealMCP/Source/UnrealMCP/Public/Commands/UnrealMCPSplineCommands.h`
- `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPSplineCommands.cpp`
- `Python/tools/spline_tools.py`

**Modified:**
- `Plugins/UnrealMCP/Source/UnrealMCP/Public/UnrealMCPBridge.h` — add include + `SplineCommands` field
- `Plugins/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp` — add include, MakeShared, Reset, dispatch branch
- `Python/unreal_mcp_server.py` — add import + register_spline_tools call

## UE 5.7 Source References

- `USplineComponent` API: `Engine/Source/Runtime/Engine/Classes/Components/SplineComponent.h`
  - `ClearSplinePoints(bool bUpdateSpline)`
  - `AddSplinePoint(FVector Position, ESplineCoordinateSpace::Type CoordinateSpace, bool bUpdateSpline)`
  - `SetLocationAtSplinePoint / SetTangentAtSplinePoint / SetTangentsAtSplinePoint / SetRotationAtSplinePoint / SetScaleAtSplinePoint / SetSplinePointType`
  - `GetNumberOfSplinePoints / GetLocationAtSplinePoint / GetArriveTangentAtSplinePoint / GetLeaveTangentAtSplinePoint / GetRotationAtSplinePoint / GetScaleAtSplinePoint / GetSplinePointType`
  - `IsClosedLoop / SetClosedLoop`
  - `ESplinePointType::Type`: Linear / Curve / Constant / CurveClamped / CurveCustomTangent

## Verification Steps

1. Build plugin (Live Coding or full rebuild)
2. Restart editor if Bridge subsystem startup signature changed
3. Test `get_spline_info` on a known spline component (e.g. `/Game/Examples/Demo/Weapon/Quad/BP_KLWeapon_Claw` HitSpline)
4. Test `set_spline_points` with 3-point payload, then `get_spline_info` to verify
5. Test `set_spline_point` with partial fields (location-only) — confirm other fields preserved
6. Test `clear_spline_points` — verify `num_points=0` after
7. After each write, `compile_blueprint` + `save_dirty_assets`, close & reopen blueprint in editor to confirm persistence

## Out of Scope (Future P1)

- `add_spline_point` (append/insert at index)
- `remove_spline_point` (delete by index)
- `set_spline_closed_loop` (dedicated toggle; current path requires `set_spline_points`)
- `set_spline_default_up_vector`

P0 ships the 4 highest-value ops; P1 covers the long tail if real usage demands.
