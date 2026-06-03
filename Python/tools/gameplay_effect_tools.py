"""
GameplayEffect Tools for Unreal MCP.

Read + write surface for UGameplayEffect blueprint assets (UE 5.3+ Component
model). Covers LT14 P0+P1+P2 (17 commands total). All writes mutate the GE
blueprint CDO and mark the blueprint modified — caller must follow up with
`compile_blueprint` + `save_dirty_assets` to persist (per Material/Spline/
Niagara convention).

Magnitude object schema (used by modifiers, duration, period, etc.):
  - ScalableFloat:  {"calculation": "ScalableFloat", "value": 12.5,
                     "curve_table": "/Game/.../CT_X", "row_name": "Foo"}
                    or flat form: {"calculation": "ScalableFloat", "value": 12.5}
  - SetByCaller:    {"calculation": "SetByCaller",
                     "set_by_caller": {"data_tag": "Data.Damage"}}
                    or by name: {"set_by_caller": {"data_name": "Damage"}}
  - AttributeBased: see add_gameplay_effect_modifier docstring (P2)
  - CustomCalculationClass: P3, not supported

P3 not implemented: Executions detail / Immunity / RemoveOther /
CustomCalculationClass magnitude / AdditionalEffects.
"""

import logging
from typing import Dict, Any, List, Optional
from mcp.server.fastmcp import FastMCP, Context

logger = logging.getLogger("UnrealMCP")


