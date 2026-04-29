"""
Data Asset Tools for Unreal MCP.

Provides tools for reading, writing, and listing UDataAsset instances
via UE reflection, without requiring asset-type-specific handlers.
"""

import logging
from typing import Dict, List, Any, Optional
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_data_asset_tools(mcp: FastMCP):
    """Register data asset tools with the MCP server."""

    @mcp.tool()
    def get_data_asset_info(
        ctx: Context,
        asset_path: str,
        max_depth: int = 2,
        category_filter: str = ""
    ) -> Dict[str, Any]:
        """Get all editable properties from a DataAsset (or any UObject asset).

        Uses UE reflection to read every UPROPERTY(EditAnywhere) on the asset.
        Works with UDataAsset, UPrimaryDataAsset, and any other saved UObject.

        Args:
            asset_path: Full asset path, e.g. "/Game/Data/DA_MyConfig"
            max_depth: How deep to recurse into nested object properties (1-4, default 2)
            category_filter: Optional category substring to filter properties
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {"asset_path": asset_path, "max_depth": max_depth}
            if category_filter:
                params["category_filter"] = category_filter

            response = unreal.send_command("get_data_asset_info", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"get_data_asset_info response for: {asset_path}")
            return response

        except Exception as e:
            error_msg = f"Error getting data asset info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_data_asset_property(
        ctx: Context,
        asset_path: str,
        property_name: str,
        property_value
    ) -> Dict[str, Any]:
        """Set a property on a DataAsset (or any UObject asset).

        Uses UE reflection to set any editable property. Supports bool, int,
        float, string, enum (by name or value), FName, and more.
        The asset is marked dirty after modification — save from the editor
        or call save_asset to persist.

        Args:
            asset_path: Full asset path, e.g. "/Game/Data/DA_MyConfig"
            property_name: Name of the UPROPERTY to set
            property_value: New value (type must be compatible with the property)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "property_name": property_name,
                "property_value": property_value
            }

            logger.info(f"Setting data asset property: {asset_path}.{property_name}")
            response = unreal.send_command("set_data_asset_property", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"set_data_asset_property response: {response}")
            return response

        except Exception as e:
            error_msg = f"Error setting data asset property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def list_data_assets(
        ctx: Context,
        class_name: str = "",
        search_path: str = ""
    ) -> Dict[str, Any]:
        """List DataAsset instances in the project, optionally filtered by class or path.

        Uses the AssetRegistry to enumerate assets. Defaults to listing all
        UDataAsset subclasses if no class_name is specified.

        Args:
            class_name: Optional UClass name to filter by (e.g. "MyGameDataAsset").
                        Defaults to "DataAsset" (includes all subclasses).
            search_path: Optional content path to restrict the search
                         (e.g. "/Game/Data"). Searches recursively.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {}
            if class_name:
                params["class_name"] = class_name
            if search_path:
                params["search_path"] = search_path

            response = unreal.send_command("list_data_assets", params)

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"list_data_assets found {response.get('count', '?')} assets")
            return response

        except Exception as e:
            error_msg = f"Error listing data assets: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
