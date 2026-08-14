#include "UnrealMCPBridge.h"
#include "MCPServerRunnable.h"
#include "Misc/FileHelper.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "JsonObjectConverter.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
// Add Blueprint related includes
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
// UE5.5 correct includes
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "UObject/Field.h"
#include "UObject/FieldPath.h"
// Blueprint Graph specific includes
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_InputAction.h"
#include "K2Node_Self.h"
#include "GameFramework/InputSettings.h"
#include "EditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
// Include our new command handler classes
#include "Commands/UnrealMCPEditorCommands.h"
#include "Commands/UnrealMCPBlueprintCommands.h"
#include "Commands/UnrealMCPBlueprintNodeCommands.h"
#include "Commands/UnrealMCPProjectCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Commands/UnrealMCPUMGCommands.h"
#include "Commands/UnrealMCPAnimationCommands.h"
#include "Commands/UnrealMCPStateTreeCommands.h"
#include "Commands/UnrealMCPMaterialCommands.h"
#include "Commands/UnrealMCPSplineCommands.h"
#include "Commands/UnrealMCPNiagaraCommands.h"
#include "Commands/UnrealMCPGameplayEffectCommands.h"
#include "Commands/UnrealMCPPythonCommands.h"

// Default settings
#define MCP_SERVER_HOST "127.0.0.1"
#define MCP_SERVER_PORT 55557

static int32 GetMCPPortFromSettings()
{
    const FString SettingsNames[] = { TEXT("settings.local.json"), TEXT("settings.json") };
    for (const FString& FileName : SettingsNames)
    {
        FString FilePath = FPaths::ProjectDir() / TEXT(".claude") / FileName;
        FString FileContent;
        if (FFileHelper::LoadFileToString(FileContent, *FilePath))
        {
            TSharedPtr<FJsonObject> JsonObj;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileContent);
            if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
            {
                const TSharedPtr<FJsonObject>* EnvObj = nullptr;
                if (JsonObj->TryGetObjectField(TEXT("env"), EnvObj) && EnvObj && (*EnvObj).IsValid())
                {
                    FString PortStr;
                    if ((*EnvObj)->TryGetStringField(TEXT("UNREAL_MCP_PORT"), PortStr))
                    {
                        int32 PortVal = FCString::Atoi(*PortStr);
                        if (PortVal > 0 && PortVal <= 65535)
                        {
                            UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Using port %d from %s"), PortVal, *FilePath);
                            return PortVal;
                        }
                    }
                }
            }
        }
    }
    return MCP_SERVER_PORT;
}

UUnrealMCPBridge::UUnrealMCPBridge()
{
    EditorCommands = MakeShared<FUnrealMCPEditorCommands>();
    BlueprintCommands = MakeShared<FUnrealMCPBlueprintCommands>();
    BlueprintNodeCommands = MakeShared<FUnrealMCPBlueprintNodeCommands>();
    ProjectCommands = MakeShared<FUnrealMCPProjectCommands>();
    UMGCommands = MakeShared<FUnrealMCPUMGCommands>();
    AnimationCommands = MakeShared<FUnrealMCPAnimationCommands>();
    StateTreeCommands = MakeShared<FUnrealMCPStateTreeCommands>();
    DataAssetCommands = MakeShared<FUnrealMCPDataAssetCommands>();
    MaterialCommands = MakeShared<FUnrealMCPMaterialCommands>();
    SplineCommands = MakeShared<FUnrealMCPSplineCommands>();
    NiagaraCommands = MakeShared<FUnrealMCPNiagaraCommands>();
    GameplayEffectCommands = MakeShared<FUnrealMCPGameplayEffectCommands>();
    PythonCommands = MakeShared<FUnrealMCPPythonCommands>();
    DataTableCommands = MakeShared<FUnrealMCPDataTableCommands>();  // Phase D 2026-06-24
}

UUnrealMCPBridge::~UUnrealMCPBridge()
{
    EditorCommands.Reset();
    BlueprintCommands.Reset();
    BlueprintNodeCommands.Reset();
    ProjectCommands.Reset();
    UMGCommands.Reset();
    AnimationCommands.Reset();
    StateTreeCommands.Reset();
    DataAssetCommands.Reset();
    MaterialCommands.Reset();
    SplineCommands.Reset();
    NiagaraCommands.Reset();
    GameplayEffectCommands.Reset();
    PythonCommands.Reset();
    DataTableCommands.Reset();  // Phase D 2026-06-24
}

