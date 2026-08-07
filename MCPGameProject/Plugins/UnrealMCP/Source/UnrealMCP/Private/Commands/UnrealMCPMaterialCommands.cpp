#include "Commands/UnrealMCPMaterialCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameters.h"
#include "Materials/MaterialInstanceBasePropertyOverrides.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "MaterialExpressionIO.h"
#include "MaterialDomain.h"
#include "MaterialShared.h"
#include "Engine/EngineTypes.h"
#include "Engine/BlendableInterface.h"
#include "Engine/Texture.h"
#include "Misc/PackageName.h"
#include "UObject/TextProperty.h"
#include "UObject/UObjectGlobals.h"
#if WITH_EDITOR
#include "MaterialEditingLibrary.h"
#endif

namespace
{
	UObject* LoadAssetWithFallback(const FString& AssetPath)
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

	FString StripEnumPrefix(const FString& EnumValueName)
	{
		int32 ColonColon = INDEX_NONE;
		if (EnumValueName.FindLastChar(TEXT(':'), ColonColon))
		{
			const FString Tail = EnumValueName.Mid(ColonColon + 1);
			return StripEnumPrefix(Tail);
		}
		int32 Underscore = INDEX_NONE;
		if (EnumValueName.FindChar(TEXT('_'), Underscore))
		{
			return EnumValueName.Mid(Underscore + 1);
		}
		return EnumValueName;
	}

	template <typename TEnum>
	FString EnumValueToString(TEnum Value)
	{
		const UEnum* EnumPtr = StaticEnum<TEnum>();
		if (!EnumPtr) return FString::Printf(TEXT("Unknown(%lld)"), (int64)Value);
		const FString Raw = EnumPtr->GetNameStringByValue((int64)Value);
		return StripEnumPrefix(Raw);
	}

	TSharedPtr<FJsonValue> LinearColorToJsonValue(const FLinearColor& Color)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueNumber>(Color.R));
		Arr.Add(MakeShared<FJsonValueNumber>(Color.G));
		Arr.Add(MakeShared<FJsonValueNumber>(Color.B));
		Arr.Add(MakeShared<FJsonValueNumber>(Color.A));
		return MakeShared<FJsonValueArray>(Arr);
	}

	void AppendShadingModels(TSharedPtr<FJsonObject>& OutObj, const FMaterialShadingModelField& Field)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		const UEnum* EnumPtr = StaticEnum<EMaterialShadingModel>();
		const int32 NumValues = EnumPtr ? EnumPtr->NumEnums() : 0;
		for (int32 i = 0; i < NumValues; ++i)
		{
			const int64 RawValue = EnumPtr->GetValueByIndex(i);
			if (RawValue < 0 || RawValue >= MSM_NUM) continue;
			const EMaterialShadingModel SM = static_cast<EMaterialShadingModel>(RawValue);
			if (Field.HasShadingModel(SM))
			{
				Arr.Add(MakeShared<FJsonValueString>(StripEnumPrefix(EnumPtr->GetNameStringByIndex(i))));
			}
		}
		OutObj->SetArrayField(TEXT("shading_models"), Arr);
	}

	void AppendUsageFlags(TSharedPtr<FJsonObject>& OutObj, const UMaterial* Material)
	{
		TSharedPtr<FJsonObject> Usage = MakeShared<FJsonObject>();
		Usage->SetBoolField(TEXT("skeletal_mesh"), Material->bUsedWithSkeletalMesh);
		Usage->SetBoolField(TEXT("editor_compositing"), Material->bUsedWithEditorCompositing);
		Usage->SetBoolField(TEXT("particle_sprites"), Material->bUsedWithParticleSprites);
		Usage->SetBoolField(TEXT("beam_trails"), Material->bUsedWithBeamTrails);
		Usage->SetBoolField(TEXT("mesh_particles"), Material->bUsedWithMeshParticles);
		Usage->SetBoolField(TEXT("niagara_sprites"), Material->bUsedWithNiagaraSprites);
		Usage->SetBoolField(TEXT("niagara_ribbons"), Material->bUsedWithNiagaraRibbons);
		Usage->SetBoolField(TEXT("niagara_mesh_particles"), Material->bUsedWithNiagaraMeshParticles);
		Usage->SetBoolField(TEXT("geometry_cache"), Material->bUsedWithGeometryCache);
		Usage->SetBoolField(TEXT("static_lighting"), Material->bUsedWithStaticLighting);
		Usage->SetBoolField(TEXT("morph_targets"), Material->bUsedWithMorphTargets);
		Usage->SetBoolField(TEXT("spline_meshes"), Material->bUsedWithSplineMeshes);
		Usage->SetBoolField(TEXT("instanced_static_meshes"), Material->bUsedWithInstancedStaticMeshes);
		Usage->SetBoolField(TEXT("geometry_collections"), Material->bUsedWithGeometryCollections);
		Usage->SetBoolField(TEXT("clothing"), Material->bUsedWithClothing);
		OutObj->SetObjectField(TEXT("usage"), Usage);
	}

	void AppendParameterListsForMaterialInterface(TSharedPtr<FJsonObject>& OutObj, UMaterialInterface* MI)
	{
		// Scalar
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			MI->GetAllScalarParameterInfo(Infos, Ids);
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FMaterialParameterInfo& Info : Infos)
			{
				float Default = 0.0f;
				MI->GetScalarParameterDefaultValue(FHashedMaterialParameterInfo(Info), Default);
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Info.Name.ToString());
				Entry->SetNumberField(TEXT("default_value"), Default);
				Arr.Add(MakeShared<FJsonValueObject>(Entry));
			}
			OutObj->SetArrayField(TEXT("scalar_parameters"), Arr);
		}
		// Vector
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			MI->GetAllVectorParameterInfo(Infos, Ids);
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FMaterialParameterInfo& Info : Infos)
			{
				FLinearColor Default(EForceInit::ForceInitToZero);
				MI->GetVectorParameterDefaultValue(FHashedMaterialParameterInfo(Info), Default);
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Info.Name.ToString());
				Entry->SetField(TEXT("default_value"), LinearColorToJsonValue(Default));
				Arr.Add(MakeShared<FJsonValueObject>(Entry));
			}
			OutObj->SetArrayField(TEXT("vector_parameters"), Arr);
		}
		// Texture
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			MI->GetAllTextureParameterInfo(Infos, Ids);
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FMaterialParameterInfo& Info : Infos)
			{
				UTexture* Default = nullptr;
				MI->GetTextureParameterDefaultValue(FHashedMaterialParameterInfo(Info), Default);
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Info.Name.ToString());
				Entry->SetStringField(TEXT("default_value"), Default ? Default->GetPathName() : TEXT(""));
				Arr.Add(MakeShared<FJsonValueObject>(Entry));
			}
			OutObj->SetArrayField(TEXT("texture_parameters"), Arr);
		}
		// Static Switch
		{
			TArray<FMaterialParameterInfo> Infos;
			TArray<FGuid> Ids;
			MI->GetAllStaticSwitchParameterInfo(Infos, Ids);
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FMaterialParameterInfo& Info : Infos)
			{
				bool DefaultValue = false;
				FGuid OutGuid;
				MI->GetStaticSwitchParameterDefaultValue(FHashedMaterialParameterInfo(Info), DefaultValue, OutGuid);
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Info.Name.ToString());
				Entry->SetBoolField(TEXT("default_value"), DefaultValue);
				Arr.Add(MakeShared<FJsonValueObject>(Entry));
			}
			OutObj->SetArrayField(TEXT("static_switch_parameters"), Arr);
		}
	}
}

