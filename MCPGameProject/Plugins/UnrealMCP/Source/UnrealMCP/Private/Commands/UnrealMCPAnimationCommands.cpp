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
    if (CommandType == TEXT("find_animations_for_skeleton"))
    {
        return HandleFindAnimationsForSkeleton(Params);
    }
    if (CommandType == TEXT("get_skeleton_bone_hierarchy"))
    {
        return HandleGetSkeletonBoneHierarchy(Params);
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
