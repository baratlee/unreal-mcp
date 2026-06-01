#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Spline component editing for Blueprint templates.
 *
 * P0 batch (2026-06-01): four commands targeting USplineComponent inside a
 * UBlueprint's SCS (own or inherited) or native C++ CDO. All modifications
 * happen on the component template; the blueprint is marked modified so a
 * subsequent compile_blueprint + save_dirty_assets persists the change.
 *
 * Out of scope (deferred):
 *   - add_spline_point_at_index / remove_spline_point (P1)
 *   - closed loop / default up vector (P1)
 *   - per-actor instance edits in a live world (not planned — modify the
 *     blueprint template, not runtime spawned actors)
 */
class UNREALMCP_API FUnrealMCPSplineCommands
{
public:
	FUnrealMCPSplineCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleGetSplineInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetSplinePoints(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetSplinePoint(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleClearSplinePoints(const TSharedPtr<FJsonObject>& Params);
};
