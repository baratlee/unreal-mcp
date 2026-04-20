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
        """Get basic info for an AnimSequence or AnimMontage, including root motion.

        Args:
            asset_path: Full asset path, e.g. "/Game/Animations/Montage_Attack.Montage_Attack"

        Returns:
            Dict with:
              asset_path, asset_class, play_length, num_frames, skeleton,
              has_root_motion       — effective flag. For AnimSequence equals
                                      bEnableRootMotion; for AnimMontage this is the
                                      OR aggregation across every AnimSequence
                                      referenced by SlotAnimTracks (any one with
                                      Root Motion enabled makes the whole montage
                                      true — see UAnimMontage::HasRootMotion).
            AnimSequence-only fields (omitted for AnimMontage):
              b_enable_root_motion, root_motion_root_lock (RefPose / AnimFirstFrame
              / Zero), b_force_root_lock, b_use_normalized_root_motion_scale.
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
    def get_montage_composite_info(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Get UAnimMontage composite structure: sections, slot anim tracks, segments, blends.

        Editor-only, structure read. Only accepts UAnimMontage paths — passing an
        AnimSequence will return an error.

        Args:
            asset_path: Full asset path, e.g. "/Game/Animations/Montage_Attack.Montage_Attack"

        Returns:
            Dict with:
              asset_path, asset_class, skeleton, play_length, is_loop,
              blend_in_time, blend_out_time, blend_out_trigger_time, enable_auto_blend_out,
              has_root_motion — OR aggregation across every underlying AnimSequence.
                                True if ANY segment has bEnableRootMotion set.
              section_count, sections[]:
                  section_index, section_name, start_time, segment_length,
                  next_section_name, link_value
              slot_count, slot_anim_tracks[]:
                  slot_index, slot_name, segment_count,
                  segments[]:
                      segment_index, anim_path, anim_class,
                      start_pos, end_pos, length,
                      anim_start_time, anim_end_time, play_rate, loop_count,
                      has_root_motion, b_enable_root_motion, root_motion_root_lock,
                      b_force_root_lock, b_use_normalized_root_motion_scale
                      (root motion fields only present when the segment is a
                       UAnimSequence — they are omitted otherwise)
              notify_track_count, notify_tracks[]: track_index, track_name
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_montage_composite_info", {"asset_path": asset_path})

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting montage composite info: {e}"
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

    @mcp.tool()
    def get_skeleton_retarget_modes(ctx: Context, skeleton_path: str) -> Dict[str, Any]:
        """Get the per-bone TranslationRetargetingMode for a USkeleton.

        For each bone in the raw reference skeleton this returns its index, name and
        retargeting mode string (one of: Animation, Skeleton, AnimationScaled,
        AnimationRelative, OrientAndScale).

        Args:
            skeleton_path: Full skeleton object path, e.g.
                "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin.SK_UEFN_Mannequin"

        Returns:
            Dict with skeleton_path, bone_count, bones list.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "get_skeleton_retarget_modes",
                {"skeleton_path": skeleton_path},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting skeleton retarget modes: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def list_ik_rigs(ctx: Context, path_filter: str = "") -> Dict[str, Any]:
        """List all UIKRigDefinition assets in the project via the Asset Registry.

        Scans the whole project by default; pass `path_filter` to narrow the search
        to a specific content path.

        Args:
            path_filter: Optional content root to restrict the search.

        Returns:
            Dict with path_filter, total_count, and an `assets` list where each entry
            has `path` and `class`.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "list_ik_rigs",
                {"path_filter": path_filter},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error listing IK rigs: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_ik_rig_info(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Read the static structure of a UIKRigDefinition asset (read-only).

        Returns the preview skeletal mesh path, pelvis bone, retarget bone chains
        (chain_name / start_bone / end_bone / goal_name), goals (goal_name /
        bone_name) and the solver stack. Each solver entry only carries its
        UScriptStruct name (e.g. `IKRigFBIKSolver`); per-solver parameters are NOT
        introspected in this tool.

        Args:
            asset_path: Full IKRig object path, e.g.
                "/Game/.../IK_Mannequin.IK_Mannequin"

        Returns:
            Dict with asset_path, asset_class, preview_skeletal_mesh, pelvis_bone,
            chain_count, chains, goal_count, goals, solver_count, solvers.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "get_ik_rig_info",
                {"asset_path": asset_path},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting IK rig info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def list_ik_retargeters(ctx: Context, path_filter: str = "") -> Dict[str, Any]:
        """List all UIKRetargeter assets in the project via the Asset Registry.

        Args:
            path_filter: Optional content root to restrict the search.

        Returns:
            Dict with path_filter, total_count, and an `assets` list where each entry
            has `path` and `class`.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "list_ik_retargeters",
                {"path_filter": path_filter},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error listing IK retargeters: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_ik_retargeter_info(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Read the static structure of a UIKRetargeter asset (read-only).

        Returns:
        - source/target IKRig asset paths and presence flags
        - current source/target retarget pose names
        - source_retarget_poses / target_retarget_poses lists, each entry has
          `name` and `bone_offset_count` (size of FIKRetargetPose.BoneRotationOffsets)
        - retarget_ops list — each entry includes:
          - `struct_name` (e.g. IKRetargetFKChainsOp, IKRetargetIKChainsOp)
          - `enabled` (bool)
          - `chain_pairs` (if the op has chain mapping): array of
            { source_chain, target_chain } pairs
          - `settings` (JSON object of the op's per-chain settings, e.g.
            FK chain rotation/translation modes, IK blend values, pole vector
            alignment, stretch chain scaling, etc.)
        - profiles list — names of the FRetargetProfile entries on the asset

        Args:
            asset_path: Full IKRetargeter object path, e.g.
                "/Game/.../RTG_Mannequin.RTG_Mannequin"

        Returns:
            Dict with the fields described above.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "get_ik_retargeter_info",
                {"asset_path": asset_path},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting IK retargeter info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ------------------------------------------------------------------
    # State Machine tools
    # ------------------------------------------------------------------

    @mcp.tool()
    async def get_anim_state_machine(ctx: Context, blueprint_path: str, state_machine_name: str = "") -> Dict[str, Any]:
        """Get the structure of an Animation State Machine inside an AnimBP.

        Returns all states (name, type, is_conduit) and all transitions
        (source, target, priority, crossfade_duration, blend_mode, logic_type,
        bidirectional, automatic_rule, disabled).

        Args:
            blueprint_path: Asset path of the Animation Blueprint
            state_machine_name: Name of the State Machine node (optional — omit to auto-pick the first one)
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {"blueprint_path": blueprint_path}
            if state_machine_name:
                params["state_machine_name"] = state_machine_name

            response = unreal.send_command("get_anim_state_machine", params)

            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting anim state machine: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    async def get_anim_state_graph(ctx: Context, blueprint_path: str, state_name: str, state_machine_name: str = "", pin_payload_mode: str = "full") -> Dict[str, Any]:
        """Get the internal animation node graph of a single state inside a State Machine.

        Returns the same node/pin format as get_blueprint_function_graph.

        Args:
            blueprint_path: Asset path of the Animation Blueprint
            state_name: Name of the state to read
            state_machine_name: Name of the State Machine node (optional)
            pin_payload_mode: "full" (default), "summary", or "names_only"
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "blueprint_path": blueprint_path,
                "state_name": state_name,
            }
            if state_machine_name:
                params["state_machine_name"] = state_machine_name
            if pin_payload_mode != "full":
                params["pin_payload_mode"] = pin_payload_mode

            response = unreal.send_command("get_anim_state_graph", params)

            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting anim state graph: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    async def get_anim_transition_graph(ctx: Context, blueprint_path: str, source_state: str, target_state: str, state_machine_name: str = "", pin_payload_mode: str = "full") -> Dict[str, Any]:
        """Get the condition graph and metadata of a transition between two states.

        Returns transition metadata (priority, crossfade_duration, blend_mode,
        logic_type, etc.) plus the condition graph nodes in the same format as
        get_blueprint_function_graph.

        Args:
            blueprint_path: Asset path of the Animation Blueprint
            source_state: Name of the source state
            target_state: Name of the target state
            state_machine_name: Name of the State Machine node (optional)
            pin_payload_mode: "full" (default), "summary", or "names_only"
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {
                "blueprint_path": blueprint_path,
                "source_state": source_state,
                "target_state": target_state,
            }
            if state_machine_name:
                params["state_machine_name"] = state_machine_name
            if pin_payload_mode != "full":
                params["pin_payload_mode"] = pin_payload_mode

            response = unreal.send_command("get_anim_transition_graph", params)

            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting anim transition graph: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_pose_search_database_info(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Read the structure of a PoseSearchDatabase asset (read-only).

        Returns schema reference, animation asset entries (with path, class,
        enabled state, mirror option, sampling range), tags, cost biases,
        and search mode.

        Args:
            asset_path: PoseSearchDatabase object path, e.g.
                "/Game/.../PSD_Locomotion.PSD_Locomotion"

        Returns:
            Dict with asset_path, schema, tag_count, tags,
            continuing_pose_cost_bias, base_cost_bias, looping_cost_bias,
            pose_search_mode, animation_asset_count, animation_assets[].
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("get_pose_search_database_info", {"asset_path": asset_path})
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            error_msg = f"Error getting pose search database info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_pose_search_schema_info(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Read the structure of a PoseSearchSchema asset (read-only).

        Returns sample rate, skeleton references, data preprocessor setting,
        and the full channel hierarchy (recursively serialized with sub-channels).

        Each channel entry includes: channel_class (e.g. "PoseSearchFeatureChannel_Trajectory"),
        weight, bone_name (if applicable), sample_time_offset, and type-specific fields.
        Trajectory channels include their samples array (offset, flags, weight).
        Pose channels include their sampled_bones array (bone_name, flags, weight).

        Args:
            asset_path: PoseSearchSchema object path, e.g.
                "/Game/.../PSS_Locomotion.PSS_Locomotion"

        Returns:
            Dict with asset_path, sample_rate, schema_cardinality,
            data_preprocessor, skeleton_count, skeletons[],
            channel_count, channels[] (recursive).
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("get_pose_search_schema_info", {"asset_path": asset_path})
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            error_msg = f"Error getting pose search schema info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    # ── PSD Write Tools ──

    @mcp.tool()
    def set_pose_search_database_schema(ctx: Context, asset_path: str, schema_path: str) -> Dict[str, Any]:
        """Set the Schema reference on a PoseSearchDatabase.

        Args:
            asset_path: PSD object path
            schema_path: PSS object path to assign
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("set_pose_search_database_schema", {
                "asset_path": asset_path, "schema_path": schema_path
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def add_pose_search_database_animation(
        ctx: Context, asset_path: str, anim_asset_path: str,
        enabled: bool = True,
        sampling_range_min: float = 0.0, sampling_range_max: float = 0.0
    ) -> Dict[str, Any]:
        """Add an animation asset entry to a PoseSearchDatabase.

        Args:
            asset_path: PSD object path
            anim_asset_path: AnimSequence/BlendSpace object path to add
            enabled: Whether this entry is enabled (default True)
            sampling_range_min: Start of sampling range in seconds (0 = from beginning)
            sampling_range_max: End of sampling range in seconds (0 = to end)
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            params = {"asset_path": asset_path, "anim_asset_path": anim_asset_path, "enabled": enabled}
            if sampling_range_min != 0.0 or sampling_range_max != 0.0:
                params["sampling_range_min"] = sampling_range_min
                params["sampling_range_max"] = sampling_range_max
            response = unreal.send_command("add_pose_search_database_animation", params)
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def remove_pose_search_database_animation(ctx: Context, asset_path: str, index: int) -> Dict[str, Any]:
        """Remove an animation asset entry from a PoseSearchDatabase by index.

        Args:
            asset_path: PSD object path
            index: Index of the animation entry to remove
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("remove_pose_search_database_animation", {
                "asset_path": asset_path, "index": index
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    # ── PSS Write Tools ──

    @mcp.tool()
    def set_pose_search_schema_settings(
        ctx: Context, asset_path: str,
        sample_rate: int = 0, skeleton_path: str = "", role: str = ""
    ) -> Dict[str, Any]:
        """Set settings on a PoseSearchSchema (sample rate, skeleton).

        Args:
            asset_path: PSS object path
            sample_rate: Sampling rate (1-240). Pass 0 to leave unchanged.
            skeleton_path: Skeleton object path. Empty to leave unchanged.
            role: Role name for the skeleton (e.g. "Default"). Empty for default.
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            params = {"asset_path": asset_path}
            if sample_rate > 0:
                params["sample_rate"] = sample_rate
            if skeleton_path:
                params["skeleton_path"] = skeleton_path
            if role:
                params["role"] = role
            response = unreal.send_command("set_pose_search_schema_settings", params)
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def add_pose_search_schema_channel(
        ctx: Context, asset_path: str, channel_type: str,
        weight: float = 1.0,
        bone_name: str = "", curve_name: str = "",
        sample_time_offset: float = 0.0,
        samples: list = None, sampled_bones: list = None
    ) -> Dict[str, Any]:
        """Add a feature channel to a PoseSearchSchema.

        Supported channel_type values:
        - "Trajectory": trajectory prediction channel. Pass `samples` as list of
          {"offset": float, "flags": int, "weight": float}.
          Flags bitmask: Position=2, Velocity=1, VelocityDirection=4,
          FacingDirection=8, PositionXY=32, VelocityXY=16.
        - "Pose": bone pose channel. Pass `sampled_bones` as list of
          {"bone_name": str, "flags": int, "weight": float}.
          Flags bitmask: Velocity=1, Position=2, Rotation=4, Phase=8.
        - "Position": single bone position. Pass `bone_name`.
        - "Velocity": single bone velocity. Pass `bone_name`.
        - "Heading": bone heading direction. Pass `bone_name`.
        - "Curve": animation curve value. Pass `curve_name`.

        Args:
            asset_path: PSS object path
            channel_type: One of Trajectory, Pose, Position, Velocity, Heading, Curve
            weight: Channel weight (default 1.0)
            bone_name: Bone name (for Position/Velocity/Heading)
            curve_name: Curve name (for Curve channel)
            sample_time_offset: Time offset in seconds (for Position/Velocity)
            samples: Trajectory samples list (for Trajectory channel)
            sampled_bones: Sampled bones list (for Pose channel)
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            params = {"asset_path": asset_path, "channel_type": channel_type, "weight": weight}
            if bone_name:
                params["bone_name"] = bone_name
            if curve_name:
                params["curve_name"] = curve_name
            if sample_time_offset != 0.0:
                params["sample_time_offset"] = sample_time_offset
            if samples is not None:
                params["samples"] = samples
            if sampled_bones is not None:
                params["sampled_bones"] = sampled_bones
            response = unreal.send_command("add_pose_search_schema_channel", params)
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def remove_pose_search_schema_channel(ctx: Context, asset_path: str, index: int) -> Dict[str, Any]:
        """Remove a feature channel from a PoseSearchSchema by index.

        Args:
            asset_path: PSS object path
            index: Index of the channel to remove
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("remove_pose_search_schema_channel", {
                "asset_path": asset_path, "index": index
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    # ── Chooser Table Write Tools ──

    @mcp.tool()
    def add_chooser_table_row(
        ctx: Context, asset_path: str, result_asset_path: str,
        column_values: list = None
    ) -> Dict[str, Any]:
        """Add a row to a ChooserTable with an asset result.

        Column values are matched by position to the table's columns.
        For Enum columns, pass the enum value name as a string.
        For Bool columns, pass "true", "false", or empty for MatchAny.
        Omitted or empty values default to MatchAny.

        Args:
            asset_path: ChooserTable object path
            result_asset_path: Asset to return when this row matches
            column_values: List of string values, one per column (positional).
                Example: ["Walk", "", "true"] for a 3-column table.
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            params = {"asset_path": asset_path, "result_asset_path": result_asset_path}
            if column_values is not None:
                params["column_values"] = column_values
            response = unreal.send_command("add_chooser_table_row", params)
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def remove_chooser_table_row(ctx: Context, asset_path: str, row_index: int) -> Dict[str, Any]:
        """Remove a row from a ChooserTable by index.

        Removes both the result entry and the corresponding values from all columns.

        Args:
            asset_path: ChooserTable object path
            row_index: Index of the row to remove
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("remove_chooser_table_row", {
                "asset_path": asset_path, "row_index": row_index
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_animation_properties(
        ctx: Context,
        asset_path: str,
        b_enable_root_motion: bool = None,
        root_motion_root_lock: str = None,
        b_force_root_lock: bool = None,
        b_use_normalized_root_motion_scale: bool = None,
    ) -> Dict[str, Any]:
        """Set root motion properties on a UAnimSequence asset (editor-only).

        Only works on AnimSequence assets (not AnimMontage). Each parameter is
        optional — only provided fields are modified, others are left unchanged.
        The asset is marked dirty after modification (Ctrl+S in editor to save).

        Args:
            asset_path: Full AnimSequence asset path
                (e.g., "/Game/Animations/Loco_Walk.Loco_Walk")
            b_enable_root_motion: Enable/disable root motion extraction.
                For Mover-driven characters, locomotion anims should be False.
            root_motion_root_lock: How to lock the root bone when root motion
                is disabled. One of "RefPose", "AnimFirstFrame", "Zero".
            b_force_root_lock: Force root bone locking regardless of other settings.
            b_use_normalized_root_motion_scale: Use normalized root motion scale.

        Returns:
            Dict with success, modified_fields list, and current values of all
            root motion properties after modification.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params = {"asset_path": asset_path}
            if b_enable_root_motion is not None:
                params["b_enable_root_motion"] = b_enable_root_motion
            if root_motion_root_lock is not None:
                params["root_motion_root_lock"] = root_motion_root_lock
            if b_force_root_lock is not None:
                params["b_force_root_lock"] = b_force_root_lock
            if b_use_normalized_root_motion_scale is not None:
                params["b_use_normalized_root_motion_scale"] = b_use_normalized_root_motion_scale

            response = unreal.send_command("set_animation_properties", params)

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            logger.info(f"Set animation properties for: {asset_path}")
            return response.get("result", response)

        except Exception as e:
            return {"success": False, "message": f"Error setting animation properties: {e}"}

    logger.info("Animation tools registered successfully")
