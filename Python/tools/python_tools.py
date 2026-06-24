"""
Python Script Execution Tools for Unreal MCP.

Provides tools for executing Python scripts and files inside the UE editor
via IPythonScriptPlugin::ExecPythonCommandEx.
"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_python_tools(mcp: FastMCP):
    """Register Python script execution tools with the MCP server."""

    @mcp.tool()
    def execute_python_script(
        ctx: Context,
        script: str
    ) -> Dict[str, Any]:
        """Execute a Python script string inside the Unreal Editor.

        Runs multi-line Python code in the editor's Python interpreter
        (EPythonCommandExecutionMode::ExecuteFile). Both print() output
        and unreal.log() calls are captured in the 'output' field.
        On failure the Python exception traceback is returned in 'error'.

        Args:
            script: Python source code to execute (may be multi-line)

        Returns:
            {
                "success": bool,
                "output": str,   # combined Info + Warning log entries
                "error":  str    # Error log entries + exception traceback
            }
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "output": "", "error": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("execute_python_script", {"script": script})

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "output": "", "error": "No response from Unreal Engine"}

            logger.info("execute_python_script: success=%s", response.get("success"))
            return response

        except Exception as e:
            error_msg = f"Error executing Python script: {e}"
            logger.error(error_msg)
            return {"success": False, "output": "", "error": error_msg}

    @mcp.tool()
    def execute_python_file(
        ctx: Context,
        file_path: str
    ) -> Dict[str, Any]:
        """Execute a Python script file inside the Unreal Editor.

        Runs a .py file in the editor's Python interpreter
        (EPythonCommandExecutionMode::ExecuteFile). Both print() output
        and unreal.log() calls are captured in the 'output' field.
        On failure the Python exception traceback is returned in 'error'.

        Args:
            file_path: Absolute path to the .py file, e.g.
                       "C:/Workspace/smg_programmer_Leo/ARTS/Projects/PrjKunlun/Content/Python/batch_retarget.py"

        Returns:
            {
                "success": bool,
                "output": str,   # combined Info + Warning log entries
                "error":  str    # Error log entries + exception traceback
            }
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "output": "", "error": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("execute_python_file", {"file_path": file_path})

            if not response:
                logger.error("No response from Unreal Engine")
                return {"success": False, "output": "", "error": "No response from Unreal Engine"}

            logger.info("execute_python_file: path=%s success=%s", file_path, response.get("success"))
            return response

        except Exception as e:
            error_msg = f"Error executing Python file: {e}"
            logger.error(error_msg)
            return {"success": False, "output": "", "error": error_msg}
