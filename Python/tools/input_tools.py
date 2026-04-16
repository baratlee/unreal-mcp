"""
Enhanced Input Tools for Unreal MCP.

Editor-only tools for reading UInputAction and UInputMappingContext data assets.
"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_input_tools(mcp: FastMCP):
    """Register Enhanced Input tools with the MCP server."""

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
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "get_input_action_info",
                {"asset_path": asset_path},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting input action info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

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
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "get_input_mapping_context_info",
                {"asset_path": asset_path},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting input mapping context info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