FUnrealMCPMaterialCommands::FUnrealMCPMaterialCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleCommand(
	const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_material_info"))
	{
		return HandleGetMaterialInfo(Params);
	}
	if (CommandType == TEXT("get_material_instance_info"))
	{
		return HandleGetMaterialInstanceInfo(Params);
	}
	if (CommandType == TEXT("get_material_parameter_collection_info"))
	{
		return HandleGetMaterialParameterCollectionInfo(Params);
	}
	if (CommandType == TEXT("get_material_graph"))
	{
		return HandleGetMaterialGraph(Params);
	}
	// [LEOCC] Write batch added 2026-06-11 (LT16 P0a+P0b+P1).
	if (CommandType == TEXT("set_material_expression_property"))
	{
		return HandleSetMaterialExpressionProperty(Params);
	}
	if (CommandType == TEXT("set_material_instance_scalar_parameter"))
	{
		return HandleSetMaterialInstanceScalarParameter(Params);
	}
	if (CommandType == TEXT("set_material_instance_vector_parameter"))
	{
		return HandleSetMaterialInstanceVectorParameter(Params);
	}
	if (CommandType == TEXT("set_material_instance_texture_parameter"))
	{
		return HandleSetMaterialInstanceTextureParameter(Params);
	}
	if (CommandType == TEXT("set_material_property"))
	{
		return HandleSetMaterialProperty(Params);
	}
	return FUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown Material command: %s"), *CommandType));
}

