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

        // Return the created actor's details
        return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create actor"));
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

    FString Root      = TEXT("/Game/Blueprints/");
    FString AssetPath = Root + BlueprintName;

    if (!FPackageName::DoesPackageExist(AssetPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint '%s' not found – it must reside under /Game/Blueprints"), *BlueprintName));
    }

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
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

    AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform, SpawnParams);
    if (NewActor)
    {
        return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to spawn blueprint actor"));
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