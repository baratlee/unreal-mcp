"""
Niagara Tools for Unreal MCP.

Read-only inspection for UNiagaraSystem assets.

P0 batch (2026-06-02, LT12): a single info command that reports the
system's basic info (emitter list) plus the full list of exposed User
Parameters (name / type / default value). Scalar and common vector types
are parsed to concrete values; other types report the type name only.
"""

import logging
from typing import Dict, Any
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def register_niagara_tools(mcp: FastMCP):
    """Register Niagara inspection tools with the MCP server."""

    @mcp.tool()
    def get_niagara_system_info(
        ctx: Context,
        asset_path: str,
    ) -> Dict[str, Any]:
        """Inspect a UNiagaraSystem asset (a Niagara VFX system / effect).

        Returns:
          - basic info: `num_emitters` + `emitters` (each {name, enabled})
          - `exposed_parameters`: the system's User Parameters — the values
            exposed to the outside (Blueprint / spawner / sequencer). Each
            entry is {name, type, is_data_interface, is_uobject, value_kind,
            default_value?}:
              * value_kind="parsed" → `default_value` holds the concrete
                value. Scalars (float / int / bool) are plain values; vector
                types (Vector2/3/4, Position, Quat, LinearColor) are float
                arrays ([x,y], [x,y,z], [x,y,z,w], [r,g,b,a]).
              * value_kind="type_only" → the type is a DataInterface / UObject
                / custom struct / enum; only `type` is reported (no
                `default_value` field). Use `type` to identify it.
          - `exposed_parameter_count`

        Args:
            asset_path: Full asset path, e.g.
                "/Game/Examples/Effects/NS_Fire" or a plugin-mounted path.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_niagara_system_info", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"get_niagara_system_info response for: {asset_path}")
            return response

        except Exception as e:
            error_msg = f"Error getting niagara system info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def list_niagara_systems(
        ctx: Context,
        path_filter: str = "",
    ) -> Dict[str, Any]:
        """List all UNiagaraSystem assets in the project, optionally under a path.

        Asset Registry scan — fast, returns asset paths + names + package paths.
        Use this to discover sample effects before drilling into individual
        systems with `get_niagara_system_info` / `get_niagara_emitter_renderers`.

        Args:
            path_filter: Optional package path prefix to scope the search
                (e.g. "/Game/Examples/Forest_VFX/Niagara"). Empty = whole project.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("list_niagara_systems", {"path_filter": path_filter})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"list_niagara_systems response (filter='{path_filter}')")
            return response

        except Exception as e:
            error_msg = f"Error listing niagara systems: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_niagara_emitter_renderers(
        ctx: Context,
        asset_path: str,
        emitter_name: str,
    ) -> Dict[str, Any]:
        """Inspect a single emitter's renderers in a UNiagaraSystem.

        For the named emitter, walks its `RendererProperties[]` and returns,
        per renderer:
          - `class_name`: e.g. UNiagaraSpriteRendererProperties /
            UNiagaraMeshRendererProperties / UNiagaraRibbonRendererProperties /
            UNiagaraLightRendererProperties / UNiagaraDecalRendererProperties /
            ...
          - `enabled`: renderer-level enable flag
          - `materials`: list of asset paths (from base-class `GetUsedMaterials`,
            covers Sprite Material, Ribbon Material, Mesh override materials, etc.)
          - `meshes` (only for Mesh renderers): list of `{mesh_path}` from the
            renderer's `Meshes[]` array (Mesh renderer's primary content).
        Also reports `emitter_enabled` (the emitter handle's own enable flag).

        Args:
            asset_path: Full path to the UNiagaraSystem
                (e.g. "/Game/Examples/.../NS_Foo").
            emitter_name: Emitter handle name, as listed by
                `get_niagara_system_info`'s `emitters[].name`.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_niagara_emitter_renderers", {
                "asset_path": asset_path,
                "emitter_name": emitter_name,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            logger.info(f"get_niagara_emitter_renderers response for: {asset_path} :: {emitter_name}")
            return response

        except Exception as e:
            error_msg = f"Error getting niagara emitter renderers: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}
