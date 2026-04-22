#include "Commands/UnrealMCPAnimationCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "AnimationBlueprintLibrary.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AssetRegistry/AssetData.h"
#include "AnimationGraph.h"
#include "AnimationStateMachineGraph.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "UObject/Package.h"
#include "StructUtils/InstancedStruct.h"
#include "Chooser.h"
#include "IObjectChooser.h"
#include "IChooserColumn.h"
#include "IChooserParameterBase.h"
#include "ChooserPropertyAccess.h"
#include "IHasContext.h"
#include "ObjectChooser_Asset.h"
#include "Rig/IKRigDefinition.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProfile.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "Retargeter/RetargetOps/PinBoneOp.h"
#include "RigEditor/IKRigController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "UObject/UnrealType.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedActionKeyMapping.h"
#include "InputTriggers.h"
#include "InputModifiers.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/PoseSearchFeatureChannel_Pose.h"
#include "PoseSearch/PoseSearchFeatureChannel_Position.h"
#include "PoseSearch/PoseSearchFeatureChannel_Velocity.h"
#include "PoseSearch/PoseSearchFeatureChannel_Heading.h"
#include "PoseSearch/PoseSearchFeatureChannel_Curve.h"
#include "PoseSearch/PoseSearchFeatureChannel_Group.h"
#include "EnumColumn.h"
#include "BoolColumn.h"
#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "JsonObjectConverter.h"

namespace
{
    FString RootMotionRootLockToString(ERootMotionRootLock::Type Lock)
    {
        switch (Lock)
        {
        case ERootMotionRootLock::RefPose:        return TEXT("RefPose");
        case ERootMotionRootLock::AnimFirstFrame: return TEXT("AnimFirstFrame");
        case ERootMotionRootLock::Zero:           return TEXT("Zero");
        default:                                   return TEXT("Unknown");
        }
    }

    void AddRootMotionFieldsForSequence(const TSharedPtr<FJsonObject>& Out, const UAnimSequence* AnimSeq)
    {
        Out->SetBoolField(TEXT("b_enable_root_motion"), AnimSeq->bEnableRootMotion);
        Out->SetStringField(TEXT("root_motion_root_lock"), RootMotionRootLockToString(AnimSeq->RootMotionRootLock.GetValue()));
        Out->SetBoolField(TEXT("b_force_root_lock"), AnimSeq->bForceRootLock);
        Out->SetBoolField(TEXT("b_use_normalized_root_motion_scale"), AnimSeq->bUseNormalizedRootMotionScale);
    }
}

FUnrealMCPAnimationCommands::FUnrealMCPAnimationCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("get_animation_info"))
    {
        return HandleGetAnimationInfo(Params);
    }
    if (CommandType == TEXT("get_animation_notifies"))
    {
        return HandleGetAnimationNotifies(Params);
    }
    if (CommandType == TEXT("get_animation_curve_names"))
    {
        return HandleGetAnimationCurveNames(Params);
    }
    if (CommandType == TEXT("get_animation_bone_track_names"))
    {
        return HandleGetAnimationBoneTrackNames(Params);
    }
    if (CommandType == TEXT("get_montage_composite_info"))
    {
        return HandleGetMontageCompositeInfo(Params);
    }
    if (CommandType == TEXT("find_animations_for_skeleton"))
    {
        return HandleFindAnimationsForSkeleton(Params);
    }
    if (CommandType == TEXT("get_anim_blueprint_info"))
    {
        return HandleGetAnimBlueprintInfo(Params);
    }
    if (CommandType == TEXT("list_animation_blueprints_for_skeleton"))
    {
        return HandleListAnimationBlueprintsForSkeleton(Params);
    }
    if (CommandType == TEXT("get_skeleton_reference_pose"))
    {
        return HandleGetSkeletonReferencePose(Params);
    }
    if (CommandType == TEXT("get_skeletal_mesh_info"))
    {
        return HandleGetSkeletalMeshInfo(Params);
    }
    if (CommandType == TEXT("get_physics_asset_info"))
    {
        return HandleGetPhysicsAssetInfo(Params);
    }
    if (CommandType == TEXT("get_asset_references"))
    {
        return HandleGetAssetReferences(Params);
    }
    if (CommandType == TEXT("get_skeleton_bone_hierarchy"))
    {
        return HandleGetSkeletonBoneHierarchy(Params);
    }
    if (CommandType == TEXT("list_chooser_tables"))
    {
        return HandleListChooserTables(Params);
    }
    if (CommandType == TEXT("get_chooser_table_info"))
    {
        return HandleGetChooserTableInfo(Params);
    }
    if (CommandType == TEXT("get_skeleton_retarget_modes"))
    {
        return HandleGetSkeletonRetargetModes(Params);
    }
    if (CommandType == TEXT("list_ik_rigs"))
    {
        return HandleListIKRigs(Params);
    }
    if (CommandType == TEXT("get_ik_rig_info"))
    {
        return HandleGetIKRigInfo(Params);
    }
    if (CommandType == TEXT("list_ik_retargeters"))
    {
        return HandleListIKRetargeters(Params);
    }
    if (CommandType == TEXT("get_ik_retargeter_info"))
    {
        return HandleGetIKRetargeterInfo(Params);
    }
    if (CommandType == TEXT("get_input_action_info"))
    {
        return HandleGetInputActionInfo(Params);
    }
    if (CommandType == TEXT("get_input_mapping_context_info"))
    {
        return HandleGetInputMappingContextInfo(Params);
    }
    if (CommandType == TEXT("get_pose_search_database_info"))
    {
        return HandleGetPoseSearchDatabaseInfo(Params);
    }
    if (CommandType == TEXT("get_pose_search_schema_info"))
    {
        return HandleGetPoseSearchSchemaInfo(Params);
    }
    if (CommandType == TEXT("set_pose_search_database_schema"))
    {
        return HandleSetPoseSearchDatabaseSchema(Params);
    }
    if (CommandType == TEXT("add_pose_search_database_animation"))
    {
        return HandleAddPoseSearchDatabaseAnimation(Params);
    }
    if (CommandType == TEXT("remove_pose_search_database_animation"))
    {
        return HandleRemovePoseSearchDatabaseAnimation(Params);
    }
    if (CommandType == TEXT("set_pose_search_database_cost_biases"))
    {
        return HandleSetPoseSearchDatabaseCostBiases(Params);
    }
    if (CommandType == TEXT("set_pose_search_database_animation_flags"))
    {
        return HandleSetPoseSearchDatabaseAnimationFlags(Params);
    }
    if (CommandType == TEXT("set_pose_search_schema_settings"))
    {
        return HandleSetPoseSearchSchemaSettings(Params);
    }
    if (CommandType == TEXT("add_pose_search_schema_channel"))
    {
        return HandleAddPoseSearchSchemaChannel(Params);
    }
    if (CommandType == TEXT("remove_pose_search_schema_channel"))
    {
        return HandleRemovePoseSearchSchemaChannel(Params);
    }
    if (CommandType == TEXT("set_pose_search_schema_channel_weight"))
    {
        return HandleSetPoseSearchSchemaChannelWeight(Params);
    }
    if (CommandType == TEXT("set_pose_search_schema_trajectory_sample"))
    {
        return HandleSetPoseSearchSchemaTrajectorySample(Params);
    }
    if (CommandType == TEXT("add_chooser_table_row"))
    {
        return HandleAddChooserTableRow(Params);
    }
    if (CommandType == TEXT("remove_chooser_table_row"))
    {
        return HandleRemoveChooserTableRow(Params);
    }
    if (CommandType == TEXT("set_animation_properties"))
    {
        return HandleSetAnimationProperties(Params);
    }
    if (CommandType == TEXT("create_ik_rig"))
    {
        return HandleCreateIKRig(Params);
    }
    if (CommandType == TEXT("set_ik_rig_retarget_root"))
    {
        return HandleSetIKRigRetargetRoot(Params);
    }
    if (CommandType == TEXT("add_ik_rig_retarget_chain"))
    {
        return HandleAddIKRigRetargetChain(Params);
    }
    if (CommandType == TEXT("add_ik_rig_goal"))
    {
        return HandleAddIKRigGoal(Params);
    }
    if (CommandType == TEXT("add_ik_rig_solver"))
    {
        return HandleAddIKRigSolver(Params);
    }
    if (CommandType == TEXT("create_ik_retargeter"))
    {
        return HandleCreateIKRetargeter(Params);
    }
    if (CommandType == TEXT("set_ik_retargeter_op_enabled"))
    {
        return HandleSetIKRetargeterOpEnabled(Params);
    }
    if (CommandType == TEXT("set_ik_retargeter_op_field"))
    {
        return HandleSetIKRetargeterOpField(Params);
    }
    if (CommandType == TEXT("add_ik_retargeter_op"))
    {
        return HandleAddIKRetargeterOp(Params);
    }
    if (CommandType == TEXT("add_ik_retargeter_pin_bones_entry"))
    {
        return HandleAddIKRetargeterPinBoneEntry(Params);
    }
    if (CommandType == TEXT("set_ik_retargeter_chain_mapping"))
    {
        return HandleSetIKRetargeterChainMapping(Params);
    }
    if (CommandType == TEXT("ik_retargeter_auto_map_chains"))
    {
        return HandleIKRetargeterAutoMapChains(Params);
    }
    if (CommandType == TEXT("set_ik_retargeter_retarget_pose"))
    {
        return HandleSetIKRetargeterRetargetPose(Params);
    }
    if (CommandType == TEXT("set_chooser_table_result"))
    {
        return HandleSetChooserTableResult(Params);
    }
    if (CommandType == TEXT("set_chooser_table_fallback_result"))
    {
        return HandleSetChooserTableFallbackResult(Params);
    }
    if (CommandType == TEXT("add_chooser_table_parameter"))
    {
        return HandleAddChooserTableParameter(Params);
    }
    if (CommandType == TEXT("remove_chooser_table_parameter"))
    {
        return HandleRemoveChooserTableParameter(Params);
    }
    if (CommandType == TEXT("add_chooser_table_column"))
    {
        return HandleAddChooserTableColumn(Params);
    }
    if (CommandType == TEXT("remove_chooser_table_column"))
    {
        return HandleRemoveChooserTableColumn(Params);
    }
    if (CommandType == TEXT("set_chooser_table_column_binding"))
    {
        return HandleSetChooserTableColumnBinding(Params);
    }
    if (CommandType == TEXT("set_chooser_table_row_result"))
    {
        return HandleSetChooserTableRowResult(Params);
    }
    if (CommandType == TEXT("set_chooser_table_row_column_value"))
    {
        return HandleSetChooserTableRowColumnValue(Params);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown animation command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetAnimationInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UAnimSequenceBase* AnimBase = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
    if (!AnimBase)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Animation asset not found: %s"), *AssetPath));
    }

    int32 NumFrames = 0;
    UAnimationBlueprintLibrary::GetNumFrames(AnimBase, NumFrames);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("asset_class"), AnimBase->GetClass()->GetName());
    Result->SetNumberField(TEXT("play_length"), AnimBase->GetPlayLength());
    Result->SetNumberField(TEXT("num_frames"), NumFrames);

    if (const USkeleton* Skeleton = AnimBase->GetSkeleton())
    {
        Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
    }
    else
    {
        Result->SetStringField(TEXT("skeleton"), FString());
    }

    // Root motion. has_root_motion is the effective virtual call: for AnimSequence
    // it returns bEnableRootMotion; for AnimMontage it OR-aggregates all underlying
    // AnimSequences referenced by SlotAnimTracks.
    Result->SetBoolField(TEXT("has_root_motion"), AnimBase->HasRootMotion());
    if (const UAnimSequence* AnimSeq = Cast<UAnimSequence>(AnimBase))
    {
        AddRootMotionFieldsForSequence(Result, AnimSeq);
    }

    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetAnimationNotifies(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UAnimSequenceBase* AnimBase = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
    if (!AnimBase)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Animation asset not found: %s"), *AssetPath));
    }

    TArray<FAnimNotifyEvent> Notifies;
    UAnimationBlueprintLibrary::GetAnimationNotifyEvents(AnimBase, Notifies);

    TArray<TSharedPtr<FJsonValue>> NotifiesJson;
    NotifiesJson.Reserve(Notifies.Num());
    for (const FAnimNotifyEvent& Notify : Notifies)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("notify_name"), Notify.NotifyName.ToString());
        Entry->SetNumberField(TEXT("trigger_time"), Notify.GetTriggerTime());
        Entry->SetNumberField(TEXT("duration"), Notify.GetDuration());
        Entry->SetNumberField(TEXT("track_index"), Notify.TrackIndex);
        Entry->SetBoolField(TEXT("is_state"), Notify.NotifyStateClass != nullptr);
        Entry->SetBoolField(TEXT("is_branching_point"), Notify.IsBranchingPoint());

        FString NotifyClass;
        if (Notify.NotifyStateClass)
        {
            NotifyClass = Notify.NotifyStateClass->GetClass()->GetPathName();
        }
        else if (Notify.Notify)
        {
            NotifyClass = Notify.Notify->GetClass()->GetPathName();
        }
        Entry->SetStringField(TEXT("notify_class"), NotifyClass);

        NotifiesJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("asset_class"), AnimBase->GetClass()->GetName());
    Result->SetNumberField(TEXT("notify_count"), Notifies.Num());
    Result->SetArrayField(TEXT("notifies"), NotifiesJson);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetAnimationCurveNames(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UAnimSequenceBase* AnimBase = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
    if (!AnimBase)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Animation asset not found: %s"), *AssetPath));
    }

    TArray<FName> CurveNames;
    UAnimationBlueprintLibrary::GetAnimationCurveNames(AnimBase, ERawCurveTrackTypes::RCT_Float, CurveNames);

    TArray<TSharedPtr<FJsonValue>> CurveJson;
    CurveJson.Reserve(CurveNames.Num());
    for (const FName& Name : CurveNames)
    {
        CurveJson.Add(MakeShared<FJsonValueString>(Name.ToString()));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("asset_class"), AnimBase->GetClass()->GetName());
    Result->SetStringField(TEXT("curve_type"), TEXT("Float"));
    Result->SetNumberField(TEXT("curve_count"), CurveNames.Num());
    Result->SetArrayField(TEXT("curves"), CurveJson);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetAnimationBoneTrackNames(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UAnimSequenceBase* AnimBase = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
    if (!AnimBase)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Animation asset not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("asset_class"), AnimBase->GetClass()->GetName());

    UAnimMontage* Montage = Cast<UAnimMontage>(AnimBase);
    if (Montage)
    {
        Result->SetBoolField(TEXT("is_montage"), true);

        TSet<FName> UnionTrackSet;
        TArray<TSharedPtr<FJsonValue>> SegmentsJson;

        for (int32 SlotIdx = 0; SlotIdx < Montage->SlotAnimTracks.Num(); ++SlotIdx)
        {
            const FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[SlotIdx];
            for (int32 SegIdx = 0; SegIdx < SlotTrack.AnimTrack.AnimSegments.Num(); ++SegIdx)
            {
                const FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments[SegIdx];
                UAnimSequenceBase* SegAnim = Segment.GetAnimReference();
                if (!SegAnim)
                {
                    continue;
                }

                TArray<FName> SegTrackNames;
                UAnimationBlueprintLibrary::GetAnimationTrackNames(SegAnim, SegTrackNames);

                TArray<TSharedPtr<FJsonValue>> SegTrackJson;
                SegTrackJson.Reserve(SegTrackNames.Num());
                for (const FName& Name : SegTrackNames)
                {
                    UnionTrackSet.Add(Name);
                    SegTrackJson.Add(MakeShared<FJsonValueString>(Name.ToString()));
                }

                TSharedPtr<FJsonObject> SegEntry = MakeShared<FJsonObject>();
                SegEntry->SetStringField(TEXT("slot"), SlotTrack.SlotName.ToString());
                SegEntry->SetNumberField(TEXT("segment_index"), SegIdx);
                SegEntry->SetStringField(TEXT("anim_path"), SegAnim->GetPathName());
                SegEntry->SetStringField(TEXT("anim_class"), SegAnim->GetClass()->GetName());
                SegEntry->SetNumberField(TEXT("track_count"), SegTrackNames.Num());
                SegEntry->SetArrayField(TEXT("tracks"), SegTrackJson);
                SegmentsJson.Add(MakeShared<FJsonValueObject>(SegEntry));
            }
        }

        TArray<TSharedPtr<FJsonValue>> UnionJson;
        UnionJson.Reserve(UnionTrackSet.Num());
        for (const FName& Name : UnionTrackSet)
        {
            UnionJson.Add(MakeShared<FJsonValueString>(Name.ToString()));
        }

        Result->SetNumberField(TEXT("bone_track_count"), UnionTrackSet.Num());
        Result->SetArrayField(TEXT("bone_tracks"), UnionJson);
        Result->SetArrayField(TEXT("segments"), SegmentsJson);
    }
    else
    {
        Result->SetBoolField(TEXT("is_montage"), false);

        TArray<FName> TrackNames;
        UAnimationBlueprintLibrary::GetAnimationTrackNames(AnimBase, TrackNames);

        TArray<TSharedPtr<FJsonValue>> TrackJson;
        TrackJson.Reserve(TrackNames.Num());
        for (const FName& Name : TrackNames)
        {
            TrackJson.Add(MakeShared<FJsonValueString>(Name.ToString()));
        }

        Result->SetNumberField(TEXT("bone_track_count"), TrackNames.Num());
        Result->SetArrayField(TEXT("bone_tracks"), TrackJson);
    }

    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleFindAnimationsForSkeleton(const TSharedPtr<FJsonObject>& Params)
{
    FString SkeletonPath;
    if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'skeleton_path' parameter"));
    }

    bool bIncludeMontages = true;
    Params->TryGetBoolField(TEXT("include_montages"), bIncludeMontages);

    FString PathFilter;
    Params->TryGetStringField(TEXT("path_filter"), PathFilter);

    // Accept both "/Game/.../SK_Foo" and "/Game/.../SK_Foo.SK_Foo" forms;
    // normalize to the full object path since LoadObject and the AR Skeleton
    // tag value both carry the ".LeafName" suffix.
    FString SkeletonObjectPath = SkeletonPath;
    {
        int32 DotIdx = INDEX_NONE;
        int32 SlashIdx = INDEX_NONE;
        SkeletonObjectPath.FindLastChar('.', DotIdx);
        SkeletonObjectPath.FindLastChar('/', SlashIdx);
        if (DotIdx <= SlashIdx && SlashIdx != INDEX_NONE)
        {
            const FString LeafName = SkeletonObjectPath.Mid(SlashIdx + 1);
            SkeletonObjectPath = SkeletonObjectPath + TEXT(".") + LeafName;
        }
    }

    // Verify the skeleton actually exists; gives a friendlier error than returning an empty list.
    USkeleton* SkeletonObj = LoadObject<USkeleton>(nullptr, *SkeletonObjectPath);
    if (!SkeletonObj)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Skeleton asset not found: %s"), *SkeletonPath));
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
    if (bIncludeMontages)
    {
        Filter.ClassPaths.Add(UAnimMontage::StaticClass()->GetClassPathName());
    }
    if (PathFilter.Len() > 0)
    {
        Filter.PackagePaths.Add(FName(*PathFilter));
    }

    TArray<FAssetData> Candidates;
    AssetRegistry.GetAssets(Filter, Candidates);

    TArray<TSharedPtr<FJsonValue>> MatchedJson;
    int32 SequenceCount = 0;
    int32 MontageCount = 0;

    for (const FAssetData& Asset : Candidates)
    {
        // The Skeleton asset registry tag is stored in ExportText form, e.g.
        //   Skeleton'/Game/Path/SK_Foo.SK_Foo'
        // Checking Contains on the resolved object path keeps the match robust across
        // UE versions without needing to parse the ExportText wrapper.
        FString TagValue;
        if (!Asset.GetTagValue<FString>(TEXT("Skeleton"), TagValue))
        {
            continue;
        }
        if (!TagValue.Contains(SkeletonObjectPath))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
        Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
        MatchedJson.Add(MakeShared<FJsonValueObject>(Entry));

        if (Asset.AssetClassPath == UAnimMontage::StaticClass()->GetClassPathName())
        {
            ++MontageCount;
        }
        else
        {
            ++SequenceCount;
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("skeleton_path"), SkeletonObj->GetPathName());
    Result->SetBoolField(TEXT("include_montages"), bIncludeMontages);
    Result->SetStringField(TEXT("path_filter"), PathFilter);
    Result->SetNumberField(TEXT("total_count"), MatchedJson.Num());
    Result->SetNumberField(TEXT("sequence_count"), SequenceCount);
    Result->SetNumberField(TEXT("montage_count"), MontageCount);
    Result->SetArrayField(TEXT("assets"), MatchedJson);
    return Result;
}

namespace
{
    TSharedPtr<FJsonObject> MakeAssetRefJson(const UObject* Obj)
    {
        if (!Obj) return nullptr;
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("name"), Obj->GetName());
        Out->SetStringField(TEXT("path"), Obj->GetPathName());
        return Out;
    }

    FString PreviewAppMethodToString(EPreviewAnimationBlueprintApplicationMethod M)
    {
        switch (M)
        {
            case EPreviewAnimationBlueprintApplicationMethod::LinkedLayers:    return TEXT("LinkedLayers");
            case EPreviewAnimationBlueprintApplicationMethod::LinkedAnimGraph: return TEXT("LinkedAnimGraph");
            default:                                                           return TEXT("Unknown");
        }
    }
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetAnimBlueprintInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Blueprint);
    if (!AnimBP)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint is not an AnimBlueprint: %s (class=%s)"),
                *BlueprintPath, *Blueprint->GetClass()->GetName()));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), AnimBP->GetName());
    Result->SetStringField(TEXT("path"), AnimBP->GetPathName());

    if (AnimBP->ParentClass)
    {
        Result->SetStringField(TEXT("parent_class"), AnimBP->ParentClass->GetName());
    }

    switch (AnimBP->BlueprintType)
    {
        case BPTYPE_Normal:           Result->SetStringField(TEXT("blueprint_type"), TEXT("Normal")); break;
        case BPTYPE_Interface:        Result->SetStringField(TEXT("blueprint_type"), TEXT("Interface")); break;
        case BPTYPE_FunctionLibrary:  Result->SetStringField(TEXT("blueprint_type"), TEXT("FunctionLibrary")); break;
        case BPTYPE_MacroLibrary:     Result->SetStringField(TEXT("blueprint_type"), TEXT("MacroLibrary")); break;
        default:                      Result->SetStringField(TEXT("blueprint_type"), TEXT("Other")); break;
    }

    // --- AnimBP-specific: skeleton / template flag ---
    Result->SetBoolField(TEXT("is_template"), AnimBP->bIsTemplate);

    if (TSharedPtr<FJsonObject> SkelJson = MakeAssetRefJson(AnimBP->TargetSkeleton))
    {
        Result->SetObjectField(TEXT("target_skeleton"), SkelJson);
    }
    else
    {
        Result->SetField(TEXT("target_skeleton"), MakeShared<FJsonValueNull>());
    }

    // --- Preview mesh / preview anim BP (editor-time preview settings) ---
    // Use const GetPreviewMesh() so we don't auto-lookup via target skeleton; we want
    // only what's explicitly set on this AnimBP. Caller can still cross-reference
    // the skeleton's preview mesh via the target_skeleton output.
    if (TSharedPtr<FJsonObject> MeshJson = MakeAssetRefJson(const_cast<const UAnimBlueprint*>(AnimBP)->GetPreviewMesh()))
    {
        Result->SetObjectField(TEXT("preview_skeletal_mesh"), MeshJson);
    }
    else
    {
        Result->SetField(TEXT("preview_skeletal_mesh"), MakeShared<FJsonValueNull>());
    }

    if (TSharedPtr<FJsonObject> PreviewAnimBPJson = MakeAssetRefJson(AnimBP->GetPreviewAnimationBlueprint()))
    {
        Result->SetObjectField(TEXT("preview_animation_blueprint"), PreviewAnimBPJson);
    }
    else
    {
        Result->SetField(TEXT("preview_animation_blueprint"), MakeShared<FJsonValueNull>());
    }

    Result->SetStringField(TEXT("preview_animation_blueprint_application_method"),
        PreviewAppMethodToString(AnimBP->GetPreviewAnimationBlueprintApplicationMethod()));
    Result->SetStringField(TEXT("preview_animation_blueprint_tag"),
        AnimBP->GetPreviewAnimationBlueprintTag().ToString());

#if WITH_EDITORONLY_DATA
    if (UClass* BindingClass = AnimBP->GetDefaultBindingClass())
    {
        Result->SetStringField(TEXT("default_binding_class"), BindingClass->GetName());
    }
    else
    {
        Result->SetField(TEXT("default_binding_class"), MakeShared<FJsonValueNull>());
    }
#endif

    // --- Optimization flags ---
    Result->SetBoolField(TEXT("use_multi_threaded_animation_update"), AnimBP->bUseMultiThreadedAnimationUpdate);
    Result->SetBoolField(TEXT("warn_about_blueprint_usage"), AnimBP->bWarnAboutBlueprintUsage);
    Result->SetBoolField(TEXT("enable_linked_anim_layer_instance_sharing"), AnimBP->bEnableLinkedAnimLayerInstanceSharing != 0);

    // --- Sync groups (names only) ---
    TArray<TSharedPtr<FJsonValue>> GroupsArray;
    for (const FAnimGroupInfo& Group : AnimBP->Groups)
    {
        GroupsArray.Add(MakeShared<FJsonValueString>(Group.Name.ToString()));
    }
    Result->SetArrayField(TEXT("anim_groups"), GroupsArray);

    Result->SetNumberField(TEXT("num_parent_asset_overrides"), AnimBP->ParentAssetOverrides.Num());
    Result->SetNumberField(TEXT("num_pose_watches"), AnimBP->PoseWatches.Num());

    // --- Parent / root anim blueprint lineage (editor-only helpers) ---
