"""
Enhanced Input Tools for Unreal MCP.

Editor-only tools for reading and writing UInputAction and UInputMappingContext
data assets, including modifiers and triggers on individual key mappings.
"""

import logging
from typing import Dict, Any, Optional
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def _send(command: str, params: dict) -> Dict[str, Any]:
    """Send a command to Unreal and return the unwrapped result."""
    from unreal_mcp_server import get_unreal_connection

    unreal = get_unreal_connection()
    if not unreal:
        return {"success": False, "message": "Failed to connect to Unreal Engine"}

    response = unreal.send_command(command, params)
    if not response:
        return {"success": False, "message": "No response from Unreal Engine"}

    if response.get("status") == "error":
        return {"success": False, "message": response.get("error", "Unknown error")}

    return response.get("result", response)


def register_input_tools(mcp: FastMCP):
    """Register Enhanced Input tools with the MCP server."""

    # ── Readers ──────────────────────────────────────────────

    @mcp.tool()
    def get_input_action_info(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Read an InputAction (IA) data asset.

        Returns the action's ValueType, Triggers, Modifiers, and flags.

        Args:
            asset_path: Asset path of the InputAction,
                e.g. "/Game/ThirdParty/GameAnimationSample/Input/IA_Move"

        Returns:
            Dict with asset_path, value_type (Boolean/Axis1D/Axis2D/Axis3D),
            description, consume_input, trigger_when_paused,
            reserve_all_mappings, trigger_count, triggers[], modifier_count,
            modifiers[].
        """
        return _send("get_input_action_info", {"asset_path": asset_path})

    @mcp.tool()
    def get_input_mapping_context_info(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Read an InputMappingContext (IMC) data asset.

        Returns every key mapping with its bound InputAction, key, triggers,
        and modifiers.

        Args:
            asset_path: Asset path of the InputMappingContext,
                e.g. "/Game/ThirdParty/GameAnimationSample/Input/IMC_Sandbox"

        Returns:
            Dict with asset_path, description, mapping_count,
            mappings[{action, action_name, value_type, key, trigger_count,
            triggers[], modifier_count, modifiers[]}].
        """
        return _send("get_input_mapping_context_info", {"asset_path": asset_path})

    # ── Writers ──────────────────────────────────────────────

    @mcp.tool()
    def create_input_action(
        ctx: Context,
        asset_path: str,
        value_type: str = "Boolean",
        description: str = "",
        consume_input: Optional[bool] = None,
        trigger_when_paused: Optional[bool] = None,
        reserve_all_mappings: Optional[bool] = None,
    ) -> Dict[str, Any]:
        """Create a new Enhanced Input Action (IA) data asset.

        Args:
            asset_path: Where to create the asset,
                e.g. "/Game/Input/IA_MyAction"
            value_type: "Boolean", "Axis1D", "Axis2D", or "Axis3D"
            description: Human-readable description for the action
            consume_input: Whether the action consumes the input
            trigger_when_paused: Whether the action triggers when paused
            reserve_all_mappings: Whether to reserve all mappings

        Returns:
            Dict with success, asset_path, value_type.
        """
        params: dict = {"asset_path": asset_path, "value_type": value_type}
        if description:
            params["description"] = description
        if consume_input is not None:
            params["consume_input"] = consume_input
        if trigger_when_paused is not None:
            params["trigger_when_paused"] = trigger_when_paused
        if reserve_all_mappings is not None:
            params["reserve_all_mappings"] = reserve_all_mappings
        return _send("create_input_action", params)

    @mcp.tool()
    def set_input_action_property(
        ctx: Context,
        asset_path: str,
        value_type: Optional[str] = None,
        description: Optional[str] = None,
        consume_input: Optional[bool] = None,
        trigger_when_paused: Optional[bool] = None,
        reserve_all_mappings: Optional[bool] = None,
    ) -> Dict[str, Any]:
        """Set properties on an existing InputAction.

        Only provided (non-None) fields are changed.

        Args:
            asset_path: Path of the existing InputAction
            value_type: "Boolean", "Axis1D", "Axis2D", or "Axis3D"
            description: Human-readable description
            consume_input: Whether the action consumes the input
            trigger_when_paused: Whether the action triggers when paused
            reserve_all_mappings: Whether to reserve all mappings

        Returns:
            Dict with success, asset_path, changed[].
        """
        params: dict = {"asset_path": asset_path}
        if value_type is not None:
            params["value_type"] = value_type
        if description is not None:
            params["description"] = description
        if consume_input is not None:
            params["consume_input"] = consume_input
        if trigger_when_paused is not None:
            params["trigger_when_paused"] = trigger_when_paused
        if reserve_all_mappings is not None:
            params["reserve_all_mappings"] = reserve_all_mappings
        return _send("set_input_action_property", params)

    @mcp.tool()
    def create_input_mapping_context(
        ctx: Context,
        asset_path: str,
        description: str = "",
    ) -> Dict[str, Any]:
        """Create a new Enhanced Input Mapping Context (IMC) data asset.

        Args:
            asset_path: Where to create the asset,
                e.g. "/Game/Input/IMC_Default"
            description: Human-readable description

        Returns:
            Dict with success, asset_path.
        """
        params: dict = {"asset_path": asset_path}
        if description:
            params["description"] = description
        return _send("create_input_mapping_context", params)

    @mcp.tool()
    def add_imc_mapping(
        ctx: Context,
        asset_path: str,
        action_path: str,
        key: str,
    ) -> Dict[str, Any]:
        """Add a key mapping to an InputMappingContext.

        Args:
            asset_path: IMC asset path
            action_path: InputAction asset path to bind
            key: Key name, e.g. "W", "SpaceBar", "Gamepad_Left2D",
                 "LeftMouseButton"

        Returns:
            Dict with success, asset_path, mapping_index, action, key.
        """
        return _send("add_imc_mapping", {
            "asset_path": asset_path,
            "action_path": action_path,
            "key": key,
        })

    @mcp.tool()
    def remove_imc_mapping(
        ctx: Context,
        asset_path: str,
        mapping_index: Optional[int] = None,
        action_path: Optional[str] = None,
        key: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Remove a key mapping from an InputMappingContext.

        Provide EITHER mapping_index (precise) OR action_path+key (removes
        all mappings for that action+key pair).

        Args:
            asset_path: IMC asset path
            mapping_index: Index of the mapping to remove
            action_path: InputAction path (used with key)
            key: Key name (used with action_path)

        Returns:
            Dict with success, asset_path, remaining_mappings.
        """
        params: dict = {"asset_path": asset_path}
        if mapping_index is not None:
            params["mapping_index"] = mapping_index
        if action_path is not None:
            params["action_path"] = action_path
        if key is not None:
            params["key"] = key
        return _send("remove_imc_mapping", params)

    @mcp.tool()
    def add_imc_mapping_modifier(
        ctx: Context,
        asset_path: str,
        mapping_index: int,
        modifier_class: str,
        # DeadZone
        lower_threshold: Optional[float] = None,
        upper_threshold: Optional[float] = None,
        dead_zone_type: Optional[str] = None,
        # Negate
        negate_x: Optional[bool] = None,
        negate_y: Optional[bool] = None,
        negate_z: Optional[bool] = None,
        # SwizzleAxis
        order: Optional[str] = None,
        # Scalar
        scalar_x: Optional[float] = None,
        scalar_y: Optional[float] = None,
        scalar_z: Optional[float] = None,
        # FOVScaling
        fov_scale: Optional[float] = None,
        # ResponseCurveExponential
        curve_exponent_x: Optional[float] = None,
        curve_exponent_y: Optional[float] = None,
        curve_exponent_z: Optional[float] = None,
    ) -> Dict[str, Any]:
        """Add a modifier to a specific mapping in an InputMappingContext.

        Args:
            asset_path: IMC asset path
            mapping_index: Index of the mapping (from get_input_mapping_context_info
                or add_imc_mapping)
            modifier_class: One of: "DeadZone", "Negate", "SwizzleAxis",
                "Scalar", "FOVScaling", "ResponseCurveExponential"

            DeadZone params:
                lower_threshold, upper_threshold: float
                dead_zone_type: "Axial" or "Radial"

            Negate params:
                negate_x, negate_y, negate_z: bool

            SwizzleAxis params:
                order: "YXZ", "ZYX", "XZY", "YZX", or "ZXY"

            Scalar params:
                scalar_x, scalar_y, scalar_z: float

            FOVScaling params:
                fov_scale: float

            ResponseCurveExponential params:
                curve_exponent_x, curve_exponent_y, curve_exponent_z: float

        Returns:
            Dict with success, asset_path, mapping_index, modifier_class,
            modifier_count.
        """
        params: dict = {
            "asset_path": asset_path,
            "mapping_index": mapping_index,
            "modifier_class": modifier_class,
        }
        for name, val in [
            ("lower_threshold", lower_threshold),
            ("upper_threshold", upper_threshold),
            ("dead_zone_type", dead_zone_type),
            ("negate_x", negate_x),
            ("negate_y", negate_y),
            ("negate_z", negate_z),
            ("order", order),
            ("scalar_x", scalar_x),
            ("scalar_y", scalar_y),
            ("scalar_z", scalar_z),
            ("fov_scale", fov_scale),
            ("curve_exponent_x", curve_exponent_x),
            ("curve_exponent_y", curve_exponent_y),
            ("curve_exponent_z", curve_exponent_z),
        ]:
            if val is not None:
                params[name] = val
        return _send("add_imc_mapping_modifier", params)

    @mcp.tool()
    def remove_imc_mapping_modifier(
        ctx: Context,
        asset_path: str,
        mapping_index: int,
        modifier_index: int,
    ) -> Dict[str, Any]:
        """Remove a modifier from a specific mapping in an InputMappingContext.

        Args:
            asset_path: IMC asset path
            mapping_index: Index of the mapping
            modifier_index: Index of the modifier to remove (from
                get_input_mapping_context_info)

        Returns:
            Dict with success, asset_path, mapping_index, remaining_modifiers.
        """
        return _send("remove_imc_mapping_modifier", {
            "asset_path": asset_path,
            "mapping_index": mapping_index,
            "modifier_index": modifier_index,
        })

    @mcp.tool()
    def set_imc_mapping_modifier(
        ctx: Context,
        asset_path: str,
        mapping_index: int,
        modifier_index: int,
        # DeadZone
        lower_threshold: Optional[float] = None,
        upper_threshold: Optional[float] = None,
        dead_zone_type: Optional[str] = None,
        # Negate
        negate_x: Optional[bool] = None,
        negate_y: Optional[bool] = None,
        negate_z: Optional[bool] = None,
        # SwizzleAxis
        order: Optional[str] = None,
        # Scalar
        scalar_x: Optional[float] = None,
        scalar_y: Optional[float] = None,
        scalar_z: Optional[float] = None,
        # FOVScaling
        fov_scale: Optional[float] = None,
        # ResponseCurveExponential
        curve_exponent_x: Optional[float] = None,
        curve_exponent_y: Optional[float] = None,
        curve_exponent_z: Optional[float] = None,
    ) -> Dict[str, Any]:
        """Modify properties of an existing modifier on a mapping.

        Only the properties you pass will be updated; others are left
        unchanged.  The modifier type is detected automatically from the
        existing object — you do NOT need to specify modifier_class.

        Args:
            asset_path: IMC asset path
            mapping_index: Index of the mapping
            modifier_index: Index of the modifier to update

            DeadZone params:
                lower_threshold, upper_threshold: float
                dead_zone_type: "Axial" or "Radial"

            Negate params:
                negate_x, negate_y, negate_z: bool

            SwizzleAxis params:
                order: "YXZ", "ZYX", "XZY", "YZX", or "ZXY"

            Scalar params:
                scalar_x, scalar_y, scalar_z: float

            FOVScaling params:
                fov_scale: float

            ResponseCurveExponential params:
                curve_exponent_x, curve_exponent_y, curve_exponent_z: float

        Returns:
            Dict with success, asset_path, mapping_index, modifier_index,
            modifier_class.
        """
        params: dict = {
            "asset_path": asset_path,
            "mapping_index": mapping_index,
            "modifier_index": modifier_index,
        }
        for name, val in [
            ("lower_threshold", lower_threshold),
            ("upper_threshold", upper_threshold),
            ("dead_zone_type", dead_zone_type),
            ("negate_x", negate_x),
            ("negate_y", negate_y),
            ("negate_z", negate_z),
            ("order", order),
            ("scalar_x", scalar_x),
            ("scalar_y", scalar_y),
            ("scalar_z", scalar_z),
            ("fov_scale", fov_scale),
            ("curve_exponent_x", curve_exponent_x),
            ("curve_exponent_y", curve_exponent_y),
            ("curve_exponent_z", curve_exponent_z),
        ]:
            if val is not None:
                params[name] = val
        return _send("set_imc_mapping_modifier", params)

    @mcp.tool()
    def add_imc_mapping_trigger(
        ctx: Context,
        asset_path: str,
        mapping_index: int,
        trigger_class: str,
        # Common
        actuation_threshold: Optional[float] = None,
        # Hold / HoldAndRelease
        hold_time_threshold: Optional[float] = None,
        # Hold
        is_one_shot: Optional[bool] = None,
        # Tap
        tap_release_time_threshold: Optional[float] = None,
        # Pulse
        trigger_on_start: Optional[bool] = None,
        interval: Optional[float] = None,
        trigger_limit: Optional[int] = None,
        # ChordAction
        chord_action_path: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Add a trigger to a specific mapping in an InputMappingContext.

        Args:
            asset_path: IMC asset path
            mapping_index: Index of the mapping
            trigger_class: One of: "Down", "Pressed", "Released", "Hold",
                "HoldAndRelease", "Tap", "Pulse", "ChordAction"

            Common params:
                actuation_threshold: float (default ~0.5)

            Hold params:
                hold_time_threshold: float (seconds)
                is_one_shot: bool

            HoldAndRelease params:
                hold_time_threshold: float

            Tap params:
                tap_release_time_threshold: float

            Pulse params:
                trigger_on_start: bool
                interval: float (seconds)
                trigger_limit: int (0 = unlimited)

            ChordAction params:
                chord_action_path: asset path of the chord InputAction

        Returns:
            Dict with success, asset_path, mapping_index, trigger_class,
            trigger_count.
        """
        params: dict = {
            "asset_path": asset_path,
            "mapping_index": mapping_index,
            "trigger_class": trigger_class,
        }
        for name, val in [
            ("actuation_threshold", actuation_threshold),
            ("hold_time_threshold", hold_time_threshold),
            ("is_one_shot", is_one_shot),
            ("tap_release_time_threshold", tap_release_time_threshold),
            ("trigger_on_start", trigger_on_start),
            ("interval", interval),
            ("trigger_limit", trigger_limit),
            ("chord_action_path", chord_action_path),
        ]:
            if val is not None:
                params[name] = val
        return _send("add_imc_mapping_trigger", params)

    @mcp.tool()
    def remove_imc_mapping_trigger(
        ctx: Context,
        asset_path: str,
        mapping_index: int,
        trigger_index: int,
    ) -> Dict[str, Any]:
        """Remove a trigger from a specific mapping in an InputMappingContext.

        Args:
            asset_path: IMC asset path
            mapping_index: Index of the mapping
            trigger_index: Index of the trigger to remove (from
                get_input_mapping_context_info)

        Returns:
            Dict with success, asset_path, mapping_index, remaining_triggers.
        """
        return _send("remove_imc_mapping_trigger", {
            "asset_path": asset_path,
            "mapping_index": mapping_index,
            "trigger_index": trigger_index,
        })

    @mcp.tool()
    def set_imc_mapping_trigger(
        ctx: Context,
        asset_path: str,
        mapping_index: int,
        trigger_index: int,
        # Common
        actuation_threshold: Optional[float] = None,
        # Hold / HoldAndRelease
        hold_time_threshold: Optional[float] = None,
        # Hold
        is_one_shot: Optional[bool] = None,
        # Tap
        tap_release_time_threshold: Optional[float] = None,
        # Pulse
        trigger_on_start: Optional[bool] = None,
        interval: Optional[float] = None,
        trigger_limit: Optional[int] = None,
        # ChordAction
        chord_action_path: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Modify properties of an existing trigger on a mapping.

        Only the properties you pass will be updated; others are left
        unchanged.  The trigger type is detected automatically from the
        existing object — you do NOT need to specify trigger_class.

        Args:
            asset_path: IMC asset path
            mapping_index: Index of the mapping
            trigger_index: Index of the trigger to update

            Common params:
                actuation_threshold: float (default ~0.5)

            Hold params:
                hold_time_threshold: float (seconds)
                is_one_shot: bool

            HoldAndRelease params:
                hold_time_threshold: float

            Tap params:
                tap_release_time_threshold: float

            Pulse params:
                trigger_on_start: bool
                interval: float (seconds)
                trigger_limit: int (0 = unlimited)

            ChordAction params:
                chord_action_path: asset path of the chord InputAction

        Returns:
            Dict with success, asset_path, mapping_index, trigger_index,
            trigger_class.
        """
        params: dict = {
            "asset_path": asset_path,
            "mapping_index": mapping_index,
            "trigger_index": trigger_index,
        }
        for name, val in [
            ("actuation_threshold", actuation_threshold),
            ("hold_time_threshold", hold_time_threshold),
            ("is_one_shot", is_one_shot),
            ("tap_release_time_threshold", tap_release_time_threshold),
            ("trigger_on_start", trigger_on_start),
            ("interval", interval),
            ("trigger_limit", trigger_limit),
            ("chord_action_path", chord_action_path),
        ]:
            if val is not None:
                params[name] = val
        return _send("set_imc_mapping_trigger", params)

    logger.info("Enhanced Input tools registered successfully (readers + writers)")
