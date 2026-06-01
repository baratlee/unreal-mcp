#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Spline component editing for Blueprint templates.
 *
 * P0 (2026-06-01): get_spline_info / set_spline_points / set_spline_point /
 *   clear_spline_points. Bulk reads + bulk writes + single-point edit + clear.
 *
 * P1 (2026-06-01): add_spline_point / remove_spline_point /
 *   set_spline_closed_loop / set_spline_default_up_vector. Incremental edits
 *   + spline-level config that didn't fit in set_spline_points payload.
 *
 * All modifications happen on the component template inside a UBlueprint
 * (own SCS, inherited SCS, or native C++ CDO). The blueprint is marked
 * modified so a subsequent compile_blueprint + save_dirty_assets persists.
 *
 * Out of scope: per-actor instance edits in a live world (not planned —
 * modify the blueprint template, not runtime spawned actors).
 */
class UNREALMCP_API FUnrealMCPSplineCommands
{
public:
	FUnrealMCPSplineCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// P0
	TSharedPtr<FJsonObject> HandleGetSplineInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetSplinePoints(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetSplinePoint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleClearSplinePoints(const TSharedPtr<FJsonObject>& Params);

	// P1
	TSharedPtr<FJsonObject> HandleAddSplinePoint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveSplinePoint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetSplineClosedLoop(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetSplineDefaultUpVector(const TSharedPtr<FJsonObject>& Params);
};