def _send(command: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Shared command dispatch — keeps each tool body terse."""
    from unreal_mcp_server import get_unreal_connection

    try:
        unreal = get_unreal_connection()
        if not unreal:
            logger.error("Failed to connect to Unreal Engine")
            return {"success": False, "message": "Failed to connect to Unreal Engine"}
        response = unreal.send_command(command, params)
        if not response:
            return {"success": False, "message": "No response from Unreal Engine"}
        logger.info(f"{command} response received")
        return response
    except Exception as e:
        msg = f"Error in {command}: {e}"
        logger.error(msg)
        return {"success": False, "message": msg}


def register_gameplay_effect_tools(mcp: FastMCP):
    """Register GameplayEffect read+write tools with the MCP server (LT14, 17 cmds)."""

    # ------------------------------------------------------------------
    # P0 — read + create
    # ------------------------------------------------------------------

    @mcp.tool()
    def get_gameplay_effect_info(
        ctx: Context,
        asset_path: str,
    ) -> Dict[str, Any]:
        """Inspect a UGameplayEffect blueprint asset's CDO.

        Returns (P0+P1+P2 fields):
          - asset_path, class_name, parent_class
          - duration_policy: Instant / HasDuration / Infinite
          - duration_magnitude, max_duration_magnitude (HasDuration only)
          - period, execute_periodic_effect_on_application (non-Instant)
          - modifiers[]: {attribute, modifier_op, magnitude,
                          source_tags, target_tags}
          - gameplay_cues[]: full FGameplayEffectCue detail (P2)
          - tag_requirements{application, ongoing, removal}: each
            {required, ignored} (P2)
          - chance_to_apply: ScalableFloat (only when component attached, P2)
          - granted_abilities[]: full FGameplayAbilitySpecConfig detail (P2)
          - executions_count, immunity_queries_count,
            remove_other_queries_count (counts only)
          - stacking{type, limit_count, duration_refresh_policy,
                     period_reset_policy, expiration_policy,
                     factor_in_stack_count}
          - inherited_tags.granted_tags / .asset_tags: {added, removed,
            combined} via UE 5.3+ Component model
          - inherited_tags.blocked_ability_tags_combined
          - components[]: attached UGameplayEffectComponent subclass names

        Args:
            asset_path: Full path, e.g. "/Game/GAS/Effects/BP_GE_Heal" or
                same with ".BP_GE_Heal" suffix.
        """
        return _send("get_gameplay_effect_info", {"asset_path": asset_path})

    @mcp.tool()
    def create_gameplay_effect(
        ctx: Context,
        asset_path: str,
        asset_name: str,
        parent_path: str = "",
    ) -> Dict[str, Any]:
        """Create a new Blueprint subclass of UGameplayEffect.

        NOT auto-compiled / NOT auto-saved — follow up with
        `compile_blueprint(asset_path)` + `save_dirty_assets()`.

        Args:
            asset_path: Folder, e.g. "/Game/Examples/Demo/GE".
            asset_name: Blueprint name without extension, e.g. "GE_Heal_Test".
            parent_path: Optional native or blueprint parent class path.
                Defaults to "/Script/GameplayAbilities.GameplayEffect".
        """
        params: Dict[str, Any] = {"asset_path": asset_path, "asset_name": asset_name}
        if parent_path:
            params["parent_path"] = parent_path
        return _send("create_gameplay_effect", params)

    # ------------------------------------------------------------------
    # P1 — top-level property, modifier CRUD, inherited tags
    # ------------------------------------------------------------------

    @mcp.tool()
    def set_gameplay_effect_property(
        ctx: Context,
        asset_path: str,
        property: str,
        value: Any,
    ) -> Dict[str, Any]:
        """Set one top-level CDO field on a GE blueprint.

        Supported `property` values + `value` types:
          - "duration_policy" → enum string: Instant / HasDuration / Infinite
          - "duration_magnitude" → magnitude object (see module docstring)
          - "max_duration_magnitude" → magnitude object
          - "period" → ScalableFloat object: {"value": 0.5} or
            {"value": 0.5, "curve_table": "/Game/.../CT_X", "row_name": "Foo"}
          - "execute_periodic_effect_on_application" → bool
          - "stacking_type" → enum string: None / AggregateBySource /
            AggregateByTarget
          - "stack_limit_count" → int (-1 or 0 = no limit)
          - "stack_duration_refresh_policy" → enum string
          - "stack_period_reset_policy" → enum string
          - "stack_expiration_policy" → enum string
          - "factor_in_stack_count" → bool

        Args:
            asset_path: Full GE blueprint path.
            property: One of the keys above.
            value: Type per the table above.
        """
        return _send(
            "set_gameplay_effect_property",
            {"asset_path": asset_path, "property": property, "value": value},
        )

    @mcp.tool()
    def add_gameplay_effect_modifier(
        ctx: Context,
        asset_path: str,
        attribute: str,
        modifier_op: str,
        magnitude: Dict[str, Any],
        source_tags: Optional[Dict[str, Any]] = None,
        target_tags: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Append a new modifier to the GE's Modifiers[] array.

        Args:
            asset_path: Full GE blueprint path.
            attribute: "ClassName.PropertyName", e.g. "KLAttributeSetBase.HP".
                Class must be loaded.
            modifier_op: Additive / Multiplicitive / Division / Override.
            magnitude: Magnitude object. Supported calculations:
              - ScalableFloat: {"calculation": "ScalableFloat", "value": 12.5,
                  "curve_table"?: "/Game/.../CT_X", "row_name"?: "Foo"}
              - SetByCaller: {"calculation": "SetByCaller",
                  "set_by_caller": {"data_tag"?: "Data.X", "data_name"?: "X"}}
              - AttributeBased (P2): {"calculation": "AttributeBased",
                  "attribute_based": {
                    "coefficient": {"value": 1.5},
                    "pre_multiply_additive_value": 0,
                    "post_multiply_additive_value": 0,
                    "backing_attribute": {"attribute": "Class.Prop",
                                          "source": "Source"|"Target",
                                          "snapshot": true},
                    "attribute_curve"?: {"curve_table": "...", "row_name": "..."},
                    "attribute_calculation_type"?: "AttributeMagnitude" | ...,
                    "final_channel"?: "Channel0" | ...,
                    "source_tag_filter"?: ["Tag.X"],
                    "target_tag_filter"?: []
                  }}
            source_tags: Optional {"required": [...], "ignored": [...]} filter
                applied against the source ASC.
            target_tags: Optional same shape, applied against target ASC.

        Returns: {index, modifiers_count}
        """
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "attribute": attribute,
            "modifier_op": modifier_op,
            "magnitude": magnitude,
        }
        if source_tags is not None:
            params["source_tags"] = source_tags
        if target_tags is not None:
            params["target_tags"] = target_tags
        return _send("add_gameplay_effect_modifier", params)

    @mcp.tool()
    def remove_gameplay_effect_modifier(
        ctx: Context,
        asset_path: str,
        index: int,
    ) -> Dict[str, Any]:
        """Remove the modifier at `index` from the GE's Modifiers[]."""
        return _send(
            "remove_gameplay_effect_modifier",
            {"asset_path": asset_path, "index": index},
        )

    @mcp.tool()
    def set_gameplay_effect_modifier(
        ctx: Context,
        asset_path: str,
        index: int,
        attribute: str = "",
        modifier_op: str = "",
        magnitude: Optional[Dict[str, Any]] = None,
        source_tags: Optional[Dict[str, Any]] = None,
        target_tags: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Partial-update a modifier at `index`.

        Any of attribute / modifier_op / magnitude / source_tags / target_tags
        can be omitted to preserve the existing value. See
        `add_gameplay_effect_modifier` for parameter shapes.
        """
        params: Dict[str, Any] = {"asset_path": asset_path, "index": index}
        if attribute:
            params["attribute"] = attribute
        if modifier_op:
            params["modifier_op"] = modifier_op
        if magnitude is not None:
            params["magnitude"] = magnitude
        if source_tags is not None:
            params["source_tags"] = source_tags
        if target_tags is not None:
            params["target_tags"] = target_tags
        return _send("set_gameplay_effect_modifier", params)

    @mcp.tool()
    def set_gameplay_effect_inherited_tags(
        ctx: Context,
        asset_path: str,
        granted: Optional[Dict[str, Any]] = None,
        asset: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Edit the Granted Tags / Asset Tags channels via Component model.

        Tags must already be registered with the GameplayTags subsystem;
        unregistered tags are silently dropped.

        Args:
            asset_path: Full GE blueprint path.
            granted: Granted Tags delta — {"added": [...], "removed": [...]}.
                Missing key = preserve that side. Replaces existing Added/Removed
                arrays for the side you provide (not append-merge).
            asset: Asset Tags delta — same shape.
        """
        params: Dict[str, Any] = {"asset_path": asset_path}
        if granted is not None:
            params["granted"] = granted
        if asset is not None:
            params["asset"] = asset
        return _send("set_gameplay_effect_inherited_tags", params)

    # ------------------------------------------------------------------
    # P2 — list / delete / cue / tag-reqs / chance / granted-abilities
    # ------------------------------------------------------------------

    @mcp.tool()
    def list_gameplay_effects(
        ctx: Context,
        path_filter: str = "",
    ) -> Dict[str, Any]:
        """List GameplayEffect blueprint assets in the project.

        Asset Registry scan — no asset loading. Substring-matches
        "GameplayEffect" against ParentClass/NativeParentClass tags, so
        catches blueprint GEs and grand-children.

        Args:
            path_filter: Optional package path prefix to scope the search
                (e.g. "/Game/Examples/Demo"). Empty = whole project.

        Returns: {effects: [{asset_path, name, package_path, parent_class}]}
        """
        return _send("list_gameplay_effects", {"path_filter": path_filter})

    @mcp.tool()
    def delete_gameplay_effect(
        ctx: Context,
        asset_path: str,
    ) -> Dict[str, Any]:
        """Hard-delete a GE blueprint asset (refuses on native classes).

        Uses ObjectTools::ForceDeleteObjects — no confirmation prompt. Only
        blueprint assets are accepted; native UGameplayEffect subclasses are
        rejected.
        """
        return _send("delete_gameplay_effect", {"asset_path": asset_path})

    @mcp.tool()
    def add_gameplay_effect_cue(
        ctx: Context,
        asset_path: str,
        cue_tags: List[str],
        min_level: float = 0.0,
        max_level: float = 0.0,
        magnitude_attribute: str = "",
    ) -> Dict[str, Any]:
        """Append a FGameplayEffectCue entry to the GE's GameplayCues[].

        Args:
            asset_path: Full GE blueprint path.
            cue_tags: One or more GameplayCue tag strings, e.g.
                ["GameplayCue.KL.Skill.HealingAura.Target"].
            min_level / max_level: Optional cue level range mapped from the
                GE's level. 0 / 0 = no clamp.
            magnitude_attribute: Optional "Class.Property" backing attribute
                for cue magnitude.

        Returns: {index, cues_count}
        """
        params: Dict[str, Any] = {"asset_path": asset_path, "cue_tags": cue_tags}
        if min_level:
            params["min_level"] = min_level
        if max_level:
            params["max_level"] = max_level
        if magnitude_attribute:
            params["magnitude_attribute"] = magnitude_attribute
        return _send("add_gameplay_effect_cue", params)

    @mcp.tool()
    def remove_gameplay_effect_cue(
        ctx: Context,
        asset_path: str,
        index: int,
    ) -> Dict[str, Any]:
        """Remove the cue at `index` from GameplayCues[]."""
        return _send(
            "remove_gameplay_effect_cue",
            {"asset_path": asset_path, "index": index},
        )

    @mcp.tool()
    def set_gameplay_effect_cue(
        ctx: Context,
        asset_path: str,
        index: int,
        cue_tags: Optional[List[str]] = None,
        min_level: Optional[float] = None,
        max_level: Optional[float] = None,
        magnitude_attribute: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Partial-update a cue entry at `index`.

        Any field set to None / omitted preserves the existing value.
        """
        params: Dict[str, Any] = {"asset_path": asset_path, "index": index}
        if cue_tags is not None:
            params["cue_tags"] = cue_tags
        if min_level is not None:
            params["min_level"] = min_level
        if max_level is not None:
            params["max_level"] = max_level
        if magnitude_attribute is not None:
            params["magnitude_attribute"] = magnitude_attribute
        return _send("set_gameplay_effect_cue", params)

    @mcp.tool()
    def set_gameplay_effect_tag_requirements(
        ctx: Context,
        asset_path: str,
        application: Optional[Dict[str, Any]] = None,
        ongoing: Optional[Dict[str, Any]] = None,
        removal: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Write to UTargetTagRequirementsGameplayEffectComponent (UE 5.3+).

        Args:
            asset_path: Full GE blueprint path.
            application: Tag requirements gating GE application.
                Shape: {"required": ["Tag.X"], "ignored": ["Tag.Y"]}.
                Missing key inside the dict = preserve that side.
            ongoing: Tag requirements gating ongoing effect (re-evaluated).
            removal: Tag requirements that, when satisfied, remove the GE.

        Any top-level argument set to None preserves that channel entirely.
        """
        params: Dict[str, Any] = {"asset_path": asset_path}
        if application is not None:
            params["application"] = application
        if ongoing is not None:
            params["ongoing"] = ongoing
        if removal is not None:
            params["removal"] = removal
        return _send("set_gameplay_effect_tag_requirements", params)

    @mcp.tool()
    def set_gameplay_effect_chance_to_apply(
        ctx: Context,
        asset_path: str,
        scalable_float: Optional[Dict[str, Any]] = None,
        value: Optional[float] = None,
    ) -> Dict[str, Any]:
        """Set the chance-to-apply ScalableFloat via UChanceToApplyGameplayEffectComponent.

        Provide ONE of:
          - `scalable_float`: full ScalableFloat object
            {"value": 0.5, "curve_table"?: "/Game/.../CT_X", "row_name"?: "Foo"}
          - `value`: bare float (= ScalableFloat with constant value, no curve)

        Args:
            asset_path: Full GE blueprint path.
            scalable_float: ScalableFloat dict (mutually exclusive with `value`).
            value: Bare float fallback if `scalable_float` is None.
        """
        params: Dict[str, Any] = {"asset_path": asset_path}
        if scalable_float is not None:
            params["scalable_float"] = scalable_float
        elif value is not None:
            params["value"] = value
        return _send("set_gameplay_effect_chance_to_apply", params)

    @mcp.tool()
    def add_gameplay_effect_granted_ability(
        ctx: Context,
        asset_path: str,
        ability_class: str,
        level: float = 1.0,
        input_id: int = -1,
        removal_policy: str = "",
    ) -> Dict[str, Any]:
        """Append a FGameplayAbilitySpecConfig to UAbilitiesGameplayEffectComponent.

        Args:
            asset_path: Full GE blueprint path.
            ability_class: GA class path. Blueprint asset path
                (e.g. "/Game/GAS/GA/BP_GA_Foo") or native class path
                ("/Script/PrjKunlun.GA_KLFoo").
            level: ScalableFloat level (defaults to 1.0).
            input_id: Optional input action ID; -1 = unbound.
            removal_policy: Optional enum string for ability removal policy
                (see EGameplayEffectGrantedAbilityRemovePolicy).

        Returns: {index, granted_abilities_count}
        """
        params: Dict[str, Any] = {
            "asset_path": asset_path,
            "ability_class": ability_class,
        }
        if level != 1.0:
            params["level"] = level
        if input_id != -1:
            params["input_id"] = input_id
        if removal_policy:
            params["removal_policy"] = removal_policy
        return _send("add_gameplay_effect_granted_ability", params)

    @mcp.tool()
    def remove_gameplay_effect_granted_ability(
        ctx: Context,
        asset_path: str,
        index: int,
    ) -> Dict[str, Any]:
        """Remove the GrantAbilityConfig at `index` (reflection-based)."""
        return _send(
            "remove_gameplay_effect_granted_ability",
            {"asset_path": asset_path, "index": index},
        )

    @mcp.tool()
    def set_gameplay_effect_granted_ability(
        ctx: Context,
        asset_path: str,
        index: int,
        ability_class: str = "",
        level: Optional[float] = None,
        input_id: Optional[int] = None,
        removal_policy: str = "",
    ) -> Dict[str, Any]:
        """Partial-update a GrantAbilityConfig at `index`.

        Any field omitted / set to default preserves the existing value.
        """
        params: Dict[str, Any] = {"asset_path": asset_path, "index": index}
        if ability_class:
            params["ability_class"] = ability_class
        if level is not None:
            params["level"] = level
        if input_id is not None:
            params["input_id"] = input_id
        if removal_policy:
            params["removal_policy"] = removal_policy
        return _send("set_gameplay_effect_granted_ability", params)
