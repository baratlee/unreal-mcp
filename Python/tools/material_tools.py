"""
Material Tools for Unreal MCP.

History:
  2026-05-22 P0   : get_material_info / get_material_instance_info / get_material_parameter_collection_info
  2026-05-22 P1   : get_material_graph
  2026-06-11 P0a  : set_material_expression_property (reflect-write any UMaterialExpression UPROPERTY)
  2026-06-11 P0b  : set_material_instance_{scalar,vector,texture}_parameter (MI overrides)
  2026-06-11 P1   : set_material_property (reflect-write top-level UMaterial UPROPERTY)
"""

import logging
from typing import Dict, Any, List, Union
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

    # ------------------------------------------------------------------
    # Write batch (LT16 2026-06-11, P0a + P0b + P1)
    # ------------------------------------------------------------------

    @mcp.tool()
    def set_material_expression_property(
        ctx: Context,
        material_path: str,
        expression_guid: str,
        property_name: str,
        value: Union[str, int, float, bool, list],
    ) -> Dict[str, Any]:
        """Reflect-write any UPROPERTY on a UMaterialExpression node.

        Locate the expression in `material_path` by `expression_guid` (preferred,
        as returned by `get_material_graph`) or fall back to GetName() match.
        Then set `property_name` to `value` using the shared SetObjectProperty
        reflection (supports dotted nested paths for FObjectProperty / FArrayProperty
        per the 2026-06-09 reflection upgrades).

        Typical uses:
          - Change a Texture Sample's `Texture` ref:
                property_name="Texture", value="/Game/Path/To/T_NewTexture"
          - Set a Scalar Parameter default:
                property_name="DefaultValue", value=0.42
          - Set a Static Switch default:
                property_name="DefaultValue", value=True

        After writing, the material is recompiled (RecompileMaterial) and
        marked dirty. User must Ctrl+S to persist.

        Args:
            material_path: Full UMaterial asset path (e.g. "/Game/.../M_Foo")
            expression_guid: GUID string from `get_material_graph` node entry, or
                the expression's object name if the GUID is not valid.
            property_name: UPROPERTY name on the UMaterialExpression subclass.
                Dotted path supported for nested fields (e.g. "SomeStruct.Field").
            value: The new value. Type depends on the target property
                (string for object refs/Name/Enum, number for scalar, bool, array, ...).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "material_path": material_path,
                "expression_guid": expression_guid,
                "property_name": property_name,
                "value": value,
            }
            response = unreal.send_command("set_material_expression_property", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"set_material_expression_property: {material_path} / {expression_guid} / {property_name}")
            return response

        except Exception as e:
            error_msg = f"Error setting material expression property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_material_instance_scalar_parameter(
        ctx: Context,
        asset_path: str,
        parameter_name: str,
        value: float,
    ) -> Dict[str, Any]:
        """Override a Scalar Parameter on a UMaterialInstanceConstant.

        Calls `UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue`
        which registers the override on the instance (not a transient runtime
        value). Triggers UpdateMaterialInstance + MarkPackageDirty.

        Args:
            asset_path: Full UMaterialInstanceConstant asset path.
            parameter_name: Parameter name as declared on the base material.
            value: Float value to write.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "parameter_name": parameter_name,
                "value": value,
            }
            response = unreal.send_command("set_material_instance_scalar_parameter", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"set_material_instance_scalar_parameter: {asset_path} / {parameter_name} = {value}")
            return response

        except Exception as e:
            error_msg = f"Error setting MI scalar parameter: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_material_instance_vector_parameter(
        ctx: Context,
        asset_path: str,
        parameter_name: str,
        value: List[float],
    ) -> Dict[str, Any]:
        """Override a Vector (FLinearColor) Parameter on a UMaterialInstanceConstant.

        Args:
            asset_path: Full UMaterialInstanceConstant asset path.
            parameter_name: Parameter name as declared on the base material.
            value: 3-element [R, G, B] or 4-element [R, G, B, A] list of floats.
                Missing alpha defaults to 1.0.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "parameter_name": parameter_name,
                "value": value,
            }
            response = unreal.send_command("set_material_instance_vector_parameter", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"set_material_instance_vector_parameter: {asset_path} / {parameter_name} = {value}")
            return response

        except Exception as e:
            error_msg = f"Error setting MI vector parameter: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_material_instance_texture_parameter(
        ctx: Context,
        asset_path: str,
        parameter_name: str,
        texture_path: str = "",
    ) -> Dict[str, Any]:
        """Override a Texture Parameter on a UMaterialInstanceConstant.

        Args:
            asset_path: Full UMaterialInstanceConstant asset path.
            parameter_name: Parameter name as declared on the base material.
            texture_path: Full UTexture asset path; pass an empty string to
                clear the override (sets to null).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "asset_path": asset_path,
                "parameter_name": parameter_name,
                "texture_path": texture_path,
            }
            response = unreal.send_command("set_material_instance_texture_parameter", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"set_material_instance_texture_parameter: {asset_path} / {parameter_name} = {texture_path}")
            return response

        except Exception as e:
            error_msg = f"Error setting MI texture parameter: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def set_material_property(
        ctx: Context,
        material_path: str,
        property_name: str,
        value: Union[str, int, float, bool, list],
    ) -> Dict[str, Any]:
        """Reflect-write a top-level UPROPERTY on a UMaterial.

        Sets a property on the base material itself (not on an expression node).
        Typical uses: change `BlendMode`, `TwoSided`, `MaterialDomain`,
        `bUsedWithSkeletalMesh`, etc. The material is recompiled after the change.

        For expression-node-level edits (e.g. Texture Sample's `Texture` ref) use
        `set_material_expression_property` instead.

        Args:
            material_path: Full UMaterial asset path.
            property_name: UPROPERTY name on UMaterial. Dotted path supported
                for nested fields.
            value: The new value. Type depends on the target property.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "material_path": material_path,
                "property_name": property_name,
                "value": value,
            }
            response = unreal.send_command("set_material_property", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"set_material_property: {material_path} / {property_name}")
            return response

        except Exception as e:
            error_msg = f"Error setting material property: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
