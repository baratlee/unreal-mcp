"""
Spline Tools for Unreal MCP.

Edit USplineComponent point data on Blueprint templates (own SCS, inherited
SCS, or native C++ CDO). All writes mutate the component template and mark
the blueprint modified — caller is responsible for compile_blueprint +
save_dirty_assets to persist.

P0 batch (2026-06-01, LT9): get_spline_info, set_spline_points,
  set_spline_point, clear_spline_points.
P1 batch (2026-06-01, LT9): add_spline_point, remove_spline_point,
  set_spline_closed_loop, set_spline_default_up_vector.
"""

import logging
from typing import Dict, Any, List, Optional
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_spline_tools(mcp: FastMCP):
    """Register spline editing tools with the MCP server."""

    @mcp.tool()
    def get_spline_info(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        coordinate_space: str = "Local",
    ) -> Dict[str, Any]:
        """Read all point data from a USplineComponent on a Blueprint template.

        Returns: num_points, closed_loop, and a points[] array — each point has
        index, location[xyz], arrive_tangent[xyz], leave_tangent[xyz],
        rotation[pitch,yaw,roll], scale[xyz], type (Linear/Curve/Constant/
        CurveClamped/CurveCustomTangent).

        Searches the blueprint's own SCS, then inherited SCS chain, then
        native C++ CDO — same lookup as set_component_property.

        Args:
            blueprint_path: Full asset path, e.g. "/Game/.../BP_MyActor"
            component_name: Spline component name, e.g. "HitSpline"
            coordinate_space: "Local" (default) or "World"
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_spline_info", {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "coordinate_space": coordinate_space,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response

        except Exception as e:
            error_msg = f"Error getting spline info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_spline_points(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        points: List[Dict[str, Any]],
        coordinate_space: str = "Local",
        closed_loop: Optional[bool] = None,
    ) -> Dict[str, Any]:
        """Replace all points on a USplineComponent (clear + add N).

        Each entry in `points` is an object with:
          - location: [x, y, z] (required)
          - arrive_tangent: [x, y, z] (optional; if both arrive+leave given,
            tangents set independently; otherwise sets both to arrive)
          - leave_tangent: [x, y, z] (optional)
          - rotation: [pitch, yaw, roll] (optional)
          - scale: [x, y, z] (optional)
          - type: one of "Linear" / "Curve" / "Constant" / "CurveClamped" /
            "CurveCustomTangent" (default "Curve")

        Caller must run compile_blueprint + save_dirty_assets afterward to
        persist changes to disk.

        Args:
            blueprint_path: Full asset path, e.g. "/Game/.../BP_MyActor"
            component_name: Spline component name
            points: List of point dicts (see above)
            coordinate_space: "Local" (default) or "World"
            closed_loop: Optional override; None means leave current setting
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "points": points,
                "coordinate_space": coordinate_space,
            }
            if closed_loop is not None:
                params["closed_loop"] = closed_loop

            response = unreal.send_command("set_spline_points", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response

        except Exception as e:
            error_msg = f"Error setting spline points: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_spline_point(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        index: int,
        location: Optional[List[float]] = None,
        arrive_tangent: Optional[List[float]] = None,
        leave_tangent: Optional[List[float]] = None,
        rotation: Optional[List[float]] = None,
        scale: Optional[List[float]] = None,
        type: Optional[str] = None,
        coordinate_space: str = "Local",
    ) -> Dict[str, Any]:
        """Modify a single existing spline point in place. Only specified
        fields are changed; the rest are preserved.

        Args:
            blueprint_path: Full asset path
            component_name: Spline component name
            index: Point index (0-based)
            location: Optional [x, y, z]
            arrive_tangent: Optional [x, y, z]; if only one tangent specified,
                both tangents are set to that value
            leave_tangent: Optional [x, y, z]
            rotation: Optional [pitch, yaw, roll]
            scale: Optional [x, y, z]
            type: Optional point type string
            coordinate_space: "Local" (default) or "World"
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params: Dict[str, Any] = {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "index": index,
                "coordinate_space": coordinate_space,
            }
            if location is not None: params["location"] = location
            if arrive_tangent is not None: params["arrive_tangent"] = arrive_tangent
            if leave_tangent is not None: params["leave_tangent"] = leave_tangent
            if rotation is not None: params["rotation"] = rotation
            if scale is not None: params["scale"] = scale
            if type is not None: params["type"] = type

            response = unreal.send_command("set_spline_point", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response

        except Exception as e:
            error_msg = f"Error setting spline point: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def clear_spline_points(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
    ) -> Dict[str, Any]:
        """Remove all points from a USplineComponent on a Blueprint template.

        Args:
            blueprint_path: Full asset path
            component_name: Spline component name
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("clear_spline_points", {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response

        except Exception as e:
            error_msg = f"Error clearing spline points: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ----------------------------------------------------------------- P1 -----

    @mcp.tool()
    def add_spline_point(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        location: List[float],
        index: Optional[int] = None,
        arrive_tangent: Optional[List[float]] = None,
        leave_tangent: Optional[List[float]] = None,
        rotation: Optional[List[float]] = None,
        scale: Optional[List[float]] = None,
        type: Optional[str] = None,
        coordinate_space: str = "Local",
    ) -> Dict[str, Any]:
        """Append a spline point at the end, or insert at a specified index.

        If `index` is omitted, -1, or >= current point count, the point is
        appended. Otherwise it's inserted at that position (existing points
        from `index` onward shift by +1).

        Optional per-point fields (type, tangents, rotation, scale) follow
        the same semantics as set_spline_points entries.

        Args:
            blueprint_path: Full asset path
            component_name: Spline component name
            location: [x, y, z]
            index: Insert position (default None = append)
            arrive_tangent / leave_tangent: Optional [x, y, z]
            rotation: Optional [pitch, yaw, roll]
            scale: Optional [x, y, z]
            type: Optional point type string
            coordinate_space: "Local" (default) or "World"
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params: Dict[str, Any] = {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "location": location,
                "coordinate_space": coordinate_space,
            }
            if index is not None: params["index"] = index
            if arrive_tangent is not None: params["arrive_tangent"] = arrive_tangent
            if leave_tangent is not None: params["leave_tangent"] = leave_tangent
            if rotation is not None: params["rotation"] = rotation
            if scale is not None: params["scale"] = scale
            if type is not None: params["type"] = type

            response = unreal.send_command("add_spline_point", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response

        except Exception as e:
            error_msg = f"Error adding spline point: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def remove_spline_point(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        index: int,
    ) -> Dict[str, Any]:
        """Remove a single spline point by index. Points after it shift by -1.

        Args:
            blueprint_path: Full asset path
            component_name: Spline component name
            index: Point index (0-based)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("remove_spline_point", {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "index": index,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response

        except Exception as e:
            error_msg = f"Error removing spline point: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_spline_closed_loop(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        closed_loop: bool,
    ) -> Dict[str, Any]:
        """Toggle whether the spline is a closed loop (last point connects back
        to the first).

        Args:
            blueprint_path: Full asset path
            component_name: Spline component name
            closed_loop: True to enable loop, False to disable
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("set_spline_closed_loop", {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "closed_loop": closed_loop,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response

        except Exception as e:
            error_msg = f"Error setting spline closed loop: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_spline_default_up_vector(
        ctx: Context,
        blueprint_path: str,
        component_name: str,
        up_vector: List[float],
        coordinate_space: str = "Local",
    ) -> Dict[str, Any]:
        """Set the spline's DefaultUpVector — drives rotation interpolation
        when point rotations aren't explicitly authored.

        Args:
            blueprint_path: Full asset path
            component_name: Spline component name
            up_vector: [x, y, z], typically [0, 0, 1] for Z-up
            coordinate_space: "Local" (default) or "World"
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("set_spline_default_up_vector", {
                "blueprint_path": blueprint_path,
                "component_name": component_name,
                "up_vector": up_vector,
                "coordinate_space": coordinate_space,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            return response

        except Exception as e:
            error_msg = f"Error setting spline default up vector: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
