# PostEditChange Notification Fix (Instance Override Persistence Bug)

Date: 2026-06-08
Author: Claude Code (LEOCC)

## TL;DR

MCP setter tools were writing property values directly via reflection
(`SetPropertyValue` / `SetPropertyValue_InContainer` / `ImportText_Direct`)
without ever calling `Object->PostEditChangeProperty(...)` /
`Object->Modify()` / Blueprint dirty-marking. Side effect: per-instance
overrides on actor/component properties were silently dropped on level
reload, BP reinstantiation, or PIE start — the value was written to disk
but UE's reactive instancing machinery didn't recognize it as a
user-authored override and reverted to CDO defaults.

Fix: a single helper `FUnrealMCPCommonUtils::NotifyPropertyChanged`
centralizes the post-write notification chain and is invoked from every
write callsite that touches a UObject's editable property.

## How the bug surfaced (BP_Quad_Enemy)

A user placed a `BP_Quad2_C` actor in `StoneBoss.umap` as an enemy.
After changing `BP_Quad2` CDO's `AIControllerClass` to `AIC_Companion_C`
and writing a per-instance override `AIControllerClass = AIC_Combat_C`
back on the map actor via `set_actor_property`, the override looked
applied (yellow reset arrow visible in Details, `Ctrl+S` reported saved),
but after switching maps and switching back the value was always
`AIC_Companion_C` — the override was gone.

Manual edit in the Details panel persisted across the same map switch.
The differential between MCP write and manual edit pinpointed the
missing `PostEditChangeProperty` call.

## Root cause

`FUnrealMCPCommonUtils::SetObjectProperty` (the central reflection-based
setter consumed by `set_actor_property`, `set_blueprint_property`,
`set_data_asset_property` and several others) wrote property bytes
directly and returned. None of:

- `Object->PreEditChange(Property)`
- `Object->PostEditChangeProperty(FPropertyChangedEvent(Property, ValueSet))`
- `Object->Modify()`

was ever invoked. Several command files outside `SetObjectProperty` also
did direct writes via `FScriptArrayHelper` / `ImportText_Direct` /
typed `SetPropertyValue_InContainer` calls without that bridge.

UE's BP reinstantiation pass (triggered by CDO changes, recompiles,
level reload, PIE start) walks instances and propagates new CDO values
to properties that aren't marked as user-edited overrides. Without the
PostEditChange call, the BP reinstancer treated the MCP-written value
as "happens to differ from CDO but never declared as override" and reset
it back to the CDO default.

## Fix

### New helper

`FUnrealMCPCommonUtils::NotifyPropertyChanged(UObject* Owner, FProperty* Prop, EPropertyChangeType::Type ChangeType = ValueSet)`
([CommonUtils.h:62](../Source/UnrealMCP/Public/Commands/UnrealMCPCommonUtils.h)).

Behavior:

- `Owner->Modify()` — transactional / dirty marking.
- If `Prop != nullptr` → `Owner->PostEditChangeProperty(FPropertyChangedEvent(Prop, ChangeType))`.
  If `Prop == nullptr` → `Owner->PostEditChange()` (fallback for handlers
  that mutate many properties in one call).
- If `Owner` is a `UActorComponent` → mirror the same PEC onto the
  owning actor (BP reinstancer keys off the Actor's PEC for nested
  component instance overrides).
- If `Owner` is a Blueprint Generated Class's CDO → also
  `FBlueprintEditorUtils::MarkBlueprintAsModified(BP)` so the BP itself
  is dirty and the changed CDO state is preserved across recompile.

### Refactor of `SetObjectProperty`

Rewrote the type-dispatch (`FBoolProperty` / `FIntProperty` /
`FFloatProperty` / `FStrProperty` / `FByteProperty` /
`FEnumProperty` / fallback `ImportText_Direct`) inside a `do { ... } while (false)`
block. Each success path sets `bWritten = true; break;` instead of
`return true;`. The function exits through a single `NotifyPropertyChanged(Object, Property)`
call before returning success. Error paths still early-return without
notifying. 12 success sites converge to 1 notify site.

### Callsite patches outside `SetObjectProperty`

