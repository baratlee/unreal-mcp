"""
Animation Tools for Unreal MCP.

Editor-only tools for reading data from AnimSequence / AnimMontage assets.
"""

import logging
from typing import Dict, Any, List, Optional, Union
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
    def list_animation_blueprints_for_skeleton(
        ctx: Context,
        skeleton_path: str,
        path_filter: str = "",
    ) -> Dict[str, Any]:
        """Find UAnimBlueprint assets that target the given Skeleton.

        Mirror of `find_animations_for_skeleton` but for AnimBPs. Uses the
        AnimBlueprint's TargetSkeleton AssetRegistrySearchable tag, so assets
        do not have to be loaded.

        Args:
            skeleton_path: Skeleton object path (".LeafName" suffix may be omitted).
            path_filter: Optional content root to restrict the search.

        Returns:
            Dict with skeleton_path, path_filter, total_count, and an
            `anim_blueprints` list where each entry has {path, name, class, is_template}.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "list_animation_blueprints_for_skeleton",
                {"skeleton_path": skeleton_path, "path_filter": path_filter},
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            error_msg = f"Error listing anim blueprints for skeleton: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_skeleton_reference_pose(
        ctx: Context,
        skeleton_path: str,
        space: str = "Local",
    ) -> Dict[str, Any]:
        """Read a USkeleton's bind / reference pose bone transforms.

        Args:
            skeleton_path: Full skeleton object path.
            space: "Local" (default) returns each bone's transform relative to its parent;
                   "Component" returns the accumulated transform relative to the component root.

        Returns:
            Dict with skeleton_path, space, bone_count, and `bones` list.
            Each bone entry: {index, name, parent_index,
                              translation:[x,y,z],
                              rotation_quat:[x,y,z,w],
                              rotation_euler_pyr:[pitch,yaw,roll],
                              scale:[x,y,z]}.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "get_skeleton_reference_pose",
                {"skeleton_path": skeleton_path, "space": space},
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            error_msg = f"Error getting skeleton reference pose: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_skeletal_mesh_info(ctx: Context, mesh_path: str) -> Dict[str, Any]:
        """Read basic info of a USkeletalMesh asset.

        Args:
            mesh_path: Full SkeletalMesh object path.

        Returns:
            Dict with:
              name, path,
              skeleton_path, default_physics_asset_path,
              post_process_anim_blueprint_class: class path or null
                  (this lives on SkeletalMesh, not AnimBlueprint — see get_anim_blueprint_info docstring),
              bounding_box: {min:[x,y,z], max:[x,y,z]},
              bounds_origin:[x,y,z], bounds_extent:[x,y,z],
              approximate_height_cm: float (2 * extent.Z),
              lod_count, material_slots:[{index, slot_name, material_path}, ...],
              socket_names:[{name, bone_name}, ...]  (mesh-only sockets; skeleton sockets are separate).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("get_skeletal_mesh_info", {"mesh_path": mesh_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            error_msg = f"Error getting skeletal mesh info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_physics_asset_info(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """Read structure of a UPhysicsAsset (PhAT asset).

        Args:
            asset_path: PhysicsAsset object path.

        Returns:
            Dict with name, path, preview_skeletal_mesh,
              body_count, bodies:[{bone_name, physics_type (Default/Kinematic/Simulated),
                                   simulate_physics, mass_override, linear_damping, angular_damping,
                                   collision_response, num_sphere, num_box, num_capsule,
                                   num_convex, num_tapered_capsule}, ...],
              constraint_count, constraints:[{constraint_name, bone1, bone2,
                                              linear_limit_size,
                                              angular_swing1_limit_deg,
                                              angular_swing2_limit_deg,
                                              angular_twist_limit_deg}, ...].
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("get_physics_asset_info", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            error_msg = f"Error getting physics asset info: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_asset_references(ctx: Context, asset_path: str) -> Dict[str, Any]:
        """List assets that reference the given asset (reverse dependency query).

        Uses the Asset Registry's package-level Referencers. Both full object path
        (`/Game/Foo/Bar.Bar`) and short form (`/Game/Foo/Bar`) are accepted.

        Args:
            asset_path: Target asset path.

        Returns:
            Dict with asset_path, package_name, referencer_count,
              referencers:[{package_name, asset_path, asset_class}, ...].
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command("get_asset_references", {"asset_path": asset_path})
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            error_msg = f"Error getting asset references: {e}"
            logger.error(error_msg)
            return {"success": False, "message": error_msg}

    @mcp.tool()
    def get_anim_blueprint_info(ctx: Context, blueprint_path: str) -> Dict[str, Any]:
        """Get AnimBlueprint-specific asset properties that `get_blueprint_info` does not expose.

        Surfaces fields on UAnimBlueprint that are not part of the generated
        AnimInstance class (TargetSkeleton, Preview Mesh / Preview AnimBP,
        DefaultBindingClass, Sync Groups, parent/root AnimBP lineage, etc.).
        Use this as the "AnimBP asset header" view; pair with `get_blueprint_info`
        for the AnimInstance-class view (variables / event graphs / functions).

        Args:
            blueprint_path: Asset path of the AnimBlueprint, e.g.
                "/Game/Characters/ABP_Hero" or "/Game/.../ABP_Hero.ABP_Hero".
                The short name form is also accepted.

        Returns:
            Dict with:
              name, path, parent_class, blueprint_type, is_template,
              target_skeleton:               {name, path} or null (null when is_template)
              preview_skeletal_mesh:         {name, path} or null
              preview_animation_blueprint:   {name, path} or null
              preview_animation_blueprint_application_method:  "LinkedLayers" / "LinkedAnimGraph"
              preview_animation_blueprint_tag:                 string
              default_binding_class:         class name or null
              use_multi_threaded_animation_update: bool
              warn_about_blueprint_usage:    bool
              enable_linked_anim_layer_instance_sharing: bool
              anim_groups:                   [group_name, ...]
              num_parent_asset_overrides:    int
              num_pose_watches:              int
              parent_anim_blueprint:         {name, path} or null
              root_anim_blueprint:           {name, path} or null (null when this AnimBP is itself the root)
              state_machines:                [{node_name, sub_graph_name, state_count}, ...] — top-level SMs in AnimGraph
              used_animation_assets:         [{name, path, class}, ...] — animation assets directly referenced
                                             by AnimGraph nodes (AssetPlayer / BlendSpacePlayer / etc).
                                             Does NOT cover Chooser-selected, binding-driven, or
                                             PoseSearchDatabase-embedded animations.

            Note: `post_process_anim_blueprint` field actually lives on USkeletalMesh
            (not on UAnimBlueprint), so it's exposed by `get_skeletal_mesh_info` instead.

            Returns an error if the asset is not a UAnimBlueprint.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                logger.error("Failed to connect to Unreal Engine")
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command(
                "get_anim_blueprint_info",
                {"blueprint_path": blueprint_path},
            )

            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}

            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}

            return response.get("result", response)

        except Exception as e:
            error_msg = f"Error getting anim blueprint info: {e}"
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
        """Read the full structure of a UChooserTable asset (Editor-only).

        Now covers top-level config (ResultType / ResultClass / Parameters +
        FallbackResult) and per-column binding details + per-row cell values, in
        addition to the original column / row outline.

        Args:
            asset_path: Full ChooserTable object path, e.g.
                "/Game/.../CT_Locomotion.CT_Locomotion"

        Returns:
            Dict with:
              asset_path, asset_class, is_cooked_data,
              result_type: "ObjectResult" / "ClassResult" / "NoPrimaryResult",
              result_class: {name, path} or null,
              parameter_count, parameters: [
                {index, struct_name, kind: "class"|"struct",
                 class_or_struct: {name, path}, direction: "Input"/"Output"/"InputOutput"}, ...],
              column_count, columns: [
                {index, struct_name,
                 has_filters, has_outputs, sub_type: "Input"/"Output"/"Mixed",
                 row_values_property, b_disabled,
                 binding: {binding_struct, context_index, is_bound_to_root,
                           property_path:[...], path_as_text, display_name,
                           enum_type / allowed_class / struct_type (if applicable)
                          } or null
                }, ...],
              row_count, used_cooked_results,
              results: [
                {index, struct_name,
                 kind: "asset"|"soft_asset"|"evaluate_chooser"|"nested_chooser"|"empty"|"unknown",
                 asset_path / asset_class / soft_path / nested_chooser_path (by kind),
                 column_values: [{column_index, value (UE JSON per column RowValue type)}, ...]
                }, ...],
              fallback_result: (same shape as a row result entry) or null

            Per-cell `value` is serialized via UPropertyToJsonValue, so numeric /
            enum / struct cells come out as JSON-native types; only rare exotic
            column row-value types may produce `null`.
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

    @mcp.tool()
    def set_pose_search_database_cost_biases(
        ctx: Context, asset_path: str,
        continuing_pose_cost_bias: float = None,
        base_cost_bias: float = None,
        looping_cost_bias: float = None,
    ) -> Dict[str, Any]:
        """Set cost bias fields on a PoseSearchDatabase.

        Each parameter is optional — only provided fields are modified.
        These biases control MM continuing-pose preference and looping preference.

        Args:
            asset_path: PSD object path
            continuing_pose_cost_bias: Negative value favors continuing current pose
                (typical: -0.1 for strong continuity, -0.01 for weak)
            base_cost_bias: Flat cost offset applied to all entries
            looping_cost_bias: Negative value favors looping animations

        Returns:
            Dict with success, modified_fields[], and current values of all three biases.
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            params = {"asset_path": asset_path}
            if continuing_pose_cost_bias is not None:
                params["continuing_pose_cost_bias"] = continuing_pose_cost_bias
            if base_cost_bias is not None:
                params["base_cost_bias"] = base_cost_bias
            if looping_cost_bias is not None:
                params["looping_cost_bias"] = looping_cost_bias
            response = unreal.send_command("set_pose_search_database_cost_biases", params)
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_pose_search_database_animation_flags(
        ctx: Context, asset_path: str, index: int,
        enabled: bool = None,
        disable_reselection: bool = None,
        mirror_option: str = "",
        sampling_range_min: float = None,
        sampling_range_max: float = None,
    ) -> Dict[str, Any]:
        """Modify flags on an existing animation entry in a PoseSearchDatabase.

        Each parameter is optional — only provided fields are modified.

        Args:
            asset_path: PSD object path
            index: Index of the animation entry (0-based)
            enabled: bEnabled flag
            disable_reselection: bDisableReselection flag (suppresses re-picking
                the same animation at a different time, reduces micro-jitter)
            mirror_option: Mirror option string. One of:
                "UnmirroredOnly" / "MirroredOnly" / "UnmirroredAndMirrored"
            sampling_range_min: Sampling range start (seconds, 0 = from beginning)
            sampling_range_max: Sampling range end (seconds, 0 = to end)

        Returns:
            Dict with success, index, modified_fields[].
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            params = {"asset_path": asset_path, "index": index}
            if enabled is not None:
                params["enabled"] = enabled
            if disable_reselection is not None:
                params["disable_reselection"] = disable_reselection
            if mirror_option:
                params["mirror_option"] = mirror_option
            if sampling_range_min is not None:
                params["sampling_range_min"] = sampling_range_min
            if sampling_range_max is not None:
                params["sampling_range_max"] = sampling_range_max
            response = unreal.send_command("set_pose_search_database_animation_flags", params)
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

    @mcp.tool()
    def set_pose_search_schema_channel_weight(
        ctx: Context, asset_path: str, channel_index: int, weight: float
    ) -> Dict[str, Any]:
        """Set the top-level Weight field on a PoseSearchSchema channel.

        Works for any channel class that has a 'Weight' UPROPERTY
        (UPoseSearchFeatureChannel_Trajectory / _Position / _Velocity / _Heading etc).
        Modifying Trajectory channel weight is the primary use case to tune
        MM search sensitivity.

        Args:
            asset_path: PSS object path
            channel_index: Channel index in the Channels array (usually 0 for Trajectory)
            weight: New weight value (typical: 1.0 for balanced, higher = more aggressive)

        Returns:
            Dict with success, channel_index, channel_class, weight (current value).
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("set_pose_search_schema_channel_weight", {
                "asset_path": asset_path,
                "channel_index": channel_index,
                "weight": weight,
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_pose_search_schema_trajectory_sample(
        ctx: Context, asset_path: str, channel_index: int, sample_index: int,
        offset: float = None,
        weight: float = None,
        flags: int = None,
    ) -> Dict[str, Any]:
        """Modify a sample on a Trajectory channel's Samples array.

        Each parameter is optional — only provided fields are modified.
        Target channel must be UPoseSearchFeatureChannel_Trajectory (has Samples array).

        Args:
            asset_path: PSS object path
            channel_index: Channel index in Channels (usually 0 for Trajectory)
            sample_index: Sample index in the channel's Samples array
            offset: Sample time offset (seconds, negative = past, positive = future)
            weight: Sample weight (float, >0)
            flags: EPoseSearchTrajectoryFlags bitmask (Velocity=1, Position=2,
                VelocityDirection=4, FacingDirection=8, VelocityXY=16, PositionXY=32,
                VelocityDirectionXY=64, FacingDirectionXY=128).
                Combine with bitwise OR, e.g. 144 = Position + FacingDirectionXY.

        Returns:
            Dict with success, channel_index, sample_index, modified_fields[].
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            params = {
                "asset_path": asset_path,
                "channel_index": channel_index,
                "sample_index": sample_index,
            }
            if offset is not None:
                params["offset"] = offset
            if weight is not None:
                params["weight"] = weight
            if flags is not None:
                params["flags"] = flags
            response = unreal.send_command("set_pose_search_schema_trajectory_sample", params)
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

    # ---------------------------------------------------------------------
    # Batch D (P1/P2/P3): ChooserTable write extensions
    # ---------------------------------------------------------------------

    @mcp.tool()
    def set_chooser_table_result(
        ctx: Context,
        asset_path: str,
        result_type: str,
        result_class_path: str = "",
    ) -> Dict[str, Any]:
        """Set the Chooser's top-level Result Type and Result Class.

        Args:
            asset_path: ChooserTable object path.
            result_type: "ObjectResult" (UI: "Object Of Type"), "ClassResult"
                (UI: "SubClass Of"), or "NoPrimaryResult" (UI hidden; writes no
                primary result — use with Output columns only).
            result_class_path: Full UClass path (e.g. "/Script/Engine.AnimationAsset"
                or "/Game/.../QuadAnimInstance_C"). Pass empty string to clear.

        Note:
            Changing `result_type` invalidates existing row Results. Re-verify rows
            (`get_chooser_table_info`) and patch via `set_chooser_table_row_result`.
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("set_chooser_table_result", {
                "asset_path": asset_path,
                "result_type": result_type,
                "result_class_path": result_class_path,
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_chooser_table_fallback_result(
        ctx: Context,
        asset_path: str,
        result_asset_path: str = "",
    ) -> Dict[str, Any]:
        """Set (or clear) the Chooser's FallbackResult (used when no row matches).

        Args:
            asset_path: ChooserTable object path.
            result_asset_path: Asset to use as fallback; empty string clears the fallback.
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("set_chooser_table_fallback_result", {
                "asset_path": asset_path,
                "result_asset_path": result_asset_path,
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def add_chooser_table_parameter(
        ctx: Context,
        asset_path: str,
        kind: str,
        class_or_struct_path: str,
        direction: str = "Input",
    ) -> Dict[str, Any]:
        """Append a new Parameter (ContextData entry) to the Chooser.

        Args:
            asset_path: ChooserTable object path.
            kind: "class" → FContextObjectTypeClass (for UObject-derived contexts,
                  e.g. AnimInstance subclass).
                  "struct" → FContextObjectTypeStruct (for USTRUCT contexts,
                  used for Output columns writing to a struct).
            class_or_struct_path: For kind="class" a UClass path
                (e.g. "/Game/.../QuadAnimInstance_C"); for kind="struct" a
                UScriptStruct path (e.g. "/Script/LeoPractice.QuadChooserOutputs").
            direction: "Input" (UI default) / "Output" / "InputOutput"
                (source enum: Read / Write / ReadWrite — auto-mapped).
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("add_chooser_table_parameter", {
                "asset_path": asset_path,
                "kind": kind,
                "class_or_struct_path": class_or_struct_path,
                "direction": direction,
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def remove_chooser_table_parameter(
        ctx: Context,
        asset_path: str,
        parameter_index: int,
    ) -> Dict[str, Any]:
        """Remove a Parameter from the Chooser by index.

        Column bindings referencing this Parameter may now be invalid — inspect
        with `get_chooser_table_info` and fix via `set_chooser_table_column_binding`.
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("remove_chooser_table_parameter", {
                "asset_path": asset_path,
                "parameter_index": parameter_index,
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def add_chooser_table_column(
        ctx: Context,
        asset_path: str,
        column_type: str,
        binding_path_as_text: str = "",
        context_index: int = 0,
        enum_path: str = "",
    ) -> Dict[str, Any]:
        """Append a new column to the Chooser (Input or Output).

        Args:
            asset_path: ChooserTable object path.
            column_type: Column struct name (bare or /Script/ form). Common values:
                Input:  "EnumColumn", "MultiEnumColumn", "BoolColumn",
                        "FloatRangeColumn", "GameplayTagColumn", "ObjectColumn",
                        "ObjectClassColumn", "FloatDistanceColumn".
                Output: "OutputBoolColumn", "OutputFloatColumn", "OutputEnumColumn",
                        "OutputObjectColumn", "OutputStructColumn",
                        "OutputGameplayTagQueryColumn".
            binding_path_as_text: Dotted property path to bind the column's
                InputValue to (e.g. "MovementState" if bound to the first Parameter,
                or "QuadChooserOutputs.bUseMM" if bound to a struct parameter).
                Empty string leaves the binding unset.
            context_index: Which Parameter (0-based) the binding targets.
            enum_path: UEnum path (required for EnumColumn / MultiEnumColumn
                to populate FChooserEnumPropertyBinding::Enum). Ignored for
                other column types.

        Note:
            RowValues array is auto-sized to current row count. Per-cell values
            are left at default; use `set_chooser_table_row_column_value` to set.
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            params = {
                "asset_path": asset_path,
                "column_type": column_type,
                "binding_path_as_text": binding_path_as_text,
                "context_index": context_index,
            }
            if enum_path:
                params["enum_path"] = enum_path
            response = unreal.send_command("add_chooser_table_column", params)
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def remove_chooser_table_column(
        ctx: Context,
        asset_path: str,
        column_index: int,
    ) -> Dict[str, Any]:
        """Remove a column from the Chooser by index (also removes its RowValues)."""
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("remove_chooser_table_column", {
                "asset_path": asset_path,
                "column_index": column_index,
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_chooser_table_column_binding(
        ctx: Context,
        asset_path: str,
        column_index: int,
        binding_path_as_text: str,
        context_index: int = 0,
        enum_path: str = "",
    ) -> Dict[str, Any]:
        """Update an existing column's binding (property path + context index).

        Useful for fixing broken bindings after Parameters have been reordered /
        removed, or for pointing an existing column at a different field.
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            params = {
                "asset_path": asset_path,
                "column_index": column_index,
                "binding_path_as_text": binding_path_as_text,
                "context_index": context_index,
            }
            if enum_path:
                params["enum_path"] = enum_path
            response = unreal.send_command("set_chooser_table_column_binding", params)
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_chooser_table_row_result(
        ctx: Context,
        asset_path: str,
        row_index: int,
        result_asset_path: str,
    ) -> Dict[str, Any]:
        """Replace the Result asset on a specific row.

        UE5.7 Chooser stores one asset per row (FAssetChooser.Asset is not a
        TArray). To represent "multiple candidate animations", use multiple rows
        with different column values; each row outputs exactly one asset.
        """
        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("set_chooser_table_row_result", {
                "asset_path": asset_path,
                "row_index": row_index,
                "result_asset_path": result_asset_path,
            })
            if isinstance(response, dict) and "error" in response:
                return {"success": False, "message": response["error"]}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": str(e)}

    @mcp.tool()
    def set_chooser_table_row_column_value(
        ctx: Context,
        asset_path: str,
        row_index: int,
        column_index: int,
        value: Union[str, bool, int, float],
    ) -> Dict[str, Any]:
        """Set a single cell value on a specific row / column.

        Value goes through UE's `ImportText_Direct`, so the same T3D text format
        rules apply as `set_ik_retargeter_op_field`:
          - bool:    True / False / "true" / "false"
          - int / float: 10 / 1.5 / "10" / "1.5"
          - FName / FString / enum value: literal text
          - struct cell (e.g. FChooserEnumRowData):
              "(Comparison=MatchEqual,Value=2)"
          - FloatRange row data: "(Min=0.0,Max=1.0)"
          - array cell: "(elem1,elem2)"

        Python bool/int/float inputs are stringified before sending (so literal
        `False` == string "false") — same pydantic workaround baked in as
        `set_ik_retargeter_op_field`.
        """
        if isinstance(value, bool):
            value_str = "true" if value else "false"
        elif isinstance(value, (int, float)):
            value_str = str(value)
        else:
            value_str = value

        try:
            from unreal_mcp_server import get_unreal_connection
            unreal = get_unreal_connection()
            response = unreal.send_command("set_chooser_table_row_column_value", {
                "asset_path": asset_path,
                "row_index": row_index,
                "column_index": column_index,
                "value": value_str,
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

    # ---------------------------------------------------------------------
    # Batch C.1: IKRig write tools (Items 8 + 9 from UnrealMCP_扩展需求清单.md)
    # ---------------------------------------------------------------------

    @mcp.tool()
    def create_ik_rig(
        ctx: Context,
        asset_path: str,
        preview_skeletal_mesh_path: str = "",
    ) -> Dict[str, Any]:
        """Create a new UIKRigDefinition asset.

        The resulting asset is dirty in-memory but NOT saved to disk — the caller
        should save via the editor or a future `save_asset` command.

        Args:
            asset_path: Target asset path, e.g. "/Game/.../IK_Tgt_StoneGolem".
            preview_skeletal_mesh_path: Optional SkeletalMesh to assign as preview
                (this also loads the skeleton hierarchy into the rig).

        Returns:
            Dict with success, asset_path (full object path of created asset),
            skeletal_mesh_assigned (bool), skeletal_mesh_warning (present only on
            failure), saved=False.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"asset_path": asset_path}
            if preview_skeletal_mesh_path:
                params["preview_skeletal_mesh_path"] = preview_skeletal_mesh_path
            response = unreal.send_command("create_ik_rig", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error creating IKRig: {e}"}

    @mcp.tool()
    def set_ik_rig_retarget_root(
        ctx: Context,
        asset_path: str,
        bone_name: str,
    ) -> Dict[str, Any]:
        """Set the retarget root (pelvis) bone on an IKRig asset.

        Args:
            asset_path: IKRig asset path.
            bone_name: Name of the root bone (typically "pelvis").

        Returns:
            Dict with success, asset_path, retarget_root_bone.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "set_ik_rig_retarget_root",
                {"asset_path": asset_path, "bone_name": bone_name},
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error setting retarget root: {e}"}

    @mcp.tool()
    def add_ik_rig_retarget_chain(
        ctx: Context,
        asset_path: str,
        chain_name: str,
        start_bone: str,
        end_bone: str,
        goal_name: str = "",
    ) -> Dict[str, Any]:
        """Add a retarget chain to an IKRig asset.

        Args:
            asset_path: IKRig asset path.
            chain_name: Unique chain name (e.g. "LeftArm"). Will be uniquified on collision.
            start_bone: First bone in the chain.
            end_bone: Last bone in the chain.
            goal_name: Optional IK goal to associate with this chain (e.g. "LeftHandIK");
                       empty or None leaves the chain without a goal.

        Returns:
            Dict with success, asset_path, assigned_chain_name (after uniquification).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {
                "asset_path": asset_path,
                "chain_name": chain_name,
                "start_bone": start_bone,
                "end_bone": end_bone,
            }
            if goal_name:
                params["goal_name"] = goal_name
            response = unreal.send_command("add_ik_rig_retarget_chain", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error adding retarget chain: {e}"}

    @mcp.tool()
    def add_ik_rig_goal(
        ctx: Context,
        asset_path: str,
        goal_name: str,
        bone_name: str,
    ) -> Dict[str, Any]:
        """Add a new IK goal to an IKRig asset.

        Args:
            asset_path: IKRig asset path.
            goal_name: Unique goal name (e.g. "LeftHandIK").
            bone_name: The bone this goal is attached to (e.g. "hand_l").

        Returns:
            Dict with success, asset_path, assigned_goal_name, bone_name.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "add_ik_rig_goal",
                {"asset_path": asset_path, "goal_name": goal_name, "bone_name": bone_name},
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error adding goal: {e}"}

    @mcp.tool()
    def add_ik_rig_solver(
        ctx: Context,
        asset_path: str,
        solver_struct_name: str,
    ) -> Dict[str, Any]:
        """Add a solver to the bottom of an IKRig's solver stack.

        Args:
            asset_path: IKRig asset path.
            solver_struct_name: Either the full UStruct package path
                (e.g. "/Script/IKRig.IKRigFBIKSolver") or just the struct name
                (e.g. "IKRigFBIKSolver"); the C++ side resolves both.

        Returns:
            Dict with success, asset_path, solver_index (position in the stack).

        Note: solver-specific parameters (Root Bone, iteration count, preferred angles
        etc.) are NOT configured by this tool — inspect them with the extended
        `get_ik_rig_info` (fields blob) and configure via a future op-field setter.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "add_ik_rig_solver",
                {"asset_path": asset_path, "solver_struct_name": solver_struct_name},
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error adding solver: {e}"}

    # ---------------------------------------------------------------------
    # Batch C.2: IKRetargeter write tools (Items 10 + 11 + 12)
    # ---------------------------------------------------------------------

    @mcp.tool()
    def create_ik_retargeter(
        ctx: Context,
        asset_path: str,
        source_ik_rig_path: str = "",
        target_ik_rig_path: str = "",
    ) -> Dict[str, Any]:
        """Create a new UIKRetargeter asset.

        Creates the asset and (optionally) assigns source/target IKRigs via the
        controller. The asset is dirty in-memory but NOT saved to disk.

        Args:
            asset_path: Target asset path, e.g. "/Game/.../RTG_UEFN_to_StoneGolem".
            source_ik_rig_path: Optional source IKRig (anim copied FROM).
            target_ik_rig_path: Optional target IKRig (anim copied TO).

        Returns:
            Dict with success, asset_path, source_assigned, target_assigned,
            source_warning?, target_warning?, saved=False.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"asset_path": asset_path}
            if source_ik_rig_path:
                params["source_ik_rig_path"] = source_ik_rig_path
            if target_ik_rig_path:
                params["target_ik_rig_path"] = target_ik_rig_path
            response = unreal.send_command("create_ik_retargeter", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error creating IKRetargeter: {e}"}

    @mcp.tool()
    def set_ik_retargeter_op_enabled(
        ctx: Context,
        asset_path: str,
        op_index: int,
        enabled: bool,
    ) -> Dict[str, Any]:
        """Toggle a retarget op on/off."""
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "set_ik_retargeter_op_enabled",
                {"asset_path": asset_path, "op_index": op_index, "enabled": enabled},
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error toggling op: {e}"}

    @mcp.tool()
    def set_ik_retargeter_op_field(
        ctx: Context,
        asset_path: str,
        op_index: int,
        field_path: str,
        value: Union[str, bool, int, float],
    ) -> Dict[str, Any]:
        """Set any property on a retarget op via reflection (generic field setter).

        Uses UE's ImportText_Direct server-side, so the value is ultimately
        interpreted as text. To keep the MCP boundary forgiving, this wrapper
        accepts bool / int / float / str and stringifies before sending — so
        passing literal `False` (Python bool) is equivalent to passing `"false"`
        (string).

        Accepted value forms (by property type):
          - bool:    True / False / "true" / "false"
          - int / float: 10 / 1.5 / "10" / "1.5"
          - FName / FString: literal text ("None" for empty FName)
          - enum: enum value name (e.g. "CopyGlobalPosition")
          - FVector: "(X=1,Y=2,Z=3)"
          - FRotator: "(Pitch=0,Yaw=90,Roll=0)"
          - FQuat: "(X=0,Y=0,Z=0,W=1)"
          - struct: "(Field1=...,Field2=...)"
          - array: "(elem1,elem2)"
        Structured types (FVector / struct / array) must still be passed as
        T3D-format strings — Python lists / dicts are NOT auto-converted.

        Args:
            asset_path: IKRetargeter asset path.
            op_index: Index of the op in the stack.
            field_path: Dotted property path on the op struct, e.g. "Settings.bCopyTranslation"
                        or "Settings.TranslationMode".
            value: Value to assign. See accepted forms above.

        Returns:
            Dict with success, asset_path, op_index, field_path, op_struct (struct name).
        """
        from unreal_mcp_server import get_unreal_connection

        # Normalize to a UE ImportText-compatible string:
        #   bool True/False → "true"/"false" (UE accepts lowercase)
        #   int/float       → str()
        #   str             → verbatim
        # This shields the MCP boundary from JSON-RPC type coercion: Claude
        # models often encode the literal "false" as a JSON boolean, which
        # pydantic then rejects on a `value: str` field.
        if isinstance(value, bool):
            value_str = "true" if value else "false"
        elif isinstance(value, (int, float)):
            value_str = str(value)
        else:
            value_str = value

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "set_ik_retargeter_op_field",
                {
                    "asset_path": asset_path,
                    "op_index": op_index,
                    "field_path": field_path,
                    "value": value_str,
                },
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error setting op field: {e}"}

    @mcp.tool()
    def add_ik_retargeter_op(
        ctx: Context,
        asset_path: str,
        op_struct_name: str,
        insert_after_index: int = -1,
    ) -> Dict[str, Any]:
        """Add a retarget op to an IKRetargeter's op stack.

        Args:
            asset_path: IKRetargeter asset path.
            op_struct_name: Either full path ("/Script/IKRig.IKRetargetPinBoneOp")
                or bare struct name ("IKRetargetPinBoneOp").
            insert_after_index: If >= 0, the new op is moved to (insert_after_index + 1)
                after being added at the bottom. UE may further constrain the final index
                due to execution-order rules; the actual final index is in the result.

        Returns:
            Dict with success, asset_path, op_index (final position), reordered (bool).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"asset_path": asset_path, "op_struct_name": op_struct_name}
            if insert_after_index >= 0:
                params["insert_after_index"] = insert_after_index
            response = unreal.send_command("add_ik_retargeter_op", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error adding op: {e}"}

    @mcp.tool()
    def add_ik_retargeter_pin_bones_entry(
        ctx: Context,
        asset_path: str,
        op_index: int,
        bone_to_copy_from: str,
        bone_to_copy_to: str,
    ) -> Dict[str, Any]:
        """Append an FPinBoneData entry to a Pin Bones op's BonesToPin list.

        The op at `op_index` must be a Pin Bones op (FIKRetargetPinBoneOp).
        Per-op settings (translation/rotation modes, copy flags) live on the op
        itself, NOT per-entry — set them via `set_ik_retargeter_op_field` with
        paths like "Settings.bCopyTranslation" or "Settings.TranslationMode".

        Args:
            asset_path: IKRetargeter asset path.
            op_index: Index of the Pin Bones op in the stack.
            bone_to_copy_from: Source bone name.
            bone_to_copy_to: Target bone name (the one whose transform gets pinned).

        Returns:
            Dict with success, asset_path, op_index, entry_index, bone_to_copy_from, bone_to_copy_to.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "add_ik_retargeter_pin_bones_entry",
                {
                    "asset_path": asset_path,
                    "op_index": op_index,
                    "bone_to_copy_from": bone_to_copy_from,
                    "bone_to_copy_to": bone_to_copy_to,
                },
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error adding pin bones entry: {e}"}

    @mcp.tool()
    def set_ik_retargeter_chain_mapping(
        ctx: Context,
        asset_path: str,
        op_index: int,
        target_chain_name: str,
        source_chain_name: str = "",
    ) -> Dict[str, Any]:
        """Map a source chain to a target chain in a specific op.

        Args:
            asset_path: IKRetargeter asset path.
            op_index: Index of the op (must be one with chain mapping, e.g. FK Chains / IK Chains).
            target_chain_name: The target IKRig chain to map (REQUIRED).
            source_chain_name: The source IKRig chain to copy FROM. Empty string
                clears the mapping (passes NAME_None to UE).

        Returns:
            Dict with success, asset_path, op_index, op_name, source_chain_name, target_chain_name.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "set_ik_retargeter_chain_mapping",
                {
                    "asset_path": asset_path,
                    "op_index": op_index,
                    "source_chain_name": source_chain_name,
                    "target_chain_name": target_chain_name,
                },
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error setting chain mapping: {e}"}

    @mcp.tool()
    def ik_retargeter_auto_map_chains(
        ctx: Context,
        asset_path: str,
        op_index: int,
        mode: str,
    ) -> Dict[str, Any]:
        """Auto-map chains for a specific op.

        Args:
            asset_path: IKRetargeter asset path.
            op_index: Index of the op containing chain mappings.
            mode: One of:
                  - "MapAllExact"       — exact name match, override existing
                  - "MapOnlyEmptyExact" — exact name match, only fill empty
                  - "MapAllFuzzy"       — closest-name (Levenshtein), override
                  - "MapOnlyEmptyFuzzy" — closest-name, only fill empty
                  - "ClearAll"          — clear all mappings to None

        Returns:
            Dict with success, asset_path, op_index, op_name, mode.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            response = unreal.send_command(
                "ik_retargeter_auto_map_chains",
                {"asset_path": asset_path, "op_index": op_index, "mode": mode},
            )
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error auto-mapping chains: {e}"}

    @mcp.tool()
    def set_ik_retargeter_retarget_pose(
        ctx: Context,
        asset_path: str,
        side: str,
        bone_name: str,
        rotation_quat: Optional[List[float]] = None,
        rotation_euler: Optional[List[float]] = None,
    ) -> Dict[str, Any]:
        """Set a bone rotation offset in the CURRENT retarget pose.

        Args:
            asset_path: IKRetargeter asset path.
            side: "Source" or "Target".
            bone_name: Bone to apply offset to.
            rotation_quat: [x, y, z, w] quaternion. Preferred — no parsing ambiguity.
            rotation_euler: [pitch, yaw, roll] in degrees. Used only if rotation_quat is omitted.

        Returns:
            Dict with success, asset_path, side, bone_name, applied_quat, note.

        Note: Always operates on the CURRENT retarget pose. To target a different
        pose, switch via SetCurrentRetargetPose first (not yet exposed as a separate
        MCP command).
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}
            params = {"asset_path": asset_path, "side": side, "bone_name": bone_name}
            if rotation_quat is not None:
                params["rotation_quat"] = rotation_quat
            elif rotation_euler is not None:
                params["rotation_euler"] = rotation_euler
            else:
                return {"success": False, "message": "Provide rotation_quat=[x,y,z,w] or rotation_euler=[pitch,yaw,roll]"}
            response = unreal.send_command("set_ik_retargeter_retarget_pose", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error setting retarget pose: {e}"}

    # -----------------------------------------------------------------------
    # Animation Notify write tools
    # -----------------------------------------------------------------------

    @mcp.tool()
    def add_animation_notify(
        ctx: Context,
        asset_path: str,
        notify_class: str,
        trigger_time: float,
        duration: float = 0.0,
        track_index: int = 0,
        notify_name: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Add an AnimNotify or AnimNotifyState to an AnimSequence / AnimMontage.

        Args:
            asset_path: Full asset path, e.g. "/Game/Animations/Montage_Attack.Montage_Attack"
            notify_class: Class path of the notify, e.g. "/Script/Engine.AnimNotify_PlaySound"
            trigger_time: Trigger time in seconds
            duration: Duration in seconds (only meaningful for AnimNotifyState)
            track_index: Notify track row index (default 0)
            notify_name: Optional display name; auto-derived from class if omitted

        Returns:
            Dict with success, notify_index, notify_name, trigger_time, duration, etc.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params: Dict[str, Any] = {
                "asset_path": asset_path,
                "notify_class": notify_class,
                "trigger_time": trigger_time,
                "duration": duration,
                "track_index": track_index,
            }
            if notify_name is not None:
                params["notify_name"] = notify_name

            response = unreal.send_command("add_animation_notify", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error adding animation notify: {e}"}

    @mcp.tool()
    def remove_animation_notify(
        ctx: Context,
        asset_path: str,
        notify_index: int,
    ) -> Dict[str, Any]:
        """Remove an animation notify by index.

        Args:
            asset_path: Full asset path of the AnimSequence or AnimMontage
            notify_index: Zero-based index into the notifies array (from get_animation_notifies)

        Returns:
            Dict with success, removed_notify name, remaining_count.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("remove_animation_notify", {
                "asset_path": asset_path,
                "notify_index": notify_index,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error removing animation notify: {e}"}

    @mcp.tool()
    def set_animation_notify(
        ctx: Context,
        asset_path: str,
        notify_index: int,
        trigger_time: Optional[float] = None,
        duration: Optional[float] = None,
        track_index: Optional[int] = None,
        notify_name: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Modify the timing, track, or name of an existing animation notify.

        Args:
            asset_path: Full asset path of the AnimSequence or AnimMontage
            notify_index: Zero-based index of the notify to modify
            trigger_time: New trigger time in seconds (optional)
            duration: New duration in seconds, for state notifies (optional)
            track_index: New track row index (optional)
            notify_name: New display name (optional)

        Returns:
            Dict with success, modified_fields list, and updated values.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            params: Dict[str, Any] = {
                "asset_path": asset_path,
                "notify_index": notify_index,
            }
            if trigger_time is not None:
                params["trigger_time"] = trigger_time
            if duration is not None:
                params["duration"] = duration
            if track_index is not None:
                params["track_index"] = track_index
            if notify_name is not None:
                params["notify_name"] = notify_name

            response = unreal.send_command("set_animation_notify", params)
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error setting animation notify: {e}"}

    @mcp.tool()
    def get_animation_notify_details(
        ctx: Context,
        asset_path: str,
        notify_index: int,
    ) -> Dict[str, Any]:
        """Get the Detail-panel properties of a specific animation notify.

        Returns timing info plus all editable properties of the UAnimNotify or
        UAnimNotifyState object (the same fields shown in the editor Details panel).

        Args:
            asset_path: Full asset path of the AnimSequence or AnimMontage
            notify_index: Zero-based index of the notify

        Returns:
            Dict with basic notify info and a 'properties' array.  Each property
            entry has: name, type, value, category.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("get_animation_notify_details", {
                "asset_path": asset_path,
                "notify_index": notify_index,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error getting notify details: {e}"}

    @mcp.tool()
    def set_animation_notify_property(
        ctx: Context,
        asset_path: str,
        notify_index: int,
        property_name: str,
        property_value: Union[str, int, float, bool],
    ) -> Dict[str, Any]:
        """Set a Detail-panel property on an animation notify object.

        Supports basic types (bool, int, float, string, enum) directly and
        complex types (struct, object ref) via UE ImportText format.

        Args:
            asset_path: Full asset path of the AnimSequence or AnimMontage
            notify_index: Zero-based index of the notify
            property_name: Name of the property to set (e.g. "VolumeMultiplier")
            property_value: New value.  For object references pass the asset path
                string; for structs use UE text format (e.g. "(X=0,Y=0,Z=100)")

        Returns:
            Dict with success and the property that was set.
        """
        from unreal_mcp_server import get_unreal_connection

        try:
            unreal = get_unreal_connection()
            if not unreal:
                return {"success": False, "message": "Failed to connect to Unreal Engine"}

            response = unreal.send_command("set_animation_notify_property", {
                "asset_path": asset_path,
                "notify_index": notify_index,
                "property_name": property_name,
                "property_value": property_value,
            })
            if not response:
                return {"success": False, "message": "No response from Unreal Engine"}
            if response.get("status") == "error":
                return {"success": False, "message": response.get("error", "Unknown error")}
            return response.get("result", response)
        except Exception as e:
            return {"success": False, "message": f"Error setting notify property: {e}"}

    logger.info("Animation tools registered successfully")