#if WITH_EDITOR
    if (TSharedPtr<FJsonObject> ParentJson = MakeAssetRefJson(UAnimBlueprint::GetParentAnimBlueprint(AnimBP)))
    {
        Result->SetObjectField(TEXT("parent_anim_blueprint"), ParentJson);
    }
    else
    {
        Result->SetField(TEXT("parent_anim_blueprint"), MakeShared<FJsonValueNull>());
    }

    UAnimBlueprint* Root = UAnimBlueprint::FindRootAnimBlueprint(AnimBP);
    if (Root && Root != AnimBP)
    {
        Result->SetObjectField(TEXT("root_anim_blueprint"), MakeAssetRefJson(Root));
    }
    else
    {
        Result->SetField(TEXT("root_anim_blueprint"), MakeShared<FJsonValueNull>());
    }
#endif

    // --- AnimGraph walk: state machines + directly referenced animation assets ---
    // Walk every function graph in the blueprint. AnimGraph nodes (UAnimGraphNode_Base) only
    // appear inside AnimGraph-style graphs, so filtering by node type naturally skips
    // regular K2 function graphs. State machines are UAnimGraphNode_StateMachineBase;
    // directly-used assets come from UAnimGraphNode_Base::GetAnimationAsset() (covers
    // AssetPlayer nodes, BlendSpace player nodes, etc.). This does NOT cover assets
    // picked at runtime (Chooser / property binding / PoseSearchDatabase).
    TArray<TSharedPtr<FJsonValue>> StateMachinesArray;
    TSet<FString> SeenAssetPaths;
    TArray<TSharedPtr<FJsonValue>> UsedAssetsArray;

    auto WalkGraphNodes = [&](UEdGraph* Graph)
    {
        if (!Graph) return;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (UAnimGraphNode_StateMachineBase* SMNode = Cast<UAnimGraphNode_StateMachineBase>(Node))
            {
                TSharedPtr<FJsonObject> SMObj = MakeShared<FJsonObject>();
                SMObj->SetStringField(TEXT("node_name"), SMNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
                // EditorStateMachineGraph holds the sub-graph with UAnimStateNodeBase nodes.
                int32 StateCount = 0;
                // EditorStateMachineGraph is TObjectPtr<UAnimationStateMachineGraph>; .Get() gives the raw derived pointer
                // which implicitly casts to UEdGraph* (UAnimationStateMachineGraph is a UEdGraph subclass).
                if (UEdGraph* SMGraph = SMNode->EditorStateMachineGraph.Get())
                {
                    for (UEdGraphNode* SubNode : SMGraph->Nodes)
                    {
                        // Count state-like nodes (UAnimStateNodeBase / UAnimStateNode / UAnimStateAliasNode / UAnimStateConduitNode).
                        // We match by class name to avoid including the private AnimationStateMachineGraph header here.
                        if (SubNode && SubNode->GetClass()->GetName().Contains(TEXT("AnimState")))
                        {
                            ++StateCount;
                        }
                    }
                    SMObj->SetStringField(TEXT("sub_graph_name"), SMGraph->GetName());
                }
                SMObj->SetNumberField(TEXT("state_count"), StateCount);
                StateMachinesArray.Add(MakeShared<FJsonValueObject>(SMObj));
            }

            if (UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node))
            {
                if (UAnimationAsset* Asset = AnimNode->GetAnimationAsset())
                {
                    const FString AssetPath = Asset->GetPathName();
                    if (!SeenAssetPaths.Contains(AssetPath))
                    {
                        SeenAssetPaths.Add(AssetPath);
                        TSharedPtr<FJsonObject> AObj = MakeShared<FJsonObject>();
                        AObj->SetStringField(TEXT("name"), Asset->GetName());
                        AObj->SetStringField(TEXT("path"), AssetPath);
                        AObj->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
                        UsedAssetsArray.Add(MakeShared<FJsonValueObject>(AObj));
                    }
                }
            }
        }
    };

    for (UEdGraph* Graph : AnimBP->FunctionGraphs)
    {
        WalkGraphNodes(Graph);
    }

    Result->SetArrayField(TEXT("state_machines"), StateMachinesArray);
    Result->SetArrayField(TEXT("used_animation_assets"), UsedAssetsArray);

    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetSkeletonBoneHierarchy(const TSharedPtr<FJsonObject>& Params)
{
    FString SkeletonPath;
    if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'skeleton_path' parameter"));
    }

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Skeleton asset not found: %s"), *SkeletonPath));
    }

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
    const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton.GetRawRefBoneInfo();
    const int32 NumBones = RefSkeleton.GetRawBoneNum();

    // Precompute direct children indices so each bone carries its sub-tree entry points.
    TArray<TArray<int32>> ChildIndices;
    ChildIndices.SetNum(NumBones);
    for (int32 i = 0; i < NumBones; ++i)
    {
        const int32 ParentIdx = BoneInfos[i].ParentIndex;
        if (ParentIdx >= 0 && ParentIdx < NumBones)
        {
            ChildIndices[ParentIdx].Add(i);
        }
    }

    TArray<TSharedPtr<FJsonValue>> BonesJson;
    BonesJson.Reserve(NumBones);
    for (int32 i = 0; i < NumBones; ++i)
    {
        const FMeshBoneInfo& Info = BoneInfos[i];

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("name"), Info.Name.ToString());
        Entry->SetNumberField(TEXT("parent_index"), Info.ParentIndex);
        if (Info.ParentIndex >= 0 && Info.ParentIndex < NumBones)
        {
            Entry->SetStringField(TEXT("parent_name"), BoneInfos[Info.ParentIndex].Name.ToString());
        }
        else
        {
            Entry->SetStringField(TEXT("parent_name"), FString());
        }

        TArray<TSharedPtr<FJsonValue>> ChildrenJson;
        ChildrenJson.Reserve(ChildIndices[i].Num());
        for (int32 ChildIdx : ChildIndices[i])
        {
            ChildrenJson.Add(MakeShared<FJsonValueNumber>(ChildIdx));
        }
        Entry->SetArrayField(TEXT("children_indices"), ChildrenJson);

        BonesJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    // Virtual bones are authored on the USkeleton and layered on top of the raw hierarchy;
    // expose them separately so callers can tell which bones are synthetic.
    const TArray<FVirtualBone>& VirtualBones = Skeleton->GetVirtualBones();
    TArray<TSharedPtr<FJsonValue>> VirtualJson;
    VirtualJson.Reserve(VirtualBones.Num());
    for (const FVirtualBone& VB : VirtualBones)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("virtual_bone_name"), VB.VirtualBoneName.ToString());
        Entry->SetStringField(TEXT("source_bone_name"), VB.SourceBoneName.ToString());
        Entry->SetStringField(TEXT("target_bone_name"), VB.TargetBoneName.ToString());
        VirtualJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
    Result->SetNumberField(TEXT("bone_count"), NumBones);
    Result->SetArrayField(TEXT("bones"), BonesJson);
    Result->SetNumberField(TEXT("virtual_bone_count"), VirtualBones.Num());
    Result->SetArrayField(TEXT("virtual_bones"), VirtualJson);
    return Result;
}

namespace
{
    TSharedPtr<FJsonObject> TransformToJson(const FTransform& T)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        const FVector Loc = T.GetLocation();
        const FQuat  Rot = T.GetRotation();
        const FRotator Euler = Rot.Rotator();
        const FVector Scale = T.GetScale3D();

        TArray<TSharedPtr<FJsonValue>> LocArr = {
            MakeShared<FJsonValueNumber>(Loc.X), MakeShared<FJsonValueNumber>(Loc.Y), MakeShared<FJsonValueNumber>(Loc.Z)
        };
        Out->SetArrayField(TEXT("translation"), LocArr);

        TArray<TSharedPtr<FJsonValue>> QuatArr = {
            MakeShared<FJsonValueNumber>(Rot.X), MakeShared<FJsonValueNumber>(Rot.Y),
            MakeShared<FJsonValueNumber>(Rot.Z), MakeShared<FJsonValueNumber>(Rot.W)
        };
        Out->SetArrayField(TEXT("rotation_quat"), QuatArr);

        TArray<TSharedPtr<FJsonValue>> EulerArr = {
            MakeShared<FJsonValueNumber>(Euler.Pitch),
            MakeShared<FJsonValueNumber>(Euler.Yaw),
            MakeShared<FJsonValueNumber>(Euler.Roll)
        };
        Out->SetArrayField(TEXT("rotation_euler_pyr"), EulerArr);

        TArray<TSharedPtr<FJsonValue>> ScaleArr = {
            MakeShared<FJsonValueNumber>(Scale.X), MakeShared<FJsonValueNumber>(Scale.Y), MakeShared<FJsonValueNumber>(Scale.Z)
        };
        Out->SetArrayField(TEXT("scale"), ScaleArr);

        return Out;
    }
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetSkeletonReferencePose(const TSharedPtr<FJsonObject>& Params)
{
    FString SkeletonPath;
    if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'skeleton_path' parameter"));
    }

    FString Space;
    Params->TryGetStringField(TEXT("space"), Space);
    if (Space.IsEmpty())
    {
        Space = TEXT("Local");
    }
    const bool bComponent = Space.Equals(TEXT("Component"), ESearchCase::IgnoreCase);
    if (!bComponent && !Space.Equals(TEXT("Local"), ESearchCase::IgnoreCase))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Invalid 'space' value: %s (expected 'Local' or 'Component')"), *Space));
    }

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Skeleton asset not found: %s"), *SkeletonPath));
    }

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
    const TArray<FMeshBoneInfo>& Infos = RefSkeleton.GetRawRefBoneInfo();
    const TArray<FTransform>& LocalPoses = RefSkeleton.GetRawRefBonePose();
    const int32 Num = RefSkeleton.GetRawBoneNum();

    // Component-space poses are composed top-down: child_component = child_local * parent_component.
    // We pre-compute them in one pass since FMeshBoneInfo guarantees a parent is stored at a lower index.
    TArray<FTransform> ComponentPoses;
    if (bComponent)
    {
        ComponentPoses.SetNum(Num);
        for (int32 i = 0; i < Num; ++i)
        {
            const int32 Parent = Infos[i].ParentIndex;
            ComponentPoses[i] = (Parent == INDEX_NONE)
                ? LocalPoses[i]
                : LocalPoses[i] * ComponentPoses[Parent];
        }
    }

    TArray<TSharedPtr<FJsonValue>> BonesJson;
    BonesJson.Reserve(Num);
    for (int32 i = 0; i < Num; ++i)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("name"), Infos[i].Name.ToString());
        Entry->SetNumberField(TEXT("parent_index"), Infos[i].ParentIndex);

        const FTransform& T = bComponent ? ComponentPoses[i] : LocalPoses[i];
        const TSharedPtr<FJsonObject> TJson = TransformToJson(T);
        // Merge the transform fields directly into the entry so the JSON is flat.
        for (const auto& Pair : TJson->Values)
        {
            Entry->SetField(Pair.Key, Pair.Value);
        }
        BonesJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
    Result->SetStringField(TEXT("space"), bComponent ? TEXT("Component") : TEXT("Local"));
    Result->SetNumberField(TEXT("bone_count"), Num);
    Result->SetArrayField(TEXT("bones"), BonesJson);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetSkeletalMeshInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString MeshPath;
    if (!Params->TryGetStringField(TEXT("mesh_path"), MeshPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'mesh_path' parameter"));
    }

    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
    if (!Mesh)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("SkeletalMesh asset not found: %s"), *MeshPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Mesh->GetName());
    Result->SetStringField(TEXT("path"), Mesh->GetPathName());

    // Skeleton
    if (USkeleton* Skel = Mesh->GetSkeleton())
    {
        Result->SetStringField(TEXT("skeleton_path"), Skel->GetPathName());
    }
    else
    {
        Result->SetField(TEXT("skeleton_path"), MakeShared<FJsonValueNull>());
    }

    // Default PhysicsAsset
    if (UPhysicsAsset* PA = Mesh->GetPhysicsAsset())
    {
        Result->SetStringField(TEXT("default_physics_asset_path"), PA->GetPathName());
    }
    else
    {
        Result->SetField(TEXT("default_physics_asset_path"), MakeShared<FJsonValueNull>());
    }

    // Post Process AnimBlueprint (property actually belongs to SKM, not AnimBP — placed here per requirement doc's real semantics)
    if (TSubclassOf<UAnimInstance> PostAnimClass = Mesh->GetPostProcessAnimBlueprint())
    {
        Result->SetStringField(TEXT("post_process_anim_blueprint_class"), PostAnimClass->GetPathName());
    }
    else
    {
        Result->SetField(TEXT("post_process_anim_blueprint_class"), MakeShared<FJsonValueNull>());
    }

    // Bounds (import bounds from imported mesh; we use the mesh's cached bounds)
    const FBoxSphereBounds Bounds = Mesh->GetImportedBounds();
    const FVector Origin = Bounds.Origin;
    const FVector Extent = Bounds.BoxExtent;
    const FVector BoundsMin = Origin - Extent;
    const FVector BoundsMax = Origin + Extent;

    TSharedPtr<FJsonObject> BBoxJson = MakeShared<FJsonObject>();
    {
        TArray<TSharedPtr<FJsonValue>> MinArr = {
            MakeShared<FJsonValueNumber>(BoundsMin.X), MakeShared<FJsonValueNumber>(BoundsMin.Y), MakeShared<FJsonValueNumber>(BoundsMin.Z)
        };
        TArray<TSharedPtr<FJsonValue>> MaxArr = {
            MakeShared<FJsonValueNumber>(BoundsMax.X), MakeShared<FJsonValueNumber>(BoundsMax.Y), MakeShared<FJsonValueNumber>(BoundsMax.Z)
        };
        BBoxJson->SetArrayField(TEXT("min"), MinArr);
        BBoxJson->SetArrayField(TEXT("max"), MaxArr);
    }
    Result->SetObjectField(TEXT("bounding_box"), BBoxJson);

    TArray<TSharedPtr<FJsonValue>> OriginArr = {
        MakeShared<FJsonValueNumber>(Origin.X), MakeShared<FJsonValueNumber>(Origin.Y), MakeShared<FJsonValueNumber>(Origin.Z)
    };
    Result->SetArrayField(TEXT("bounds_origin"), OriginArr);

    TArray<TSharedPtr<FJsonValue>> ExtentArr = {
        MakeShared<FJsonValueNumber>(Extent.X), MakeShared<FJsonValueNumber>(Extent.Y), MakeShared<FJsonValueNumber>(Extent.Z)
    };
    Result->SetArrayField(TEXT("bounds_extent"), ExtentArr);

    // Height is the Z-span of the box extent doubled (extent is half-size), in the mesh's import space.
    Result->SetNumberField(TEXT("approximate_height_cm"), Extent.Z * 2.0);

    // LOD count (includes LOD0)
    Result->SetNumberField(TEXT("lod_count"), Mesh->GetLODNum());

    // Material slots
    TArray<TSharedPtr<FJsonValue>> MatArr;
    const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
    for (int32 i = 0; i < Materials.Num(); ++i)
    {
        const FSkeletalMaterial& SkMat = Materials[i];
        TSharedPtr<FJsonObject> MatObj = MakeShared<FJsonObject>();
        MatObj->SetNumberField(TEXT("index"), i);
        MatObj->SetStringField(TEXT("slot_name"), SkMat.MaterialSlotName.ToString());
        MatObj->SetStringField(TEXT("material_path"), SkMat.MaterialInterface ? SkMat.MaterialInterface->GetPathName() : FString());
        MatArr.Add(MakeShared<FJsonValueObject>(MatObj));
    }
    Result->SetArrayField(TEXT("material_slots"), MatArr);

    // Sockets defined on the mesh itself (separate from Skeleton sockets).
    // Call through the const overload to get raw USkeletalMeshSocket*.
    TArray<TSharedPtr<FJsonValue>> SocketArr;
    for (USkeletalMeshSocket* Socket : const_cast<const USkeletalMesh*>(Mesh)->GetMeshOnlySocketList())
    {
        if (!Socket) continue;
        TSharedPtr<FJsonObject> SockObj = MakeShared<FJsonObject>();
        SockObj->SetStringField(TEXT("name"), Socket->SocketName.ToString());
        SockObj->SetStringField(TEXT("bone_name"), Socket->BoneName.ToString());
        SocketArr.Add(MakeShared<FJsonValueObject>(SockObj));
    }
    Result->SetArrayField(TEXT("socket_names"), SocketArr);

    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetPhysicsAssetInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UPhysicsAsset* PhysAsset = LoadObject<UPhysicsAsset>(nullptr, *AssetPath);
    if (!PhysAsset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("PhysicsAsset not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), PhysAsset->GetName());
    Result->SetStringField(TEXT("path"), PhysAsset->GetPathName());

    // Preview mesh (editor-only asset reference; used by PhAT)
    if (USkeletalMesh* Preview = PhysAsset->GetPreviewMesh())
    {
        Result->SetStringField(TEXT("preview_skeletal_mesh"), Preview->GetPathName());
    }
    else
    {
        Result->SetField(TEXT("preview_skeletal_mesh"), MakeShared<FJsonValueNull>());
    }

    // Bodies
    TArray<TSharedPtr<FJsonValue>> BodiesJson;
    for (USkeletalBodySetup* Body : PhysAsset->SkeletalBodySetups)
    {
        if (!Body) continue;
        TSharedPtr<FJsonObject> BodyObj = MakeShared<FJsonObject>();
        BodyObj->SetStringField(TEXT("bone_name"), Body->BoneName.ToString());

        FString PhysicsTypeStr;
        switch (Body->PhysicsType)
        {
            case PhysType_Default:   PhysicsTypeStr = TEXT("Default"); break;
            case PhysType_Kinematic: PhysicsTypeStr = TEXT("Kinematic"); break;
            case PhysType_Simulated: PhysicsTypeStr = TEXT("Simulated"); break;
            default:                 PhysicsTypeStr = TEXT("Unknown"); break;
        }
        BodyObj->SetStringField(TEXT("physics_type"), PhysicsTypeStr);

        // DefaultInstance carries the per-body physical settings
        const FBodyInstance& Inst = Body->DefaultInstance;
        BodyObj->SetBoolField(TEXT("simulate_physics"), Inst.bSimulatePhysics);
        BodyObj->SetNumberField(TEXT("mass_override"), Inst.GetMassOverride());
        BodyObj->SetNumberField(TEXT("linear_damping"), Inst.LinearDamping);
        BodyObj->SetNumberField(TEXT("angular_damping"), Inst.AngularDamping);
        BodyObj->SetStringField(TEXT("collision_response"),
            Inst.GetCollisionEnabled() == ECollisionEnabled::NoCollision                    ? TEXT("NoCollision") :
            Inst.GetCollisionEnabled() == ECollisionEnabled::QueryOnly                      ? TEXT("QueryOnly") :
            Inst.GetCollisionEnabled() == ECollisionEnabled::PhysicsOnly                    ? TEXT("PhysicsOnly") :
            Inst.GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics                ? TEXT("QueryAndPhysics") :
            Inst.GetCollisionEnabled() == ECollisionEnabled::ProbeOnly                      ? TEXT("ProbeOnly") :
            Inst.GetCollisionEnabled() == ECollisionEnabled::QueryAndProbe                  ? TEXT("QueryAndProbe") :
                                                                                              TEXT("Unknown"));

        // Primitive counts per shape class
        const FKAggregateGeom& Agg = Body->AggGeom;
        BodyObj->SetNumberField(TEXT("num_sphere"),  Agg.SphereElems.Num());
        BodyObj->SetNumberField(TEXT("num_box"),     Agg.BoxElems.Num());
        BodyObj->SetNumberField(TEXT("num_capsule"), Agg.SphylElems.Num());
        BodyObj->SetNumberField(TEXT("num_convex"),  Agg.ConvexElems.Num());
        BodyObj->SetNumberField(TEXT("num_tapered_capsule"), Agg.TaperedCapsuleElems.Num());

        BodiesJson.Add(MakeShared<FJsonValueObject>(BodyObj));
    }
    Result->SetArrayField(TEXT("bodies"), BodiesJson);
    Result->SetNumberField(TEXT("body_count"), BodiesJson.Num());

    // Constraints
    TArray<TSharedPtr<FJsonValue>> ConstraintsJson;
    for (UPhysicsConstraintTemplate* Constraint : PhysAsset->ConstraintSetup)
    {
        if (!Constraint) continue;
        const FConstraintInstance& CI = Constraint->DefaultInstance;
        TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
        CObj->SetStringField(TEXT("constraint_name"), CI.JointName.ToString());
        CObj->SetStringField(TEXT("bone1"), CI.ConstraintBone1.ToString());
        CObj->SetStringField(TEXT("bone2"), CI.ConstraintBone2.ToString());

        // Linear limit size (single radius for locked/limited axes). Motion type per axis is also useful.
        CObj->SetNumberField(TEXT("linear_limit_size"), CI.GetLinearLimit());
        CObj->SetNumberField(TEXT("angular_swing1_limit_deg"), CI.GetAngularSwing1Limit());
        CObj->SetNumberField(TEXT("angular_swing2_limit_deg"), CI.GetAngularSwing2Limit());
        CObj->SetNumberField(TEXT("angular_twist_limit_deg"),  CI.GetAngularTwistLimit());

        ConstraintsJson.Add(MakeShared<FJsonValueObject>(CObj));
    }
    Result->SetArrayField(TEXT("constraints"), ConstraintsJson);
    Result->SetNumberField(TEXT("constraint_count"), ConstraintsJson.Num());

    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetAssetReferences(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    // Asset Registry's referencer query works on package names; derive the package name from the path.
    // Accept both "/Game/Foo/Bar" and "/Game/Foo/Bar.Bar" object paths.
    FString PackageName = AssetPath;
    int32 DotIdx = INDEX_NONE;
    if (PackageName.FindChar('.', DotIdx))
    {
        PackageName = PackageName.Left(DotIdx);
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    TArray<FName> Referencers;
    AssetRegistry.GetReferencers(FName(*PackageName), Referencers);

    TArray<TSharedPtr<FJsonValue>> RefsJson;
    RefsJson.Reserve(Referencers.Num());
    for (const FName& RefPkg : Referencers)
    {
        // Enrich with asset class info by looking up the package contents.
        TArray<FAssetData> AssetsInRef;
        AssetRegistry.GetAssetsByPackageName(RefPkg, AssetsInRef);
        if (AssetsInRef.Num() == 0)
        {
            TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
            Obj->SetStringField(TEXT("package_name"), RefPkg.ToString());
            RefsJson.Add(MakeShared<FJsonValueObject>(Obj));
            continue;
        }
        for (const FAssetData& Data : AssetsInRef)
        {
            TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
            Obj->SetStringField(TEXT("package_name"), RefPkg.ToString());
            Obj->SetStringField(TEXT("asset_path"), Data.GetObjectPathString());
            Obj->SetStringField(TEXT("asset_class"), Data.AssetClassPath.GetAssetName().ToString());
            RefsJson.Add(MakeShared<FJsonValueObject>(Obj));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("package_name"), PackageName);
    Result->SetNumberField(TEXT("referencer_count"), RefsJson.Num());
    Result->SetArrayField(TEXT("referencers"), RefsJson);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleListAnimationBlueprintsForSkeleton(const TSharedPtr<FJsonObject>& Params)
{
    FString SkeletonPath;
    if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'skeleton_path' parameter"));
    }

    FString PathFilter;
    Params->TryGetStringField(TEXT("path_filter"), PathFilter);

    // Normalize to full object path (".LeafName") to match the AR Skeleton tag format.
    FString SkeletonObjectPath = SkeletonPath;
    {
        int32 DotIdx = INDEX_NONE;
        int32 SlashIdx = INDEX_NONE;
        SkeletonObjectPath.FindLastChar('.', DotIdx);
        SkeletonObjectPath.FindLastChar('/', SlashIdx);
        if (DotIdx <= SlashIdx && SlashIdx != INDEX_NONE)
        {
            const FString LeafName = SkeletonObjectPath.Mid(SlashIdx + 1);
            SkeletonObjectPath = SkeletonObjectPath + TEXT(".") + LeafName;
        }
    }

    USkeleton* SkeletonObj = LoadObject<USkeleton>(nullptr, *SkeletonObjectPath);
    if (!SkeletonObj)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Skeleton asset not found: %s"), *SkeletonPath));
    }

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UAnimBlueprint::StaticClass()->GetClassPathName());
    if (PathFilter.Len() > 0)
    {
        Filter.PackagePaths.Add(FName(*PathFilter));
    }

    TArray<FAssetData> Candidates;
    AssetRegistry.GetAssets(Filter, Candidates);

    TArray<TSharedPtr<FJsonValue>> Matched;
    for (const FAssetData& Asset : Candidates)
    {
        // UAnimBlueprint exposes TargetSkeleton as an AssetRegistrySearchable UPROPERTY.
        // The tag value format is Skeleton'/Game/Path/SK_Foo.SK_Foo'; Contains-check on the
        // object path is robust across UE versions without needing ExportText parsing.
        FString TagValue;
        if (!Asset.GetTagValue<FString>(TEXT("TargetSkeleton"), TagValue))
        {
            continue;
        }
        if (!TagValue.Contains(SkeletonObjectPath))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
        Entry->SetStringField(TEXT("name"), Asset.AssetName.ToString());
        Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());

        // Is this a template AnimBP? Available as an AR tag as well.
        FString IsTemplateTag;
        if (Asset.GetTagValue<FString>(TEXT("bIsTemplate"), IsTemplateTag))
        {
            Entry->SetBoolField(TEXT("is_template"), IsTemplateTag.Equals(TEXT("true"), ESearchCase::IgnoreCase));
        }

        Matched.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("skeleton_path"), SkeletonPath);
    Result->SetStringField(TEXT("path_filter"), PathFilter);
    Result->SetNumberField(TEXT("total_count"), Matched.Num());
    Result->SetArrayField(TEXT("anim_blueprints"), Matched);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleListChooserTables(const TSharedPtr<FJsonObject>& Params)
{
    FString PathFilter;
    Params->TryGetStringField(TEXT("path_filter"), PathFilter);

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UChooserTable::StaticClass()->GetClassPathName());
    if (PathFilter.Len() > 0)
    {
        Filter.PackagePaths.Add(FName(*PathFilter));
    }

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> AssetsJson;
    AssetsJson.Reserve(Assets.Num());
    for (const FAssetData& Asset : Assets)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
        Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
        AssetsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path_filter"), PathFilter);
    Result->SetNumberField(TEXT("total_count"), Assets.Num());
    Result->SetArrayField(TEXT("assets"), AssetsJson);
    return Result;
}

namespace
{
    // Map EObjectChooserResultType enum → string that matches source enum names (not the UMETA display).
    FString ChooserResultTypeToString(EObjectChooserResultType T)
    {
        switch (T)
        {
            case EObjectChooserResultType::ObjectResult:    return TEXT("ObjectResult");
            case EObjectChooserResultType::ClassResult:     return TEXT("ClassResult");
            case EObjectChooserResultType::NoPrimaryResult: return TEXT("NoPrimaryResult");
            default:                                         return TEXT("Unknown");
        }
    }

    // Map EContextObjectDirection (source: Read/Write/ReadWrite) → UI-visible Input/Output/InputOutput.
    FString ContextObjectDirectionToString(EContextObjectDirection D)
    {
        switch (D)
        {
            case EContextObjectDirection::Read:      return TEXT("Input");
            case EContextObjectDirection::Write:     return TEXT("Output");
            case EContextObjectDirection::ReadWrite: return TEXT("InputOutput");
            default:                                  return TEXT("Unknown");
        }
    }

    // Serialize a Chooser Row Result FInstancedStruct into a JSON entry.
    // Used for both row results and the FallbackResult field.
    TSharedPtr<FJsonObject> SerializeChooserResult(const FInstancedStruct& ResultInst, int32 Index)
    {
        const UScriptStruct* RStruct = ResultInst.GetScriptStruct();

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        if (Index >= 0)
        {
            Entry->SetNumberField(TEXT("index"), Index);
        }
        Entry->SetStringField(TEXT("struct_name"), RStruct ? RStruct->GetName() : FString());

        if (!RStruct || !ResultInst.IsValid())
        {
            Entry->SetStringField(TEXT("kind"), TEXT("empty"));
        }
        else if (RStruct == FAssetChooser::StaticStruct())
        {
            const FAssetChooser& Ch = ResultInst.Get<FAssetChooser>();
            Entry->SetStringField(TEXT("kind"), TEXT("asset"));
            if (Ch.Asset)
            {
                Entry->SetStringField(TEXT("asset_path"), Ch.Asset->GetPathName());
                Entry->SetStringField(TEXT("asset_class"), Ch.Asset->GetClass()->GetName());
            }
        }
        else if (RStruct == FSoftAssetChooser::StaticStruct())
        {
            const FSoftAssetChooser& Ch = ResultInst.Get<FSoftAssetChooser>();
            Entry->SetStringField(TEXT("kind"), TEXT("soft_asset"));
            Entry->SetStringField(TEXT("soft_path"), Ch.Asset.ToString());
        }
        else if (RStruct == FEvaluateChooser::StaticStruct())
        {
            const FEvaluateChooser& Ch = ResultInst.Get<FEvaluateChooser>();
            Entry->SetStringField(TEXT("kind"), TEXT("evaluate_chooser"));
            if (Ch.Chooser)
            {
                Entry->SetStringField(TEXT("nested_chooser_path"), Ch.Chooser->GetPathName());
            }
        }
        else if (RStruct == FNestedChooser::StaticStruct())
        {
            const FNestedChooser& Ch = ResultInst.Get<FNestedChooser>();
            Entry->SetStringField(TEXT("kind"), TEXT("nested_chooser"));
            if (Ch.Chooser)
            {
                Entry->SetStringField(TEXT("nested_chooser_path"), Ch.Chooser->GetPathName());
            }
        }
        else
        {
            Entry->SetStringField(TEXT("kind"), TEXT("unknown"));
        }
        return Entry;
    }

    // Serialize one FContextObjectTypeBase subclass (either FContextObjectTypeClass or FContextObjectTypeStruct).
    TSharedPtr<FJsonObject> SerializeChooserParameter(const FInstancedStruct& ParamInst, int32 Index)
    {
        const UScriptStruct* Kind = ParamInst.GetScriptStruct();

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), Index);
        Entry->SetStringField(TEXT("struct_name"), Kind ? Kind->GetName() : FString());

        if (!Kind || !ParamInst.IsValid())
        {
            Entry->SetStringField(TEXT("kind"), TEXT("empty"));
            return Entry;
        }

        const FContextObjectTypeBase* Base = ParamInst.GetPtr<FContextObjectTypeBase>();
        if (!Base)
        {
            Entry->SetStringField(TEXT("kind"), TEXT("unknown"));
            return Entry;
        }
        Entry->SetStringField(TEXT("direction"), ContextObjectDirectionToString(Base->Direction));

        if (Kind == FContextObjectTypeClass::StaticStruct())
        {
            const FContextObjectTypeClass& C = ParamInst.Get<FContextObjectTypeClass>();
            Entry->SetStringField(TEXT("kind"), TEXT("class"));
            TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
            if (C.Class)
            {
                Ref->SetStringField(TEXT("name"), C.Class->GetName());
                Ref->SetStringField(TEXT("path"), C.Class->GetPathName());
            }
            Entry->SetObjectField(TEXT("class_or_struct"), Ref);
        }
        else if (Kind == FContextObjectTypeStruct::StaticStruct())
        {
            const FContextObjectTypeStruct& S = ParamInst.Get<FContextObjectTypeStruct>();
            Entry->SetStringField(TEXT("kind"), TEXT("struct"));
            TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
            if (S.Struct)
            {
                Ref->SetStringField(TEXT("name"), S.Struct->GetName());
                Ref->SetStringField(TEXT("path"), S.Struct->GetPathName());
            }
            Entry->SetObjectField(TEXT("class_or_struct"), Ref);
        }
        else
        {
            Entry->SetStringField(TEXT("kind"), TEXT("unknown"));
        }
        return Entry;
    }

    // Reach into a column's FInstancedStruct InputValue to extract the embedded
    // FChooserPropertyBinding (or a subclass-specific enum_type / allowed_class /
    // struct_type). Works by reflection on the parameter struct: every chooser
    // parameter class inherited from FChooserParameterBase exposes its binding
    // as a FStructProperty named "Binding" (per CHOOSER_PARAMETER_BOILERPLATE).
    TSharedPtr<FJsonObject> SerializeChooserColumnBinding(const FInstancedStruct& ColInst)
    {
        const UScriptStruct* ColStruct = ColInst.GetScriptStruct();
        if (!ColStruct || !ColInst.IsValid())
        {
            return nullptr;
        }

        // Find InputValue FStructProperty on the column (itself an FInstancedStruct).
        FStructProperty* InputValueProp = FindFProperty<FStructProperty>(ColStruct, TEXT("InputValue"));
        if (!InputValueProp)
        {
            return nullptr;
        }
        const FInstancedStruct* InputValue = InputValueProp->ContainerPtrToValuePtr<FInstancedStruct>(ColInst.GetMemory());
        if (!InputValue || !InputValue->IsValid())
        {
            return nullptr;
        }

        const UScriptStruct* ParamStruct = InputValue->GetScriptStruct();
        if (!ParamStruct)
        {
            return nullptr;
        }

        // Find Binding FStructProperty on the parameter struct.
        FStructProperty* BindingProp = FindFProperty<FStructProperty>(ParamStruct, TEXT("Binding"));
        if (!BindingProp || !BindingProp->Struct)
        {
            return nullptr;
        }
        const void* BindingMem = BindingProp->ContainerPtrToValuePtr<void>(InputValue->GetMemory());
        if (!BindingMem)
        {
            return nullptr;
        }

        // Base fields from FChooserPropertyBinding (PropertyBindingChain / ContextIndex / IsBoundToRoot / DisplayName).
        const FChooserPropertyBinding* Binding = static_cast<const FChooserPropertyBinding*>(BindingMem);

        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("binding_struct"), BindingProp->Struct->GetName());
        Out->SetNumberField(TEXT("context_index"), Binding->ContextIndex);
        Out->SetBoolField(TEXT("is_bound_to_root"), Binding->IsBoundToRoot);

        TArray<TSharedPtr<FJsonValue>> ChainJson;
        ChainJson.Reserve(Binding->PropertyBindingChain.Num());
        for (const FName& Seg : Binding->PropertyBindingChain)
        {
            ChainJson.Add(MakeShared<FJsonValueString>(Seg.ToString()));
        }
        Out->SetArrayField(TEXT("property_path"), ChainJson);

        FString Joined;
        for (int32 i = 0; i < Binding->PropertyBindingChain.Num(); ++i)
        {
            if (i > 0) Joined += TEXT(".");
            Joined += Binding->PropertyBindingChain[i].ToString();
        }
        Out->SetStringField(TEXT("path_as_text"), Joined);

