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
#include "AnimationBlueprintLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "UObject/Package.h"
#include "StructUtils/InstancedStruct.h"
#include "Chooser.h"
#include "IObjectChooser.h"
#include "IChooserColumn.h"
#include "ObjectChooser_Asset.h"
#include "Rig/IKRigDefinition.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetProfile.h"
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
    if (CommandType == TEXT("add_chooser_table_row"))
    {
        return HandleAddChooserTableRow(Params);
    }
    if (CommandType == TEXT("remove_chooser_table_row"))
    {
        return HandleRemoveChooserTableRow(Params);
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

    // Columns: one entry per ColumnsStructs slot. We output the column's struct name
    // and (editor-only) the reflected RowValues property name so callers can tell
    // which column kind it is without loading the Chooser module themselves.
    TArray<TSharedPtr<FJsonValue>> ColumnsJson;
    ColumnsJson.Reserve(Table->ColumnsStructs.Num());
    for (int32 i = 0; i < Table->ColumnsStructs.Num(); ++i)
    {
        FInstancedStruct& ColInst = Table->ColumnsStructs[i];
        const UScriptStruct* ColStruct = ColInst.GetScriptStruct();

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("struct_name"), ColStruct ? ColStruct->GetName() : FString());

#if WITH_EDITOR
        if (ColStruct && ColInst.IsValid())
        {
            FChooserColumnBase& ColBase = ColInst.GetMutable<FChooserColumnBase>();
            Entry->SetStringField(TEXT("row_values_property"), ColBase.RowValuesPropertyName().ToString());
        }
#endif

        ColumnsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetNumberField(TEXT("column_count"), Table->ColumnsStructs.Num());
    Result->SetArrayField(TEXT("columns"), ColumnsJson);

    // Results: ResultsStructs carries the authored row order (editor-only). For a
    // cooked asset that array is gone and CookedResults takes over, so fall through
    // to it when the authored data is empty.
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
        const FInstancedStruct& ResultInst = ResultsView[i];
        const UScriptStruct* RStruct = ResultInst.GetScriptStruct();

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
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

        ResultsJson.Add(MakeShared<FJsonValueObject>(Entry));
    }

    Result->SetNumberField(TEXT("row_count"), ResultsView.Num());
    Result->SetBoolField(TEXT("used_cooked_results"), bUsedCooked);
    Result->SetArrayField(TEXT("results"), ResultsJson);
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

    // Solvers: SolverStack is FInstancedStruct; emit the solver struct name (e.g. FIKRigFBIKSolver, FIKRigLimbSolver)
    // without trying to introspect each solver's parameters — same trade-off as ChooserTable columns.
    const TArray<FInstancedStruct>& Solvers = Rig->GetSolverStructs();
    TArray<TSharedPtr<FJsonValue>> SolversJson;
    SolversJson.Reserve(Solvers.Num());
    for (int32 i = 0; i < Solvers.Num(); ++i)
    {
        const UScriptStruct* SStruct = Solvers[i].GetScriptStruct();
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("struct_name"), SStruct ? SStruct->GetName() : FString());
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

    // Retarget op stack — UE5.7 stores chain mapping inside FInstancedStruct ops
    // (FK Chains Op / IK Chains Op / Pelvis Motion Op / etc), so we surface the op
    // struct names but do not introspect their internal chain mapping yet.
    const TArray<FInstancedStruct>& Ops = Retargeter->GetRetargetOps();
    TArray<TSharedPtr<FJsonValue>> OpsJson;
    OpsJson.Reserve(Ops.Num());
    for (int32 i = 0; i < Ops.Num(); ++i)
    {
        const UScriptStruct* OStruct = Ops[i].GetScriptStruct();
        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("index"), i);
        Entry->SetStringField(TEXT("struct_name"), OStruct ? OStruct->GetName() : FString());
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
