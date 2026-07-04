#include "Commands/UnrealMCPDataTableCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Engine/DataTable.h"
#include "Json.h"
#include "JsonObjectConverter.h"

FUnrealMCPDataTableCommands::FUnrealMCPDataTableCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPDataTableCommands::HandleCommand(
	const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_datatable_info"))
	{
		return HandleGetDataTableInfo(Params);
	}
	return FUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown DataTable command: %s"), *CommandType));
}

// ---------------------------------------------------------------------------
// HandleGetDataTableInfo
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPDataTableCommands::HandleGetDataTableInfo(
	const TSharedPtr<FJsonObject>& Params)
{
	// [LEOCC] Phase D: 读取 UDataTable 资产内容。
	// asset_path: /Game/... 全路径；row_name（可选）：只返回指定行，缺省返回所有行。
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *AssetPath);
	if (!DataTable)
	{
		// [LEOCC] 尝试补全 .AssetName 后缀后再加载一次
		const FString BaseName = FPackageName::ObjectPathToObjectName(AssetPath);
		if (!AssetPath.EndsWith(TEXT(".") + BaseName))
		{
			DataTable = LoadObject<UDataTable>(nullptr, *(AssetPath + TEXT(".") + BaseName));
		}
	}

	if (!DataTable)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("DataTable not found: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	const UScriptStruct* RowStructPtr = DataTable->GetRowStruct();
	Result->SetStringField(TEXT("row_struct"), RowStructPtr ? RowStructPtr->GetName() : TEXT("None"));

	// [LEOCC] 列名列表（从 RowStruct 反射获取）
	TArray<TSharedPtr<FJsonValue>> ColumnNames;
	if (const UScriptStruct* RowStruct = RowStructPtr)
	{
		for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
		{
			ColumnNames.Add(MakeShared<FJsonValueString>(PropIt->GetName()));
		}
	}
	Result->SetArrayField(TEXT("columns"), ColumnNames);

	// [LEOCC] 可选 row_name 过滤
	FString RowFilter;
	const bool bFilterRow = Params->TryGetStringField(TEXT("row_name"), RowFilter) && !RowFilter.IsEmpty();

	// [LEOCC] 用 UDataTable 内置 JSON 导出（完整类型感知）。
	// GetTableRowAsJSON 在 UE 5.7 不存在，统一走 GetTableAsJSON 后按 row_name 过滤。
	const FString TableJson = DataTable->GetTableAsJSON();

	TSharedPtr<FJsonObject> AllRowsObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TableJson);
	if (FJsonSerializer::Deserialize(Reader, AllRowsObj) && AllRowsObj.IsValid())
	{
		if (bFilterRow)
		{
			// [LEOCC] 只返回指定行
			TSharedPtr<FJsonObject> SingleRowObj = MakeShared<FJsonObject>();
			if (AllRowsObj->HasField(RowFilter))
			{
				// [LEOCC] 不直接操作 Values（UE5.8 起键类型为 UE::FSharedString，FindRef(FString) 不再匹配）；
				// SetField/TryGetField 在 5.7/5.8 均可用
				SingleRowObj->SetField(RowFilter, AllRowsObj->TryGetField(RowFilter));
				Result->SetObjectField(TEXT("rows"), SingleRowObj);
				Result->SetNumberField(TEXT("row_count"), 1);
			}
			else
			{
				Result->SetObjectField(TEXT("rows"), SingleRowObj);
				Result->SetNumberField(TEXT("row_count"), 0);
				Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("Row '%s' not found"), *RowFilter));
			}
		}
		else
		{
			Result->SetObjectField(TEXT("rows"), AllRowsObj);
			Result->SetNumberField(TEXT("row_count"), static_cast<double>(AllRowsObj->Values.Num()));
		}
	}
	else
	{
		// [LEOCC] 解析失败时回退：返回原始 JSON 字符串，不阻塞调用方
		Result->SetStringField(TEXT("rows_raw"), TableJson);
		Result->SetNumberField(TEXT("row_count"), static_cast<double>(DataTable->GetRowMap().Num()));
	}

	if (bFilterRow)
	{
		Result->SetStringField(TEXT("row_filter"), RowFilter);
	}

	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}
