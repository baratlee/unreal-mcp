# GameplayEffect Write P2 (LT14)

Date: 2026-06-03
Author: Claude Code (LEOCC)

## Why

Building on P0+P1 (read + create + duration/period/stacking/modifier/inherited-tags).
P2 lands the remaining writes that LT11 GAS framework rewrite hits when authoring
production GEs (Damage / Cue / Cooldown / Buff): GameplayCue array, Tag
Requirements component, AttributeBased magnitude, GrantedAbilities, plus
listing/delete/chance utilities and modifier source/target tag filter support.

Scope confirmed with user (2026-06-03 reply "p2和P2.5一起进行"):
- ✅ Cue CRUD / TagReqs / AttributeBased / list / delete / ChanceToApply / GrantedAbility CRUD / modifier source-target tags / get_info expansion
- ❌ Immunity / RemoveOther moved to P3 — both back onto `TArray<FGameplayEffectQuery>`
  which is significantly more complex than tag containers and warrants its own pass
- ❌ Executions / CustomCalculationClass magnitude / AdditionalEffects deferred to P3

## New Tools (10)

### `list_gameplay_effects(path_filter?)`
Scans Asset Registry for blueprints whose ParentClass is or derives from
`UGameplayEffect`. Returns `{asset_path, name, package_path, parent_class}`
per hit. Optional `path_filter` is a recursive package path (e.g. `/Game/GAS`).

### `delete_gameplay_effect(asset_path)`
Hard-deletes a GE blueprint (refuses on native UGameplayEffect — only blueprint
assets). Uses `ObjectTools::ForceDeleteObjects`.

### `add_gameplay_effect_cue(asset_path, cue_tags, min_level?, max_level?, magnitude_attribute?)`
Appends a `FGameplayEffectCue` to `GameplayCues[]`. `cue_tags` is an array of
tag strings; level range and magnitude_attribute are optional. Returns
`{index, cues_count}`.

### `remove_gameplay_effect_cue(asset_path, index)`
### `set_gameplay_effect_cue(asset_path, index, [cue_tags?, min_level?, max_level?, magnitude_attribute?])`
Partial update by index.

### `set_gameplay_effect_tag_requirements(asset_path, application?, ongoing?, removal?)`
Writes to `UTargetTagRequirementsGameplayEffectComponent` (UE 5.3+). Each
channel is `{required: [tag strings], ignored: [tag strings]}`. Missing channel
or missing inner field = keep existing.

### `set_gameplay_effect_chance_to_apply(asset_path, scalable_float? | value?)`
Writes to `UChanceToApplyGameplayEffectComponent`. Accepts a `{value, curve_table?, row_name?}`
object via `scalable_float` field, or a flat `{value: number}`, or a top-level
number. Uses `SetChanceToApplyToTarget()`.

### `add_gameplay_effect_granted_ability(asset_path, ability_class, level?, input_id?, removal_policy?)`
Appends to `UAbilitiesGameplayEffectComponent::GrantAbilityConfigs` via the
public `AddGrantedAbilityConfig()`. `ability_class` accepts Blueprint asset path
(`/Game/.../GA_Foo`) or native class path (`/Script/Module.GA_Foo`).

### `remove_gameplay_effect_granted_ability(asset_path, index)`
### `set_gameplay_effect_granted_ability(asset_path, index, [ability_class?, level?, input_id?, removal_policy?])`
Both go through `FScriptArrayHelper` reflection on `GrantAbilityConfigs`
(field is `protected` with no public remove/set API — reflection is the only
hatch).

## Extensions to Existing Tools (3)

### `add_gameplay_effect_modifier` / `set_gameplay_effect_modifier`
Both now accept optional `source_tags` and `target_tags`, each `{required, ignored}`.
Writes to `FGameplayModifierInfo::SourceTags` and `TargetTags` (which are
`FGameplayTagRequirements`). Read end was already returning these fields in P0;
this closes the symmetry.

### Magnitude AttributeBased calculation
`add/set_gameplay_effect_modifier` and `set_gameplay_effect_property` magnitude
params now accept `calculation: "AttributeBased"` with full `FAttributeBasedFloat`
schema:

```jsonc
{
  "calculation": "AttributeBased",
  "attribute_based": {
    "coefficient": { "value": 1.5 },
    "pre_multiply_additive_value": 0,
    "post_multiply_additive_value": 0,
    "backing_attribute": {
      "attribute": "KLAttributeSetBase.Strength",
      "source": "Source",        // or "Target"
      "snapshot": true
    },
    "attribute_curve": { "curve_table": "/Game/.../CT_X", "row_name": "Foo" },
    "attribute_calculation_type": "AttributeMagnitude",
    "final_channel": "Channel0",
    "source_tag_filter": ["Some.Tag"],
    "target_tag_filter": []
  }
}
```

Each ScalableFloat-typed inner field accepts either the nested object form or
a bare number. Read path uses reflection to peek at the (protected) inner
`AttributeBasedMagnitude` field on `FGameplayEffectModifierMagnitude`.

