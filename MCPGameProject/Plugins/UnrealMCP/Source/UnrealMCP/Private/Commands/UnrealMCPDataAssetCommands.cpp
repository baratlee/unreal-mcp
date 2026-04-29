#include "Commands/UnrealMCPDataAssetCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataAsset.h"
#include "UObject/UObjectIterator.h"

FUnrealMCPDataAssetCommands::FUnrealMCPDataAssetCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPDataAssetCommands::HandleCommand(
	const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_data_asset_info"))
	{
		return HandleGetDataAssetInfo(Params);
	}
	else if (CommandType == TEXT("set_data_asset_property"))
	{
		return HandleSetDataAssetProperty(Params);
	}
	else if (CommandType == TEXT("list_data_assets"))
	{
		return HandleListDataAssets(Params);
	}

	return FUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown DataAsset command: %s"), *CommandType));
}

// ---------------------------------------------------------------------------
// HandleGetDataAssetInfo
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPDataAssetCommands::HandleGetDataAssetInfo(
	const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	int32 MaxDepth = 2;
	if (Params->HasField(TEXT("max_depth")))
	{
		MaxDepth = FMath::Clamp(static_cast<int32>(Params->GetNumberField(TEXT("max_depth"))), 1, 4);
	}

	FString CategoryFilter;
	Params->TryGetStringField(TEXT("category_filter"), CategoryFilter);

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		// Try with common suffixes
		FString BaseName = FPackageName::ObjectPathToObjectName(AssetPath);
		if (!AssetPath.EndsWith(FString(TEXT(".")) + BaseName))
		{
			FString TryPath = AssetPath + TEXT(".") + BaseName;
			Asset = LoadObject<UObject>(nullptr, *TryPath);
		}
	}

	if (!Asset)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	ResultObj->SetStringField(TEXT("class_name"), Asset->GetClass()->GetName());
	ResultObj->SetStringField(TEXT("parent_class"),
		Asset->GetClass()->GetSuperClass()
			? Asset->GetClass()->GetSuperClass()->GetName()
			: TEXT("None"));

	TArray<TSharedPtr<FJsonValue>> PropsArray;

	for (TFieldIterator<FProperty> PropIt(Asset->GetClass(), EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
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

		void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(Asset);

		FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
		FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
		FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop);

		if (SoftObjProp)
		{
			FSoftObjectPtr SoftPtr = SoftObjProp->GetPropertyValue(ValueAddr);
			PropObj->SetStringField(TEXT("type"), TEXT("SoftObjectReference"));
			PropObj->SetStringField(TEXT("value"), SoftPtr.ToString());
			if (SoftObjProp->PropertyClass)
			{
				PropObj->SetStringField(TEXT("object_class"), SoftObjProp->PropertyClass->GetName());
			}
		}
		else if (ObjProp)
		{
			UObject* SubObj = ObjProp->GetObjectPropertyValue(ValueAddr);
			if (SubObj)
			{
				PropObj->SetStringField(TEXT("type"), TEXT("object"));
				PropObj->SetStringField(TEXT("value"), SubObj->GetPathName());
				PropObj->SetStringField(TEXT("object_class"), SubObj->GetClass()->GetName());

				if (MaxDepth > 1 && SubObj->IsIn(Asset))
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
			FScriptArrayHelper ArrayHelper(ArrayProp, ValueAddr);
			FObjectProperty* InnerObjProp = CastField<FObjectProperty>(ArrayProp->Inner);

			if (InnerObjProp && ArrayHelper.Num() > 0)
			{
				PropObj->SetStringField(TEXT("type"), TEXT("TArray"));
				PropObj->SetNumberField(TEXT("count"), ArrayHelper.Num());
				TArray<TSharedPtr<FJsonValue>> Elements;
				for (int32 i = 0; i < ArrayHelper.Num(); ++i)
				{
					UObject* ElemObj = InnerObjProp->GetObjectPropertyValue(ArrayHelper.GetRawPtr(i));
					if (ElemObj)
					{
						TSharedPtr<FJsonObject> ElemJson = MakeShared<FJsonObject>();
						ElemJson->SetStringField(TEXT("class"), ElemObj->GetClass()->GetName());
						ElemJson->SetStringField(TEXT("path"), ElemObj->GetPathName());

						if (MaxDepth > 1 && ElemObj->IsIn(Asset))
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
				ArrayProp->ExportTextItem_Direct(ValueStr, ValueAddr, nullptr, nullptr, PPF_None);
				PropObj->SetStringField(TEXT("type"), TEXT("TArray"));
				PropObj->SetNumberField(TEXT("count"), ArrayHelper.Num());
				PropObj->SetStringField(TEXT("value"), ValueStr);
			}
		}
		else
		{
			FString ValueStr;
			Prop->ExportTextItem_Direct(ValueStr, ValueAddr, nullptr, nullptr, PPF_None);

			FString TypeStr;
			if (CastField<FBoolProperty>(Prop)) TypeStr = TEXT("bool");
			else if (CastField<FIntProperty>(Prop)) TypeStr = TEXT("int");
			else if (CastField<FFloatProperty>(Prop)) TypeStr = TEXT("float");
			else if (CastField<FDoubleProperty>(Prop)) TypeStr = TEXT("double");
			else if (CastField<FStrProperty>(Prop)) TypeStr = TEXT("string");
			else if (CastField<FNameProperty>(Prop)) TypeStr = TEXT("FName");
			else if (CastField<FTextProperty>(Prop)) TypeStr = TEXT("FText");
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

// ---------------------------------------------------------------------------
// HandleSetDataAssetProperty
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPDataAssetCommands::HandleSetDataAssetProperty(
	const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		FString BaseName = FPackageName::ObjectPathToObjectName(AssetPath);
		if (!AssetPath.EndsWith(FString(TEXT(".")) + BaseName))
		{
			FString TryPath = AssetPath + TEXT(".") + BaseName;
			Asset = LoadObject<UObject>(nullptr, *TryPath);
		}
	}

	if (!Asset)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	if (!Params->HasField(TEXT("property_value")))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
	}

	TSharedPtr<FJsonValue> JsonValue = Params->Values.FindRef(TEXT("property_value"));

	FString ErrorMessage;
	if (FUnrealMCPCommonUtils::SetObjectProperty(Asset, PropertyName, JsonValue, ErrorMessage))
	{
		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
		ResultObj->SetStringField(TEXT("asset_path"), Asset->GetPathName());
		ResultObj->SetStringField(TEXT("property"), PropertyName);
		ResultObj->SetBoolField(TEXT("success"), true);
		return ResultObj;
	}

	return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMessage);
}

// ---------------------------------------------------------------------------
// HandleListDataAssets
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPDataAssetCommands::HandleListDataAssets(
	const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	Params->TryGetStringField(TEXT("class_name"), ClassName);

	FString SearchPath;
	Params->TryGetStringField(TEXT("search_path"), SearchPath);

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;

	if (!ClassName.IsEmpty())
	{
		UClass* FilterClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
		if (!FilterClass)
		{
			FilterClass = LoadObject<UClass>(nullptr,
				*FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
		}
		if (FilterClass)
		{
			Filter.ClassPaths.Add(FilterClass->GetClassPathName());
			Filter.bRecursiveClasses = true;
		}
		else
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Class not found: %s"), *ClassName));
		}
	}
	else
	{
		Filter.ClassPaths.Add(UDataAsset::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
	}

	if (!SearchPath.IsEmpty())
	{
		Filter.PackagePaths.Add(*SearchPath);
		Filter.bRecursivePaths = true;
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> AssetsArray;

	for (const FAssetData& AssetData : AssetDataList)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
		AssetObj->SetStringField(TEXT("asset_name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("class_name"), AssetData.AssetClassPath.GetAssetName().ToString());
		AssetObj->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
		AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}

	ResultObj->SetNumberField(TEXT("count"), AssetsArray.Num());
	ResultObj->SetArrayField(TEXT("assets"), AssetsArray);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(ResultObj);
}

// ---------------------------------------------------------------------------
// SerializePropertiesToJson
// ---------------------------------------------------------------------------
void FUnrealMCPDataAssetCommands::SerializePropertiesToJson(
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

		FString ValueStr;
		Prop->ExportTextItem_Direct(ValueStr, ValueAddr, nullptr, nullptr, PPF_None);
		if (!ValueStr.IsEmpty())
		{
			PropObj->SetStringField(TEXT("value"), ValueStr);
		}

		OutArray.Add(MakeShared<FJsonValueObject>(PropObj));
	}
}
