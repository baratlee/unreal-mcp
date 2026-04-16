# Enhanced Input Read Tools (2026-04-16)

## New Tools

### `get_input_action_info`
Read a `UInputAction` (IA) data asset. Returns:
- `value_type`: Boolean / Axis1D / Axis2D / Axis3D
- `description`, `consume_input`, `trigger_when_paused`, `reserve_all_mappings`
- `triggers[]`: class name + type-specific fields (hold threshold, tap threshold, etc.)
- `modifiers[]`: class name + type-specific fields (dead zone, scalar, negate, swizzle, etc.)

### `get_input_mapping_context_info`
Read a `UInputMappingContext` (IMC) data asset. Returns:
- `description`
- `mappings[]`: each entry has `action` (path + name + value_type), `key`, `triggers[]`, `modifiers[]`

## Changed Files

### C++ (Plugins/UnrealMCP/)
- `Public/Commands/UnrealMCPAnimationCommands.h` — +2 handler declarations
- `Private/Commands/UnrealMCPAnimationCommands.cpp` — +5 includes, +4 helper functions (SerializeTrigger/Modifier/arrays), +2 handlers
- `Private/UnrealMCPBridge.cpp` — +2 command strings in dispatch
- `UnrealMCP.Build.cs` — +EnhancedInput module dependency

### Python (unreal-mcp/)
- `Python/tools/input_tools.py` — new file, 2 tool functions
- `Python/unreal_mcp_server.py` — import + register
