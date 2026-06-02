# Niagara System Read P1 (LT12)

Date: 2026-06-02
Author: Claude Code (LEOCC)

## Why

After P0 landed `get_niagara_system_info`, the workflow still required the
user to know an asset path up front and provided no way to see *which assets*
(materials / meshes) a sample effect uses. P1 fills those two gaps with the
two highest-ROI tools.

## New Tools (2)

### `list_niagara_systems(path_filter="")`

Asset Registry scan for all `UNiagaraSystem` assets in the project. Same shape
as `list_data_assets` / `list_chooser_tables`. Returns
`systems[]` (`{asset_path, name, package_path}`) + `count`. `path_filter` is
an optional `/Game/...` package-path prefix; empty = whole project.

### `get_niagara_emitter_renderers(asset_path, emitter_name)`

For the named emitter on a system, walks `FVersionedNiagaraEmitterData::GetRenderers()`
and reports per renderer:
- `class_name` (e.g. `UNiagaraSpriteRendererProperties` /
  `UNiagaraMeshRendererProperties` / `UNiagaraRibbonRendererProperties` /
  `UNiagaraLightRendererProperties` / `UNiagaraDecalRendererProperties` / etc.)
- `enabled` — renderer-level enable flag (`GetIsEnabled()`)
- `materials[]` — asset path list from `GetUsedMaterials(nullptr, ...)`. This
  base-class virtual covers Sprite `Material`, Ribbon `Material`, Mesh
  per-slot override materials, etc., uniformly.
- `meshes[]` *(Mesh renderer only)* — list of `{mesh_path}` extracted from
  `UNiagaraMeshRendererProperties::Meshes[]` (each entry's `Mesh` field).

Top-level result also reports `emitter_enabled` (the emitter handle's own
enable flag — distinct from the renderer enable flag).

## Implementation Notes

- Emitter lookup: scan `UNiagaraSystem::GetEmitterHandles()` for handle whose
  `GetName().ToString()` matches `emitter_name`. Case-sensitive.
- Renderer list: `Handle.GetEmitterData()->GetRenderers()`. `GetEmitterData()`
  is the no-arg convenience overload on `FNiagaraEmitterHandle`; uses the
  handle's resolved version GUID.
- Materials via the base-class virtual `GetUsedMaterials(nullptr, OutMaterials)`.
  Passing `nullptr` for the FNiagaraEmitterInstance is fine for editor-time
  inspection — returns configured materials (not per-instance dynamic overrides).
- Mesh special case: cast to `UNiagaraMeshRendererProperties` and iterate the
  `Meshes` UPROPERTY array of `FNiagaraMeshRendererMeshProperties` (defined in
  `Public/NiagaraMeshRendererMeshProperties.h`, field `Mesh : TObjectPtr<UStaticMesh>`).
- No new Build.cs dependency — all types are in the `Niagara` module already pulled in by P0.

### Why not per-renderer-class field dumps

Each renderer class has 10–30 specific UPROPERTYs (Sprite alignment / facing /
sort mode; Ribbon tessellation; Light radius/intensity; Decal sort order;
etc.). Surfacing them generically would require either reflection-based
property walking (P2) or per-class handlers (high maintenance for low value).
For the "what assets does this sample use" workflow, `materials + meshes` is
the 80/20 answer; finer details can be inspected by opening the asset.

## Files

- `Source/UnrealMCP/Public/Commands/UnrealMCPNiagaraCommands.h` (+2 handlers)
- `Source/UnrealMCP/Private/Commands/UnrealMCPNiagaraCommands.cpp` (+2 handlers, +6 includes)
- `Source/UnrealMCP/Private/UnrealMCPBridge.cpp` (route — expand the else-if condition)
- `Python/tools/niagara_tools.py` (+2 mcp.tool definitions)

## UE 5.7 Source References

- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraEmitterHandle.h`
  — `GetEmitterData()` (L86), `GetIsEnabled()` (L62)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraEmitter.h`
  — `FVersionedNiagaraEmitterData::GetRenderers()` (L382)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraRendererProperties.h`
  — `GetIsEnabled()` (L427), `GetUsedMaterials(...)` PURE_VIRTUAL (L324)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraMeshRendererProperties.h`
  — `TArray<FNiagaraMeshRendererMeshProperties> Meshes` (L195)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraMeshRendererMeshProperties.h`
  — `TObjectPtr<UStaticMesh> Mesh` (L75)

## Verification (✅ passed 2026-06-02)

Built, restarted, exercised three renderer classes end-to-end:

- `list_niagara_systems("/Game/Examples/Forest_VFX/Niagara")` → 18 NS_* assets
  with full asset path + name + package path.
- **Sprite** `NS_AreaBuff :: Floor001` → 1 `NiagaraSpriteRendererProperties`,
  materials `[MI_Floor_Basic_7]`.
- **Mesh** `NS_RoundedVine_Forest :: vine` → 1 `NiagaraMeshRendererProperties`,
  materials `[MI_Vines_Growth_Forest2]`, meshes `[SM_Vine_Upaxis_02]`. ✅ Mesh
  array extraction working.
- **Ribbon** `NS_Ribbon_Nature :: Trail001` → 1 `NiagaraRibbonRendererProperties`,
  materials `[M_LaserTwist]`. ✅ Base-class `GetUsedMaterials` covers ribbon
  uniformly with no extra code.

## LT12 Status After P1

- P0 `get_niagara_system_info`: ✅ tested
- P1 `list_niagara_systems` / `get_niagara_emitter_renderers`: ✅ tested
- P2 / not started: per-renderer-class detail (reflection dump), per-emitter
  module stack — deferred indefinitely per "high cost, low ROI" assessment.
