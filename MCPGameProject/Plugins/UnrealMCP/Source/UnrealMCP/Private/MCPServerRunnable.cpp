#include "MCPServerRunnable.h"
#include "UnrealMCPBridge.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"

namespace
{
    constexpr int32 ReceiveBufferSize = 64 * 1024;
    constexpr int32 MaxRequestBytes = 1024 * 1024;
    constexpr int32 MaxBatchOperations = 8;
    constexpr int32 MaxBatchResponseChars = 4 * 1024 * 1024;
    constexpr double RequestTimeoutSeconds = 10.0;
    constexpr double SendTimeoutSeconds = 30.0;
    constexpr double BatchTimeoutSeconds = 10.0;

    const FTimespan SocketWaitSlice = FTimespan::FromMilliseconds(50.0);

    FString SerializeJson(const TSharedPtr<FJsonObject>& JsonObject)
    {
        FString Result;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
        FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
        return Result;
    }

    FString MakeErrorResponse(const FString& Error)
    {
        TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
        Response->SetStringField(TEXT("status"), TEXT("error"));
        Response->SetStringField(TEXT("error"), Error);
        return SerializeJson(Response);
    }

    const TSet<FString>& GetBatchReadAllowlist()
    {
        static const TSet<FString> Commands = {
            TEXT("ping"), TEXT("get_project_info"), TEXT("get_actors_in_level"),
            TEXT("find_actors_by_name"), TEXT("get_actor_properties"), TEXT("get_static_mesh_info"),
            TEXT("get_blueprint_info"), TEXT("get_blueprint_function_graph"),
            TEXT("get_anim_graph_node_property_bindings"), TEXT("get_anim_state_machine"),
            TEXT("get_anim_state_graph"), TEXT("get_anim_transition_graph"),
            TEXT("get_component_properties"), TEXT("get_blueprint_cdo_properties"),
            TEXT("get_animation_info"), TEXT("get_animation_runtime_snapshot"),
            TEXT("get_animation_sync_markers"), TEXT("get_animation_notifies"),
            TEXT("get_animation_curve_names"), TEXT("get_animation_bone_track_names"),
            TEXT("get_montage_composite_info"), TEXT("find_animations_for_skeleton"),
            TEXT("get_anim_blueprint_info"), TEXT("get_anim_parent_asset_overrides"),
            TEXT("list_animation_blueprints_for_skeleton"), TEXT("get_skeleton_reference_pose"),
            TEXT("get_skeletal_mesh_info"), TEXT("get_physics_asset_info"),
            TEXT("get_asset_references"), TEXT("get_skeleton_bone_hierarchy"),
            TEXT("list_chooser_tables"), TEXT("get_chooser_table_info"),
            TEXT("get_skeleton_retarget_modes"), TEXT("list_ik_rigs"), TEXT("get_ik_rig_info"),
            TEXT("list_ik_retargeters"), TEXT("get_ik_retargeter_info"),
            TEXT("get_input_action_info"), TEXT("get_input_mapping_context_info"),
            TEXT("get_pose_search_database_info"), TEXT("get_pose_search_schema_info"),
            TEXT("get_animation_notify_details"), TEXT("get_state_tree_info"),
            TEXT("get_state_tree_node_properties"), TEXT("get_data_asset_info"),
            TEXT("list_data_assets"), TEXT("get_datatable_info"), TEXT("get_material_info"),
            TEXT("get_material_instance_info"), TEXT("get_material_parameter_collection_info"),
            TEXT("get_material_graph"), TEXT("get_spline_info"), TEXT("get_niagara_system_info"),
            TEXT("list_niagara_systems"), TEXT("get_niagara_emitter_renderers"),
            TEXT("get_gameplay_effect_info"), TEXT("list_gameplay_effects")
        };
        return Commands;
    }
}

FMCPServerRunnable::FMCPServerRunnable(UUnrealMCPBridge* InBridge, FSocket* InListenerSocket)
    : Bridge(InBridge)
    , ListenerSocket(InListenerSocket)
    , ClientSocket(nullptr)
    , bRunning(true)
{
}

FMCPServerRunnable::~FMCPServerRunnable()
{
    CloseClientSocket();
}

bool FMCPServerRunnable::Init()
{
    return Bridge != nullptr && ListenerSocket != nullptr;
}

