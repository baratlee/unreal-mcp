#include "Commands/UnrealMCPBlueprintCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Factories/BlueprintFactory.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
#include "EditorAssetLibrary.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
// State Machine editor classes (for get_anim_state_machine / get_anim_state_graph / get_anim_transition_graph)
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_TransitionResult.h"
// AnimGraph node reflection (for method-A: anim_node_properties / property_bindings dump)
#include "AnimGraphNode_Base.h"
#include "JsonObjectConverter.h"

namespace
{
    // How much pin payload to serialize. AnimGraph nodes carry InstancedStruct
    // text in DefaultValue that can be multi-KB per pin and easily blow past
    // the MCP socket response timeout, so callers can opt into a smaller shape.
    enum class EPinPayloadMode : uint8
    {
        Full,       // Verbatim DefaultValue (existing behavior).
        Summary,    // DefaultValue truncated to a short preview when long.
        NamesOnly,  // DefaultValue dropped entirely; keep type/links only.
    };

    EPinPayloadMode ParsePinPayloadMode(const FString& Raw)
    {
        if (Raw.Equals(TEXT("summary"), ESearchCase::IgnoreCase))
        {
            return EPinPayloadMode::Summary;
        }
        if (Raw.Equals(TEXT("names_only"), ESearchCase::IgnoreCase))
        {
            return EPinPayloadMode::NamesOnly;
        }
        return EPinPayloadMode::Full;
    }

    // Threshold and preview length used by Summary mode.
    constexpr int32 GPinSummaryThreshold = 256;
    constexpr int32 GPinSummaryPreviewLen = 96;

    // Method A: serialize the two Details-panel-only dimensions of AnimGraph
    // nodes that would otherwise be invisible to callers:
    //   1. anim_node_properties — EditAnywhere UPROPERTY on the inner
    //      FAnimNode_* struct (e.g. TwoWayBlend's AlphaInputType / bEnabled /
    //      AlwaysUpdateChildren). These render in the Details panel but have
    //      no dedicated pin in most configurations.
    //   2. property_bindings   — entries in UAnimGraphNodeBinding_Base's
    //      PropertyBindings TMap (UE 5.x: the old per-node PropertyBindings_DEPRECATED
    //      map was moved onto the Instanced `Binding` sub-object).
    //
    // We reach both via reflection rather than hard-coded casts, so this works
    // for every UAnimGraphNode_* subclass regardless of its inner-node field
    // name (some use "Node", TwoWayBlend uses "BlendNode", etc.).
    //
    // Both payloads are skipped entirely in NamesOnly mode to keep that mode
    // maximally lightweight.
    void SerializeAnimGraphNodeExtras(
        UAnimGraphNode_Base* AnimNode,
        TSharedPtr<FJsonObject> NodeObj,
        EPinPayloadMode Mode)
    {
        if (!AnimNode || !NodeObj.IsValid() || Mode == EPinPayloadMode::NamesOnly)
        {
            return;
        }

        // (1) Inner FAnimNode_* — find the first FStructProperty on this UClass
        // whose UScriptStruct name starts with "AnimNode_". Per UE convention
        // each UAnimGraphNode_* has exactly one such member.
        for (TFieldIterator<FStructProperty> PropIt(AnimNode->GetClass()); PropIt; ++PropIt)
        {
            FStructProperty* StructProp = *PropIt;
            if (!StructProp || !StructProp->Struct) continue;

            const FString StructName = StructProp->Struct->GetName();
            if (!StructName.StartsWith(TEXT("AnimNode_"))) continue;

            const void* StructData = StructProp->ContainerPtrToValuePtr<void>(AnimNode);
            TSharedRef<FJsonObject> InnerObj = MakeShared<FJsonObject>();

            // CheckFlags=CPF_Edit  → only include properties that are EditAnywhere / EditDefaultsOnly / EditInstanceOnly
            //                        (what a user sees in the Details panel)
            // SkipFlags=CPF_Transient | CPF_DuplicateTransient → drop runtime scratch
            FJsonObjectConverter::UStructToJsonObject(
                StructProp->Struct,
                StructData,
                InnerObj,
                /*CheckFlags=*/CPF_Edit,
                /*SkipFlags=*/CPF_Transient | CPF_DuplicateTransient);

            NodeObj->SetObjectField(TEXT("anim_node_properties"), InnerObj);
            NodeObj->SetStringField(TEXT("anim_node_struct"), StructName);
            break;
        }

        // (2) PropertyBindings — walk UAnimGraphNode_Base::Binding (which is
        // a UAnimGraphNodeBinding_Base instance whose private header we do not
        // want to include) and reflect its "PropertyBindings" TMap.
        FProperty* BindingMemberProp = AnimNode->GetClass()->FindPropertyByName(TEXT("Binding"));
        FObjectProperty* BindingObjProp = CastField<FObjectProperty>(BindingMemberProp);
        if (!BindingObjProp) return;

        UObject* BindingObj = BindingObjProp->GetObjectPropertyValue(
            BindingObjProp->ContainerPtrToValuePtr<void>(AnimNode));
        if (!BindingObj) return;

        FProperty* PropertyBindingsProp = BindingObj->GetClass()->FindPropertyByName(TEXT("PropertyBindings"));
        FMapProperty* MapProp = CastField<FMapProperty>(PropertyBindingsProp);
        if (!MapProp) return;

        void* MapContainer = MapProp->ContainerPtrToValuePtr<void>(BindingObj);
        FScriptMapHelper MapHelper(MapProp, MapContainer);

        UScriptStruct* BindingStructDef = FAnimGraphNodePropertyBinding::StaticStruct();
        TArray<TSharedPtr<FJsonValue>> BindingsArray;

        // GetMaxIndex + IsValidIndex is the version-stable way to iterate
        // FScriptMap; FIterator exists on 5.7 but adds nothing for this use.
        for (int32 It = 0; It < MapHelper.GetMaxIndex(); ++It)
        {
            if (!MapHelper.IsValidIndex(It)) continue;

            const void* KeyRaw = MapHelper.GetKeyPtr(It);
            const void* ValRaw = MapHelper.GetValuePtr(It);
            if (!KeyRaw || !ValRaw) continue;

            const FName BindingKey = *reinterpret_cast<const FName*>(KeyRaw);

            TSharedRef<FJsonObject> BObj = MakeShared<FJsonObject>();
            BObj->SetStringField(TEXT("property_name"), BindingKey.ToString());

            if (BindingStructDef)
            {
                TSharedRef<FJsonObject> BDetail = MakeShared<FJsonObject>();
                FJsonObjectConverter::UStructToJsonObject(
                    BindingStructDef,
                    ValRaw,
                    BDetail,
                    /*CheckFlags=*/0,                // FAnimGraphNodePropertyBinding fields aren't EditAnywhere, they're UPROPERTY()
                    /*SkipFlags=*/CPF_Transient | CPF_DuplicateTransient);
                BObj->SetObjectField(TEXT("detail"), BDetail);
            }

            BindingsArray.Add(MakeShared<FJsonValueObject>(BObj));
        }

        NodeObj->SetArrayField(TEXT("property_bindings"), BindingsArray);
    }

    // Serialize a single UEdGraphNode (and its pin links) to a JSON object.
    // Shared between HandleGetBlueprintInfo and HandleGetBlueprintFunctionGraph
    // so the two commands always agree on node/pin shape.
    TSharedPtr<FJsonObject> SerializeGraphNodeToJson(UEdGraphNode* Node, EPinPayloadMode Mode = EPinPayloadMode::Full)
    {
        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        if (!Node)
        {
            return NodeObj;
        }

        NodeObj->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
        NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
        NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

        if (!Node->NodeComment.IsEmpty())
        {
            NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);
        }

        if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
        {
            NodeObj->SetStringField(TEXT("event_name"),
                EventNode->EventReference.GetMemberName().ToString());
        }

