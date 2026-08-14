# Compact Graph Output

## Purpose

Graph read tools now support a semantic `compact` response that omits editor-layout coordinates and empty containers before the payload reaches the AI client.

## Affected tools

- `get_blueprint_function_graph`
- `get_anim_state_graph`
- `get_anim_transition_graph`

The Python MCP tools default to `output_profile="compact"` and `pin_payload_mode="summary"`. Callers that need the legacy response shape can request `output_profile="full"` and `pin_payload_mode="full"`.

## Response contract

Both profiles preserve node identity, class, title, semantic fields, pins, and connection topology.

- `compact` omits `pos_x` and `pos_y`, empty `pins`, empty `property_bindings`, and empty AnimGraph property objects.
- `full` preserves the previous node response shape, including editor-layout coordinates and empty containers.
- Every graph response reports the effective `output_profile` and `pin_payload_mode`.

`get_anim_state_machine` is unchanged because its state and transition topology response does not contain graph-node coordinates.
