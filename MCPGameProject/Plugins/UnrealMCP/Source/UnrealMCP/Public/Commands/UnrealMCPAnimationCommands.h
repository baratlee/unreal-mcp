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
    TSharedPtr<FJsonObject> HandleGetMontageCompositeInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleFindAnimationsForSkeleton(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAnimBlueprintInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSkeletonBoneHierarchy(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListChooserTables(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetChooserTableInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSkeletonRetargetModes(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListIKRigs(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetIKRigInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleListIKRetargeters(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetIKRetargeterInfo(const TSharedPtr<FJsonObject>& Params);

    // Enhanced Input asset readers
    TSharedPtr<FJsonObject> HandleGetInputActionInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetInputMappingContextInfo(const TSharedPtr<FJsonObject>& Params);

    // Pose Search asset readers
    TSharedPtr<FJsonObject> HandleGetPoseSearchDatabaseInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetPoseSearchSchemaInfo(const TSharedPtr<FJsonObject>& Params);

    // Pose Search write tools
    TSharedPtr<FJsonObject> HandleSetPoseSearchDatabaseSchema(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddPoseSearchDatabaseAnimation(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemovePoseSearchDatabaseAnimation(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPoseSearchDatabaseCostBiases(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPoseSearchDatabaseAnimationFlags(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPoseSearchSchemaSettings(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddPoseSearchSchemaChannel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemovePoseSearchSchemaChannel(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPoseSearchSchemaChannelWeight(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetPoseSearchSchemaTrajectorySample(const TSharedPtr<FJsonObject>& Params);

    // Chooser Table write tools
    TSharedPtr<FJsonObject> HandleAddChooserTableRow(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveChooserTableRow(const TSharedPtr<FJsonObject>& Params);

    // Animation write tools
    TSharedPtr<FJsonObject> HandleSetAnimationProperties(const TSharedPtr<FJsonObject>& Params);
};