        if (UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node))
        {
            NodeObj->SetStringField(TEXT("function_name"),
                FuncNode->FunctionReference.GetMemberName().ToString());
            if (UClass* OwnerClass = FuncNode->FunctionReference.GetMemberParentClass())
            {
                NodeObj->SetStringField(TEXT("function_owner"), OwnerClass->GetName());
            }
        }

        TArray<TSharedPtr<FJsonValue>> PinsArray;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin) continue;

            TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
            PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
            PinObj->SetStringField(TEXT("direction"),
                Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
            PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());

            if (Pin->PinType.PinSubCategoryObject.IsValid())
            {
                PinObj->SetStringField(TEXT("sub_type"),
                    Pin->PinType.PinSubCategoryObject->GetName());
            }

            if (!Pin->DefaultValue.IsEmpty())
            {
                switch (Mode)
                {
                case EPinPayloadMode::Full:
                    PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
                    break;
                case EPinPayloadMode::Summary:
                    if (Pin->DefaultValue.Len() <= GPinSummaryThreshold)
                    {
                        PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
                    }
                    else
                    {
                        PinObj->SetNumberField(TEXT("default_value_len"), Pin->DefaultValue.Len());
                        PinObj->SetStringField(TEXT("default_value_preview"),
                            Pin->DefaultValue.Left(GPinSummaryPreviewLen));
                        PinObj->SetBoolField(TEXT("default_value_truncated"), true);
                    }
                    break;
                case EPinPayloadMode::NamesOnly:
                    // Drop DefaultValue entirely.
                    break;
                }
            }

            if (Pin->LinkedTo.Num() > 0)
            {
                TArray<TSharedPtr<FJsonValue>> LinksArray;
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;

                    TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
                    LinkObj->SetStringField(TEXT("node_guid"),
                        LinkedPin->GetOwningNode()->NodeGuid.ToString());
                    LinkObj->SetStringField(TEXT("pin_name"),
                        LinkedPin->PinName.ToString());
                    LinksArray.Add(MakeShared<FJsonValueObject>(LinkObj));
                }
                PinObj->SetArrayField(TEXT("links"), LinksArray);
            }

            PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
        }
        NodeObj->SetArrayField(TEXT("pins"), PinsArray);

        // Method A: AnimGraph-only Details-panel + PropertyBindings dump.
        // Non-AnimGraph nodes (K2Node_* in EventGraph / user functions) just
        // fall through the Cast and get no extra fields.
        if (UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node))
        {
            SerializeAnimGraphNodeExtras(AnimNode, NodeObj, Mode);
        }

        return NodeObj;
    }
}

