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

    @mcp.tool()
    def find_animations_for_skeleton(
        ctx: Context,
        skeleton_path: str,
        include_montages: bool = True,
        path_filter: str = "",
    ) -> Dict[str, Any]:
        """Find AnimSequence/AnimMontage assets that reference the given Skeleton.

        Uses the Asset Registry, so assets do not have to be loaded. Scans the
        whole project by default; pass `path_filter` to narrow the search to a
        specific content path like "/Game/ThirdParty/GameAnimationSample".

        Args:
            skeleton_path: Skeleton object path, e.g.
                "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin.SK_UEFN_Mannequin"
                (the ".LeafName" suffix may be omitted).
            include_montages: If True, also returns AnimMontage assets. Default True.
            path_filter: Optional content root to restrict the search.

        Returns:
            Dict with skeleton_path, include_montages, path_filter, total_count,
            sequence_count, montage_count, and an `assets` list where each entry has
            `path` and `class`.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "find_animations_for_skeleton",
                {
                    "skeleton_path": skeleton_path,
                    "include_montages": include_montages,
                    "path_filter": path_filter,
                },
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error finding animations for skeleton: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_skeleton_bone_hierarchy(ctx: Context, skeleton_path: str) -> Dict[str, Any]:
        """Get the raw bone hierarchy of a USkeleton asset.

        Returns each bone's index, name, parent_index, parent_name and the list of
        direct child indices, plus any virtual bones defined on the skeleton.

        Args:
            skeleton_path: Full skeleton object path, e.g.
                "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin.SK_UEFN_Mannequin"

        Returns:
            Dict with skeleton_path, bone_count, bones list, virtual_bone_count, virtual_bones list.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "get_skeleton_bone_hierarchy",
                {"skeleton_path": skeleton_path},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting skeleton bone hierarchy: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def list_chooser_tables(ctx: Context, path_filter: str = "") -> Dict[str, Any]:
        """List all UChooserTable assets in the project via the Asset Registry.

        Scans the whole project by default; pass `path_filter` to narrow the
        search to a specific content path such as
        "/Game/ThirdParty/GameAnimationSample".

        Args:
            path_filter: Optional content root to restrict the search.

        Returns:
            Dict with path_filter, total_count, and an `assets` list where each
            entry has `path` and `class`.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "list_chooser_tables",
                {"path_filter": path_filter},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error listing chooser tables: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_chooser_table_info(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Read the static structure of a UChooserTable asset (Editor-only, read-only).

        Returns the column list (each column's struct name and editor-only
        RowValues property name) and the result rows. Each result row is
        classified by its FObjectChooserBase subclass:

        - `asset`            → row's `asset_path` / `asset_class` point at a hard UObject reference
        - `soft_asset`       → row's `soft_path` holds a TSoftObjectPtr
        - `evaluate_chooser` → row references another ChooserTable (`nested_chooser_path`)
        - `nested_chooser`   → row references another ChooserTable (`nested_chooser_path`)
        - `unknown` / `empty` → unrecognised or uninitialised row

        Per-cell condition values (float ranges, bool masks, gameplay tags, etc.)
        are NOT returned; only the structural outline of the table is exposed.

        Args:
            asset_path: Full ChooserTable object path, e.g.
                "/Game/.../CT_Locomotion.CT_Locomotion"

        Returns:
            Dict with asset_path, asset_class, is_cooked_data, column_count,
            columns list, row_count, used_cooked_results, results list.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "get_chooser_table_info",
                {"asset_path": asset_path},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting chooser table info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    logger.info("Animation tools registered successfully")
