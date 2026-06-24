#include "Commands/UnrealMCPEditorCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "ImageUtils.h"
#include "HighResScreenshot.h"
#include "Engine/GameViewportClient.h"
#include "Misc/FileHelper.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
// Batch E: P1 — for save_dirty_assets / delete_asset
#include "EditorAssetLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
// LT16 2026-06-12 — spawn_blueprint_actor 任意路径支持：AssetRegistry 短名查找
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
// Phase D 2026-06-24 — PIE 控制 / 关卡切换 / Actor 组件属性写入
#include "LevelEditor.h"
#include "LevelEditorSubsystem.h"
#include "EngineUtils.h"

FUnrealMCPEditorCommands::FUnrealMCPEditorCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    // Actor manipulation commands
    if (CommandType == TEXT("get_actors_in_level"))
    {
        return HandleGetActorsInLevel(Params);
    }
    else if (CommandType == TEXT("find_actors_by_name"))
    {
        return HandleFindActorsByName(Params);
    }
    else if (CommandType == TEXT("spawn_actor") || CommandType == TEXT("create_actor"))
    {
        if (CommandType == TEXT("create_actor"))
        {
            UE_LOG(LogTemp, Warning, TEXT("'create_actor' command is deprecated and will be removed in a future version. Please use 'spawn_actor' instead."));
        }
        return HandleSpawnActor(Params);
    }
    else if (CommandType == TEXT("delete_actor"))
    {
        return HandleDeleteActor(Params);
    }
    else if (CommandType == TEXT("set_actor_transform"))
    {
        return HandleSetActorTransform(Params);
    }
    else if (CommandType == TEXT("get_actor_properties"))
    {
        return HandleGetActorProperties(Params);
    }
    else if (CommandType == TEXT("set_actor_property"))
    {
        return HandleSetActorProperty(Params);
    }
    // Blueprint actor spawning
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    // Editor viewport commands
    else if (CommandType == TEXT("focus_viewport"))
    {
        return HandleFocusViewport(Params);
    }
    else if (CommandType == TEXT("take_screenshot"))
    {
        return HandleTakeScreenshot(Params);
    }
    // Asset inspection commands
    else if (CommandType == TEXT("get_static_mesh_info"))
    {
        return HandleGetStaticMeshInfo(Params);
    }
    // Batch E: P1 — asset persistence & deletion
    else if (CommandType == TEXT("save_dirty_assets"))
    {
        return HandleSaveDirtyAssets(Params);
    }
    else if (CommandType == TEXT("delete_asset"))
    {
        return HandleDeleteAsset(Params);
    }
    else if (CommandType == TEXT("start_pie"))
    {
        return HandleStartPIE(Params);
    }
    else if (CommandType == TEXT("stop_pie"))
    {
        return HandleStopPIE(Params);
    }
    else if (CommandType == TEXT("open_level"))
    {
        return HandleOpenLevel(Params);
    }
    else if (CommandType == TEXT("set_actor_component_property"))
    {
        return HandleSetActorComponentProperty(Params);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown editor command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params)
{
    TArray<TSharedPtr<FJsonValue>> ActorArray;
    UWorld* World = GWorld;
    if (!World)
    {
        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetArrayField(TEXT("actors"), ActorArray);
        return ResultObj;
    }

    UWorldPartition* WorldPartition = World->GetWorldPartition();
    if (WorldPartition)
    {
        TSet<FName> SeenActorNames;
        FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, AActor::StaticClass(),
            [&ActorArray, &SeenActorNames](const FWorldPartitionActorDescInstance* ActorDescInstance) -> bool
            {
                if (!ActorDescInstance) return true;

                FName ActorName = ActorDescInstance->GetActorName();
                SeenActorNames.Add(ActorName);

                if (ActorDescInstance->IsLoaded())
                {
                    AActor* Actor = ActorDescInstance->GetActor();
                    if (Actor)
                    {
                        ActorArray.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
                        return true;
                    }
                }

                // Unloaded actor: extract metadata from descriptor
                TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
                ActorObject->SetStringField(TEXT("name"), ActorDescInstance->GetActorLabelOrName().ToString());

                UClass* NativeClass = ActorDescInstance->GetActorNativeClass();
                ActorObject->SetStringField(TEXT("class"), NativeClass ? NativeClass->GetName() : TEXT("Unknown"));

                const FTransform& Transform = ActorDescInstance->GetActorTransform();
                FVector Location = Transform.GetLocation();
                FRotator Rotation = Transform.GetRotation().Rotator();
                FVector Scale = Transform.GetScale3D();

                TArray<TSharedPtr<FJsonValue>> LocationArray;
                LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
                LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
                LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
                ActorObject->SetArrayField(TEXT("location"), LocationArray);

                TArray<TSharedPtr<FJsonValue>> RotationArray;
                RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
                RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
                RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
                ActorObject->SetArrayField(TEXT("rotation"), RotationArray);

                TArray<TSharedPtr<FJsonValue>> ScaleArray;
                ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.X));
                ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Y));
                ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Z));
                ActorObject->SetArrayField(TEXT("scale"), ScaleArray);

                ActorObject->SetBoolField(TEXT("is_wp_unloaded"), true);

                ActorArray.Add(MakeShared<FJsonValueObject>(ActorObject));
                return true;
            });

        // Also pick up non-WP actors (WorldSettings, system actors, etc.)
        TArray<AActor*> RuntimeActors;
        UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), RuntimeActors);
        for (AActor* Actor : RuntimeActors)
        {
            if (Actor && !SeenActorNames.Contains(Actor->GetFName()))
            {
                ActorArray.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
            }
        }
    }
    else
    {
        // Non-World-Partition map: original behavior
        TArray<AActor*> AllActors;
        UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
        for (AActor* Actor : AllActors)
        {
            if (Actor)
            {
                ActorArray.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
            }
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), ActorArray);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params)
{
    FString Pattern;
    if (!Params->TryGetStringField(TEXT("pattern"), Pattern))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pattern' parameter"));
    }

    TArray<TSharedPtr<FJsonValue>> MatchingActors;
    UWorld* World = GWorld;
    if (!World)
    {
        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetArrayField(TEXT("actors"), MatchingActors);
        return ResultObj;
    }

    UWorldPartition* WorldPartition = World->GetWorldPartition();
    if (WorldPartition)
    {
        TSet<FName> SeenActorNames;
        FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, AActor::StaticClass(),
            [&MatchingActors, &SeenActorNames, &Pattern](const FWorldPartitionActorDescInstance* ActorDescInstance) -> bool
            {
                if (!ActorDescInstance) return true;

                FName ActorName = ActorDescInstance->GetActorName();
                FString DisplayName = ActorDescInstance->GetActorLabelOrName().ToString();
                FString InternalName = ActorName.ToString();
                SeenActorNames.Add(ActorName);

                if (!DisplayName.Contains(Pattern) && !InternalName.Contains(Pattern)) return true;

                if (ActorDescInstance->IsLoaded())
                {
                    AActor* Actor = ActorDescInstance->GetActor();
                    if (Actor)
                    {
                        MatchingActors.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
                        return true;
                    }
                }

                TSharedPtr<FJsonObject> ActorObject = MakeShared<FJsonObject>();
                ActorObject->SetStringField(TEXT("name"), DisplayName);

                UClass* NativeClass = ActorDescInstance->GetActorNativeClass();
                ActorObject->SetStringField(TEXT("class"), NativeClass ? NativeClass->GetName() : TEXT("Unknown"));

                const FTransform& Transform = ActorDescInstance->GetActorTransform();
                FVector Location = Transform.GetLocation();
                FRotator Rotation = Transform.GetRotation().Rotator();
                FVector Scale = Transform.GetScale3D();

                TArray<TSharedPtr<FJsonValue>> LocationArray;
                LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
                LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
                LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
                ActorObject->SetArrayField(TEXT("location"), LocationArray);

                TArray<TSharedPtr<FJsonValue>> RotationArray;
                RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
                RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
                RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
                ActorObject->SetArrayField(TEXT("rotation"), RotationArray);

                TArray<TSharedPtr<FJsonValue>> ScaleArray;
                ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.X));
                ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Y));
                ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Z));
                ActorObject->SetArrayField(TEXT("scale"), ScaleArray);

                ActorObject->SetBoolField(TEXT("is_wp_unloaded"), true);

                MatchingActors.Add(MakeShared<FJsonValueObject>(ActorObject));
                return true;
            });

        // Also check non-WP actors
        TArray<AActor*> RuntimeActors;
        UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), RuntimeActors);
        for (AActor* Actor : RuntimeActors)
        {
            if (Actor && !SeenActorNames.Contains(Actor->GetFName()) && Actor->GetName().Contains(Pattern))
            {
                MatchingActors.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
            }
        }
    }
    else
    {
        TArray<AActor*> AllActors;
        UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
        for (AActor* Actor : AllActors)
        {
            if (Actor && Actor->GetName().Contains(Pattern))
            {
                MatchingActors.Add(FUnrealMCPCommonUtils::ActorToJson(Actor));
            }
        }
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetArrayField(TEXT("actors"), MatchingActors);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSpawnActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString ActorType;
    if (!Params->TryGetStringField(TEXT("type"), ActorType))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    // Get actor name (required parameter)
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Get optional transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);
    FVector Scale(1.0f, 1.0f, 1.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    // Create the actor based on type
    AActor* NewActor = nullptr;
    UWorld* World = GEditor->GetEditorWorldContext().World();

    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    // Check if an actor with this name already exists
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor with name '%s' already exists"), *ActorName));
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;
    // [LEOCC · LT16 2026-06-12] 名字冲突时不要 fatal —— 返回 nullptr 让上层报错
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;

    if (ActorType == TEXT("StaticMeshActor"))
    {
        NewActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
        if (NewActor)
        {
            FString MeshPath;
            if (Params->TryGetStringField(TEXT("mesh"), MeshPath))
            {
                UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
                if (Mesh)
                {
                    AStaticMeshActor* SMActor = Cast<AStaticMeshActor>(NewActor);
                    SMActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
                }
            }
        }
    }
    else if (ActorType == TEXT("PointLight"))
    {
        NewActor = World->SpawnActor<APointLight>(APointLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("SpotLight"))
    {
        NewActor = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("DirectionalLight"))
    {
        NewActor = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Location, Rotation, SpawnParams);
    }
    else if (ActorType == TEXT("CameraActor"))
    {
        NewActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Location, Rotation, SpawnParams);
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown actor type: %s"), *ActorType));
    }

    if (NewActor)
    {
        // Set scale (since SpawnActor only takes location and rotation)
        FTransform Transform = NewActor->GetTransform();
        Transform.SetScale3D(Scale);
        NewActor->SetActorTransform(Transform);

#if WITH_EDITOR
        // [LEOCC · LT16 2026-06-12] 同步 outliner 显示名（必须显式 SetActorLabel）
        NewActor->SetActorLabel(ActorName);
#endif
        // Return the created actor's details
        return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
        TEXT("Failed to spawn actor '%s' — likely name conflict with an existing actor in this world"), *ActorName));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleDeleteActor(const TSharedPtr<FJsonObject>& Params)
{
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            // Store actor info before deletion for the response
            TSharedPtr<FJsonObject> ActorInfo = FUnrealMCPCommonUtils::ActorToJsonObject(Actor);
            
            // Delete the actor
            Actor->Destroy();
            
            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetObjectField(TEXT("deleted_actor"), ActorInfo);
            return ResultObj;
        }
    }
    
    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get transform parameters
    FTransform NewTransform = TargetActor->GetTransform();

    if (Params->HasField(TEXT("location")))
    {
        NewTransform.SetLocation(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        NewTransform.SetRotation(FQuat(FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
    }
    if (Params->HasField(TEXT("scale")))
    {
        NewTransform.SetScale3D(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
    }

    // Set the new transform
    TargetActor->SetActorTransform(NewTransform);

    // Return updated actor info
    return FUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetActorProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Always return detailed properties for this command
    return FUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params)
{
    // Get actor name
    FString ActorName;
    if (!Params->TryGetStringField(TEXT("name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the actor
    AActor* TargetActor = nullptr;
    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
    
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName() == ActorName)
        {
            TargetActor = Actor;
            break;
        }
    }

    if (!TargetActor)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
    }

    // Get property name
    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    // Get property value
    if (!Params->HasField(TEXT("property_value")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
    }
    
    TSharedPtr<FJsonValue> PropertyValue = Params->Values.FindRef(TEXT("property_value"));
    
    // Set the property using our utility function
    FString ErrorMessage;
    if (FUnrealMCPCommonUtils::SetObjectProperty(TargetActor, PropertyName, PropertyValue, ErrorMessage))
    {
        // Property set successfully
        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("actor"), ActorName);
        ResultObj->SetStringField(TEXT("property"), PropertyName);
        ResultObj->SetBoolField(TEXT("success"), true);
        
        // Also include the full actor details
        ResultObj->SetObjectField(TEXT("actor_details"), FUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true));
        return ResultObj;
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
    }
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ActorName;
    if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_name' parameter"));
    }

    // Find the blueprint
    if (BlueprintName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint name is empty"));
    }

    // [LEOCC · LT16 2026-06-12] 三段式 BP 路径解析：
    //   1) Object path with class suffix —— "/Game/Foo/BP_X.BP_X"     → 去后缀拿 package path
    //   2) Bare package path             —— "/Game/Foo/BP_X"          → 直接 DoesPackageExist
    //   3) Short name (legacy)           —— "BP_X"                    → AssetRegistry 全 /Game 扫
    // 三种形式都失败才报错；优先级 1>2>3，命中即用。
    FString AssetPath;
    if (BlueprintName.StartsWith(TEXT("/")))
    {
        FString PackagePath = BlueprintName;
        int32 DotIdx = INDEX_NONE;
        if (PackagePath.FindChar(TEXT('.'), DotIdx))
        {
            PackagePath = PackagePath.Left(DotIdx);
        }
        if (FPackageName::DoesPackageExist(PackagePath))
        {
            AssetPath = PackagePath;
        }
        else
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
                TEXT("Blueprint package '%s' does not exist"), *PackagePath));
        }
    }
    else
    {
        // 短名兼容：先试老路径 /Game/Blueprints/<Name>，再走 AssetRegistry 全工程查
        const FString LegacyPath = FString::Printf(TEXT("/Game/Blueprints/%s"), *BlueprintName);
        if (FPackageName::DoesPackageExist(LegacyPath))
        {
            AssetPath = LegacyPath;
        }
        else
        {
            IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();
            TArray<FAssetData> Found;
            AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Found, /*bSearchSubClasses*/ true);
            FAssetData Match;
            for (const FAssetData& A : Found)
            {
                if (A.AssetName == FName(*BlueprintName))
                {
                    Match = A;
                    break;
                }
            }
            if (!Match.IsValid())
            {
                return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
                    TEXT("Blueprint '%s' not found in /Game/Blueprints/ or anywhere else under /Game; pass a full path like /Game/Foo/BP_X"),
                    *BlueprintName));
            }
            AssetPath = Match.PackageName.ToString();
        }
    }

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint asset failed to load: %s"), *AssetPath));
    }

    // Get transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);
    FVector Scale(1.0f, 1.0f, 1.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
    }
    if (Params->HasField(TEXT("scale")))
    {
        Scale = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
    }

    // Spawn the actor
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
    }

    FTransform SpawnTransform;
    SpawnTransform.SetLocation(Location);
    SpawnTransform.SetRotation(FQuat(Rotation));
    SpawnTransform.SetScale3D(Scale);

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = *ActorName;
    // [LEOCC · LT16 2026-06-12] 名字冲突时不要 fatal —— 返回 nullptr 让上层报错
    SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;

    AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform, SpawnParams);
    if (NewActor)
    {
#if WITH_EDITOR
        // [LEOCC · LT16 2026-06-12] 同步 outliner 显示名（默认只设 FName，Outliner 显示的是 ActorLabel —— 必须显式同步）
        NewActor->SetActorLabel(ActorName);
#endif
        return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
        TEXT("Failed to spawn blueprint actor '%s' — likely name conflict with an existing actor in this world"), *ActorName));
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleFocusViewport(const TSharedPtr<FJsonObject>& Params)
{
    // Get target actor name if provided
    FString TargetActorName;
    bool HasTargetActor = Params->TryGetStringField(TEXT("target"), TargetActorName);

    // Get location if provided
    FVector Location(0.0f, 0.0f, 0.0f);
    bool HasLocation = false;
    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
        HasLocation = true;
    }

    // Get distance
    float Distance = 1000.0f;
    if (Params->HasField(TEXT("distance")))
    {
        Distance = Params->GetNumberField(TEXT("distance"));
    }

    // Get orientation if provided
    FRotator Orientation(0.0f, 0.0f, 0.0f);
    bool HasOrientation = false;
    if (Params->HasField(TEXT("orientation")))
    {
        Orientation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("orientation"));
        HasOrientation = true;
    }

    // Get the active viewport
    FLevelEditorViewportClient* ViewportClient = (FLevelEditorViewportClient*)GEditor->GetActiveViewport()->GetClient();
    if (!ViewportClient)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get active viewport"));
    }

    // If we have a target actor, focus on it
    if (HasTargetActor)
    {
        // Find the actor
        AActor* TargetActor = nullptr;
        TArray<AActor*> AllActors;
        UGameplayStatics::GetAllActorsOfClass(GWorld, AActor::StaticClass(), AllActors);
        
        for (AActor* Actor : AllActors)
        {
            if (Actor && Actor->GetName() == TargetActorName)
            {
                TargetActor = Actor;
                break;
            }
        }

        if (!TargetActor)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *TargetActorName));
        }

        // Focus on the actor
        ViewportClient->SetViewLocation(TargetActor->GetActorLocation() - FVector(Distance, 0.0f, 0.0f));
    }
    // Otherwise use the provided location
    else if (HasLocation)
    {
        ViewportClient->SetViewLocation(Location - FVector(Distance, 0.0f, 0.0f));
    }
    else
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Either 'target' or 'location' must be provided"));
    }

    // Set orientation if provided
    if (HasOrientation)
    {
        ViewportClient->SetViewRotation(Orientation);
    }

    // Force viewport to redraw
    ViewportClient->Invalidate();

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetBoolField(TEXT("success"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleTakeScreenshot(const TSharedPtr<FJsonObject>& Params)
{
    // Get file path parameter
    FString FilePath;
    if (!Params->TryGetStringField(TEXT("filepath"), FilePath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'filepath' parameter"));
    }
    
    // Ensure the file path has a proper extension
    if (!FilePath.EndsWith(TEXT(".png")))
    {
        FilePath += TEXT(".png");
    }

    // Get the active viewport
    if (GEditor && GEditor->GetActiveViewport())
    {
        FViewport* Viewport = GEditor->GetActiveViewport();
        TArray<FColor> Bitmap;
        FIntRect ViewportRect(0, 0, Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y);
        
        if (Viewport->ReadPixels(Bitmap, FReadSurfaceDataFlags(), ViewportRect))
        {
            TArray64<uint8> CompressedBitmap;
            FImageUtils::PNGCompressImageArray(Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y, Bitmap, CompressedBitmap);

            if (FFileHelper::SaveArrayToFile(CompressedBitmap, *FilePath))
            {
                TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
                ResultObj->SetStringField(TEXT("filepath"), FilePath);
                return ResultObj;
            }
        }
    }
    
    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to take screenshot"));
}

// ─────────────────────────────────────────────────────────────────────
// Static Mesh Info
// ─────────────────────────────────────────────────────────────────────

namespace
{
    FString CollisionTraceFlagToString(ECollisionTraceFlag Flag)
    {
        switch (Flag)
        {
        case CTF_UseDefault:           return TEXT("UseDefault");
        case CTF_UseSimpleAndComplex:  return TEXT("SimpleAndComplex");
        case CTF_UseSimpleAsComplex:   return TEXT("SimpleAsComplex");
        case CTF_UseComplexAsSimple:   return TEXT("ComplexAsSimple");
        default:                       return TEXT("Unknown");
        }
    }
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleGetStaticMeshInfo(const TSharedPtr<FJsonObject>& Params)
{
    // ── params ──────────────────────────────────────────────────────
    FString AssetPath = Params->GetStringField(TEXT("asset_path"));
    if (AssetPath.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("asset_path is required"));
    }

    const bool bIncludeVertices = Params->HasField(TEXT("include_vertices"))
        ? Params->GetBoolField(TEXT("include_vertices"))
        : false;

    const int32 LodIndex = Params->HasField(TEXT("lod_index"))
        ? static_cast<int32>(Params->GetNumberField(TEXT("lod_index")))
        : 0;

    const int32 MaxVertices = Params->HasField(TEXT("max_vertices"))
        ? static_cast<int32>(Params->GetNumberField(TEXT("max_vertices")))
        : 5000;

    // ── load asset ──────────────────────────────────────────────────
    UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
    if (!Mesh)
    {
        // Try appending the asset name (short-path convention)
        const FString LeafName = FPackageName::GetShortName(AssetPath);
        const FString FullPath = AssetPath + TEXT(".") + LeafName;
        Mesh = LoadObject<UStaticMesh>(nullptr, *FullPath);
    }
    if (!Mesh)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("StaticMesh asset not found"));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Mesh->GetPathName());

    // ── render data guard ───────────────────────────────────────────
    const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
    if (!RenderData || RenderData->LODResources.Num() == 0)
    {
        Result->SetStringField(TEXT("warning"), TEXT("No render data available (mesh may not be built)"));
        Result->SetNumberField(TEXT("lod_count"), 0);
        return Result;
    }

    const int32 NumLODs = RenderData->LODResources.Num();
    Result->SetNumberField(TEXT("lod_count"), NumLODs);

    // ── bounding box ────────────────────────────────────────────────
    const FBox BBox = Mesh->GetBoundingBox();
    {
        TSharedPtr<FJsonObject> BBoxObj = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> MinArr, MaxArr;
        MinArr.Add(MakeShared<FJsonValueNumber>(BBox.Min.X));
        MinArr.Add(MakeShared<FJsonValueNumber>(BBox.Min.Y));
        MinArr.Add(MakeShared<FJsonValueNumber>(BBox.Min.Z));
        MaxArr.Add(MakeShared<FJsonValueNumber>(BBox.Max.X));
        MaxArr.Add(MakeShared<FJsonValueNumber>(BBox.Max.Y));
        MaxArr.Add(MakeShared<FJsonValueNumber>(BBox.Max.Z));
        BBoxObj->SetArrayField(TEXT("min"), MinArr);
        BBoxObj->SetArrayField(TEXT("max"), MaxArr);
        Result->SetObjectField(TEXT("bounding_box"), BBoxObj);
    }

    // ── nanite ──────────────────────────────────────────────────────
    Result->SetBoolField(TEXT("nanite_enabled"), Mesh->HasValidNaniteData());

    // ── lightmap ────────────────────────────────────────────────────
    Result->SetNumberField(TEXT("lightmap_resolution"), Mesh->GetLightMapResolution());

    // ── LOD 0 summary (convenience) ─────────────────────────────────
    {
        const FStaticMeshLODResources& LOD0 = RenderData->LODResources[0];
        Result->SetNumberField(TEXT("vertex_count"), LOD0.GetNumVertices());
        Result->SetNumberField(TEXT("triangle_count"), static_cast<int64>(LOD0.GetNumTriangles()));
    }

    // ── material slots ──────────────────────────────────────────────
    {
        const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
        Result->SetNumberField(TEXT("material_slot_count"), Materials.Num());
        TArray<TSharedPtr<FJsonValue>> SlotsArr;
        for (int32 i = 0; i < Materials.Num(); ++i)
        {
            TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
            SlotObj->SetNumberField(TEXT("index"), i);
            SlotObj->SetStringField(TEXT("slot_name"), Materials[i].MaterialSlotName.ToString());
            SlotObj->SetStringField(TEXT("material_path"),
                Materials[i].MaterialInterface ? Materials[i].MaterialInterface->GetPathName() : TEXT("None"));
            SlotsArr.Add(MakeShared<FJsonValueObject>(SlotObj));
        }
        Result->SetArrayField(TEXT("material_slots"), SlotsArr);
    }

    // ── per-LOD details ─────────────────────────────────────────────
    {
        TArray<TSharedPtr<FJsonValue>> LodArr;
        for (int32 i = 0; i < NumLODs; ++i)
        {
            const FStaticMeshLODResources& LODRes = RenderData->LODResources[i];
            TSharedPtr<FJsonObject> LodObj = MakeShared<FJsonObject>();
            LodObj->SetNumberField(TEXT("lod_index"), i);
            LodObj->SetNumberField(TEXT("vertex_count"), LODRes.GetNumVertices());
            LodObj->SetNumberField(TEXT("triangle_count"), static_cast<int64>(LODRes.GetNumTriangles()));
            LodObj->SetNumberField(TEXT("section_count"), LODRes.Sections.Num());
            LodObj->SetNumberField(TEXT("num_uv_channels"),
                static_cast<int32>(LODRes.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords()));

            // Screen size
            if (i < MAX_STATIC_MESH_LODS)
            {
                LodObj->SetNumberField(TEXT("screen_size"), RenderData->ScreenSize[i].Default);
            }

            LodArr.Add(MakeShared<FJsonValueObject>(LodObj));
        }
        Result->SetArrayField(TEXT("lod_details"), LodArr);
    }

    // ── collision ───────────────────────────────────────────────────
    {
        TSharedPtr<FJsonObject> CollObj = MakeShared<FJsonObject>();
        UBodySetup* BodySetup = Mesh->GetBodySetup();
        if (BodySetup)
        {
            CollObj->SetStringField(TEXT("collision_type"),
                CollisionTraceFlagToString(BodySetup->CollisionTraceFlag));

            TSharedPtr<FJsonObject> ShapesObj = MakeShared<FJsonObject>();
            ShapesObj->SetNumberField(TEXT("box_count"), BodySetup->AggGeom.BoxElems.Num());
            ShapesObj->SetNumberField(TEXT("sphere_count"), BodySetup->AggGeom.SphereElems.Num());
            ShapesObj->SetNumberField(TEXT("capsule_count"), BodySetup->AggGeom.SphylElems.Num());
            ShapesObj->SetNumberField(TEXT("convex_count"), BodySetup->AggGeom.ConvexElems.Num());
            CollObj->SetObjectField(TEXT("simple_shapes"), ShapesObj);
        }
        else
        {
            CollObj->SetStringField(TEXT("collision_type"), TEXT("None"));
        }
        Result->SetObjectField(TEXT("collision"), CollObj);
    }

    // ── vertex positions (opt-in) ───────────────────────────────────
    if (bIncludeVertices)
    {
        if (LodIndex < 0 || LodIndex >= NumLODs)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("lod_index %d out of range [0, %d)"), LodIndex, NumLODs));
        }

        const FStaticMeshLODResources& LODRes = RenderData->LODResources[LodIndex];
        const FPositionVertexBuffer& PosBuffer = LODRes.VertexBuffers.PositionVertexBuffer;
        const uint32 NumVerts = PosBuffer.GetNumVertices();

        TSharedPtr<FJsonObject> VertsObj = MakeShared<FJsonObject>();
        VertsObj->SetNumberField(TEXT("lod_index"), LodIndex);
        VertsObj->SetNumberField(TEXT("total_vertex_count"), static_cast<int64>(NumVerts));

        const uint32 Count = FMath::Min(NumVerts, static_cast<uint32>(FMath::Max(0, MaxVertices)));
        VertsObj->SetBoolField(TEXT("truncated"), Count < NumVerts);
        VertsObj->SetNumberField(TEXT("returned_count"), static_cast<int64>(Count));

        TArray<TSharedPtr<FJsonValue>> PosArr;
        PosArr.Reserve(Count);
        for (uint32 i = 0; i < Count; ++i)
        {
            const FVector3f& Pos = PosBuffer.VertexPosition(i);
            TArray<TSharedPtr<FJsonValue>> XYZ;
            XYZ.Add(MakeShared<FJsonValueNumber>(Pos.X));
            XYZ.Add(MakeShared<FJsonValueNumber>(Pos.Y));
            XYZ.Add(MakeShared<FJsonValueNumber>(Pos.Z));
            PosArr.Add(MakeShared<FJsonValueArray>(XYZ));
        }
        VertsObj->SetArrayField(TEXT("positions"), PosArr);
        Result->SetObjectField(TEXT("vertices"), VertsObj);
    }

    return Result;
}

