#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for DataTable read commands.
 * Provides get_datatable_info — reads row data from a UDataTable asset.
 */
class UNREALMCP_API FUnrealMCPDataTableCommands
{
public:
	FUnrealMCPDataTableCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleGetDataTableInfo(const TSharedPtr<FJsonObject>& Params);
};