// ---------------------------------------------------------------------------
// get_material_info
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleGetMaterialInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UObject* Asset = LoadAssetWithFallback(AssetPath);
	UMaterial* Material = Cast<UMaterial>(Asset);
	if (!Material)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UMaterial: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Material->GetPathName());
	Result->SetStringField(TEXT("class_name"), Material->GetClass()->GetName());

	Result->SetStringField(TEXT("material_domain"), EnumValueToString<EMaterialDomain>(Material->MaterialDomain.GetValue()));
	Result->SetStringField(TEXT("blend_mode"), EnumValueToString<EBlendMode>(Material->BlendMode.GetValue()));
	AppendShadingModels(Result, Material->GetShadingModels());

	Result->SetBoolField(TEXT("two_sided"), Material->TwoSided != 0);
	Result->SetBoolField(TEXT("is_thin_surface"), Material->bIsThinSurface != 0);

	if (Material->MaterialDomain == MD_PostProcess)
	{
		TSharedPtr<FJsonObject> PP = MakeShared<FJsonObject>();
		PP->SetStringField(TEXT("blendable_location"),
			EnumValueToString<EBlendableLocation>(Material->BlendableLocation.GetValue()));
		PP->SetNumberField(TEXT("blendable_priority"), Material->BlendablePriority);
		PP->SetBoolField(TEXT("blendable_output_alpha"), Material->BlendableOutputAlpha != 0);
		Result->SetObjectField(TEXT("post_process"), PP);
	}

	AppendUsageFlags(Result, Material);
	AppendParameterListsForMaterialInterface(Result, Material);

	TSharedPtr<FJsonObject> Counts = MakeShared<FJsonObject>();
	Counts->SetNumberField(TEXT("scalar"), Result->GetArrayField(TEXT("scalar_parameters")).Num());
	Counts->SetNumberField(TEXT("vector"), Result->GetArrayField(TEXT("vector_parameters")).Num());
	Counts->SetNumberField(TEXT("texture"), Result->GetArrayField(TEXT("texture_parameters")).Num());
	Counts->SetNumberField(TEXT("static_switch"), Result->GetArrayField(TEXT("static_switch_parameters")).Num());
	Result->SetObjectField(TEXT("parameter_counts"), Counts);

	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// ---------------------------------------------------------------------------
// get_material_instance_info
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleGetMaterialInstanceInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UObject* Asset = LoadAssetWithFallback(AssetPath);
	UMaterialInstance* MI = Cast<UMaterialInstance>(Asset);
	if (!MI)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UMaterialInstance: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), MI->GetPathName());
	Result->SetStringField(TEXT("class_name"), MI->GetClass()->GetName());

	if (UMaterialInterface* Parent = MI->Parent)
	{
		Result->SetStringField(TEXT("parent_path"), Parent->GetPathName());
		Result->SetStringField(TEXT("parent_class"), Parent->GetClass()->GetName());
	}
	else
	{
		Result->SetStringField(TEXT("parent_path"), FString());
		Result->SetStringField(TEXT("parent_class"), FString());
	}
	if (UMaterial* Base = MI->GetMaterial())
	{
		Result->SetStringField(TEXT("base_material_path"), Base->GetPathName());
	}
	else
	{
		Result->SetStringField(TEXT("base_material_path"), FString());
	}

	// Scalar overrides
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FScalarParameterValue& V : MI->ScalarParameterValues)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), V.ParameterInfo.Name.ToString());
			Entry->SetNumberField(TEXT("value"), V.ParameterValue);
			Arr.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("scalar_overrides"), Arr);
	}
	// Vector overrides
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FVectorParameterValue& V : MI->VectorParameterValues)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), V.ParameterInfo.Name.ToString());
			Entry->SetField(TEXT("value"), LinearColorToJsonValue(V.ParameterValue));
			Arr.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("vector_overrides"), Arr);
	}
	// Texture overrides
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FTextureParameterValue& V : MI->TextureParameterValues)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), V.ParameterInfo.Name.ToString());
			Entry->SetStringField(TEXT("value"), V.ParameterValue ? V.ParameterValue->GetPathName() : TEXT(""));
			Arr.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("texture_overrides"), Arr);
	}

	// Base property overrides (FMaterialInstanceBasePropertyOverrides)
	{
		const FMaterialInstanceBasePropertyOverrides& BPO = MI->BasePropertyOverrides;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("override_opacity_mask_clip_value"), BPO.bOverride_OpacityMaskClipValue);
		Obj->SetNumberField(TEXT("opacity_mask_clip_value"), BPO.OpacityMaskClipValue);
		Obj->SetBoolField(TEXT("override_blend_mode"), BPO.bOverride_BlendMode);
		Obj->SetStringField(TEXT("blend_mode"), EnumValueToString<EBlendMode>(BPO.BlendMode.GetValue()));
		Obj->SetBoolField(TEXT("override_two_sided"), BPO.bOverride_TwoSided);
		Obj->SetBoolField(TEXT("two_sided"), BPO.TwoSided != 0);
		Obj->SetBoolField(TEXT("override_dithered_lod_transition"), BPO.bOverride_DitheredLODTransition);
		Obj->SetBoolField(TEXT("dithered_lod_transition"), BPO.DitheredLODTransition != 0);
		Obj->SetBoolField(TEXT("override_cast_dynamic_shadow_as_masked"), BPO.bOverride_CastDynamicShadowAsMasked);
		Obj->SetBoolField(TEXT("cast_dynamic_shadow_as_masked"), BPO.bCastDynamicShadowAsMasked != 0);
		Result->SetObjectField(TEXT("base_property_overrides"), Obj);
	}

	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// ---------------------------------------------------------------------------
