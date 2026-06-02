#include "Commands/UnrealMCPNiagaraCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterStore.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraTypes.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraMeshRendererMeshProperties.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "UObject/UObjectGlobals.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Math/Vector4.h"
#include "Math/Quat.h"
#include "Math/Color.h"

namespace
{
	UObject* NiagaraLoadAssetWithFallback(const FString& AssetPath)
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (Asset) return Asset;
		const FString BaseName = FPackageName::ObjectPathToObjectName(AssetPath);
		if (!AssetPath.EndsWith(FString(TEXT(".")) + BaseName))
		{
			const FString TryPath = AssetPath + TEXT(".") + BaseName;
			Asset = LoadObject<UObject>(nullptr, *TryPath);
		}
		return Asset;
	}

	TSharedPtr<FJsonValue> NiagaraFloatArrayToJson(std::initializer_list<float> Components)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (float C : Components)
		{
			Arr.Add(MakeShared<FJsonValueNumber>(C));
		}
		return MakeShared<FJsonValueArray>(Arr);
	}

	// [LEOCC] Parse a single exposed (User) parameter's default value out of the
	// store. Scalar + common vector/color/quat types resolve to concrete JSON
	// values; everything else (DataInterface / UObject / custom struct / enum)
	// reports type-name only with OutKind = "type_only".
	TSharedPtr<FJsonValue> NiagaraParseParameterValue(
		const FNiagaraParameterStore& Store, const FNiagaraVariable& Var, FString& OutKind)
	{
		const FNiagaraTypeDefinition& Type = Var.GetType();

		if (Type == FNiagaraTypeDefinition::GetFloatDef())
		{
			OutKind = TEXT("parsed");
			return MakeShared<FJsonValueNumber>(Store.GetParameterValue<float>(Var));
		}
		if (Type == FNiagaraTypeDefinition::GetIntDef())
		{
			OutKind = TEXT("parsed");
			return MakeShared<FJsonValueNumber>(Store.GetParameterValue<int32>(Var));
		}
		if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			// Niagara stores bools as a 4-byte FNiagaraBool (0 = false, non-zero = true).
			OutKind = TEXT("parsed");
			return MakeShared<FJsonValueBoolean>(Store.GetParameterValue<int32>(Var) != 0);
		}
		if (Type == FNiagaraTypeDefinition::GetVec2Def())
		{
			OutKind = TEXT("parsed");
			const FVector2f V = Store.GetParameterValue<FVector2f>(Var);
			return NiagaraFloatArrayToJson({ V.X, V.Y });
		}
		if (Type == FNiagaraTypeDefinition::GetVec3Def() || Type == FNiagaraTypeDefinition::GetPositionDef())
		{
			OutKind = TEXT("parsed");
			const FVector3f V = Store.GetParameterValue<FVector3f>(Var);
			return NiagaraFloatArrayToJson({ V.X, V.Y, V.Z });
		}
		if (Type == FNiagaraTypeDefinition::GetVec4Def())
		{
			OutKind = TEXT("parsed");
			const FVector4f V = Store.GetParameterValue<FVector4f>(Var);
			return NiagaraFloatArrayToJson({ V.X, V.Y, V.Z, V.W });
		}
		if (Type == FNiagaraTypeDefinition::GetColorDef())
		{
			OutKind = TEXT("parsed");
			const FLinearColor C = Store.GetParameterValue<FLinearColor>(Var);
			return NiagaraFloatArrayToJson({ C.R, C.G, C.B, C.A });
		}
		if (Type == FNiagaraTypeDefinition::GetQuatDef())
		{
			OutKind = TEXT("parsed");
			const FQuat4f Q = Store.GetParameterValue<FQuat4f>(Var);
			return NiagaraFloatArrayToJson({ Q.X, Q.Y, Q.Z, Q.W });
		}

		// DataInterface / UObject / custom struct / enum: type name only.
		OutKind = TEXT("type_only");
		return nullptr;
	}
}

FUnrealMCPNiagaraCommands::FUnrealMCPNiagaraCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleCommand(
	const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_niagara_system_info"))
	{
		return HandleGetNiagaraSystemInfo(Params);
	}
	if (CommandType == TEXT("list_niagara_systems"))
	{
		return HandleListNiagaraSystems(Params);
	}
	if (CommandType == TEXT("get_niagara_emitter_renderers"))
	{
		return HandleGetNiagaraEmitterRenderers(Params);
	}
	return FUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown Niagara command: %s"), *CommandType));
}