FUnrealMCPBlueprintCommands::FUnrealMCPBlueprintCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    if (CommandType == TEXT("create_blueprint"))
    {
        return HandleCreateBlueprint(Params);
    }
    else if (CommandType == TEXT("add_component_to_blueprint"))
    {
        return HandleAddComponentToBlueprint(Params);
    }
    else if (CommandType == TEXT("set_component_property"))
    {
        return HandleSetComponentProperty(Params);
    }
    else if (CommandType == TEXT("set_physics_properties"))
    {
        return HandleSetPhysicsProperties(Params);
    }
    else if (CommandType == TEXT("compile_blueprint"))
    {
        return HandleCompileBlueprint(Params);
    }
    else if (CommandType == TEXT("spawn_blueprint_actor"))
    {
        return HandleSpawnBlueprintActor(Params);
    }
    else if (CommandType == TEXT("set_blueprint_property"))
    {
        return HandleSetBlueprintProperty(Params);
    }
    else if (CommandType == TEXT("set_static_mesh_properties"))
    {
        return HandleSetStaticMeshProperties(Params);
    }
    else if (CommandType == TEXT("set_pawn_properties"))
    {
        return HandleSetPawnProperties(Params);
    }
    else if (CommandType == TEXT("get_blueprint_info"))
    {
        return HandleGetBlueprintInfo(Params);
    }
    else if (CommandType == TEXT("get_blueprint_function_graph"))
    {
        return HandleGetBlueprintFunctionGraph(Params);
    }
    else if (CommandType == TEXT("get_anim_state_machine"))
    {
        return HandleGetAnimStateMachine(Params);
    }
    else if (CommandType == TEXT("get_anim_state_graph"))
    {
        return HandleGetAnimStateGraph(Params);
    }
    else if (CommandType == TEXT("get_anim_transition_graph"))
    {
        return HandleGetAnimTransitionGraph(Params);
    }
    else if (CommandType == TEXT("get_component_properties"))
    {
        return HandleGetComponentProperties(Params);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown blueprint command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCreateBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Check if blueprint already exists
    FString PackagePath = TEXT("/Game/Blueprints/");
    FString AssetName = BlueprintName;
    if (UEditorAssetLibrary::DoesAssetExist(PackagePath + AssetName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint already exists: %s"), *BlueprintName));
    }

    // Create the blueprint factory
    UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
    
    // Handle parent class
    FString ParentClass;
    Params->TryGetStringField(TEXT("parent_class"), ParentClass);
    
    // Default to Actor if no parent class specified
    UClass* SelectedParentClass = AActor::StaticClass();
    
    // Try to find the specified parent class
    if (!ParentClass.IsEmpty())
    {
        FString ClassName = ParentClass;
        if (!ClassName.StartsWith(TEXT("A")))
        {
            ClassName = TEXT("A") + ClassName;
        }
        
        // First try direct StaticClass lookup for common classes
        UClass* FoundClass = nullptr;
        if (ClassName == TEXT("APawn"))
        {
            FoundClass = APawn::StaticClass();
        }
        else if (ClassName == TEXT("AActor"))
        {
            FoundClass = AActor::StaticClass();
        }
        else
        {
            // Try loading the class using LoadClass which is more reliable than FindObject
            const FString ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
            FoundClass = LoadClass<AActor>(nullptr, *ClassPath);
            
            if (!FoundClass)
            {
                // Try alternate paths if not found
                const FString GameClassPath = FString::Printf(TEXT("/Script/Game.%s"), *ClassName);
                FoundClass = LoadClass<AActor>(nullptr, *GameClassPath);
            }
        }

        if (FoundClass)
        {
            SelectedParentClass = FoundClass;
            UE_LOG(LogTemp, Log, TEXT("Successfully set parent class to '%s'"), *ClassName);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Could not find specified parent class '%s' at paths: /Script/Engine.%s or /Script/Game.%s, defaulting to AActor"), 
                *ClassName, *ClassName, *ClassName);
        }
    }
    
    Factory->ParentClass = SelectedParentClass;

    // Create the blueprint
    UPackage* Package = CreatePackage(*(PackagePath + AssetName));
    UBlueprint* NewBlueprint = Cast<UBlueprint>(Factory->FactoryCreateNew(UBlueprint::StaticClass(), Package, *AssetName, RF_Standalone | RF_Public, nullptr, GWarn));

    if (NewBlueprint)
    {
        // Notify the asset registry
        FAssetRegistryModule::AssetCreated(NewBlueprint);

        // Mark the package dirty
        Package->MarkPackageDirty();

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("name"), AssetName);
        ResultObj->SetStringField(TEXT("path"), PackagePath + AssetName);
        return ResultObj;
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create blueprint"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleAddComponentToBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentType;
    if (!Params->TryGetStringField(TEXT("component_type"), ComponentType))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Create the component - dynamically find the component class by name
    UClass* ComponentClass = nullptr;

    // Try to find the class with exact name first
    ComponentClass = FindObject<UClass>(nullptr, *ComponentType);
    
    // If not found, try with "Component" suffix
    if (!ComponentClass && !ComponentType.EndsWith(TEXT("Component")))
    {
        FString ComponentTypeWithSuffix = ComponentType + TEXT("Component");
        ComponentClass = FindObject<UClass>(nullptr, *ComponentTypeWithSuffix);
    }
    
    // If still not found, try with "U" prefix
    if (!ComponentClass && !ComponentType.StartsWith(TEXT("U")))
    {
        FString ComponentTypeWithPrefix = TEXT("U") + ComponentType;
        ComponentClass = FindObject<UClass>(nullptr, *ComponentTypeWithPrefix);
        
        // Try with both prefix and suffix
        if (!ComponentClass && !ComponentType.EndsWith(TEXT("Component")))
        {
            FString ComponentTypeWithBoth = TEXT("U") + ComponentType + TEXT("Component");
            ComponentClass = FindObject<UClass>(nullptr, *ComponentTypeWithBoth);
        }
    }
    
    // Verify that the class is a valid component type
    if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown component type: %s"), *ComponentType));
    }

    // Add the component to the blueprint
    USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, *ComponentName);
    if (NewNode)
    {
        // Set transform if provided
        USceneComponent* SceneComponent = Cast<USceneComponent>(NewNode->ComponentTemplate);
        if (SceneComponent)
        {
            if (Params->HasField(TEXT("location")))
            {
                SceneComponent->SetRelativeLocation(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
            }
            if (Params->HasField(TEXT("rotation")))
            {
                SceneComponent->SetRelativeRotation(FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
            }
            if (Params->HasField(TEXT("scale")))
            {
                SceneComponent->SetRelativeScale3D(FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
            }
        }

        // Add to root if no parent specified
        Blueprint->SimpleConstructionScript->AddNode(NewNode);

        // Compile the blueprint
        FKismetEditorUtilities::CompileBlueprint(Blueprint);

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("component_name"), ComponentName);
        ResultObj->SetStringField(TEXT("component_type"), ComponentType);
        return ResultObj;
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to add component to blueprint"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetComponentProperty(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    // Log all input parameters for debugging
    UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Blueprint: %s, Component: %s, Property: %s"), 
        *BlueprintName, *ComponentName, *PropertyName);
    
    // Log property_value if available
    if (Params->HasField(TEXT("property_value")))
    {
        TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
        FString ValueType;
        
        switch(JsonValue->Type)
        {
            case EJson::Boolean: ValueType = FString::Printf(TEXT("Boolean: %s"), JsonValue->AsBool() ? TEXT("true") : TEXT("false")); break;
            case EJson::Number: ValueType = FString::Printf(TEXT("Number: %f"), JsonValue->AsNumber()); break;
            case EJson::String: ValueType = FString::Printf(TEXT("String: %s"), *JsonValue->AsString()); break;
            case EJson::Array: ValueType = TEXT("Array"); break;
            case EJson::Object: ValueType = TEXT("Object"); break;
            default: ValueType = TEXT("Unknown"); break;
        }
        
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Value Type: %s"), *ValueType);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - No property_value provided"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Blueprint not found: %s"), *BlueprintName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Blueprint found: %s (Class: %s)"), 
            *BlueprintName, 
            Blueprint->GeneratedClass ? *Blueprint->GeneratedClass->GetName() : TEXT("NULL"));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Searching for component %s in blueprint nodes"), *ComponentName);
    
    if (!Blueprint->SimpleConstructionScript)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - SimpleConstructionScript is NULL for blueprint %s"), *BlueprintName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid blueprint construction script"));
    }
    
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node)
        {
            UE_LOG(LogTemp, Verbose, TEXT("SetComponentProperty - Found node: %s"), *Node->GetVariableName().ToString());
            if (Node->GetVariableName().ToString() == ComponentName)
            {
                ComponentNode = Node;
                break;
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Found NULL node in blueprint"));
        }
    }

    if (!ComponentNode)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Component not found: %s"), *ComponentName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }
    else
    {
        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Component found: %s (Class: %s)"), 
            *ComponentName, 
            ComponentNode->ComponentTemplate ? *ComponentNode->ComponentTemplate->GetClass()->GetName() : TEXT("NULL"));
    }

    // Get the component template
    UObject* ComponentTemplate = ComponentNode->ComponentTemplate;
    if (!ComponentTemplate)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Component template is NULL for %s"), *ComponentName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Invalid component template"));
    }

    // Check if this is a Spring Arm component and log special debug info
    if (ComponentTemplate->GetClass()->GetName().Contains(TEXT("SpringArm")))
    {
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - SpringArm component detected! Class: %s"), 
            *ComponentTemplate->GetClass()->GetPathName());
            
        // Log all properties of the SpringArm component class
        UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - SpringArm properties:"));
        for (TFieldIterator<FProperty> PropIt(ComponentTemplate->GetClass()); PropIt; ++PropIt)
        {
            FProperty* Prop = *PropIt;
            UE_LOG(LogTemp, Warning, TEXT("  - %s (%s)"), *Prop->GetName(), *Prop->GetCPPType());
        }

        // Special handling for Spring Arm properties
        if (Params->HasField(TEXT("property_value")))
        {
            TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
            
            // Get the property using the new FField system
            FProperty* Property = FindFProperty<FProperty>(ComponentTemplate->GetClass(), *PropertyName);
            if (!Property)
            {
                UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Property %s not found on SpringArm component"), *PropertyName);
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Property %s not found on SpringArm component"), *PropertyName));
            }

            // Create a scope guard to ensure property cleanup
            struct FScopeGuard
            {
                UObject* Object;
                FScopeGuard(UObject* InObject) : Object(InObject) 
                {
                    if (Object)
                    {
                        Object->Modify();
                    }
                }
                ~FScopeGuard()
                {
                    if (Object)
                    {
                        Object->PostEditChange();
                    }
                }
            } ScopeGuard(ComponentTemplate);

            bool bSuccess = false;
            FString ErrorMessage;

            // Handle specific Spring Arm property types
            if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
            {
                if (JsonValue->Type == EJson::Number)
                {
                    const float Value = JsonValue->AsNumber();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting float property %s to %f"), *PropertyName, Value);
                    FloatProp->SetPropertyValue_InContainer(ComponentTemplate, Value);
                    bSuccess = true;
                }
            }
            else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
            {
                if (JsonValue->Type == EJson::Boolean)
                {
                    const bool Value = JsonValue->AsBool();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting bool property %s to %d"), *PropertyName, Value);
                    BoolProp->SetPropertyValue_InContainer(ComponentTemplate, Value);
                    bSuccess = true;
                }
            }
            else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
            {
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Handling struct property %s of type %s"), 
                    *PropertyName, *StructProp->Struct->GetName());
                
                // Special handling for common Spring Arm struct properties
                if (StructProp->Struct == TBaseStructure<FVector>::Get())
                {
                    if (JsonValue->Type == EJson::Array)
                    {
                        const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
                        if (Arr.Num() == 3)
                        {
                            FVector Vec(
                                Arr[0]->AsNumber(),
                                Arr[1]->AsNumber(),
                                Arr[2]->AsNumber()
                            );
                            void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                            StructProp->CopySingleValue(PropertyAddr, &Vec);
                            bSuccess = true;
                        }
                    }
                }
                else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
                {
                    if (JsonValue->Type == EJson::Array)
                    {
                        const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
                        if (Arr.Num() == 3)
                        {
                            FRotator Rot(
                                Arr[0]->AsNumber(),
                                Arr[1]->AsNumber(),
                                Arr[2]->AsNumber()
                            );
                            void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                            StructProp->CopySingleValue(PropertyAddr, &Rot);
                            bSuccess = true;
                        }
                    }
                }
            }

            if (bSuccess)
            {
                // Mark the blueprint as modified
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Successfully set SpringArm property %s"), *PropertyName);
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

                TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
                ResultObj->SetStringField(TEXT("component"), ComponentName);
                ResultObj->SetStringField(TEXT("property"), PropertyName);
                ResultObj->SetBoolField(TEXT("success"), true);
                return ResultObj;
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set SpringArm property %s"), *PropertyName);
                return FUnrealMCPCommonUtils::CreateErrorResponse(
                    FString::Printf(TEXT("Failed to set SpringArm property %s"), *PropertyName));
            }
        }
    }

    // Regular property handling for non-Spring Arm components continues...

    // Set the property value
    if (Params->HasField(TEXT("property_value")))
    {
        TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
        
        // Get the property
        FProperty* Property = FindFProperty<FProperty>(ComponentTemplate->GetClass(), *PropertyName);
        if (!Property)
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Property %s not found on component %s"), 
                *PropertyName, *ComponentName);
            
            // List all available properties for this component
            UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Available properties for %s:"), *ComponentName);
            for (TFieldIterator<FProperty> PropIt(ComponentTemplate->GetClass()); PropIt; ++PropIt)
            {
                FProperty* Prop = *PropIt;
                UE_LOG(LogTemp, Warning, TEXT("  - %s (%s)"), *Prop->GetName(), *Prop->GetCPPType());
            }
            
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Property %s not found on component %s"), *PropertyName, *ComponentName));
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property found: %s (Type: %s)"), 
                *PropertyName, *Property->GetCPPType());
        }

        bool bSuccess = false;
        FString ErrorMessage;

        // Handle different property types
        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Attempting to set property %s"), *PropertyName);
        
        // Add try-catch block to catch and log any crashes
        try
        {
            if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
            {
                // Handle vector properties
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property is a struct: %s"), 
                    StructProp->Struct ? *StructProp->Struct->GetName() : TEXT("NULL"));
                    
                if (StructProp->Struct == TBaseStructure<FVector>::Get())
                {
                    if (JsonValue->Type == EJson::Array)
                    {
                        // Handle array input [x, y, z]
                        const TArray<TSharedPtr<FJsonValue>>& Arr = JsonValue->AsArray();
                        if (Arr.Num() == 3)
                        {
                            FVector Vec(
                                Arr[0]->AsNumber(),
                                Arr[1]->AsNumber(),
                                Arr[2]->AsNumber()
                            );
                            void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting Vector(%f, %f, %f)"), 
                                Vec.X, Vec.Y, Vec.Z);
                            StructProp->CopySingleValue(PropertyAddr, &Vec);
                            bSuccess = true;
                        }
                        else
                        {
                            ErrorMessage = FString::Printf(TEXT("Vector property requires 3 values, got %d"), Arr.Num());
                            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                        }
                    }
                    else if (JsonValue->Type == EJson::Number)
                    {
                        // Handle scalar input (sets all components to same value)
                        float Value = JsonValue->AsNumber();
                        FVector Vec(Value, Value, Value);
                        void* PropertyAddr = StructProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
                        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting Vector(%f, %f, %f) from scalar"), 
                            Vec.X, Vec.Y, Vec.Z);
                        StructProp->CopySingleValue(PropertyAddr, &Vec);
                        bSuccess = true;
                    }
                    else
                    {
                        ErrorMessage = TEXT("Vector property requires either a single number or array of 3 numbers");
                        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                    }
                }
                else
                {
                    // Handle other struct properties using default handler
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Using generic struct handler for %s"), 
                        *PropertyName);
                    bSuccess = FUnrealMCPCommonUtils::SetObjectProperty(ComponentTemplate, PropertyName, JsonValue, ErrorMessage);
                    if (!bSuccess)
                    {
                        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set struct property: %s"), *ErrorMessage);
                    }
                }
            }
            else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
            {
                // Handle enum properties
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property is an enum"));
                if (JsonValue->Type == EJson::String)
                {
                    FString EnumValueName = JsonValue->AsString();
                    UEnum* Enum = EnumProp->GetEnum();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting enum from string: %s"), *EnumValueName);
                    
                    if (Enum)
                    {
                        int64 EnumValue = Enum->GetValueByNameString(EnumValueName);
                        
                        if (EnumValue != INDEX_NONE)
                        {
                            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Found enum value: %lld"), EnumValue);
                            EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(
                                ComponentTemplate, 
                                EnumValue
                            );
                            bSuccess = true;
                        }
                        else
                        {
                            // List all possible enum values
                            UE_LOG(LogTemp, Warning, TEXT("SetComponentProperty - Available enum values for %s:"), 
                                *Enum->GetName());
                            for (int32 i = 0; i < Enum->NumEnums(); i++)
                            {
                                UE_LOG(LogTemp, Warning, TEXT("  - %s (%lld)"), 
                                    *Enum->GetNameStringByIndex(i),
                                    Enum->GetValueByIndex(i));
                            }
                            
                            ErrorMessage = FString::Printf(TEXT("Invalid enum value '%s' for property %s"), 
                                *EnumValueName, *PropertyName);
                            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                        }
                    }
                    else
                    {
                        ErrorMessage = TEXT("Enum object is NULL");
                        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                    }
                }
                else if (JsonValue->Type == EJson::Number)
                {
                    // Allow setting enum by integer value
                    int64 EnumValue = JsonValue->AsNumber();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting enum from number: %lld"), EnumValue);
                    EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(
                        ComponentTemplate, 
                        EnumValue
                    );
                    bSuccess = true;
                }
                else
                {
                    ErrorMessage = TEXT("Enum property requires either a string name or integer value");
                    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                }
            }
            else if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
            {
                // Handle numeric properties
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Property is numeric: IsInteger=%d, IsFloat=%d"), 
                    NumericProp->IsInteger(), NumericProp->IsFloatingPoint());
                    
                if (JsonValue->Type == EJson::Number)
                {
                    double Value = JsonValue->AsNumber();
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Setting numeric value: %f"), Value);
                    
                    if (NumericProp->IsInteger())
                    {
                        NumericProp->SetIntPropertyValue(ComponentTemplate, (int64)Value);
                        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Set integer value: %lld"), (int64)Value);
                        bSuccess = true;
                    }
                    else if (NumericProp->IsFloatingPoint())
                    {
                        NumericProp->SetFloatingPointPropertyValue(ComponentTemplate, Value);
                        UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Set float value: %f"), Value);
                        bSuccess = true;
                    }
                }
                else
                {
                    ErrorMessage = TEXT("Numeric property requires a number value");
                    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - %s"), *ErrorMessage);
                }
            }
            else
            {
                // Handle all other property types using default handler
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Using generic property handler for %s (Type: %s)"), 
                    *PropertyName, *Property->GetCPPType());
                bSuccess = FUnrealMCPCommonUtils::SetObjectProperty(ComponentTemplate, PropertyName, JsonValue, ErrorMessage);
                if (!bSuccess)
                {
                    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set property: %s"), *ErrorMessage);
                }
            }
        }
        catch (const std::exception& Ex)
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - EXCEPTION: %s"), ANSI_TO_TCHAR(Ex.what()));
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Exception while setting property %s: %s"), *PropertyName, ANSI_TO_TCHAR(Ex.what())));
        }
        catch (...)
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - UNKNOWN EXCEPTION occurred while setting property %s"), *PropertyName);
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Unknown exception while setting property %s"), *PropertyName));
        }

        if (bSuccess)
        {
            // Mark the blueprint as modified
            UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Successfully set property %s on component %s"), 
                *PropertyName, *ComponentName);
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetStringField(TEXT("component"), ComponentName);
            ResultObj->SetStringField(TEXT("property"), PropertyName);
            ResultObj->SetBoolField(TEXT("success"), true);
            return ResultObj;
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Failed to set property %s: %s"), 
                *PropertyName, *ErrorMessage);
            return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
        }
    }

    UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Missing 'property_value' parameter"));
    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetPhysicsProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(ComponentNode->ComponentTemplate);
    if (!PrimComponent)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a primitive component"));
    }

    // Set physics properties
    if (Params->HasField(TEXT("simulate_physics")))
    {
        PrimComponent->SetSimulatePhysics(Params->GetBoolField(TEXT("simulate_physics")));
    }

    if (Params->HasField(TEXT("mass")))
    {
        float Mass = Params->GetNumberField(TEXT("mass"));
        // In UE5.5, use proper overrideMass instead of just scaling
        PrimComponent->SetMassOverrideInKg(NAME_None, Mass);
        UE_LOG(LogTemp, Display, TEXT("Set mass for component %s to %f kg"), *ComponentName, Mass);
    }

    if (Params->HasField(TEXT("linear_damping")))
    {
        PrimComponent->SetLinearDamping(Params->GetNumberField(TEXT("linear_damping")));
    }

    if (Params->HasField(TEXT("angular_damping")))
    {
        PrimComponent->SetAngularDamping(Params->GetNumberField(TEXT("angular_damping")));
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCompileBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Compile the blueprint
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), BlueprintName);
    ResultObj->SetBoolField(TEXT("compiled"), true);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params)
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
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get transform parameters
    FVector Location(0.0f, 0.0f, 0.0f);
    FRotator Rotation(0.0f, 0.0f, 0.0f);

    if (Params->HasField(TEXT("location")))
    {
        Location = FUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
    }
    if (Params->HasField(TEXT("rotation")))
    {
        Rotation = FUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
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

    AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform);
    if (NewActor)
    {
        NewActor->SetActorLabel(*ActorName);
        return FUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to spawn blueprint actor"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetBlueprintProperty(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString PropertyName;
    if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the default object
    UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
    if (!DefaultObject)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get default object"));
    }

    // Set the property value
    if (Params->HasField(TEXT("property_value")))
    {
        TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));
        
        FString ErrorMessage;
        if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, PropertyName, JsonValue, ErrorMessage))
        {
            // Mark the blueprint as modified
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetStringField(TEXT("property"), PropertyName);
            ResultObj->SetBoolField(TEXT("success"), true);
            return ResultObj;
        }
        else
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
        }
    }

    return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetStaticMeshProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Find the component
    USCS_Node* ComponentNode = nullptr;
    for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            ComponentNode = Node;
            break;
        }
    }

    if (!ComponentNode)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ComponentNode->ComponentTemplate);
    if (!MeshComponent)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Component is not a static mesh component"));
    }

    // Set static mesh properties
    if (Params->HasField(TEXT("static_mesh")))
    {
        FString MeshPath = Params->GetStringField(TEXT("static_mesh"));
        UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
        if (Mesh)
        {
            MeshComponent->SetStaticMesh(Mesh);
        }
    }

    if (Params->HasField(TEXT("material")))
    {
        FString MaterialPath = Params->GetStringField(TEXT("material"));
        UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
        if (Material)
        {
            MeshComponent->SetMaterial(0, Material);
        }
    }

    // Mark the blueprint as modified
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("component"), ComponentName);
    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetPawnProperties(const TSharedPtr<FJsonObject>& Params)
{
    // Get required parameters
    FString BlueprintName;
    if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
    }

    // Find the blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprint(BlueprintName);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
    }

    // Get the default object
    UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
    if (!DefaultObject)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get default object"));
    }

    // Track if any properties were set successfully
    bool bAnyPropertiesSet = false;
    TSharedPtr<FJsonObject> ResultsObj = MakeShared<FJsonObject>();
    
    // Set auto possess player if specified
    if (Params->HasField(TEXT("auto_possess_player")))
    {
        TSharedPtr<FJsonValue> AutoPossessValue = Params->Values.FindRef(TEXT("auto_possess_player"));
        
        FString ErrorMessage;
        if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, TEXT("AutoPossessPlayer"), AutoPossessValue, ErrorMessage))
        {
            bAnyPropertiesSet = true;
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), true);
            ResultsObj->SetObjectField(TEXT("AutoPossessPlayer"), PropResultObj);
        }
        else
        {
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), false);
            PropResultObj->SetStringField(TEXT("error"), ErrorMessage);
            ResultsObj->SetObjectField(TEXT("AutoPossessPlayer"), PropResultObj);
        }
    }
    
    // Set controller rotation properties
    const TCHAR* RotationProps[] = {
        TEXT("bUseControllerRotationYaw"),
        TEXT("bUseControllerRotationPitch"),
        TEXT("bUseControllerRotationRoll")
    };
    
    const TCHAR* ParamNames[] = {
        TEXT("use_controller_rotation_yaw"),
        TEXT("use_controller_rotation_pitch"),
        TEXT("use_controller_rotation_roll")
    };
    
    for (int32 i = 0; i < 3; i++)
    {
        if (Params->HasField(ParamNames[i]))
        {
            TSharedPtr<FJsonValue> Value = Params->Values.FindRef(ParamNames[i]);
            
            FString ErrorMessage;
            if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, RotationProps[i], Value, ErrorMessage))
            {
                bAnyPropertiesSet = true;
                TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
                PropResultObj->SetBoolField(TEXT("success"), true);
                ResultsObj->SetObjectField(RotationProps[i], PropResultObj);
            }
            else
            {
                TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
                PropResultObj->SetBoolField(TEXT("success"), false);
                PropResultObj->SetStringField(TEXT("error"), ErrorMessage);
                ResultsObj->SetObjectField(RotationProps[i], PropResultObj);
            }
        }
    }
    
    // Set can be damaged property
    if (Params->HasField(TEXT("can_be_damaged")))
    {
        TSharedPtr<FJsonValue> Value = Params->Values.FindRef(TEXT("can_be_damaged"));
        
        FString ErrorMessage;
        if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, TEXT("bCanBeDamaged"), Value, ErrorMessage))
        {
            bAnyPropertiesSet = true;
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), true);
            ResultsObj->SetObjectField(TEXT("bCanBeDamaged"), PropResultObj);
        }
        else
        {
            TSharedPtr<FJsonObject> PropResultObj = MakeShared<FJsonObject>();
            PropResultObj->SetBoolField(TEXT("success"), false);
            PropResultObj->SetStringField(TEXT("error"), ErrorMessage);
            ResultsObj->SetObjectField(TEXT("bCanBeDamaged"), PropResultObj);
        }
    }

    // Mark the blueprint as modified if any properties were set
    if (bAnyPropertiesSet)
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }
    else if (ResultsObj->Values.Num() == 0)
    {
        // No properties were specified
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No properties specified to set"));
    }

    TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
    ResponseObj->SetStringField(TEXT("blueprint"), BlueprintName);
    ResponseObj->SetBoolField(TEXT("success"), bAnyPropertiesSet);
    ResponseObj->SetObjectField(TEXT("results"), ResultsObj);
    return ResponseObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetBlueprintInfo(const TSharedPtr<FJsonObject>& Params)
{
    // Get blueprint path parameter
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    // Load blueprint
    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

    // --- Basic info ---
    ResultObj->SetStringField(TEXT("name"), Blueprint->GetName());
    ResultObj->SetStringField(TEXT("path"), Blueprint->GetPathName());

    if (Blueprint->ParentClass)
    {
        ResultObj->SetStringField(TEXT("parent_class"), Blueprint->ParentClass->GetName());

        // Build full class hierarchy
        TArray<TSharedPtr<FJsonValue>> HierarchyArray;
        UClass* Current = Blueprint->ParentClass;
        while (Current)
        {
            HierarchyArray.Add(MakeShared<FJsonValueString>(Current->GetName()));
            Current = Current->GetSuperClass();
        }
        ResultObj->SetArrayField(TEXT("class_hierarchy"), HierarchyArray);
    }

    // Blueprint type
    switch (Blueprint->BlueprintType)
    {
        case BPTYPE_Normal:        ResultObj->SetStringField(TEXT("blueprint_type"), TEXT("Normal")); break;
        case BPTYPE_MacroLibrary:  ResultObj->SetStringField(TEXT("blueprint_type"), TEXT("MacroLibrary")); break;
        case BPTYPE_Interface:     ResultObj->SetStringField(TEXT("blueprint_type"), TEXT("Interface")); break;
        case BPTYPE_FunctionLibrary: ResultObj->SetStringField(TEXT("blueprint_type"), TEXT("FunctionLibrary")); break;
        default:                   ResultObj->SetStringField(TEXT("blueprint_type"), TEXT("Other")); break;
    }

    // --- Implemented interfaces ---
    TArray<TSharedPtr<FJsonValue>> InterfacesArray;
    for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
    {
        if (InterfaceDesc.Interface)
        {
            InterfacesArray.Add(MakeShared<FJsonValueString>(InterfaceDesc.Interface->GetName()));
        }
    }
    ResultObj->SetArrayField(TEXT("interfaces"), InterfacesArray);

    // --- Components (from SimpleConstructionScript) ---
    TArray<TSharedPtr<FJsonValue>> ComponentsArray;
    if (Blueprint->SimpleConstructionScript)
    {
        const TArray<USCS_Node*>& AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
        USCS_Node* RootNode = Blueprint->SimpleConstructionScript->GetDefaultSceneRootNode();

        for (USCS_Node* Node : AllNodes)
        {
            if (!Node || !Node->ComponentTemplate) continue;

            TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
            CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
            CompObj->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
            CompObj->SetBoolField(TEXT("is_root"), Node == RootNode);

            // Parent component
            if (Node->ParentComponentOrVariableName != NAME_None)
            {
                CompObj->SetStringField(TEXT("parent"), Node->ParentComponentOrVariableName.ToString());
            }

            // Gather editable properties from the component template
            TArray<TSharedPtr<FJsonValue>> PropsArray;
            for (TFieldIterator<FProperty> PropIt(Node->ComponentTemplate->GetClass()); PropIt; ++PropIt)
            {
                FProperty* Prop = *PropIt;
                // Only include properties that are editable in the editor
                if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;

                TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
                PropObj->SetStringField(TEXT("name"), Prop->GetName());
                PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());
                PropObj->SetStringField(TEXT("category"), Prop->GetMetaData(TEXT("Category")));

                // Try to get the current value as string
                FString ValueStr;
                void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(Node->ComponentTemplate);
                Prop->ExportTextItem_Direct(ValueStr, ValueAddr, nullptr, nullptr, PPF_None);
                if (!ValueStr.IsEmpty())
                {
                    PropObj->SetStringField(TEXT("value"), ValueStr);
                }

                PropsArray.Add(MakeShared<FJsonValueObject>(PropObj));
            }
            CompObj->SetArrayField(TEXT("properties"), PropsArray);

            ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
        }
    }
    ResultObj->SetArrayField(TEXT("components"), ComponentsArray);

    // --- Inherited Components (from parent BP SCS chain + native CDO) ---
    TArray<TSharedPtr<FJsonValue>> InheritedComponentsArray;
    {
        UClass* ParentClass = Blueprint->ParentClass;
        while (ParentClass)
        {
            UBlueprint* ParentBP = Cast<UBlueprint>(ParentClass->ClassGeneratedBy);
            if (ParentBP && ParentBP->SimpleConstructionScript)
            {
                for (USCS_Node* Node : ParentBP->SimpleConstructionScript->GetAllNodes())
                {
                    if (!Node || !Node->ComponentTemplate) continue;
                    TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
                    CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
                    CompObj->SetStringField(TEXT("class"), Node->ComponentTemplate->GetClass()->GetName());
                    CompObj->SetStringField(TEXT("inherited_from"), ParentBP->GetName());
                    InheritedComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
                }
                ParentClass = ParentClass->GetSuperClass();
            }
            else
            {
                if (AActor* CDO = Cast<AActor>(ParentClass->GetDefaultObject()))
                {
                    TInlineComponentArray<UActorComponent*> NativeComps;
                    CDO->GetComponents(NativeComps);
                    for (UActorComponent* Comp : NativeComps)
                    {
                        if (!Comp) continue;
                        TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
                        CompObj->SetStringField(TEXT("name"), Comp->GetName());
                        CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
                        CompObj->SetStringField(TEXT("inherited_from"), ParentClass->GetName());
                        InheritedComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
                    }
                }
                break;
            }
        }
    }
    ResultObj->SetArrayField(TEXT("inherited_components"), InheritedComponentsArray);

    // --- Variables (from NewVariables) ---
    TArray<TSharedPtr<FJsonValue>> VariablesArray;
    for (const FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
        VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());

        // Sub-category (e.g., the class name for object types)
        if (Var.VarType.PinSubCategoryObject.IsValid())
        {
            VarObj->SetStringField(TEXT("sub_type"), Var.VarType.PinSubCategoryObject->GetName());
        }

        VarObj->SetStringField(TEXT("category"), Var.Category.ToString());
        VarObj->SetBoolField(TEXT("is_instance_editable"),
            Var.PropertyFlags & CPF_Edit ? true : false);
        VarObj->SetBoolField(TEXT("is_blueprint_read_only"),
            Var.PropertyFlags & CPF_BlueprintReadOnly ? true : false);

        // Replication
        if (Var.RepNotifyFunc != NAME_None)
        {
            VarObj->SetStringField(TEXT("rep_notify_func"), Var.RepNotifyFunc.ToString());
        }

        // Default value
        if (!Var.DefaultValue.IsEmpty())
        {
            VarObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
        }

        VariablesArray.Add(MakeShared<FJsonValueObject>(VarObj));
    }
    ResultObj->SetArrayField(TEXT("variables"), VariablesArray);

    // --- Event Graphs ---
    TArray<TSharedPtr<FJsonValue>> GraphsArray;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (!Graph) continue;

        TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
        GraphObj->SetStringField(TEXT("name"), Graph->GetName());
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

        TArray<TSharedPtr<FJsonValue>> NodesArray;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node) continue;
            NodesArray.Add(MakeShared<FJsonValueObject>(SerializeGraphNodeToJson(Node)));
        }
        GraphObj->SetArrayField(TEXT("nodes"), NodesArray);

        GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
    }
    ResultObj->SetArrayField(TEXT("event_graphs"), GraphsArray);

    // --- Function Graphs ---
    TArray<TSharedPtr<FJsonValue>> FuncGraphsArray;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (!Graph) continue;

        TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
        GraphObj->SetStringField(TEXT("name"), Graph->GetName());
        GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

        // Get function signature from the entry node
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UK2Node_Event* EntryNode = Cast<UK2Node_Event>(Node);
            if (!EntryNode) continue;

            TArray<TSharedPtr<FJsonValue>> ParamsArray;
            for (UEdGraphPin* Pin : EntryNode->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Output) continue;
                if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;

                TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
                ParamObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
                ParamObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
                ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
            }
            GraphObj->SetArrayField(TEXT("parameters"), ParamsArray);
            break;
        }

        FuncGraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
    }
    ResultObj->SetArrayField(TEXT("function_graphs"), FuncGraphsArray);

    return ResultObj;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetBlueprintFunctionGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString FunctionName;
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
    }

    FString PinPayloadModeRaw;
    Params->TryGetStringField(TEXT("pin_payload_mode"), PinPayloadModeRaw);
    const EPinPayloadMode PayloadMode = ParsePinPayloadMode(PinPayloadModeRaw);

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    // AnimGraph and user functions both live in FunctionGraphs; UbergraphPages
    // holds the EventGraph(s). Search FunctionGraphs first, then Ubergraph as
    // a fallback so callers can also retrieve EventGraph node detail by name.
    UEdGraph* TargetGraph = nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph && Graph->GetName() == FunctionName)
        {
            TargetGraph = Graph;
            break;
        }
    }
    if (!TargetGraph)
    {
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Graph && Graph->GetName() == FunctionName)
            {
                TargetGraph = Graph;
                break;
            }
        }
    }

    if (!TargetGraph)
    {
        TArray<FString> AvailableNames;
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph) AvailableNames.Add(Graph->GetName());
        }
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Graph) AvailableNames.Add(Graph->GetName());
        }
        const FString Joined = FString::Join(AvailableNames, TEXT(", "));
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Function graph '%s' not found in blueprint '%s'. Available graphs: [%s]"),
                *FunctionName, *BlueprintPath, *Joined));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("function_name"), TargetGraph->GetName());
    ResultObj->SetStringField(TEXT("graph_class"), TargetGraph->GetClass()->GetName());
    ResultObj->SetNumberField(TEXT("node_count"), TargetGraph->Nodes.Num());

    TArray<TSharedPtr<FJsonValue>> NodesArray;
    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (!Node) continue;
        NodesArray.Add(MakeShared<FJsonValueObject>(SerializeGraphNodeToJson(Node, PayloadMode)));
    }
    ResultObj->SetArrayField(TEXT("nodes"), NodesArray);

    return ResultObj;
}

