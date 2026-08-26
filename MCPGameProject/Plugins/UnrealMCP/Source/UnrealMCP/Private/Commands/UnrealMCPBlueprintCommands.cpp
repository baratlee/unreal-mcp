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
#include "AnimationStateMachineSchema.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateNode.h"
#include "AnimStateAliasNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateConduitNode.h"
#include "AnimStateEntryNode.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_TransitionResult.h"
// AnimGraph node reflection (for method-A: anim_node_properties / property_bindings dump)
#include "AnimGraphNode_Base.h"
#include "JsonObjectConverter.h"
// Batch E: P0/P1 from UnrealMCP_API_ExpansionRequest.md
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
// [LEOCC] FCompilerResultsLog / FTokenizedMessage — needed for compile error capture
#include "KismetCompiler.h"
#include "Logging/TokenizedMessage.h"

namespace
{
    void CollectBlueprintGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
    {
        OutGraphs.Reset();
        if (!Blueprint) return;

        TArray<UEdGraph*> GraphStack;
        GraphStack.Append(Blueprint->UbergraphPages);
        GraphStack.Append(Blueprint->FunctionGraphs);
        GraphStack.Append(Blueprint->MacroGraphs);
        GraphStack.Append(Blueprint->DelegateSignatureGraphs);
        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
        {
            GraphStack.Append(Interface.Graphs);
        }

        TSet<UEdGraph*> VisitedGraphs;
        for (int32 Index = 0; Index < GraphStack.Num(); ++Index)
        {
            UEdGraph* Graph = GraphStack[Index];
            if (!Graph || VisitedGraphs.Contains(Graph)) continue;

            VisitedGraphs.Add(Graph);
            OutGraphs.Add(Graph);
            for (UEdGraph* SubGraph : Graph->SubGraphs)
            {
                if (SubGraph && !VisitedGraphs.Contains(SubGraph)) GraphStack.Add(SubGraph);
            }
        }
    }

    // How much pin payload to serialize. AnimGraph nodes carry InstancedStruct
    // text in DefaultValue that can be multi-KB per pin and easily blow past
    // the MCP socket response timeout, so callers can opt into a smaller shape.
    enum class EPinPayloadMode : uint8
    {
        Full,       // Verbatim DefaultValue (existing behavior).
        Summary,    // DefaultValue truncated to a short preview when long.
        NamesOnly,  // DefaultValue dropped entirely; keep type/links only.
    };

    enum class EGraphOutputProfile : uint8
    {
        Compact,    // Semantic graph data without editor-layout coordinates or empty containers.
        Full,       // Complete legacy response shape.
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

    EGraphOutputProfile ParseGraphOutputProfile(const FString& Raw)
    {
        if (Raw.Equals(TEXT("compact"), ESearchCase::IgnoreCase))
        {
            return EGraphOutputProfile::Compact;
        }
        return EGraphOutputProfile::Full;
    }

    EGraphOutputProfile ResolveGraphOutputProfile(const TSharedPtr<FJsonObject>& Params)
    {
        bool bCompactOutput = false;
        if (Params.IsValid() && Params->TryGetBoolField(TEXT("compact_output"), bCompactOutput))
        {
            return bCompactOutput ? EGraphOutputProfile::Compact : EGraphOutputProfile::Full;
        }

        FString OutputProfileRaw;
        if (Params.IsValid())
        {
            Params->TryGetStringField(TEXT("output_profile"), OutputProfileRaw);
        }
        return ParseGraphOutputProfile(OutputProfileRaw);
    }

    FString PinPayloadModeToString(EPinPayloadMode Mode)
    {
        switch (Mode)
        {
        case EPinPayloadMode::Summary:   return TEXT("summary");
        case EPinPayloadMode::NamesOnly: return TEXT("names_only");
        default:                         return TEXT("full");
        }
    }

    FString GraphOutputProfileToString(EGraphOutputProfile Profile)
    {
        return Profile == EGraphOutputProfile::Compact ? TEXT("compact") : TEXT("full");
    }

    // Threshold and preview length used by Summary mode.
    constexpr int32 GPinSummaryThreshold = 256;
    constexpr int32 GPinSummaryPreviewLen = 96;

    // Method A: serialize the three Details-panel-only dimensions of AnimGraph
    // nodes that would otherwise be invisible to callers:
    //   1. anim_node_properties — EditAnywhere UPROPERTY on the inner
    //      FAnimNode_* struct (e.g. TwoWayBlend's AlphaInputType / bEnabled /
    //      AlwaysUpdateChildren). These render in the Details panel but have
    //      no dedicated pin in most configurations.
    //   2. property_bindings   — entries in UAnimGraphNodeBinding_Base's
    //      PropertyBindings TMap (UE 5.x: the old per-node PropertyBindings_DEPRECATED
    //      map was moved onto the Instanced `Binding` sub-object).
    //   3. node_object_properties — EditAnywhere UPROPERTY on the editor-side
    //      UAnimGraphNode_* UObject itself (NOT the runtime struct). This
    //      surfaces `Tag` (UAnimGraphNode_Base private UPROPERTY used to look
    //      up nodes at runtime), `ShowPinForProperties`, `InitialUpdateFunction`
    //      / `BecomeRelevantFunction` / `UpdateFunction`, plus any subclass-
    //      specific editor fields. Excludes the inner FAnimNode_* (already in
    //      payload 1) and the Binding sub-object (already in payload 2).
    //
    // We reach both via reflection rather than hard-coded casts, so this works
    // for every UAnimGraphNode_* subclass regardless of its inner-node field
    // name (some use "Node", TwoWayBlend uses "BlendNode", etc.).
    //
    // Both payloads are skipped entirely in NamesOnly mode to keep that mode
    // maximally lightweight.
    TArray<TSharedPtr<FJsonValue>> SerializeAnimGraphPropertyBindings(UAnimGraphNode_Base* AnimNode)
    {
        TArray<TSharedPtr<FJsonValue>> BindingsArray;
        if (!AnimNode)
        {
            return BindingsArray;
        }

        FProperty* BindingMemberProp = AnimNode->GetClass()->FindPropertyByName(TEXT("Binding"));
        FObjectProperty* BindingObjProp = CastField<FObjectProperty>(BindingMemberProp);
        if (!BindingObjProp) return BindingsArray;

        UObject* BindingObj = BindingObjProp->GetObjectPropertyValue(
            BindingObjProp->ContainerPtrToValuePtr<void>(AnimNode));
        if (!BindingObj) return BindingsArray;

        FProperty* PropertyBindingsProp = BindingObj->GetClass()->FindPropertyByName(TEXT("PropertyBindings"));
        FMapProperty* MapProp = CastField<FMapProperty>(PropertyBindingsProp);
        if (!MapProp) return BindingsArray;

        void* MapContainer = MapProp->ContainerPtrToValuePtr<void>(BindingObj);
        FScriptMapHelper MapHelper(MapProp, MapContainer);

        UScriptStruct* BindingStructDef = FAnimGraphNodePropertyBinding::StaticStruct();
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

        return BindingsArray;
    }

    void SerializeAnimGraphNodeExtras(
        UAnimGraphNode_Base* AnimNode,
        TSharedPtr<FJsonObject> NodeObj,
        EPinPayloadMode Mode,
        EGraphOutputProfile OutputProfile)
    {
        if (!AnimNode || !NodeObj.IsValid() || Mode == EPinPayloadMode::NamesOnly)
        {
            return;
        }

        if (OutputProfile == EGraphOutputProfile::Compact)
        {
            for (TFieldIterator<FStructProperty> PropIt(AnimNode->GetClass()); PropIt; ++PropIt)
            {
                FStructProperty* StructProp = *PropIt;
                if (StructProp && StructProp->Struct && StructProp->Struct->GetName().StartsWith(TEXT("AnimNode_")))
                {
                    NodeObj->SetStringField(TEXT("anim_node_struct"), StructProp->Struct->GetName());
                    break;
                }
            }
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

            FJsonObjectConverter::UStructToJsonObject(
                StructProp->Struct,
                StructData,
                InnerObj,
                /*CheckFlags=*/CPF_Edit,
                /*SkipFlags=*/CPF_Transient | CPF_DuplicateTransient);

            NodeObj->SetStringField(TEXT("anim_node_struct"), StructName);
            if (OutputProfile == EGraphOutputProfile::Full || InnerObj->Values.Num() > 0)
            {
                NodeObj->SetObjectField(TEXT("anim_node_properties"), InnerObj);
            }
            break;
        }

        // (2) PropertyBindings — reflected from the editor-side Binding object.
        TArray<TSharedPtr<FJsonValue>> PropertyBindings = SerializeAnimGraphPropertyBindings(AnimNode);
        if (OutputProfile == EGraphOutputProfile::Full || PropertyBindings.Num() > 0)
        {
            NodeObj->SetArrayField(TEXT("property_bindings"), PropertyBindings);
        }

        // (3) Node-UObject-level UPROPERTY (Tag, ShowPinForProperties,
        // InitialUpdateFunction etc.). Walks the UClass with TFieldIterator so
        // parent-class properties (UAnimGraphNode_Base) come along automatically.
        // Skips the inner FAnimNode_* StructProperty and the Binding ObjectProperty
        // because those already drive payloads (1) and (2) respectively.
        TSharedRef<FJsonObject> NodeObjProps = MakeShared<FJsonObject>();
        for (TFieldIterator<FProperty> ObjPropIt(AnimNode->GetClass()); ObjPropIt; ++ObjPropIt)
        {
            FProperty* ObjProp = *ObjPropIt;
            if (!ObjProp) continue;
            if (!ObjProp->HasAnyPropertyFlags(CPF_Edit)) continue;
            if (ObjProp->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated)) continue;

            // Skip the inner runtime FAnimNode_* struct — already in anim_node_properties
            if (FStructProperty* SP = CastField<FStructProperty>(ObjProp))
            {
                if (SP->Struct && SP->Struct->GetName().StartsWith(TEXT("AnimNode_")))
                {
                    continue;
                }
            }
            // Skip the Binding sub-object — already in property_bindings
            if (ObjProp->GetFName() == TEXT("Binding"))
            {
                continue;
            }

            const void* ValuePtr = ObjProp->ContainerPtrToValuePtr<void>(AnimNode);
            TSharedPtr<FJsonValue> JsonValue = FJsonObjectConverter::UPropertyToJsonValue(
                ObjProp,
                ValuePtr,
                /*CheckFlags=*/0,
                /*SkipFlags=*/CPF_Transient | CPF_DuplicateTransient);
            if (JsonValue.IsValid())
            {
                NodeObjProps->SetField(ObjProp->GetName(), JsonValue);
            }
        }
        if (OutputProfile == EGraphOutputProfile::Full || NodeObjProps->Values.Num() > 0)
        {
            NodeObj->SetObjectField(TEXT("node_object_properties"), NodeObjProps);
        }
    }