// ---------------------------------------------------------------------------
// get_niagara_system_info
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleGetNiagaraSystemInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UObject* Asset = NiagaraLoadAssetWithFallback(AssetPath);
	UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset);
	if (!System)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UNiagaraSystem: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), System->GetPathName());
	Result->SetStringField(TEXT("class_name"), System->GetClass()->GetName());

	// Emitter list (basic info only: name + enabled flag).
	{
		const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
		Result->SetNumberField(TEXT("num_emitters"), Handles.Num());
		TArray<TSharedPtr<FJsonValue>> EmitterArr;
		for (const FNiagaraEmitterHandle& Handle : Handles)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Handle.GetName().ToString());
			Entry->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
			EmitterArr.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("emitters"), EmitterArr);
	}

	// Exposed (User) parameters.
	{
		const FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
		TArray<FNiagaraVariable> UserVars;
		Store.GetUserParameters(UserVars);

		TArray<TSharedPtr<FJsonValue>> ParamArr;
		for (const FNiagaraVariable& Var : UserVars)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Var.GetName().ToString());
			Entry->SetStringField(TEXT("type"), Var.GetType().GetName());
			Entry->SetBoolField(TEXT("is_data_interface"), Var.IsDataInterface());
			Entry->SetBoolField(TEXT("is_uobject"), Var.IsUObject());

			FString ValueKind;
			TSharedPtr<FJsonValue> ValueJson = NiagaraParseParameterValue(Store, Var, ValueKind);
			Entry->SetStringField(TEXT("value_kind"), ValueKind);
			if (ValueJson.IsValid())
			{
				Entry->SetField(TEXT("default_value"), ValueJson);
			}
			ParamArr.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("exposed_parameters"), ParamArr);
		Result->SetNumberField(TEXT("exposed_parameter_count"), ParamArr.Num());
	}

	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// ---------------------------------------------------------------------------
// list_niagara_systems
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleListNiagaraSystems(const TSharedPtr<FJsonObject>& Params)
{
	FString PathFilter;
	Params->TryGetStringField(TEXT("path_filter"), PathFilter);

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(*PathFilter);
		Filter.bRecursivePaths = true;
	}

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssets(Filter, AssetDataList);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetData& AssetData : AssetDataList)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
		Entry->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		Entry->SetStringField(TEXT("package_path"), AssetData.PackagePath.ToString());
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("systems"), Arr);
	Result->SetNumberField(TEXT("count"), Arr.Num());
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// ---------------------------------------------------------------------------
// get_niagara_emitter_renderers
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPNiagaraCommands::HandleGetNiagaraEmitterRenderers(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}
	FString EmitterName;
	if (!Params->TryGetStringField(TEXT("emitter_name"), EmitterName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'emitter_name' parameter"));
	}

	UObject* Asset = NiagaraLoadAssetWithFallback(AssetPath);
	UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset);
	if (!System)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UNiagaraSystem: %s"), *AssetPath));
	}

	// [LEOCC] Match emitter by handle name (matches get_niagara_system_info's
	// `emitters[].name`). Case-sensitive — Niagara names are FName but compared
	// as string here for clarity.
	const FNiagaraEmitterHandle* MatchedHandle = nullptr;
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName().ToString() == EmitterName)
		{
			MatchedHandle = &Handle;
			break;
		}
	}
	if (!MatchedHandle)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Emitter '%s' not found on system %s"), *EmitterName, *AssetPath));
	}

	const FVersionedNiagaraEmitterData* EmitterData = MatchedHandle->GetEmitterData();
	if (!EmitterData)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Emitter '%s' has no resolved EmitterData (version issue?)"), *EmitterName));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), System->GetPathName());
	Result->SetStringField(TEXT("emitter_name"), EmitterName);
	Result->SetBoolField(TEXT("emitter_enabled"), MatchedHandle->GetIsEnabled());

	TArray<TSharedPtr<FJsonValue>> RendererArr;
	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	for (UNiagaraRendererProperties* Renderer : Renderers)
	{
		if (!Renderer) continue;

		TSharedPtr<FJsonObject> RendererJson = MakeShared<FJsonObject>();
		RendererJson->SetStringField(TEXT("class_name"), Renderer->GetClass()->GetName());
		RendererJson->SetBoolField(TEXT("enabled"), Renderer->GetIsEnabled());

		// Materials via the base-class virtual — covers Sprite/Mesh/Ribbon/Light/Decal/etc.
		// nullptr emitter instance is fine for editor-time asset inspection (returns
		// the configured materials, not the per-instance overrides).
		{
			TArray<UMaterialInterface*> Materials;
			Renderer->GetUsedMaterials(nullptr, Materials);
			TArray<TSharedPtr<FJsonValue>> MatArr;
			for (UMaterialInterface* Mat : Materials)
			{
				MatArr.Add(MakeShared<FJsonValueString>(Mat ? Mat->GetPathName() : FString()));
			}
			RendererJson->SetArrayField(TEXT("materials"), MatArr);
		}

		// Mesh renderer special-case: expose the Meshes[] static mesh refs.
		if (const UNiagaraMeshRendererProperties* MeshRenderer = Cast<UNiagaraMeshRendererProperties>(Renderer))
		{
			TArray<TSharedPtr<FJsonValue>> MeshArr;
			for (const FNiagaraMeshRendererMeshProperties& MP : MeshRenderer->Meshes)
			{
				TSharedPtr<FJsonObject> MeshEntry = MakeShared<FJsonObject>();
				MeshEntry->SetStringField(TEXT("mesh_path"),
					MP.Mesh ? MP.Mesh->GetPathName() : FString());
				MeshArr.Add(MakeShared<FJsonValueObject>(MeshEntry));
			}
			RendererJson->SetArrayField(TEXT("meshes"), MeshArr);
		}

		RendererArr.Add(MakeShared<FJsonValueObject>(RendererJson));
	}
	Result->SetArrayField(TEXT("renderers"), RendererArr);
	Result->SetNumberField(TEXT("renderer_count"), RendererArr.Num());

	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}
