"""
Project Tools for Unreal MCP.

This module provides tools for managing project-wide settings and configuration.
"""

import logging
from typing import Dict, Any, List
from mcp.server.fastmcp import FastMCP, Context

# Get logger
logger = logging.getLogger("UnrealMCP")

def register_project_tools(mcp: FastMCP):
    """Register project tools with the MCP server."""
    
    @mcp.tool()
    def create_input_mapping(
        ctx: Context,
        action_name: str,
        key: str,
        input_type: str = "Action"
    ) -> Dict[str, Any]:
        """
        Create an input mapping for the project.
        
        Args:
            action_name: Name of the input action
            key: Key to bind (SpaceBar, LeftMouseButton, etc.)
            input_type: Type of input mapping (Action or Axis)
            
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
                "action_name": action_name,
                "key": key,
                "input_type": input_type
            }
            
            logger.info(f"Creating input mapping '{action_name}' with key '{key}'")
            response = unreal.send_command("create_input_mapping", params)
            
            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "message": "No response from Unreal Engine"}
            
            logger.info(f"Input mapping creation response: {response}")
            return response
            
        except Exception as e:
            error_msg = f"Error creating input mapping: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
    
    @mcp.tool()
    def get_project_info(ctx: Context) -> Dict[str, Any]:
        """Get the current Unreal Engine project information.

        Returns project name, project directory, .uproject file path,
        engine version, and the currently loaded level name.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_project_info", {})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting project info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def batch_read(
        ctx: Context,
        operations: List[Dict[str, Any]],
        stop_on_error: bool = False,
    ) -> Dict[str, Any]:
        """Run 1-8 independent allowlisted read commands in one Unreal round trip.

        Each operation is {"id": str, "command": str, "params": object}. Writes,
        nested batches, and unlisted commands are rejected per operation.
        """
        from unreal_mcp_server import get_unreal_connection

        if not 1 <= len(operations) <= 8:
            return {"success": False, "message": "operations must contain 1-8 entries"}

        unreal = get_unreal_connection()
        if not unreal:
            return {"success": False, "message": "Failed to connect to Unreal Engine"}

        response = unreal.send_command("batch_read", {
            "operations": operations,
            "stop_on_error": stop_on_error,
        })
        if not response:
            return {"success": False, "message": "No response from Unreal Engine"}
        if response.get("status") == "error":
            return {"success": False, "message": response.get("error", "Unknown error")}
        return response.get("result", response)

    logger.info("Project tools registered successfully")