// ---------------------------------------------------------------------------
// State Machine tools
// ---------------------------------------------------------------------------

// Helper: find a StateMachine sub-graph inside a Blueprint's AnimGraph.
// Returns nullptr if not found.  When StateMachineName is empty, returns
// the first StateMachine discovered.
namespace
{
    UAnimationStateMachineGraph* FindStateMachineGraph(
        UBlueprint* Blueprint,
        const FString& StateMachineName)
    {
        // Iterate FunctionGraphs looking for the AnimGraph, then search its
        // nodes for StateMachine nodes.
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (!Graph) continue;
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UAnimGraphNode_StateMachineBase* SMNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
                if (!SMNode) continue;
                UAnimationStateMachineGraph* SMGraph = SMNode->EditorStateMachineGraph;
                if (!SMGraph) continue;

                if (StateMachineName.IsEmpty() ||
                    SMNode->GetStateMachineName().Equals(StateMachineName, ESearchCase::IgnoreCase))
                {
                    return SMGraph;
                }
            }
        }
        return nullptr;
    }

    FString TransitionLogicTypeToString(ETransitionLogicType::Type Type)
    {
        switch (Type)
        {
        case ETransitionLogicType::TLT_StandardBlend:   return TEXT("Standard");
        case ETransitionLogicType::TLT_Inertialization: return TEXT("Inertialization");
        case ETransitionLogicType::TLT_Custom:          return TEXT("Custom");
        default:                                          return TEXT("Unknown");
        }
    }

    FString BlendOptionToString(EAlphaBlendOption Option)
    {
        switch (Option)
        {
        case EAlphaBlendOption::Linear:            return TEXT("Linear");
        case EAlphaBlendOption::Cubic:             return TEXT("Cubic");
        case EAlphaBlendOption::HermiteCubic:      return TEXT("HermiteCubic");
        case EAlphaBlendOption::Sinusoidal:        return TEXT("Sinusoidal");
        case EAlphaBlendOption::QuadraticInOut:    return TEXT("QuadraticInOut");
        case EAlphaBlendOption::CubicInOut:        return TEXT("CubicInOut");
        case EAlphaBlendOption::QuarticInOut:      return TEXT("QuarticInOut");
        case EAlphaBlendOption::QuinticInOut:      return TEXT("QuinticInOut");
        case EAlphaBlendOption::CircularIn:        return TEXT("CircularIn");
        case EAlphaBlendOption::CircularOut:       return TEXT("CircularOut");
        case EAlphaBlendOption::CircularInOut:     return TEXT("CircularInOut");
        case EAlphaBlendOption::ExpIn:             return TEXT("ExpIn");
        case EAlphaBlendOption::ExpOut:            return TEXT("ExpOut");
        case EAlphaBlendOption::ExpInOut:          return TEXT("ExpInOut");
        case EAlphaBlendOption::Custom:            return TEXT("Custom");
        default:                                    return TEXT("Unknown");
        }
    }
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetAnimStateMachine(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
    {
        Blueprint = FUnrealMCPCommonUtils::FindBlueprintByName(BlueprintPath);
    }
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    FString SMName;
    Params->TryGetStringField(TEXT("state_machine_name"), SMName);

    UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(Blueprint, SMName);
    if (!SMGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            SMName.IsEmpty()
                ? TEXT("No State Machine found in this Blueprint's AnimGraph")
                : FString::Printf(TEXT("State Machine '%s' not found"), *SMName));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("state_machine_name"),
        SMGraph->OwnerAnimGraphNode ? SMGraph->OwnerAnimGraphNode->GetStateMachineName() : TEXT(""));

    // Entry state
    FString EntryStateName;
    if (SMGraph->EntryNode)
    {
        if (UEdGraphPin* OutPin = SMGraph->EntryNode->GetOutputPin())
        {
            if (OutPin->LinkedTo.Num() > 0)
            {
                if (UAnimStateNodeBase* EntryTarget = Cast<UAnimStateNodeBase>(OutPin->LinkedTo[0]->GetOwningNode()))
                {
                    EntryStateName = EntryTarget->GetStateName();
                }
            }
        }
    }
    ResultObj->SetStringField(TEXT("entry_state"), EntryStateName);

    // Collect states
    TArray<UAnimStateNode*> StateNodes;
    SMGraph->GetNodesOfClass(StateNodes);
    TArray<UAnimStateConduitNode*> ConduitNodes;
    SMGraph->GetNodesOfClass(ConduitNodes);

    TArray<TSharedPtr<FJsonValue>> StatesArray;
    for (UAnimStateNode* StateNode : StateNodes)
    {
        if (!StateNode) continue;
        TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
        StateObj->SetStringField(TEXT("name"), StateNode->GetStateName());
        StateObj->SetStringField(TEXT("type"), TEXT("state"));
        StateObj->SetBoolField(TEXT("is_conduit"), false);
        StateObj->SetBoolField(TEXT("always_reset_on_entry"), StateNode->bAlwaysResetOnEntry);
        StatesArray.Add(MakeShared<FJsonValueObject>(StateObj));
    }
    for (UAnimStateConduitNode* ConduitNode : ConduitNodes)
    {
        if (!ConduitNode) continue;
        TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
        StateObj->SetStringField(TEXT("name"), ConduitNode->GetStateName());
        StateObj->SetStringField(TEXT("type"), TEXT("conduit"));
        StateObj->SetBoolField(TEXT("is_conduit"), true);
        StateObj->SetBoolField(TEXT("always_reset_on_entry"), false);
        StatesArray.Add(MakeShared<FJsonValueObject>(StateObj));
    }
    ResultObj->SetNumberField(TEXT("state_count"), StatesArray.Num());
    ResultObj->SetArrayField(TEXT("states"), StatesArray);

    // Collect transitions
    TArray<UAnimStateTransitionNode*> TransitionNodes;
    SMGraph->GetNodesOfClass(TransitionNodes);

    TArray<TSharedPtr<FJsonValue>> TransitionsArray;
    for (UAnimStateTransitionNode* TransNode : TransitionNodes)
    {
        if (!TransNode) continue;
        TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();

        UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
        UAnimStateNodeBase* NextState = TransNode->GetNextState();
        TransObj->SetStringField(TEXT("source"), PrevState ? PrevState->GetStateName() : TEXT(""));
        TransObj->SetStringField(TEXT("target"), NextState ? NextState->GetStateName() : TEXT(""));
        TransObj->SetNumberField(TEXT("priority"), TransNode->PriorityOrder);
        TransObj->SetNumberField(TEXT("crossfade_duration"), TransNode->CrossfadeDuration);
        TransObj->SetStringField(TEXT("blend_mode"), BlendOptionToString(TransNode->BlendMode));
        TransObj->SetStringField(TEXT("logic_type"), TransitionLogicTypeToString(TransNode->LogicType));
        TransObj->SetBoolField(TEXT("bidirectional"), TransNode->Bidirectional);
        TransObj->SetBoolField(TEXT("automatic_rule"), TransNode->bAutomaticRuleBasedOnSequencePlayerInState);
        if (TransNode->bAutomaticRuleBasedOnSequencePlayerInState)
        {
            TransObj->SetNumberField(TEXT("automatic_rule_trigger_time"), TransNode->AutomaticRuleTriggerTime);
        }
        TransObj->SetBoolField(TEXT("disabled"), TransNode->bDisabled);

        // Check if condition graph has nodes (beyond the result node)
        if (UEdGraph* CondGraph = TransNode->GetBoundGraph())
        {
            TransObj->SetNumberField(TEXT("condition_graph_node_count"), CondGraph->Nodes.Num());
        }
        // Check if custom transition graph exists
        if (UEdGraph* CustomGraph = TransNode->GetCustomTransitionGraph())
        {
            TransObj->SetBoolField(TEXT("has_custom_blend_graph"), CustomGraph->Nodes.Num() > 0);
        }

        TransitionsArray.Add(MakeShared<FJsonValueObject>(TransObj));
    }
    ResultObj->SetNumberField(TEXT("transition_count"), TransitionsArray.Num());
    ResultObj->SetArrayField(TEXT("transitions"), TransitionsArray);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResultObj);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetAnimStateGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString StateName;
    if (!Params->TryGetStringField(TEXT("state_name"), StateName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
    }

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
    {
        Blueprint = FUnrealMCPCommonUtils::FindBlueprintByName(BlueprintPath);
    }
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    FString SMName;
    Params->TryGetStringField(TEXT("state_machine_name"), SMName);

    FString PinPayloadStr;
    Params->TryGetStringField(TEXT("pin_payload_mode"), PinPayloadStr);
    EPinPayloadMode PayloadMode = ParsePinPayloadMode(PinPayloadStr);

    UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(Blueprint, SMName);
    if (!SMGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("State Machine not found in this Blueprint"));
    }

    // Find the target state
    UEdGraph* TargetBoundGraph = nullptr;
    FString FoundStateName;

    // Search normal states
    TArray<UAnimStateNode*> StateNodes;
    SMGraph->GetNodesOfClass(StateNodes);
    for (UAnimStateNode* StateNode : StateNodes)
    {
        if (StateNode && StateNode->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
        {
            TargetBoundGraph = StateNode->BoundGraph;
            FoundStateName = StateNode->GetStateName();
            break;
        }
    }

    // Search conduits if not found
    if (!TargetBoundGraph)
    {
        TArray<UAnimStateConduitNode*> ConduitNodes;
        SMGraph->GetNodesOfClass(ConduitNodes);
        for (UAnimStateConduitNode* ConduitNode : ConduitNodes)
        {
            if (ConduitNode && ConduitNode->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
            {
                TargetBoundGraph = ConduitNode->BoundGraph;
                FoundStateName = ConduitNode->GetStateName();
                break;
            }
        }
    }

    if (!TargetBoundGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("State '%s' not found in the State Machine"), *StateName));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("state_name"), FoundStateName);
    ResultObj->SetStringField(TEXT("graph_class"), TargetBoundGraph->GetClass()->GetName());
    ResultObj->SetNumberField(TEXT("node_count"), TargetBoundGraph->Nodes.Num());

    TArray<TSharedPtr<FJsonValue>> NodesArray;
    for (UEdGraphNode* Node : TargetBoundGraph->Nodes)
    {
        if (!Node) continue;
        NodesArray.Add(MakeShared<FJsonValueObject>(SerializeGraphNodeToJson(Node, PayloadMode)));
    }
    ResultObj->SetArrayField(TEXT("nodes"), NodesArray);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResultObj);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetAnimTransitionGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString SourceState;
    if (!Params->TryGetStringField(TEXT("source_state"), SourceState))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_state' parameter"));
    }

    FString TargetState;
    if (!Params->TryGetStringField(TEXT("target_state"), TargetState))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_state' parameter"));
    }

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
    {
        Blueprint = FUnrealMCPCommonUtils::FindBlueprintByName(BlueprintPath);
    }
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    FString SMName;
    Params->TryGetStringField(TEXT("state_machine_name"), SMName);

    FString PinPayloadStr;
    Params->TryGetStringField(TEXT("pin_payload_mode"), PinPayloadStr);
    EPinPayloadMode PayloadMode = ParsePinPayloadMode(PinPayloadStr);

    UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(Blueprint, SMName);
    if (!SMGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("State Machine not found in this Blueprint"));
    }

    // Find the matching transition
    TArray<UAnimStateTransitionNode*> TransitionNodes;
    SMGraph->GetNodesOfClass(TransitionNodes);

    UAnimStateTransitionNode* TargetTransition = nullptr;
    for (UAnimStateTransitionNode* TransNode : TransitionNodes)
    {
        if (!TransNode) continue;
        UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
        UAnimStateNodeBase* NextState = TransNode->GetNextState();
        if (PrevState && NextState &&
            PrevState->GetStateName().Equals(SourceState, ESearchCase::IgnoreCase) &&
            NextState->GetStateName().Equals(TargetState, ESearchCase::IgnoreCase))
        {
            TargetTransition = TransNode;
            break;
        }
    }

    if (!TargetTransition)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Transition from '%s' to '%s' not found"), *SourceState, *TargetState));
    }

    UEdGraph* CondGraph = TargetTransition->GetBoundGraph();
    if (!CondGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Transition has no condition graph"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("source_state"), TargetTransition->GetPreviousState()->GetStateName());
    ResultObj->SetStringField(TEXT("target_state"), TargetTransition->GetNextState()->GetStateName());

    // Transition metadata
    ResultObj->SetNumberField(TEXT("priority"), TargetTransition->PriorityOrder);
    ResultObj->SetNumberField(TEXT("crossfade_duration"), TargetTransition->CrossfadeDuration);
    ResultObj->SetStringField(TEXT("blend_mode"), BlendOptionToString(TargetTransition->BlendMode));
    ResultObj->SetStringField(TEXT("logic_type"), TransitionLogicTypeToString(TargetTransition->LogicType));
    ResultObj->SetBoolField(TEXT("bidirectional"), TargetTransition->Bidirectional);
    ResultObj->SetBoolField(TEXT("automatic_rule"), TargetTransition->bAutomaticRuleBasedOnSequencePlayerInState);
    if (TargetTransition->bAutomaticRuleBasedOnSequencePlayerInState)
    {
        ResultObj->SetNumberField(TEXT("automatic_rule_trigger_time"), TargetTransition->AutomaticRuleTriggerTime);
    }

    // Condition graph nodes
    ResultObj->SetStringField(TEXT("graph_class"), CondGraph->GetClass()->GetName());
    ResultObj->SetNumberField(TEXT("node_count"), CondGraph->Nodes.Num());

    TArray<TSharedPtr<FJsonValue>> NodesArray;
    for (UEdGraphNode* Node : CondGraph->Nodes)
    {
        if (!Node) continue;
        NodesArray.Add(MakeShared<FJsonValueObject>(SerializeGraphNodeToJson(Node, PayloadMode)));
    }
    ResultObj->SetArrayField(TEXT("nodes"), NodesArray);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResultObj);
}

