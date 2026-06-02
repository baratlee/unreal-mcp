# Niagara System Read P0 (LT12)

Date: 2026-06-02
Author: Claude Code (LEOCC)

## Why

UnrealMCP had **zero Niagara read capability** — Niagara System / Emitter
assets could not be inspected at all (only listed under "❌ cannot do" in the
capability boundary). The user needs to read sample Niagara effects' basic
info, **especially the exposed parameters (User Parameters)**, to reference
them when authoring effects, without opening each asset by hand in the editor.

## New Tools (1)

- `get_niagara_system_info(asset_path)` — inspect a `UNiagaraSystem`:
  - basic info: `num_emitters` + `emitters[]` (each `{name, enabled}`)
  - `exposed_parameters[]` — the system's User Parameters (the values exposed
    to Blueprint / spawner / sequencer). Each entry:
    `{name, type, is_data_interface, is_uobject, value_kind, default_value?}`
  - `exposed_parameter_count`

### exposed parameter value semantics

- `value_kind = "parsed"` → `default_value` holds the concrete value:
  - float / int → plain number; bool → boolean
  - Vector2/3/4, Position, Quat, LinearColor → float array
    (`[x,y]` / `[x,y,z]` / `[x,y,z,w]` / `[r,g,b,a]`)
- `value_kind = "type_only"` → DataInterface / UObject / custom struct / enum;
  only `type` is reported, no `default_value` field. (P0 scope: scalar +
  common vector parsing only, per user decision.)

## Implementation Notes

- Exposed params source: `UNiagaraSystem::GetExposedParameters()` →
  `FNiagaraUserRedirectionParameterStore`. Listed via
  `GetUserParameters(TArray<FNiagaraVariable>&)` — returns the clean user
  variable list (without the internal `User.` redirection prefix).
- Value read: `FNiagaraParameterStore::GetParameterValue<T>(Var)`. Type is
  matched against `FNiagaraTypeDefinition::Get*Def()` statics. Niagara sim
  storage is float-based (FVector3f etc.), so T is the `f` POD variant; bool
  is a 4-byte FNiagaraBool read as int32 (`!= 0`).
- Emitter list: `GetEmitterHandles()` → `FNiagaraEmitterHandle::GetName()` /
  `GetIsEnabled()`.
- Build.cs: added `"Niagara"` to `PrivateDependencyModuleNames` (runtime
  module, not editor-gated). `.uplugin` Plugins list gained a `Niagara`
  `Enabled:true` entry for consistency with the other plugin deps.

## Files

- `Source/UnrealMCP/Public/Commands/UnrealMCPNiagaraCommands.h` (new)
- `Source/UnrealMCP/Private/Commands/UnrealMCPNiagaraCommands.cpp` (new)
- `Source/UnrealMCP/Public/UnrealMCPBridge.h` (include + member)
- `Source/UnrealMCP/Private/UnrealMCPBridge.cpp` (include + ctor + dtor + route)
- `Source/UnrealMCP/UnrealMCP.Build.cs` (+Niagara module)
- `UnrealMCP.uplugin` (+Niagara plugin dep)
- `Python/tools/niagara_tools.py` (new)
- `Python/unreal_mcp_server.py` (import + register)

## UE 5.7 Source References

- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraSystem.h`
  — `GetExposedParameters()` (L364), `GetEmitterHandles()` (L309)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraUserRedirectionParameterStore.h`
  — `GetUserParameters()` (L32)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraParameterStore.h`
  — `GetParameterValue<T>()` (L403)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Public/NiagaraTypes.h`
  — type defs (L1025-1042), `GetName()/GetType()/IsDataInterface()/IsUObject()`

## Verification (✅ passed 2026-06-02)

Built Development Editor | Win64, restarted editor, tested two sample systems:

- `/Game/Examples/Forest_VFX/Niagara/AreaBuff/NS_AreaBuff` — 12 emitters
  (incl. one `enabled:false`), 9 exposed params all `value_kind="parsed"`:
  LinearColor → `[r,g,b,a]`, NiagaraFloat → scalar, Vector3f → `[x,y,z]`.
- `/Game/Examples/Forest_VFX/Niagara/Projectile/NS_Projectile_Grenade_Nature`
  — 3 emitters, 13 exposed params all parsed correctly.

## Known Pitfall — unity build helper name clash

UE unity build merges multiple `*Commands.cpp` into one translation unit, so
**anonymous-namespace helpers with the same name across files collide** (C2084
"already has a body"). The first cut hit this against Material's
`LoadAssetWithFallback`. Fix: all Niagara helpers are prefixed
(`NiagaraLoadAssetWithFallback` / `NiagaraFloatArrayToJson` /
`NiagaraParseParameterValue`). **Any future commands cpp must module-prefix its
anonymous-namespace helpers.**

## LT12 Status After P0

- P0 `get_niagara_system_info`: **✅ compiled + tested, working**.
- Possible P1 (not started): `list_niagara_systems`, `get_niagara_emitter_info`
  (per-emitter modules / renderers). Deferred per user decision.
