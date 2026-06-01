"""
Material Tools for Unreal MCP.

Read-only inspection for UMaterial / UMaterialInstance /
UMaterialParameterCollection assets.

P0 batch (2026-05-22): three info commands. Graph traversal (per-node
inspection of the material expression network) is deferred to P1.
"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_material_tools(mcp: FastMCP):
    """Register material inspection tools with the MCP server."""

    @mcp.tool()
    def get_material_info(
        ctx: Context,
        asset_path: str,
    ) -> Dict[str, Any]:
        """Inspect a UMaterial asset (base material, not an instance).

        Returns material settings (domain / blend mode / shading models /
        two-sided flags / post-process blendable fields when applicable /
        per-feature Usage flags) plus the full parameter list with default
        values (scalar, vector, texture, static switch). Parameter counts
        are also reported under `parameter_counts`.

        For UMaterialInstance assets, use `get_material_instance_info`
        instead — this command rejects non-Material assets.

        Args:
            asset_path: Full asset path, e.g. "/Game/Examples/Leo/M_PP_DreamRipple"
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_material_info", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"get_material_info response for: {asset_path}")
            return response

        except Exception as e:
            error_msg = f"Error getting material info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_material_instance_info(
        ctx: Context,
        asset_path: str,
    ) -> Dict[str, Any]:
        """Inspect a UMaterialInstance asset (Constant or Dynamic).

        Returns the direct `parent` plus the resolved base UMaterial,
        the lists of overridden scalar / vector / texture parameters
        (only values explicitly stored on this instance — not the union
        with the parent), and `base_property_overrides` (BlendMode /
        TwoSided / OpacityMaskClipValue / DitheredLODTransition /
        CastDynamicShadowAsMasked, each with its `bOverride_*` flag).

        Static switch overrides are not included in this P0 batch (planned
        for P1). For base material properties walk up to the parent via
        `base_material_path` and call `get_material_info`.

        Args:
            asset_path: Full asset path, e.g. "/Game/.../MI_MyInstance"
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_material_instance_info", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"get_material_instance_info response for: {asset_path}")
            return response

        except Exception as e:
            error_msg = f"Error getting material instance info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_material_graph(
        ctx: Context,
        asset_path: str,
        pin_payload_mode: str = "summary",
        include_comments: bool = True,
        include_root_inputs: bool = True,
    ) -> Dict[str, Any]:
        """Read the full expression graph of a UMaterial: nodes + connections.

        Walks `UMaterial::GetExpressionCollection().Expressions` and dumps:
          - `nodes`: each expression as {guid, class, position, outputs[], inputs[], properties{}}
          - `connections`: every wired input as {source_guid, source_output_index,
            target_guid, target_input_index, target_input_name}
          - `comments` (optional): UMaterialExpressionComment list with text + size
          - `root_inputs` (optional): the Material's root pins on UMaterialEditorOnlyData
            (BaseColor / EmissiveColor / Roughness / etc.) and which expression each is
            wired to

        Nested Material Function bodies are NOT expanded in this P1 batch — the
        UMaterialExpressionMaterialFunctionCall node appears as a single node with
        its `MaterialFunction` reference in `properties`. Function-internal graph
        traversal is a P2 task.

        Args:
            asset_path: Full asset path, e.g. "/Game/Examples/Leo/M_PP_DreamRipple"
            pin_payload_mode: "names_only" (just guid+class — list mode) /
                "summary" (default — adds position, input/output names,
                only simple-value UPROPERTYs like ParameterName/DefaultValue/etc.) /
                "full" (every non-input UPROPERTY, including object references).
            include_comments: Include UMaterialExpressionComment nodes (default True).
            include_root_inputs: Include the Material's root pin wiring (default True).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "pin_payload_mode": pin_payload_mode,
                "include_comments": include_comments,
                "include_root_inputs": include_root_inputs,
            }
            response = unreal.send_command("get_material_graph", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"get_material_graph response for: {asset_path}")
            return response

        except Exception as e:
            error_msg = f"Error getting material graph: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_material_parameter_collection_info(
        ctx: Context,
        asset_path: str,
    ) -> Dict[str, Any]:
        """Inspect a UMaterialParameterCollection (MPC) asset.

        Returns the scalar and vector parameter declarations on the MPC
        (name + default value + parameter GUID). MPCs only own
        declarations; per-instance values are set at runtime via
        `UKismetMaterialLibrary::SetScalarParameterValue` etc. against a
        UWorld and are not part of the asset.

        Args:
            asset_path: Full asset path, e.g. "/Game/.../MPC_GlobalParams"
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_material_parameter_collection_info", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"get_material_parameter_collection_info response for: {asset_path}")
            return response

        except Exception as e:
            error_msg = f"Error getting MPC info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
