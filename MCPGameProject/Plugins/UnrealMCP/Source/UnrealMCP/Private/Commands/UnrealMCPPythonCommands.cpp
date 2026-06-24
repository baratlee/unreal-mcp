#include "Commands/UnrealMCPPythonCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "IPythonScriptPlugin.h"

namespace UnrealMCPPythonCommandsInternal
{
	// [LEOCC] 执行后把 LogOutput 拆成 output（Info/Warning）和 error（Error + 异常 trace）
	static TSharedPtr<FJsonObject> ExecAndBuildResult(FPythonCommandEx& Cmd)
	{
		IPythonScriptPlugin* Plugin = IPythonScriptPlugin::Get();
		if (!Plugin)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				TEXT("PythonScriptPlugin module not loaded; ensure Python is enabled in editor"));
		}

		if (!Plugin->IsPythonInitialized())
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				TEXT("Python is not yet initialized; retry after editor finishes startup"));
		}

		const bool bSuccess = Plugin->ExecPythonCommandEx(Cmd);

		FString Output;
		FString Error;

		for (const FPythonLogOutputEntry& Entry : Cmd.LogOutput)
		{
			if (Entry.Type == EPythonLogOutputType::Error)
			{
				if (!Error.IsEmpty()) Error += TEXT("\n");
				Error += Entry.Output;
			}
			else
			{
				if (!Output.IsEmpty()) Output += TEXT("\n");
				Output += Entry.Output;
			}
		}

		// [LEOCC] ExecuteFile 成功時の CommandResult は常に "None"（Python None の文字列化）なので無視する。
		// 失敗時は Python 例外 traceback が入るので error に追加する。
		if (!bSuccess && !Cmd.CommandResult.IsEmpty())
		{
			if (!Error.IsEmpty()) Error += TEXT("\n");
			Error += Cmd.CommandResult;
		}

		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
		Result->SetBoolField(TEXT("success"), bSuccess);
		Result->SetStringField(TEXT("output"), Output);
		Result->SetStringField(TEXT("error"), Error);
		return Result;
	}
}

FUnrealMCPPythonCommands::FUnrealMCPPythonCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPPythonCommands::HandleCommand(
	const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("execute_python_script"))
	{
		return HandleExecutePythonScript(Params);
	}
	else if (CommandType == TEXT("execute_python_file"))
	{
		return HandleExecutePythonFile(Params);
	}
	return FUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown Python command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FUnrealMCPPythonCommands::HandleExecutePythonScript(
	const TSharedPtr<FJsonObject>& Params)
{
	FString Script;
	if (!Params->TryGetStringField(TEXT("script"), Script))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'script' parameter"));
	}

	FPythonCommandEx Cmd;
	Cmd.Command = Script;
	Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;

	return UnrealMCPPythonCommandsInternal::ExecAndBuildResult(Cmd);
}

TSharedPtr<FJsonObject> FUnrealMCPPythonCommands::HandleExecutePythonFile(
	const TSharedPtr<FJsonObject>& Params)
{
	FString FilePath;
	if (!Params->TryGetStringField(TEXT("file_path"), FilePath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'file_path' parameter"));
	}

	FPythonCommandEx Cmd;
	Cmd.Command = FilePath;
	Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;

	return UnrealMCPPythonCommandsInternal::ExecAndBuildResult(Cmd);
}