#if WITH_EDITORONLY_DATA
        Out->SetStringField(TEXT("display_name"), Binding->DisplayName);
#endif

        // Subclass-specific fields (editor-only).
#if WITH_EDITORONLY_DATA
        if (BindingProp->Struct->IsChildOf(FChooserEnumPropertyBinding::StaticStruct()))
        {
            const FChooserEnumPropertyBinding* EB = static_cast<const FChooserEnumPropertyBinding*>(BindingMem);
            if (EB->Enum)
            {
                TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
                Ref->SetStringField(TEXT("name"), EB->Enum->GetName());
                Ref->SetStringField(TEXT("path"), EB->Enum->GetPathName());
                Out->SetObjectField(TEXT("enum_type"), Ref);
            }
        }
        else if (BindingProp->Struct->IsChildOf(FChooserObjectPropertyBinding::StaticStruct()))
        {
            const FChooserObjectPropertyBinding* OB = static_cast<const FChooserObjectPropertyBinding*>(BindingMem);
            if (OB->AllowedClass)
            {
                TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
                Ref->SetStringField(TEXT("name"), OB->AllowedClass->GetName());
                Ref->SetStringField(TEXT("path"), OB->AllowedClass->GetPathName());
                Out->SetObjectField(TEXT("allowed_class"), Ref);
            }
        }
        else if (BindingProp->Struct->IsChildOf(FChooserStructPropertyBinding::StaticStruct()))
        {
            const FChooserStructPropertyBinding* SB = static_cast<const FChooserStructPropertyBinding*>(BindingMem);
            if (SB->StructType)
            {
                TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
                Ref->SetStringField(TEXT("name"), SB->StructType->GetName());
                Ref->SetStringField(TEXT("path"), SB->StructType->GetPathName());
                Out->SetObjectField(TEXT("struct_type"), Ref);
            }
        }
#endif

        return Out;
    }
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetChooserTableInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UChooserTable* Table = LoadObject<UChooserTable>(nullptr, *AssetPath);
    if (!Table)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("ChooserTable asset not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Result->SetStringField(TEXT("asset_class"), Table->GetClass()->GetName());
    Result->SetBoolField(TEXT("is_cooked_data"), Table->IsCookedData());

    // --- Top-level configuration (UChooserSignature parent) ---
    Result->SetStringField(TEXT("result_type"), ChooserResultTypeToString(Table->ResultType));

    if (Table->OutputObjectType)
    {
        TSharedPtr<FJsonObject> Ref = MakeShared<FJsonObject>();
        Ref->SetStringField(TEXT("name"), Table->OutputObjectType->GetName());
        Ref->SetStringField(TEXT("path"), Table->OutputObjectType->GetPathName());
        Result->SetObjectField(TEXT("result_class"), Ref);
    }
    else
    {
        Result->SetField(TEXT("result_class"), MakeShared<FJsonValueNull>());
    }

    // Parameters (UI name) / ContextData (source field).
    TArray<TSharedPtr<FJsonValue>> ParamsJson;
    ParamsJson.Reserve(Table->ContextData.Num());
    for (int32 i = 0; i < Table->ContextData.Num(); ++i)
    {
        ParamsJson.Add(MakeShared<FJsonValueObject>(SerializeChooserParameter(Table->ContextData[i], i)));
    }
    Result->SetNumberField(TEXT("parameter_count"), Table->ContextData.Num());
    Result->SetArrayField(TEXT("parameters"), ParamsJson);

    // --- Columns ---
    // One entry per ColumnsStructs slot. Now includes:
    //   - has_filters / has_outputs (virtual methods on FChooserColumnBase)
    //   - sub_type: "Input" / "Output" / "Mixed" (derived from has_filters/has_outputs)
    //   - binding: dict from InputValue → Binding sub-struct (property path, context index,
    //     enum / allowed_class / struct_type sub-fields where applicable)
    //   - b_disabled (editor-only)
    TArray<TSharedPtr<FJsonValue>> ColumnsJson;
    ColumnsJson.Reserve(Table->ColumnsStructs.Num());
    for (int32 i = 0; i < Table->ColumnsStructs.Num(); ++i)
    {
        FInstancedStruct& ColInst = Table->ColumnsStructs[i];
        const UScriptStruct* ColStruct = ColInst.GetScriptStruct();

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("struct_name"), ColStruct ? ColStruct->GetName() : FString());

        if (ColStruct && ColInst.IsValid())
        {
            const FChooserColumnBase& ColBase = ColInst.Get<FChooserColumnBase>();
            Entry->SetBoolField(TEXT("has_filters"), ColBase.HasFilters());
            Entry->SetBoolField(TEXT("has_outputs"), ColBase.HasOutputs());

            FString SubType;
            if (ColBase.HasFilters() && ColBase.HasOutputs()) SubType = TEXT("Mixed");
            else if (ColBase.HasOutputs())                    SubType = TEXT("Output");
            else                                              SubType = TEXT("Input");
            Entry->SetStringField(TEXT("sub_type"), SubType);

#if WITH_EDITORONLY_DATA
            Entry->SetBoolField(TEXT("b_disabled"), ColBase.bDisabled);
#endif

#if WITH_EDITOR
            // RowValuesPropertyName() requires non-const access
            FChooserColumnBase& ColBaseMut = ColInst.GetMutable<FChooserColumnBase>();
            Entry->SetStringField(TEXT("row_values_property"), ColBaseMut.RowValuesPropertyName().ToString());
#endif

            if (TSharedPtr<FJsonObject> BindingJson = SerializeChooserColumnBinding(ColInst))
            {
                Entry->SetObjectField(TEXT("binding"), BindingJson);
            }
            else
            {
                Entry->SetField(TEXT("binding"), MakeShared<FJsonValueNull>());
            }
        }

        ColumnsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetNumberField(TEXT("column_count"), Table->ColumnsStructs.Num());
    Result->SetArrayField(TEXT("columns"), ColumnsJson);

    // --- Results (rows) ---
    TArrayView<const FInstancedStruct> ResultsView;
    bool bUsedCooked = false;
#if WITH_EDITORONLY_DATA
    if (Table->ResultsStructs.Num() > 0)
    {
        ResultsView = MakeArrayView(Table->ResultsStructs.GetData(), Table->ResultsStructs.Num());
    }
    else
#endif
    {
        ResultsView = MakeArrayView(Table->CookedResults.GetData(), Table->CookedResults.Num());
        bUsedCooked = true;
    }

    TArray<TSharedPtr<FJsonValue>> ResultsJson;
    ResultsJson.Reserve(ResultsView.Num());
    for (int32 i = 0; i < ResultsView.Num(); ++i)
    {
        TSharedPtr<FJsonObject> Entry = SerializeChooserResult(ResultsView[i], i);

        // --- Per-row column_values ---
        // For each column in this Chooser, reflect out its RowValues[i] entry so the
        // caller can verify what matching / output value a given row has per column.
        TArray<TSharedPtr<FJsonValue>> RowColValsJson;
        RowColValsJson.Reserve(Table->ColumnsStructs.Num());
        for (int32 c = 0; c < Table->ColumnsStructs.Num(); ++c)
        {
            FInstancedStruct& ColInst = Table->ColumnsStructs[c];
            const UScriptStruct* ColStruct = ColInst.GetScriptStruct();

            TSharedPtr<FJsonObject> Cell = MakeShared<FJsonObject>();
            Cell->SetNumberField(TEXT("column_index"), c);

            if (!ColStruct || !ColInst.IsValid())
            {
                Cell->SetField(TEXT("value"), MakeShared<FJsonValueNull>());
                RowColValsJson.Add(MakeShared<FJsonValueObject>(Cell));
                continue;
            }

#if WITH_EDITOR
            const FName RowValsPropName = ColInst.GetMutable<FChooserColumnBase>().RowValuesPropertyName();
            FArrayProperty* RowValsProp = FindFProperty<FArrayProperty>(ColStruct, RowValsPropName);
#else
            FArrayProperty* RowValsProp = FindFProperty<FArrayProperty>(ColStruct, TEXT("RowValues"));
#endif

            if (!RowValsProp || !RowValsProp->Inner)
            {
                Cell->SetField(TEXT("value"), MakeShared<FJsonValueNull>());
                RowColValsJson.Add(MakeShared<FJsonValueObject>(Cell));
                continue;
            }

            FScriptArrayHelper ArrayHelper(RowValsProp, RowValsProp->ContainerPtrToValuePtr<void>(ColInst.GetMutableMemory()));
            if (!ArrayHelper.IsValidIndex(i))
            {
                Cell->SetField(TEXT("value"), MakeShared<FJsonValueNull>());
                RowColValsJson.Add(MakeShared<FJsonValueObject>(Cell));
                continue;
            }

            const void* ElemPtr = ArrayHelper.GetRawPtr(i);
            TSharedPtr<FJsonValue> CellJson = FJsonObjectConverter::UPropertyToJsonValue(
                RowValsProp->Inner,
                ElemPtr,
                /*CheckFlags=*/0,
                /*SkipFlags=*/CPF_Transient | CPF_DuplicateTransient);
            if (CellJson.IsValid())
            {
                Cell->SetField(TEXT("value"), CellJson);
            }
            else
            {
                Cell->SetField(TEXT("value"), MakeShared<FJsonValueNull>());
            }
            RowColValsJson.Add(MakeShared<FJsonValueObject>(Cell));
        }
        Entry->SetArrayField(TEXT("column_values"), RowColValsJson);

        ResultsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    Result->SetNumberField(TEXT("row_count"), ResultsView.Num());
    Result->SetBoolField(TEXT("used_cooked_results"), bUsedCooked);
    Result->SetArrayField(TEXT("results"), ResultsJson);

    // --- Fallback result (same shape as a row result entry) ---
    if (Table->FallbackResult.IsValid())
    {
        Result->SetObjectField(TEXT("fallback_result"), SerializeChooserResult(Table->FallbackResult, INDEX_NONE));
    }
    else
    {
        Result->SetField(TEXT("fallback_result"), MakeShared<FJsonValueNull>());
    }

    return Result;
}

namespace
{
    static FString BoneTranslationRetargetingModeToString(EBoneTranslationRetargetingMode::Type Mode)
    {
        switch (Mode)
        {
            case EBoneTranslationRetargetingMode::Animation:         return TEXT("Animation");
            case EBoneTranslationRetargetingMode::Skeleton:          return TEXT("Skeleton");
            case EBoneTranslationRetargetingMode::AnimationScaled:   return TEXT("AnimationScaled");
            case EBoneTranslationRetargetingMode::AnimationRelative: return TEXT("AnimationRelative");
            case EBoneTranslationRetargetingMode::OrientAndScale:    return TEXT("OrientAndScale");
            default:                                                 return TEXT("Unknown");
        }
    }

    // UIKRetargeter keeps its retarget pose maps and Profiles map private with only
    // a UIKRetargeterController friend, so we read them via reflection (FProperty).
    // Returns the keys (FName) of a TMap<FName, ?> UPROPERTY by name; values are not
    // touched here to keep the helper agnostic to the value struct type.
    static TArray<FName> ReadNameKeysFromMapProperty(const UObject* Owner, FName PropName)
    {
        TArray<FName> OutKeys;
        if (!Owner)
        {
            return OutKeys;
        }
        FMapProperty* MapProp = CastField<FMapProperty>(Owner->GetClass()->FindPropertyByName(PropName));
        if (!MapProp)
        {
            return OutKeys;
        }
        const void* MapAddr = MapProp->ContainerPtrToValuePtr<void>(Owner);
        FScriptMapHelper Helper(MapProp, MapAddr);
        for (int32 i = 0; i < Helper.GetMaxIndex(); ++i)
        {
            if (!Helper.IsValidIndex(i))
            {
                continue;
            }
            const FName* KeyPtr = reinterpret_cast<const FName*>(Helper.GetKeyPtr(i));
            if (KeyPtr)
            {
                OutKeys.Add(*KeyPtr);
            }
        }
        return OutKeys;
    }
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetSkeletonRetargetModes(const TSharedPtr<FJsonObject>& Params)
{
    FString SkeletonPath;
    if (!Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'skeleton_path' parameter"));
    }

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Skeleton asset not found: %s"), *SkeletonPath));
    }

    const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
    const TArray<FMeshBoneInfo>& BoneInfos = RefSkeleton.GetRawRefBoneInfo();
    const int32 NumBones = RefSkeleton.GetRawBoneNum();

    TArray<TSharedPtr<FJsonValue>> BonesJson;
    BonesJson.Reserve(NumBones);
    for (int32 i = 0; i < NumBones; ++i)
    {
        const EBoneTranslationRetargetingMode::Type Mode = Skeleton->GetBoneTranslationRetargetingMode(i);

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("name"), BoneInfos[i].Name.ToString());
        Entry->SetStringField(TEXT("retargeting_mode"), BoneTranslationRetargetingModeToString(Mode));
        BonesJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
    Result->SetNumberField(TEXT("bone_count"), NumBones);
    Result->SetArrayField(TEXT("bones"), BonesJson);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleListIKRigs(const TSharedPtr<FJsonObject>& Params)
{
    FString PathFilter;
    Params->TryGetStringField(TEXT("path_filter"), PathFilter);

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UIKRigDefinition::StaticClass()->GetClassPathName());
    if (PathFilter.Len() > 0)
    {
        Filter.PackagePaths.Add(FName(*PathFilter));
    }

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> AssetsJson;
    AssetsJson.Reserve(Assets.Num());
    for (const FAssetData& Asset : Assets)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
        Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
        AssetsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path_filter"), PathFilter);
    Result->SetNumberField(TEXT("total_count"), Assets.Num());
    Result->SetArrayField(TEXT("assets"), AssetsJson);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetIKRigInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UIKRigDefinition* Rig = LoadObject<UIKRigDefinition>(nullptr, *AssetPath);
    if (!Rig)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("IKRig asset not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Rig->GetPathName());
    Result->SetStringField(TEXT("asset_class"), Rig->GetClass()->GetName());
    Result->SetStringField(TEXT("preview_skeletal_mesh"), Rig->PreviewSkeletalMesh.ToString());
    Result->SetStringField(TEXT("pelvis_bone"), Rig->GetPelvis().ToString());

    // Chains: FBoneChain entries describe one retarget bone chain (ChainName, StartBone, EndBone, IKGoalName).
    const TArray<FBoneChain>& Chains = Rig->GetRetargetChains();
    TArray<TSharedPtr<FJsonValue>> ChainsJson;
    ChainsJson.Reserve(Chains.Num());
    for (int32 i = 0; i < Chains.Num(); ++i)
    {
        const FBoneChain& Chain = Chains[i];
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("chain_name"), Chain.ChainName.ToString());
        Entry->SetStringField(TEXT("start_bone"), Chain.StartBone.BoneName.ToString());
        Entry->SetStringField(TEXT("end_bone"), Chain.EndBone.BoneName.ToString());
        Entry->SetStringField(TEXT("goal_name"), Chain.IKGoalName.ToString());
        ChainsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetNumberField(TEXT("chain_count"), Chains.Num());
    Result->SetArrayField(TEXT("chains"), ChainsJson);

    // Goals: UIKRigEffectorGoal carries the goal name, attached bone, and editor transforms.
    const TArray<UIKRigEffectorGoal*>& Goals = Rig->GetGoalArray();
    TArray<TSharedPtr<FJsonValue>> GoalsJson;
    GoalsJson.Reserve(Goals.Num());
    for (int32 i = 0; i < Goals.Num(); ++i)
    {
        const UIKRigEffectorGoal* Goal = Goals[i];
        if (!Goal)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("goal_name"), Goal->GoalName.ToString());
        Entry->SetStringField(TEXT("bone_name"), Goal->BoneName.ToString());
        GoalsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetNumberField(TEXT("goal_count"), GoalsJson.Num());
    Result->SetArrayField(TEXT("goals"), GoalsJson);

    // Solvers: SolverStack is FInstancedStruct. Emit struct_name and the full UStruct as JSON
    // (all editable fields — Root Bone, iteration counts, per-bone preferred angles etc.).
    // This replaces the earlier summary-only output; intentionally verbose because the whole point
    // of this read is to compare solver configs across IKRigs.
    const TArray<FInstancedStruct>& Solvers = Rig->GetSolverStructs();
    TArray<TSharedPtr<FJsonValue>> SolversJson;
    SolversJson.Reserve(Solvers.Num());
    for (int32 i = 0; i < Solvers.Num(); ++i)
    {
        const UScriptStruct* SStruct = Solvers[i].GetScriptStruct();
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("struct_name"), SStruct ? SStruct->GetName() : FString());

        if (SStruct && Solvers[i].GetMemory())
        {
            TSharedRef<FJsonObject> FieldsJson = MakeShared<FJsonObject>();
            FJsonObjectConverter::UStructToJsonObject(
                SStruct,
                Solvers[i].GetMemory(),
                FieldsJson,
                /*CheckFlags=*/0,
                /*SkipFlags=*/0);
            Entry->SetObjectField(TEXT("fields"), FieldsJson);
        }

        SolversJson.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetNumberField(TEXT("solver_count"), Solvers.Num());
    Result->SetArrayField(TEXT("solvers"), SolversJson);

    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleListIKRetargeters(const TSharedPtr<FJsonObject>& Params)
{
    FString PathFilter;
    Params->TryGetStringField(TEXT("path_filter"), PathFilter);

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

    FARFilter Filter;
    Filter.bRecursiveClasses = true;
    Filter.bRecursivePaths = true;
    Filter.ClassPaths.Add(UIKRetargeter::StaticClass()->GetClassPathName());
    if (PathFilter.Len() > 0)
    {
        Filter.PackagePaths.Add(FName(*PathFilter));
    }

    TArray<FAssetData> Assets;
    AssetRegistry.GetAssets(Filter, Assets);

    TArray<TSharedPtr<FJsonValue>> AssetsJson;
    AssetsJson.Reserve(Assets.Num());
    for (const FAssetData& Asset : Assets)
    {
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("path"), Asset.GetObjectPathString());
        Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
        AssetsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("path_filter"), PathFilter);
    Result->SetNumberField(TEXT("total_count"), Assets.Num());
    Result->SetArrayField(TEXT("assets"), AssetsJson);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetIKRetargeterInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UIKRetargeter* Retargeter = LoadObject<UIKRetargeter>(nullptr, *AssetPath);
    if (!Retargeter)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("IKRetargeter asset not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Retargeter->GetPathName());
    Result->SetStringField(TEXT("asset_class"), Retargeter->GetClass()->GetName());

    // Source / target IK Rig assets — public getter walks both pointers so we don't
    // need to touch the private SourceIKRigAsset / TargetIKRigAsset fields directly.
    const UIKRigDefinition* SourceRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Source);
    const UIKRigDefinition* TargetRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Target);
    Result->SetStringField(TEXT("source_ik_rig_path"), SourceRig ? SourceRig->GetPathName() : FString());
    Result->SetStringField(TEXT("target_ik_rig_path"), TargetRig ? TargetRig->GetPathName() : FString());
    Result->SetBoolField(TEXT("has_source_ik_rig"), Retargeter->HasSourceIKRig());
    Result->SetBoolField(TEXT("has_target_ik_rig"), Retargeter->HasTargetIKRig());

    // Current retarget pose names (public API, no reflection needed).
    Result->SetStringField(TEXT("current_source_pose"), Retargeter->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Source).ToString());
    Result->SetStringField(TEXT("current_target_pose"), Retargeter->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target).ToString());

    // Source / target retarget pose lists. The TMaps are private with only a
    // UIKRetargeterController friend, so we read keys via reflection then look up
    // each pose with the public GetRetargetPoseByName getter to grab bone offset counts.
    auto EmitPoseList = [Retargeter](FName MapPropName, ERetargetSourceOrTarget Side) -> TArray<TSharedPtr<FJsonValue>>
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        const TArray<FName> PoseNames = ReadNameKeysFromMapProperty(Retargeter, MapPropName);
        Out.Reserve(PoseNames.Num());
        for (const FName& PoseName : PoseNames)
        {
            const FIKRetargetPose* Pose = Retargeter->GetRetargetPoseByName(Side, PoseName);
            const int32 OffsetCount = Pose ? Pose->GetAllDeltaRotations().Num() : 0;
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), PoseName.ToString());
            Entry->SetNumberField(TEXT("bone_offset_count"), OffsetCount);
            Out.Add(MakeShared<FJsonValueObject>(Entry));
        }
        return Out;
    };

    const TArray<TSharedPtr<FJsonValue>> SourcePosesJson = EmitPoseList(TEXT("SourceRetargetPoses"), ERetargetSourceOrTarget::Source);
    const TArray<TSharedPtr<FJsonValue>> TargetPosesJson = EmitPoseList(TEXT("TargetRetargetPoses"), ERetargetSourceOrTarget::Target);
    Result->SetNumberField(TEXT("source_pose_count"), SourcePosesJson.Num());
    Result->SetArrayField(TEXT("source_retarget_poses"), SourcePosesJson);
    Result->SetNumberField(TEXT("target_pose_count"), TargetPosesJson.Num());
    Result->SetArrayField(TEXT("target_retarget_poses"), TargetPosesJson);

    const TArray<FInstancedStruct>& Ops = Retargeter->GetRetargetOps();
    TArray<TSharedPtr<FJsonValue>> OpsJson;
    OpsJson.Reserve(Ops.Num());
    for (int32 i = 0; i < Ops.Num(); ++i)
    {
        const UScriptStruct* OStruct = Ops[i].GetScriptStruct();
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("struct_name"), OStruct ? OStruct->GetName() : FString());

        if (const FIKRetargetOpBase* OpPtr = Ops[i].GetPtr<FIKRetargetOpBase>())
        {
            Entry->SetBoolField(TEXT("enabled"), OpPtr->IsEnabled());

            if (const FRetargetChainMapping* Mapping = OpPtr->GetChainMapping())
            {
                const TArray<FRetargetChainPair>& Pairs = Mapping->GetChainPairs();
                TArray<TSharedPtr<FJsonValue>> PairsJson;
                PairsJson.Reserve(Pairs.Num());
                for (const FRetargetChainPair& Pair : Pairs)
                {
                    TSharedPtr<FJsonObject> PairObj = MakeShared<FJsonObject>();
                    PairObj->SetStringField(TEXT("source_chain"), Pair.SourceChainName.ToString());
                    PairObj->SetStringField(TEXT("target_chain"), Pair.TargetChainName.ToString());
                    PairsJson.Add(MakeShared<FJsonValueObject>(PairObj));
                }
                Entry->SetNumberField(TEXT("chain_pair_count"), Pairs.Num());
                Entry->SetArrayField(TEXT("chain_pairs"), PairsJson);
            }

            FIKRetargetOpBase* MutableOpPtr = const_cast<FIKRetargetOpBase*>(OpPtr);
            if (const FIKRetargetOpSettingsBase* Settings = MutableOpPtr->GetSettings())
            {
                const UScriptStruct* SettingsStruct = OpPtr->GetSettingsType();
                if (SettingsStruct)
                {
                    TSharedRef<FJsonObject> SettingsObj = MakeShared<FJsonObject>();
                    FJsonObjectConverter::UStructToJsonObject(
                        SettingsStruct, Settings, SettingsObj,
                        CPF_Edit, CPF_Transient | CPF_DuplicateTransient);
                    Entry->SetObjectField(TEXT("settings"), SettingsObj);
                }
            }
        }

        OpsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetNumberField(TEXT("retarget_op_count"), Ops.Num());
    Result->SetArrayField(TEXT("retarget_ops"), OpsJson);

    // Profile names (also a private TMap<FName, FRetargetProfile>, read via reflection).
    const TArray<FName> ProfileNames = ReadNameKeysFromMapProperty(Retargeter, TEXT("Profiles"));
    TArray<TSharedPtr<FJsonValue>> ProfilesJson;
    ProfilesJson.Reserve(ProfileNames.Num());
    for (const FName& Name : ProfileNames)
    {
        ProfilesJson.Add(MakeShared<FJsonValueString>(Name.ToString()));
    }
    Result->SetNumberField(TEXT("profile_count"), ProfileNames.Num());
    Result->SetArrayField(TEXT("profiles"), ProfilesJson);

    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetMontageCompositeInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AssetPath);
    if (!Montage)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("AnimMontage asset not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("asset_class"), Montage->GetClass()->GetName());
    Result->SetNumberField(TEXT("play_length"), Montage->GetPlayLength());
    Result->SetBoolField(TEXT("is_loop"), Montage->bLoop);
    Result->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
    Result->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());
    Result->SetNumberField(TEXT("blend_out_trigger_time"), Montage->BlendOutTriggerTime);
    Result->SetBoolField(TEXT("enable_auto_blend_out"), Montage->bEnableAutoBlendOut);

    // OR-aggregated across all underlying AnimSequences in SlotAnimTracks.
    // True if ANY segment's AnimSequence has bEnableRootMotion set — see
    // UAnimMontage::HasRootMotion in Engine/Source/Runtime/Engine/Private/Animation/AnimMontage.cpp.
    Result->SetBoolField(TEXT("has_root_motion"), Montage->HasRootMotion());

    if (const USkeleton* Skeleton = Montage->GetSkeleton())
    {
        Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
    }
    else
    {
        Result->SetStringField(TEXT("skeleton"), FString());
    }

    TArray<TSharedPtr<FJsonValue>> SectionsJson;
    SectionsJson.Reserve(Montage->CompositeSections.Num());
    for (int32 SectionIdx = 0; SectionIdx < Montage->CompositeSections.Num(); ++SectionIdx)
    {
        const FCompositeSection& Section = Montage->CompositeSections[SectionIdx];
        TSharedPtr<FJsonObject> SectionEntry = MakeShared<FJsonObject>();
        SectionEntry->SetNumberField(TEXT("section_index"), SectionIdx);
        SectionEntry->SetStringField(TEXT("section_name"), Section.SectionName.ToString());
        SectionEntry->SetNumberField(TEXT("start_time"), Section.GetTime());
        SectionEntry->SetNumberField(TEXT("segment_length"), Montage->GetSectionLength(SectionIdx));
        SectionEntry->SetStringField(TEXT("next_section_name"), Section.NextSectionName.ToString());
        SectionsJson.Add(MakeShared<FJsonValueObject>(SectionEntry));
    }
    Result->SetNumberField(TEXT("section_count"), Montage->CompositeSections.Num());
    Result->SetArrayField(TEXT("sections"), SectionsJson);

    TArray<TSharedPtr<FJsonValue>> SlotsJson;
    SlotsJson.Reserve(Montage->SlotAnimTracks.Num());
    for (int32 SlotIdx = 0; SlotIdx < Montage->SlotAnimTracks.Num(); ++SlotIdx)
    {
        const FSlotAnimationTrack& SlotTrack = Montage->SlotAnimTracks[SlotIdx];

        TArray<TSharedPtr<FJsonValue>> SegmentsJson;
        SegmentsJson.Reserve(SlotTrack.AnimTrack.AnimSegments.Num());
        for (int32 SegIdx = 0; SegIdx < SlotTrack.AnimTrack.AnimSegments.Num(); ++SegIdx)
        {
            const FAnimSegment& Segment = SlotTrack.AnimTrack.AnimSegments[SegIdx];
            TSharedPtr<FJsonObject> SegEntry = MakeShared<FJsonObject>();
            SegEntry->SetNumberField(TEXT("segment_index"), SegIdx);

            const UAnimSequenceBase* SegAnim = Segment.GetAnimReference();
            SegEntry->SetStringField(TEXT("anim_path"), SegAnim ? SegAnim->GetPathName() : FString());
            SegEntry->SetStringField(TEXT("anim_class"), SegAnim ? SegAnim->GetClass()->GetName() : FString());

            SegEntry->SetNumberField(TEXT("start_pos"), Segment.StartPos);
            SegEntry->SetNumberField(TEXT("end_pos"), Segment.GetEndPos());
            SegEntry->SetNumberField(TEXT("length"), Segment.GetLength());
            SegEntry->SetNumberField(TEXT("anim_start_time"), Segment.AnimStartTime);
            SegEntry->SetNumberField(TEXT("anim_end_time"), Segment.AnimEndTime);
            SegEntry->SetNumberField(TEXT("play_rate"), Segment.AnimPlayRate);
            SegEntry->SetNumberField(TEXT("loop_count"), Segment.LoopingCount);

            // Per-segment root motion. Only AnimSequence carries the active flag —
            // the same fields on AnimMontage are deprecated since 4.5.
            if (const UAnimSequence* SegSeq = Cast<UAnimSequence>(SegAnim))
            {
                SegEntry->SetBoolField(TEXT("has_root_motion"), SegSeq->HasRootMotion());
                AddRootMotionFieldsForSequence(SegEntry, SegSeq);
            }

            SegmentsJson.Add(MakeShared<FJsonValueObject>(SegEntry));
        }

        TSharedPtr<FJsonObject> SlotEntry = MakeShared<FJsonObject>();
        SlotEntry->SetNumberField(TEXT("slot_index"), SlotIdx);
        SlotEntry->SetStringField(TEXT("slot_name"), SlotTrack.SlotName.ToString());
        SlotEntry->SetNumberField(TEXT("segment_count"), SlotTrack.AnimTrack.AnimSegments.Num());
        SlotEntry->SetArrayField(TEXT("segments"), SegmentsJson);
        SlotsJson.Add(MakeShared<FJsonValueObject>(SlotEntry));
    }
    Result->SetNumberField(TEXT("slot_count"), Montage->SlotAnimTracks.Num());
    Result->SetArrayField(TEXT("slot_anim_tracks"), SlotsJson);

    TArray<TSharedPtr<FJsonValue>> NotifyTracksJson;