// get_material_parameter_collection_info
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleGetMaterialParameterCollectionInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UObject* Asset = LoadAssetWithFallback(AssetPath);
	UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(Asset);
	if (!MPC)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UMaterialParameterCollection: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), MPC->GetPathName());
	Result->SetStringField(TEXT("class_name"), MPC->GetClass()->GetName());

	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FCollectionScalarParameter& P : MPC->ScalarParameters)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), P.ParameterName.ToString());
			Entry->SetNumberField(TEXT("default_value"), P.DefaultValue);
			Entry->SetStringField(TEXT("id"), P.Id.ToString(EGuidFormats::DigitsWithHyphens));
			Arr.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("scalar_parameters"), Arr);
	}
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FCollectionVectorParameter& P : MPC->VectorParameters)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), P.ParameterName.ToString());
			Entry->SetField(TEXT("default_value"), LinearColorToJsonValue(P.DefaultValue));
			Entry->SetStringField(TEXT("id"), P.Id.ToString(EGuidFormats::DigitsWithHyphens));
			Arr.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("vector_parameters"), Arr);
	}

	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// ---------------------------------------------------------------------------
// get_material_graph (P1, 2026-05-22)
// ---------------------------------------------------------------------------
namespace
{
	enum class EMaterialPayloadMode : uint8 { NamesOnly, Summary, Full };

	EMaterialPayloadMode ParsePayloadMode(const TSharedPtr<FJsonObject>& Params)
	{
		FString S;
		if (Params->TryGetStringField(TEXT("pin_payload_mode"), S))
		{
			if (S.Equals(TEXT("names_only"), ESearchCase::IgnoreCase)) return EMaterialPayloadMode::NamesOnly;
			if (S.Equals(TEXT("full"), ESearchCase::IgnoreCase)) return EMaterialPayloadMode::Full;
		}
		return EMaterialPayloadMode::Summary;
	}

	bool ParseBoolDefault(const TSharedPtr<FJsonObject>& Params, const TCHAR* Field, bool Default)
	{
		bool Out;
		if (Params->TryGetBoolField(Field, Out)) return Out;
		return Default;
	}

