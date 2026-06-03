# GameplayEffect Write P1 (LT14)

Date: 2026-06-03
Author: Claude Code (LEOCC)

## Why

Building on P0 (read + create). P1 lands the core write surface so MCP can
batch-author / batch-tune GE assets without manual editor work: top-level
duration/period/stacking fields, the Modifiers array CRUD, and the inherited
tag channels (Granted + Asset) via UE 5.3+ Component model.

Scope confirmed with user (2026-06-03): P1 Magnitude calculation support is
**ScalableFloat + SetByCaller only**; AttributeBased / CustomCalculationClass
remain deferred (P2).

## New Tools (5)

### `set_gameplay_effect_property(asset_path, property, value)`

Top-level CDO field setter. Supported `property` values:

| property | value type | notes |
|---|---|---|
| `duration_policy` | enum string | `Instant` / `HasDuration` / `Infinite` |
| `duration_magnitude` | magnitude object | see Magnitude schema below |
| `max_duration_magnitude` | magnitude object | same |
| `period` | scalable_float object | `{ value, curve_table?, row_name? }` |
| `execute_periodic_effect_on_application` | bool | |
| `stacking_type` | enum string | `None` / `AggregateBySource` / `AggregateByTarget` |
| `stack_limit_count` | int | -1/0 = no limit |
| `stack_duration_refresh_policy` | enum string | |
| `stack_period_reset_policy` | enum string | |
| `stack_expiration_policy` | enum string | |
| `factor_in_stack_count` | bool | |

### `add_gameplay_effect_modifier(asset_path, attribute, modifier_op, magnitude)`

Append a new `FGameplayModifierInfo` to `Modifiers[]`. Returns the new index.

- `attribute` — `"ClassName.PropertyName"`, e.g. `"KLAttributeSet.Health"`.
  Class must already be loaded.
- `modifier_op` — `Additive` / `Multiplicitive` / `Division` / `Override`.
- `magnitude` — see schema below.

### `remove_gameplay_effect_modifier(asset_path, index)`

### `set_gameplay_effect_modifier(asset_path, index, attribute?, modifier_op?, magnitude?)`

Partial update; any of the three fields can be omitted to keep existing value.

### `set_gameplay_effect_inherited_tags(asset_path, granted?, asset?)`

Writes to Granted Tags / Asset Tags channels via Component model.

- `granted` / `asset` — each `{ added: [tag strings], removed: [tag strings] }`.
  Missing channel = no change. Missing `added` or `removed` inside a channel =
  keep existing value (only the side you provide gets overwritten).
- Implementation: `FindOrAddComponent<UTargetTagsGameplayEffectComponent>()` /
  `FindOrAddComponent<UAssetTagsGameplayEffectComponent>()` → mutate copy of
  `FInheritedTagContainer` → `SetAndApplyXxxTagChanges()` (handles
  `UpdateInheritedTagProperties` + Cached* sync internally).
- Tags must be registered with the GameplayTags subsystem; unregistered tags
  are silently dropped from the input (rather than erroring) to keep batch
  scripts robust.

## Magnitude Object Schema

```jsonc
// ScalableFloat — nested form
{ "calculation": "ScalableFloat", "scalable_float": { "value": 12.5, "curve_table": "/Game/.../CT_Damage", "row_name": "Fireball" } }
// ScalableFloat — flat form (no nested object needed)
{ "calculation": "ScalableFloat", "value": 12.5 }
// SetByCaller — nested
{ "calculation": "SetByCaller", "set_by_caller": { "data_tag": "Data.Damage" } }
// SetByCaller — by name instead of tag
{ "calculation": "SetByCaller", "set_by_caller": { "data_name": "Damage" } }
```

`AttributeBased` and `CustomCalculationClass` are explicitly rejected with a
helpful error message ("P1 supports ScalableFloat + SetByCaller only"). P2.

## Implementation Notes

- Enum parsing tolerates both fully-qualified (`EGameplayEffectDurationType::Instant`)
  and short (`Instant`) forms, case-insensitive on the short form.
- `EGameplayModOp::Type` is a legacy namespaced enum; reflected as the
  `EGameplayModOp` UEnum.
- All writes mark the owning UBlueprint via
  `FBlueprintEditorUtils::MarkBlueprintAsModified` + Package dirty. CDO-only
  edits don't need `MarkBlueprintAsStructurallyModified` (that's for graphs).
- **Not auto-compiled / not auto-saved** — caller follows up with
  `compile_blueprint` + `save_dirty_assets` (per Material/Spline/Niagara
  convention).
- `StackingType` write wrapped in deprecation pragma (deprecated in 5.7 but
  still the canonical storage; the deprecation only affects external access).
- For `set_gameplay_effect_inherited_tags`, we **read the current container
  first**, then patch only the provided side (added/removed) before calling
  `SetAndApplyXxxTagChanges`. This means callers can update one side without
  wiping the other.

## Files

- `Source/UnrealMCP/Public/Commands/UnrealMCPGameplayEffectCommands.h`
  (+5 P1 handler declarations)
- `Source/UnrealMCP/Private/Commands/UnrealMCPGameplayEffectCommands.cpp`
  (+P1 helpers `GEParseEnum/GEParseModifierOp/GEParseAttribute/GEParseScalableFloat/GEParseMagnitude/GEParseTagArray/GEMarkBlueprintModified`,
   +5 handler implementations, +include `Kismet2/BlueprintEditorUtils.h`)
- `Source/UnrealMCP/Private/UnrealMCPBridge.cpp`
  (extend the GE route block to cover 5 new command types)

No additional module / plugin deps over what P0 added (`GameplayAbilities` +
`GameplayTags` already in Build.cs).

## Verification

Awaiting user-driven Development Editor | Win64 build. Suggested test plan:

1. Create a fresh GE via P0 `create_gameplay_effect`.
2. `set_gameplay_effect_property(duration_policy="HasDuration")` → re-read,
   verify policy.
3. `set_gameplay_effect_property(duration_magnitude={ScalableFloat,value=5})` →
   verify duration shows 5s in editor.
4. `add_gameplay_effect_modifier(attribute="KLAttributeSet.Health", modifier_op="Additive", magnitude={ScalableFloat, value=-10})` → verify modifier appears.
5. `set_gameplay_effect_modifier(index=0, magnitude={SetByCaller, data_tag="Data.Damage"})` → verify SetByCaller swap.
6. `remove_gameplay_effect_modifier(index=0)` → verify cleanup.
7. `set_gameplay_effect_inherited_tags(granted={added:["GameplayCue.Burning"]})` →
   verify TargetTagsGameplayEffectComponent attached + tag in Added.
8. `compile_blueprint` + `save_dirty_assets` → reload editor → state persists.

## Known Limitations (carry from P0)

- CDO editing; doesn't affect in-flight FActiveGameplayEffect instances
  (those re-create from spec when GE is re-applied).
- Tag-array overwrite semantics: providing `added` replaces the full Added
  container, not "append to existing". To incrementally add tags, the caller
  must first read via `get_gameplay_effect_info` and merge client-side.

## LT14 Status After P1

- ✅ P0 + P1 code complete, awaiting compile + manual test
- ❌ P2 deferred: Executions / GameplayCues / TagRequirements / GrantedAbilities
  / Immunity / RemoveGameplayEffectsWithTags / Magnitude AttributeBased &
  CustomCalculationClass (per scope decision 2026-06-03)