#if WITH_EDITORONLY_DATA
    NotifyTracksJson.Reserve(Montage->AnimNotifyTracks.Num());
    for (int32 TrackIdx = 0; TrackIdx < Montage->AnimNotifyTracks.Num(); ++TrackIdx)
    {
        const FAnimNotifyTrack& Track = Montage->AnimNotifyTracks[TrackIdx];
        TSharedPtr<FJsonObject> TrackEntry = MakeShared<FJsonObject>();
        TrackEntry->SetNumberField(TEXT("track_index"), TrackIdx);
        TrackEntry->SetStringField(TEXT("track_name"), Track.TrackName.ToString());
        NotifyTracksJson.Add(MakeShared<FJsonValueObject>(TrackEntry));
    }
#endif
    Result->SetNumberField(TEXT("notify_track_count"), NotifyTracksJson.Num());
    Result->SetArrayField(TEXT("notify_tracks"), NotifyTracksJson);

    return Result;
}

// ─────────────────────────────────────────────────────────────
// Enhanced Input helpers
// ─────────────────────────────────────────────────────────────
namespace
{
    FString InputActionValueTypeToString(EInputActionValueType V)
    {
        switch (V)
        {
        case EInputActionValueType::Boolean: return TEXT("Boolean");
        case EInputActionValueType::Axis1D:  return TEXT("Axis1D");
        case EInputActionValueType::Axis2D:  return TEXT("Axis2D");
        case EInputActionValueType::Axis3D:  return TEXT("Axis3D");
        default:                              return TEXT("Unknown");
        }
    }

    TSharedPtr<FJsonObject> SerializeTrigger(const UInputTrigger* Trigger)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        if (!Trigger) return Obj;

        Obj->SetStringField(TEXT("class"), Trigger->GetClass()->GetName());
        Obj->SetNumberField(TEXT("actuation_threshold"), Trigger->ActuationThreshold);

        // Timed triggers
        if (const UInputTriggerHold* Hold = Cast<UInputTriggerHold>(Trigger))
        {
            Obj->SetNumberField(TEXT("hold_time_threshold"), Hold->HoldTimeThreshold);
            Obj->SetBoolField(TEXT("is_one_shot"), Hold->bIsOneShot);
        }
        else if (const UInputTriggerHoldAndRelease* HAR = Cast<UInputTriggerHoldAndRelease>(Trigger))
        {
            Obj->SetNumberField(TEXT("hold_time_threshold"), HAR->HoldTimeThreshold);
        }
        else if (const UInputTriggerTap* Tap = Cast<UInputTriggerTap>(Trigger))
        {
            Obj->SetNumberField(TEXT("tap_release_time_threshold"), Tap->TapReleaseTimeThreshold);
        }
        else if (const UInputTriggerPulse* Pulse = Cast<UInputTriggerPulse>(Trigger))
        {
            Obj->SetBoolField(TEXT("trigger_on_start"), Pulse->bTriggerOnStart);
            Obj->SetNumberField(TEXT("interval"), Pulse->Interval);
            Obj->SetNumberField(TEXT("trigger_limit"), Pulse->TriggerLimit);
        }
        else if (const UInputTriggerChordAction* Chord = Cast<UInputTriggerChordAction>(Trigger))
        {
            if (Chord->ChordAction)
            {
                Obj->SetStringField(TEXT("chord_action"), Chord->ChordAction->GetPathName());
            }
        }

        return Obj;
    }

    TSharedPtr<FJsonObject> SerializeModifier(const UInputModifier* Modifier)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        if (!Modifier) return Obj;

        Obj->SetStringField(TEXT("class"), Modifier->GetClass()->GetName());

        if (const UInputModifierDeadZone* DZ = Cast<UInputModifierDeadZone>(Modifier))
        {
            Obj->SetNumberField(TEXT("lower_threshold"), DZ->LowerThreshold);
            Obj->SetNumberField(TEXT("upper_threshold"), DZ->UpperThreshold);
            Obj->SetStringField(TEXT("type"), DZ->Type == EDeadZoneType::Axial ? TEXT("Axial") :
                                              DZ->Type == EDeadZoneType::Radial ? TEXT("Radial") : TEXT("UnscaledRadial"));
        }
        else if (const UInputModifierScalar* Scalar = Cast<UInputModifierScalar>(Modifier))
        {
            Obj->SetStringField(TEXT("scalar"), FString::Printf(TEXT("(%g, %g, %g)"), Scalar->Scalar.X, Scalar->Scalar.Y, Scalar->Scalar.Z));
        }
        else if (const UInputModifierNegate* Neg = Cast<UInputModifierNegate>(Modifier))
        {
            Obj->SetBoolField(TEXT("x"), Neg->bX);
            Obj->SetBoolField(TEXT("y"), Neg->bY);
            Obj->SetBoolField(TEXT("z"), Neg->bZ);
        }
        else if (const UInputModifierSwizzleAxis* Swizzle = Cast<UInputModifierSwizzleAxis>(Modifier))
        {
            switch (Swizzle->Order)
            {
            case EInputAxisSwizzle::YXZ: Obj->SetStringField(TEXT("order"), TEXT("YXZ")); break;
            case EInputAxisSwizzle::ZYX: Obj->SetStringField(TEXT("order"), TEXT("ZYX")); break;
            case EInputAxisSwizzle::XZY: Obj->SetStringField(TEXT("order"), TEXT("XZY")); break;
            case EInputAxisSwizzle::YZX: Obj->SetStringField(TEXT("order"), TEXT("YZX")); break;
            case EInputAxisSwizzle::ZXY: Obj->SetStringField(TEXT("order"), TEXT("ZXY")); break;
            default: Obj->SetStringField(TEXT("order"), TEXT("Unknown")); break;
            }
        }
        else if (const UInputModifierResponseCurveExponential* Exp = Cast<UInputModifierResponseCurveExponential>(Modifier))
        {
            Obj->SetStringField(TEXT("curve_exponent"), FString::Printf(TEXT("(%g, %g, %g)"), Exp->CurveExponent.X, Exp->CurveExponent.Y, Exp->CurveExponent.Z));
        }
        else if (const UInputModifierFOVScaling* FOV = Cast<UInputModifierFOVScaling>(Modifier))
        {
            Obj->SetNumberField(TEXT("fov_scale"), FOV->FOVScale);
        }

        return Obj;
    }

    TArray<TSharedPtr<FJsonValue>> SerializeTriggerArray(const TArray<TObjectPtr<UInputTrigger>>& Triggers)
    {
        TArray<TSharedPtr<FJsonValue>> Arr;
        for (const auto& T : Triggers)
        {
            if (T) Arr.Add(MakeShared<FJsonValueObject>(SerializeTrigger(T)));
        }
        return Arr;
    }

    TArray<TSharedPtr<FJsonValue>> SerializeModifierArray(const TArray<TObjectPtr<UInputModifier>>& Modifiers)
    {
        TArray<TSharedPtr<FJsonValue>> Arr;
        for (const auto& M : Modifiers)
        {
            if (M) Arr.Add(MakeShared<FJsonValueObject>(SerializeModifier(M)));
        }
        return Arr;
    }
}

// ─────────────────────────────────────────────────────────────
// get_input_action_info
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetInputActionInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UInputAction* IA = LoadObject<UInputAction>(nullptr, *AssetPath);
    if (!IA)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("InputAction not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), IA->GetPathName());
    Result->SetStringField(TEXT("value_type"), InputActionValueTypeToString(IA->ValueType));
    Result->SetStringField(TEXT("description"), IA->ActionDescription.ToString());
    Result->SetBoolField(TEXT("consume_input"), IA->bConsumeInput);
    Result->SetBoolField(TEXT("trigger_when_paused"), IA->bTriggerWhenPaused);
    Result->SetBoolField(TEXT("reserve_all_mappings"), IA->bReserveAllMappings);

    // Triggers
    TArray<TSharedPtr<FJsonValue>> TriggersJson = SerializeTriggerArray(IA->Triggers);
    Result->SetNumberField(TEXT("trigger_count"), TriggersJson.Num());
    Result->SetArrayField(TEXT("triggers"), TriggersJson);

    // Modifiers
    TArray<TSharedPtr<FJsonValue>> ModifiersJson = SerializeModifierArray(IA->Modifiers);
    Result->SetNumberField(TEXT("modifier_count"), ModifiersJson.Num());
    Result->SetArrayField(TEXT("modifiers"), ModifiersJson);

    return Result;
}

// ─────────────────────────────────────────────────────────────
// get_input_mapping_context_info
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetInputMappingContextInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *AssetPath);
    if (!IMC)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("InputMappingContext not found: %s"), *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), IMC->GetPathName());
    Result->SetStringField(TEXT("description"), IMC->ContextDescription.ToString());

    // Get mappings via public API
    const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();

    TArray<TSharedPtr<FJsonValue>> MappingsJson;
    for (const FEnhancedActionKeyMapping& Mapping : Mappings)
    {
        TSharedPtr<FJsonObject> MappingObj = MakeShared<FJsonObject>();

        // Action
        if (Mapping.Action)
        {
            MappingObj->SetStringField(TEXT("action"), Mapping.Action->GetPathName());
            MappingObj->SetStringField(TEXT("action_name"), Mapping.Action->GetName());
            MappingObj->SetStringField(TEXT("value_type"), InputActionValueTypeToString(Mapping.Action->ValueType));
        }
        else
        {
            MappingObj->SetStringField(TEXT("action"), TEXT("None"));
        }

        // Key
        MappingObj->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());

        // Triggers
        TArray<TSharedPtr<FJsonValue>> TriggersJson = SerializeTriggerArray(Mapping.Triggers);
        MappingObj->SetNumberField(TEXT("trigger_count"), TriggersJson.Num());
        MappingObj->SetArrayField(TEXT("triggers"), TriggersJson);

        // Modifiers
        TArray<TSharedPtr<FJsonValue>> ModifiersJson = SerializeModifierArray(Mapping.Modifiers);
        MappingObj->SetNumberField(TEXT("modifier_count"), ModifiersJson.Num());
        MappingObj->SetArrayField(TEXT("modifiers"), ModifiersJson);

        MappingsJson.Add(MakeShared<FJsonValueObject>(MappingObj));
    }

    Result->SetNumberField(TEXT("mapping_count"), MappingsJson.Num());
    Result->SetArrayField(TEXT("mappings"), MappingsJson);

    return Result;
}

// ── Pose Search helpers ──────────────────────────────────────────────

namespace
{
    TSharedPtr<FJsonObject> SerializeChannelRecursive(const UPoseSearchFeatureChannel* Channel)
    {
        if (!Channel) return nullptr;

        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

        // Class name (e.g. "PoseSearchFeatureChannel_Trajectory")
        Obj->SetStringField(TEXT("channel_class"), Channel->GetClass()->GetName());


        // Use UE reflection to read all EditAnywhere UPROPERTY fields
        // and serialize the most useful ones by known property names.
        const UClass* Class = Channel->GetClass();

        // Weight (most channel subclasses have this, WITH_EDITORONLY_DATA)
#if WITH_EDITORONLY_DATA
        if (const FProperty* WeightProp = Class->FindPropertyByName(TEXT("Weight")))
        {
            if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(WeightProp))
            {
                Obj->SetNumberField(TEXT("weight"), FloatProp->GetPropertyValue_InContainer(Channel));
            }
        }
#endif

        // Bone (FBoneReference — has BoneName)
        if (const FProperty* BoneProp = Class->FindPropertyByName(TEXT("Bone")))
        {
            if (const FStructProperty* StructProp = CastField<FStructProperty>(BoneProp))
            {
                const void* StructAddr = StructProp->ContainerPtrToValuePtr<void>(Channel);
                if (const FProperty* BoneNameProp = StructProp->Struct->FindPropertyByName(TEXT("BoneName")))
                {
                    if (const FNameProperty* NameProp = CastField<FNameProperty>(BoneNameProp))
                    {
                        FName BoneName = NameProp->GetPropertyValue_InContainer(StructAddr);
                        Obj->SetStringField(TEXT("bone_name"), BoneName.ToString());
                    }
                }
            }
        }

        // SampleRole
        if (const FProperty* RoleProp = Class->FindPropertyByName(TEXT("SampleRole")))
        {
            if (const FNameProperty* NameProp = CastField<FNameProperty>(RoleProp))
            {
                Obj->SetStringField(TEXT("sample_role"), NameProp->GetPropertyValue_InContainer(Channel).ToString());
            }
        }

        // SampleTimeOffset
        if (const FProperty* Prop = Class->FindPropertyByName(TEXT("SampleTimeOffset")))
        {
            if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
            {
                Obj->SetNumberField(TEXT("sample_time_offset"), FloatProp->GetPropertyValue_InContainer(Channel));
            }
        }

