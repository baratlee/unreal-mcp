#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UNREALMCP_API FUnrealMCPPythonCommands
{
public:
	FUnrealMCPPythonCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleExecutePythonScript(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleExecutePythonFile(const TSharedPtr<FJsonObject>& Params);
};
