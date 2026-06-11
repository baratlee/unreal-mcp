# Material Write Batch — P0a + P0b + P1 (2026-06-11)

Add five write commands to `FUnrealMCPMaterialCommands` so MCP can edit material assets in place. Read-only inspection was landed 2026-05-22 (P0 / P1); this batch covers the most common write needs without touching node graph CRUD or connections, which stay in P2/P3 backlog.

## Commands

| # | Tier | Command | Schema |
|---|---|---|---|
| 1 | P0a | `set_material_expression_property` | `(material_path, expression_guid, property_name, value)` |
| 2 | P0b | `set_material_instance_scalar_parameter` | `(asset_path, parameter_name, value: float)` |
| 3 | P0b | `set_material_instance_vector_parameter` | `(asset_path, parameter_name, value: [R,G,B,A])` |
| 4 | P0b | `set_material_instance_texture_parameter` | `(asset_path, parameter_name, texture_path)` |
| 5 | P1  | `set_material_property` | `(material_path, property_name, value)` |

## Files

| File | Change |
|---|---|
| `Public/Commands/UnrealMCPMaterialCommands.h` | +5 Handler decls + updated header comment |
| `Private/Commands/UnrealMCPMaterialCommands.cpp` | +5 Handler impls, +`#include "MaterialEditingLibrary.h"`, +dispatch entries, +helper `FindMaterialExpressionByGuidOrName` + `NotifyTopLevelPropertyChanged` in inner anon namespace |
| `Private/UnrealMCPBridge.cpp` | **(critical)** +5 command names in the Material elseif block of the central dispatcher. Without this every new Material write command returns "Unknown command" at the Bridge layer — Commands class's own dispatch is never reached. |
| `Source/UnrealMCP/UnrealMCP.Build.cs` | +`"MaterialEditor"` in editor-only `PrivateDependencyModuleNames` (for `UMaterialEditingLibrary`) |
| `Python/tools/material_tools.py` | +5 `@mcp.tool()` wrappers |

## ⚠️ MCP Extension Gotcha: Two-Level Dispatch

Adding a new command requires **both** layers wired up; missing either silently fails:

1. **`UnrealMCPBridge.cpp` (top-level)** — adds command name to one of the `if/elseif (CommandType == TEXT("..."))` blocks, routing to the right Commands class. Without this: `"Unknown command"` error at Bridge layer.
2. **`<Module>Commands.cpp` HandleCommand** — adds a per-command branch dispatching to the Handler. Without this: `"Unknown <Module> command"` error at Commands class layer.

This batch needed (1)+(2) plus a Build.cs module reference. Future LT16 extensions should add this triple to a checklist.

## Implementation Notes

### P0a — `set_material_expression_property`
- Locate target node by `expression_guid` from `get_material_graph` (preferred). Falls back to `UMaterialExpression::GetName()` match when the GUID is not a valid `FGuid` string (some non-parameter nodes have `MaterialExpressionGuid == invalid` and `MakeNodeId()` returns the object name).
- Calls `FUnrealMCPCommonUtils::SetObjectProperty(Expr, PropertyName, Value, ErrorMsg)` so dotted nested paths / `FObjectProperty` instanced subobjects / `FArrayProperty` import via T3D literal all work out of the box (see `2026-06-09_set_object_property_nested_subobject.md` + `2026-06-09_arrayproperty_import_v1.md`).
- After the write: `NotifyPropertyChanged(Expr, TopProp)` + `UMaterialEditingLibrary::RecompileMaterial(Material)` + `MarkPackageDirty`. Recompile is required so editor preview + cooked shader pipeline stay in sync.
- `Expr->Modify()` + `Material->Modify()` are called before the write to record an undo step.

### P0b — MI parameter overrides
All three call into `UMaterialEditingLibrary`:
- `SetMaterialInstanceScalarParameterValue(MIC, FName, float)` → returns bool, surfaces as `changed` in the response so callers can see if the value was actually new.
- `SetMaterialInstanceVectorParameterValue(MIC, FName, FLinearColor)` — JSON array `[R,G,B]` or `[R,G,B,A]`. Missing alpha defaults to 1.0.
- `SetMaterialInstanceTextureParameterValue(MIC, FName, UTexture*)` — pass empty `texture_path` to clear override (write null).
- All three call `UpdateMaterialInstance` then `MarkPackageDirty`.
- Asset must `Cast<UMaterialInstanceConstant>`; UMaterialInstanceDynamic is not supported (Editor library is MIC-only).

### P1 — `set_material_property`
- Reflect-write on `UMaterial` itself. Same pattern as P0a (uses `SetObjectProperty` + `NotifyTopLevelPropertyChanged` + `RecompileMaterial`).
- Useful for changing `BlendMode`, `TwoSided`, `MaterialDomain`, `bUsedWithSkeletalMesh`, etc.
- Some `UMaterial` fields (notably `ShadingModels`) are wrapped in `FMaterialShadingModelField` and may not respond to a single enum write — those still need a dedicated path (deferred to P2).

## Known Gaps / Out of Scope

- **No node CRUD / connection editing**: `add_material_expression`, `delete_material_expression`, `connect_material_expressions` deferred to P2/P3.
- **No MI static switch / runtime virtual texture / sparse volume texture overrides**: only scalar / vector / texture in P0b.
- **No `FMaterialShadingModelField` setter helper**: P2.
- **No Material Function internal graph edits**: same as the existing read-side gap.
- **No `EMaterialParameterAssociation` selector**: P0b always writes to `GlobalParameter`. Layer / Blend parameter associations are P2.

## Testing Plan

After VS rebuild of UnrealEditor.exe and editor restart:

1. **P0a verify** (LT23 immediate need):
   - Call `get_material_graph("/Game/Examples/Demo/Character/ArmoredBeetle/Materials/M_bodi_set_1")` → grab guid of the Normal-output Texture Sample.
   - Call `set_material_expression_property(material_path=…, expression_guid=…, property_name="Texture", value="/Game/Examples/Demo/Character/ArmoredBeetle/Textures/T_bodi_Normal")`.
   - Save, restart editor, verify with PowerShell string-grep on the .uasset that `Examples/Illegal/.../T_bodi_Normal` is gone and `ArmoredBeetle/Textures/T_bodi_Normal` is present.
   - Repeat for Emissive.

2. **P0b verify**:
   - Pick an MI with a scalar parameter, call set, then `get_material_instance_info` to confirm the override landed.

3. **P1 verify**:
   - `set_material_property(material_path=…, property_name="TwoSided", value=true)` → `get_material_info` confirms `two_sided=true`.

4. **Negative tests**:
   - Bad expression GUID → expect `expression not found` error.
   - Bad property name → expect `Failed to set property` error from SetObjectProperty.
   - Non-Material asset → expect type-check error.

## Build Notes

- This batch requires VS 2022 rebuild of UnrealEditor.exe. Hot reload not supported for new MCP commands (per existing convention).
- After rebuild + editor restart, the 5 new tools become available via MCP without any client-side change beyond a tool list refresh.