uint32 FMCPServerRunnable::Run()
{
    UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Server thread started"));

    while (bRunning.Load())
    {
        bool bHasPendingConnection = false;
        if (!ListenerSocket->WaitForPendingConnection(bHasPendingConnection, SocketWaitSlice))
        {
            if (bRunning.Load())
            {
                UE_LOG(LogTemp, Warning, TEXT("MCPServerRunnable: Listener wait failed"));
                FPlatformProcess::Sleep(0.05f);
            }
            continue;
        }
        if (!bHasPendingConnection)
        {
            continue;
        }

        ClientSocket = ListenerSocket->Accept(TEXT("MCPClient"));
        if (!ClientSocket)
        {
            UE_LOG(LogTemp, Warning, TEXT("MCPServerRunnable: Failed to accept client connection"));
            continue;
        }

        ClientSocket->SetNonBlocking(true);
        ClientSocket->SetNoDelay(true);
        int32 ActualBufferSize = 0;
        ClientSocket->SetSendBufferSize(ReceiveBufferSize, ActualBufferSize);
        ClientSocket->SetReceiveBufferSize(ReceiveBufferSize, ActualBufferSize);

        const double RequestStartedAt = FPlatformTime::Seconds();
        FString CommandType;
        FString ReceiveError;
        TSharedPtr<FJsonObject> Params;
        int32 RequestBytes = 0;

        if (ReceiveRequest(CommandType, Params, ReceiveError, RequestBytes))
        {
            const double DispatchStartedAt = FPlatformTime::Seconds();
            const FString Response = CommandType == TEXT("batch_read")
                ? ExecuteBatchRead(Params)
                : Bridge->ExecuteCommand(CommandType, Params);
            const double DispatchMilliseconds = (FPlatformTime::Seconds() - DispatchStartedAt) * 1000.0;

            int32 ResponseBytes = 0;
            const bool bSent = SendResponse(Response, ResponseBytes);
            const double TotalMilliseconds = (FPlatformTime::Seconds() - RequestStartedAt) * 1000.0;
            UE_LOG(LogTemp, Display,
                TEXT("MCPServerRunnable: command=%s request_bytes=%d response_bytes=%d dispatch_ms=%.2f total_ms=%.2f sent=%s"),
                *CommandType, RequestBytes, ResponseBytes, DispatchMilliseconds, TotalMilliseconds,
                bSent ? TEXT("true") : TEXT("false"));
        }
        else if (bRunning.Load())
        {
            int32 ResponseBytes = 0;
            SendResponse(MakeErrorResponse(ReceiveError), ResponseBytes);
            UE_LOG(LogTemp, Warning, TEXT("MCPServerRunnable: Rejected request bytes=%d error=%s"),
                RequestBytes, *ReceiveError);
        }

        CloseClientSocket();
    }

    CloseClientSocket();
    UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Server thread stopped"));
    return 0;
}

void FMCPServerRunnable::Stop()
{
    bRunning.Store(false);
}

void FMCPServerRunnable::Exit()
{
}

bool FMCPServerRunnable::ReceiveRequest(
    FString& OutCommandType,
    TSharedPtr<FJsonObject>& OutParams,
    FString& OutError,
    int32& OutRequestBytes)
{
    OutParams = MakeShared<FJsonObject>();
    OutRequestBytes = 0;
    TArray<uint8> RequestBytes;
    RequestBytes.Reserve(4096);
    TArray<uint8> ReceiveBuffer;
    ReceiveBuffer.SetNumUninitialized(ReceiveBufferSize);
    const double Deadline = FPlatformTime::Seconds() + RequestTimeoutSeconds;

    while (bRunning.Load() && ClientSocket)
    {
        if (!ClientSocket->Wait(ESocketWaitConditions::WaitForRead, SocketWaitSlice))
        {
            if (FPlatformTime::Seconds() >= Deadline)
            {
                OutError = TEXT("Timed out while receiving request");
                return false;
            }
            if (ClientSocket->GetConnectionState() == SCS_ConnectionError)
            {
                OutError = TEXT("Client connection failed while receiving request");
                return false;
            }
            continue;
        }

        int32 BytesRead = 0;
        if (!ClientSocket->Recv(ReceiveBuffer.GetData(), ReceiveBuffer.Num(), BytesRead, ESocketReceiveFlags::None))
        {
            const ESocketErrors Error = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
            if (Error == SE_EWOULDBLOCK || Error == SE_EINTR)
            {
                continue;
            }
            OutError = FString::Printf(TEXT("Socket receive failed with error %d"), static_cast<int32>(Error));
            return false;
        }

        if (BytesRead <= 0)
        {
            OutError = TEXT("Client disconnected before sending a complete request");
            return false;
        }

        RequestBytes.Append(ReceiveBuffer.GetData(), BytesRead);
        OutRequestBytes = RequestBytes.Num();
        if (OutRequestBytes > MaxRequestBytes)
        {
            OutError = FString::Printf(TEXT("Request exceeds the %d byte limit"), MaxRequestBytes);
            return false;
        }

        RequestBytes.Add(0);
        const FString RequestText = UTF8_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(RequestBytes.GetData()));
        RequestBytes.SetNum(RequestBytes.Num() - 1, EAllowShrinking::No);

        TSharedPtr<FJsonObject> RequestObject;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestText);
        if (!FJsonSerializer::Deserialize(Reader, RequestObject) || !RequestObject.IsValid())
        {
            continue;
        }

        if (!RequestObject->TryGetStringField(TEXT("type"), OutCommandType) || OutCommandType.IsEmpty())
        {
            OutError = TEXT("Missing non-empty 'type' field in request");
            return false;
        }

        const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
        if (RequestObject->TryGetObjectField(TEXT("params"), ParamsObject) && ParamsObject && ParamsObject->IsValid())
        {
            OutParams = *ParamsObject;
        }
        return true;
    }

    OutError = TEXT("Server is stopping");
    return false;
}

