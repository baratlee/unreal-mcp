#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Read-only inspection for UNiagaraSystem assets.
 * P0 batch (2026-06-02, LT12): single info command that reports the system's
 * basic info (emitter list) plus the full list of exposed User Parameters
 * (name / type / default value). Scalar and common vector types are parsed
 * to concrete values; other types report the type name only.
 */
class UNREALMCP_API FUnrealMCPNiagaraCommands
{
public:
	FUnrealMCPNiagaraCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleGetNiagaraSystemInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListNiagaraSystems(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetNiagaraEmitterRenderers(const TSharedPtr<FJsonObject>& Params);
};
