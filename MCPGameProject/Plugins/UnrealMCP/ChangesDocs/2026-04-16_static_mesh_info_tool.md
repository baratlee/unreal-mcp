# Static Mesh Info Tool (2026-04-16)

## New Tool

### `get_static_mesh_info`
Read a `UStaticMesh` asset. Returns:
- **Basic**: vertex_count, triangle_count, lod_count, bounding_box (min/max), nanite_enabled, lightmap_resolution
- **Materials**: material_slots[] with slot_name, material_path per slot
- **LODs**: lod_details[] with vertex_count, triangle_count, section_count, num_uv_channels, screen_size per LOD
- **Collision**: collision_type (SimpleAndComplex/SimpleAsComplex/ComplexAsSimple/None), simple_shapes (box/sphere/capsule/convex counts)
- **Vertices** (opt-in): positions as [[x,y,z],...] for specified LOD, with safety cap (default 5000)

Parameters:
- `asset_path` (required): UStaticMesh asset path
- `include_vertices` (optional, default false): Include vertex positions
- `lod_index` (optional, default 0): Which LOD for vertex data
- `max_vertices` (optional, default 5000): Safety cap on vertex count

## Changed Files

### C++ (Plugins/UnrealMCP/)
- `Public/Commands/UnrealMCPEditorCommands.h` — +1 handler declaration
- `Private/Commands/UnrealMCPEditorCommands.cpp` — +2 includes (Engine/StaticMesh.h, PhysicsEngine/BodySetup.h), +1 anonymous namespace helper (CollisionTraceFlagToString), +1 handler (~140 lines)
- `Private/UnrealMCPBridge.cpp` — +1 command string in Editor dispatch

### Python (unreal-mcp/)
- `Python/tools/mesh_tools.py` — new file, 1 tool function
- `Python/unreal_mcp_server.py` — import + register

### Build.cs
No new module dependencies. `Engine` provides UStaticMesh/FStaticMeshRenderData, `PhysicsEngine` is transitive via Engine.
If vertex buffer access fails to compile, add `RenderCore` to PrivateDependencyModuleNames.