// Initialize subsystem
void UUnrealMCPBridge::Initialize(FSubsystemCollectionBase& Collection)
{
    UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Initializing"));
    
    bIsRunning = false;
    ListenerSocket = nullptr;
    ConnectionSocket = nullptr;
    ServerThread = nullptr;
    Port = GetMCPPortFromSettings();
    FIPv4Address::Parse(MCP_SERVER_HOST, ServerAddress);

    // Start the server automatically
    StartServer();
}

// Clean up resources when subsystem is destroyed
void UUnrealMCPBridge::Deinitialize()
{
    UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Shutting down"));
    StopServer();
}

// Start the MCP server
void UUnrealMCPBridge::StartServer()
{
    if (bIsRunning)
    {
        UE_LOG(LogTemp, Warning, TEXT("UnrealMCPBridge: Server is already running"));
        return;
    }

    // Create socket subsystem
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (!SocketSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealMCPBridge: Failed to get socket subsystem"));
        return;
    }

    // Create listener socket
    TSharedPtr<FSocket> NewListenerSocket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("UnrealMCPListener"), false));
    if (!NewListenerSocket.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealMCPBridge: Failed to create listener socket"));
        return;
    }

    // Allow address reuse for quick restarts
    NewListenerSocket->SetReuseAddr(true);
    NewListenerSocket->SetNonBlocking(true);

    // Bind to address
    FIPv4Endpoint Endpoint(ServerAddress, Port);
    if (!NewListenerSocket->Bind(*Endpoint.ToInternetAddr()))
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealMCPBridge: Failed to bind listener socket to %s:%d"), *ServerAddress.ToString(), Port);
        return;
    }

    // Start listening
    if (!NewListenerSocket->Listen(5))
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealMCPBridge: Failed to start listening"));
        return;
    }

    ListenerSocket = NewListenerSocket;
    bIsRunning = true;
    UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Server started on %s:%d"), *ServerAddress.ToString(), Port);

    // Start server thread
    ServerThread = FRunnableThread::Create(
        new FMCPServerRunnable(this, ListenerSocket),
        TEXT("UnrealMCPServerThread"),
        0, TPri_Normal
    );

    if (!ServerThread)
    {
        UE_LOG(LogTemp, Error, TEXT("UnrealMCPBridge: Failed to create server thread"));
        StopServer();
        return;
    }
}

// Stop the MCP server
void UUnrealMCPBridge::StopServer()
{
    if (!bIsRunning)
    {
        return;
    }

    bIsRunning = false;

    // Clean up thread
    if (ServerThread)
    {
        ServerThread->Kill(true);
        delete ServerThread;
        ServerThread = nullptr;
    }

    // Close sockets
    if (ConnectionSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ConnectionSocket.Get());
        ConnectionSocket.Reset();
    }

    if (ListenerSocket.IsValid())
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket.Get());
        ListenerSocket.Reset();
    }

    UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Server stopped"));
}