| File | Notes |
|---|---|
| `UnrealMCPStateTreeCommands.cpp` | 6 handlers (`add_state_tree_state`, `remove_state_tree_state`, `set_state_tree_state_property`, `add_state_tree_task`, `add_state_tree_transition`, `set_state_tree_node_property`) gained `NotifyPropertyChanged(StateTree, nullptr)` before `StateTree->GetPackage()->MarkPackageDirty()` — the StateTree asset is the outer that needs PEC even when mutations happen on nested `FInstancedStruct` data. |
| `UnrealMCPBlueprintCommands.cpp` | Three direct-write sites in the AnimGraph node setter: the main `bAnyChange` branch (replaces bare `AnimNode->Modify()` with `NotifyPropertyChanged(AnimNode, nullptr)`); the `property_binding` set path (post-`AddPair`/`*Existing = *NewBindData`); and the `clear_binding` path (post-`RemovePair`) — all three now notify the BindObj subobject so AnimGraph compile picks up the binding change. |
| `UnrealMCPGameplayEffectCommands.cpp` | Local helper `GEMarkBlueprintModified` upgraded from `(UBlueprint* BP)` to `(UBlueprint* BP, UObject* WrittenObject = nullptr)`. When `WrittenObject` is passed (typically the GE CDO), it routes through `NotifyPropertyChanged` before `MarkBlueprintAsModified`. All 13 callsites were updated to `GEMarkBlueprintModified(BP, GE)`. |
| `UnrealMCPAnimationCommands.cpp` | ~50 direct writes touching `Database` (PoseSearchDatabase), `Schema` (PoseSearchSchema), `Table` (ChooserTable), `AnimSeq` (AnimSequence), `IA` (InputAction), `IMC` (InputMappingContext), `Ctrl->GetAsset()` (IK Rig / Retargeter via UIKRigController). Every `XXX->MarkPackageDirty()` is now preceded by `NotifyPropertyChanged(XXX, nullptr)`. Five Animation Notify handlers were already using `Object->PostEditChange()` and were left untouched. |

### Files not modified (intentional)

- `UnrealMCPBlueprintNodeCommands.cpp` — already routes through
  `FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint)` for graph
  edits (the right primitive for BP graph mutations).
- `UnrealMCPSplineCommands.cpp` — same pattern, all 7 handlers already
  call `MarkBlueprintAsModified`.
- `UnrealMCPUMGCommands.cpp` — uses `FKismetEditorUtilities::CompileBlueprint`
  (stronger than PEC; full recompile path).
- `UnrealMCPMaterialCommands.cpp`, `UnrealMCPNiagaraCommands.cpp`,
  `UnrealMCPProjectCommands.cpp` — read/serialize only, no write paths
  affected.
- A handful of `Package->MarkPackageDirty()` sites in `Animation` are
  brand-new asset creation flows where `FAssetRegistryModule::AssetCreated`
  is the correct notification — no PEC needed.

## Activation

The plugin must be recompiled (Visual Studio, Development Editor | Win64).
Until the new DLL loads, the runtime behavior is unchanged and the old
workaround applies — actor/component instance overrides on properties
that previously dropped must be edited manually via Details panel.

## Validation

After recompile, repeat the trigger scenario:

1. With a placed BP_Quad2_C actor in the level, change `BP_Quad2` CDO's
   `AIControllerClass` to `AIC_Companion_C`.
2. Call `set_actor_property` on the placed actor with `AIControllerClass = AIC_Combat_C`.
3. `Ctrl+S` the level.
4. Switch to a different map, switch back.
5. Confirm the placed actor's `AIControllerClass` is still `AIC_Combat_C`
   and the Details panel shows the reset arrow (override marker).

Equivalent checks apply to `set_component_property`, `set_state_tree_node_property`,
GameplayEffect setters, and Animation setters.

### Validation result (2026-06-09)

The BP_Quad_Enemy scenario above was run end-to-end after recompile:

- Step 1 (post-write): value `AIC_Combat`, reset arrow visible after
  re-selecting the actor (first-frame Details panel cache may need a
  manual refresh — not a PEC failure).
- Step 2 (Ctrl+S): value and arrow preserved.
- Step 3 (switch level, switch back): **value and arrow preserved**;
  PIE confirmed the enemy uses Hostile team type and aggroes the player.

The original symptom (reload silently dropping the instance override)
is fixed.

## Backwards compatibility

- No tool schemas changed.
- No JSON request/response formats changed.
- `GEMarkBlueprintModified` gained an optional second parameter with a
  default value of `nullptr`, so existing call signatures remain valid;
  all existing in-tree callsites were updated to pass `GE`.
- No new include dependencies in public headers (the new helper
  signature only references types already forward-declared or included
  in `CommonUtils.h`).