	FString MakeNodeId(UMaterialExpression* Expr)
	{
		if (!Expr) return FString();
		if (Expr->MaterialExpressionGuid.IsValid())
		{
			return Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens);
		}
		return Expr->GetName();
	}

	// Walk super-struct chain looking for FExpressionInput OR FMaterialInput
	// (UE 5.7 reflection mirrors them as two independent USTRUCT(noexport) chains
	// in Material.h L155-187 + MaterialExpression.h L43-46. FExpressionInput
	// covers FMaterialAttributesInput; FMaterialInput covers FColor/FScalar/
	// FVector/FVector2/FShadingModel/FSubstrate MaterialInput. Layout headers
	// are interchangeable so reinterpret_cast<FExpressionInput*> is safe.)
	bool IsExpressionInputStruct(const UStruct* S)
	{
		static const FName NameExpressionInput(TEXT("ExpressionInput"));
		static const FName NameMaterialInput(TEXT("MaterialInput"));
		for (const UStruct* Cur = S; Cur != nullptr; Cur = Cur->GetSuperStruct())
		{
			const FName N = Cur->GetFName();
			if (N == NameExpressionInput || N == NameMaterialInput) return true;
		}
		return false;
	}

	bool IsSkippableExpressionProperty(FProperty* Prop)
	{
		if (!Prop) return true;
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) return true;
		const FName N = Prop->GetFName();
		if (N == TEXT("MaterialExpressionGuid")) return true;
		if (N == TEXT("MaterialExpressionEditorX")) return true;
		if (N == TEXT("MaterialExpressionEditorY")) return true;
		if (N == TEXT("DesiredOutputs")) return true;
		if (N == TEXT("Outputs")) return true;
		if (N == TEXT("GraphNode")) return true;
		if (N == TEXT("SubgraphExpression")) return true;
		if (N == TEXT("Function")) return true;
		if (N == TEXT("Material")) return true;
		// Skip FExpressionInput-derived fields — those are the input pins, output separately
		if (FStructProperty* SP = CastField<FStructProperty>(Prop))
		{
			if (IsExpressionInputStruct(SP->Struct)) return true;
		}
		return false;
	}

	bool IsSimpleValueProperty(FProperty* Prop)
	{
		if (!Prop) return false;
		if (CastField<FBoolProperty>(Prop)) return true;
		if (CastField<FIntProperty>(Prop)) return true;
		if (CastField<FFloatProperty>(Prop)) return true;
		if (CastField<FDoubleProperty>(Prop)) return true;
		if (CastField<FStrProperty>(Prop)) return true;
		if (CastField<FNameProperty>(Prop)) return true;
		if (CastField<FTextProperty>(Prop)) return true;
		if (CastField<FByteProperty>(Prop)) return true;
		if (CastField<FEnumProperty>(Prop)) return true;
		if (FStructProperty* SP = CastField<FStructProperty>(Prop))
		{
			static const FName WhitelistedStructs[] = {
				FName(TEXT("LinearColor")), FName(TEXT("Color")),
				FName(TEXT("Vector")), FName(TEXT("Vector2D")),
				FName(TEXT("Vector3f")), FName(TEXT("Vector4")),
				FName(TEXT("Rotator")), FName(TEXT("Quat")),
				FName(TEXT("Guid")), FName(TEXT("IntPoint")),
			};
			if (SP->Struct)
			{
				const FName SName = SP->Struct->GetFName();
				for (const FName& W : WhitelistedStructs)
				{
					if (SName == W) return true;
				}
			}
		}
		return false;
	}

	void SerializeExpressionProperties(UMaterialExpression* Expr, TSharedPtr<FJsonObject>& OutProps, bool bFullMode)
	{
		for (TFieldIterator<FProperty> It(Expr->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (IsSkippableExpressionProperty(Prop)) continue;

			if (!bFullMode && !IsSimpleValueProperty(Prop)) continue;

			const void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(Expr);
			FString ValueStr;
			Prop->ExportTextItem_Direct(ValueStr, ValueAddr, nullptr, nullptr, PPF_None);
			OutProps->SetStringField(Prop->GetName(), ValueStr);
		}
	}

	TSharedPtr<FJsonObject> SerializeExpressionNode(
		UMaterialExpression* Expr, EMaterialPayloadMode Mode)
	{
		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("guid"), MakeNodeId(Expr));
		NodeObj->SetStringField(TEXT("class"), Expr->GetClass()->GetName());

		if (Mode == EMaterialPayloadMode::NamesOnly) return NodeObj;

		// position
		{
			TArray<TSharedPtr<FJsonValue>> Pos;
			Pos.Add(MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorX));
			Pos.Add(MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorY));
			NodeObj->SetArrayField(TEXT("position"), Pos);
		}

		// outputs (FExpressionOutput list)
		{
			TArray<TSharedPtr<FJsonValue>> Outs;
			const TArray<FExpressionOutput>& OutputArr = Expr->GetOutputs();
			for (int32 i = 0; i < OutputArr.Num(); ++i)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("index"), i);
				O->SetStringField(TEXT("name"), OutputArr[i].OutputName.ToString());
				Outs.Add(MakeShared<FJsonValueObject>(O));
			}
			NodeObj->SetArrayField(TEXT("outputs"), Outs);
		}

		// inputs (FExpressionInput* list via FExpressionInputIterator)
		{
			TArray<TSharedPtr<FJsonValue>> Ins;
			for (FExpressionInputIterator It{Expr}; It; ++It)
			{
				TSharedPtr<FJsonObject> InObj = MakeShared<FJsonObject>();
				InObj->SetNumberField(TEXT("index"), It.Index);
				const FName ResolvedName = Expr->GetInputName(It.Index);
				InObj->SetStringField(TEXT("name"), ResolvedName.IsNone() ? FString() : ResolvedName.ToString());
				const bool bConnected = (It.Input != nullptr && It.Input->Expression != nullptr);
				InObj->SetBoolField(TEXT("connected"), bConnected);
				Ins.Add(MakeShared<FJsonValueObject>(InObj));
			}
			NodeObj->SetArrayField(TEXT("inputs"), Ins);
		}

		// properties (UPROPERTY reflection)
		{
			TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
			SerializeExpressionProperties(Expr, Props, Mode == EMaterialPayloadMode::Full);
			NodeObj->SetObjectField(TEXT("properties"), Props);
		}

		return NodeObj;
	}

	void CollectConnectionsFromExpression(
		UMaterialExpression* Expr,
		TArray<TSharedPtr<FJsonValue>>& OutConnections)
	{
		const FString TargetGuid = MakeNodeId(Expr);
		for (FExpressionInputIterator It{Expr}; It; ++It)
		{
			if (!It.Input || !It.Input->Expression) continue;
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("source_guid"), MakeNodeId(It.Input->Expression));
			C->SetNumberField(TEXT("source_output_index"), It.Input->OutputIndex);
			C->SetStringField(TEXT("target_guid"), TargetGuid);
			C->SetNumberField(TEXT("target_input_index"), It.Index);
			const FName InName = Expr->GetInputName(It.Index);
			C->SetStringField(TEXT("target_input_name"), InName.IsNone() ? FString() : InName.ToString());
			OutConnections.Add(MakeShared<FJsonValueObject>(C));
		}
	}

	void CollectRootInputs(
		UObject* EditorOnlyData,
		TArray<TSharedPtr<FJsonValue>>& OutRootInputs,
		int32& OutConnectionCount)
	{
		if (!EditorOnlyData) return;
		for (TFieldIterator<FProperty> It(EditorOnlyData->GetClass()); It; ++It)
		{
			FStructProperty* SP = CastField<FStructProperty>(*It);
			if (!SP || !IsExpressionInputStruct(SP->Struct)) continue;

			const void* ValueAddr = SP->ContainerPtrToValuePtr<void>(EditorOnlyData);
			// FExpressionInput layout sits at the head of all derived structs.
			const FExpressionInput* Input = reinterpret_cast<const FExpressionInput*>(ValueAddr);
			if (!Input) continue;

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), SP->GetName());
			Entry->SetStringField(TEXT("input_struct"), SP->Struct->GetName());
			const bool bConnected = (Input->Expression != nullptr);
			Entry->SetBoolField(TEXT("connected"), bConnected);
			if (bConnected)
			{
				Entry->SetStringField(TEXT("source_guid"), MakeNodeId(Input->Expression));
				Entry->SetNumberField(TEXT("source_output_index"), Input->OutputIndex);
				++OutConnectionCount;
			}
			OutRootInputs.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}
}

TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleGetMaterialGraph(const TSharedPtr<FJsonObject>& Params)
{
#if !WITH_EDITOR
	return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("get_material_graph requires editor build"));
#else
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UObject* Asset = LoadAssetWithFallback(AssetPath);
	UMaterial* Material = Cast<UMaterial>(Asset);
	if (!Material)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UMaterial: %s"), *AssetPath));
	}

	const EMaterialPayloadMode Mode = ParsePayloadMode(Params);
	const bool bIncludeComments = ParseBoolDefault(Params, TEXT("include_comments"), true);
	const bool bIncludeRootInputs = ParseBoolDefault(Params, TEXT("include_root_inputs"), true);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), Material->GetPathName());
	Result->SetStringField(TEXT("class_name"), Material->GetClass()->GetName());

	const FMaterialExpressionCollection& Collection = Material->GetExpressionCollection();

	// Nodes
	TArray<TSharedPtr<FJsonValue>> NodesArr;
	TArray<TSharedPtr<FJsonValue>> ConnectionsArr;
	for (const TObjectPtr<UMaterialExpression>& ExprPtr : Collection.Expressions)
	{
		UMaterialExpression* Expr = ExprPtr.Get();
		if (!Expr) continue;
		NodesArr.Add(MakeShared<FJsonValueObject>(SerializeExpressionNode(Expr, Mode)));
		CollectConnectionsFromExpression(Expr, ConnectionsArr);
	}
	Result->SetArrayField(TEXT("nodes"), NodesArr);
	Result->SetNumberField(TEXT("node_count"), NodesArr.Num());

	// Comments
	if (bIncludeComments)
	{
		TArray<TSharedPtr<FJsonValue>> CommentsArr;
		for (const TObjectPtr<UMaterialExpressionComment>& CommentPtr : Collection.EditorComments)
		{
			UMaterialExpressionComment* Comment = CommentPtr.Get();
			if (!Comment) continue;
			TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("guid"), MakeNodeId(Comment));
			TArray<TSharedPtr<FJsonValue>> Pos;
			Pos.Add(MakeShared<FJsonValueNumber>(Comment->MaterialExpressionEditorX));
			Pos.Add(MakeShared<FJsonValueNumber>(Comment->MaterialExpressionEditorY));
			C->SetArrayField(TEXT("position"), Pos);
			TArray<TSharedPtr<FJsonValue>> Size;
			Size.Add(MakeShared<FJsonValueNumber>(Comment->SizeX));
			Size.Add(MakeShared<FJsonValueNumber>(Comment->SizeY));
			C->SetArrayField(TEXT("size"), Size);
			C->SetStringField(TEXT("text"), Comment->Text);
			CommentsArr.Add(MakeShared<FJsonValueObject>(C));
		}
		Result->SetArrayField(TEXT("comments"), CommentsArr);
		Result->SetNumberField(TEXT("comment_count"), CommentsArr.Num());
	}

	// Root inputs (BaseColor / EmissiveColor / Roughness / etc. on UMaterialEditorOnlyData)
	if (bIncludeRootInputs)
	{
		TArray<TSharedPtr<FJsonValue>> RootArr;
		int32 RootConnections = 0;
		CollectRootInputs(Material->GetEditorOnlyData(), RootArr, RootConnections);
		Result->SetArrayField(TEXT("root_inputs"), RootArr);
		Result->SetNumberField(TEXT("root_connection_count"), RootConnections);
	}

	Result->SetArrayField(TEXT("connections"), ConnectionsArr);
	Result->SetNumberField(TEXT("connection_count"), ConnectionsArr.Num());

	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