// =============================================================================
// Batch E: P1 from UnrealMCP_API_ExpansionRequest.md
//   - save_dirty_assets : persist in-memory edits to disk
//   - delete_asset      : remove an asset from Content Browser
// =============================================================================

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSaveDirtyAssets(const TSharedPtr<FJsonObject>& Params)
{
    // Two modes:
    //   1. asset_paths provided → save exactly those (skip silently if not dirty / not loaded).
    //   2. asset_paths omitted/empty → walk all loaded packages and save anything dirty.
    // We deliberately do NOT save Maps/Levels here — the user-facing risk profile of overwriting
    // a level mid-PIE / mid-edit is much higher than for content assets, and saving levels needs
    // a different code path (FEditorFileUtils::SaveDirtyPackages with the bSaveMapPackages flag).
    // Callers that need that should request it via /scripted/ python instead.

    const TArray<TSharedPtr<FJsonValue>>* AssetPathsArray = nullptr;
    const bool bHasArray = Params->TryGetArrayField(TEXT("asset_paths"), AssetPathsArray);

    TArray<TSharedPtr<FJsonValue>> SavedJson;
    TArray<TSharedPtr<FJsonValue>> SkippedJson;
    TArray<TSharedPtr<FJsonValue>> FailedJson;

    auto SavePackageIfDirty = [&](UPackage* Pkg, const FString& OriginPath)
    {
        if (!Pkg)
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("path"), OriginPath);
            Entry->SetStringField(TEXT("reason"), TEXT("package not loaded"));
            SkippedJson.Add(MakeShared<FJsonValueObject>(Entry));
            return;
        }
        if (!Pkg->IsDirty())
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("path"), Pkg->GetName());
            Entry->SetStringField(TEXT("reason"), TEXT("not dirty"));
            SkippedJson.Add(MakeShared<FJsonValueObject>(Entry));
            return;
        }
        // Skip Map packages — see header comment above.
        if (Pkg->ContainsMap())
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("path"), Pkg->GetName());
            Entry->SetStringField(TEXT("reason"), TEXT("map package; use FEditorFileUtils::SaveDirtyPackages for levels"));
            SkippedJson.Add(MakeShared<FJsonValueObject>(Entry));
            return;
        }

        const FString PackageName = Pkg->GetName();
        const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        SaveArgs.Error = GError;

        const FSavePackageResultStruct Result = UPackage::Save(Pkg, /*InAsset=*/nullptr, *Filename, SaveArgs);
        if (Result.Result == ESavePackageResult::Success)
        {
            SavedJson.Add(MakeShared<FJsonValueString>(PackageName));
        }
        else
        {
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("path"), PackageName);
            Entry->SetStringField(TEXT("filename"), Filename);
            Entry->SetNumberField(TEXT("save_result_enum"), static_cast<int32>(Result.Result));
            FailedJson.Add(MakeShared<FJsonValueObject>(Entry));
        }
    };

    if (bHasArray && AssetPathsArray && AssetPathsArray->Num() > 0)
    {
        for (const TSharedPtr<FJsonValue>& Val : *AssetPathsArray)
        {
            if (!Val.IsValid() || Val->Type != EJson::String) continue;
            const FString AssetPath = Val->AsString();

            // Strip ".AssetName" suffix to get package name.
            FString PackageName = AssetPath;
            int32 DotIdx = INDEX_NONE;
            if (PackageName.FindChar('.', DotIdx))
            {
                PackageName = PackageName.Left(DotIdx);
            }

            UPackage* Pkg = FindPackage(nullptr, *PackageName);
            // Fall back to LoadPackage when the user passes a path we haven't loaded yet —
            // dirty state only matters for already-loaded packages, but loading lets us at least
            // report "not dirty" instead of "package not loaded".
            if (!Pkg)
            {
                Pkg = LoadPackage(nullptr, *PackageName, LOAD_NoWarn);
            }
            SavePackageIfDirty(Pkg, AssetPath);
        }
    }
    else
    {
        // Whole-editor sweep. Bound at 1024 to avoid surprising the user with a multi-minute save
        // when the editor has hundreds of dirty packages; the limit is heuristic — bump if needed.
        TArray<UPackage*> DirtyPkgs;
        DirtyPkgs.Reserve(64);
        for (TObjectIterator<UPackage> It; It; ++It)
        {
            UPackage* P = *It;
            if (P && P->IsDirty()) DirtyPkgs.Add(P);
        }
        for (UPackage* P : DirtyPkgs)
        {
            SavePackageIfDirty(P, P->GetName());
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), FailedJson.Num() == 0);
    Result->SetArrayField(TEXT("saved"), SavedJson);
    Result->SetArrayField(TEXT("skipped"), SkippedJson);
    Result->SetArrayField(TEXT("failed"), FailedJson);
    Result->SetNumberField(TEXT("saved_count"), SavedJson.Num());
    Result->SetNumberField(TEXT("skipped_count"), SkippedJson.Num());
    Result->SetNumberField(TEXT("failed_count"), FailedJson.Num());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleDeleteAsset(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
    }

    bool bForce = false;
    Params->TryGetBoolField(TEXT("force"), bForce); // optional; default false → safer (refuses if referenced)

    // UEditorAssetLibrary handles BOTH the "loaded in memory" case and the "on-disk-only" case
    // by routing through ObjectTools, which is what Content-Browser deletion uses internally.
    if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Asset does not exist: %s"), *AssetPath));
    }

    const bool bOK = bForce
        ? UEditorAssetLibrary::DeleteLoadedAsset(UEditorAssetLibrary::LoadAsset(AssetPath))
        : UEditorAssetLibrary::DeleteAsset(AssetPath);

    if (!bOK)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("DeleteAsset failed for: %s (asset may have hard references; pass force=true to delete loaded asset anyway)"),
                *AssetPath));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetBoolField(TEXT("force"), bForce);
    return Result;
}

