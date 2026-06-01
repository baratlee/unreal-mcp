#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Read-only inspection for UMaterial / UMaterialInstance / UMaterialParameterCollection.
 * P0 batch (2026-05-22): three info commands, no graph traversal yet.
 */
class UNREALMCP_API FUnrealMCPMaterialCommands
{
public:
	FUnrealMCPMaterialCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleGetMaterialInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialInstanceInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialParameterCollectionInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialGraph(const TSharedPtr<FJsonObject>& Params);
};