    // Serialize a single UEdGraphNode (and its pin links) to a JSON object.
    // Shared between HandleGetBlueprintInfo and HandleGetBlueprintFunctionGraph
    // so the two commands always agree on node/pin shape.
    TSharedPtr<FJsonObject> SerializeGraphNodeToJson(
        UEdGraphNode* Node,
        EPinPayloadMode Mode = EPinPayloadMode::Full,
        EGraphOutputProfile OutputProfile = EGraphOutputProfile::Full)
    {
        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        if (!Node)
        {
            return NodeObj;
        }

        NodeObj->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
        NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        if (OutputProfile == EGraphOutputProfile::Full)
        {
            NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
            NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);
        }

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
        if (OutputProfile == EGraphOutputProfile::Full || PinsArray.Num() > 0)
        {
            NodeObj->SetArrayField(TEXT("pins"), PinsArray);
        }

        // Method A: AnimGraph-only Details-panel + PropertyBindings dump.
        // Non-AnimGraph nodes (K2Node_* in EventGraph / user functions) just
        // fall through the Cast and get no extra fields.
        if (UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node))
        {
            SerializeAnimGraphNodeExtras(AnimNode, NodeObj, Mode, OutputProfile);
        }

        return NodeObj;
    }

    TSharedPtr<FJsonObject> SerializeTopologyNodeToJson(UEdGraphNode* Node)
    {
        TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
        if (!Node)
        {
            return NodeObj;
        }

        NodeObj->SetStringField(TEXT("guid"), Node->NodeGuid.ToString());
        NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        NodeObj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

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

        if (UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node))
        {
            SerializeAnimGraphNodeExtras(
                AnimNode,
                NodeObj,
                EPinPayloadMode::Summary,
                EGraphOutputProfile::Compact);
        }

        return NodeObj;
    }

    TArray<TSharedPtr<FJsonValue>> SerializeGraphEdgesToJson(
        const TArray<UEdGraphNode*>& Nodes,
        const TMap<const UEdGraphNode*, int32>& NodeIndices)
    {
        TArray<TSharedPtr<FJsonValue>> EdgesArray;
        for (UEdGraphNode* Node : Nodes)
        {
            if (!Node) continue;

            const int32* SourceNodeIndex = NodeIndices.Find(Node);
            if (!SourceNodeIndex) continue;

            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (!Pin || Pin->Direction != EGPD_Output) continue;

                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;

                    const int32* TargetNodeIndex = NodeIndices.Find(LinkedPin->GetOwningNode());
                    if (!TargetNodeIndex) continue;

                    TArray<TSharedPtr<FJsonValue>> Edge;
                    Edge.Add(MakeShared<FJsonValueNumber>(*SourceNodeIndex));
                    Edge.Add(MakeShared<FJsonValueString>(Pin->PinName.ToString()));
                    Edge.Add(MakeShared<FJsonValueNumber>(*TargetNodeIndex));
                    Edge.Add(MakeShared<FJsonValueString>(LinkedPin->PinName.ToString()));
                    EdgesArray.Add(MakeShared<FJsonValueArray>(Edge));
                }
            }
        }
        return EdgesArray;
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
    else if (CommandType == TEXT("get_anim_graph_node_property_bindings"))
    {
        return HandleGetAnimGraphNodePropertyBindings(Params);
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
    else if (CommandType == TEXT("add_anim_state"))
    {
        return HandleAddAnimState(Params);
    }
    else if (CommandType == TEXT("add_anim_transition"))
    {
        return HandleAddAnimTransition(Params);
    }
    else if (CommandType == TEXT("set_anim_transition_properties"))
    {
        return HandleSetAnimTransitionProperties(Params);
    }
    else if (CommandType == TEXT("set_anim_state_entry"))
    {
        return HandleSetAnimStateEntry(Params);
    }
    else if (CommandType == TEXT("remove_anim_state"))
    {
        return HandleRemoveAnimState(Params);
    }
    else if (CommandType == TEXT("remove_anim_transition"))
    {
        return HandleRemoveAnimTransition(Params);
    }
    else if (CommandType == TEXT("rename_anim_state_machine"))
    {
        return HandleRenameAnimStateMachine(Params);
    }
    else if (CommandType == TEXT("get_component_properties"))
    {
        return HandleGetComponentProperties(Params);
    }
    else if (CommandType == TEXT("get_blueprint_cdo_properties"))
    {
        return HandleGetBlueprintCDOProperties(Params);
    }
    // Batch E (Docs/UnrealMCP_API_ExpansionRequest.md)
    else if (CommandType == TEXT("create_blueprint_from_parent_blueprint"))
    {
        return HandleCreateBlueprintFromParentBlueprint(Params);
    }
    else if (CommandType == TEXT("add_anim_graph_node"))
    {
        return HandleAddAnimGraphNode(Params);
    }
    else if (CommandType == TEXT("connect_anim_graph_nodes"))
    {
        return HandleConnectAnimGraphNodes(Params);
    }
    else if (CommandType == TEXT("set_graph_node_pin_default_value"))
    {
        return HandleSetGraphNodePinDefaultValue(Params);
    }
    else if (CommandType == TEXT("set_anim_graph_node_property"))
    {
        return HandleSetAnimGraphNodeProperty(Params);
    }
    else if (CommandType == TEXT("add_blueprint_function_graph"))
    {
        return HandleAddBlueprintFunctionGraph(Params);
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

    // [LEOCC] name 智能解析：以 '/' 开头视为完整资产路径，按最后一个 '/' 拆 PackagePath + AssetName；
    // 否则保留旧默认 '/Game/Blueprints/' 前缀。这样可避免传完整路径时被强行拼前缀产生 '//' 双斜杠
    // （UE 的 CreatePackage 会在 PackageName 含 '//' 时触发 Fatal 断言）。
    FString PackagePath;
    FString AssetName;
    if (BlueprintName.StartsWith(TEXT("/")))
    {
        int32 LastSlashIdx = INDEX_NONE;
        if (BlueprintName.FindLastChar(TEXT('/'), LastSlashIdx) && LastSlashIdx > 0)
        {
            PackagePath = BlueprintName.Left(LastSlashIdx + 1); // 含末尾 '/'
            AssetName = BlueprintName.Mid(LastSlashIdx + 1);
        }
        else
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Invalid blueprint path: %s"), *BlueprintName));
        }
    }
    else
    {
        PackagePath = TEXT("/Game/Blueprints/");
        AssetName = BlueprintName;
    }

    if (AssetName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Invalid blueprint name (empty asset name): %s"), *BlueprintName));
    }

    const FString FullPackageName = PackagePath + AssetName;

    // [LEOCC] 防御性护栏：合成后的 PackageName 一旦包含 '//' 直接返回 error，禁止流到 CreatePackage
    // 触发引擎 Fatal 断言（UObjectGlobals.cpp 中 "double slashes" 检查）。
    if (FullPackageName.Contains(TEXT("//")))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Invalid package name (contains '//'): %s"), *FullPackageName));
    }

    if (UEditorAssetLibrary::DoesAssetExist(FullPackageName))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint already exists: %s"), *FullPackageName));
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
        UClass* FoundClass = nullptr;

        // [LEOCC] parent_class 智能解析：
        // - 含 '/' 或 '.' 视为完整路径（如 /Script/PrjKunlun.GA_KLComboAttackBase 或 /Game/.../BP_X.BP_X_C），
        //   直接 LoadClass<UObject>，不强行套 "A" 前缀（旧逻辑会把 /Script/... 变成 A/Script/...）；
        // - 否则才走老的"加 A 前缀 + Engine/Game 兜底"。
        // 失败时返回 error 而不是静默回退到 AActor，避免父类被悄悄错配。
        if (ParentClass.Contains(TEXT("/")) || ParentClass.Contains(TEXT(".")))
        {
            FString ClassPath = ParentClass;
            // /Script/Module.ClassName 形式不附加 _C；蓝图类（/Game/...）才需要 _C 后缀
            if (ClassPath.StartsWith(TEXT("/Game/")) && !ClassPath.EndsWith(TEXT("_C")))
            {
                ClassPath += TEXT("_C");
            }
            FoundClass = LoadClass<UObject>(nullptr, *ClassPath);
        }
        else
        {
            FString ClassName = ParentClass;
            if (!ClassName.StartsWith(TEXT("A")))
            {
                ClassName = TEXT("A") + ClassName;
            }

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
                const FString EngineClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
                FoundClass = LoadClass<UObject>(nullptr, *EngineClassPath);
                if (!FoundClass)
                {
                    const FString GameClassPath = FString::Printf(TEXT("/Script/Game.%s"), *ClassName);
                    FoundClass = LoadClass<UObject>(nullptr, *GameClassPath);
                }
            }
        }

        if (FoundClass)
        {
            SelectedParentClass = FoundClass;
            UE_LOG(LogTemp, Log, TEXT("Successfully set parent class to '%s'"), *ParentClass);
        }
        else
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Could not resolve parent class '%s'"), *ParentClass));
        }
    }

    Factory->ParentClass = SelectedParentClass;

    // Create the blueprint
    UPackage* Package = CreatePackage(*FullPackageName);
    UBlueprint* NewBlueprint = Cast<UBlueprint>(Factory->FactoryCreateNew(UBlueprint::StaticClass(), Package, *AssetName, RF_Standalone | RF_Public, nullptr, GWarn));

    if (NewBlueprint)
    {
        // Notify the asset registry
        FAssetRegistryModule::AssetCreated(NewBlueprint);

        // Mark the package dirty
        Package->MarkPackageDirty();

        TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
        ResultObj->SetStringField(TEXT("name"), AssetName);
        ResultObj->SetStringField(TEXT("path"), FullPackageName);
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

    // Find the component template using unified search (SCS + inherited + native)
    UObject* ComponentTemplate = nullptr;
    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Searching for component %s"), *ComponentName);

    // 1. Try current BP's own SCS first
    if (Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node && Node->ComponentTemplate &&
                Node->GetVariableName().ToString() == ComponentName)
            {
                ComponentTemplate = Node->ComponentTemplate;
                UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Found in SCS"));
                break;
            }
        }
    }

    // 2. If not in SCS, search inherited/native via generated class CDO
    if (!ComponentTemplate && Blueprint->GeneratedClass)
    {
        AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
        if (CDO)
        {
            TInlineComponentArray<UActorComponent*> Components;
            CDO->GetComponents(Components);
            for (UActorComponent* Comp : Components)
            {
                if (Comp && Comp->GetName() == ComponentName)
                {
                    ComponentTemplate = Comp;
                    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Found on generated class CDO (inherited/native)"));
                    break;
                }
            }
        }
    }

    if (!ComponentTemplate)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty - Component not found: %s"), *ComponentName);
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Component not found: %s"), *ComponentName));
    }

    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty - Component found: %s (Class: %s)"),
        *ComponentName, *ComponentTemplate->GetClass()->GetName());

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

        // [LEOCC] 点分嵌套路径（如 BodyInstance.CollisionProfileName）直接走 SetObjectProperty，
        // FindFProperty 只做精确名匹配，无法处理点分路径。
        if (PropertyName.Contains(TEXT(".")))
        {
            ComponentTemplate->Modify();
            FString NestedError;
            if (FUnrealMCPCommonUtils::SetObjectProperty(ComponentTemplate, PropertyName, JsonValue, NestedError))
            {
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
                ResultObj->SetStringField(TEXT("component"), ComponentName);
                ResultObj->SetStringField(TEXT("property"), PropertyName);
                ResultObj->SetBoolField(TEXT("success"), true);
                return ResultObj;
            }
            return FUnrealMCPCommonUtils::CreateErrorResponse(NestedError);
        }

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

    // [LEOCC] Use FCompilerResultsLog to capture errors/warnings instead of a bare compile call
    FCompilerResultsLog ResultsLog;
    // [LEOCC] bSilentMode = true: suppress duplicate output to the UE Output Log
    ResultsLog.bSilentMode = true;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &ResultsLog);

    // [LEOCC] Build the compile_log array: severity + message per entry
    TArray<TSharedPtr<FJsonValue>> LogEntries;
    for (const TSharedRef<FTokenizedMessage>& Msg : ResultsLog.Messages)
    {
        EMessageSeverity::Type Sev = Msg->GetSeverity();
        FString SevStr;
        // [LEOCC] CriticalError 在 UE 5.7 已移除，只判 Error
        if (Sev == EMessageSeverity::Error)
        {
            SevStr = TEXT("error");
        }
        else if (Sev == EMessageSeverity::Warning || Sev == EMessageSeverity::PerformanceWarning)
        {
            SevStr = TEXT("warning");
        }
        else
        {
            SevStr = TEXT("info");
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("severity"), SevStr);
        // [LEOCC] FTokenizedMessage 无 GetMessageText，用 ToText() 拼接所有 token
        Entry->SetStringField(TEXT("message"), Msg->ToText().ToString());
        LogEntries.Add(MakeShared<FJsonValueObject>(Entry));
    }

    // [LEOCC] success = no errors in the results log
    bool bSuccess = (ResultsLog.NumErrors == 0);

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("name"), BlueprintName);
    ResultObj->SetBoolField(TEXT("compiled"), true);
    ResultObj->SetBoolField(TEXT("success"), bSuccess);
    ResultObj->SetNumberField(TEXT("error_count"), static_cast<double>(ResultsLog.NumErrors));
    ResultObj->SetNumberField(TEXT("warning_count"), static_cast<double>(ResultsLog.NumWarnings));
    ResultObj->SetArrayField(TEXT("compile_log"), LogEntries);
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

        // [LEOCC] 子蓝图落盘修复：写 CDO 前先让 Blueprint 对象和 CDO 一起进入 UE 事务系统，
        // 避免编译时 CDO 重建覆盖掉只写到 in-memory CDO 而未登记 override 的改动。
        Blueprint->Modify();
        DefaultObject->Modify();

        FString ErrorMessage;
        if (FUnrealMCPCommonUtils::SetObjectProperty(DefaultObject, PropertyName, JsonValue, ErrorMessage))
        {
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

            // [LEOCC] 子蓝图落盘修复 Part 2：若属性在本 BP 的 NewVariables 中声明，
            // 把导出值同步写回 FBPVariableDescription::DefaultValue，确保编译期 CDO 重建使用正确值。
            // 继承自父 BP（非 C++ 基类）的属性不在 NewVariables 里，此路径无法覆盖，会输出 warning。
            const FName PropFName(*PropertyName);
            bool bFoundInNewVars = false;
            if (FProperty* Prop = DefaultObject->GetClass()->FindPropertyByName(PropFName))
            {
                for (FBPVariableDescription& VarDesc : Blueprint->NewVariables)
                {
                    if (VarDesc.VarName == PropFName)
                    {
                        FString ExportedValue;
                        void* PropAddr = Prop->ContainerPtrToValuePtr<void>(DefaultObject);
                        Prop->ExportTextItem_Direct(ExportedValue, PropAddr, nullptr, DefaultObject, PPF_None);
                        VarDesc.DefaultValue = ExportedValue;
                        bFoundInNewVars = true;
                        break;
                    }
                }
            }

            TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
            ResultObj->SetStringField(TEXT("property"), PropertyName);
            ResultObj->SetBoolField(TEXT("success"), true);
            if (!bFoundInNewVars)
            {
                ResultObj->SetStringField(TEXT("warning"),
                    TEXT("Property not found in this blueprint's NewVariables — if inherited from a parent blueprint, compile may reset this value. Verify with get_blueprint_cdo_properties after compile+save."));
            }
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

    const EGraphOutputProfile OutputProfile = ResolveGraphOutputProfile(Params);

    bool bTopologyOnly = false;
    Params->TryGetBoolField(TEXT("topology_only"), bTopologyOnly);

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    TArray<UEdGraph*> AvailableGraphs;
    CollectBlueprintGraphs(Blueprint, AvailableGraphs);

    UEdGraph* TargetGraph = nullptr;
    for (UEdGraph* Graph : AvailableGraphs)
    {
        if (Graph && Graph->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
        {
            TargetGraph = Graph;
            break;
        }
    }

    if (!TargetGraph)
    {
        TArray<FString> AvailableNames;
        for (UEdGraph* Graph : AvailableGraphs)
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
    ResultObj->SetStringField(TEXT("output_profile"),
        bTopologyOnly ? TEXT("topology") : GraphOutputProfileToString(OutputProfile));
    ResultObj->SetBoolField(TEXT("compact_output"),
        bTopologyOnly || OutputProfile == EGraphOutputProfile::Compact);
    ResultObj->SetBoolField(TEXT("topology_only"), bTopologyOnly);
    ResultObj->SetStringField(TEXT("pin_payload_mode"),
        bTopologyOnly ? TEXT("names_only") : PinPayloadModeToString(PayloadMode));

    TArray<TSharedPtr<FJsonValue>> NodesArray;
    TMap<const UEdGraphNode*, int32> NodeIndices;
    for (UEdGraphNode* Node : TargetGraph->Nodes)
    {
        if (!Node) continue;

        NodeIndices.Add(Node, NodesArray.Num());
        NodesArray.Add(MakeShared<FJsonValueObject>(
            bTopologyOnly
                ? SerializeTopologyNodeToJson(Node)
                : SerializeGraphNodeToJson(Node, PayloadMode, OutputProfile)));
    }
    ResultObj->SetArrayField(TEXT("nodes"), NodesArray);

    if (bTopologyOnly)
    {
        TArray<TSharedPtr<FJsonValue>> EdgesArray = SerializeGraphEdgesToJson(TargetGraph->Nodes, NodeIndices);
        ResultObj->SetNumberField(TEXT("edge_count"), EdgesArray.Num());
        ResultObj->SetArrayField(TEXT("edges"), EdgesArray);
    }

    return ResultObj;
}

// ---------------------------------------------------------------------------
// State Machine tools
// ---------------------------------------------------------------------------

// Helper: find a StateMachine in any Blueprint graph, including Anim Layer
// Interface implementation graphs. When StateMachineName is empty, returns
// the first StateMachine discovered.
namespace
{
    UAnimationStateMachineGraph* FindStateMachineGraph(
        UBlueprint* Blueprint,
        const FString& StateMachineName)
    {
        if (!Blueprint) return nullptr;

        TArray<UEdGraph*> Graphs;
        CollectBlueprintGraphs(Blueprint, Graphs);
        for (UEdGraph* Graph : Graphs)
        {
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

    // Collect State Aliases separately so the existing state_count/states contract remains stable.
    TArray<UAnimStateAliasNode*> StateAliasNodes;
    SMGraph->GetNodesOfClass(StateAliasNodes);

    TArray<TSharedPtr<FJsonValue>> StateAliasesArray;
    for (UAnimStateAliasNode* StateAliasNode : StateAliasNodes)
    {
        if (!StateAliasNode) continue;

        TArray<FString> AliasedStateNames;
        for (const TWeakObjectPtr<UAnimStateNodeBase>& AliasedState : StateAliasNode->GetAliasedStates())
        {
            if (const UAnimStateNodeBase* AliasedStateNode = AliasedState.Get())
            {
                AliasedStateNames.Add(AliasedStateNode->GetStateName());
            }
        }
        AliasedStateNames.Sort();

        TArray<TSharedPtr<FJsonValue>> AliasedStatesArray;
        AliasedStatesArray.Reserve(AliasedStateNames.Num());
        for (const FString& AliasedStateName : AliasedStateNames)
        {
            AliasedStatesArray.Add(MakeShared<FJsonValueString>(AliasedStateName));
        }

        TSharedPtr<FJsonObject> StateAliasObj = MakeShared<FJsonObject>();
        StateAliasObj->SetStringField(TEXT("name"), StateAliasNode->GetStateName());
        StateAliasObj->SetBoolField(TEXT("global_alias"), StateAliasNode->bGlobalAlias);
        StateAliasObj->SetNumberField(TEXT("aliased_state_count"), AliasedStatesArray.Num());
        StateAliasObj->SetArrayField(TEXT("aliased_states"), AliasedStatesArray);
        StateAliasesArray.Add(MakeShared<FJsonValueObject>(StateAliasObj));
    }
    ResultObj->SetNumberField(TEXT("state_alias_count"), StateAliasesArray.Num());
    ResultObj->SetArrayField(TEXT("state_aliases"), StateAliasesArray);

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

    EGraphOutputProfile OutputProfile = ResolveGraphOutputProfile(Params);

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
    ResultObj->SetStringField(TEXT("output_profile"), GraphOutputProfileToString(OutputProfile));
    ResultObj->SetBoolField(TEXT("compact_output"), OutputProfile == EGraphOutputProfile::Compact);
    ResultObj->SetStringField(TEXT("pin_payload_mode"), PinPayloadModeToString(PayloadMode));

    TArray<TSharedPtr<FJsonValue>> NodesArray;
    for (UEdGraphNode* Node : TargetBoundGraph->Nodes)
    {
        if (!Node) continue;
        NodesArray.Add(MakeShared<FJsonValueObject>(SerializeGraphNodeToJson(Node, PayloadMode, OutputProfile)));
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

    EGraphOutputProfile OutputProfile = ResolveGraphOutputProfile(Params);

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
    ResultObj->SetStringField(TEXT("output_profile"), GraphOutputProfileToString(OutputProfile));
    ResultObj->SetBoolField(TEXT("compact_output"), OutputProfile == EGraphOutputProfile::Compact);
    ResultObj->SetStringField(TEXT("pin_payload_mode"), PinPayloadModeToString(PayloadMode));

    TArray<TSharedPtr<FJsonValue>> NodesArray;
    for (UEdGraphNode* Node : CondGraph->Nodes)
    {
        if (!Node) continue;
        NodesArray.Add(MakeShared<FJsonValueObject>(SerializeGraphNodeToJson(Node, PayloadMode, OutputProfile)));
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
namespace
{
    UAnimStateNode* FindAnimState(UAnimationStateMachineGraph* Graph, const FString& StateName)
    {
        if (!Graph) return nullptr;
        TArray<UAnimStateNode*> States;
        Graph->GetNodesOfClass(States);
        for (UAnimStateNode* State : States)
        {
            if (State && State->GetStateName().Equals(StateName, ESearchCase::IgnoreCase)) return State;
        }
        return nullptr;
    }

    UAnimStateNodeBase* FindAnimStateEndpoint(UAnimationStateMachineGraph* Graph, const FString& StateName)
    {
        if (UAnimStateNode* State = FindAnimState(Graph, StateName))
        {
            return State;
        }
        if (!Graph) return nullptr;

        TArray<UAnimStateConduitNode*> Conduits;
        Graph->GetNodesOfClass(Conduits);
        for (UAnimStateConduitNode* Conduit : Conduits)
        {
            if (Conduit && Conduit->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
            {
                return Conduit;
            }
        }

        TArray<UAnimStateAliasNode*> StateAliases;
        Graph->GetNodesOfClass(StateAliases);
        for (UAnimStateAliasNode* StateAlias : StateAliases)
        {
            if (StateAlias && StateAlias->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
            {
                return StateAlias;
            }
        }
        return nullptr;
    }

    UAnimStateTransitionNode* FindAnimTransition(UAnimationStateMachineGraph* Graph, const FString& Source, const FString& Target)
    {
        if (!Graph) return nullptr;
        TArray<UAnimStateTransitionNode*> Transitions;
        Graph->GetNodesOfClass(Transitions);
        for (UAnimStateTransitionNode* Transition : Transitions)
        {
            const UAnimStateNodeBase* Previous = Transition ? Transition->GetPreviousState() : nullptr;
            const UAnimStateNodeBase* Next = Transition ? Transition->GetNextState() : nullptr;
            if (Previous && Next
                && Previous->GetStateName().Equals(Source, ESearchCase::IgnoreCase)
                && Next->GetStateName().Equals(Target, ESearchCase::IgnoreCase))
            {
                return Transition;
            }
        }
        return nullptr;
    }

    UBlueprint* ResolveAnimBlueprint(const FString& BlueprintPath)
    {
        UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
        return Blueprint ? Blueprint : FUnrealMCPCommonUtils::FindBlueprintByName(BlueprintPath);
    }
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleAddAnimState(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, MachineName, StateName;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("state_machine_name"), MachineName)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
    if (!Params->TryGetStringField(TEXT("state_name"), StateName)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));

    UBlueprint* Blueprint = ResolveAnimBlueprint(BlueprintPath);
    UAnimationStateMachineGraph* Graph = FindStateMachineGraph(Blueprint, MachineName);
    if (!Blueprint || !Graph) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint or State Machine not found"));
    if (FindAnimState(Graph, StateName)) return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("State already exists: %s"), *StateName));

    FVector2D Position(0.0f, 0.0f);
    if (Params->HasField(TEXT("node_position"))) Position = FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("node_position"));
    Blueprint->Modify();
    Graph->Modify();
    UAnimStateNode* State = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(
        Graph, NewObject<UAnimStateNode>(), FVector2f(static_cast<float>(Position.X), static_cast<float>(Position.Y)), false);
    if (!State) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create Anim State"));
    State->OnRenameNode(StateName);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("state_machine_name"), MachineName);
    Result->SetStringField(TEXT("state_name"), State->GetStateName());
    Result->SetStringField(TEXT("node_guid"), State->NodeGuid.ToString());
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleAddAnimTransition(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, MachineName, Source, Target;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("state_machine_name"), MachineName)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
    if (!Params->TryGetStringField(TEXT("source_state"), Source)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_state' parameter"));
    if (!Params->TryGetStringField(TEXT("target_state"), Target)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_state' parameter"));

    UBlueprint* Blueprint = ResolveAnimBlueprint(BlueprintPath);
    UAnimationStateMachineGraph* Graph = FindStateMachineGraph(Blueprint, MachineName);
    UAnimStateNodeBase* SourceState = FindAnimStateEndpoint(Graph, Source);
    UAnimStateNodeBase* TargetState = FindAnimStateEndpoint(Graph, Target);
    if (!Blueprint || !Graph || !SourceState || !TargetState) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint, State Machine, source state, or target state not found"));
    if (FindAnimTransition(Graph, Source, Target)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Transition already exists"));
    Blueprint->Modify();
    Graph->Modify();
    if (!Graph->GetSchema()->TryCreateConnection(SourceState->GetOutputPin(), TargetState->GetInputPin()))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("State Machine schema rejected the transition"));

    UAnimStateTransitionNode* Transition = FindAnimTransition(Graph, Source, Target);
    if (!Transition) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Transition node was not created"));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("state_machine_name"), MachineName);
    Result->SetStringField(TEXT("source_state"), Source);
    Result->SetStringField(TEXT("target_state"), Target);
    Result->SetStringField(TEXT("node_guid"), Transition->NodeGuid.ToString());
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetAnimTransitionProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, MachineName, Source, Target;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("state_machine_name"), MachineName)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
    if (!Params->TryGetStringField(TEXT("source_state"), Source)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_state' parameter"));
    if (!Params->TryGetStringField(TEXT("target_state"), Target)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_state' parameter"));

    UBlueprint* Blueprint = ResolveAnimBlueprint(BlueprintPath);
    UAnimStateTransitionNode* Transition = FindAnimTransition(FindStateMachineGraph(Blueprint, MachineName), Source, Target);
    if (!Blueprint || !Transition) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint or transition not found"));

    FString LogicType;
    if (Params->TryGetStringField(TEXT("logic_type"), LogicType)
        && !LogicType.Equals(TEXT("Standard"), ESearchCase::IgnoreCase)
        && !LogicType.Equals(TEXT("Inertialization"), ESearchCase::IgnoreCase))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("logic_type must be Standard or Inertialization"));
    }

    FString BlendMode;
    if (Params->TryGetStringField(TEXT("blend_mode"), BlendMode)
        && !BlendMode.Equals(TEXT("Linear"), ESearchCase::IgnoreCase)
        && !BlendMode.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase)
        && !BlendMode.Equals(TEXT("HermiteCubic"), ESearchCase::IgnoreCase))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("blend_mode must be Linear, Cubic, or HermiteCubic"));
    }

    Blueprint->Modify();
    Transition->Modify();

    double Number = 0.0;
    bool Flag = false;
    if (Params->TryGetNumberField(TEXT("priority"), Number)) Transition->PriorityOrder = FMath::Max(1, FMath::RoundToInt(Number));
    if (Params->TryGetNumberField(TEXT("crossfade_duration"), Number)) Transition->CrossfadeDuration = static_cast<float>(FMath::Max(0.0, Number));
    if (Params->TryGetBoolField(TEXT("automatic_rule"), Flag)) Transition->bAutomaticRuleBasedOnSequencePlayerInState = Flag;
    if (Params->TryGetNumberField(TEXT("automatic_rule_trigger_time"), Number)) Transition->AutomaticRuleTriggerTime = Number;
    if (Params->TryGetBoolField(TEXT("bidirectional"), Flag)) Transition->Bidirectional = Flag;
    if (Params->TryGetBoolField(TEXT("disabled"), Flag)) Transition->bDisabled = Flag;
    if (!LogicType.IsEmpty())
    {
        Transition->LogicType = LogicType.Equals(TEXT("Standard"), ESearchCase::IgnoreCase)
            ? ETransitionLogicType::TLT_StandardBlend
            : ETransitionLogicType::TLT_Inertialization;
    }
    if (!BlendMode.IsEmpty())
    {
        Transition->BlendMode = BlendMode.Equals(TEXT("Linear"), ESearchCase::IgnoreCase)
            ? EAlphaBlendOption::Linear
            : EAlphaBlendOption::HermiteCubic;
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("state_machine_name"), MachineName);
    Result->SetStringField(TEXT("source_state"), Source);
    Result->SetStringField(TEXT("target_state"), Target);
    Result->SetNumberField(TEXT("priority"), Transition->PriorityOrder);
    Result->SetNumberField(TEXT("crossfade_duration"), Transition->CrossfadeDuration);
    Result->SetStringField(TEXT("blend_mode"), BlendOptionToString(Transition->BlendMode));
    Result->SetStringField(TEXT("logic_type"), TransitionLogicTypeToString(Transition->LogicType));
    Result->SetBoolField(TEXT("automatic_rule"), Transition->bAutomaticRuleBasedOnSequencePlayerInState);
    Result->SetNumberField(TEXT("automatic_rule_trigger_time"), Transition->AutomaticRuleTriggerTime);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetAnimStateEntry(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, MachineName, StateName;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("state_machine_name"), MachineName)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_machine_name' parameter"));
    if (!Params->TryGetStringField(TEXT("state_name"), StateName)) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
    UBlueprint* Blueprint = ResolveAnimBlueprint(BlueprintPath);
    UAnimationStateMachineGraph* Graph = FindStateMachineGraph(Blueprint, MachineName);
    UAnimStateNode* State = FindAnimState(Graph, StateName);
    if (!Blueprint || !Graph || !Graph->EntryNode || !State) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint, State Machine, entry node, or state not found"));
    Blueprint->Modify();
    Graph->Modify();
    Graph->EntryNode->Modify();
    UEdGraphPin* EntryPin = Graph->EntryNode->GetOutputPin();
    EntryPin->BreakAllPinLinks();
    if (!Graph->GetSchema()->TryCreateConnection(EntryPin, State->GetInputPin())) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to connect entry state"));
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("state_machine_name"), MachineName);
    Result->SetStringField(TEXT("entry_state"), State->GetStateName());
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleRemoveAnimState(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, MachineName, StateName;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || !Params->TryGetStringField(TEXT("state_machine_name"), MachineName) || !Params->TryGetStringField(TEXT("state_name"), StateName))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Required: blueprint_path, state_machine_name, state_name"));
    UBlueprint* Blueprint = ResolveAnimBlueprint(BlueprintPath);
    UAnimationStateMachineGraph* Graph = FindStateMachineGraph(Blueprint, MachineName);
    UAnimStateNode* State = FindAnimState(Graph, StateName);
    if (!Blueprint || !Graph || !State) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint or state not found"));
    if (Graph->EntryNode && Graph->EntryNode->GetOutputPin()->LinkedTo.Contains(State->GetInputPin()))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Cannot remove the entry state; set another entry state first"));
    Blueprint->Modify();
    Graph->Modify();
    State->Modify();
    State->DestroyNode();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return FUnrealMCPCommonUtils::CreateSuccessResponse();
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleRemoveAnimTransition(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, MachineName, Source, Target;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || !Params->TryGetStringField(TEXT("state_machine_name"), MachineName) || !Params->TryGetStringField(TEXT("source_state"), Source) || !Params->TryGetStringField(TEXT("target_state"), Target))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Required: blueprint_path, state_machine_name, source_state, target_state"));
    UBlueprint* Blueprint = ResolveAnimBlueprint(BlueprintPath);
    UAnimStateTransitionNode* Transition = FindAnimTransition(FindStateMachineGraph(Blueprint, MachineName), Source, Target);
    if (!Blueprint || !Transition) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint or transition not found"));
    Blueprint->Modify();
    Transition->Modify();
    Transition->DestroyNode();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return FUnrealMCPCommonUtils::CreateSuccessResponse();
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleRenameAnimStateMachine(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, MachineName, NewName;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || !Params->TryGetStringField(TEXT("state_machine_name"), MachineName) || !Params->TryGetStringField(TEXT("new_name"), NewName))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Required: blueprint_path, state_machine_name, new_name"));
    UBlueprint* Blueprint = ResolveAnimBlueprint(BlueprintPath);
    UAnimationStateMachineGraph* Graph = FindStateMachineGraph(Blueprint, MachineName);
    if (!Blueprint || !Graph || !Graph->OwnerAnimGraphNode) return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint or State Machine not found"));
    Blueprint->Modify();
    Graph->Modify();
    Graph->OwnerAnimGraphNode->Modify();
    Graph->OwnerAnimGraphNode->OnRenameNode(NewName);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("state_machine_name"), Graph->OwnerAnimGraphNode->GetStateMachineName());
    return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

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

    // For native/inherited components, prefer the generated class CDO which
    // holds Blueprint-level property overrides rather than parent defaults
    if (Source.StartsWith(TEXT("native:")) && Blueprint->GeneratedClass)
    {
        if (AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
        {
            TInlineComponentArray<UActorComponent*> Components;
            CDO->GetComponents(Components);
            for (UActorComponent* Comp : Components)
            {
                if (Comp && Comp->GetName() == ComponentName)
                {
                    ComponentObj = Comp;
                    break;
                }
            }
        }
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

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetBlueprintCDOProperties(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }

    int32 MaxDepth = 2;
    if (Params->HasField(TEXT("max_depth")))
    {
        MaxDepth = FMath::Clamp(static_cast<int32>(Params->GetNumberField(TEXT("max_depth"))), 1, 4);
    }

    FString CategoryFilter;
    Params->TryGetStringField(TEXT("category_filter"), CategoryFilter);

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    UClass* GenClass = Blueprint->GeneratedClass;
    if (!GenClass)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Blueprint has no GeneratedClass"));
    }

    UObject* CDO = GenClass->GetDefaultObject();
    if (!CDO)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get CDO"));
    }

    TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
    ResultObj->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    ResultObj->SetStringField(TEXT("class_name"), GenClass->GetName());
    ResultObj->SetStringField(TEXT("parent_class"), GenClass->GetSuperClass() ? GenClass->GetSuperClass()->GetName() : TEXT("None"));

    TArray<TSharedPtr<FJsonValue>> PropsArray;

    for (TFieldIterator<FProperty> PropIt(GenClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;
        if (!Prop) continue;

        if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
        if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;

        if (!CategoryFilter.IsEmpty())
        {
            const FString& Cat = Prop->GetMetaData(TEXT("Category"));
            if (!Cat.Contains(CategoryFilter, ESearchCase::IgnoreCase))
            {
                continue;
            }
        }

        TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
        PropObj->SetStringField(TEXT("name"), Prop->GetName());
        PropObj->SetStringField(TEXT("category"), Prop->GetMetaData(TEXT("Category")));

        FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
        FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);

        if (ObjProp)
        {
            UObject* SubObj = ObjProp->GetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(CDO));
            if (SubObj)
            {
                PropObj->SetStringField(TEXT("type"), TEXT("object"));
                PropObj->SetStringField(TEXT("value"), SubObj->GetPathName());
                PropObj->SetStringField(TEXT("object_class"), SubObj->GetClass()->GetName());

                if (MaxDepth > 1 && SubObj->IsIn(CDO))
                {
                    TArray<TSharedPtr<FJsonValue>> SubPropsArray;
                    SerializePropertiesToJson(SubObj, SubPropsArray, MaxDepth - 1);
                    PropObj->SetArrayField(TEXT("sub_properties"), SubPropsArray);
                }
            }
            else
            {
                PropObj->SetStringField(TEXT("type"), TEXT("object"));
                PropObj->SetStringField(TEXT("value"), TEXT("None"));
            }
        }
        else if (ArrayProp)
        {
            FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(CDO));
            FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner);

            if (InnerObjProp && ArrayHelper.Num() > 0)
            {
                PropObj->SetStringField(TEXT("type"), TEXT("TArray"));
                TArray<TSharedPtr<FJsonValue>> Elements;
                for (int32 i = 0; i < ArrayHelper.Num(); ++i)
                {
                    UObject* ElemObj = InnerObjProp->GetObjectPropertyValue(ArrayHelper.GetRawPtr(i));
                    if (ElemObj)
                    {
                        TSharedPtr<FJsonObject> ElemJson = MakeShared<FJsonObject>();
                        ElemJson->SetStringField(TEXT("class"), ElemObj->GetClass()->GetName());
                        ElemJson->SetStringField(TEXT("path"), ElemObj->GetPathName());

                        if (MaxDepth > 1 && ElemObj->IsIn(CDO))
                        {
                            TArray<TSharedPtr<FJsonValue>> SubPropsArray;
                            SerializePropertiesToJson(ElemObj, SubPropsArray, MaxDepth - 1);
                            ElemJson->SetArrayField(TEXT("properties"), SubPropsArray);
                        }

                        Elements.Add(MakeShared<FJsonValueObject>(ElemJson));
                    }
                }
                PropObj->SetArrayField(TEXT("elements"), Elements);
            }
            else
            {
                FString ValueStr;
                ArrayProp->ExportTextItem_Direct(ValueStr, ArrayProp->ContainerPtrToValuePtr<void>(CDO), nullptr, nullptr, PPF_None);
                PropObj->SetStringField(TEXT("type"), TEXT("TArray"));
                PropObj->SetStringField(TEXT("value"), ValueStr);
            }
        }
        else
        {
            FString ValueStr;
            Prop->ExportTextItem_Direct(ValueStr, Prop->ContainerPtrToValuePtr<void>(CDO), nullptr, nullptr, PPF_None);

            FString TypeStr;
            if (CastField<FBoolProperty>(Prop)) TypeStr = TEXT("bool");
            else if (CastField<FIntProperty>(Prop)) TypeStr = TEXT("int");
            else if (CastField<FFloatProperty>(Prop)) TypeStr = TEXT("float");
            else if (CastField<FDoubleProperty>(Prop)) TypeStr = TEXT("double");
            else if (CastField<FStrProperty>(Prop)) TypeStr = TEXT("string");
            else if (CastField<FNameProperty>(Prop)) TypeStr = TEXT("FName");
            else if (CastField<FEnumProperty>(Prop) || CastField<FByteProperty>(Prop)) TypeStr = TEXT("enum");
            else if (CastField<FStructProperty>(Prop)) TypeStr = CastField<FStructProperty>(Prop)->Struct->GetName();
            else TypeStr = Prop->GetCPPType();

            PropObj->SetStringField(TEXT("type"), TypeStr);
            PropObj->SetStringField(TEXT("value"), ValueStr);
        }

        PropsArray.Add(MakeShared<FJsonValueObject>(PropObj));
    }

    ResultObj->SetArrayField(TEXT("properties"), PropsArray);
    return FUnrealMCPCommonUtils::CreateSuccessResponse(ResultObj);
}