// =============================================================================
// Phase D 2026-06-24 — PIE 控制 / 关卡切换 / Actor 组件属性写入
// =============================================================================

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleStartPIE(const TSharedPtr<FJsonObject>& Params)
{
    // [LEOCC] Phase D: 启动 PIE。退出 PIE 后资产才可落盘，调用前请确认编辑器未在 PIE 中。
    if (!GEditor)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("GEditor not available"));

    if (GEditor->IsPlayingSessionInEditor())
    {
        TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
        R->SetBoolField(TEXT("success"), false);
        R->SetStringField(TEXT("message"), TEXT("PIE is already running"));
        return R;
    }

    FRequestPlaySessionParams PlayParams;
    GEditor->RequestPlaySession(PlayParams);

    TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
    R->SetBoolField(TEXT("success"), true);
    R->SetStringField(TEXT("message"), TEXT("PIE start requested"));
    return R;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleStopPIE(const TSharedPtr<FJsonObject>& Params)
{
    // [LEOCC] Phase D: 停止 PIE。
    if (!GEditor)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("GEditor not available"));

    if (!GEditor->IsPlayingSessionInEditor())
    {
        TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
        R->SetBoolField(TEXT("success"), false);
        R->SetStringField(TEXT("message"), TEXT("PIE is not running"));
        return R;
    }

    GEditor->RequestEndPlayMap();

    TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
    R->SetBoolField(TEXT("success"), true);
    R->SetStringField(TEXT("message"), TEXT("PIE stop requested"));
    return R;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleOpenLevel(const TSharedPtr<FJsonObject>& Params)
{
    // [LEOCC] Phase D: 在编辑器中打开指定关卡（map_path 格式 /Game/Levels/MyMap）。
    FString MapPath;
    if (!Params->TryGetStringField(TEXT("map_path"), MapPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'map_path' parameter"));

    // [LEOCC] UEditorLevelLibrary::LoadLevel 在 UE5.x 已废弃，改用 ULevelEditorSubsystem
    ULevelEditorSubsystem* LevelEditorSub = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
    if (!LevelEditorSub)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("LevelEditorSubsystem not available"));

    const bool bLoaded = LevelEditorSub->LoadLevel(MapPath);
    TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
    R->SetBoolField(TEXT("success"), bLoaded);
    R->SetStringField(TEXT("map_path"), MapPath);
    if (!bLoaded)
        R->SetStringField(TEXT("error"), TEXT("LoadLevel returned false — check map_path"));
    return R;
}

TSharedPtr<FJsonObject> FUnrealMCPEditorCommands::HandleSetActorComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
    // [LEOCC] Phase D: 设置关卡中 Actor 的组件属性（写入 World 实例，不是 CDO）。
    // 支持点分嵌套路径（FStructProperty / FObjectProperty）。
    FString ActorLabel;
    if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_label' parameter"));

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));

    if (!Params->HasField(TEXT("property_value")))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
    TSharedPtr<FJsonValue> PropertyValue = Params->Values.FindRef(TEXT("property_value"));

    UWorld* World = GWorld;
    if (!World)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No world available"));

    // [LEOCC] 通过 ActorLabel 找 Actor（与 set_actor_property 保持一致）
    AActor* TargetActor = nullptr;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == ActorLabel)
        {
            TargetActor = *It;
            break;
        }
    }
    if (!TargetActor)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

    // [LEOCC] 通过 GetName() 匹配组件
    UActorComponent* TargetComp = nullptr;
    TInlineComponentArray<UActorComponent*> Components;
    TargetActor->GetComponents(Components);
    for (UActorComponent* Comp : Components)
    {
        if (Comp && Comp->GetName() == ComponentName)
        {
            TargetComp = Comp;
            break;
        }
    }
    if (!TargetComp)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s on actor %s"), *ComponentName, *ActorLabel));

    TargetComp->Modify();

    FString ErrorMessage;
    if (!FUnrealMCPCommonUtils::SetObjectProperty(TargetComp, PropertyName, PropertyValue, ErrorMessage))
        return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);

    TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
    R->SetBoolField(TEXT("success"), true);
    R->SetStringField(TEXT("actor"), ActorLabel);
    R->SetStringField(TEXT("component"), ComponentName);
    R->SetStringField(TEXT("property"), PropertyName);
    return R;
}