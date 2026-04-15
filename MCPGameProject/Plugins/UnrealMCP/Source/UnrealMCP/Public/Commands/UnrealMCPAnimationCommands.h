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
    TSharedPtr<FJsonObject> HandleFindAnimationsForSkeleton(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSkeletonBoneHierarchy(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListChooserTables(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetChooserTableInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSkeletonRetargetModes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListIKRigs(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetIKRigInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListIKRetargeters(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetIKRetargeterInfo(const TSharedPtr<FJsonObject>& Params);
};
