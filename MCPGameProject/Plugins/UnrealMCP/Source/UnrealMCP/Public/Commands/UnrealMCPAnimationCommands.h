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
    TSharedPtr<FJsonObject> HandleListAnimationBlueprintsForSkeleton(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSkeletonBoneHierarchy(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSkeletonReferencePose(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetSkeletalMeshInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetPhysicsAssetInfo(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAssetReferences(const TSharedPtr<FJsonObject>& Params);
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

    // Chooser Table extension (Batch D): top-level config + column + row writes
    TSharedPtr<FJsonObject> HandleSetChooserTableResult(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetChooserTableFallbackResult(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddChooserTableParameter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveChooserTableParameter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddChooserTableColumn(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveChooserTableColumn(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetChooserTableColumnBinding(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetChooserTableRowResult(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetChooserTableRowColumnValue(const TSharedPtr<FJsonObject>& Params);

    // Animation write tools
    TSharedPtr<FJsonObject> HandleSetAnimationProperties(const TSharedPtr<FJsonObject>& Params);

    // Animation notify write tools
    TSharedPtr<FJsonObject> HandleAddAnimationNotify(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleRemoveAnimationNotify(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetAnimationNotify(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleGetAnimationNotifyDetails(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetAnimationNotifyProperty(const TSharedPtr<FJsonObject>& Params);

    // Helper: serialize all editable properties of a UObject to JSON
    void SerializeNotifyProperties(UObject* Object, TArray<TSharedPtr<FJsonValue>>& OutArray);

    // IKRig write tools (Batch C.1)
    TSharedPtr<FJsonObject> HandleCreateIKRig(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetIKRigRetargetRoot(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddIKRigRetargetChain(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddIKRigGoal(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddIKRigSolver(const TSharedPtr<FJsonObject>& Params);

    // IKRetargeter write tools (Batch C.2)
    TSharedPtr<FJsonObject> HandleCreateIKRetargeter(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetIKRetargeterOpEnabled(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetIKRetargeterOpField(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddIKRetargeterOp(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleAddIKRetargeterPinBoneEntry(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetIKRetargeterChainMapping(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleIKRetargeterAutoMapChains(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleSetIKRetargeterRetargetPose(const TSharedPtr<FJsonObject>& Params);
};