// ---------------------------------------------------------------------------
// FindComponentTemplate
// ---------------------------------------------------------------------------
UObject* FUnrealMCPBlueprintCommands::FindComponentTemplate(
    UBlueprint* Blueprint, const FString& ComponentName, FString& OutSource)
{
    // 1. This BP's SCS
    if (Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->ComponentTemplate &&
                Node->GetVariableName().ToString() == ComponentName)
            {
                OutSource = TEXT("scs");
                return Node->ComponentTemplate;
            }
        }
    }

    // 2. Walk parent BP chain
    UClass* ParentClass = Blueprint->ParentClass;
    while (ParentClass)
    {
        UBlueprint* ParentBP = Cast<UBlueprint>(ParentClass->ClassGeneratedBy);
        if (ParentBP && ParentBP->SimpleConstructionScript)
        {
            for (USCS_Node* Node : ParentBP->SimpleConstructionScript->GetAllNodes())
            {
                if (Node && Node->ComponentTemplate &&
                    Node->GetVariableName().ToString() == ComponentName)
                {
                    OutSource = FString::Printf(TEXT("inherited_scs:%s"), *ParentBP->GetName());
                    return Node->ComponentTemplate;
                }
            }
            ParentClass = ParentClass->GetSuperClass();
        }
        else
        {
            // 3. C++ class – check native CDO components
            if (AActor* CDO = Cast<AActor>(ParentClass->GetDefaultObject()))
            {
                TInlineComponentArray<UActorComponent*> NativeComps;
                CDO->GetComponents(NativeComps);
                for (UActorComponent* Comp : NativeComps)
                {
                    if (Comp && Comp->GetName() == ComponentName)
                    {
                        OutSource = FString::Printf(TEXT("native:%s"), *ParentClass->GetName());
                        return Comp;
                    }
                }
            }
            break;
        }
    }

    return nullptr;
}