### `get_gameplay_effect_info` output expansion
Added fields:
- `gameplay_cues[]` (was `gameplay_cues_count`) — full FGameplayEffectCue detail
- `tag_requirements{application, ongoing, removal}` — each `{required, ignored}`
- `chance_to_apply` — ScalableFloat (only present if component attached)
- `granted_abilities[]` — full FGameplayAbilitySpecConfig detail via reflection
- `immunity_queries_count`, `remove_other_queries_count` — counts only (P3 for
  detail)
- Modifier magnitude objects now include `attribute_based{...}` when applicable

## Implementation Notes

- **AttributeBased read via reflection**: `FGameplayEffectModifierMagnitude` has
  `protected: FAttributeBasedFloat AttributeBasedMagnitude;` with no public
  getter. Solution: `FGameplayEffectModifierMagnitude::StaticStruct()
  ->FindPropertyByName("AttributeBasedMagnitude")` → cast to `FStructProperty`
  → `ContainerPtrToValuePtr<FAttributeBasedFloat>`. Same pattern would work for
  any other protected magnitude variant if needed later.
- **GrantedAbilities CRUD via reflection**: `UAbilitiesGameplayEffectComponent::GrantAbilityConfigs`
  is `protected` with only a public `AddGrantedAbilityConfig` adder. Remove/set
  by index go through `FArrayProperty` + `FScriptArrayHelper` (helper-bound to
  the component's container pointer) and `reinterpret_cast<FGameplayAbilitySpecConfig*>`
  on `GetRawPtr(Index)`.
- **TagRequirementsComponent fields are public** — no reflection needed; direct
  field write + `MarkBlueprintAsModified` is sufficient (component handles
  reapplication via `OnGameplayEffectChanged`).
- **list_gameplay_effects uses Asset Registry's `ParentClass`/`NativeParentClass`
  tag** — no asset loading required. Substring match on `"GameplayEffect"`;
  loose but covers blueprint GEs, native subclasses, and grand-children.
- **delete_gameplay_effect** refuses to act on native classes (only blueprint
  assets reach this path safely). Uses `ObjectTools::ForceDeleteObjects` with
  no confirmation prompt.
- All writes mark blueprint modified; no auto-compile / no auto-save (same
  convention as P0/P1).

## Files

- `Source/UnrealMCP/Public/Commands/UnrealMCPGameplayEffectCommands.h`
  (+10 P2 handler decls)
- `Source/UnrealMCP/Private/Commands/UnrealMCPGameplayEffectCommands.cpp`
  - +includes: `TargetTagRequirements/ChanceToApply/Abilities/Immunity/RemoveOther`
    GE components, `Abilities/GameplayAbility.h`, `GameplayAbilitySpec.h`,
    `AssetRegistry/ARFilter`, `ObjectTools.h`, reflection helpers
  - +helpers: `GEParseTagRequirements/GETagRequirementsToJson`,
    `GEParseAttributeBasedFloat/GEAttributeBasedFloatToJson`,
    `GEParseGameplayEffectCue/GECueToJson`,
    `GEParseAbilitySpecConfig/GEAbilitySpecConfigToJson`,
    `GEFindGrantAbilityConfigsProp`, `GEParseScalableFloatFromValue`
  - +`GEParseMagnitude` AttributeBased branch; +`GEMagnitudeToJson` AttributeBased read
  - +10 handler impls
  - `HandleAddGameplayEffectModifier` / `HandleSetGameplayEffectModifier` extended
    with `source_tags` / `target_tags` parsing
  - `HandleGetGameplayEffectInfo` extended with cues / tag_reqs / chance /
    granted_abilities / immunity+remove counts
- `Source/UnrealMCP/Private/UnrealMCPBridge.cpp` route block now lists all 17 GE
  commands (P0+P1+P2)

No new Build.cs / uplugin deps needed (`UnrealEd` already provides ObjectTools).

## Verification

Awaiting user-driven Development Editor | Win64 build. Suggested test plan:

1. `list_gameplay_effects(path_filter="/Game/Examples/Demo")` → expect at least
   `GE_Heal_Test`, `GE_StaminaCost`, `GE_LT14_TestProbe`
2. `add_gameplay_effect_cue` on TestProbe with tags `[GameplayCue.Hit.Generic]`
3. `set_gameplay_effect_tag_requirements` application `{required:[KL.Combat.State.Invincible]}` → re-read, confirm TagRequirementsComponent attached
4. `set_gameplay_effect_chance_to_apply` with `{value:0.5}` → re-read
5. `add_gameplay_effect_granted_ability` with a real GA blueprint path → confirm `granted_abilities[]` non-empty
6. Modifier with `AttributeBased` magnitude → re-read → confirm 7-field round-trip
7. `add_gameplay_effect_modifier` with `source_tags: {required:[Some.Tag]}` →
   re-read modifier shows it
8. `delete_gameplay_effect` on TestProbe at end → asset gone from CB

## LT14 Status After P2

- ✅ P0 + P1 + P2 code complete (17 commands total)
- ❌ P3 remainder (per scope decision):
  - Immunity component (`TArray<FGameplayEffectQuery>`)
  - RemoveOther component (`TArray<FGameplayEffectQuery>`)
  - Executions full detail (`FGameplayEffectExecutionDefinition`)
  - CustomCalculationClass magnitude
  - AdditionalEffects (chain GEs)
