#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Templates/Atomic.h"
#include "Sockets.h"
#include "Interfaces/IPv4/IPv4Address.h"

class UUnrealMCPBridge;
class FJsonObject;

/**
 * Runnable class for the MCP server thread
 */
class FMCPServerRunnable : public FRunnable
{
public:
	FMCPServerRunnable(UUnrealMCPBridge* InBridge, FSocket* InListenerSocket);
	virtual ~FMCPServerRunnable();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

private:
	bool ReceiveRequest(FString& OutCommandType, TSharedPtr<FJsonObject>& OutParams, FString& OutError, int32& OutRequestBytes);
	bool SendResponse(const FString& Response, int32& OutResponseBytes);
	FString ExecuteBatchRead(const TSharedPtr<FJsonObject>& Params);
	void CloseClientSocket();

	UUnrealMCPBridge* Bridge;
	FSocket* ListenerSocket;
	FSocket* ClientSocket;
	TAtomic<bool> bRunning;
};