bool FMCPServerRunnable::SendResponse(const FString& Response, int32& OutResponseBytes)
{
    OutResponseBytes = 0;
    if (!ClientSocket)
    {
        return false;
    }

    FTCHARToUTF8 Utf8Converter(*Response);
    const uint8* Utf8Data = reinterpret_cast<const uint8*>(Utf8Converter.Get());
    const int32 TotalBytes = Utf8Converter.Length();
    const double Deadline = FPlatformTime::Seconds() + SendTimeoutSeconds;

    while (bRunning.Load() && OutResponseBytes < TotalBytes)
    {
        if (!ClientSocket->Wait(ESocketWaitConditions::WaitForWrite, SocketWaitSlice))
        {
            if (FPlatformTime::Seconds() >= Deadline || ClientSocket->GetConnectionState() == SCS_ConnectionError)
            {
                return false;
            }
            continue;
        }

        int32 BytesSent = 0;
        if (!ClientSocket->Send(Utf8Data + OutResponseBytes, TotalBytes - OutResponseBytes, BytesSent))
        {
            const ESocketErrors Error = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
            if (Error == SE_EWOULDBLOCK || Error == SE_EINTR)
            {
                continue;
            }
            return false;
        }

        if (BytesSent > 0)
        {
            OutResponseBytes += BytesSent;
        }
    }

    return OutResponseBytes == TotalBytes;
}

