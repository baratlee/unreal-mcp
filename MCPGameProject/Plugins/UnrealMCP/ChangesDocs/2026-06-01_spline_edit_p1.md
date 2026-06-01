# Spline Component Editing P1 (LT9)

**Date:** 2026-06-01
**Scope:** Incremental edit operations + spline-level config that didn't fit in `set_spline_points` payload.
**Builds on:** `2026-06-01_spline_edit_p0.md` (P0 four core tools)

## Why

P0 covered the high-frequency "read / bulk-replace / single-point edit / clear" shape. P1 fills the long tail:

- **add_spline_point** — append or insert; lets you grow a spline without re-sending the full point list
- **remove_spline_point** — delete by index without rebuilding
- **set_spline_closed_loop** — dedicated toggle; previously only reachable as side effect inside `set_spline_points`
- **set_spline_default_up_vector** — drives rotation interpolation when per-point rotations aren't authored; matters for paths and ribbons

No specific blocking task triggered P1 — bundled now to close LT9.

## New Tools (4)

| Tool | Required Params | Optional Params |
|---|---|---|
| `add_spline_point` | blueprint_path, component_name, location | index, arrive_tangent, leave_tangent, rotation, scale, type, coordinate_space |
| `remove_spline_point` | blueprint_path, component_name, index | — |
| `set_spline_closed_loop` | blueprint_path, component_name, closed_loop | — |
| `set_spline_default_up_vector` | blueprint_path, component_name, up_vector | coordinate_space |

### add_spline_point semantics

- `index` omitted, < 0, or ≥ current point count → **append** at end
- otherwise → **insert at index** (existing points from `index` onward shift +1)
- Optional per-point fields applied via Set*AtSplinePoint after the add — same fields and semantics as `set_spline_points` entries

### set_spline_default_up_vector

Wraps `USplineComponent::SetDefaultUpVector(Vector, CoordinateSpace)`. Default Z-up convention: `[0, 0, 1]` in Local space.

## Implementation Notes

- Reuses all P0 helpers from anonymous namespace: `FindSplineComponentTemplate`, `ParseCoordinateSpace`, `ParsePointType`, `ReadVector`, `ReadRotator`, `VectorToJson`.
- All write paths call `MarkBlueprintAsModified(Blueprint)` after the modification + UpdateSpline (per-op or explicit).
- Same caller contract as P0: compile_blueprint + save_dirty_assets to persist.
- No new includes needed (all UE APIs already pulled in via `Components/SplineComponent.h`).

## Files

**Modified (no new files):**
- `Plugins/UnrealMCP/Source/UnrealMCP/Public/Commands/UnrealMCPSplineCommands.h` — 4 new private handler declarations
- `Plugins/UnrealMCP/Source/UnrealMCP/Private/Commands/UnrealMCPSplineCommands.cpp` — 4 new handler implementations + dispatch
- `Plugins/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPBridge.cpp` — extend Spline dispatch branch with 4 new CommandType strings
- `Python/tools/spline_tools.py` — 4 new `@mcp.tool()` functions

## UE 5.7 Source References

- `USplineComponent::AddSplinePoint(FVector, ESplineCoordinateSpace::Type, bool)`
- `USplineComponent::AddSplinePointAtIndex(FVector, int32, ESplineCoordinateSpace::Type, bool)`
- `USplineComponent::RemoveSplinePoint(int32, bool)`
- `USplineComponent::SetClosedLoop(bool, bool)`
- `USplineComponent::SetDefaultUpVector(FVector, ESplineCoordinateSpace::Type)`
- `USplineComponent::GetDefaultUpVector(ESplineCoordinateSpace::Type) const`

## Verification Steps

1. Build plugin (Live Coding or full rebuild)
2. Restart editor (Bridge state, new commands registered)
3. Smoke test on `BP_KLWeapon_Claw` HitSpline:
   - `add_spline_point` append → num_points goes from 2 to 3
   - `add_spline_point(index=1)` insert → num_points to 4, new point at index 1
   - `remove_spline_point(index=2)` → num_points to 3
   - `set_spline_closed_loop(closed_loop=true)` → IsClosedLoop = true
   - `set_spline_closed_loop(closed_loop=false)` → restore
   - `set_spline_default_up_vector(up_vector=[0, 1, 0])` → confirm Y-up, then restore Z-up
4. compile_blueprint + save_dirty_assets

## LT9 Status After P1

LT9 P0 + P1 complete. Together they cover:
- ✅ Read (full data + coordinate space choice)
- ✅ Bulk write (clear + add N)
- ✅ Single-point write (in-place modify, partial fields)
- ✅ Append / insert / delete by index
- ✅ Closed-loop toggle
- ✅ Default up vector

Not in LT9 (no foreseen demand):
- Per-actor Spline instance edits in a live World (P0/P1 only edit Blueprint templates)
- Spline mesh deformation hookups (separate component family)
