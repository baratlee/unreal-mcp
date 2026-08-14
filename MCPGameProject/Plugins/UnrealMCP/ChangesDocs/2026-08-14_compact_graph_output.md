# Compact Graph Output

## Purpose

Graph read tools now support a semantic `compact` response that omits editor-layout coordinates and empty containers before the payload reaches the AI client.

## Affected tools

- `get_blueprint_function_graph`
- `get_anim_state_graph`
- `get_anim_transition_graph`

The Python MCP tools expose `compact_output: bool = True` and default to `pin_payload_mode="summary"`. Callers that need the legacy response shape can request `compact_output=false` and `pin_payload_mode="full"`.

`get_blueprint_function_graph` additionally exposes `topology_only: bool = False`. This mode keeps node identity and semantic fields but replaces per-pin bidirectional links with a single graph-level `edges` array. It takes precedence over `compact_output` and `pin_payload_mode`.

The C++ command layer continues to accept the earlier `output_profile="compact"|"full"` field for compatibility. When `compact_output` is present, the boolean switch takes precedence.

## Response contract

Both profiles preserve node identity, class, title, semantic fields, pins, and connection topology.

- `compact_output=true` omits `pos_x`, `pos_y`, empty `pins`, Property Binding details, and verbose `anim_node_properties` / `node_object_properties` dumps. It retains `anim_node_struct` so the runtime node type remains visible.
- `full` preserves the previous node response shape, including editor-layout coordinates and empty containers.
- Every graph response reports the effective `compact_output`, `output_profile`, and `pin_payload_mode`.
- `topology_only=true` reports `output_profile="topology"`, forces the effective Pin payload to `names_only`, omits node Pin arrays, and returns each connection once as `[source_node_index, source_pin_name, target_node_index, target_pin_name]`. Node indices address the response's `nodes` array.

`get_anim_state_machine` is unchanged because its state and transition topology response does not contain graph-node coordinates.
