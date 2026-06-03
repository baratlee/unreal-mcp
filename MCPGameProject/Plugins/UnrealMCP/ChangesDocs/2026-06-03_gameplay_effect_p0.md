# GameplayEffect Read + Create P0 (LT14)

Date: 2026-06-03
Author: Claude Code (LEOCC)

## Why

UnrealMCP had **zero GameplayEffect (GE) capability** — GE blueprint assets could
not be inspected or created from outside the editor. LT11 GAS framework rewrite
needs to read/audit existing GE configs (Duration / Modifier / Tags) and
batch-create new ones; doing this by hand in the editor is not sustainable.

## New Tools (2)

### `get_gameplay_effect_info(asset_path)`

Reads the CDO of a GE Blueprint. Accepts `/Game/X/BP_GE_Foo` or
`/Game/X/BP_GE_Foo.BP_GE_Foo` form. Output:

- `asset_path`, `class_name`, `parent_class`
- `duration_policy` — Instant / Infinite / HasDuration
- `duration_magnitude`, `max_duration_magnitude` (only for HasDuration)
- `period` (only for non-Instant), `execute_periodic_effect_on_application`
- `modifiers[]` — each `{attribute, modifier_op, magnitude, source/target tags}`
- `executions_count`, `gameplay_cues_count` (details deferred to P2)
- `stacking{ type, limit_count, duration_refresh_policy, period_reset_policy,
  expiration_policy, factor_in_stack_count }`
- `inherited_tags.granted_tags` / `.asset_tags` — `{added, removed, combined}`
  read via UE 5.3+ Component model (`UTargetTagsGameplayEffectComponent`,
  `UAssetTagsGameplayEffectComponent`); falls back to GE cached tags when the
  Component is absent
- `inherited_tags.blocked_ability_tags_combined`
- `components[]` — list of `UGameplayEffectComponent` subclass names attached

#### Magnitude detail level

- `ScalableFloat` → `{ calculation: "ScalableFloat", scalable_float: { value } }`
  (value at level 0; full Curve/Registry deferred to P1 ScalableFloat parsing)
- `SetByCaller` → `{ calculation: "SetByCaller", set_by_caller: { data_name?, data_tag? } }`
- `AttributeBased` / `CustomCalculationClass` → only `calculation` field; inner
  struct fields are `protected` with no public getter, deferred to P2.

### `create_gameplay_effect(asset_path, asset_name, parent_path?)`

Creates a new Blueprint subclass of UGameplayEffect (or any GE subclass).

- `asset_path` — folder, e.g. `/Game/GAS/Effects`
- `asset_name` — blueprint name without extension, e.g. `BP_GE_Heal`
- `parent_path` — optional; defaults to `/Script/GameplayAbilities.GameplayEffect`.
  Accepts native class path (`/Script/...`) or blueprint asset path.
- Resulting blueprint is **not** auto-compiled or auto-saved (caller follows up
  with `compile_blueprint` + `save_dirty_assets`, per Material/Spline
  convention).

## Implementation Notes

- Asset CDO retrieval: `LoadObject<UBlueprint>` → `GeneratedClass` →
  `GetDefaultObject<UGameplayEffect>()`. Falls back to direct `UClass` load
  for native subclasses.
- Tag Component access uses public inline getters
  `GetConfiguredTargetTagChanges()` / `GetConfiguredAssetTagChanges()` (UE 5.7).
- Component enumeration: `GEComponents` is `protected`; we use
  `ForEachObjectWithOuter(GE, …, false)` and filter by
  `IsA<UGameplayEffectComponent>()` to list attached components without
  reaching into private members.
- `StackingType` field is deprecated in 5.7 but still the canonical CDO storage;
  read wrapped in `PRAGMA_DISABLE_DEPRECATION_WARNINGS`.
- Enum → string serialization uses `StaticEnum<T>()->GetNameStringByValue()` for
  all GE enums (no manual switch maintenance).

## Files

- `Source/UnrealMCP/Public/Commands/UnrealMCPGameplayEffectCommands.h` (new)
- `Source/UnrealMCP/Private/Commands/UnrealMCPGameplayEffectCommands.cpp` (new)
- `Source/UnrealMCP/Public/UnrealMCPBridge.h` (include + member)
- `Source/UnrealMCP/Private/UnrealMCPBridge.cpp` (include + ctor + dtor + route)
- `Source/UnrealMCP/UnrealMCP.Build.cs`
  - `+GameplayAbilities` in `PublicDependencyModuleNames`
  - `GameplayTags` moved from editor-only `PrivateDependencyModuleNames` to
    `PublicDependencyModuleNames` (it's now needed by runtime GE types too)
- `UnrealMCP.uplugin` (+`GameplayAbilities` plugin dep)

## UE 5.7 Source References

- `Engine/Plugins/Runtime/GameplayAbilities/Source/GameplayAbilities/Public/GameplayEffect.h`
  — UGameplayEffect L2096, FGameplayModifierInfo L542, FGameplayEffectModifierMagnitude L276,
  FInheritedTagContainer L630, EGameplayEffectMagnitudeCalculation L67
- `…/GameplayEffectComponents/TargetTagsGameplayEffectComponent.h` — getter L25, setter L28
- `…/GameplayEffectComponents/AssetTagsGameplayEffectComponent.h` — getter L25, setter L28

## Key Architectural Note (UE 5.3+ Component Model)

UGameplayEffect was refactored to move tag / requirement / immunity / granted
abilities data out of the monolithic CDO and into instanced
`UGameplayEffectComponent` subobjects (`GEComponents[]`). Fields like
`InheritableGameplayEffectTags` / `InheritableOwnedTagsContainer` /
`OngoingTagRequirements` / `Immunity*` are now **deprecated** on UGameplayEffect
and live on dedicated Components instead. P0 reads tags through the Component
getters; the corresponding writers in P1 will go through
`SetAndApplyXxxTagChanges`.

## Known Pitfall — unity build helper name clash

All anonymous-namespace helpers are prefixed with `GE` (e.g.
`GELoadAssetWithFallback`, `GEEnumToString`) to avoid the same C2084 collision
hit by Niagara P0 against Material's `LoadAssetWithFallback`. Any future
commands.cpp must follow this convention.

## Verification

Awaiting user-driven Development Editor | Win64 build (per project convention
"不自动编译"). Test plan:

1. `get_gameplay_effect_info` on an existing third-party GE
   (`/Game/Examples/.../GE_*`)  → verify duration/modifier/stacking shape
2. `create_gameplay_effect("/Game/GAS/Effects", "BP_GE_TestHeal")` → verify new
   blueprint appears in Content Browser, parent class = GameplayEffect
3. `get_gameplay_effect_info` on the freshly created blueprint → verify defaults
4. Manual `compile_blueprint` + `save_dirty_assets` to persist
