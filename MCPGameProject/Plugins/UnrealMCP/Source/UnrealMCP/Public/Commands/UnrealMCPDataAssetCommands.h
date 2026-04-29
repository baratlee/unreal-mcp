#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UNREALMCP_API FUnrealMCPDataAssetCommands
{
public:
	FUnrealMCPDataAssetCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleGetDataAssetInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetDataAssetProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleListDataAssets(const TSharedPtr<FJsonObject>& Params);

	void SerializePropertiesToJson(UObject* Object, TArray<TSharedPtr<FJsonValue>>& OutArray, int32 Depth);
};