// ---------------------------------------------------------------------------
// SerializePropertiesToJson
// ---------------------------------------------------------------------------
void FUnrealMCPBlueprintCommands::SerializePropertiesToJson(
    UObject* Object, TArray<TSharedPtr<FJsonValue>>& OutArray, int32 Depth)
{
    if (!Object || Depth <= 0) return;

    for (TFieldIterator<FProperty> PropIt(Object->GetClass()); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;
        if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;

        TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
        PropObj->SetStringField(TEXT("name"), Prop->GetName());
        PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());
        PropObj->SetStringField(TEXT("category"), Prop->GetMetaData(TEXT("Category")));

        void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(Object);

        // --- Object property (single nested sub-object) ---
        FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
        if (ObjProp)
        {
            UObject* SubObj = ObjProp->GetObjectPropertyValue(ValueAddr);
            if (SubObj)
            {
                PropObj->SetStringField(TEXT("value"), SubObj->GetPathName());
                PropObj->SetStringField(TEXT("object_class"), SubObj->GetClass()->GetName());

                if (Depth > 1 && SubObj->IsIn(Object))
                {
                    TArray<TSharedPtr<FJsonValue>> SubProps;
                    SerializePropertiesToJson(SubObj, SubProps, Depth - 1);
                    PropObj->SetArrayField(TEXT("sub_properties"), SubProps);
                }
            }
            else
            {
                PropObj->SetStringField(TEXT("value"), TEXT("None"));
            }
            OutArray.Add(MakeShared<FJsonValueObject>(PropObj));
            continue;
        }

        // --- Array property – expand object elements ---
        FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
        if (ArrayProp)
        {
            FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner);
            if (InnerObjProp && Depth > 1)
            {
                FScriptArrayHelper ArrayHelper(ArrayProp, ValueAddr);
                TArray<TSharedPtr<FJsonValue>> ElementsArray;
                for (int32 i = 0; i < ArrayHelper.Num(); ++i)
                {
                    UObject* ElemObj = InnerObjProp->GetObjectPropertyValue(ArrayHelper.GetRawPtr(i));
                    if (!ElemObj) continue;

                    TSharedPtr<FJsonObject> ElemJson = MakeShared<FJsonObject>();
                    ElemJson->SetStringField(TEXT("class"), ElemObj->GetClass()->GetName());
                    ElemJson->SetStringField(TEXT("path"), ElemObj->GetPathName());

                    TArray<TSharedPtr<FJsonValue>> ElemProps;
                    SerializePropertiesToJson(ElemObj, ElemProps, Depth - 1);
                    ElemJson->SetArrayField(TEXT("properties"), ElemProps);

                    ElementsArray.Add(MakeShared<FJsonValueObject>(ElemJson));
                }
                PropObj->SetArrayField(TEXT("elements"), ElementsArray);
            }
            else
            {
                FString ValueStr;
                Prop->ExportTextItem_Direct(ValueStr, ValueAddr, nullptr, nullptr, PPF_None);
                if (!ValueStr.IsEmpty())
                {
                    PropObj->SetStringField(TEXT("value"), ValueStr);
                }
            }
            OutArray.Add(MakeShared<FJsonValueObject>(PropObj));
            continue;
        }

        // --- All other properties – export as text ---
        FString ValueStr;
        Prop->ExportTextItem_Direct(ValueStr, ValueAddr, nullptr, nullptr, PPF_None);
        if (!ValueStr.IsEmpty())
        {
            PropObj->SetStringField(TEXT("value"), ValueStr);
        }

        OutArray.Add(MakeShared<FJsonValueObject>(PropObj));
    }
}

// ---------------------------------------------------------------------------
// HandleGetComponentProperties
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetComponentProperties(
    const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    FString ComponentName;
    if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
    }

    int32 MaxDepth = 2;
    if (Params->HasField(TEXT("max_depth")))
    {
        MaxDepth = FMath::Clamp(static_cast<int32>(Params->GetNumberField(TEXT("max_depth"))), 1, 4);
    }

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    FString Source;
    UObject* ComponentObj = FindComponentTemplate(Blueprint, ComponentName, Source);
    if (!ComponentObj)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("component_name"), ComponentName);
    ResultObj->SetStringField(TEXT("component_class"), ComponentObj->GetClass()->GetName());
    ResultObj->SetStringField(TEXT("source"), Source);

    TArray<TSharedPtr<FJsonValue>> PropsArray;
    SerializePropertiesToJson(ComponentObj, PropsArray, MaxDepth);
    ResultObj->SetArrayField(TEXT("properties"), PropsArray);

    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResultObj);
}