FString FMCPServerRunnable::ExecuteBatchRead(const TSharedPtr<FJsonObject>& Params)
{
    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Params.IsValid() || !Params->TryGetArrayField(TEXT("operations"), Operations) || !Operations)
    {
        return MakeErrorResponse(TEXT("Missing 'operations' array"));
    }
    if (Operations->Num() == 0 || Operations->Num() > MaxBatchOperations)
    {
        return MakeErrorResponse(FString::Printf(
            TEXT("'operations' must contain between 1 and %d entries"), MaxBatchOperations));
    }

    bool bStopOnError = false;
    Params->TryGetBoolField(TEXT("stop_on_error"), bStopOnError);

    TArray<TSharedPtr<FJsonValue>> Results;
    Results.Reserve(Operations->Num());
    int32 AccumulatedResponseChars = 0;
    bool bStoppedEarly = false;
    const double Deadline = FPlatformTime::Seconds() + BatchTimeoutSeconds;

    for (int32 Index = 0; Index < Operations->Num(); ++Index)
    {
        TSharedPtr<FJsonObject> OperationResult = MakeShared<FJsonObject>();
        TSharedPtr<FJsonObject> Operation = (*Operations)[Index].IsValid() &&
            (*Operations)[Index]->Type == EJson::Object
            ? (*Operations)[Index]->AsObject()
            : nullptr;

        FString Id;
        FString Command;
        bool bOperationSucceeded = false;
        if (!Operation.IsValid())
        {
            Id = FString::FromInt(Index);
            OperationResult->SetStringField(TEXT("error"), TEXT("Operation must be an object"));
        }
        else if (!Operation->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
        {
            Id = FString::FromInt(Index);
            OperationResult->SetStringField(TEXT("error"), TEXT("Operation requires a non-empty string 'id'"));
        }
        else if (!Operation->TryGetStringField(TEXT("command"), Command) || Command.IsEmpty())
        {
            OperationResult->SetStringField(TEXT("error"), TEXT("Operation requires a non-empty string 'command'"));
        }
        else if (!GetBatchReadAllowlist().Contains(Command))
        {
            OperationResult->SetStringField(TEXT("error"),
                FString::Printf(TEXT("Command '%s' is not allowed in read-only batches"), *Command));
        }
        else if (FPlatformTime::Seconds() >= Deadline)
        {
            OperationResult->SetStringField(TEXT("error"), TEXT("Batch execution time limit reached"));
            bStoppedEarly = true;
        }
        else
        {
            TSharedPtr<FJsonObject> OperationParams = MakeShared<FJsonObject>();
            const TSharedPtr<FJsonObject>* OperationParamsPtr = nullptr;
            if (Operation->TryGetObjectField(TEXT("params"), OperationParamsPtr) &&
                OperationParamsPtr && OperationParamsPtr->IsValid())
            {
                OperationParams = *OperationParamsPtr;
            }

            if (Command == TEXT("get_blueprint_info") && !OperationParams->HasField(TEXT("output_profile")))
            {
                OperationParams->SetStringField(TEXT("output_profile"), TEXT("summary"));
            }
            if (Command == TEXT("get_blueprint_function_graph") ||
                Command == TEXT("get_anim_state_graph") ||
                Command == TEXT("get_anim_transition_graph"))
            {
                if (!OperationParams->HasField(TEXT("pin_payload_mode")))
                {
                    OperationParams->SetStringField(TEXT("pin_payload_mode"), TEXT("summary"));
                }
                if (!OperationParams->HasField(TEXT("compact_output")))
                {
                    OperationParams->SetBoolField(TEXT("compact_output"), true);
                }
            }

            const FString SubResponse = Bridge->ExecuteCommand(Command, OperationParams);
            AccumulatedResponseChars += SubResponse.Len();
            if (AccumulatedResponseChars > MaxBatchResponseChars)
            {
                OperationResult->SetStringField(TEXT("error"), TEXT("Batch response size limit reached"));
                bStoppedEarly = true;
            }
            else
            {
                TSharedPtr<FJsonObject> SubResponseObject;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SubResponse);
                if (!FJsonSerializer::Deserialize(Reader, SubResponseObject) || !SubResponseObject.IsValid())
                {
                    OperationResult->SetStringField(TEXT("error"), TEXT("Invalid response from command dispatcher"));
                }
                else
                {
                    FString Status;
                    SubResponseObject->TryGetStringField(TEXT("status"), Status);
                    if (Status == TEXT("success"))
                    {
                        const TSharedPtr<FJsonObject>* ResultObject = nullptr;
                        if (SubResponseObject->TryGetObjectField(TEXT("result"), ResultObject) &&
                            ResultObject && ResultObject->IsValid())
                        {
                            OperationResult->SetObjectField(TEXT("result"), *ResultObject);
                        }
                        bOperationSucceeded = true;
                    }
                    else
                    {
                        FString Error = TEXT("Unknown command error");
                        SubResponseObject->TryGetStringField(TEXT("error"), Error);
                        OperationResult->SetStringField(TEXT("error"), Error);
                    }
                }
            }
        }

        OperationResult->SetStringField(TEXT("id"), Id);
        OperationResult->SetStringField(TEXT("status"), bOperationSucceeded ? TEXT("success") : TEXT("error"));
        Results.Add(MakeShared<FJsonValueObject>(OperationResult));

        if (!bRunning.Load() || bStoppedEarly || (bStopOnError && !bOperationSucceeded))
        {
            bStoppedEarly = Index + 1 < Operations->Num();
            break;
        }
    }

    TSharedPtr<FJsonObject> BatchResult = MakeShared<FJsonObject>();
    BatchResult->SetArrayField(TEXT("results"), Results);
    BatchResult->SetNumberField(TEXT("requested_count"), Operations->Num());
    BatchResult->SetNumberField(TEXT("completed_count"), Results.Num());
    if (bStoppedEarly)
    {
        BatchResult->SetBoolField(TEXT("stopped_early"), true);
    }

    TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
    Response->SetStringField(TEXT("status"), TEXT("success"));
    Response->SetObjectField(TEXT("result"), BatchResult);
    return SerializeJson(Response);
}

void FMCPServerRunnable::CloseClientSocket()
{
    if (!ClientSocket)
    {
        return;
    }

    ClientSocket->Shutdown(ESocketShutdownMode::ReadWrite);
    ClientSocket->Close();
    if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
    {
        SocketSubsystem->DestroySocket(ClientSocket);
    }
    ClientSocket = nullptr;
}