        // InputQueryPose enum
        if (const FProperty* Prop = Class->FindPropertyByName(TEXT("InputQueryPose")))
        {
            if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
            {
                Obj->SetNumberField(TEXT("input_query_pose"), (int32)ByteProp->GetPropertyValue_InContainer(Channel));
            }
            else if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
            {
                const UEnum* EnumDef = EnumProp->GetEnum();
                const FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
                int64 Val = UnderlyingProp->GetSignedIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(Channel));
                Obj->SetStringField(TEXT("input_query_pose"), EnumDef ? EnumDef->GetNameStringByValue(Val) : FString::FromInt(Val));
            }
        }

        // ComponentStripping enum
        if (const FProperty* Prop = Class->FindPropertyByName(TEXT("ComponentStripping")))
        {
            if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
            {
                const UEnum* EnumDef = EnumProp->GetEnum();
                const FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
                int64 Val = UnderlyingProp->GetSignedIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(Channel));
                Obj->SetStringField(TEXT("component_stripping"), EnumDef ? EnumDef->GetNameStringByValue(Val) : FString::FromInt(Val));
            }
        }

        // bUseCharacterSpaceVelocities
        if (const FProperty* Prop = Class->FindPropertyByName(TEXT("bUseCharacterSpaceVelocities")))
        {
            if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
            {
                Obj->SetBoolField(TEXT("use_character_space_velocities"), BoolProp->GetPropertyValue_InContainer(Channel));
            }
        }

        // ── Trajectory channel: Samples array ──
        if (const FProperty* SamplesProp = Class->FindPropertyByName(TEXT("Samples")))
        {
            if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(SamplesProp))
            {
                FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Channel));
                TArray<TSharedPtr<FJsonValue>> SamplesJson;
                const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner);

                for (int32 i = 0; i < ArrayHelper.Num(); ++i)
                {
                    TSharedPtr<FJsonObject> SampleObj = MakeShared<FJsonObject>();
                    const void* ElemPtr = ArrayHelper.GetRawPtr(i);

                    if (InnerStruct && InnerStruct->Struct)
                    {
                        if (const FFloatProperty* OffsetProp = CastField<FFloatProperty>(InnerStruct->Struct->FindPropertyByName(TEXT("Offset"))))
                        {
                            SampleObj->SetNumberField(TEXT("offset"), OffsetProp->GetPropertyValue(OffsetProp->ContainerPtrToValuePtr<void>(ElemPtr)));
                        }
                        if (const FIntProperty* FlagsProp = CastField<FIntProperty>(InnerStruct->Struct->FindPropertyByName(TEXT("Flags"))))
                        {
                            SampleObj->SetNumberField(TEXT("flags"), FlagsProp->GetPropertyValue(FlagsProp->ContainerPtrToValuePtr<void>(ElemPtr)));
                        }
#if WITH_EDITORONLY_DATA
                        if (const FFloatProperty* WProp = CastField<FFloatProperty>(InnerStruct->Struct->FindPropertyByName(TEXT("Weight"))))
                        {
                            SampleObj->SetNumberField(TEXT("weight"), WProp->GetPropertyValue(WProp->ContainerPtrToValuePtr<void>(ElemPtr)));
                        }
#endif
                    }
                    SamplesJson.Add(MakeShared<FJsonValueObject>(SampleObj));
                }
                Obj->SetNumberField(TEXT("sample_count"), SamplesJson.Num());
                Obj->SetArrayField(TEXT("samples"), SamplesJson);
            }
        }

        // ── Pose channel: SampledBones array ──
        if (const FProperty* BonesProp = Class->FindPropertyByName(TEXT("SampledBones")))
        {
            if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(BonesProp))
            {
                FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Channel));
                TArray<TSharedPtr<FJsonValue>> BonesJson;
                const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner);

                for (int32 i = 0; i < ArrayHelper.Num(); ++i)
                {
                    TSharedPtr<FJsonObject> BoneObj = MakeShared<FJsonObject>();
                    const void* ElemPtr = ArrayHelper.GetRawPtr(i);

                    if (InnerStruct && InnerStruct->Struct)
                    {
                        // FBoneReference.BoneName
                        if (const FStructProperty* RefProp = CastField<FStructProperty>(InnerStruct->Struct->FindPropertyByName(TEXT("Reference"))))
                        {
                            const void* RefAddr = RefProp->ContainerPtrToValuePtr<void>(ElemPtr);
                            if (const FNameProperty* BNProp = CastField<FNameProperty>(RefProp->Struct->FindPropertyByName(TEXT("BoneName"))))
                            {
                                BoneObj->SetStringField(TEXT("bone_name"), BNProp->GetPropertyValue_InContainer(RefAddr).ToString());
                            }
                        }
                        if (const FIntProperty* FlagsProp = CastField<FIntProperty>(InnerStruct->Struct->FindPropertyByName(TEXT("Flags"))))
                        {
                            BoneObj->SetNumberField(TEXT("flags"), FlagsProp->GetPropertyValue(FlagsProp->ContainerPtrToValuePtr<void>(ElemPtr)));
                        }
#if WITH_EDITORONLY_DATA
                        if (const FFloatProperty* WProp = CastField<FFloatProperty>(InnerStruct->Struct->FindPropertyByName(TEXT("Weight"))))
                        {
                            BoneObj->SetNumberField(TEXT("weight"), WProp->GetPropertyValue(WProp->ContainerPtrToValuePtr<void>(ElemPtr)));
                        }
#endif
                    }
                    BonesJson.Add(MakeShared<FJsonValueObject>(BoneObj));
                }
                Obj->SetNumberField(TEXT("sampled_bone_count"), BonesJson.Num());
                Obj->SetArrayField(TEXT("sampled_bones"), BonesJson);
            }
        }

        // ── Curve channel: CurveName ──
        if (const FProperty* Prop = Class->FindPropertyByName(TEXT("CurveName")))
        {
            if (const FNameProperty* NameProp = CastField<FNameProperty>(Prop))
            {
                Obj->SetStringField(TEXT("curve_name"), NameProp->GetPropertyValue_InContainer(Channel).ToString());
            }
        }

        // ── Sub-channels (Group/Trajectory/Pose channels) ──
        TConstArrayView<TObjectPtr<UPoseSearchFeatureChannel>> SubChannels = Channel->GetSubChannels();
        if (SubChannels.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> SubJson;
            for (const TObjectPtr<UPoseSearchFeatureChannel>& Sub : SubChannels)
            {
                if (TSharedPtr<FJsonObject> SubObj = SerializeChannelRecursive(Sub.Get()))
                {
                    SubJson.Add(MakeShared<FJsonValueObject>(SubObj));
                }
            }
            Obj->SetNumberField(TEXT("sub_channel_count"), SubJson.Num());
            Obj->SetArrayField(TEXT("sub_channels"), SubJson);
        }

        return Obj;
    }
}

// ── get_pose_search_database_info ──────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetPoseSearchDatabaseInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *AssetPath);
    if (!Database)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchDatabase asset not found"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("asset_class"), TEXT("PoseSearchDatabase"));

    // Schema reference
    const UPoseSearchSchema* Schema = Database->Schema;
    Result->SetStringField(TEXT("schema"), Schema ? Schema->GetPathName() : TEXT("None"));

    // Tags
    TArray<TSharedPtr<FJsonValue>> TagsJson;
    for (const FName& Tag : Database->Tags)
    {
        TagsJson.Add(MakeShared<FJsonValueString>(Tag.ToString()));
    }
    Result->SetNumberField(TEXT("tag_count"), TagsJson.Num());
    Result->SetArrayField(TEXT("tags"), TagsJson);

    // Cost biases
    Result->SetNumberField(TEXT("continuing_pose_cost_bias"), Database->ContinuingPoseCostBias);
    Result->SetNumberField(TEXT("base_cost_bias"), Database->BaseCostBias);
    Result->SetNumberField(TEXT("looping_cost_bias"), Database->LoopingCostBias);

    // Search mode
    FString ModeStr;
    switch (Database->PoseSearchMode)
    {
    case EPoseSearchMode::BruteForce: ModeStr = TEXT("BruteForce"); break;
    case EPoseSearchMode::PCAKDTree:  ModeStr = TEXT("PCAKDTree"); break;
    case EPoseSearchMode::VPTree:     ModeStr = TEXT("VPTree"); break;
    default:                           ModeStr = TEXT("Unknown"); break;
    }
    Result->SetStringField(TEXT("pose_search_mode"), ModeStr);

    // Animation assets — access via reflection (DatabaseAnimationAssets is private UPROPERTY)
    const FProperty* AssetsProp = Database->GetClass()->FindPropertyByName(TEXT("DatabaseAnimationAssets"));
    const FArrayProperty* AssetsArrayProp = AssetsProp ? CastField<FArrayProperty>(AssetsProp) : nullptr;

    TArray<TSharedPtr<FJsonValue>> AssetsJson;

    if (AssetsArrayProp)
    {
        FScriptArrayHelper ArrayHelper(AssetsArrayProp, AssetsArrayProp->ContainerPtrToValuePtr<void>(Database));
        const FStructProperty* InnerStruct = CastField<FStructProperty>(AssetsArrayProp->Inner);

        for (int32 i = 0; i < ArrayHelper.Num(); ++i)
        {
            TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
            const void* ElemPtr = ArrayHelper.GetRawPtr(i);
            AssetObj->SetNumberField(TEXT("index"), i);

            if (InnerStruct && InnerStruct->Struct)
            {
                const UStruct* S = InnerStruct->Struct;

                // AnimAsset (TObjectPtr<UObject>)
                if (const FObjectProperty* AnimProp = CastField<FObjectProperty>(S->FindPropertyByName(TEXT("AnimAsset"))))
                {
                    UObject* AnimObj = AnimProp->GetObjectPropertyValue(AnimProp->ContainerPtrToValuePtr<void>(ElemPtr));
                    if (AnimObj)
                    {
                        AssetObj->SetStringField(TEXT("anim_asset_path"), AnimObj->GetPathName());
                        AssetObj->SetStringField(TEXT("anim_asset_class"), AnimObj->GetClass()->GetName());
                    }
                    else
                    {
                        AssetObj->SetStringField(TEXT("anim_asset_path"), TEXT("None"));
                    }
                }

#if WITH_EDITORONLY_DATA
                // bEnabled
                if (const FBoolProperty* Prop = CastField<FBoolProperty>(S->FindPropertyByName(TEXT("bEnabled"))))
                {
                    AssetObj->SetBoolField(TEXT("enabled"), Prop->GetPropertyValue_InContainer(ElemPtr));
                }

                // bDisableReselection
                if (const FBoolProperty* Prop = CastField<FBoolProperty>(S->FindPropertyByName(TEXT("bDisableReselection"))))
                {
                    AssetObj->SetBoolField(TEXT("disable_reselection"), Prop->GetPropertyValue_InContainer(ElemPtr));
                }

                // MirrorOption
                if (const FProperty* Prop = S->FindPropertyByName(TEXT("MirrorOption")))
                {
                    if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
                    {
                        const UEnum* EnumDef = EnumProp->GetEnum();
                        const FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
                        int64 Val = UnderlyingProp->GetSignedIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(ElemPtr));
                        AssetObj->SetStringField(TEXT("mirror_option"), EnumDef ? EnumDef->GetNameStringByValue(Val) : FString::FromInt(Val));
                    }
                }

                // SamplingRange
                if (const FStructProperty* Prop = CastField<FStructProperty>(S->FindPropertyByName(TEXT("SamplingRange"))))
                {
                    const FFloatInterval* Interval = Prop->ContainerPtrToValuePtr<FFloatInterval>(ElemPtr);
                    if (Interval)
                    {
                        AssetObj->SetNumberField(TEXT("sampling_range_min"), Interval->Min);
                        AssetObj->SetNumberField(TEXT("sampling_range_max"), Interval->Max);
                    }
                }

                // bUseSingleSample
                if (const FBoolProperty* Prop = CastField<FBoolProperty>(S->FindPropertyByName(TEXT("bUseSingleSample"))))
                {
                    AssetObj->SetBoolField(TEXT("use_single_sample"), Prop->GetPropertyValue_InContainer(ElemPtr));
                }
#endif
            }

            AssetsJson.Add(MakeShared<FJsonValueObject>(AssetObj));
        }
    }

    Result->SetNumberField(TEXT("animation_asset_count"), AssetsJson.Num());
    Result->SetArrayField(TEXT("animation_assets"), AssetsJson);

    return Result;
}

// ── get_pose_search_schema_info ────────────────────────────────────

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleGetPoseSearchSchemaInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *AssetPath);
    if (!Schema)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchSchema asset not found"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("asset_class"), TEXT("PoseSearchSchema"));

    // SampleRate
    Result->SetNumberField(TEXT("sample_rate"), Schema->SampleRate);

    // SchemaCardinality
    Result->SetNumberField(TEXT("schema_cardinality"), Schema->SchemaCardinality);

#if WITH_EDITORONLY_DATA
    // DataPreprocessor
    FString PreprocessorStr;
    switch (Schema->DataPreprocessor)
    {
    case EPoseSearchDataPreprocessor::None:                        PreprocessorStr = TEXT("None"); break;
    case EPoseSearchDataPreprocessor::Normalize:                   PreprocessorStr = TEXT("Normalize"); break;
    case EPoseSearchDataPreprocessor::NormalizeOnlyByDeviation:    PreprocessorStr = TEXT("NormalizeOnlyByDeviation"); break;
    case EPoseSearchDataPreprocessor::NormalizeWithCommonSchema:   PreprocessorStr = TEXT("NormalizeWithCommonSchema"); break;
    default:                                                       PreprocessorStr = TEXT("Unknown"); break;
    }
    Result->SetStringField(TEXT("data_preprocessor"), PreprocessorStr);
#endif

    // Skeletons
    const TArray<FPoseSearchRoledSkeleton>& Skeletons = Schema->GetRoledSkeletons();
    TArray<TSharedPtr<FJsonValue>> SkeletonsJson;
    for (const FPoseSearchRoledSkeleton& RS : Skeletons)
    {
        TSharedPtr<FJsonObject> SkObj = MakeShared<FJsonObject>();
        SkObj->SetStringField(TEXT("skeleton"), RS.Skeleton ? RS.Skeleton->GetPathName() : TEXT("None"));
        SkObj->SetStringField(TEXT("role"), RS.Role.ToString());
        if (const FObjectProperty* MirrorProp = CastField<FObjectProperty>(FPoseSearchRoledSkeleton::StaticStruct()->FindPropertyByName(TEXT("MirrorDataTable"))))
        {
            const UObject* MirrorObj = MirrorProp->GetObjectPropertyValue_InContainer(&RS);
            SkObj->SetStringField(TEXT("mirror_data_table"), MirrorObj ? MirrorObj->GetPathName() : TEXT("None"));
        }
        SkeletonsJson.Add(MakeShared<FJsonValueObject>(SkObj));
    }
    Result->SetNumberField(TEXT("skeleton_count"), SkeletonsJson.Num());
    Result->SetArrayField(TEXT("skeletons"), SkeletonsJson);

    // Channels — access via reflection (Channels is private UPROPERTY)
    // We read the user-authored Channels array, not FinalizedChannels (which may be empty if not finalized)
    const FProperty* ChannelsProp = Schema->GetClass()->FindPropertyByName(TEXT("Channels"));
    const FArrayProperty* ChannelsArrayProp = ChannelsProp ? CastField<FArrayProperty>(ChannelsProp) : nullptr;

    TArray<TSharedPtr<FJsonValue>> ChannelsJson;

    if (ChannelsArrayProp)
    {
        FScriptArrayHelper ArrayHelper(ChannelsArrayProp, ChannelsArrayProp->ContainerPtrToValuePtr<void>(Schema));

        for (int32 i = 0; i < ArrayHelper.Num(); ++i)
        {
            const void* ElemPtr = ArrayHelper.GetRawPtr(i);
            // TObjectPtr<UPoseSearchFeatureChannel> — dereference via FObjectProperty
            const FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ChannelsArrayProp->Inner);
            UObject* ChannelObj = InnerObjProp ? InnerObjProp->GetObjectPropertyValue(ElemPtr) : nullptr;
            UPoseSearchFeatureChannel* Channel = Cast<UPoseSearchFeatureChannel>(ChannelObj);

            if (TSharedPtr<FJsonObject> ChObj = SerializeChannelRecursive(Channel))
            {
                ChannelsJson.Add(MakeShared<FJsonValueObject>(ChObj));
            }
        }
    }

    Result->SetNumberField(TEXT("channel_count"), ChannelsJson.Num());
    Result->SetArrayField(TEXT("channels"), ChannelsJson);

    return Result;
}

// ══════════════════════════════════════════════════════════════════════
// PSD Write Tools
// ══════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetPoseSearchDatabaseSchema(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, SchemaPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
    if (!Params->TryGetStringField(TEXT("schema_path"), SchemaPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'schema_path'"));

    UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *AssetPath);
    if (!Database)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchDatabase not found"));

    UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
    if (!Schema)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchSchema not found"));

    Database->Modify();
    FObjectProperty* SchemaProp = CastField<FObjectProperty>(Database->GetClass()->FindPropertyByName(TEXT("Schema")));
    if (SchemaProp)
    {
        SchemaProp->SetObjectPropertyValue_InContainer(Database, Schema);
    }
    Database->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("schema"), Schema->GetPathName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleAddPoseSearchDatabaseAnimation(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, AnimAssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
    if (!Params->TryGetStringField(TEXT("anim_asset_path"), AnimAssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'anim_asset_path'"));

    UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *AssetPath);
    if (!Database)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchDatabase not found"));

    UObject* AnimAsset = LoadObject<UObject>(nullptr, *AnimAssetPath);
    if (!AnimAsset)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Animation asset not found: %s"), *AnimAssetPath));

    Database->Modify();

    // Access DatabaseAnimationAssets via reflection
    FArrayProperty* ArrayProp = CastField<FArrayProperty>(Database->GetClass()->FindPropertyByName(TEXT("DatabaseAnimationAssets")));
    if (!ArrayProp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("DatabaseAnimationAssets property not found"));

    FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Database));
    int32 NewIndex = ArrayHelper.AddValue();
    void* ElemPtr = ArrayHelper.GetRawPtr(NewIndex);

    const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner);
    if (InnerStruct && InnerStruct->Struct)
    {
        const UStruct* S = InnerStruct->Struct;

        // Set AnimAsset
        if (FObjectProperty* AnimProp = CastField<FObjectProperty>(S->FindPropertyByName(TEXT("AnimAsset"))))
        {
            AnimProp->SetObjectPropertyValue(AnimProp->ContainerPtrToValuePtr<void>(ElemPtr), AnimAsset);
        }

#if WITH_EDITORONLY_DATA
        // Set bEnabled (default true)
        bool bEnabled = true;
        Params->TryGetBoolField(TEXT("enabled"), bEnabled);
        if (FBoolProperty* Prop = CastField<FBoolProperty>(S->FindPropertyByName(TEXT("bEnabled"))))
        {
            Prop->SetPropertyValue_InContainer(ElemPtr, bEnabled);
        }

        // Set SamplingRange
        double RangeMin = 0.0, RangeMax = 0.0;
        if (Params->TryGetNumberField(TEXT("sampling_range_min"), RangeMin) || Params->TryGetNumberField(TEXT("sampling_range_max"), RangeMax))
        {
            if (FStructProperty* RangeProp = CastField<FStructProperty>(S->FindPropertyByName(TEXT("SamplingRange"))))
            {
                FFloatInterval* Interval = RangeProp->ContainerPtrToValuePtr<FFloatInterval>(ElemPtr);
                if (Interval)
                {
                    Interval->Min = (float)RangeMin;
                    Interval->Max = (float)RangeMax;
                }
            }
        }
#endif
    }

    Database->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("new_index"), NewIndex);
    Result->SetStringField(TEXT("anim_asset_path"), AnimAsset->GetPathName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleRemovePoseSearchDatabaseAnimation(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    double IndexD = -1;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
    if (!Params->TryGetNumberField(TEXT("index"), IndexD))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'index'"));

    int32 Index = (int32)IndexD;

    UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *AssetPath);
    if (!Database)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchDatabase not found"));

    FArrayProperty* ArrayProp = CastField<FArrayProperty>(Database->GetClass()->FindPropertyByName(TEXT("DatabaseAnimationAssets")));
    if (!ArrayProp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("DatabaseAnimationAssets property not found"));

    FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Database));
    if (Index < 0 || Index >= ArrayHelper.Num())
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Index %d out of range (0..%d)"), Index, ArrayHelper.Num() - 1));

    Database->Modify();
    ArrayHelper.RemoveValues(Index, 1);
    Database->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("removed_index"), Index);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetPoseSearchDatabaseCostBiases(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));

    UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *AssetPath);
    if (!Database)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchDatabase not found"));

    Database->Modify();

    TArray<FString> ModifiedFields;
    auto TrySetFloat = [&](const TCHAR* JsonKey, const TCHAR* PropName)
    {
        double Value = 0.0;
        if (Params->TryGetNumberField(JsonKey, Value))
        {
            if (FFloatProperty* Prop = CastField<FFloatProperty>(Database->GetClass()->FindPropertyByName(PropName)))
            {
                Prop->SetPropertyValue_InContainer(Database, (float)Value);
                ModifiedFields.Add(FString(PropName));
            }
        }
    };

    TrySetFloat(TEXT("continuing_pose_cost_bias"), TEXT("ContinuingPoseCostBias"));
    TrySetFloat(TEXT("base_cost_bias"), TEXT("BaseCostBias"));
    TrySetFloat(TEXT("looping_cost_bias"), TEXT("LoopingCostBias"));

    if (ModifiedFields.Num() > 0)
    {
        Database->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("modified_count"), ModifiedFields.Num());

    TArray<TSharedPtr<FJsonValue>> FieldsArray;
    for (const FString& F : ModifiedFields)
    {
        FieldsArray.Add(MakeShared<FJsonValueString>(F));
    }
    Result->SetArrayField(TEXT("modified_fields"), FieldsArray);

    // Read back current values
    auto GetFloat = [&](const TCHAR* PropName) -> float
    {
        if (FFloatProperty* Prop = CastField<FFloatProperty>(Database->GetClass()->FindPropertyByName(PropName)))
        {
            return Prop->GetPropertyValue_InContainer(Database);
        }
        return 0.f;
    };
    Result->SetNumberField(TEXT("continuing_pose_cost_bias"), GetFloat(TEXT("ContinuingPoseCostBias")));
    Result->SetNumberField(TEXT("base_cost_bias"), GetFloat(TEXT("BaseCostBias")));
    Result->SetNumberField(TEXT("looping_cost_bias"), GetFloat(TEXT("LoopingCostBias")));

    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetPoseSearchDatabaseAnimationFlags(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    double IndexD = -1;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
    if (!Params->TryGetNumberField(TEXT("index"), IndexD))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'index'"));

    int32 Index = (int32)IndexD;

    UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *AssetPath);
    if (!Database)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchDatabase not found"));

    FArrayProperty* ArrayProp = CastField<FArrayProperty>(Database->GetClass()->FindPropertyByName(TEXT("DatabaseAnimationAssets")));
    if (!ArrayProp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("DatabaseAnimationAssets property not found"));

    FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Database));
    if (Index < 0 || Index >= ArrayHelper.Num())
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Index %d out of range (0..%d)"), Index, ArrayHelper.Num() - 1));

    void* ElemPtr = ArrayHelper.GetRawPtr(Index);
    const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner);
    if (!InnerStruct || !InnerStruct->Struct)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid DatabaseAnimationAssets element struct"));

    const UStruct* S = InnerStruct->Struct;
    Database->Modify();

    TArray<FString> ModifiedFields;

#if WITH_EDITORONLY_DATA
    // bEnabled
    bool bEnabled = false;
    if (Params->TryGetBoolField(TEXT("enabled"), bEnabled))
    {
        if (FBoolProperty* Prop = CastField<FBoolProperty>(S->FindPropertyByName(TEXT("bEnabled"))))
        {
            Prop->SetPropertyValue_InContainer(ElemPtr, bEnabled);
            ModifiedFields.Add(TEXT("bEnabled"));
        }
    }

    // bDisableReselection
    bool bDisableReselection = false;
    if (Params->TryGetBoolField(TEXT("disable_reselection"), bDisableReselection))
    {
        if (FBoolProperty* Prop = CastField<FBoolProperty>(S->FindPropertyByName(TEXT("bDisableReselection"))))
        {
            Prop->SetPropertyValue_InContainer(ElemPtr, bDisableReselection);
            ModifiedFields.Add(TEXT("bDisableReselection"));
        }
    }

    // MirrorOption (enum, string)
    FString MirrorOptionStr;
    if (Params->TryGetStringField(TEXT("mirror_option"), MirrorOptionStr))
    {
        if (FEnumProperty* EnumProp = CastField<FEnumProperty>(S->FindPropertyByName(TEXT("MirrorOption"))))
        {
            UEnum* EnumType = EnumProp->GetEnum();
            if (EnumType)
            {
                int64 EnumValue = EnumType->GetValueByNameString(MirrorOptionStr);
                if (EnumValue == INDEX_NONE)
                {
                    // Try with short name prefix
                    FString FullName = FString::Printf(TEXT("EPoseSearchMirrorOption::%s"), *MirrorOptionStr);
                    EnumValue = EnumType->GetValueByNameString(FullName);
                }
                if (EnumValue != INDEX_NONE)
                {
                    void* EnumPtr = EnumProp->ContainerPtrToValuePtr<void>(ElemPtr);
                    EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(EnumPtr, EnumValue);
                    ModifiedFields.Add(TEXT("MirrorOption"));
                }
            }
        }
    }

    // SamplingRange
    double RangeMin = 0.0, RangeMax = 0.0;
    bool bHasRangeMin = Params->TryGetNumberField(TEXT("sampling_range_min"), RangeMin);
    bool bHasRangeMax = Params->TryGetNumberField(TEXT("sampling_range_max"), RangeMax);
    if (bHasRangeMin || bHasRangeMax)
    {
        if (FStructProperty* RangeProp = CastField<FStructProperty>(S->FindPropertyByName(TEXT("SamplingRange"))))
        {
            FFloatInterval* Interval = RangeProp->ContainerPtrToValuePtr<FFloatInterval>(ElemPtr);
            if (Interval)
            {
                if (bHasRangeMin) Interval->Min = (float)RangeMin;
                if (bHasRangeMax) Interval->Max = (float)RangeMax;
                ModifiedFields.Add(TEXT("SamplingRange"));
            }
        }
    }
