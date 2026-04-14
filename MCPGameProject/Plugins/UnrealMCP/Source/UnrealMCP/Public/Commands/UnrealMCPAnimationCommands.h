#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler class for Animation-related MCP commands.
 * Reads data from UAnimSequence / UAnimMontage assets (Editor-only).
 */
class UNREALMCP_API FUnrealMCPAnimationCommands
{
public:
    FUnrealMCPAnimationCommands();

    TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
    TSharedPtr<FJsonObject> HandleGetAnimationInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAnimationNotifies(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAnimationCurveNames(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAnimationBoneTrackNames(const TSharedPtr<FJsonObject>& Params);
};