#endif
}

// ---------------------------------------------------------------------------
// Write batch (LT16 2026-06-11, P0a + P0b + P1)
//   - set_material_expression_property        (P0a)
//   - set_material_instance_scalar_parameter  (P0b)
//   - set_material_instance_vector_parameter  (P0b)
//   - set_material_instance_texture_parameter (P0b)
//   - set_material_property                   (P1)
// ---------------------------------------------------------------------------

#if WITH_EDITOR
namespace
{
	// [LEOCC] Locate a UMaterialExpression by Guid string (preferred) or fall back to GetName().
	// get_material_graph returns Guid via MakeNodeId(); if MaterialExpressionGuid is not valid the name is returned instead.
	UMaterialExpression* FindMaterialExpressionByGuidOrName(UMaterial* Material, const FString& Identifier)
	{
		if (!Material) return nullptr;
		FGuid TargetGuid;
		const bool bGuidParsed = FGuid::Parse(Identifier, TargetGuid);
		const FMaterialExpressionCollection& Collection = Material->GetExpressionCollection();
		for (const TObjectPtr<UMaterialExpression>& ExprPtr : Collection.Expressions)
		{
			UMaterialExpression* Expr = ExprPtr.Get();
			if (!Expr) continue;
			if (bGuidParsed && Expr->MaterialExpressionGuid == TargetGuid) return Expr;
			if (!bGuidParsed && Expr->GetName().Equals(Identifier, ESearchCase::IgnoreCase)) return Expr;
		}
		return nullptr;
	}

	// [LEOCC] Common helper: notify property change for the *top-level* UPROPERTY when a dotted
	// path was used (e.g. PropertyName="Texture" or "SomeStruct.Field" — we still notify on "Texture"/"SomeStruct").
	void NotifyTopLevelPropertyChanged(UObject* Owner, const FString& PropertyName)
	{
		if (!Owner) return;
		int32 DotIdx = INDEX_NONE;
		PropertyName.FindChar(TEXT('.'), DotIdx);
		const FString TopName = (DotIdx == INDEX_NONE) ? PropertyName : PropertyName.Left(DotIdx);
		FProperty* TopProp = Owner->GetClass()->FindPropertyByName(*TopName);
		FUnrealMCPCommonUtils::NotifyPropertyChanged(Owner, TopProp);
	}
}
#endif

// P0a — set_material_expression_property
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetMaterialExpressionProperty(const TSharedPtr<FJsonObject>& Params)
{
#if !WITH_EDITOR
	return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("set_material_expression_property requires editor build"));
#else
	FString MaterialPath, ExpressionId, PropertyName;
	if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
	if (!Params->TryGetStringField(TEXT("expression_guid"), ExpressionId))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'expression_guid' parameter"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));

	const TSharedPtr<FJsonValue> ValueField = Params->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'value' parameter"));

	UObject* Asset = LoadAssetWithFallback(MaterialPath);
	UMaterial* Material = Cast<UMaterial>(Asset);
	if (!Material)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UMaterial: %s"), *MaterialPath));

	UMaterialExpression* Expr = FindMaterialExpressionByGuidOrName(Material, ExpressionId);
	if (!Expr)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("MaterialExpression not found by guid/name: %s"), *ExpressionId));

	Expr->Modify();
	Material->Modify();

	FString ErrorMsg;
	if (!FUnrealMCPCommonUtils::SetObjectProperty(Expr, PropertyName, ValueField, ErrorMsg))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to set property '%s' on expression: %s"), *PropertyName, *ErrorMsg));
	}

	NotifyTopLevelPropertyChanged(Expr, PropertyName);

	// [LEOCC] Recompile so editor preview + cooked shader stay in sync after the change.
	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_path"), Material->GetPathName());
	Result->SetStringField(TEXT("expression_guid"),
		Expr->MaterialExpressionGuid.IsValid()
			? Expr->MaterialExpressionGuid.ToString(EGuidFormats::DigitsWithHyphens)
			: Expr->GetName());
	Result->SetStringField(TEXT("expression_class"), Expr->GetClass()->GetName());
	Result->SetStringField(TEXT("property_name"), PropertyName);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
#endif
}

// P0b-1 — set_material_instance_scalar_parameter
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetMaterialInstanceScalarParameter(const TSharedPtr<FJsonObject>& Params)
{
#if !WITH_EDITOR
	return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("set_material_instance_scalar_parameter requires editor build"));
