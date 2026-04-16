"""
Static Mesh Tools for Unreal MCP.

Editor-only tools for reading UStaticMesh geometry data.
"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_mesh_tools(mcp: FastMCP):
    """Register Static Mesh tools with the MCP server."""

    @mcp.tool()
    def get_static_mesh_info(
        ctx: Context,
        asset_path: str,
        include_vertices: bool = False,
        lod_index: int = 0,
        max_vertices: int = 5000,
    ) -> Dict[str, Any]:
        """Read a UStaticMesh asset's geometry, materials, LODs, and collision.

        Returns basic info (vertex/triangle counts, bounding box, Nanite status,
        lightmap resolution), material slots, per-LOD details, and collision data.
        Optionally includes vertex positions for a specified LOD.

        Args:
            asset_path: Asset path of the StaticMesh,
                e.g. "/Game/Meshes/SM_Wall"
            include_vertices: If True, include vertex positions for the
                specified LOD. Default False (summary only).
            lod_index: Which LOD to return vertex data for (default 0).
                Only used when include_vertices=True.
            max_vertices: Safety cap on returned vertex count (default 5000).
                If the mesh has more vertices, the result is truncated and
                the 'truncated' flag is set to True.

        Returns:
            Dict with asset_path, vertex_count, triangle_count, lod_count,
            nanite_enabled, bounding_box{min,max}, lightmap_resolution,
            material_slot_count, material_slots[], lod_details[],
            collision{collision_type, simple_shapes{}}.
            When include_vertices=True, also includes
            vertices{lod_index, total_vertex_count, returned_count,
            truncated, positions[[x,y,z],...]}.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "include_vertices": include_vertices,
                "lod_index": lod_index,
                "max_vertices": max_vertices,
            }

            response = unreal.send_command("get_static_mesh_info", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting static mesh info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