// Execute a command received from a client
FString UUnrealMCPBridge::ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
    UE_LOG(LogTemp, Display, TEXT("UnrealMCPBridge: Executing command: %s"), *CommandType);
    
    // Create a promise to wait for the result
    TPromise<FString> Promise;
    TFuture<FString> Future = Promise.GetFuture();
    
    // Queue execution on Game Thread
    AsyncTask(ENamedThreads::GameThread, [this, CommandType, Params, Promise = MoveTemp(Promise)]() mutable
    {
        TSharedPtr<FJsonObject> ResponseJson = MakeShareable(new FJsonObject);
        
        try
        {
            TSharedPtr<FJsonObject> ResultJson;
            
            if (CommandType == TEXT("ping"))
            {
                ResultJson = MakeShareable(new FJsonObject);
                ResultJson->SetStringField(TEXT("message"), TEXT("pong"));
            }
            // Editor Commands (including actor manipulation)
            else if (CommandType == TEXT("get_actors_in_level") ||
                     CommandType == TEXT("find_actors_by_name") ||
                     CommandType == TEXT("spawn_actor") ||
                     CommandType == TEXT("create_actor") ||
                     CommandType == TEXT("delete_actor") ||
                     CommandType == TEXT("set_actor_transform") ||
                     CommandType == TEXT("get_actor_properties") ||
                     CommandType == TEXT("set_actor_property") ||
                     CommandType == TEXT("spawn_blueprint_actor") ||
                     CommandType == TEXT("focus_viewport") ||
                     CommandType == TEXT("take_screenshot") ||
                     CommandType == TEXT("get_static_mesh_info") ||
                     // Batch E: P1 asset persistence + deletion
                     CommandType == TEXT("save_dirty_assets") ||
                     CommandType == TEXT("delete_asset") ||
                     // Phase D 2026-06-24 — PIE 控制 / 关卡切换 / Actor 组件属性写入
                     CommandType == TEXT("start_pie") ||
                     CommandType == TEXT("stop_pie") ||
                     CommandType == TEXT("open_level") ||
                     CommandType == TEXT("set_actor_component_property"))
            {
                ResultJson = EditorCommands->HandleCommand(CommandType, Params);
            }
            // Blueprint Commands
            else if (CommandType == TEXT("create_blueprint") ||
                     CommandType == TEXT("add_component_to_blueprint") ||
                     CommandType == TEXT("set_component_property") ||
                     CommandType == TEXT("set_physics_properties") ||
                     CommandType == TEXT("compile_blueprint") ||
                     CommandType == TEXT("set_blueprint_property") ||
                     CommandType == TEXT("set_static_mesh_properties") ||
                     CommandType == TEXT("set_pawn_properties") ||
                     CommandType == TEXT("get_blueprint_info") ||
                     CommandType == TEXT("get_blueprint_function_graph") ||
                     CommandType == TEXT("get_anim_graph_node_property_bindings") ||
                     CommandType == TEXT("get_anim_state_machine") ||
                     CommandType == TEXT("get_anim_state_graph") ||
                     CommandType == TEXT("get_anim_transition_graph") ||
                     CommandType == TEXT("add_anim_state") ||
                     CommandType == TEXT("add_anim_transition") ||
                     CommandType == TEXT("set_anim_transition_properties") ||
                     CommandType == TEXT("set_anim_state_entry") ||
                     CommandType == TEXT("remove_anim_state") ||
                     CommandType == TEXT("remove_anim_transition") ||
                     CommandType == TEXT("rename_anim_state_machine") ||
                     CommandType == TEXT("get_component_properties") ||
                     CommandType == TEXT("get_blueprint_cdo_properties") ||
                     // Batch E: P0/P1 blueprint write extensions
                     CommandType == TEXT("create_blueprint_from_parent_blueprint") ||
                     CommandType == TEXT("add_anim_graph_node") ||
                     CommandType == TEXT("connect_anim_graph_nodes") ||
                     CommandType == TEXT("set_anim_graph_node_property") ||
                     CommandType == TEXT("add_blueprint_function_graph"))
            {
                ResultJson = BlueprintCommands->HandleCommand(CommandType, Params);
            }
            // Blueprint Node Commands
            else if (CommandType == TEXT("connect_blueprint_nodes") || 
                     CommandType == TEXT("add_blueprint_get_self_component_reference") ||
                     CommandType == TEXT("add_blueprint_self_reference") ||
                     CommandType == TEXT("find_blueprint_nodes") ||
                     CommandType == TEXT("add_blueprint_event_node") ||
                     CommandType == TEXT("add_blueprint_input_action_node") ||
                     CommandType == TEXT("add_blueprint_function_node") ||
                     CommandType == TEXT("add_blueprint_get_component_node") ||
                     CommandType == TEXT("add_blueprint_variable") ||
                     CommandType == TEXT("add_blueprint_variable_get") ||
                     CommandType == TEXT("add_blueprint_variable_set") ||
                     CommandType == TEXT("add_blueprint_branch"))
            {
                ResultJson = BlueprintNodeCommands->HandleCommand(CommandType, Params);
            }
            // Project Commands
            else if (CommandType == TEXT("create_input_mapping") ||
                     CommandType == TEXT("get_project_info"))
            {
                ResultJson = ProjectCommands->HandleCommand(CommandType, Params);
            }
            // UMG Commands
            else if (CommandType == TEXT("create_umg_widget_blueprint") ||
                     CommandType == TEXT("add_text_block_to_widget") ||
                     CommandType == TEXT("add_button_to_widget") ||
                     CommandType == TEXT("bind_widget_event") ||
                     CommandType == TEXT("set_text_block_binding") ||
                     CommandType == TEXT("add_widget_to_viewport"))
            {
                ResultJson = UMGCommands->HandleCommand(CommandType, Params);
            }
            // Animation Commands
            else if (CommandType == TEXT("get_animation_info") ||
                     CommandType == TEXT("get_animation_runtime_snapshot") ||
                     CommandType == TEXT("get_animation_sync_markers") ||
                     CommandType == TEXT("get_animation_notifies") ||
                     CommandType == TEXT("get_animation_curve_names") ||
                     CommandType == TEXT("get_animation_bone_track_names") ||
                     CommandType == TEXT("get_montage_composite_info") ||
                     CommandType == TEXT("find_animations_for_skeleton") ||
                     CommandType == TEXT("get_anim_blueprint_info") ||
                     CommandType == TEXT("list_animation_blueprints_for_skeleton") ||
                     CommandType == TEXT("get_skeleton_reference_pose") ||
                     CommandType == TEXT("get_skeletal_mesh_info") ||
                     CommandType == TEXT("get_physics_asset_info") ||
                     CommandType == TEXT("get_asset_references") ||
                     CommandType == TEXT("get_skeleton_bone_hierarchy") ||
                     CommandType == TEXT("list_chooser_tables") ||
                     CommandType == TEXT("get_chooser_table_info") ||
                     CommandType == TEXT("get_skeleton_retarget_modes") ||
                     CommandType == TEXT("list_ik_rigs") ||
                     CommandType == TEXT("get_ik_rig_info") ||
                     CommandType == TEXT("list_ik_retargeters") ||
                     CommandType == TEXT("get_ik_retargeter_info") ||
                     CommandType == TEXT("get_input_action_info") ||
                     CommandType == TEXT("get_input_mapping_context_info") ||
                     CommandType == TEXT("create_input_action") ||
                     CommandType == TEXT("set_input_action_property") ||
                     CommandType == TEXT("create_input_mapping_context") ||
                     CommandType == TEXT("add_imc_mapping") ||
                     CommandType == TEXT("remove_imc_mapping") ||
                     CommandType == TEXT("add_imc_mapping_modifier") ||
                     CommandType == TEXT("remove_imc_mapping_modifier") ||
                     CommandType == TEXT("set_imc_mapping_modifier") ||
                     CommandType == TEXT("add_imc_mapping_trigger") ||
                     CommandType == TEXT("remove_imc_mapping_trigger") ||
                     CommandType == TEXT("set_imc_mapping_trigger") ||
                     CommandType == TEXT("get_pose_search_database_info") ||
                     CommandType == TEXT("get_pose_search_schema_info") ||
                     CommandType == TEXT("set_pose_search_database_schema") ||
                     CommandType == TEXT("add_pose_search_database_animation") ||
                     CommandType == TEXT("remove_pose_search_database_animation") ||
                     CommandType == TEXT("set_pose_search_database_cost_biases") ||
                     CommandType == TEXT("set_pose_search_database_animation_flags") ||
                     CommandType == TEXT("set_pose_search_schema_settings") ||
                     CommandType == TEXT("add_pose_search_schema_channel") ||
                     CommandType == TEXT("remove_pose_search_schema_channel") ||
                     CommandType == TEXT("set_pose_search_schema_channel_weight") ||
                     CommandType == TEXT("set_pose_search_schema_trajectory_sample") ||
                     CommandType == TEXT("add_chooser_table_row") ||
                     CommandType == TEXT("remove_chooser_table_row") ||
                     CommandType == TEXT("set_animation_properties") ||
                     CommandType == TEXT("create_ik_rig") ||
                     CommandType == TEXT("set_ik_rig_retarget_root") ||
                     CommandType == TEXT("add_ik_rig_retarget_chain") ||
                     CommandType == TEXT("add_ik_rig_goal") ||
                     CommandType == TEXT("add_ik_rig_solver") ||
                     CommandType == TEXT("create_ik_retargeter") ||
                     CommandType == TEXT("set_ik_retargeter_op_enabled") ||
                     CommandType == TEXT("set_ik_retargeter_op_field") ||
                     CommandType == TEXT("add_ik_retargeter_op") ||
                     CommandType == TEXT("add_ik_retargeter_pin_bones_entry") ||
                     CommandType == TEXT("set_ik_retargeter_chain_mapping") ||
                     CommandType == TEXT("ik_retargeter_auto_map_chains") ||
                     CommandType == TEXT("set_ik_retargeter_retarget_pose") ||
                     CommandType == TEXT("set_chooser_table_result") ||
                     CommandType == TEXT("set_chooser_table_fallback_result") ||
                     CommandType == TEXT("add_chooser_table_parameter") ||
                     CommandType == TEXT("remove_chooser_table_parameter") ||
                     CommandType == TEXT("add_chooser_table_column") ||
                     CommandType == TEXT("remove_chooser_table_column") ||
                     CommandType == TEXT("set_chooser_table_column_binding") ||
                     CommandType == TEXT("set_chooser_table_row_result") ||
                     CommandType == TEXT("set_chooser_table_row_column_value") ||
                     CommandType == TEXT("add_animation_notify") ||
                     CommandType == TEXT("remove_animation_notify") ||
                     CommandType == TEXT("set_animation_notify") ||
                     CommandType == TEXT("get_animation_notify_details") ||
                     CommandType == TEXT("set_animation_notify_property") ||
                     // Batch E: P0/P1 from UnrealMCP_API_ExpansionRequest.md
                     CommandType == TEXT("create_anim_blueprint") ||
                     CommandType == TEXT("create_anim_montage") ||
                     CommandType == TEXT("connect_ik_rig_goal_to_solver") ||
                     CommandType == TEXT("set_ik_rig_solver_field") ||
                     CommandType == TEXT("delete_ik_rig_chain") ||
                     CommandType == TEXT("delete_ik_rig_goal") ||
                     CommandType == TEXT("delete_ik_rig_solver") ||
                     CommandType == TEXT("update_ik_rig_chain"))
            {
                ResultJson = AnimationCommands->HandleCommand(CommandType, Params);
            }
            // StateTree Commands
            else if (CommandType == TEXT("create_state_tree") ||
                     CommandType == TEXT("get_state_tree_info") ||
                     CommandType == TEXT("get_state_tree_node_properties") ||
                     CommandType == TEXT("add_state_tree_state") ||
                     CommandType == TEXT("remove_state_tree_state") ||
                     CommandType == TEXT("set_state_tree_state_property") ||
                     CommandType == TEXT("add_state_tree_task") ||
                     CommandType == TEXT("add_state_tree_transition") ||
                     CommandType == TEXT("set_state_tree_node_property") ||
                     CommandType == TEXT("compile_state_tree"))
            {
                ResultJson = StateTreeCommands->HandleCommand(CommandType, Params);
            }
            // DataAsset Commands
            else if (CommandType == TEXT("get_data_asset_info") ||
                     CommandType == TEXT("set_data_asset_property") ||
                     CommandType == TEXT("list_data_assets") ||
                     CommandType == TEXT("create_data_asset"))
            {
                ResultJson = DataAssetCommands->HandleCommand(CommandType, Params);
            }
            // DataTable Commands — Phase D 2026-06-24
            else if (CommandType == TEXT("get_datatable_info"))
            {
                ResultJson = DataTableCommands->HandleCommand(CommandType, Params);
            }
            // Material Commands
            //   P0 2026-05-22: read-only inspection (info x3)
            //   P1 2026-05-22: graph traversal (get_material_graph)
            //   P0a+P0b+P1 2026-06-11: write batch (set_material_expression_property,
            //     set_material_instance_{scalar,vector,texture}_parameter, set_material_property)
            else if (CommandType == TEXT("get_material_info") ||
                     CommandType == TEXT("get_material_instance_info") ||
                     CommandType == TEXT("get_material_parameter_collection_info") ||
                     CommandType == TEXT("get_material_graph") ||
                     CommandType == TEXT("set_material_expression_property") ||
                     CommandType == TEXT("set_material_instance_scalar_parameter") ||
                     CommandType == TEXT("set_material_instance_vector_parameter") ||
                     CommandType == TEXT("set_material_instance_texture_parameter") ||
                     CommandType == TEXT("set_material_property"))
            {
                ResultJson = MaterialCommands->HandleCommand(CommandType, Params);
            }
            // Spline Commands (LT9 P0+P1, 2026-06-01)
            else if (CommandType == TEXT("get_spline_info") ||
                     CommandType == TEXT("set_spline_points") ||
                     CommandType == TEXT("set_spline_point") ||
                     CommandType == TEXT("clear_spline_points") ||
                     CommandType == TEXT("add_spline_point") ||
                     CommandType == TEXT("remove_spline_point") ||
                     CommandType == TEXT("set_spline_closed_loop") ||
                     CommandType == TEXT("set_spline_default_up_vector"))
            {
                ResultJson = SplineCommands->HandleCommand(CommandType, Params);
            }
            // Niagara Commands (LT12 P0+P1, 2026-06-02: read-only system / exposed parameters / list / emitter renderers)
            else if (CommandType == TEXT("get_niagara_system_info") ||
                     CommandType == TEXT("list_niagara_systems") ||
                     CommandType == TEXT("get_niagara_emitter_renderers"))
            {
                ResultJson = NiagaraCommands->HandleCommand(CommandType, Params);
            }
            // GameplayEffect Commands (LT14 P0+P1+P2, 2026-06-03)
            // P0: read + create  |  P1: set property / modifier CRUD / inherited tags
            // P2: list / delete / cue CRUD / tag_requirements / chance_to_apply / granted ability CRUD
            else if (CommandType == TEXT("get_gameplay_effect_info") ||
                     CommandType == TEXT("create_gameplay_effect") ||
                     CommandType == TEXT("set_gameplay_effect_property") ||
                     CommandType == TEXT("add_gameplay_effect_modifier") ||
                     CommandType == TEXT("remove_gameplay_effect_modifier") ||
                     CommandType == TEXT("set_gameplay_effect_modifier") ||
                     CommandType == TEXT("set_gameplay_effect_inherited_tags") ||
                     CommandType == TEXT("list_gameplay_effects") ||
                     CommandType == TEXT("delete_gameplay_effect") ||
                     CommandType == TEXT("add_gameplay_effect_cue") ||
                     CommandType == TEXT("remove_gameplay_effect_cue") ||
                     CommandType == TEXT("set_gameplay_effect_cue") ||
                     CommandType == TEXT("set_gameplay_effect_tag_requirements") ||
                     CommandType == TEXT("set_gameplay_effect_chance_to_apply") ||
                     CommandType == TEXT("add_gameplay_effect_granted_ability") ||
                     CommandType == TEXT("remove_gameplay_effect_granted_ability") ||
                     CommandType == TEXT("set_gameplay_effect_granted_ability"))
            {
                ResultJson = GameplayEffectCommands->HandleCommand(CommandType, Params);
            }
            else if (CommandType == TEXT("execute_python_script") ||
                     CommandType == TEXT("execute_python_file"))
            {
                ResultJson = PythonCommands->HandleCommand(CommandType, Params);
            }
            else
            {
                ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                ResponseJson->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown command: %s"), *CommandType));
                
                FString ResultString;
                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
                FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
                Promise.SetValue(ResultString);
                return;
            }
            
            // Check if the result contains an error
            bool bSuccess = true;
            FString ErrorMessage;
            
            if (ResultJson->HasField(TEXT("success")))
            {
                bSuccess = ResultJson->GetBoolField(TEXT("success"));
                if (!bSuccess && ResultJson->HasField(TEXT("error")))
                {
                    ErrorMessage = ResultJson->GetStringField(TEXT("error"));
                }
            }
            
            if (bSuccess)
            {
                // Set success status and include the result
                ResponseJson->SetStringField(TEXT("status"), TEXT("success"));
                ResponseJson->SetObjectField(TEXT("result"), ResultJson);
            }
            else
            {
                // Set error status and include the error message
                ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
                ResponseJson->SetStringField(TEXT("error"), ErrorMessage);
            }
        }
        catch (const std::exception& e)
        {
            ResponseJson->SetStringField(TEXT("status"), TEXT("error"));
            ResponseJson->SetStringField(TEXT("error"), UTF8_TO_TCHAR(e.what()));
        }
        
        FString ResultString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
        FJsonSerializer::Serialize(ResponseJson.ToSharedRef(), Writer);
        Promise.SetValue(ResultString);
    });
    
    return Future.Get();
}