#endif

    if (ModifiedFields.Num() > 0)
    {
        Database->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("index"), Index);
    Result->SetNumberField(TEXT("modified_count"), ModifiedFields.Num());
    TArray<TSharedPtr<FJsonValue>> FieldsArray;
    for (const FString& F : ModifiedFields)
    {
        FieldsArray.Add(MakeShared<FJsonValueString>(F));
    }
    Result->SetArrayField(TEXT("modified_fields"), FieldsArray);
    return Result;
}

// ══════════════════════════════════════════════════════════════════════
// PSS Write Tools
// ══════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetPoseSearchSchemaSettings(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));

    UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *AssetPath);
    if (!Schema)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchSchema not found"));

    Schema->Modify();

    // Sample rate
    double SampleRate = 0;
    if (Params->TryGetNumberField(TEXT("sample_rate"), SampleRate))
    {
        Schema->SampleRate = FMath::Clamp((int32)SampleRate, 1, 240);
    }

    // Skeleton
    FString SkeletonPath;
    if (Params->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
    {
        USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
        if (!Skeleton)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
        }

        // Access Skeletons array via reflection
        FArrayProperty* SkArrayProp = CastField<FArrayProperty>(Schema->GetClass()->FindPropertyByName(TEXT("Skeletons")));
        if (SkArrayProp)
        {
            FScriptArrayHelper ArrayHelper(SkArrayProp, SkArrayProp->ContainerPtrToValuePtr<void>(Schema));
            const FStructProperty* InnerStruct = CastField<FStructProperty>(SkArrayProp->Inner);

            // If array is empty, add one element; otherwise set first element
            if (ArrayHelper.Num() == 0)
            {
                ArrayHelper.AddValue();
            }
            void* ElemPtr = ArrayHelper.GetRawPtr(0);

            if (InnerStruct && InnerStruct->Struct)
            {
                if (FObjectProperty* SkProp = CastField<FObjectProperty>(InnerStruct->Struct->FindPropertyByName(TEXT("Skeleton"))))
                {
                    SkProp->SetObjectPropertyValue(SkProp->ContainerPtrToValuePtr<void>(ElemPtr), Skeleton);
                }

                // Role
                FString Role;
                if (Params->TryGetStringField(TEXT("role"), Role))
                {
                    if (FNameProperty* RoleProp = CastField<FNameProperty>(InnerStruct->Struct->FindPropertyByName(TEXT("Role"))))
                    {
                        RoleProp->SetPropertyValue(RoleProp->ContainerPtrToValuePtr<void>(ElemPtr), FName(*Role));
                    }
                }
            }
        }
    }

    Schema->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("sample_rate"), Schema->SampleRate);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleAddPoseSearchSchemaChannel(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, ChannelType;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
    if (!Params->TryGetStringField(TEXT("channel_type"), ChannelType))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'channel_type'"));

    UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *AssetPath);
    if (!Schema)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchSchema not found"));

    Schema->Modify();

    // Create the channel object based on type
    UPoseSearchFeatureChannel* NewChannel = nullptr;

    if (ChannelType == TEXT("Trajectory"))
    {
        auto* Ch = NewObject<UPoseSearchFeatureChannel_Trajectory>(Schema);

        // Parse samples array
        const TArray<TSharedPtr<FJsonValue>>* SamplesArr = nullptr;
        if (Params->TryGetArrayField(TEXT("samples"), SamplesArr))
        {
            Ch->Samples.Empty();
            for (const TSharedPtr<FJsonValue>& SampleVal : *SamplesArr)
            {
                const TSharedPtr<FJsonObject>* SampleObj = nullptr;
                if (SampleVal->TryGetObject(SampleObj))
                {
                    FPoseSearchTrajectorySample Sample;
                    double Offset = 0, Flags = 0;
                    (*SampleObj)->TryGetNumberField(TEXT("offset"), Offset);
                    (*SampleObj)->TryGetNumberField(TEXT("flags"), Flags);
                    Sample.Offset = (float)Offset;
                    Sample.Flags = (int32)Flags;
#if WITH_EDITORONLY_DATA
                    double Weight = 1.0;
                    (*SampleObj)->TryGetNumberField(TEXT("weight"), Weight);
                    Sample.Weight = (float)Weight;
#endif
                    Ch->Samples.Add(Sample);
                }
            }
        }

#if WITH_EDITORONLY_DATA
        double Weight = 1.0;
        if (Params->TryGetNumberField(TEXT("weight"), Weight))
        {
            Ch->Weight = (float)Weight;
        }
#endif
        NewChannel = Ch;
    }
    else if (ChannelType == TEXT("Pose"))
    {
        auto* Ch = NewObject<UPoseSearchFeatureChannel_Pose>(Schema);

        // Parse sampled_bones array
        const TArray<TSharedPtr<FJsonValue>>* BonesArr = nullptr;
        if (Params->TryGetArrayField(TEXT("sampled_bones"), BonesArr))
        {
            Ch->SampledBones.Empty();
            for (const TSharedPtr<FJsonValue>& BoneVal : *BonesArr)
            {
                const TSharedPtr<FJsonObject>* BoneObj = nullptr;
                if (BoneVal->TryGetObject(BoneObj))
                {
                    FPoseSearchBone Bone;
                    FString BoneName;
                    if ((*BoneObj)->TryGetStringField(TEXT("bone_name"), BoneName))
                    {
                        Bone.Reference.BoneName = FName(*BoneName);
                    }
                    double Flags = (double)(int32)EPoseSearchBoneFlags::Position;
                    (*BoneObj)->TryGetNumberField(TEXT("flags"), Flags);
                    Bone.Flags = (int32)Flags;
#if WITH_EDITORONLY_DATA
                    double Weight = 1.0;
                    (*BoneObj)->TryGetNumberField(TEXT("weight"), Weight);
                    Bone.Weight = (float)Weight;
#endif
                    Ch->SampledBones.Add(Bone);
                }
            }
        }

#if WITH_EDITORONLY_DATA
        double Weight = 1.0;
        if (Params->TryGetNumberField(TEXT("weight"), Weight))
        {
            Ch->Weight = (float)Weight;
        }
#endif
        NewChannel = Ch;
    }
    else if (ChannelType == TEXT("Position"))
    {
        auto* Ch = NewObject<UPoseSearchFeatureChannel_Position>(Schema);
        FString BoneName;
        if (Params->TryGetStringField(TEXT("bone_name"), BoneName))
        {
            Ch->Bone.BoneName = FName(*BoneName);
        }
        double TimeOffset = 0;
        if (Params->TryGetNumberField(TEXT("sample_time_offset"), TimeOffset))
        {
            Ch->SampleTimeOffset = (float)TimeOffset;
        }
#if WITH_EDITORONLY_DATA
        double Weight = 1.0;
        if (Params->TryGetNumberField(TEXT("weight"), Weight))
        {
            Ch->Weight = (float)Weight;
        }
#endif
        NewChannel = Ch;
    }
    else if (ChannelType == TEXT("Velocity"))
    {
        auto* Ch = NewObject<UPoseSearchFeatureChannel_Velocity>(Schema);
        FString BoneName;
        if (Params->TryGetStringField(TEXT("bone_name"), BoneName))
        {
            Ch->Bone.BoneName = FName(*BoneName);
        }
        double TimeOffset = 0;
        if (Params->TryGetNumberField(TEXT("sample_time_offset"), TimeOffset))
        {
            Ch->SampleTimeOffset = (float)TimeOffset;
        }
#if WITH_EDITORONLY_DATA
        double Weight = 1.0;
        if (Params->TryGetNumberField(TEXT("weight"), Weight))
        {
            Ch->Weight = (float)Weight;
        }
#endif
        NewChannel = Ch;
    }
    else if (ChannelType == TEXT("Heading"))
    {
        auto* Ch = NewObject<UPoseSearchFeatureChannel_Heading>(Schema);
        FString BoneName;
        if (Params->TryGetStringField(TEXT("bone_name"), BoneName))
        {
            Ch->Bone.BoneName = FName(*BoneName);
        }
#if WITH_EDITORONLY_DATA
        double Weight = 1.0;
        if (Params->TryGetNumberField(TEXT("weight"), Weight))
        {
            Ch->Weight = (float)Weight;
        }
#endif
        NewChannel = Ch;
    }
    else if (ChannelType == TEXT("Curve"))
    {
        auto* Ch = NewObject<UPoseSearchFeatureChannel_Curve>(Schema);
        FString CurveName;
        if (Params->TryGetStringField(TEXT("curve_name"), CurveName))
        {
            Ch->CurveName = FName(*CurveName);
        }
#if WITH_EDITORONLY_DATA
        double Weight = 1.0;
        if (Params->TryGetNumberField(TEXT("weight"), Weight))
        {
            Ch->Weight = (float)Weight;
        }
#endif
        NewChannel = Ch;
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
            TEXT("Unknown channel_type '%s'. Supported: Trajectory, Pose, Position, Velocity, Heading, Curve"), *ChannelType));
    }

    // Add to Channels array via reflection
    FArrayProperty* ChannelsProp = CastField<FArrayProperty>(Schema->GetClass()->FindPropertyByName(TEXT("Channels")));
    if (!ChannelsProp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Channels property not found"));

    FScriptArrayHelper ArrayHelper(ChannelsProp, ChannelsProp->ContainerPtrToValuePtr<void>(Schema));
    int32 NewIndex = ArrayHelper.AddValue();
    FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ChannelsProp->Inner);
    if (InnerObjProp)
    {
        InnerObjProp->SetObjectPropertyValue(ArrayHelper.GetRawPtr(NewIndex), NewChannel);
    }

    Schema->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("new_index"), NewIndex);
    Result->SetStringField(TEXT("channel_class"), NewChannel->GetClass()->GetName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleRemovePoseSearchSchemaChannel(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    double IndexD = -1;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
    if (!Params->TryGetNumberField(TEXT("index"), IndexD))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'index'"));

    int32 Index = (int32)IndexD;

    UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *AssetPath);
    if (!Schema)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchSchema not found"));

    FArrayProperty* ChannelsProp = CastField<FArrayProperty>(Schema->GetClass()->FindPropertyByName(TEXT("Channels")));
    if (!ChannelsProp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Channels property not found"));

    FScriptArrayHelper ArrayHelper(ChannelsProp, ChannelsProp->ContainerPtrToValuePtr<void>(Schema));
    if (Index < 0 || Index >= ArrayHelper.Num())
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Index %d out of range (0..%d)"), Index, ArrayHelper.Num() - 1));

    Schema->Modify();
    ArrayHelper.RemoveValues(Index, 1);
    Schema->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("removed_index"), Index);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetPoseSearchSchemaChannelWeight(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    double ChannelIndexD = 0;
    double WeightD = 0.0;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
    if (!Params->TryGetNumberField(TEXT("channel_index"), ChannelIndexD))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'channel_index'"));
    if (!Params->TryGetNumberField(TEXT("weight"), WeightD))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'weight'"));

    int32 ChannelIndex = (int32)ChannelIndexD;
    float Weight = (float)WeightD;

    UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *AssetPath);
    if (!Schema)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchSchema not found"));

    FArrayProperty* ChannelsProp = CastField<FArrayProperty>(Schema->GetClass()->FindPropertyByName(TEXT("Channels")));
    if (!ChannelsProp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Channels property not found"));

    FScriptArrayHelper ArrayHelper(ChannelsProp, ChannelsProp->ContainerPtrToValuePtr<void>(Schema));
    if (ChannelIndex < 0 || ChannelIndex >= ArrayHelper.Num())
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Channel index %d out of range (0..%d)"), ChannelIndex, ArrayHelper.Num() - 1));

    FObjectProperty* InnerObjectProp = CastField<FObjectProperty>(ChannelsProp->Inner);
    if (!InnerObjectProp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Channels inner is not ObjectProperty"));

    UObject* ChannelObject = InnerObjectProp->GetObjectPropertyValue(ArrayHelper.GetRawPtr(ChannelIndex));
    if (!ChannelObject)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Channel UObject is null"));

    FFloatProperty* WeightProp = CastField<FFloatProperty>(ChannelObject->GetClass()->FindPropertyByName(TEXT("Weight")));
    if (!WeightProp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Channel class %s has no Weight property"), *ChannelObject->GetClass()->GetName()));

    Schema->Modify();
    ChannelObject->Modify();
    WeightProp->SetPropertyValue_InContainer(ChannelObject, Weight);
    Schema->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("channel_index"), ChannelIndex);
    Result->SetStringField(TEXT("channel_class"), ChannelObject->GetClass()->GetName());
    Result->SetNumberField(TEXT("weight"), WeightProp->GetPropertyValue_InContainer(ChannelObject));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetPoseSearchSchemaTrajectorySample(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    double ChannelIndexD = 0;
    double SampleIndexD = 0;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
    if (!Params->TryGetNumberField(TEXT("channel_index"), ChannelIndexD))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'channel_index'"));
    if (!Params->TryGetNumberField(TEXT("sample_index"), SampleIndexD))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'sample_index'"));

    int32 ChannelIndex = (int32)ChannelIndexD;
    int32 SampleIndex = (int32)SampleIndexD;

    UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *AssetPath);
    if (!Schema)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PoseSearchSchema not found"));

    FArrayProperty* ChannelsProp = CastField<FArrayProperty>(Schema->GetClass()->FindPropertyByName(TEXT("Channels")));
    if (!ChannelsProp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Channels property not found"));

    FScriptArrayHelper ChannelsHelper(ChannelsProp, ChannelsProp->ContainerPtrToValuePtr<void>(Schema));
    if (ChannelIndex < 0 || ChannelIndex >= ChannelsHelper.Num())
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Channel index %d out of range (0..%d)"), ChannelIndex, ChannelsHelper.Num() - 1));

    FObjectProperty* InnerObjectProp = CastField<FObjectProperty>(ChannelsProp->Inner);
    UObject* ChannelObject = InnerObjectProp ? InnerObjectProp->GetObjectPropertyValue(ChannelsHelper.GetRawPtr(ChannelIndex)) : nullptr;
    if (!ChannelObject)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Channel UObject is null"));

    FArrayProperty* SamplesProp = CastField<FArrayProperty>(ChannelObject->GetClass()->FindPropertyByName(TEXT("Samples")));
    if (!SamplesProp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Channel class %s has no Samples property (not a Trajectory channel?)"), *ChannelObject->GetClass()->GetName()));

    FScriptArrayHelper SamplesHelper(SamplesProp, SamplesProp->ContainerPtrToValuePtr<void>(ChannelObject));
    if (SampleIndex < 0 || SampleIndex >= SamplesHelper.Num())
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Sample index %d out of range (0..%d)"), SampleIndex, SamplesHelper.Num() - 1));

    FStructProperty* InnerStruct = CastField<FStructProperty>(SamplesProp->Inner);
    if (!InnerStruct || !InnerStruct->Struct)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid Samples element struct"));

    const UStruct* SampleStructType = InnerStruct->Struct;
    void* SampleElemPtr = SamplesHelper.GetRawPtr(SampleIndex);

    Schema->Modify();
    ChannelObject->Modify();

    TArray<FString> ModifiedFields;

    double OffsetD = 0.0;
    if (Params->TryGetNumberField(TEXT("offset"), OffsetD))
    {
        if (FFloatProperty* Prop = CastField<FFloatProperty>(SampleStructType->FindPropertyByName(TEXT("Offset"))))
        {
            Prop->SetPropertyValue_InContainer(SampleElemPtr, (float)OffsetD);
            ModifiedFields.Add(TEXT("Offset"));
        }
    }

    double WeightD = 0.0;
    if (Params->TryGetNumberField(TEXT("weight"), WeightD))
    {
#if WITH_EDITORONLY_DATA
        if (FFloatProperty* Prop = CastField<FFloatProperty>(SampleStructType->FindPropertyByName(TEXT("Weight"))))
        {
            Prop->SetPropertyValue_InContainer(SampleElemPtr, (float)WeightD);
            ModifiedFields.Add(TEXT("Weight"));
        }
#endif
    }

    double FlagsD = 0.0;
    if (Params->TryGetNumberField(TEXT("flags"), FlagsD))
    {
        if (FIntProperty* Prop = CastField<FIntProperty>(SampleStructType->FindPropertyByName(TEXT("Flags"))))
        {
            Prop->SetPropertyValue_InContainer(SampleElemPtr, (int32)FlagsD);
            ModifiedFields.Add(TEXT("Flags"));
        }
    }

    if (ModifiedFields.Num() > 0)
    {
        Schema->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("channel_index"), ChannelIndex);
    Result->SetNumberField(TEXT("sample_index"), SampleIndex);
    Result->SetNumberField(TEXT("modified_count"), ModifiedFields.Num());
    TArray<TSharedPtr<FJsonValue>> FieldsArray;
    for (const FString& F : ModifiedFields)
    {
        FieldsArray.Add(MakeShared<FJsonValueString>(F));
    }
    Result->SetArrayField(TEXT("modified_fields"), FieldsArray);
    return Result;
}

// ══════════════════════════════════════════════════════════════════════
// Chooser Table Write Tools
// ══════════════════════════════════════════════════════════════════════

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleAddChooserTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, ResultAssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
    if (!Params->TryGetStringField(TEXT("result_asset_path"), ResultAssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'result_asset_path'"));

    UChooserTable* Table = LoadObject<UChooserTable>(nullptr, *AssetPath);
    if (!Table)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ChooserTable not found"));

    UObject* ResultAsset = LoadObject<UObject>(nullptr, *ResultAssetPath);
    if (!ResultAsset)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Result asset not found: %s"), *ResultAssetPath));

    Table->Modify();

    // Add result entry
#if WITH_EDITORONLY_DATA
    FInstancedStruct NewResult;
    NewResult.InitializeAs<FAssetChooser>();
    FAssetChooser& AssetResult = NewResult.GetMutable<FAssetChooser>();
    AssetResult.Asset = ResultAsset;
    Table->ResultsStructs.Add(MoveTemp(NewResult));
    int32 NewRowIndex = Table->ResultsStructs.Num() - 1;
#else
    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot add rows outside of editor"));
#endif

    // Add default row values to each column
    const TArray<TSharedPtr<FJsonValue>>* ColumnValues = nullptr;
    Params->TryGetArrayField(TEXT("column_values"), ColumnValues);

    for (int32 ColIdx = 0; ColIdx < Table->ColumnsStructs.Num(); ++ColIdx)
    {
        FInstancedStruct& ColInst = Table->ColumnsStructs[ColIdx];
        const UScriptStruct* ColStruct = ColInst.GetScriptStruct();
        if (!ColStruct || !ColInst.IsValid()) continue;

        // Get the column value for this column from params (if provided)
        FString ColValue;
        if (ColumnValues && ColumnValues->IsValidIndex(ColIdx))
        {
            (*ColumnValues)[ColIdx]->TryGetString(ColValue);
        }

        if (ColStruct == FEnumColumn::StaticStruct())
        {
            FEnumColumn& EnumCol = ColInst.GetMutable<FEnumColumn>();
            FChooserEnumRowData RowData;
            if (!ColValue.IsEmpty())
            {
                // Look up enum value by name
                const UEnum* Enum = EnumCol.GetEnum();
                if (Enum)
                {
                    int64 EnumValue = Enum->GetValueByNameString(ColValue);
                    if (EnumValue != INDEX_NONE)
                    {
                        RowData.Value = (uint8)EnumValue;
                        RowData.Comparison = EEnumColumnCellValueComparison::MatchEqual;
#if WITH_EDITORONLY_DATA
                        RowData.ValueName = FName(*ColValue);
#endif
                    }
                    else
                    {
                        RowData.Comparison = EEnumColumnCellValueComparison::MatchAny;
                    }
                }
                else
                {
                    RowData.Comparison = EEnumColumnCellValueComparison::MatchAny;
                }
            }
            else
            {
                RowData.Comparison = EEnumColumnCellValueComparison::MatchAny;
            }
            EnumCol.RowValues.Add(RowData);
        }
        else if (ColStruct == FBoolColumn::StaticStruct())
        {
            FBoolColumn& BoolCol = ColInst.GetMutable<FBoolColumn>();
            if (ColValue == TEXT("true"))
            {
                BoolCol.RowValuesWithAny.Add(EBoolColumnCellValue::MatchTrue);
            }
            else if (ColValue == TEXT("false"))
            {
                BoolCol.RowValuesWithAny.Add(EBoolColumnCellValue::MatchFalse);
            }
            else
            {
                BoolCol.RowValuesWithAny.Add(EBoolColumnCellValue::MatchAny);
            }
        }
        else
        {
            // For unsupported column types, use reflection to add a default value
            // to the RowValues array via RowValuesPropertyName
#if WITH_EDITOR
            FChooserColumnBase& ColBase = ColInst.GetMutable<FChooserColumnBase>();
            FName RowPropName = ColBase.RowValuesPropertyName();
            if (FProperty* RowProp = ColStruct->FindPropertyByName(RowPropName))
            {
                if (FArrayProperty* RowArrayProp = CastField<FArrayProperty>(RowProp))
                {
                    FScriptArrayHelper RowHelper(RowArrayProp, RowArrayProp->ContainerPtrToValuePtr<void>(&ColBase));
                    RowHelper.AddValue();
                }
            }
#endif
        }
    }

    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("new_row_index"), NewRowIndex);
    Result->SetStringField(TEXT("result_asset"), ResultAsset->GetPathName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleRemoveChooserTableRow(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    double IndexD = -1;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
    if (!Params->TryGetNumberField(TEXT("row_index"), IndexD))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'row_index'"));

    int32 RowIndex = (int32)IndexD;

    UChooserTable* Table = LoadObject<UChooserTable>(nullptr, *AssetPath);
    if (!Table)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ChooserTable not found"));

#if WITH_EDITORONLY_DATA
    if (RowIndex < 0 || RowIndex >= Table->ResultsStructs.Num())
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Row index %d out of range (0..%d)"), RowIndex, Table->ResultsStructs.Num() - 1));

    Table->Modify();

    // Remove result entry
    Table->ResultsStructs.RemoveAt(RowIndex);

    // Remove corresponding row values from each column
    for (int32 ColIdx = 0; ColIdx < Table->ColumnsStructs.Num(); ++ColIdx)
    {
        FInstancedStruct& ColInst = Table->ColumnsStructs[ColIdx];
        const UScriptStruct* ColStruct = ColInst.GetScriptStruct();
        if (!ColStruct || !ColInst.IsValid()) continue;

        if (ColStruct == FEnumColumn::StaticStruct())
        {
            FEnumColumn& EnumCol = ColInst.GetMutable<FEnumColumn>();
            if (EnumCol.RowValues.IsValidIndex(RowIndex))
                EnumCol.RowValues.RemoveAt(RowIndex);
        }
        else if (ColStruct == FBoolColumn::StaticStruct())
        {
            FBoolColumn& BoolCol = ColInst.GetMutable<FBoolColumn>();
            if (BoolCol.RowValuesWithAny.IsValidIndex(RowIndex))
                BoolCol.RowValuesWithAny.RemoveAt(RowIndex);
        }
        else
        {
#if WITH_EDITOR
            FChooserColumnBase& ColBase = ColInst.GetMutable<FChooserColumnBase>();
            FName RowPropName = ColBase.RowValuesPropertyName();
            if (FProperty* RowProp = ColStruct->FindPropertyByName(RowPropName))
            {
                if (FArrayProperty* RowArrayProp = CastField<FArrayProperty>(RowProp))
                {
                    FScriptArrayHelper RowHelper(RowArrayProp, RowArrayProp->ContainerPtrToValuePtr<void>(&ColBase));
                    if (RowIndex < RowHelper.Num())
                        RowHelper.RemoveValues(RowIndex, 1);
                }
            }
#endif
        }
    }

    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("removed_row_index"), RowIndex);
    return Result;
#else
    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot remove rows outside of editor"));
#endif
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetAnimationProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    UAnimSequence* AnimSeq = LoadObject<UAnimSequence>(nullptr, *AssetPath);
    if (!AnimSeq)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AnimSequence not found: %s"), *AssetPath));
    }

    TArray<FString> ModifiedFields;

    if (Params->HasField(TEXT("b_enable_root_motion")))
    {
        AnimSeq->bEnableRootMotion = Params->GetBoolField(TEXT("b_enable_root_motion"));
        ModifiedFields.Add(TEXT("bEnableRootMotion"));
    }

    if (Params->HasField(TEXT("root_motion_root_lock")))
    {
        FString LockStr = Params->GetStringField(TEXT("root_motion_root_lock"));
        if (LockStr.Equals(TEXT("RefPose"), ESearchCase::IgnoreCase))
        {
            AnimSeq->RootMotionRootLock = ERootMotionRootLock::RefPose;
        }
        else if (LockStr.Equals(TEXT("AnimFirstFrame"), ESearchCase::IgnoreCase))
        {
            AnimSeq->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
        }
        else if (LockStr.Equals(TEXT("Zero"), ESearchCase::IgnoreCase))
        {
            AnimSeq->RootMotionRootLock = ERootMotionRootLock::Zero;
        }
        else
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Invalid root_motion_root_lock: %s (expected RefPose/AnimFirstFrame/Zero)"), *LockStr));
        }
        ModifiedFields.Add(TEXT("RootMotionRootLock"));
    }

    if (Params->HasField(TEXT("b_force_root_lock")))
    {
        AnimSeq->bForceRootLock = Params->GetBoolField(TEXT("b_force_root_lock"));
        ModifiedFields.Add(TEXT("bForceRootLock"));
    }

    if (Params->HasField(TEXT("b_use_normalized_root_motion_scale")))
    {
        AnimSeq->bUseNormalizedRootMotionScale = Params->GetBoolField(TEXT("b_use_normalized_root_motion_scale"));
        ModifiedFields.Add(TEXT("bUseNormalizedRootMotionScale"));
    }

    if (ModifiedFields.Num() == 0)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No recognized properties to set"));
    }

    AnimSeq->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), AnimSeq->GetPathName());
    Result->SetNumberField(TEXT("modified_count"), ModifiedFields.Num());

    TArray<TSharedPtr<FJsonValue>> FieldsJson;
    for (const FString& F : ModifiedFields)
    {
        FieldsJson.Add(MakeShared<FJsonValueString>(F));
    }
    Result->SetArrayField(TEXT("modified_fields"), FieldsJson);

    Result->SetBoolField(TEXT("b_enable_root_motion"), AnimSeq->bEnableRootMotion);
    Result->SetStringField(TEXT("root_motion_root_lock"), RootMotionRootLockToString(AnimSeq->RootMotionRootLock.GetValue()));
    Result->SetBoolField(TEXT("b_force_root_lock"), AnimSeq->bForceRootLock);
    Result->SetBoolField(TEXT("b_use_normalized_root_motion_scale"), AnimSeq->bUseNormalizedRootMotionScale);

    return Result;
}

