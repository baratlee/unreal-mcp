# Skeleton Blend Mask batch interfaces

Skeleton mask weights are private to the Python/editor property interface, requiring repeated UI operations. Native animation commands expose list/read and validated batch creation/updates.

- `list_skeleton_blend_masks(skeleton_path)`: mask names, object paths, nonzero entry counts.
- `get_skeleton_blend_mask(skeleton_path, mask_name, include_zero_weights=True)`: all effective per-bone weights, including implicit zeros; compact nonzero output is optional.
- `set_skeleton_blend_mask(skeleton_path, mask_name, bone_weights=None, branch_filters=None, create_if_missing=False, replace=False)`: applies ordered weights and recursive overrides. All request entries are checked before any asset changes. A transaction supports Undo. Existing time/weight profiles and subobject name collisions are rejected.

`branch_filters` generates a complete mask using the Engine BranchFilter formula and ordered clamped accumulation. Bone overrides then apply. `[{"bone_name":"Bip001_Spine1","blend_depth":4}]` yields 0.25 on Spine1, 0.5 on Spine2, 0.75 on its children, 1 on deeper descendants, and zero elsewhere.

Both readers are allowlisted for `batch_read`; the writer is not. Only the Skeleton is marked dirty. No automatic save, compilation, AnimGraph mutation or source-control action occurs. Existing `set_anim_graph_node_property` binds the returned mask_path to a node separately.

Validation: Python AST and command-registration/static source checks. C++ build and Editor roundtrip remain outstanding; rebuild plugin and restart Editor and Python MCP before using the tools. This is asset configuration support, not animation runtime acceptance evidence.
