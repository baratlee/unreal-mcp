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