// =============================================================================
// Batch E (Docs/UnrealMCP_API_ExpansionRequest.md): Blueprint write extensions
//   - create_blueprint_from_parent_blueprint
//   - add_anim_graph_node / connect_anim_graph_nodes / set_anim_graph_node_property
//   - add_blueprint_function_graph
// =============================================================================

namespace
{
    // Walk Blueprint->UbergraphPages, ->FunctionGraphs, ->IntermediateGeneratedGraphs to find a
    // named graph (e.g. "AnimGraph" / "EventGraph" / user-defined function name).
    // Returns nullptr if not found. Case-insensitive match.
    UEdGraph* FindGraphInBlueprint(UBlueprint* Blueprint, const FString& GraphName)
    {
        if (!Blueprint) return nullptr;
        TArray<UEdGraph*> Graphs;
        CollectBlueprintGraphs(Blueprint, Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) return Graph;
        }
        return nullptr;
    }

    // Find a node by GUID anywhere in a blueprint (searches all graph collections including
    // sub-graphs reachable via SubGraphs). Returns the node and the owning graph.
    bool FindNodeByGuid(UBlueprint* Blueprint, const FString& Guid, UEdGraphNode*& OutNode, UEdGraph*& OutGraph)
    {
        OutNode = nullptr; OutGraph = nullptr;
        if (!Blueprint) return false;

        TArray<UEdGraph*> Graphs;
        CollectBlueprintGraphs(Blueprint, Graphs);
        for (UEdGraph* G : Graphs)
        {
            if (!G) continue;
            for (UEdGraphNode* N : G->Nodes)
            {
                if (N && N->NodeGuid.ToString() == Guid)
                {
                    OutNode = N; OutGraph = G;
                    return true;
                }
            }
        }
        return false;
    }

    // Split "/Game/Foo/Bar.Bar" or "/Game/Foo/Bar" to (package, asset).
    bool SplitAssetPathBP(const FString& InPath, FString& OutPackage, FString& OutAsset)
    {
        OutPackage = InPath;
        int32 Dot = INDEX_NONE;
        if (OutPackage.FindChar('.', Dot)) OutPackage = OutPackage.Left(Dot);
        int32 Slash = INDEX_NONE;
        if (!OutPackage.FindLastChar('/', Slash) || Slash + 1 >= OutPackage.Len()) return false;
        OutAsset = OutPackage.Mid(Slash + 1);
        return !OutAsset.IsEmpty();
    }
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleGetAnimGraphNodePropertyBindings(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, NodeGuid, GraphName, NodeClass;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    }
    Params->TryGetStringField(TEXT("node_guid"), NodeGuid);
    Params->TryGetStringField(TEXT("graph_name"), GraphName);
    Params->TryGetStringField(TEXT("node_class"), NodeClass);
    if (NodeGuid.IsEmpty() && GraphName.IsEmpty())
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            TEXT("Provide either 'node_guid' or 'graph_name'"));
    }

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
    }

    auto SerializeNodeBindings = [](UAnimGraphNode_Base* AnimNode, UEdGraph* Graph) -> TSharedPtr<FJsonObject>
    {
        TSharedPtr<FJsonObject> NodeResult = MakeShared<FJsonObject>();
        NodeResult->SetStringField(TEXT("node_guid"), AnimNode->NodeGuid.ToString());
        NodeResult->SetStringField(TEXT("node_class"), AnimNode->GetClass()->GetName());
        NodeResult->SetStringField(TEXT("node_title"), AnimNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
        NodeResult->SetStringField(TEXT("graph_name"), Graph ? Graph->GetName() : FString());
        NodeResult->SetStringField(TEXT("graph_class"), Graph ? Graph->GetClass()->GetName() : FString());
        NodeResult->SetArrayField(TEXT("bindings"), SerializeAnimGraphPropertyBindings(AnimNode));
        return NodeResult;
    };

    if (!NodeGuid.IsEmpty())
    {
        UEdGraphNode* Node = nullptr;
        UEdGraph* Graph = nullptr;
        if (!FindNodeByGuid(Blueprint, NodeGuid, Node, Graph) || !Node)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Node not found by GUID: %s"), *NodeGuid));
        }

        UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
        if (!AnimNode)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Node is not a UAnimGraphNode_Base (got %s)"), *Node->GetClass()->GetName()));
        }

        TSharedPtr<FJsonObject> Result = SerializeNodeBindings(AnimNode, Graph);
        Result->SetBoolField(TEXT("success"), true);
        Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
        return Result;
    }

    FString NormalizedNodeClass = NodeClass;
    NormalizedNodeClass.RemoveFromStart(TEXT("U"));
    TArray<UEdGraph*> Graphs;
    CollectBlueprintGraphs(Blueprint, Graphs);
    TArray<TSharedPtr<FJsonValue>> MatchingNodes;
    bool bFoundGraph = false;

    for (UEdGraph* Graph : Graphs)
    {
        if (!Graph) continue;
        if (!Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase)) continue;
        bFoundGraph = true;

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
            if (!AnimNode) continue;
            if (!NormalizedNodeClass.IsEmpty() &&
                !AnimNode->GetClass()->GetName().Equals(NormalizedNodeClass, ESearchCase::IgnoreCase))
            {
                continue;
            }
            MatchingNodes.Add(MakeShared<FJsonValueObject>(SerializeNodeBindings(AnimNode, Graph)));
        }
    }

    if (!bFoundGraph)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Graph not found: %s"), *GraphName));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("graph_name"), GraphName);
    if (!NormalizedNodeClass.IsEmpty()) Result->SetStringField(TEXT("node_class_filter"), NormalizedNodeClass);
    Result->SetNumberField(TEXT("node_count"), MatchingNodes.Num());
    Result->SetArrayField(TEXT("nodes"), MatchingNodes);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleCreateBlueprintFromParentBlueprint(const TSharedPtr<FJsonObject>& Params)
{
    FString ParentBPPath, NewAssetPath;
    if (!Params->TryGetStringField(TEXT("parent_blueprint_path"), ParentBPPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'parent_blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("new_asset_path"), NewAssetPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'new_asset_path' parameter"));

    UBlueprint* ParentBP = FUnrealMCPCommonUtils::FindBlueprintByPath(ParentBPPath);
    if (!ParentBP)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Parent blueprint not found: %s"), *ParentBPPath));

    UClass* ParentClass = Cast<UClass>(ParentBP->GeneratedClass);
    if (!ParentClass)
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Parent blueprint has no GeneratedClass (uncompiled?): %s"), *ParentBPPath));
    }

    FString PackageName, AssetName;
    if (!SplitAssetPathBP(NewAssetPath, PackageName, AssetName))
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Invalid new_asset_path: %s"), *NewAssetPath));

    if (UPackage* Existing = FindPackage(nullptr, *PackageName))
    {
        if (FindObject<UBlueprint>(Existing, *AssetName))
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Blueprint already exists: %s"), *NewAssetPath));
        }
    }

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("CreatePackage failed: %s"), *PackageName));
    Package->FullyLoad();

    // Pick the right Blueprint UClass to use — for AnimBlueprints we want UAnimBlueprint so the
    // resulting asset has FunctionGraphs[0]=AnimGraph etc. wired up by the engine.
    TSubclassOf<UBlueprint> BPClass = ParentBP->GetClass();
    TSubclassOf<UBlueprintGeneratedClass> BPGenClass = ParentBP->GeneratedClass ? ParentBP->GeneratedClass->GetClass() : UBlueprintGeneratedClass::StaticClass();

    UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
        ParentClass,
        Package,
        FName(*AssetName),
        BPTYPE_Normal,
        BPClass,
        BPGenClass,
        NAME_None);

    if (!NewBP)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("FKismetEditorUtilities::CreateBlueprint returned null"));

    FAssetRegistryModule::AssetCreated(NewBP);
    Package->MarkPackageDirty();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("asset_path"), NewBP->GetPathName());
    Result->SetStringField(TEXT("parent_blueprint_path"), ParentBP->GetPathName());
    Result->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
    Result->SetStringField(TEXT("blueprint_class"), NewBP->GetClass()->GetName());
    Result->SetBoolField(TEXT("saved"), false);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleAddAnimGraphNode(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, NodeClassName;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("node_class"), NodeClassName))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_class' parameter (e.g. 'AnimGraphNode_RetargetPoseFromMesh')"));

    FString GraphName = TEXT("AnimGraph");
    Params->TryGetStringField(TEXT("graph_name"), GraphName);

    FVector2D Pos(0.0f, 0.0f);
    if (Params->HasField(TEXT("node_position")))
    {
        Pos = FUnrealMCPCommonUtils::GetVector2DFromJson(Params, TEXT("node_position"));
    }

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));

    UEdGraph* Graph = FindGraphInBlueprint(Blueprint, GraphName);
    if (!Graph)
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Graph '%s' not found in blueprint %s"), *GraphName, *BlueprintPath));

    // Resolve the node UClass. Accept either a bare name ("AnimGraphNode_RetargetPoseFromMesh") or
    // a full classpath ("/Script/AnimGraph.AnimGraphNode_RetargetPoseFromMesh"). FindFirstObject
    // is more forgiving than FindObject for bare names.
    UClass* NodeClass = nullptr;
    if (NodeClassName.Contains(TEXT(".")) || NodeClassName.StartsWith(TEXT("/Script/")))
    {
        NodeClass = LoadClass<UEdGraphNode>(nullptr, *NodeClassName);
    }
    if (!NodeClass)
    {
        NodeClass = FindFirstObject<UClass>(*NodeClassName, EFindFirstObjectOptions::None, ELogVerbosity::Warning);
    }
    if (!NodeClass || !NodeClass->IsChildOf(UAnimGraphNode_Base::StaticClass()))
    {
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("node_class '%s' is not a UAnimGraphNode_Base subclass"), *NodeClassName));
    }

    UAnimGraphNode_Base* NewNode = NewObject<UAnimGraphNode_Base>(Graph, NodeClass, NAME_None, RF_Transactional);
    if (!NewNode)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("NewObject<UAnimGraphNode_Base> returned null"));

    NewNode->NodePosX = Pos.X;
    NewNode->NodePosY = Pos.Y;
    Graph->AddNode(NewNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
    NewNode->CreateNewGuid();
    NewNode->PostPlacedNewNode();
    NewNode->AllocateDefaultPins();
    NewNode->ReconstructNode();

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("graph_name"), Graph->GetName());
    Result->SetStringField(TEXT("node_guid"), NewNode->NodeGuid.ToString());
    Result->SetStringField(TEXT("node_class"), NodeClass->GetName());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleConnectAnimGraphNodes(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, SourceGuid, TargetGuid, SourcePin, TargetPin;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("source_node_id"), SourceGuid))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_node_id' parameter"));
    if (!Params->TryGetStringField(TEXT("target_node_id"), TargetGuid))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_node_id' parameter"));
    if (!Params->TryGetStringField(TEXT("source_pin"), SourcePin))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_pin' parameter"));
    if (!Params->TryGetStringField(TEXT("target_pin"), TargetPin))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_pin' parameter"));

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));

    UEdGraphNode* SrcNode = nullptr; UEdGraph* SrcGraph = nullptr;
    UEdGraphNode* DstNode = nullptr; UEdGraph* DstGraph = nullptr;
    FindNodeByGuid(Blueprint, SourceGuid, SrcNode, SrcGraph);
    FindNodeByGuid(Blueprint, TargetGuid, DstNode, DstGraph);
    if (!SrcNode || !DstNode)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Source or target node not found by GUID"));
    if (SrcGraph != DstGraph)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Source and target nodes are in different graphs"));

    if (!FUnrealMCPCommonUtils::ConnectGraphNodes(SrcGraph, SrcNode, SourcePin, DstNode, TargetPin))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("ConnectGraphNodes failed (check pin names + directions)"));

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("graph_name"), SrcGraph->GetName());
    Result->SetStringField(TEXT("source_node_id"), SourceGuid);
    Result->SetStringField(TEXT("target_node_id"), TargetGuid);
    Result->SetStringField(TEXT("source_pin"), SourcePin);
    Result->SetStringField(TEXT("target_pin"), TargetPin);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetGraphNodePinDefaultValue(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, NodeGuid, PinName, RequestedValue;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("node_guid"), NodeGuid))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_guid' parameter"));
    if (!Params->TryGetStringField(TEXT("pin_name"), PinName))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pin_name' parameter"));
    if (!Params->TryGetStringField(TEXT("value"), RequestedValue))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'value' parameter"));

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));

    UEdGraphNode* Node = nullptr;
    UEdGraph* Graph = nullptr;
    if (!FindNodeByGuid(Blueprint, NodeGuid, Node, Graph) || !Node || !Graph)
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Node not found by GUID: %s"), *NodeGuid));

    UEdGraphPin* Pin = nullptr;
    for (UEdGraphPin* Candidate : Node->Pins)
    {
        if (Candidate && Candidate->Direction == EGPD_Input &&
            Candidate->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
        {
            Pin = Candidate;
            break;
        }
    }
    if (!Pin)
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Input pin not found: %s on node %s"), *PinName, *Node->GetClass()->GetName()));
    if (Pin->bOrphanedPin)
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Pin is orphaned and cannot be edited: %s"), *Pin->PinName.ToString()));
    if (Pin->bDefaultValueIsReadOnly)
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Pin default value is read-only: %s"), *Pin->PinName.ToString()));
    if (!Pin->LinkedTo.IsEmpty())
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Pin is connected; disconnect it before setting a default value: %s"), *Pin->PinName.ToString()));

    const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(Pin->GetSchema());
    if (!K2Schema)
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Graph schema is not K2-compatible: %s"), *GetNameSafe(Pin->GetSchema())));

    FString CanonicalValue;
    TObjectPtr<UObject> CanonicalObject = nullptr;
    FText CanonicalText;
    K2Schema->GetPinDefaultValuesFromString(
        Pin->PinType,
        Node,
        RequestedValue,
        CanonicalValue,
        CanonicalObject,
        CanonicalText,
        /*bPreserveTextIdentity=*/false);
    const FString ValidationError = K2Schema->IsPinDefaultValid(
        Pin, CanonicalValue, CanonicalObject, CanonicalText);
    if (!ValidationError.IsEmpty())
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Invalid default value '%s' for pin %s: %s"),
                *RequestedValue, *Pin->PinName.ToString(), *ValidationError));

    const FString PreviousDefaultValue = Pin->DefaultValue;
    Node->Modify();
    Graph->Modify();
    Blueprint->Modify();
    K2Schema->TrySetDefaultValue(*Pin, RequestedValue, /*bMarkAsModified=*/false);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("graph_name"), Graph->GetName());
    Result->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
    Result->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
    Result->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
    Result->SetStringField(TEXT("requested_value"), RequestedValue);
    Result->SetStringField(TEXT("previous_default_value"), PreviousDefaultValue);
    Result->SetStringField(TEXT("default_value"), Pin->DefaultValue);
    Result->SetStringField(TEXT("default_object"), GetPathNameSafe(Pin->DefaultObject));
    Result->SetStringField(TEXT("default_text_value"), Pin->DefaultTextValue.ToString());
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleSetAnimGraphNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
    // Three update modes — caller picks via params:
    //   (A) field_path + value           → set a UPROPERTY on either the inner FAnimNode_* struct
    //                                       (anim_node_properties domain) or the editor-side
    //                                       UAnimGraphNode_* UObject (node_object_properties).
    //                                       We try the inner FAnimNode_* first, then fall back to
    //                                       the node UObject.
    //   (B) property_binding={
    //          property_name, property_path[], (optional) context_id, (optional) type
    //       }                              → add/replace an entry in Binding->PropertyBindings.
    //   (C) clear_binding=property_name    → remove an entry from Binding->PropertyBindings.
    // All three coexist in one call; they run in (A) → (B) → (C) order.

    FString BlueprintPath, NodeGuid;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("node_guid"), NodeGuid))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_guid' parameter"));

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));

    UEdGraphNode* Node = nullptr; UEdGraph* Graph = nullptr;
    if (!FindNodeByGuid(Blueprint, NodeGuid, Node, Graph) || !Node)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Node not found by GUID: %s"), *NodeGuid));

    UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(Node);
    if (!AnimNode)
        return FUnrealMCPCommonUtils::CreateErrorResponse(
            FString::Printf(TEXT("Node is not a UAnimGraphNode_Base (got %s)"), *Node->GetClass()->GetName()));

    TArray<TSharedPtr<FJsonValue>> AppliedJson;
    bool bAnyChange = false;

    // ── (A) Direct field write ──
    FString FieldPath, FieldValue;
    if (Params->TryGetStringField(TEXT("field_path"), FieldPath) &&
        Params->TryGetStringField(TEXT("value"), FieldValue))
    {
        // Locate the inner FAnimNode_* struct via reflection.
        FStructProperty* InnerStructProp = nullptr;
        for (TFieldIterator<FStructProperty> PropIt(AnimNode->GetClass()); PropIt; ++PropIt)
        {
            FStructProperty* SP = *PropIt;
            if (SP && SP->Struct && SP->Struct->GetName().StartsWith(TEXT("AnimNode_")))
            {
                InnerStructProp = SP;
                break;
            }
        }

        bool bApplied = false;
        FString WhichDomain;
        FProperty* LeafProp = nullptr;
        void* LeafValPtr = nullptr;

        // Try (1) inner FAnimNode_* struct first.
        if (InnerStructProp)
        {
            void* InnerMem = InnerStructProp->ContainerPtrToValuePtr<void>(AnimNode);
            TArray<FString> Segments;
            FieldPath.ParseIntoArray(Segments, TEXT("."), true);

            const UScriptStruct* CurStruct = InnerStructProp->Struct;
            void* CurMem = InnerMem;
            bool bResolved = true;
            for (int32 i = 0; i < Segments.Num(); ++i)
            {
                FProperty* P = CurStruct->FindPropertyByName(FName(*Segments[i]));
                if (!P) { bResolved = false; break; }
                void* InnerVal = P->ContainerPtrToValuePtr<void>(CurMem);
                if (i == Segments.Num() - 1)
                {
                    LeafProp = P; LeafValPtr = InnerVal;
                    break;
                }
                FStructProperty* AsStruct = CastField<FStructProperty>(P);
                if (!AsStruct) { bResolved = false; break; }
                CurStruct = AsStruct->Struct;
                CurMem = InnerVal;
            }
            if (bResolved && LeafProp)
            {
                if (LeafProp->ImportText_Direct(*FieldValue, LeafValPtr, /*OwnerObject=*/nullptr, PPF_None, GLog))
                {
                    bApplied = true;
                    WhichDomain = TEXT("anim_node_properties");
                }
            }
        }

        // Fallback (2): UAnimGraphNode_* UObject-side UPROPERTY.
        if (!bApplied)
        {
            TArray<FString> Segments;
            FieldPath.ParseIntoArray(Segments, TEXT("."), true);
            UStruct* CurStruct = AnimNode->GetClass();
            void* CurMem = AnimNode;
            bool bResolved = true;
            LeafProp = nullptr; LeafValPtr = nullptr;
            for (int32 i = 0; i < Segments.Num(); ++i)
            {
                FProperty* P = CurStruct->FindPropertyByName(FName(*Segments[i]));
                if (!P) { bResolved = false; break; }
                void* InnerVal = P->ContainerPtrToValuePtr<void>(CurMem);
                if (i == Segments.Num() - 1)
                {
                    LeafProp = P; LeafValPtr = InnerVal;
                    break;
                }
                FStructProperty* AsStruct = CastField<FStructProperty>(P);
                if (!AsStruct) { bResolved = false; break; }
                CurStruct = AsStruct->Struct;
                CurMem = InnerVal;
            }
            if (bResolved && LeafProp)
            {
                if (LeafProp->ImportText_Direct(*FieldValue, LeafValPtr, /*OwnerObject=*/AnimNode, PPF_None, GLog))
                {
                    bApplied = true;
                    WhichDomain = TEXT("node_object_properties");
                }
            }
        }

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("mode"), TEXT("field"));
        Entry->SetStringField(TEXT("field_path"), FieldPath);
        Entry->SetStringField(TEXT("value"), FieldValue);
        Entry->SetBoolField(TEXT("ok"), bApplied);
        if (bApplied) Entry->SetStringField(TEXT("domain"), WhichDomain);
        AppliedJson.Add(MakeShared<FJsonValueObject>(Entry));
        bAnyChange = bAnyChange || bApplied;

        if (!bApplied)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Failed to set field '%s'='%s' on node %s — neither the inner FAnimNode_* nor the UAnimGraphNode_* UPROPERTY resolved that path"),
                    *FieldPath, *FieldValue, *AnimNode->GetClass()->GetName()));
        }
    }

    // Helper: resolve the Binding sub-object + its PropertyBindings TMap. Defined once and
    // shared by both the (B) write path and the (C) clear path.
    auto ResolveBindingMap = [&](UObject*& OutBindingObj, FMapProperty*& OutMapProp, void*& OutMapContainer) -> bool
    {
        OutBindingObj = nullptr; OutMapProp = nullptr; OutMapContainer = nullptr;
        FObjectProperty* BindingObjProp = CastField<FObjectProperty>(AnimNode->GetClass()->FindPropertyByName(TEXT("Binding")));
        if (!BindingObjProp) return false;
        UObject* BindingObj = BindingObjProp->GetObjectPropertyValue(BindingObjProp->ContainerPtrToValuePtr<void>(AnimNode));
        if (!BindingObj) return false;
        FMapProperty* MapProp = CastField<FMapProperty>(BindingObj->GetClass()->FindPropertyByName(TEXT("PropertyBindings")));
        if (!MapProp) return false;
        OutBindingObj = BindingObj;
        OutMapProp = MapProp;
        OutMapContainer = MapProp->ContainerPtrToValuePtr<void>(BindingObj);
        return true;
    };

    // ── (B) Property binding add/replace ──
    const TSharedPtr<FJsonObject>* BindingObj = nullptr;
    if (Params->TryGetObjectField(TEXT("property_binding"), BindingObj) && BindingObj && BindingObj->IsValid())
    {
        const TSharedPtr<FJsonObject> Binding = *BindingObj;
        FString PropName;
        if (!Binding->TryGetStringField(TEXT("property_name"), PropName))
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("property_binding requires 'property_name'"));

        const TArray<TSharedPtr<FJsonValue>>* PathArr = nullptr;
        if (!Binding->TryGetArrayField(TEXT("property_path"), PathArr) || PathArr->Num() == 0)
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("property_binding requires non-empty 'property_path' array"));

        UObject* BindObjLocal = nullptr; FMapProperty* MapProp = nullptr; void* MapCont = nullptr;
        if (!ResolveBindingMap(BindObjLocal, MapProp, MapCont))
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Node has no Binding/PropertyBindings (older AnimGraphNode subclass?)"));

        FScriptMapHelper MapHelper(MapProp, MapCont);

        // Construct an FAnimGraphNodePropertyBinding instance, populate fields, then insert/replace.
        UScriptStruct* BindStruct = FAnimGraphNodePropertyBinding::StaticStruct();
        TArray<uint8> ScratchBuf;
        ScratchBuf.SetNumZeroed(BindStruct->GetStructureSize());
        BindStruct->InitializeStruct(ScratchBuf.GetData());
        FAnimGraphNodePropertyBinding* NewBindData = reinterpret_cast<FAnimGraphNodePropertyBinding*>(ScratchBuf.GetData());

        NewBindData->PropertyName = FName(*PropName);
        TArray<FString>& OutPath = NewBindData->PropertyPath;
        OutPath.Reset();
        for (const TSharedPtr<FJsonValue>& V : *PathArr)
        {
            if (V.IsValid() && V->Type == EJson::String) OutPath.Add(V->AsString());
        }
        NewBindData->bIsBound = true;
        NewBindData->Type = EAnimGraphNodePropertyBindingType::Property;

        FString CtxId;
        if (Binding->TryGetStringField(TEXT("context_id"), CtxId)) NewBindData->ContextId = FName(*CtxId);

        FString TypeStr;
        if (Binding->TryGetStringField(TEXT("type"), TypeStr))
        {
            if (TypeStr.Equals(TEXT("Function"), ESearchCase::IgnoreCase))
                NewBindData->Type = EAnimGraphNodePropertyBindingType::Function;
            else if (TypeStr.Equals(TEXT("None"), ESearchCase::IgnoreCase))
                NewBindData->Type = EAnimGraphNodePropertyBindingType::None;
        }

        // PathAsText defaults to FText::FromString(joined path) — keeps the binding's UI label sane.
        NewBindData->PathAsText = FText::FromString(FString::Join(OutPath, TEXT(".")));

        // Property bindings compile through an exposed node pin. Mirror the
        // Details panel by making a hidden-by-default target pin visible.
        if (FArrayProperty* ShowPinsProperty = CastField<FArrayProperty>(
            AnimNode->GetClass()->FindPropertyByName(TEXT("ShowPinForProperties"))))
        {
            if (FStructProperty* OptionalPinProperty = CastField<FStructProperty>(ShowPinsProperty->Inner))
            {
                FNameProperty* PropertyNameProperty = CastField<FNameProperty>(
                    OptionalPinProperty->Struct->FindPropertyByName(TEXT("PropertyName")));
                FBoolProperty* ShowPinProperty = CastField<FBoolProperty>(
                    OptionalPinProperty->Struct->FindPropertyByName(TEXT("bShowPin")));
                FScriptArrayHelper ShowPins(ShowPinsProperty,
                    ShowPinsProperty->ContainerPtrToValuePtr<void>(AnimNode));

                for (int32 PinIndex = 0; PinIndex < ShowPins.Num(); ++PinIndex)
                {
                    void* OptionalPin = ShowPins.GetRawPtr(PinIndex);
                    if (PropertyNameProperty && ShowPinProperty
                        && PropertyNameProperty->GetPropertyValue_InContainer(OptionalPin) == FName(*PropName))
                    {
                        ShowPinProperty->SetPropertyValue_InContainer(OptionalPin, true);
                        break;
                    }
                }
            }
        }

        if (UEdGraphPin* BoundPin = AnimNode->FindPin(FName(*PropName)))
        {
            BoundPin->BreakAllPinLinks();
        }

        const FName Key = FName(*PropName);
        FAnimGraphNodePropertyBinding* Existing = reinterpret_cast<FAnimGraphNodePropertyBinding*>(MapHelper.FindValueFromHash(&Key));
        if (Existing)
        {
            // Move-assign to replace fields while preserving the map slot.
            *Existing = *NewBindData;
        }
        else
        {
            MapHelper.AddPair(&Key, NewBindData);
        }

        BindStruct->DestroyStruct(ScratchBuf.GetData());
        // [LEOCC] AnimGraphNode 上的 Binding 子对象写完 PropertyBindings TMap 后必须 PostEditChange，
        // 否则 AnimGraphNode 重编译会忽略此次 binding 变化
        FUnrealMCPCommonUtils::NotifyPropertyChanged(BindObjLocal, nullptr);
        AnimNode->ReconstructNode();

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("mode"), TEXT("binding_set"));
        Entry->SetStringField(TEXT("property_name"), PropName);
        Entry->SetBoolField(TEXT("ok"), true);
        AppliedJson.Add(MakeShared<FJsonValueObject>(Entry));
        bAnyChange = true;
    }

    // ── (C) Binding remove ──
    FString ClearName;
    if (Params->TryGetStringField(TEXT("clear_binding"), ClearName))
    {
        UObject* BindObjLocal = nullptr; FMapProperty* MapProp = nullptr; void* MapCont = nullptr;
        if (!ResolveBindingMap(BindObjLocal, MapProp, MapCont))
            return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Node has no Binding/PropertyBindings"));
        FScriptMapHelper MapHelper(MapProp, MapCont);
        const FName Key = FName(*ClearName);
        const bool bRemoved = MapHelper.RemovePair(&Key);
        // [LEOCC] 同 binding_set：清除后也必须 PostEditChange
        FUnrealMCPCommonUtils::NotifyPropertyChanged(BindObjLocal, nullptr);

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("mode"), TEXT("binding_clear"));
        Entry->SetStringField(TEXT("property_name"), ClearName);
        Entry->SetBoolField(TEXT("ok"), bRemoved);
        AppliedJson.Add(MakeShared<FJsonValueObject>(Entry));
        bAnyChange = bAnyChange || bRemoved;
    }

    if (bAnyChange)
    {
        // [LEOCC] 用 NotifyPropertyChanged 取代裸 Modify，确保 AnimNode 的 PostEditChange 也被触发；
        // ReconstructNode 只重建图，不会替代 PEC 的 instance-edit 簿记
        FUnrealMCPCommonUtils::NotifyPropertyChanged(AnimNode, nullptr);
        AnimNode->ReconstructNode();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), bAnyChange || AppliedJson.Num() == 0);
    Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("node_guid"), NodeGuid);
    Result->SetStringField(TEXT("node_class"), AnimNode->GetClass()->GetName());
    Result->SetArrayField(TEXT("applied"), AppliedJson);
    return Result;
}

TSharedPtr<FJsonObject> FUnrealMCPBlueprintCommands::HandleAddBlueprintFunctionGraph(const TSharedPtr<FJsonObject>& Params)
{
    FString BlueprintPath, FunctionName;
    if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
    if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));

    UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));

    // Reject duplicates up-front so callers get a clear error instead of UE's auto-rename.
    const FName DesiredName(*FunctionName);
    for (UEdGraph* G : Blueprint->FunctionGraphs)
    {
        if (G && G->GetFName() == DesiredName)
        {
            return FUnrealMCPCommonUtils::CreateErrorResponse(
                FString::Printf(TEXT("Function '%s' already exists on blueprint %s"), *FunctionName, *BlueprintPath));
        }
    }

    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint,
        DesiredName,
        UEdGraph::StaticClass(),
        UEdGraphSchema_K2::StaticClass());
    if (!NewGraph)
        return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("CreateNewGraph returned null"));

    // AddFunctionGraph<UK2Node_FunctionEntry> wires up Entry/Result + the function category etc.
    FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated=*/true, /*SignatureFromClass=*/nullptr);

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("function_name"), NewGraph->GetName());
    Result->SetStringField(TEXT("graph_path"), NewGraph->GetPathName());
    return Result;
}