#else
	FString AssetPath, ParameterName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("parameter_name"), ParameterName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'parameter_name' parameter"));
	double Value = 0.0;
	if (!Params->TryGetNumberField(TEXT("value"), Value))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'value' parameter (expects number)"));

	UObject* Asset = LoadAssetWithFallback(AssetPath);
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Asset);
	if (!MIC)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UMaterialInstanceConstant: %s"), *AssetPath));

	MIC->Modify();
	const bool bChanged = UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
		MIC, FName(*ParameterName), (float)Value);
	UMaterialEditingLibrary::UpdateMaterialInstance(MIC);
	MIC->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), MIC->GetPathName());
	Result->SetStringField(TEXT("parameter_name"), ParameterName);
	Result->SetNumberField(TEXT("value"), Value);
	Result->SetBoolField(TEXT("changed"), bChanged);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
#endif
}

// P0b-2 — set_material_instance_vector_parameter
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetMaterialInstanceVectorParameter(const TSharedPtr<FJsonObject>& Params)
{
#if !WITH_EDITOR
	return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("set_material_instance_vector_parameter requires editor build"));
#else
	FString AssetPath, ParameterName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("parameter_name"), ParameterName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'parameter_name' parameter"));

	const TArray<TSharedPtr<FJsonValue>>* ValueArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("value"), ValueArr) || !ValueArr || ValueArr->Num() < 3)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("'value' must be a number array of length 3 (RGB) or 4 (RGBA)"));

	const FLinearColor Color(
		(float)(*ValueArr)[0]->AsNumber(),
		(float)(*ValueArr)[1]->AsNumber(),
		(float)(*ValueArr)[2]->AsNumber(),
		ValueArr->Num() >= 4 ? (float)(*ValueArr)[3]->AsNumber() : 1.0f);

	UObject* Asset = LoadAssetWithFallback(AssetPath);
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Asset);
	if (!MIC)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UMaterialInstanceConstant: %s"), *AssetPath));

	MIC->Modify();
	const bool bChanged = UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(
		MIC, FName(*ParameterName), Color);
	UMaterialEditingLibrary::UpdateMaterialInstance(MIC);
	MIC->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), MIC->GetPathName());
	Result->SetStringField(TEXT("parameter_name"), ParameterName);
	Result->SetField(TEXT("value"), LinearColorToJsonValue(Color));
	Result->SetBoolField(TEXT("changed"), bChanged);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
#endif
}

// P0b-3 — set_material_instance_texture_parameter
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetMaterialInstanceTextureParameter(const TSharedPtr<FJsonObject>& Params)
{
#if !WITH_EDITOR
	return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("set_material_instance_texture_parameter requires editor build"));
#else
	FString AssetPath, ParameterName, TexturePath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	if (!Params->TryGetStringField(TEXT("parameter_name"), ParameterName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'parameter_name' parameter"));
	// texture_path is optional (empty -> clear override to null)
	Params->TryGetStringField(TEXT("texture_path"), TexturePath);

	UObject* MIAsset = LoadAssetWithFallback(AssetPath);
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(MIAsset);
	if (!MIC)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UMaterialInstanceConstant: %s"), *AssetPath));

	UTexture* Texture = nullptr;
	if (!TexturePath.IsEmpty())
	{
		UObject* TexAsset = LoadAssetWithFallback(TexturePath);
		Texture = Cast<UTexture>(TexAsset);
		if (!Texture)
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Asset is not a UTexture: %s"), *TexturePath));
	}

	MIC->Modify();
	const bool bChanged = UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(
		MIC, FName(*ParameterName), Texture);
	UMaterialEditingLibrary::UpdateMaterialInstance(MIC);
	MIC->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), MIC->GetPathName());
	Result->SetStringField(TEXT("parameter_name"), ParameterName);
	Result->SetStringField(TEXT("texture_path"), Texture ? Texture->GetPathName() : TEXT(""));
	Result->SetBoolField(TEXT("changed"), bChanged);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
#endif
}

// P1 — set_material_property
TSharedPtr<FJsonObject> FUnrealMCPMaterialCommands::HandleSetMaterialProperty(const TSharedPtr<FJsonObject>& Params)
{
#if !WITH_EDITOR
	return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("set_material_property requires editor build"));
#else
	FString MaterialPath, PropertyName;
	if (!Params->TryGetStringField(TEXT("material_path"), MaterialPath))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'material_path' parameter"));
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));

	const TSharedPtr<FJsonValue> ValueField = Params->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'value' parameter"));

	UObject* Asset = LoadAssetWithFallback(MaterialPath);
	UMaterial* Material = Cast<UMaterial>(Asset);
	if (!Material)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a UMaterial: %s"), *MaterialPath));

	Material->Modify();

	FString ErrorMsg;
	if (!FUnrealMCPCommonUtils::SetObjectProperty(Material, PropertyName, ValueField, ErrorMsg))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to set material property '%s': %s"), *PropertyName, *ErrorMsg));
	}

	NotifyTopLevelPropertyChanged(Material, PropertyName);

	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("material_path"), Material->GetPathName());
	Result->SetStringField(TEXT("property_name"), PropertyName);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
#endif
}