// =============================================================================
// Batch C.1: IKRig write tools (Items 8 + 9 from Docs/UnrealMCP_扩展需求清单.md)
// =============================================================================

namespace
{
    // Normalize /Game/Foo/Bar.Bar or /Game/Foo/Bar to the (package_name, asset_name) pair
    // used when creating a package + NewObject pair.
    bool SplitAssetPath(const FString& InPath, FString& OutPackageName, FString& OutAssetName)
    {
        OutPackageName = InPath;
        int32 DotIdx = INDEX_NONE;
        if (OutPackageName.FindChar('.', DotIdx))
        {
            OutPackageName = OutPackageName.Left(DotIdx);
        }
        int32 SlashIdx = INDEX_NONE;
        if (!OutPackageName.FindLastChar('/', SlashIdx) || SlashIdx + 1 >= OutPackageName.Len())
        {
            return false;
        }
        OutAssetName = OutPackageName.Mid(SlashIdx + 1);
        return !OutAssetName.IsEmpty();
    }

    // Resolve a UIKRigDefinition asset then a controller for it. Returns nullptr on failure
    // and writes a friendly error into OutError.
    UIKRigController* GetIKRigControllerForPath(const FString& AssetPath, FString& OutError)
    {
        UIKRigDefinition* Asset = LoadObject<UIKRigDefinition>(nullptr, *AssetPath);
        if (!Asset)
        {
            OutError = FString::Printf(TEXT("IKRig asset not found: %s"), *AssetPath);
            return nullptr;
        }
        UIKRigController* Ctrl = UIKRigController::GetController(Asset);
        if (!Ctrl)
        {
            OutError = FString::Printf(TEXT("Failed to obtain UIKRigController for: %s"), *AssetPath);
            return nullptr;
        }
        return Ctrl;
    }
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleCreateIKRig(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    FString PreviewMeshPath;
    Params->TryGetStringField(TEXT("preview_skeletal_mesh_path"), PreviewMeshPath);

    FString PackageName, AssetName;
    if (!SplitAssetPath(AssetPath, PackageName, AssetName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid asset_path: %s"), *AssetPath));
    }

    // Guard: if the asset already exists, fail rather than silently overwrite.
    // Use FindPackage+FindObject to avoid loading unintended packages as side-effect.
    if (UPackage* Existing = FindPackage(nullptr, *PackageName))
    {
        if (FindObject<UIKRigDefinition>(Existing, *AssetName))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("IKRig asset already exists: %s"), *AssetPath));
        }
    }

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
    }
    Package->FullyLoad();

    UIKRigDefinition* NewAsset = NewObject<UIKRigDefinition>(
        Package,
        UIKRigDefinition::StaticClass(),
        *AssetName,
        RF_Public | RF_Standalone);
    if (!NewAsset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("NewObject<UIKRigDefinition> returned null"));
    }

    FAssetRegistryModule::AssetCreated(NewAsset);
    Package->MarkPackageDirty();

    // Set preview mesh via controller. This also loads the skeleton hierarchy into the IKRig's internal skeleton.
    bool bSkeletalMeshAssigned = false;
    FString SkeletalMeshError;
    if (!PreviewMeshPath.IsEmpty())
    {
        USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *PreviewMeshPath);
        if (!Mesh)
        {
            SkeletalMeshError = FString::Printf(TEXT("Preview SkeletalMesh not found: %s"), *PreviewMeshPath);
        }
        else if (UIKRigController* Ctrl = UIKRigController::GetController(NewAsset))
        {
            bSkeletalMeshAssigned = Ctrl->SetSkeletalMesh(Mesh);
            if (!bSkeletalMeshAssigned)
            {
                SkeletalMeshError = FString::Printf(TEXT("SetSkeletalMesh returned false (mesh may be incompatible): %s"), *PreviewMeshPath);
            }
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
    Result->SetBoolField(TEXT("skeletal_mesh_assigned"), bSkeletalMeshAssigned);
    if (!SkeletalMeshError.IsEmpty())
    {
        Result->SetStringField(TEXT("skeletal_mesh_warning"), SkeletalMeshError);
    }
    // Not auto-saved — caller must call save_asset (future) or save in editor.
    Result->SetBoolField(TEXT("saved"), false);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetIKRigRetargetRoot(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, BoneName;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("bone_name"), BoneName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'bone_name' parameter"));
    }

    FString Err;
    UIKRigController* Ctrl = GetIKRigControllerForPath(AssetPath, Err);
    if (!Ctrl)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
    }

    const bool bOK = Ctrl->SetRetargetRoot(FName(*BoneName));
    if (!bOK)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("SetRetargetRoot failed (bone may not exist on the rig's skeleton): %s"), *BoneName));
    }

    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Result->SetStringField(TEXT("retarget_root_bone"), BoneName);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleAddIKRigRetargetChain(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, ChainName, StartBone, EndBone, GoalName;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("chain_name"), ChainName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'chain_name' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("start_bone"), StartBone))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'start_bone' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("end_bone"), EndBone))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'end_bone' parameter"));
    }
    Params->TryGetStringField(TEXT("goal_name"), GoalName); // optional

    FString Err;
    UIKRigController* Ctrl = GetIKRigControllerForPath(AssetPath, Err);
    if (!Ctrl)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
    }

    const FName Assigned = Ctrl->AddRetargetChain(
        FName(*ChainName),
        FName(*StartBone),
        FName(*EndBone),
        GoalName.IsEmpty() ? NAME_None : FName(*GoalName));
    if (Assigned.IsNone())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AddRetargetChain returned None (bones may not exist or chain invalid): %s [%s → %s]"),
                *ChainName, *StartBone, *EndBone));
    }

    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Result->SetStringField(TEXT("assigned_chain_name"), Assigned.ToString());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleAddIKRigGoal(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, GoalName, BoneName;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("goal_name"), GoalName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'goal_name' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("bone_name"), BoneName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'bone_name' parameter"));
    }

    FString Err;
    UIKRigController* Ctrl = GetIKRigControllerForPath(AssetPath, Err);
    if (!Ctrl)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
    }

    const FName Assigned = Ctrl->AddNewGoal(FName(*GoalName), FName(*BoneName));
    if (Assigned.IsNone())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AddNewGoal returned None (duplicate name or missing bone): goal=%s bone=%s"),
                *GoalName, *BoneName));
    }

    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Result->SetStringField(TEXT("assigned_goal_name"), Assigned.ToString());
    Result->SetStringField(TEXT("bone_name"), BoneName);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleAddIKRigSolver(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, SolverStructName;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }
    if (!Params->TryGetStringField(TEXT("solver_struct_name"), SolverStructName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'solver_struct_name' parameter"));
    }

    FString Err;
    UIKRigController* Ctrl = GetIKRigControllerForPath(AssetPath, Err);
    if (!Ctrl)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
    }

    // UIKRigController::AddSolver(FString) expects the full UStruct package path, e.g.
    //   /Script/IKRig.IKRigFBIKSolver
    // Accept either that form or a bare struct name like "IKRigFBIKSolver" and try to
    // resolve it via FindFirstObject<UScriptStruct>.
    int32 SolverIndex = INDEX_NONE;
    if (SolverStructName.StartsWith(TEXT("/Script/")))
    {
        SolverIndex = Ctrl->AddSolver(SolverStructName);
    }
    else
    {
        UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*SolverStructName, EFindFirstObjectOptions::None, ELogVerbosity::Warning);
        if (!Struct)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown solver struct (pass /Script/<Module>.<Struct> or a valid UScriptStruct name): %s"), *SolverStructName));
        }
        SolverIndex = Ctrl->AddSolver(Struct);
    }

    if (SolverIndex == INDEX_NONE)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AddSolver returned INDEX_NONE for type: %s"), *SolverStructName));
    }

    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Result->SetNumberField(TEXT("solver_index"), SolverIndex);
    return Result;
}

// =============================================================================
// Batch C.2: IKRetargeter write tools (Items 10 + 11 + 12)
// =============================================================================

namespace
{
    UIKRetargeterController* GetIKRetargeterControllerForPath(const FString& AssetPath, FString& OutError)
    {
        UIKRetargeter* Asset = LoadObject<UIKRetargeter>(nullptr, *AssetPath);
        if (!Asset)
        {
            OutError = FString::Printf(TEXT("IKRetargeter asset not found: %s"), *AssetPath);
            return nullptr;
        }
        UIKRetargeterController* Ctrl = UIKRetargeterController::GetController(Asset);
        if (!Ctrl)
        {
            OutError = FString::Printf(TEXT("Failed to obtain UIKRetargeterController for: %s"), *AssetPath);
            return nullptr;
        }
        return Ctrl;
    }

    bool ParseSourceOrTarget(const FString& Side, ERetargetSourceOrTarget& Out, FString& OutError)
    {
        if (Side.Equals(TEXT("Source"), ESearchCase::IgnoreCase))
        {
            Out = ERetargetSourceOrTarget::Source;
            return true;
        }
        if (Side.Equals(TEXT("Target"), ESearchCase::IgnoreCase))
        {
            Out = ERetargetSourceOrTarget::Target;
            return true;
        }
        OutError = FString::Printf(TEXT("Invalid 'side' value: %s (expected 'Source' or 'Target')"), *Side);
        return false;
    }

    // Walk a dotted property path through the script struct, e.g. "Settings.bCopyTranslation".
    // Returns the leaf FProperty + value pointer; returns false when any segment fails to resolve.
    // Container may be the FInstancedStruct memory for the op itself; on success ContainerOut points
    // to the memory inside the leaf (not the leaf value itself), and PropertyOut is the leaf property.
    bool ResolveStructFieldPath(
        const UScriptStruct* StructType,
        void* StructMemory,
        const FString& FieldPath,
        FProperty*& PropertyOut,
        void*& ValuePtrOut,
        FString& OutError)
    {
        if (!StructType || !StructMemory)
        {
            OutError = TEXT("ResolveStructFieldPath: null struct or memory");
            return false;
        }
        TArray<FString> Segments;
        FieldPath.ParseIntoArray(Segments, TEXT("."), true);
        if (Segments.Num() == 0)
        {
            OutError = TEXT("Empty field_path");
            return false;
        }

        const UScriptStruct* CurrentStruct = StructType;
        void* CurrentMem = StructMemory;
        for (int32 i = 0; i < Segments.Num(); ++i)
        {
            FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*Segments[i]));
            if (!Prop)
            {
                OutError = FString::Printf(TEXT("Property '%s' not found on struct '%s' at segment %d of '%s'"),
                    *Segments[i], *CurrentStruct->GetName(), i, *FieldPath);
                return false;
            }
            void* InnerVal = Prop->ContainerPtrToValuePtr<void>(CurrentMem);
            if (i == Segments.Num() - 1)
            {
                PropertyOut = Prop;
                ValuePtrOut = InnerVal;
                return true;
            }
            // Need to descend into a struct
            FStructProperty* AsStruct = CastField<FStructProperty>(Prop);
            if (!AsStruct)
            {
                OutError = FString::Printf(TEXT("Cannot descend into non-struct property '%s' at segment %d of '%s'"),
                    *Segments[i], i, *FieldPath);
                return false;
            }
            CurrentStruct = AsStruct->Struct;
            CurrentMem = InnerVal;
        }
        return false; // unreachable
    }
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleCreateIKRetargeter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, SourceIKRigPath, TargetIKRigPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }
    Params->TryGetStringField(TEXT("source_ik_rig_path"), SourceIKRigPath); // optional
    Params->TryGetStringField(TEXT("target_ik_rig_path"), TargetIKRigPath); // optional

    FString PackageName, AssetName;
    if (!SplitAssetPath(AssetPath, PackageName, AssetName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid asset_path: %s"), *AssetPath));
    }

    if (UPackage* Existing = FindPackage(nullptr, *PackageName))
    {
        if (FindObject<UIKRetargeter>(Existing, *AssetName))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("IKRetargeter asset already exists: %s"), *AssetPath));
        }
    }

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
    }
    Package->FullyLoad();

    UIKRetargeter* NewAsset = NewObject<UIKRetargeter>(
        Package,
        UIKRetargeter::StaticClass(),
        *AssetName,
        RF_Public | RF_Standalone);
    if (!NewAsset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("NewObject<UIKRetargeter> returned null"));
    }

    FAssetRegistryModule::AssetCreated(NewAsset);
    Package->MarkPackageDirty();

    UIKRetargeterController* Ctrl = UIKRetargeterController::GetController(NewAsset);
    if (!Ctrl)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("UIKRetargeterController::GetController returned null on the newly created asset"));
    }

    bool bSourceAssigned = false, bTargetAssigned = false;
    FString SourceWarn, TargetWarn;
    if (!SourceIKRigPath.IsEmpty())
    {
        UIKRigDefinition* Src = LoadObject<UIKRigDefinition>(nullptr, *SourceIKRigPath);
        if (!Src) SourceWarn = FString::Printf(TEXT("Source IKRig not found: %s"), *SourceIKRigPath);
        else { Ctrl->SetIKRig(ERetargetSourceOrTarget::Source, Src); bSourceAssigned = true; }
    }
    if (!TargetIKRigPath.IsEmpty())
    {
        UIKRigDefinition* Tgt = LoadObject<UIKRigDefinition>(nullptr, *TargetIKRigPath);
        if (!Tgt) TargetWarn = FString::Printf(TEXT("Target IKRig not found: %s"), *TargetIKRigPath);
        else { Ctrl->SetIKRig(ERetargetSourceOrTarget::Target, Tgt); bTargetAssigned = true; }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
    Result->SetBoolField(TEXT("source_assigned"), bSourceAssigned);
    Result->SetBoolField(TEXT("target_assigned"), bTargetAssigned);
    if (!SourceWarn.IsEmpty()) Result->SetStringField(TEXT("source_warning"), SourceWarn);
    if (!TargetWarn.IsEmpty()) Result->SetStringField(TEXT("target_warning"), TargetWarn);
    Result->SetBoolField(TEXT("saved"), false);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetIKRetargeterOpEnabled(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    int32 OpIndex = INDEX_NONE;
    bool bEnabled = false;

    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetNumberField(TEXT("op_index"), OpIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'op_index' parameter"));
    if (!Params->TryGetBoolField(TEXT("enabled"), bEnabled))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'enabled' parameter"));

    FString Err;
    UIKRetargeterController* Ctrl = GetIKRetargeterControllerForPath(AssetPath, Err);
    if (!Ctrl) return FUnrealMCPCommonUtils::CreateErrorResponse(Err);

    const bool bOK = Ctrl->SetRetargetOpEnabled(OpIndex, bEnabled);
    if (!bOK)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("SetRetargetOpEnabled failed (invalid index?): op_index=%d"), OpIndex));
    }

    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Result->SetNumberField(TEXT("op_index"), OpIndex);
    Result->SetBoolField(TEXT("enabled"), bEnabled);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetIKRetargeterOpField(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, FieldPath, Value;
    int32 OpIndex = INDEX_NONE;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetNumberField(TEXT("op_index"), OpIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'op_index' parameter"));
    if (!Params->TryGetStringField(TEXT("field_path"), FieldPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'field_path' parameter"));
    if (!Params->TryGetStringField(TEXT("value"), Value))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'value' parameter (must be string; for structs/arrays use the same text format UE accepts in T3D)"));

    FString Err;
    UIKRetargeterController* Ctrl = GetIKRetargeterControllerForPath(AssetPath, Err);
    if (!Ctrl) return FUnrealMCPCommonUtils::CreateErrorResponse(Err);

    FInstancedStruct* OpStruct = Ctrl->GetRetargetOpStructAtIndex(OpIndex);
    if (!OpStruct || !OpStruct->IsValid())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Op not found at index %d"), OpIndex));
    }

    const UScriptStruct* OpType = OpStruct->GetScriptStruct();
    void* OpMemory = OpStruct->GetMutableMemory();

    FProperty* Prop = nullptr;
    void* ValuePtr = nullptr;
    FString PathErr;
    if (!ResolveStructFieldPath(OpType, OpMemory, FieldPath, Prop, ValuePtr, PathErr))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(PathErr);
    }

    // Use ImportText_Direct: handles primitives, FName, FString, enums, FVector, FRotator, FQuat,
    // arrays-as-text, etc. Returns null on parse failure.
    const TCHAR* Result = Prop->ImportText_Direct(*Value, ValuePtr, /*OwnerObject=*/nullptr, PPF_None, GLog);
    if (!Result)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("ImportText_Direct failed for field '%s' (value='%s', expected text-form for type %s)"),
                *FieldPath, *Value, *Prop->GetCPPType()));
    }

    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
    Out->SetBoolField(TEXT("success"), true);
    Out->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Out->SetNumberField(TEXT("op_index"), OpIndex);
    Out->SetStringField(TEXT("field_path"), FieldPath);
    Out->SetStringField(TEXT("op_struct"), OpType->GetName());
    return Out;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleAddIKRetargeterOp(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, OpStructName;
    int32 InsertAfter = INDEX_NONE;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetStringField(TEXT("op_struct_name"), OpStructName))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'op_struct_name' parameter"));
    Params->TryGetNumberField(TEXT("insert_after_index"), InsertAfter); // optional

    FString Err;
    UIKRetargeterController* Ctrl = GetIKRetargeterControllerForPath(AssetPath, Err);
    if (!Ctrl) return FUnrealMCPCommonUtils::CreateErrorResponse(Err);

    int32 NewIndex = INDEX_NONE;
    if (OpStructName.StartsWith(TEXT("/Script/")))
    {
        NewIndex = Ctrl->AddRetargetOp(OpStructName);
    }
    else
    {
        UScriptStruct* Struct = FindFirstObject<UScriptStruct>(*OpStructName, EFindFirstObjectOptions::None, ELogVerbosity::Warning);
        if (!Struct)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown op struct: %s"), *OpStructName));
        }
        NewIndex = Ctrl->AddRetargetOp(Struct);
    }

    if (NewIndex == INDEX_NONE)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("AddRetargetOp returned INDEX_NONE for type: %s"), *OpStructName));
    }

    // Optional reorder: move the new op so it sits right after insert_after_index.
    bool bMoved = false;
    int32 FinalIndex = NewIndex;
    if (InsertAfter != INDEX_NONE)
    {
        const int32 TargetIdx = InsertAfter + 1;
        if (TargetIdx != NewIndex)
        {
            bMoved = Ctrl->MoveRetargetOpInStack(NewIndex, TargetIdx);
            // The actual final index may differ from TargetIdx because of execution-order constraints;
            // we re-resolve via op name.
            const FName OpName = Ctrl->GetOpName(NewIndex);
            if (!OpName.IsNone())
            {
                FinalIndex = Ctrl->GetIndexOfOpByName(OpName);
            }
        }
    }

    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Result->SetNumberField(TEXT("op_index"), FinalIndex);
    Result->SetBoolField(TEXT("reordered"), bMoved);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleAddIKRetargeterPinBoneEntry(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, BoneCopyFrom, BoneCopyTo;
    int32 OpIndex = INDEX_NONE;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetNumberField(TEXT("op_index"), OpIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'op_index' parameter"));
    if (!Params->TryGetStringField(TEXT("bone_to_copy_from"), BoneCopyFrom))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'bone_to_copy_from' parameter"));
    if (!Params->TryGetStringField(TEXT("bone_to_copy_to"), BoneCopyTo))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'bone_to_copy_to' parameter"));

    FString Err;
    UIKRetargeterController* Ctrl = GetIKRetargeterControllerForPath(AssetPath, Err);
    if (!Ctrl) return FUnrealMCPCommonUtils::CreateErrorResponse(Err);

    FInstancedStruct* OpStruct = Ctrl->GetRetargetOpStructAtIndex(OpIndex);
    if (!OpStruct || !OpStruct->IsValid())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Op not found at index %d"), OpIndex));
    }
    const UScriptStruct* OpType = OpStruct->GetScriptStruct();
    if (!OpType || !OpType->IsChildOf(FIKRetargetPinBoneOp::StaticStruct()))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Op at index %d is not a Pin Bones op (got %s)"),
                OpIndex, OpType ? *OpType->GetName() : TEXT("null")));
    }

    FIKRetargetPinBoneOp* PinOp = OpStruct->GetMutablePtr<FIKRetargetPinBoneOp>();
    if (!PinOp)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Pin Bones op memory is null"));
    }

    FPinBoneData NewEntry;
    NewEntry.BoneToCopyFrom.BoneName = FName(*BoneCopyFrom);
    NewEntry.BoneToCopyTo.BoneName = FName(*BoneCopyTo);
    PinOp->Settings.BonesToPin.Add(NewEntry);

    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Result->SetNumberField(TEXT("op_index"), OpIndex);
    Result->SetNumberField(TEXT("entry_index"), PinOp->Settings.BonesToPin.Num() - 1);
    Result->SetStringField(TEXT("bone_to_copy_from"), BoneCopyFrom);
    Result->SetStringField(TEXT("bone_to_copy_to"), BoneCopyTo);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetIKRetargeterChainMapping(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, SourceChain, TargetChain;
    int32 OpIndex = INDEX_NONE;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetNumberField(TEXT("op_index"), OpIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'op_index' parameter"));
    Params->TryGetStringField(TEXT("source_chain_name"), SourceChain); // empty → clear mapping
    if (!Params->TryGetStringField(TEXT("target_chain_name"), TargetChain))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_chain_name' parameter"));

    FString Err;
    UIKRetargeterController* Ctrl = GetIKRetargeterControllerForPath(AssetPath, Err);
    if (!Ctrl) return FUnrealMCPCommonUtils::CreateErrorResponse(Err);

    const FName OpName = Ctrl->GetOpName(OpIndex);
    if (OpName.IsNone())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("No op found at index %d"), OpIndex));
    }

    const FName SourceFName = SourceChain.IsEmpty() ? NAME_None : FName(*SourceChain);
    const FName TargetFName = FName(*TargetChain);
    const bool bOK = Ctrl->SetSourceChain(SourceFName, TargetFName, OpName);
    if (!bOK)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("SetSourceChain failed: source='%s' target='%s' op='%s' (chain may not exist on the op's IKRig)"),
                *SourceChain, *TargetChain, *OpName.ToString()));
    }

    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Result->SetNumberField(TEXT("op_index"), OpIndex);
    Result->SetStringField(TEXT("op_name"), OpName.ToString());
    Result->SetStringField(TEXT("source_chain_name"), SourceChain);
    Result->SetStringField(TEXT("target_chain_name"), TargetChain);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleIKRetargeterAutoMapChains(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, Mode;
    int32 OpIndex = INDEX_NONE;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetNumberField(TEXT("op_index"), OpIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'op_index' parameter"));
    if (!Params->TryGetStringField(TEXT("mode"), Mode))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'mode' parameter"));

    // Map combined-mode strings (per requirement doc) → (EAutoMapChainType, bForceRemap).
    EAutoMapChainType MapType = EAutoMapChainType::Exact;
    bool bForceRemap = false;
    if      (Mode.Equals(TEXT("MapAllExact"),       ESearchCase::IgnoreCase)) { MapType = EAutoMapChainType::Exact; bForceRemap = true;  }
    else if (Mode.Equals(TEXT("MapOnlyEmptyExact"), ESearchCase::IgnoreCase)) { MapType = EAutoMapChainType::Exact; bForceRemap = false; }
    else if (Mode.Equals(TEXT("MapAllFuzzy"),       ESearchCase::IgnoreCase)) { MapType = EAutoMapChainType::Fuzzy; bForceRemap = true;  }
    else if (Mode.Equals(TEXT("MapOnlyEmptyFuzzy"), ESearchCase::IgnoreCase)) { MapType = EAutoMapChainType::Fuzzy; bForceRemap = false; }
    else if (Mode.Equals(TEXT("ClearAll"),          ESearchCase::IgnoreCase)) { MapType = EAutoMapChainType::Clear; bForceRemap = true;  }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Invalid mode '%s' (expected MapAllExact / MapOnlyEmptyExact / MapAllFuzzy / MapOnlyEmptyFuzzy / ClearAll)"), *Mode));
    }

    FString Err;
    UIKRetargeterController* Ctrl = GetIKRetargeterControllerForPath(AssetPath, Err);
    if (!Ctrl) return FUnrealMCPCommonUtils::CreateErrorResponse(Err);

    const FName OpName = Ctrl->GetOpName(OpIndex);
    if (OpName.IsNone())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("No op found at index %d"), OpIndex));
    }

    Ctrl->AutoMapChains(MapType, bForceRemap, OpName);
    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Result->SetNumberField(TEXT("op_index"), OpIndex);
    Result->SetStringField(TEXT("op_name"), OpName.ToString());
    Result->SetStringField(TEXT("mode"), Mode);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetIKRetargeterRetargetPose(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, Side, BoneName;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetStringField(TEXT("side"), Side))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'side' parameter (expected 'Source' or 'Target')"));
    if (!Params->TryGetStringField(TEXT("bone_name"), BoneName))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'bone_name' parameter"));

    ERetargetSourceOrTarget SideEnum;
    FString SideErr;
    if (!ParseSourceOrTarget(Side, SideEnum, SideErr))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(SideErr);
    }

    // Accept either rotation_quat=[x,y,z,w] (preferred) or rotation_euler=[pitch,yaw,roll].
    FQuat Offset = FQuat::Identity;
    bool bGotRotation = false;
    const TArray<TSharedPtr<FJsonValue>>* QuatArr = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* EulerArr = nullptr;
    if (Params->TryGetArrayField(TEXT("rotation_quat"), QuatArr) && QuatArr->Num() == 4)
    {
        Offset = FQuat(
            (*QuatArr)[0]->AsNumber(),
            (*QuatArr)[1]->AsNumber(),
            (*QuatArr)[2]->AsNumber(),
            (*QuatArr)[3]->AsNumber());
        bGotRotation = true;
    }
    else if (Params->TryGetArrayField(TEXT("rotation_euler"), EulerArr) && EulerArr->Num() == 3)
    {
        const FRotator Rot(
            (*EulerArr)[0]->AsNumber(), // Pitch
            (*EulerArr)[1]->AsNumber(), // Yaw
            (*EulerArr)[2]->AsNumber()); // Roll
        Offset = Rot.Quaternion();
        bGotRotation = true;
    }

    if (!bGotRotation)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Provide either 'rotation_quat'=[x,y,z,w] or 'rotation_euler'=[pitch,yaw,roll]"));
    }

    FString Err;
    UIKRetargeterController* Ctrl = GetIKRetargeterControllerForPath(AssetPath, Err);
    if (!Ctrl) return FUnrealMCPCommonUtils::CreateErrorResponse(Err);

    Ctrl->SetRotationOffsetForRetargetPoseBone(FName(*BoneName), Offset, SideEnum);
    Ctrl->GetAsset()->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Ctrl->GetAsset()->GetPathName());
    Result->SetStringField(TEXT("side"), Side);
    Result->SetStringField(TEXT("bone_name"), BoneName);
    TArray<TSharedPtr<FJsonValue>> AppliedQuat = {
        MakeShared<FJsonValueNumber>(Offset.X),
        MakeShared<FJsonValueNumber>(Offset.Y),
        MakeShared<FJsonValueNumber>(Offset.Z),
        MakeShared<FJsonValueNumber>(Offset.W)
    };
    Result->SetArrayField(TEXT("applied_quat"), AppliedQuat);
    Result->SetStringField(TEXT("note"), TEXT("Always operates on current retarget pose. Switch active pose first via SetCurrentRetargetPose if you need a different target."));
    return Result;
}

