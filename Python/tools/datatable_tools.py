"""
DataTable Tools for Unreal MCP.

Provides read-only inspection of UDataTable assets.
"""

import logging
from typing import Dict, Any, Optional
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_datatable_tools(mcp: FastMCP):
    """Register DataTable tools with the MCP server."""

    @mcp.tool()
    def get_datatable_info(
        ctx: Context,
        asset_path: str,
        row_name: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Read the contents of a UDataTable asset.

        Args:
            asset_path: Full asset path, e.g. "/Game/Data/DT_CharacterStats".
                        The ".AssetName" suffix is added automatically if missing.
            row_name:   Optional row key. When provided, only that row is returned.
                        Omit to return all rows.

        Returns:
            {
              asset_path, row_struct, columns: [str],
              rows: {row_name: {field: value}},
              row_count: int,
              row_filter (if row_name was given),
              warning (if row_name was given but not found),
              rows_raw (only if JSON parsing failed — fallback raw string),
            }
        """
        from unreal_mcp_server import get_unreal_connection
        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params: Dict[str, Any] = {"asset_path": asset_path}
            if row_name:
                params["row_name"] = row_name
            response = unreal.send_command("get_datatable_info", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            err = f"Error getting DataTable info: {e}"
            logger.error(err)
            return {"success": False, "message": err}

    logger.info("DataTable tools registered successfully")
