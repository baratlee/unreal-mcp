"""
Animation Tools for Unreal MCP.

Editor-only tools for reading data from AnimSequence / AnimMontage assets.
"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_animation_tools(mcp: FastMCP):
    """Register animation tools with the MCP server."""

    @mcp.tool()
    def get_animation_info(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Get basic info (play length, num frames, skeleton) for an AnimSequence or AnimMontage.

        Args:
            asset_path: Full asset path, e.g. "/Game/Animations/Montage_Attack.Montage_Attack"

        Returns:
            Dict with asset_path, asset_class, play_length, num_frames, skeleton.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_animation_info", {"asset_path": asset_path})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting animation info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_animation_notifies(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Get anim notify events for an AnimSequence or AnimMontage.

        Args:
            asset_path: Full asset path, e.g. "/Game/Animations/Montage_Attack.Montage_Attack"

        Returns:
            Dict with asset_path, asset_class, notify_count, and notifies list.
            Each notify entry has: notify_name, trigger_time, duration, track_index,
            is_state, is_branching_point, notify_class.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_animation_notifies", {"asset_path": asset_path})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting animation notifies: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_animation_curve_names(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Get float curve names for an AnimSequence or AnimMontage.

        Args:
            asset_path: Full asset path, e.g. "/Game/Animations/Run.Run"

        Returns:
            Dict with asset_path, asset_class, curve_type, curve_count, curves list.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_animation_curve_names", {"asset_path": asset_path})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting animation curve names: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_animation_bone_track_names(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Get bone track names for an AnimSequence or AnimMontage.

        For AnimMontage, walks SlotAnimTracks -> AnimSegments and aggregates the
        underlying AnimSequence tracks, returning both a unioned list and per-segment
        detail.

        Args:
            asset_path: Full asset path, e.g. "/Game/Animations/Montage_Attack.Montage_Attack"

        Returns:
            Dict with asset_path, asset_class, is_montage, bone_track_count, bone_tracks.
            Montage results additionally include a `segments` list describing each
            segment's slot, anim_path, anim_class and tracks.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_animation_bone_track_names", {"asset_path": asset_path})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting animation bone track names: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Animation tools registered successfully")