// =============================================================================
// Batch D (P1/P2/P3): Chooser Table write tools
// (top-level config + parameters + columns + row-level extensions)
// =============================================================================

namespace
{
    UChooserTable* LoadChooserTableOrError(const FString& AssetPath, TSharedPtr<FJsonObject>& OutError)
    {
        UChooserTable* Table = LoadObject<UChooserTable>(nullptr, *AssetPath);
        if (!Table)
        {
            OutError = FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("ChooserTable asset not found: %s"), *AssetPath));
        }
        return Table;
    }

    bool ParseChooserResultType(const FString& InStr, EObjectChooserResultType& Out, FString& OutError)
    {
        if (InStr.Equals(TEXT("ObjectResult"), ESearchCase::IgnoreCase)
            || InStr.Equals(TEXT("Object Of Type"), ESearchCase::IgnoreCase)
            || InStr.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
        {
            Out = EObjectChooserResultType::ObjectResult; return true;
        }
        if (InStr.Equals(TEXT("ClassResult"), ESearchCase::IgnoreCase)
            || InStr.Equals(TEXT("SubClass Of"), ESearchCase::IgnoreCase)
            || InStr.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
        {
            Out = EObjectChooserResultType::ClassResult; return true;
        }
        if (InStr.Equals(TEXT("NoPrimaryResult"), ESearchCase::IgnoreCase)
            || InStr.Equals(TEXT("None"), ESearchCase::IgnoreCase))
        {
            Out = EObjectChooserResultType::NoPrimaryResult; return true;
        }
        OutError = FString::Printf(TEXT("Invalid result_type '%s' (expected ObjectResult / ClassResult / NoPrimaryResult)"), *InStr);
        return false;
    }

    bool ParseContextObjectDirection(const FString& InStr, EContextObjectDirection& Out, FString& OutError)
    {
        if (InStr.Equals(TEXT("Input"), ESearchCase::IgnoreCase) || InStr.Equals(TEXT("Read"), ESearchCase::IgnoreCase))
        {
            Out = EContextObjectDirection::Read; return true;
        }
        if (InStr.Equals(TEXT("Output"), ESearchCase::IgnoreCase) || InStr.Equals(TEXT("Write"), ESearchCase::IgnoreCase))
        {
            Out = EContextObjectDirection::Write; return true;
        }
        if (InStr.Equals(TEXT("InputOutput"), ESearchCase::IgnoreCase)
            || InStr.Equals(TEXT("Input/Output"), ESearchCase::IgnoreCase)
            || InStr.Equals(TEXT("ReadWrite"), ESearchCase::IgnoreCase))
        {
            Out = EContextObjectDirection::ReadWrite; return true;
        }
        OutError = FString::Printf(TEXT("Invalid direction '%s' (expected Input / Output / InputOutput)"), *InStr);
        return false;
    }

    // Resolve "FooColumn" / "FFooColumn" / "/Script/Chooser.FooColumn" → UScriptStruct*.
    UScriptStruct* ResolveColumnStruct(const FString& InName)
    {
        if (InName.StartsWith(TEXT("/Script/")))
        {
            return FindObject<UScriptStruct>(nullptr, *InName);
        }
        return FindFirstObject<UScriptStruct>(*InName, EFindFirstObjectOptions::None, ELogVerbosity::Warning);
    }

    // Write PropertyBindingChain + ContextIndex on the Binding sub-struct of a column's
    // InputValue FInstancedStruct. Splits `PathAsText` on "." to build the chain.
    // Optional EnumToSet sets FChooserEnumPropertyBinding::Enum for Enum columns.
    bool ApplyBindingToColumn(FInstancedStruct& ColInst, const FString& PathAsText, int32 ContextIndex, UEnum* EnumToSet, FString& OutError)
    {
        const UScriptStruct* ColStruct = ColInst.GetScriptStruct();
        if (!ColStruct || !ColInst.IsValid())
        {
            OutError = TEXT("Invalid column InstancedStruct");
            return false;
        }
        FStructProperty* InputValueProp = FindFProperty<FStructProperty>(ColStruct, TEXT("InputValue"));
        if (!InputValueProp)
        {
            OutError = FString::Printf(TEXT("Column '%s' has no InputValue property"), *ColStruct->GetName());
            return false;
        }
        FInstancedStruct* InputValue = InputValueProp->ContainerPtrToValuePtr<FInstancedStruct>(ColInst.GetMutableMemory());
        if (!InputValue || !InputValue->IsValid())
        {
            OutError = TEXT("Column InputValue is not initialized");
            return false;
        }
        const UScriptStruct* ParamStruct = InputValue->GetScriptStruct();
        if (!ParamStruct)
        {
            OutError = TEXT("Column InputValue has no ScriptStruct");
            return false;
        }
        FStructProperty* BindingProp = FindFProperty<FStructProperty>(ParamStruct, TEXT("Binding"));
        if (!BindingProp || !BindingProp->Struct)
        {
            OutError = FString::Printf(TEXT("Parameter struct '%s' has no Binding property"), *ParamStruct->GetName());
            return false;
        }
        FChooserPropertyBinding* Binding = static_cast<FChooserPropertyBinding*>(
            BindingProp->ContainerPtrToValuePtr<void>(InputValue->GetMutableMemory()));

        Binding->PropertyBindingChain.Reset();
        TArray<FString> Segments;
        PathAsText.ParseIntoArray(Segments, TEXT("."), true);
        for (const FString& Seg : Segments)
        {
            Binding->PropertyBindingChain.Add(FName(*Seg));
        }
        Binding->ContextIndex = ContextIndex;
        Binding->IsBoundToRoot = false;
#if WITH_EDITORONLY_DATA
        Binding->DisplayName = PathAsText;
#endif

#if WITH_EDITORONLY_DATA
        if (EnumToSet && BindingProp->Struct->IsChildOf(FChooserEnumPropertyBinding::StaticStruct()))
        {
            FChooserEnumPropertyBinding* EB = static_cast<FChooserEnumPropertyBinding*>(
                BindingProp->ContainerPtrToValuePtr<void>(InputValue->GetMutableMemory()));
            EB->Enum = EnumToSet;
        }
#endif
        return true;
    }

#if WITH_EDITOR
    // When a write touches the Chooser, we need to trigger its Compile to re-propagate
    // bindings and keep the asset consistent. Also keep RowValues arrays sized to Results.
    void PostWriteChooserCompile(UChooserTable* Table)
    {
        if (!Table) return;
        Table->Compile(/*bForce=*/true);
    }

    // Resize every column's RowValues array to match Results count (editor-only code path
    // that the Chooser editor normally runs). Needed after add_chooser_table_column and
    // after add/remove row to keep per-column row arrays consistent.
    void SyncChooserColumnRowCounts(UChooserTable* Table)
    {
        if (!Table) return;
        const int32 NumRows = Table->ResultsStructs.Num();
        for (FInstancedStruct& ColInst : Table->ColumnsStructs)
        {
            if (!ColInst.IsValid()) continue;
            ColInst.GetMutable<FChooserColumnBase>().SetNumRows(NumRows);
        }
    }
#endif
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetChooserTableResult(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, ResultTypeStr, ResultClassPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetStringField(TEXT("result_type"), ResultTypeStr))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'result_type' parameter"));
    Params->TryGetStringField(TEXT("result_class_path"), ResultClassPath); // optional

    TSharedPtr<FJsonObject> Err;
    UChooserTable* Table = LoadChooserTableOrError(AssetPath, Err);
    if (!Table) return Err;

    EObjectChooserResultType NewType;
    FString ParseErr;
    if (!ParseChooserResultType(ResultTypeStr, NewType, ParseErr))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ParseErr);
    }
    Table->ResultType = NewType;

    UClass* NewClass = nullptr;
    if (!ResultClassPath.IsEmpty())
    {
        NewClass = LoadObject<UClass>(nullptr, *ResultClassPath);
        if (!NewClass)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("result_class_path not found: %s"), *ResultClassPath));
        }
    }
    Table->OutputObjectType = NewClass;

#if WITH_EDITOR
    PostWriteChooserCompile(Table);
#endif
    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Result->SetStringField(TEXT("result_type"), ResultTypeStr);
    Result->SetStringField(TEXT("result_class_path"), ResultClassPath);
    Result->SetStringField(TEXT("note"), TEXT("Changing ResultType invalidates existing row Results — verify rows afterwards."));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetChooserTableFallbackResult(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, ResultAssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    Params->TryGetStringField(TEXT("result_asset_path"), ResultAssetPath); // empty → clear

    TSharedPtr<FJsonObject> Err;
    UChooserTable* Table = LoadChooserTableOrError(AssetPath, Err);
    if (!Table) return Err;

    if (ResultAssetPath.IsEmpty())
    {
        Table->FallbackResult.Reset();
    }
    else
    {
        UObject* Asset = LoadObject<UObject>(nullptr, *ResultAssetPath);
        if (!Asset)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Fallback result asset not found: %s"), *ResultAssetPath));
        }
        FAssetChooser NewFallback;
        NewFallback.Asset = Asset;
        Table->FallbackResult.InitializeAs(FAssetChooser::StaticStruct(), reinterpret_cast<const uint8*>(&NewFallback));
    }

#if WITH_EDITOR
    PostWriteChooserCompile(Table);
#endif
    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Result->SetStringField(TEXT("result_asset_path"), ResultAssetPath);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleAddChooserTableParameter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, Kind, ClassOrStructPath, DirectionStr;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetStringField(TEXT("kind"), Kind))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'kind' parameter (expected 'class' or 'struct')"));
    if (!Params->TryGetStringField(TEXT("class_or_struct_path"), ClassOrStructPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'class_or_struct_path' parameter"));
    Params->TryGetStringField(TEXT("direction"), DirectionStr);
    if (DirectionStr.IsEmpty()) DirectionStr = TEXT("Input");

    TSharedPtr<FJsonObject> Err;
    UChooserTable* Table = LoadChooserTableOrError(AssetPath, Err);
    if (!Table) return Err;

    EContextObjectDirection Dir;
    FString DirErr;
    if (!ParseContextObjectDirection(DirectionStr, Dir, DirErr))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(DirErr);
    }

    FInstancedStruct NewParam;
    if (Kind.Equals(TEXT("class"), ESearchCase::IgnoreCase))
    {
        UClass* TargetClass = LoadObject<UClass>(nullptr, *ClassOrStructPath);
        if (!TargetClass)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("class not found: %s"), *ClassOrStructPath));
        }
        FContextObjectTypeClass ParamVal;
        ParamVal.Class = TargetClass;
        ParamVal.Direction = Dir;
        NewParam.InitializeAs(FContextObjectTypeClass::StaticStruct(), reinterpret_cast<const uint8*>(&ParamVal));
    }
    else if (Kind.Equals(TEXT("struct"), ESearchCase::IgnoreCase))
    {
        UScriptStruct* TargetStruct = FindObject<UScriptStruct>(nullptr, *ClassOrStructPath);
        if (!TargetStruct)
        {
            TargetStruct = FindFirstObject<UScriptStruct>(*ClassOrStructPath, EFindFirstObjectOptions::None, ELogVerbosity::Warning);
        }
        if (!TargetStruct)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("struct not found: %s"), *ClassOrStructPath));
        }
        FContextObjectTypeStruct ParamVal;
        ParamVal.Struct = TargetStruct;
        ParamVal.Direction = Dir;
        NewParam.InitializeAs(FContextObjectTypeStruct::StaticStruct(), reinterpret_cast<const uint8*>(&ParamVal));
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Invalid 'kind' value: %s (expected 'class' or 'struct')"), *Kind));
    }

    const int32 NewIndex = Table->ContextData.Add(MoveTemp(NewParam));

#if WITH_EDITOR
    PostWriteChooserCompile(Table);
#endif
    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Result->SetNumberField(TEXT("parameter_index"), NewIndex);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleRemoveChooserTableParameter(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    int32 ParamIndex = INDEX_NONE;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetNumberField(TEXT("parameter_index"), ParamIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'parameter_index' parameter"));

    TSharedPtr<FJsonObject> Err;
    UChooserTable* Table = LoadChooserTableOrError(AssetPath, Err);
    if (!Table) return Err;

    if (!Table->ContextData.IsValidIndex(ParamIndex))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("parameter_index %d out of range [0..%d)"), ParamIndex, Table->ContextData.Num()));
    }
    Table->ContextData.RemoveAt(ParamIndex);

#if WITH_EDITOR
    PostWriteChooserCompile(Table);
#endif
    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Result->SetNumberField(TEXT("removed_parameter_index"), ParamIndex);
    Result->SetNumberField(TEXT("parameter_count"), Table->ContextData.Num());
    Result->SetStringField(TEXT("note"), TEXT("Column bindings referencing this parameter may now be invalid — inspect with get_chooser_table_info and fix via set_chooser_table_column_binding."));
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleAddChooserTableColumn(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, ColumnType, PathAsText, EnumPath;
    int32 ContextIndex = 0;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetStringField(TEXT("column_type"), ColumnType))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'column_type' parameter (e.g. EnumColumn / BoolColumn / OutputBoolColumn)"));
    Params->TryGetStringField(TEXT("binding_path_as_text"), PathAsText);
    Params->TryGetNumberField(TEXT("context_index"), ContextIndex);
    Params->TryGetStringField(TEXT("enum_path"), EnumPath); // optional: for EnumColumn/MultiEnumColumn

    TSharedPtr<FJsonObject> Err;
    UChooserTable* Table = LoadChooserTableOrError(AssetPath, Err);
    if (!Table) return Err;

    UScriptStruct* ColStruct = ResolveColumnStruct(ColumnType);
    if (!ColStruct)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Unknown column type '%s' (pass bare struct name or /Script/Chooser.<name>)"), *ColumnType));
    }
    if (!ColStruct->IsChildOf(FChooserColumnBase::StaticStruct()))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Struct '%s' is not a FChooserColumnBase subclass"), *ColStruct->GetName()));
    }

    UEnum* EnumToSet = nullptr;
    if (!EnumPath.IsEmpty())
    {
        EnumToSet = LoadObject<UEnum>(nullptr, *EnumPath);
        if (!EnumToSet)
        {
            EnumToSet = FindFirstObject<UEnum>(*EnumPath, EFindFirstObjectOptions::None, ELogVerbosity::Warning);
        }
        if (!EnumToSet)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("enum_path not found: %s"), *EnumPath));
        }
    }

    FInstancedStruct NewCol;
    NewCol.InitializeAs(ColStruct);

#if WITH_EDITOR
    // CHOOSER_COLUMN_BOILERPLATE sets up the default InputValue ParamStruct
    // inside the column's constructor. Apply binding if a path was provided.
    FChooserColumnBase& ColBase = NewCol.GetMutable<FChooserColumnBase>();

    // Ensure the column's InputValue has a default parameter struct initialized.
    // CHOOSER_COLUMN_BOILERPLATE's SetInputType defaults to ParameterType::StaticStruct,
    // which the concrete column subclass's GetInputBaseType returns. The column
    // constructor already called this, but we play safe in case the InputValue is empty.
    if (UScriptStruct* BaseType = ColBase.GetInputBaseType())
    {
        FStructProperty* InputValueProp = FindFProperty<FStructProperty>(ColStruct, TEXT("InputValue"));
        if (InputValueProp)
        {
            FInstancedStruct* InputValue = InputValueProp->ContainerPtrToValuePtr<FInstancedStruct>(NewCol.GetMutableMemory());
            if (InputValue && !InputValue->IsValid())
            {
                InputValue->InitializeAs(BaseType);
            }
        }
    }
#endif

    if (!PathAsText.IsEmpty())
    {
        FString ApplyErr;
        if (!ApplyBindingToColumn(NewCol, PathAsText, ContextIndex, EnumToSet, ApplyErr))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("ApplyBindingToColumn failed: %s"), *ApplyErr));
        }
    }

    const int32 NewIndex = Table->ColumnsStructs.Add(MoveTemp(NewCol));

#if WITH_EDITOR
    // Size the new column's RowValues array to match current row count.
    SyncChooserColumnRowCounts(Table);
    PostWriteChooserCompile(Table);
#endif
    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Result->SetNumberField(TEXT("column_index"), NewIndex);
    Result->SetStringField(TEXT("column_struct"), ColStruct->GetName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleRemoveChooserTableColumn(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    int32 ColumnIndex = INDEX_NONE;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetNumberField(TEXT("column_index"), ColumnIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'column_index' parameter"));

    TSharedPtr<FJsonObject> Err;
    UChooserTable* Table = LoadChooserTableOrError(AssetPath, Err);
    if (!Table) return Err;

    if (!Table->ColumnsStructs.IsValidIndex(ColumnIndex))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("column_index %d out of range [0..%d)"), ColumnIndex, Table->ColumnsStructs.Num()));
    }
    Table->ColumnsStructs.RemoveAt(ColumnIndex);

#if WITH_EDITOR
    PostWriteChooserCompile(Table);
#endif
    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Result->SetNumberField(TEXT("removed_column_index"), ColumnIndex);
    Result->SetNumberField(TEXT("column_count"), Table->ColumnsStructs.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetChooserTableColumnBinding(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, PathAsText, EnumPath;
    int32 ColumnIndex = INDEX_NONE;
    int32 ContextIndex = 0;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetNumberField(TEXT("column_index"), ColumnIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'column_index' parameter"));
    if (!Params->TryGetStringField(TEXT("binding_path_as_text"), PathAsText))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'binding_path_as_text' parameter"));
    Params->TryGetNumberField(TEXT("context_index"), ContextIndex);
    Params->TryGetStringField(TEXT("enum_path"), EnumPath);

    TSharedPtr<FJsonObject> Err;
    UChooserTable* Table = LoadChooserTableOrError(AssetPath, Err);
    if (!Table) return Err;

    if (!Table->ColumnsStructs.IsValidIndex(ColumnIndex))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("column_index %d out of range [0..%d)"), ColumnIndex, Table->ColumnsStructs.Num()));
    }

    UEnum* EnumToSet = nullptr;
    if (!EnumPath.IsEmpty())
    {
        EnumToSet = LoadObject<UEnum>(nullptr, *EnumPath);
        if (!EnumToSet) EnumToSet = FindFirstObject<UEnum>(*EnumPath, EFindFirstObjectOptions::None, ELogVerbosity::Warning);
        if (!EnumToSet)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("enum_path not found: %s"), *EnumPath));
        }
    }

    FString ApplyErr;
    if (!ApplyBindingToColumn(Table->ColumnsStructs[ColumnIndex], PathAsText, ContextIndex, EnumToSet, ApplyErr))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ApplyErr);
    }

#if WITH_EDITOR
    PostWriteChooserCompile(Table);
#endif
    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Result->SetNumberField(TEXT("column_index"), ColumnIndex);
    Result->SetStringField(TEXT("binding_path_as_text"), PathAsText);
    Result->SetNumberField(TEXT("context_index"), ContextIndex);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetChooserTableRowResult(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, ResultAssetPath;
    int32 RowIndex = INDEX_NONE;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetNumberField(TEXT("row_index"), RowIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'row_index' parameter"));
    if (!Params->TryGetStringField(TEXT("result_asset_path"), ResultAssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'result_asset_path' parameter"));

    TSharedPtr<FJsonObject> Err;
    UChooserTable* Table = LoadChooserTableOrError(AssetPath, Err);
    if (!Table) return Err;

#if WITH_EDITORONLY_DATA
    if (!Table->ResultsStructs.IsValidIndex(RowIndex))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("row_index %d out of range [0..%d)"), RowIndex, Table->ResultsStructs.Num()));
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *ResultAssetPath);
    if (!Asset)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Result asset not found: %s"), *ResultAssetPath));
    }

    FAssetChooser NewResult;
    NewResult.Asset = Asset;
    Table->ResultsStructs[RowIndex].InitializeAs(FAssetChooser::StaticStruct(), reinterpret_cast<const uint8*>(&NewResult));

#if WITH_EDITOR
    PostWriteChooserCompile(Table);
#endif
    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Result->SetNumberField(TEXT("row_index"), RowIndex);
    Result->SetStringField(TEXT("result_asset_path"), Asset->GetPathName());
    return Result;
#else
    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ResultsStructs is editor-only; cannot edit rows in non-editor build"));
#endif
}

TSharedPtr<FJsonObject> FUnrealMCPAnimationCommands::HandleSetChooserTableRowColumnValue(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath, Value;
    int32 RowIndex = INDEX_NONE;
    int32 ColumnIndex = INDEX_NONE;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    if (!Params->TryGetNumberField(TEXT("row_index"), RowIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'row_index' parameter"));
    if (!Params->TryGetNumberField(TEXT("column_index"), ColumnIndex))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'column_index' parameter"));
    if (!Params->TryGetStringField(TEXT("value"), Value))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'value' parameter (T3D text form — see set_ik_retargeter_op_field docstring)"));

    TSharedPtr<FJsonObject> Err;
    UChooserTable* Table = LoadChooserTableOrError(AssetPath, Err);
    if (!Table) return Err;

    if (!Table->ColumnsStructs.IsValidIndex(ColumnIndex))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("column_index %d out of range [0..%d)"), ColumnIndex, Table->ColumnsStructs.Num()));
    }

    FInstancedStruct& ColInst = Table->ColumnsStructs[ColumnIndex];
    const UScriptStruct* ColStruct = ColInst.GetScriptStruct();
    if (!ColStruct || !ColInst.IsValid())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Column not initialized"));
    }

#if WITH_EDITOR
    const FName RowValsName = ColInst.GetMutable<FChooserColumnBase>().RowValuesPropertyName();
    FArrayProperty* RowValsProp = FindFProperty<FArrayProperty>(ColStruct, RowValsName);
#else
    FArrayProperty* RowValsProp = FindFProperty<FArrayProperty>(ColStruct, TEXT("RowValues"));
#endif
    if (!RowValsProp || !RowValsProp->Inner)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Column '%s' has no RowValues array property"), *ColStruct->GetName()));
    }

    FScriptArrayHelper ArrayHelper(RowValsProp, RowValsProp->ContainerPtrToValuePtr<void>(ColInst.GetMutableMemory()));
    if (!ArrayHelper.IsValidIndex(RowIndex))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("row_index %d out of range for column RowValues [0..%d)"), RowIndex, ArrayHelper.Num()));
    }

    void* ElemPtr = ArrayHelper.GetRawPtr(RowIndex);
    const TCHAR* ImportResult = RowValsProp->Inner->ImportText_Direct(*Value, ElemPtr, /*OwnerObject=*/nullptr, PPF_None, GLog);
    if (!ImportResult)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("ImportText_Direct failed for column %d row %d (value='%s', element type=%s)"),
                ColumnIndex, RowIndex, *Value, *RowValsProp->Inner->GetCPPType()));
    }

#if WITH_EDITOR
    PostWriteChooserCompile(Table);
#endif
    Table->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), Table->GetPathName());
    Result->SetNumberField(TEXT("row_index"), RowIndex);
    Result->SetNumberField(TEXT("column_index"), ColumnIndex);
    return Result;
}
