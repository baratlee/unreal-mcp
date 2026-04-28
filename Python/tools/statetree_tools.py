"""StateTree tools for UnrealMCP - create, read, and write StateTree assets."""

from typing import Any, Dict, Optional
from mcp.server.fastmcp import Context, FastMCP


def register_statetree_tools(mcp: FastMCP):
    """Register StateTree tools with the MCP server."""

    # -----------------------------------------------------------------------
    # Create
    # -----------------------------------------------------------------------

    @mcp.tool()
    def create_state_tree(
        ctx: Context,
        asset_path: str,
        schema_class: str = "",
    ) -> Dict[str, Any]:
        """Create a new StateTree asset.

        The asset is dirty in-memory but NOT saved to disk.

        Args:
            asset_path: Target content path, e.g. "/Game/AI/ST_MyTree".
            schema_class: Optional schema class name, e.g. "StateTreeComponentSchema".
                          If empty the factory will prompt or use a default.

        Returns:
            Dict with success, asset_path, schema (if set), saved=False.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"asset_path": asset_path}
            if schema_class:
                params["schema_class"] = schema_class
            response = unreal.send_command("create_state_tree", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error creating StateTree: {e}"}

    # -----------------------------------------------------------------------
    # Read
    # -----------------------------------------------------------------------

    @mcp.tool()
    def get_state_tree_info(
        ctx: Context,
        asset_path: str,
    ) -> Dict[str, Any]:
        """Get complete StateTree structure including states hierarchy, tasks,
        transitions, evaluators, global tasks, and schema.

        Args:
            asset_path: Content path of the StateTree asset.

        Returns:
            Dict with success, asset_path, schema, evaluators, global_tasks,
            and states (recursive hierarchy with tasks/transitions/conditions).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("get_state_tree_info", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error getting StateTree info: {e}"}

    @mcp.tool()
    def get_state_tree_node_properties(
        ctx: Context,
        asset_path: str,
        node_id: str,
    ) -> Dict[str, Any]:
        """Read all detail-panel properties of a specific StateTree node
        (task, condition, or evaluator) identified by its GUID.

        Use get_state_tree_info first to discover node IDs.

        Args:
            asset_path: Content path of the StateTree asset.
            node_id: GUID of the node (from get_state_tree_info output).

        Returns:
            Dict with node/instance struct type names and all their
            UPROPERTY fields serialised to JSON.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"asset_path": asset_path, "node_id": node_id}
            response = unreal.send_command("get_state_tree_node_properties", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error getting node properties: {e}"}

    # -----------------------------------------------------------------------
    # Write – State manipulation
    # -----------------------------------------------------------------------

    @mcp.tool()
    def add_state_tree_state(
        ctx: Context,
        asset_path: str,
        state_name: str,
        parent_state_name: str = "",
        state_type: str = "State",
    ) -> Dict[str, Any]:
        """Add a new state to the StateTree.

        Args:
            asset_path: Content path of the StateTree asset.
            state_name: Name for the new state.
            parent_state_name: If empty, adds as a root-level state (subtree).
                               Otherwise adds as a child of the named state.
            state_type: One of: State, Group, Linked, LinkedAsset, Subtree.

        Returns:
            Dict with success, state_name, state_id.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "asset_path": asset_path,
                "state_name": state_name,
                "state_type": state_type,
            }
            if parent_state_name:
                params["parent_state_name"] = parent_state_name
            response = unreal.send_command("add_state_tree_state", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error adding state: {e}"}

    @mcp.tool()
    def remove_state_tree_state(
        ctx: Context,
        asset_path: str,
        state_name: str,
    ) -> Dict[str, Any]:
        """Remove a state from the StateTree by name.

        Args:
            asset_path: Content path of the StateTree asset.
            state_name: Name of the state to remove.

        Returns:
            Dict with success, removed_state.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"asset_path": asset_path, "state_name": state_name}
            response = unreal.send_command("remove_state_tree_state", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error removing state: {e}"}

    @mcp.tool()
    def set_state_tree_state_property(
        ctx: Context,
        asset_path: str,
        state_name: str,
        name: str = "",
        type: str = "",
        selection_behavior: str = "",
        tasks_completion: str = "",
        tag: str = "",
        enabled: Optional[bool] = None,
    ) -> Dict[str, Any]:
        """Set properties on a StateTree state.

        Only provided (non-empty) fields are changed. All fields are optional
        beyond asset_path and state_name.

        Args:
            asset_path: Content path of the StateTree asset.
            state_name: Name of the state to modify.
            name: New display name.
            type: State/Group/Linked/LinkedAsset/Subtree.
            selection_behavior: TryEnterState, TrySelectChildrenInOrder,
                                TrySelectChildrenAtRandom, etc.
            tasks_completion: Any or All.
            tag: GameplayTag string.
            enabled: Whether the state is enabled.

        Returns:
            Dict with success, state_name, changed.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "asset_path": asset_path,
                "state_name": state_name,
            }
            if name:
                params["name"] = name
            if type:
                params["type"] = type
            if selection_behavior:
                params["selection_behavior"] = selection_behavior
            if tasks_completion:
                params["tasks_completion"] = tasks_completion
            if tag:
                params["tag"] = tag
            if enabled is not None:
                params["enabled"] = enabled
            response = unreal.send_command("set_state_tree_state_property", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error setting state property: {e}"}

    # -----------------------------------------------------------------------
    # Write – Node manipulation
    # -----------------------------------------------------------------------

    @mcp.tool()
    def add_state_tree_task(
        ctx: Context,
        asset_path: str,
        state_name: str,
        task_type: str,
    ) -> Dict[str, Any]:
        """Add a task node to a state in the StateTree.

        Args:
            asset_path: Content path of the StateTree asset.
            state_name: Name of the state to add the task to.
            task_type: C++ struct name of the task,
                       e.g. "FStateTreeRunParallelStateTreeTask" or a custom
                       task struct. The "F" prefix is optional.

        Returns:
            Dict with success, node_id (GUID for subsequent property edits),
            task_type, state_name.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "asset_path": asset_path,
                "state_name": state_name,
                "task_type": task_type,
            }
            response = unreal.send_command("add_state_tree_task", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error adding task: {e}"}

    @mcp.tool()
    def add_state_tree_transition(
        ctx: Context,
        asset_path: str,
        state_name: str,
        trigger: str,
        transition_type: str,
        target_state_name: str = "",
        event_tag: str = "",
        priority: str = "Normal",
        delay_transition: bool = False,
        delay_duration: float = 0.0,
    ) -> Dict[str, Any]:
        """Add a transition to a state in the StateTree.

        Args:
            asset_path: Content path of the StateTree asset.
            state_name: Name of the state to add the transition to.
            trigger: OnStateCompleted, OnStateSucceeded, OnStateFailed,
                     OnTick, OnEvent, OnDelegate.
            transition_type: None, Succeeded, Failed, GotoState, NextState,
                             NextSelectableState.
            target_state_name: Required when transition_type is GotoState.
            event_tag: GameplayTag for OnEvent triggers.
            priority: Normal or High.
            delay_transition: Whether to delay the transition.
            delay_duration: Delay in seconds.

        Returns:
            Dict with success, transition_id, state_name, trigger,
            transition_type.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "asset_path": asset_path,
                "state_name": state_name,
                "trigger": trigger,
                "transition_type": transition_type,
            }
            if target_state_name:
                params["target_state_name"] = target_state_name
            if event_tag:
                params["event_tag"] = event_tag
            if priority != "Normal":
                params["priority"] = priority
            if delay_transition:
                params["delay_transition"] = True
                params["delay_duration"] = delay_duration
            response = unreal.send_command("add_state_tree_transition", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error adding transition: {e}"}

    @mcp.tool()
    def set_state_tree_node_property(
        ctx: Context,
        asset_path: str,
        node_id: str,
        property_name: str,
        property_value: Any,
        target: str = "instance",
    ) -> Dict[str, Any]:
        """Set a property on a StateTree node's detail panel.

        Use get_state_tree_node_properties to discover available properties.

        Args:
            asset_path: Content path of the StateTree asset.
            node_id: GUID of the node.
            property_name: Name of the UPROPERTY field to set.
            property_value: New value. Type depends on the property:
                - bool → true/false
                - int/float → number
                - enum → string name of enum value
                - FGameplayTag → tag string
                - object ref → asset path string
                - struct → exported text string
            target: Which struct to modify: "instance" (default, the instance
                    data shown in details) or "node" (the node definition).

        Returns:
            Dict with success, node_id, property_name, target.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {
                "asset_path": asset_path,
                "node_id": node_id,
                "property_name": property_name,
                "property_value": property_value,
                "target": target,
            }
            response = unreal.send_command("set_state_tree_node_property", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error setting node property: {e}"}

    # -----------------------------------------------------------------------
    # Compile
    # -----------------------------------------------------------------------

    @mcp.tool()
    def compile_state_tree(
        ctx: Context,
        asset_path: str,
    ) -> Dict[str, Any]:
        """Compile a StateTree asset, baking editor data into runtime format.

        Should be called after making structural changes (adding states,
        tasks, transitions) to ensure the runtime data is up to date.

        Args:
            asset_path: Content path of the StateTree asset.

        Returns:
            Dict with success, message, asset_path.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("compile_state_tree", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error compiling StateTree: {e}"}
