# Compact Transition Result Property Bindings

## Problem

`get_anim_transition_graph` defaults to compact output. Compact node serialization returned before inspecting `UAnimGraphNode_Base::Binding`, so a Transition Result condition configured through the Details-panel binding appeared as an unconnected `bCanEnterTransition=False` pin.

## Change

Compact AnimGraph node output now includes `property_bindings` when the node has at least one binding. Empty binding arrays and the other verbose Details payloads remain omitted.

This applies to Transition Result nodes and other AnimGraph node subclasses through the existing reflection-based binding serializer. The response shape of full output is unchanged.

## Read contract

Call `get_anim_transition_graph` normally; its default `compact_output=true` is sufficient after this change. Find the node whose `class` is `AnimGraphNode_TransitionResult` and inspect:

```json
{
  "property_bindings": [
    {
      "property_name": "bCanEnterTransition",
      "detail": {
        "PropertyPath": ["bShouldEnterWalkStart"],
        "PathAsText": "bShouldEnterWalkStart",
        "bIsBound": true
      }
    }
  ]
}
```

The `bCanEnterTransition` pin can still report `default_value=false`. That value is not the effective Transition condition when a Property Binding exists.

For a targeted second read, pass the Result node GUID to `get_anim_graph_node_property_bindings`. Its `bindings` array uses the same binding detail serializer.

## Compatibility and deployment

- Full graph output is unchanged.
- Compact output still omits an empty `property_bindings` field.
- The plugin must be rebuilt and the Unreal Editor restarted before the new response contract is observable. An empty array from an older loaded DLL is not evidence that the asset has no binding.

## Files

- `Source/UnrealMCP/Private/Commands/UnrealMCPBlueprintCommands.cpp`
- `ChangesDocs/2026-08-14_compact_graph_output.md`
- `ChangesDocs/ToolList.md`

## Verification

1. Build the UnrealMCP plugin and restart the Editor.
2. Read a Transition whose Result uses a Details-panel binding with default compact output.
3. Confirm `property_bindings[0].property_name == "bCanEnterTransition"` and the expected `detail.PropertyPath`.
4. Repeat with `compact_output=false` and with `get_anim_graph_node_property_bindings(node_guid=...)`.
5. Confirm an unbound Transition Result omits `property_bindings` only in Compact output.
