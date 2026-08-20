"""
Blueprint Tools for Unreal MCP.

This module provides tools for creating and manipulating Blueprint assets in Unreal Engine.
"""

import logging
from typing import Dict, List, Any, Union
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def register_blueprint_tools(mcp: FastMCP):
    """Register Blueprint tools with the MCP server."""
    
    @mcp.tool()
    def create_blueprint(
        ctx: Context,
        name: str,
        parent_class: str
    ) -> Dict[str, Any]:
        """Create a new Blueprint class."""
        # Import inside function to avoid circular imports
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            response = unreal.send_command("create_blueprint", {
                "name": name,
                "parent_class": parent_class
            })
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Blueprint creation response: {response}")
            return response or {}
            
        except Exception as e:
            error_msg = f"Error creating blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def add_component_to_blueprint(
        ctx: Context,
        blueprint_name: str,
        component_type: str,
        component_name: str,
        location: List[float] = [],
        rotation: List[float] = [],
        scale: List[float] = [],
        component_properties: Dict[str, Any] = {}
    ) -> Dict[str, Any]:
        """
        Add a component to a Blueprint.
        
        Args:
            blueprint_name: Name of the target Blueprint
            component_type: Type of component to add (use component class name without U prefix)
            component_name: Name for the new component
            location: [X, Y, Z] coordinates for component's position
            rotation: [Pitch, Yaw, Roll] values for component's rotation
            scale: [X, Y, Z] values for component's scale
            component_properties: Additional properties to set on the component
        
        Returns:
            Information about the added component
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            # Ensure all parameters are properly formatted
            params = {
                "blueprint_name": blueprint_name,
                "component_type": component_type,
                "component_name": component_name,
                "location": location or [0.0, 0.0, 0.0],
                "rotation": rotation or [0.0, 0.0, 0.0],
                "scale": scale or [1.0, 1.0, 1.0]
            }
            
            # Add component_properties if provided
            if component_properties and len(component_properties) > 0:
                params["component_properties"] = component_properties
            
            # Validate location, rotation, and scale formats
            for param_name in ["location", "rotation", "scale"]:
                param_value = params[param_name]
                if not isinstance(param_value, list) or len(param_value) != 3:
                    logger.error(f"Invalid {param_name} format: {param_value}. Must be a list of 3 float values.")
                    return {"success": False, "message": f"Invalid {param_name} format. Must be a list of 3 float values."}
                # Ensure all values are float
                params[param_name] = [float(val) for val in param_value]
            
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
                
            logger.info(f"Adding component to blueprint with params: {params}")
            response = unreal.send_command("add_component_to_blueprint", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Component addition response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error adding component to blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def set_static_mesh_properties(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        static_mesh: str = "/Engine/BasicShapes/Cube.Cube"
    ) -> Dict[str, Any]:
        """
        Set static mesh properties on a StaticMeshComponent.
        
        Args:
            blueprint_name: Name of the target Blueprint
            component_name: Name of the StaticMeshComponent
            static_mesh: Path to the static mesh asset (e.g., "/Engine/BasicShapes/Cube.Cube")
            
        Returns:
            Response indicating success or failure
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "static_mesh": static_mesh
            }
            
            logger.info(f"Setting static mesh properties with params: {params}")
            response = unreal.send_command("set_static_mesh_properties", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set static mesh properties response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting static mesh properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def set_component_property(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        property_name: str,
        property_value,
    ) -> Dict[str, Any]:
        """Set a property on a component in a Blueprint."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "property_name": property_name,
                "property_value": property_value
            }
            
            logger.info(f"Setting component property with params: {params}")
            response = unreal.send_command("set_component_property", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set component property response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting component property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def set_physics_properties(
        ctx: Context,
        blueprint_name: str,
        component_name: str,
        simulate_physics: bool = True,
        gravity_enabled: bool = True,
        mass: float = 1.0,
        linear_damping: float = 0.01,
        angular_damping: float = 0.0
    ) -> Dict[str, Any]:
        """Set physics properties on a component."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": blueprint_name,
                "component_name": component_name,
                "simulate_physics": simulate_physics,
                "gravity_enabled": gravity_enabled,
                "mass": float(mass),
                "linear_damping": float(linear_damping),
                "angular_damping": float(angular_damping)
            }
            
            logger.info(f"Setting physics properties with params: {params}")
            response = unreal.send_command("set_physics_properties", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set physics properties response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting physics properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def compile_blueprint(
        ctx: Context,
        blueprint_name: str
    ) -> Dict[str, Any]:
        """Compile a Blueprint."""
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": blueprint_name
            }
            
            logger.info(f"Compiling blueprint: {blueprint_name}")
            response = unreal.send_command("compile_blueprint", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Compile blueprint response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error compiling blueprint: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_blueprint_property(
        ctx: Context,
        blueprint_name: str,
        property_name: str,
        property_value
    ) -> Dict[str, Any]:
        """
        Set a property on a Blueprint class default object.
        
        Args:
            blueprint_name: Name of the target Blueprint
            property_name: Name of the property to set
            property_value: Value to set the property to
            
        Returns:
            Response indicating success or failure
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            params = {
                "blueprint_name": blueprint_name,
                "property_name": property_name,
                "property_value": property_value
            }
            
            logger.info(f"Setting blueprint property with params: {params}")
            response = unreal.send_command("set_blueprint_property", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Set blueprint property response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error setting blueprint property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # @mcp.tool() commented out, just use set_component_property instead
    def set_pawn_properties(
        ctx: Context,
        blueprint_name: str,
        auto_possess_player: str = "",
        use_controller_rotation_yaw: bool = None,
        use_controller_rotation_pitch: bool = None,
        use_controller_rotation_roll: bool = None,
        can_be_damaged: bool = None
    ) -> Dict[str, Any]:
        """
        Set common Pawn properties on a Blueprint.
        This is a utility function that sets multiple pawn-related properties at once.
        
        Args:
            blueprint_name: Name of the target Blueprint (must be a Pawn or Character)
            auto_possess_player: Auto possess player setting (None, "Disabled", "Player0", "Player1", etc.)
            use_controller_rotation_yaw: Whether the pawn should use the controller's yaw rotation
            use_controller_rotation_pitch: Whether the pawn should use the controller's pitch rotation
            use_controller_rotation_roll: Whether the pawn should use the controller's roll rotation
            can_be_damaged: Whether the pawn can be damaged
            
        Returns:
            Response indicating success or failure with detailed results for each property
        """
        from unreal_mcp_server import get_unreal_connection
        
        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            
            # Define the properties to set
            properties = {}
            if auto_possess_player and auto_possess_player != "":
                properties["auto_possess_player"] = auto_possess_player
            
            # Only include boolean properties if they were explicitly set
            if use_controller_rotation_yaw is not None:
                properties["bUseControllerRotationYaw"] = use_controller_rotation_yaw
            if use_controller_rotation_pitch is not None:
                properties["bUseControllerRotationPitch"] = use_controller_rotation_pitch
            if use_controller_rotation_roll is not None:
                properties["bUseControllerRotationRoll"] = use_controller_rotation_roll
            if can_be_damaged is not None:
                properties["bCanBeDamaged"] = can_be_damaged
                
            if not properties:
                logger.warning("No properties specified to set")
                return {"success": True, "message": "No properties specified to set", "results": {}}
            
            # Set each property using the generic set_blueprint_property function
            results = {}
            overall_success = True
            
            for prop_name, prop_value in properties.items():
                params = {
                    "blueprint_name": blueprint_name,
                    "property_name": prop_name,
                    "property_value": prop_value
                }
                
                logger.info(f"Setting pawn property {prop_name} to {prop_value}")
                response = unreal.send_command("set_blueprint_property", params)
                
                if not response:
                    logger.error(f"No response from Unreal Engine for property {prop_name}")
                    results[prop_name] = {"success": False, "message": "No response from Unreal Engine"}
                    overall_success = False
                    continue
                
                results[prop_name] = response
                if not response.get("success", False):
                    overall_success = False
            
            return {
                "success": overall_success,
                "message": "Pawn properties set" if overall_success else "Some pawn properties failed to set",
                "results": results
            }
            
        except Exception as e:
            error_msg = f"Error setting pawn properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def get_blueprint_info(
        ctx: Context,
        blueprint_path: str
    ) -> Dict[str, Any]:
        """
        Get detailed information about a Blueprint asset, including components, variables, and event graph nodes.

        Args:
            blueprint_path: Asset path of the Blueprint (e.g., "/Game/Blueprints/MyBlueprint" or just "MyBlueprint")

        Returns:
            Blueprint info as JSON: name, parent class, components, variables, event graphs with nodes and connections
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_blueprint_info", {
                "blueprint_path": blueprint_path
            })

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Get blueprint info response received for: {blueprint_path}")
            return response

        except Exception as e:
            error_msg = f"Error getting blueprint info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_component_properties(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        max_depth: int = 2
    ) -> Dict[str, Any]:
        """Get all editable properties of a component in a Blueprint, including
        inherited components and nested sub-object properties.

        Searches for the component across:
        1. The Blueprint's own SimpleConstructionScript
        2. Parent Blueprint SCS chains (inherited BP components)
        3. Native C++ CDO components (inherited C++ components)

        Args:
            blueprint_path: Asset path of the Blueprint
                (e.g., "/Game/Blueprints/BP_Character")
            component_name: Name of the component to inspect
                (e.g., "CharacterMover", "Mesh", "CapsuleComponent")
            max_depth: How deep to recurse into nested sub-objects (1-4, default 2).
                Use 1 for flat properties only, 2+ to expand sub-objects like
                SharedSettings/CommonLegacyMovementSettings.

        Returns:
            Component info with blueprint_path, component_name, component_class,
            source (where the component was found), and a properties array.
            Nested sub-objects appear as elements with their own properties arrays.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_component_properties", {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "max_depth": max_depth
            })

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Get component properties response for: {component_name}")
            return response

        except Exception as e:
            error_msg = f"Error getting component properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_blueprint_cdo_properties(
        ctx: Context,
        blueprint_path: str,
        max_depth: int = 2,
        category_filter: str = ""
    ) -> Dict[str, Any]:
        """Get all editable properties from a Blueprint's Class Default Object (CDO).

        Reads every UPROPERTY marked EditAnywhere / EditDefaultsOnly on the
        Blueprint's generated class CDO. This includes C++ properties defined
        in the parent class that are set via the Blueprint's Class Defaults panel
        (e.g., animation references, config floats, enum settings).

        Args:
            blueprint_path: Asset path of the Blueprint
                (e.g., "/Game/Blueprints/MyABP" or just "MyABP")
            max_depth: How deep to recurse into nested sub-objects (1-4, default 2).
            category_filter: Optional substring filter on property Category metadata.
                Only properties whose Category contains this string (case-insensitive)
                are returned. E.g., "Quad" to see only Quad-related properties,
                "Animation" for animation properties. Empty string returns all.

        Returns:
            CDO info with blueprint_path, class_name, parent_class, and a
            properties array. Each property has name, category, type, and value.
            Object properties include object_class and optionally sub_properties.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "blueprint_path": blueprint_path,
                "max_depth": max_depth
            }
            if category_filter:
                params["category_filter"] = category_filter

            response = unreal.send_command("get_blueprint_cdo_properties", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Get blueprint CDO properties response for: {blueprint_path}")
            return response

        except Exception as e:
            error_msg = f"Error getting blueprint CDO properties: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_blueprint_function_graph(
        ctx: Context,
        blueprint_path: str,
        function_name: str,
        pin_payload_mode: str = "summary",
        compact_output: bool = True,
        topology_only: bool = False
    ) -> Dict[str, Any]:
        """
        Get the full node detail of a single function or AnimGraph inside a Blueprint.

        Unlike `get_blueprint_info`, which only returns name + node_count for
        every entry in `function_graphs`, this command expands one named graph
        into its full node list, including pin types, default values, and
        connection links between nodes. Works for user functions, the AnimGraph
        of an Animation Blueprint, Anim Layer Interface implementation graphs,
        nested sub-graphs, and EventGraph pages.

        Args:
            blueprint_path: Asset path of the Blueprint
                (e.g., "/Game/Blueprints/MyABP" or just "MyABP")
            function_name: Name of the function / AnimGraph / event graph to expand
                (e.g., "AnimGraph", "Update_Logic", "EventGraph")
            pin_payload_mode: How much pin payload to include. AnimGraph nodes
                often carry multi-KB InstancedStruct text in DefaultValue and
                can blow past the MCP socket timeout in "full" mode. Options:
                  - "full"       : verbatim DefaultValue
                  - "summary"    : DefaultValue truncated to a 96-char preview (default)
                                   when longer than 256 chars; adds
                                   default_value_len / default_value_preview /
                                   default_value_truncated fields instead
                  - "names_only" : drop DefaultValue entirely; keep type / links only
                Use "summary" or "names_only" when reading AnimGraph or other
                large graphs that time out in full mode.
            compact_output: True (default) omits editor-layout coordinates,
                empty containers, and verbose AnimGraph property dumps. False
                preserves the complete legacy response shape.
            topology_only: True returns only node identity / semantic fields and
                a graph-level `edges` array. Each edge is
                [source_node_index, source_pin_name, target_node_index,
                target_pin_name], where node indices address the `nodes` array.
                This takes precedence over compact_output and pin_payload_mode.

        Returns:
            Dict with blueprint_path, function_name, graph_class, node_count,
            and a `nodes` array. Each node has the same shape as nodes returned
            inside `event_graphs` from `get_blueprint_info`.

            AnimGraph nodes (UAnimGraphNode_* subclasses) additionally carry
            three Details-panel-only fields beyond the standard pin array:
              - "anim_node_struct"       : name of the inner FAnimNode_* struct
                                           (e.g. "AnimNode_TwoWayBlend")
              - "anim_node_properties"   : JSON object of every EditAnywhere
                                           UPROPERTY on the runtime FAnimNode_*
                                           struct (e.g. AlphaInputType,
                                           bAlphaBoolEnabled, bAlwaysUpdateChildren,
                                           bResetChildOnActivation, Alpha, etc.)
              - "node_object_properties" : JSON object of every EditAnywhere
                                           UPROPERTY on the editor-side
                                           UAnimGraphNode_* UObject itself
                                           (NOT on the runtime struct). Surfaces
                                           UAnimGraphNode_Base inherited fields:
                                           Tag (FName, runtime node lookup),
                                           ShowPinForProperties (per-property pin
                                           visibility), InitialUpdateFunction /
                                           BecomeRelevantFunction / UpdateFunction
                                           (FMemberReference function callbacks),
                                           plus any subclass-specific editor
                                           UPROPERTY. Inner FAnimNode_* and
                                           Binding sub-object are excluded
                                           (already in payloads above).
              - "property_bindings"      : array of Details-panel Property
                                           Bindings (the menu behind the small
                                           binding button next to a pin). Each
                                           entry has { property_name, detail:
                                           { PathAsText, PropertyPath[], Type,
                                           bIsBound, bIsPromotion, PinType, ... } }
            All three are omitted when pin_payload_mode="names_only". Non-AnimGraph
            nodes (K2Node_* in EventGraph / user functions) never carry these
            fields.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_blueprint_function_graph", {
                "blueprint_path": blueprint_path,
                "function_name": function_name,
                "pin_payload_mode": pin_payload_mode,
                "compact_output": compact_output,
                "topology_only": topology_only
            })

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"Get blueprint function graph response received: {blueprint_path} :: {function_name}")
            return response

        except Exception as e:
            error_msg = f"Error getting blueprint function graph: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_anim_graph_node_property_bindings(
        ctx: Context,
        blueprint_path: str,
        node_guid: str = None,
        graph_name: str = None,
        node_class: str = None,
    ) -> Dict[str, Any]:
        """Get Property Bindings by node GUID or enumerate nodes in a nested AnimGraph.

        Args:
            blueprint_path: Asset path of the Animation Blueprint.
            node_guid: Optional GUID of one target UAnimGraphNode_Base.
            graph_name: Optional nested graph name, such as "FreeStop". Required when
                node_guid is omitted.
            node_class: Optional exact UAnimGraphNode class filter for graph-name mode,
                such as "AnimGraphNode_BlendListByEnum".

        Returns:
            GUID mode returns one node and its compact `bindings` array. Graph-name mode
            returns every matching node in `nodes`, allowing callers to discover GUIDs and
            bindings without expanding the whole Blueprint graph.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params: Dict[str, Any] = {"blueprint_path": blueprint_path}
            if node_guid:
                params["node_guid"] = node_guid
            if graph_name:
                params["graph_name"] = graph_name
            if node_class:
                params["node_class"] = node_class
            if not node_guid and not graph_name:
                return {"success": False, "message": "Provide either node_guid or graph_name"}

            response = unreal.send_command("get_anim_graph_node_property_bindings", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            err = f"Error getting AnimGraph node property bindings: {e}"
            logger.error(err)
            return {"success": False, "message": err}

    # ── Batch E: P0/P1 from UnrealMCP_API_ExpansionRequest.md ─────────────────

    @mcp.tool()
    def create_blueprint_from_parent_blueprint(
        ctx: Context,
        parent_blueprint_path: str,
        new_asset_path: str,
    ) -> Dict[str, Any]:
        """Create a child blueprint whose parent is another BLUEPRINT (not a C++ class).

        Mirrors the Content Browser's "Create Child Blueprint Class" action. Works for any
        UBlueprint subclass — including UAnimBlueprint, where the resulting child preserves the
        parent's FunctionGraphs/AnimGraph wiring.

        Args:
            parent_blueprint_path: e.g. "/Game/Kunlun/Pawn/BP_KLPawn_Human.BP_KLPawn_Human"
            new_asset_path:        e.g. "/Game/Kunlun/Pawn/BP_KLPawn_StoneGolem.BP_KLPawn_StoneGolem"
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("create_blueprint_from_parent_blueprint", {
                "parent_blueprint_path": parent_blueprint_path,
                "new_asset_path": new_asset_path,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            err = f"Error creating child blueprint: {e}"
            logger.error(err)
            return {"success": False, "message": err}

    @mcp.tool()
    def add_anim_graph_node(
        ctx: Context,
        blueprint_path: str,
        node_class: str,
        graph_name: str = "AnimGraph",
        node_position: List[float] = None,
    ) -> Dict[str, Any]:
        """Add a new UAnimGraphNode_* into an AnimBlueprint graph.

        Args:
            blueprint_path: AnimBlueprint asset path.
            node_class:     Class name — bare ("AnimGraphNode_RetargetPoseFromMesh") or full
                            ("/Script/AnimGraph.AnimGraphNode_RetargetPoseFromMesh"). Must be a
                            UAnimGraphNode_Base subclass.
            graph_name:     Graph name (defaults to "AnimGraph"). Pass "AnimGraph_StateMachine"
                            etc. when targeting a sub-graph by name.
            node_position:  [X, Y] in graph coordinates. Defaults to (0, 0).

        Returns:
            success, blueprint_path, graph_name, node_guid, node_class.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "blueprint_path": blueprint_path,
                "node_class": node_class,
                "graph_name": graph_name,
            }
            if node_position is not None:
                params["node_position"] = [float(v) for v in node_position]
            response = unreal.send_command("add_anim_graph_node", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            err = f"Error adding anim graph node: {e}"
            logger.error(err)
            return {"success": False, "message": err}

    @mcp.tool()
    def connect_anim_graph_nodes(
        ctx: Context,
        blueprint_path: str,
        source_node_id: str,
        source_pin: str,
        target_node_id: str,
        target_pin: str,
    ) -> Dict[str, Any]:
        """Connect two nodes inside an AnimGraph (or any sub-graph) by GUID + pin name.

        Nodes are looked up across all graphs in the blueprint, so you don't have to specify the
        graph — but both nodes must live in the same graph. Returns the resolved graph name.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("connect_anim_graph_nodes", {
                "blueprint_path": blueprint_path,
                "source_node_id": source_node_id,
                "source_pin": source_pin,
                "target_node_id": target_node_id,
                "target_pin": target_pin,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            err = f"Error connecting anim graph nodes: {e}"
            logger.error(err)
            return {"success": False, "message": err}

    @mcp.tool()
    def set_graph_node_pin_default_value(
        ctx: Context,
        blueprint_path: str,
        node_guid: str,
        pin_name: str,
        value: Union[str, int, float, bool],
    ) -> Dict[str, Any]:
        """Set the default value of an unconnected input pin on a K2/AnimGraph node.

        The node is resolved by GUID across every graph owned by the Blueprint. The Unreal
        graph schema parses and validates ``value`` using the same path as the editor UI.
        Connected, output, orphaned, or read-only pins are rejected.

        Args:
            blueprint_path: Blueprint or AnimBlueprint asset path.
            node_guid: Node GUID returned by ``get_blueprint_function_graph``.
            pin_name: Internal input pin name, e.g. ``B`` on a Divide node.
            value: Editor-formatted value. Natural bool/int/float values are accepted.

        Returns:
            success, blueprint_path, graph_name, node_guid, node_class, pin_name,
            requested_value, previous_default_value, and canonical default fields.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            pin_value = str(value).lower() if isinstance(value, bool) else str(value)
            response = unreal.send_command("set_graph_node_pin_default_value", {
                "blueprint_path": blueprint_path,
                "node_guid": node_guid,
                "pin_name": pin_name,
                "value": pin_value,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            err = f"Error setting graph node pin default value: {e}"
            logger.error(err)
            return {"success": False, "message": err}

    @mcp.tool()
    def set_anim_graph_node_property(
        ctx: Context,
        blueprint_path: str,
        node_guid: str,
        field_path: str = None,
        value: Union[str, int, float, bool] = None,
        property_binding: Dict[str, Any] = None,
        clear_binding: str = None,
    ) -> Dict[str, Any]:
        """Set a field or property binding on an AnimGraph node. Three modes (combinable):

        - field_path + value: write to the inner FAnimNode_* struct first (anim_node_properties
          domain — e.g. "iKRetargeterAsset", "retargetFrom"), falling back to the editor-side
          UAnimGraphNode_* UPROPERTY (node_object_properties). Value is text in the same form
          UE accepts in T3D (ImportText_Direct semantics).
        - property_binding={
              "property_name": "<inner struct member>",  # e.g. "SourceMeshComponent"
              "property_path": ["LeaderMeshComponent"],   # AnimInstance variable name(s)
              "context_id": "...",                        # optional FName
              "type": "Property" | "Function" | "None"    # optional, defaults to Property
          }: add/replace an entry in Binding->PropertyBindings. This is what the editor writes
          when you drag-bind a pin to an AnimInstance variable in the Details panel.
        - clear_binding="<property_name>": remove a binding entry by key.

        Modes execute in (field) → (binding_set) → (binding_clear) order.

        Returns: success, blueprint_path, node_guid, node_class, applied (per-mode log).
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "blueprint_path": blueprint_path,
                "node_guid": node_guid,
            }
            if field_path is not None and value is not None:
                params["field_path"] = field_path
                # ImportText_Direct takes text; stringify ints/floats/bools so callers can
                # pass natural Python types (iterations=25, enabled=True) instead of "25" / "true".
                params["value"] = str(value).lower() if isinstance(value, bool) else str(value)
            if property_binding is not None:
                params["property_binding"] = property_binding
            if clear_binding is not None:
                params["clear_binding"] = clear_binding
            response = unreal.send_command("set_anim_graph_node_property", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            err = f"Error setting anim graph node property: {e}"
            logger.error(err)
            return {"success": False, "message": err}

    @mcp.tool()
    def add_blueprint_function_graph(
        ctx: Context,
        blueprint_path: str,
        function_name: str,
    ) -> Dict[str, Any]:
        """Create a new user function graph on a blueprint (with Entry + Result nodes auto-wired).

        Use this to add the kind of independent function you'd call from BeginPlay — e.g. an "Init"
        helper. To add nodes inside the new graph use add_blueprint_function_node / connect_blueprint_nodes.
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("add_blueprint_function_graph", {
                "blueprint_path": blueprint_path,
                "function_name": function_name,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            err = f"Error adding blueprint function graph: {e}"
            logger.error(err)
            return {"success": False, "message": err}

    logger.info("Blueprint tools registered successfully")
