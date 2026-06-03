#include "Commands/UnrealMCPGameplayEffectCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"

#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffectComponent.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "AttributeSet.h"
#include "ScalableFloat.h"
#include "GameplayTagContainer.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/CurveTable.h"
#include "DataRegistryId.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/PackageName.h"

// =============================================================================
// Anonymous-namespace helpers (must be GE-prefixed: unity build merges all
// *Commands.cpp into one TU, see Niagara P0 changelog for the C2084 pitfall).
// =============================================================================
namespace
{
	// ------------------------------------------------------------------
	// Asset loading
	// ------------------------------------------------------------------
	UObject* GELoadAssetWithFallback(const FString& AssetPath)
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (Asset) return Asset;
		// Tolerate "/Game/X/BP" form (no .ObjectName suffix).
		const FString BaseName = FPackageName::ObjectPathToObjectName(AssetPath);
		if (!AssetPath.EndsWith(FString(TEXT(".")) + BaseName))
		{
			const FString TryPath = AssetPath + TEXT(".") + BaseName;
			Asset = LoadObject<UObject>(nullptr, *TryPath);
		}
		return Asset;
	}

	// Resolve a GE Blueprint by path → return its CDO (UGameplayEffect*).
	// Accepts both `/Game/X/BP_GE.BP_GE` and `/Game/X/BP_GE` forms.
	UGameplayEffect* GEFindCDO(const FString& AssetPath, UBlueprint** OutBP)
	{
		UObject* Asset = GELoadAssetWithFallback(AssetPath);
		if (!Asset) return nullptr;

		UBlueprint* BP = Cast<UBlueprint>(Asset);
		if (!BP)
		{
			// Could be a direct UClass (rare for GE) — try its CDO too.
			if (UClass* Cls = Cast<UClass>(Asset))
			{
				if (UGameplayEffect* CDO = Cast<UGameplayEffect>(Cls->GetDefaultObject()))
				{
					if (OutBP) *OutBP = nullptr;
					return CDO;
				}
			}
			return nullptr;
		}

		UClass* GenClass = BP->GeneratedClass ? BP->GeneratedClass : BP->SkeletonGeneratedClass;
		if (!GenClass) return nullptr;

		UGameplayEffect* CDO = Cast<UGameplayEffect>(GenClass->GetDefaultObject());
		if (CDO && OutBP) *OutBP = BP;
		return CDO;
	}

	// ------------------------------------------------------------------
	// Enum → string (UE reflection-based, no manual switch)
	// ------------------------------------------------------------------
	template<typename TEnum>
	FString GEEnumToString(TEnum Value)
	{
		if (UEnum* EnumPtr = StaticEnum<TEnum>())
		{
			return EnumPtr->GetNameStringByValue(static_cast<int64>(Value));
		}
		return TEXT("Unknown");
	}

	// EGameplayModOp::Type lives in a namespaced enum (legacy), reflected as the
	// `EGameplayModOp` UEnum.
	FString GEModifierOpToString(EGameplayModOp::Type Value)
	{
		if (UEnum* EnumPtr = StaticEnum<EGameplayModOp::Type>())
		{
			return EnumPtr->GetNameStringByValue(static_cast<int64>(Value));
		}
		return TEXT("Unknown");
	}

	// ------------------------------------------------------------------
	// Tag container → JSON array of dotted-name strings
	// ------------------------------------------------------------------
	TSharedPtr<FJsonValue> GETagContainerToJson(const FGameplayTagContainer& Container)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FGameplayTag& Tag : Container)
		{
			Arr.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
		return MakeShared<FJsonValueArray>(Arr);
	}

	// ------------------------------------------------------------------
	// FScalableFloat → JSON
	// ------------------------------------------------------------------
	TSharedPtr<FJsonObject> GEScalableFloatToJson(const FScalableFloat& SF)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("value"), SF.Value);
		if (SF.Curve.CurveTable)
		{
			Obj->SetStringField(TEXT("curve_table"), SF.Curve.CurveTable->GetPathName());
		}
		if (!SF.Curve.RowName.IsNone())
		{
			Obj->SetStringField(TEXT("row_name"), SF.Curve.RowName.ToString());
		}
		const FName RegistryName = SF.RegistryType.GetName();
		if (!RegistryName.IsNone())
		{
			Obj->SetStringField(TEXT("registry_type"), RegistryName.ToString());
		}
		return Obj;
	}

	// ------------------------------------------------------------------
	// FGameplayEffectModifierMagnitude → JSON
	// (Reads MagnitudeCalculationType via getter; for ScalableFloat &
	// SetByCaller we have safe accessors; for AttributeBased / CustomCalc the
	// inner structs are private — we report calculation kind + a hint only.)
	// ------------------------------------------------------------------
	TSharedPtr<FJsonObject> GEMagnitudeToJson(const FGameplayEffectModifierMagnitude& Mag)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		const EGameplayEffectMagnitudeCalculation Kind = Mag.GetMagnitudeCalculationType();
		Obj->SetStringField(TEXT("calculation"),
			GEEnumToString<EGameplayEffectMagnitudeCalculation>(Kind));

		switch (Kind)
		{
		case EGameplayEffectMagnitudeCalculation::ScalableFloat:
		{
			// Try to read the static value at level 0; falls back gracefully if curve-only.
			float StaticVal = 0.f;
			Mag.GetStaticMagnitudeIfPossible(0.f, StaticVal);
			TSharedPtr<FJsonObject> SF = MakeShared<FJsonObject>();
			SF->SetNumberField(TEXT("value"), StaticVal);
			Obj->SetObjectField(TEXT("scalable_float"), SF);
			break;
		}
		case EGameplayEffectMagnitudeCalculation::SetByCaller:
		{
			const FSetByCallerFloat& SBC = Mag.GetSetByCallerFloat();
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			if (!SBC.DataName.IsNone())
			{
				S->SetStringField(TEXT("data_name"), SBC.DataName.ToString());
			}
			if (SBC.DataTag.IsValid())
			{
				S->SetStringField(TEXT("data_tag"), SBC.DataTag.ToString());
			}
			Obj->SetObjectField(TEXT("set_by_caller"), S);
			break;
		}
		case EGameplayEffectMagnitudeCalculation::AttributeBased:
		case EGameplayEffectMagnitudeCalculation::CustomCalculationClass:
		default:
			// Detail extraction deferred to P2 — private fields, no public getter.
			break;
		}
		return Obj;
	}

	// ------------------------------------------------------------------
	// FGameplayAttribute → "ClassName.PropertyName" string (matches Editor display)
	// ------------------------------------------------------------------
	FString GEAttributeToString(const FGameplayAttribute& Attr)
	{
		if (!Attr.IsValid()) return FString();
		const FProperty* Prop = Attr.GetUProperty();
		if (!Prop) return FString();
		const UClass* OwnerClass = Prop->GetOwnerClass();
		const FString ClassName = OwnerClass ? OwnerClass->GetName() : TEXT("");
		return ClassName.IsEmpty() ? Prop->GetName() : (ClassName + TEXT(".") + Prop->GetName());
	}

	// ------------------------------------------------------------------
	// FGameplayModifierInfo → JSON
	// ------------------------------------------------------------------
	TSharedPtr<FJsonObject> GEModifierInfoToJson(const FGameplayModifierInfo& Mod)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("attribute"), GEAttributeToString(Mod.Attribute));
		Obj->SetStringField(TEXT("modifier_op"), GEModifierOpToString(Mod.ModifierOp));
		Obj->SetObjectField(TEXT("magnitude"), GEMagnitudeToJson(Mod.ModifierMagnitude));
		Obj->SetField(TEXT("source_tags_required"), GETagContainerToJson(Mod.SourceTags.RequireTags));
		Obj->SetField(TEXT("source_tags_ignored"),  GETagContainerToJson(Mod.SourceTags.IgnoreTags));
		Obj->SetField(TEXT("target_tags_required"), GETagContainerToJson(Mod.TargetTags.RequireTags));
		Obj->SetField(TEXT("target_tags_ignored"),  GETagContainerToJson(Mod.TargetTags.IgnoreTags));
		return Obj;
	}

	// ------------------------------------------------------------------
	// FInheritedTagContainer → JSON {added, removed, combined}
	// ------------------------------------------------------------------
	TSharedPtr<FJsonObject> GEInheritedTagsToJson(const FInheritedTagContainer& ITC)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetField(TEXT("added"),    GETagContainerToJson(ITC.Added));
		Obj->SetField(TEXT("removed"),  GETagContainerToJson(ITC.Removed));
		Obj->SetField(TEXT("combined"), GETagContainerToJson(ITC.CombinedTags));
		return Obj;
	}

	// ==================================================================
	// P1 helpers — parse JSON into GE types
	// ==================================================================

	// Enum from string (case-sensitive on UE enum-value name).
	template<typename TEnum>
	bool GEParseEnum(const FString& InName, TEnum& OutValue)
	{
		if (UEnum* EnumPtr = StaticEnum<TEnum>())
		{
			const int64 Idx = EnumPtr->GetValueByNameString(InName);
			if (Idx != INDEX_NONE)
			{
				OutValue = static_cast<TEnum>(Idx);
				return true;
			}
			// Tolerate non-prefixed short names (e.g. "Instant" for
			// "EGameplayEffectDurationType::Instant").
			for (int32 I = 0; I < EnumPtr->NumEnums() - 1; ++I)
			{
				const FString Short = EnumPtr->GetNameStringByIndex(I);
				if (Short.Equals(InName, ESearchCase::IgnoreCase))
				{
					OutValue = static_cast<TEnum>(EnumPtr->GetValueByIndex(I));
					return true;
				}
			}
		}
		return false;
	}

	// EGameplayModOp::Type variant (reflected as UEnum). FGameplayModifierInfo
	// stores it as TEnumAsByte<>, so we write through that wrapper directly.
	bool GEParseModifierOp(const FString& InName, TEnumAsByte<EGameplayModOp::Type>& OutValue)
	{
		if (UEnum* EnumPtr = StaticEnum<EGameplayModOp::Type>())
		{
			int64 Idx = EnumPtr->GetValueByNameString(InName);
			if (Idx == INDEX_NONE)
			{
				for (int32 I = 0; I < EnumPtr->NumEnums() - 1; ++I)
				{
					const FString Short = EnumPtr->GetNameStringByIndex(I);
					if (Short.Equals(InName, ESearchCase::IgnoreCase))
					{
						Idx = EnumPtr->GetValueByIndex(I);
						break;
					}
				}
			}
			if (Idx != INDEX_NONE)
			{
				OutValue = static_cast<EGameplayModOp::Type>(Idx);
				return true;
			}
		}
		return false;
	}

	// Parse "ClassName.PropertyName" → FGameplayAttribute. Class must be loaded
	// (typically an AttributeSet subclass).
	bool GEParseAttribute(const FString& InAttr, FGameplayAttribute& OutAttr, FString& OutError)
	{
		int32 DotIdx;
		if (!InAttr.FindChar('.', DotIdx))
		{
			OutError = FString::Printf(TEXT("Attribute must be 'ClassName.PropertyName', got '%s'"), *InAttr);
			return false;
		}
		const FString ClassName = InAttr.Left(DotIdx);
		const FString PropName = InAttr.RightChop(DotIdx + 1);

		// Search loaded classes by short name.
		UClass* FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
		if (!FoundClass)
		{
			OutError = FString::Printf(TEXT("Class not found (must be loaded): %s"), *ClassName);
			return false;
		}
		FProperty* Prop = FindFProperty<FProperty>(FoundClass, *PropName);
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Property '%s' not found on class '%s'"), *PropName, *ClassName);
			return false;
		}
		OutAttr = FGameplayAttribute(Prop);
		return true;
	}

	// Parse a JSON ScalableFloat: { value, curve_table?, row_name? }.
	FScalableFloat GEParseScalableFloat(const TSharedPtr<FJsonObject>& Obj)
	{
		FScalableFloat SF;
		if (!Obj.IsValid()) return SF;

		double V = 0.0;
		if (Obj->TryGetNumberField(TEXT("value"), V))
		{
			SF.Value = static_cast<float>(V);
		}
		FString CurveTablePath;
		if (Obj->TryGetStringField(TEXT("curve_table"), CurveTablePath) && !CurveTablePath.IsEmpty())
		{
			SF.Curve.CurveTable = Cast<UCurveTable>(GELoadAssetWithFallback(CurveTablePath));
		}
		FString RowName;
		if (Obj->TryGetStringField(TEXT("row_name"), RowName) && !RowName.IsEmpty())
		{
			SF.Curve.RowName = FName(*RowName);
		}
		return SF;
	}

	// Parse magnitude: { calculation: "ScalableFloat"|"SetByCaller", scalable_float|set_by_caller }.
	// Only ScalableFloat + SetByCaller supported in P1 (per user decision).
	bool GEParseMagnitude(const TSharedPtr<FJsonObject>& Obj,
	                     FGameplayEffectModifierMagnitude& OutMag, FString& OutError)
	{
		if (!Obj.IsValid())
		{
			OutError = TEXT("magnitude object missing");
			return false;
		}
		FString CalcStr;
		if (!Obj->TryGetStringField(TEXT("calculation"), CalcStr))
		{
			CalcStr = TEXT("ScalableFloat");
		}
		if (CalcStr.Equals(TEXT("ScalableFloat"), ESearchCase::IgnoreCase))
		{
			const TSharedPtr<FJsonObject>* SFObj = nullptr;
			TSharedPtr<FJsonObject> SFRoot;
			if (Obj->TryGetObjectField(TEXT("scalable_float"), SFObj) && SFObj && SFObj->IsValid())
			{
				SFRoot = *SFObj;
			}
			else
			{
				// Allow flat form: { calculation: "ScalableFloat", value: 5.0 }
				SFRoot = Obj;
			}
			OutMag = FGameplayEffectModifierMagnitude(GEParseScalableFloat(SFRoot));
			return true;
		}
		if (CalcStr.Equals(TEXT("SetByCaller"), ESearchCase::IgnoreCase))
		{
			FSetByCallerFloat SBC;
			const TSharedPtr<FJsonObject>* SBCObj = nullptr;
			TSharedPtr<FJsonObject> Root;
			if (Obj->TryGetObjectField(TEXT("set_by_caller"), SBCObj) && SBCObj && SBCObj->IsValid())
			{
				Root = *SBCObj;
			}
			else
			{
				Root = Obj;
			}
			FString TagStr;
			if (Root->TryGetStringField(TEXT("data_tag"), TagStr) && !TagStr.IsEmpty())
			{
				SBC.DataTag = FGameplayTag::RequestGameplayTag(FName(*TagStr), /*ErrorIfNotFound=*/false);
				if (!SBC.DataTag.IsValid())
				{
					OutError = FString::Printf(TEXT("SetByCaller data_tag not registered: %s"), *TagStr);
					return false;
				}
			}
			FString NameStr;
			if (Root->TryGetStringField(TEXT("data_name"), NameStr) && !NameStr.IsEmpty())
			{
				SBC.DataName = FName(*NameStr);
			}
			if (SBC.DataTag.IsValid() == false && SBC.DataName.IsNone())
			{
				OutError = TEXT("SetByCaller requires either 'data_tag' or 'data_name'");
				return false;
			}
			OutMag = FGameplayEffectModifierMagnitude(SBC);
			return true;
		}
		OutError = FString::Printf(TEXT("Unsupported magnitude calculation '%s' (P1 supports ScalableFloat + SetByCaller only)"), *CalcStr);
		return false;
	}

	// Parse a JSON array of dotted-name strings → FGameplayTagContainer.
	void GEParseTagArray(const TArray<TSharedPtr<FJsonValue>>& Arr, FGameplayTagContainer& Out)
	{
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& V : Arr)
		{
			if (!V.IsValid()) continue;
			const FString S = V->AsString();
			if (S.IsEmpty()) continue;
			const FGameplayTag T = FGameplayTag::RequestGameplayTag(FName(*S), /*ErrorIfNotFound=*/false);
			if (T.IsValid())
			{
				Out.AddTag(T);
			}
		}
	}

	// Mark blueprint as modified after CDO edit. CDO-only changes don't need
	// MarkBlueprintAsStructurallyModified (that's for graphs); package dirty +
	// MarkBlueprintAsModified is sufficient.
	void GEMarkBlueprintModified(UBlueprint* BP)
	{
		if (!BP) return;
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		if (UPackage* Pkg = BP->GetOutermost())
		{
			Pkg->MarkPackageDirty();
		}
	}

	// ------------------------------------------------------------------
	// Asset path splitter: "/Game/Foo/BP_X" -> Package "/Game/Foo/BP_X", Asset "BP_X"
	// ------------------------------------------------------------------
	bool GESplitAssetPath(const FString& InPath, FString& OutPackageName, FString& OutAssetName)
	{
		FString Path = InPath;
		// Trim ".ObjectName" suffix if present.
		int32 DotIdx;
		if (Path.FindLastChar('.', DotIdx))
		{
			int32 SlashIdx;
			if (Path.FindLastChar('/', SlashIdx) && DotIdx > SlashIdx)
			{
				Path = Path.Left(DotIdx);
			}
		}
		int32 LastSlash;
		if (!Path.FindLastChar('/', LastSlash) || LastSlash <= 0)
		{
			return false;
		}
		OutPackageName = Path;
		OutAssetName = Path.RightChop(LastSlash + 1);
		return !OutAssetName.IsEmpty();
	}
}

// =============================================================================
// FUnrealMCPGameplayEffectCommands
// =============================================================================
FUnrealMCPGameplayEffectCommands::FUnrealMCPGameplayEffectCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleCommand(
	const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_gameplay_effect_info"))
	{
		return HandleGetGameplayEffectInfo(Params);
	}
	if (CommandType == TEXT("create_gameplay_effect"))
	{
		return HandleCreateGameplayEffect(Params);
	}
	if (CommandType == TEXT("set_gameplay_effect_property"))
	{
		return HandleSetGameplayEffectProperty(Params);
	}
	if (CommandType == TEXT("add_gameplay_effect_modifier"))
	{
		return HandleAddGameplayEffectModifier(Params);
	}
	if (CommandType == TEXT("remove_gameplay_effect_modifier"))
	{
		return HandleRemoveGameplayEffectModifier(Params);
	}
	if (CommandType == TEXT("set_gameplay_effect_modifier"))
	{
		return HandleSetGameplayEffectModifier(Params);
	}
	if (CommandType == TEXT("set_gameplay_effect_inherited_tags"))
	{
		return HandleSetGameplayEffectInheritedTags(Params);
	}
	return FUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown GameplayEffect command: %s"), *CommandType));
}

// -----------------------------------------------------------------------------
// get_gameplay_effect_info
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleGetGameplayEffectInfo(
	const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UBlueprint* BP = nullptr;
	UGameplayEffect* GE = GEFindCDO(AssetPath, &BP);
	if (!GE)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a GameplayEffect (or could not be loaded): %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), BP ? BP->GetPathName() : GE->GetClass()->GetPathName());
	Result->SetStringField(TEXT("class_name"), GE->GetClass()->GetName());
	if (BP && BP->ParentClass)
	{
		Result->SetStringField(TEXT("parent_class"), BP->ParentClass->GetPathName());
	}

	// --- Duration -----------------------------------------------------------
	Result->SetStringField(TEXT("duration_policy"),
		GEEnumToString<EGameplayEffectDurationType>(GE->DurationPolicy));

	if (GE->DurationPolicy == EGameplayEffectDurationType::HasDuration)
	{
		Result->SetObjectField(TEXT("duration_magnitude"), GEMagnitudeToJson(GE->DurationMagnitude));
		Result->SetObjectField(TEXT("max_duration_magnitude"), GEMagnitudeToJson(GE->MaxDurationMagnitude));
	}

	// --- Period (HasDuration / Infinite) ------------------------------------
	if (GE->DurationPolicy != EGameplayEffectDurationType::Instant)
	{
		Result->SetObjectField(TEXT("period"), GEScalableFloatToJson(GE->Period));
		Result->SetBoolField(TEXT("execute_periodic_effect_on_application"), GE->bExecutePeriodicEffectOnApplication);
	}

	// --- Modifiers ----------------------------------------------------------
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FGameplayModifierInfo& Mod : GE->Modifiers)
		{
			Arr.Add(MakeShared<FJsonValueObject>(GEModifierInfoToJson(Mod)));
		}
		Result->SetArrayField(TEXT("modifiers"), Arr);
	}

	// --- Counts (details deferred to P2) ------------------------------------
	Result->SetNumberField(TEXT("executions_count"), GE->Executions.Num());
	Result->SetNumberField(TEXT("gameplay_cues_count"), GE->GameplayCues.Num());

	// --- Stacking -----------------------------------------------------------
	{
		TSharedPtr<FJsonObject> Stack = MakeShared<FJsonObject>();
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Stack->SetStringField(TEXT("type"),
			GEEnumToString<EGameplayEffectStackingType>(GE->StackingType));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		Stack->SetNumberField(TEXT("limit_count"), GE->StackLimitCount);
		Stack->SetStringField(TEXT("duration_refresh_policy"),
			GEEnumToString<EGameplayEffectStackingDurationPolicy>(GE->StackDurationRefreshPolicy));
		Stack->SetStringField(TEXT("period_reset_policy"),
			GEEnumToString<EGameplayEffectStackingPeriodPolicy>(GE->StackPeriodResetPolicy));
		Stack->SetStringField(TEXT("expiration_policy"),
			GEEnumToString<EGameplayEffectStackingExpirationPolicy>(GE->StackExpirationPolicy));
		Stack->SetBoolField(TEXT("factor_in_stack_count"), GE->bFactorInStackCount);
		Result->SetObjectField(TEXT("stacking"), Stack);
	}

	// --- Inherited tags (via Components, UE 5.3+ Component model) -----------
	{
		TSharedPtr<FJsonObject> Tags = MakeShared<FJsonObject>();

		// Granted Tags (and Cached Blocked Ability Tags live alongside on UGameplayEffect)
		TSharedPtr<FJsonObject> Granted = MakeShared<FJsonObject>();
		if (const UTargetTagsGameplayEffectComponent* TGC = GE->FindComponent<UTargetTagsGameplayEffectComponent>())
		{
			Granted = GEInheritedTagsToJson(TGC->GetConfiguredTargetTagChanges());
		}
		else
		{
			// No component → just emit empty added/removed; combined falls back to GE cache.
			TArray<TSharedPtr<FJsonValue>> Empty;
			Granted->SetArrayField(TEXT("added"), Empty);
			Granted->SetArrayField(TEXT("removed"), Empty);
			Granted->SetField(TEXT("combined"), GETagContainerToJson(GE->GetGrantedTags()));
		}
		Tags->SetObjectField(TEXT("granted_tags"), Granted);

		// Asset Tags
		TSharedPtr<FJsonObject> Asset = MakeShared<FJsonObject>();
		if (const UAssetTagsGameplayEffectComponent* AGC = GE->FindComponent<UAssetTagsGameplayEffectComponent>())
		{
			Asset = GEInheritedTagsToJson(AGC->GetConfiguredAssetTagChanges());
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> Empty;
			Asset->SetArrayField(TEXT("added"), Empty);
			Asset->SetArrayField(TEXT("removed"), Empty);
			Asset->SetField(TEXT("combined"), GETagContainerToJson(GE->GetAssetTags()));
		}
		Tags->SetObjectField(TEXT("asset_tags"), Asset);

		// Cached blocked ability tags (computed from TargetTags component on PostLoad)
		Tags->SetField(TEXT("blocked_ability_tags_combined"), GETagContainerToJson(GE->GetBlockedAbilityTags()));

		Result->SetObjectField(TEXT("inherited_tags"), Tags);
	}

	// --- Component summary (which UGameplayEffectComponent subclasses present) ---
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		// GEComponents is protected — use ForEachObjectWithOuter to discover them by class.
		ForEachObjectWithOuter(GE, [&](UObject* Inner)
		{
			if (Inner && Inner->IsA<UGameplayEffectComponent>())
			{
				Arr.Add(MakeShared<FJsonValueString>(Inner->GetClass()->GetName()));
			}
		}, /*bIncludeNestedObjects=*/ false);
		Result->SetArrayField(TEXT("components"), Arr);
		Result->SetNumberField(TEXT("components_count"), Arr.Num());
	}

	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// create_gameplay_effect
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleCreateGameplayEffect(
	const TSharedPtr<FJsonObject>& Params)
{
	FString PackagePath;
	FString AssetName;
	FString ParentPath;

	const bool bHasAssetPath = Params->TryGetStringField(TEXT("asset_path"), PackagePath);
	const bool bHasAssetName = Params->TryGetStringField(TEXT("asset_name"), AssetName);
	if (!bHasAssetPath || !bHasAssetName)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing required parameters: need both 'asset_path' (folder) and 'asset_name'"));
	}
	Params->TryGetStringField(TEXT("parent_path"), ParentPath);

	// Sanitize folder path: must start with "/Game/" or "/Plugin/" — accept "/Game/Foo" or "/Game/Foo/".
	if (PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath = PackagePath.LeftChop(1);
	}
	const FString FullPackageName = PackagePath + TEXT("/") + AssetName;

	// Existence check.
	if (UPackage* Existing = FindPackage(nullptr, *FullPackageName))
	{
		if (FindObject<UBlueprint>(Existing, *AssetName))
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Blueprint already exists: %s"), *FullPackageName));
		}
	}

	// Resolve parent class.
	UClass* ParentClass = UGameplayEffect::StaticClass();
	if (!ParentPath.IsEmpty())
	{
		// Try direct UClass load first (handles `/Script/Module.Class`).
		UClass* Loaded = LoadClass<UObject>(nullptr, *ParentPath);
		if (!Loaded)
		{
			// Maybe the user passed a Blueprint path — load its GeneratedClass.
			UObject* AsObj = GELoadAssetWithFallback(ParentPath);
			if (UBlueprint* AsBP = Cast<UBlueprint>(AsObj))
			{
				Loaded = AsBP->GeneratedClass;
			}
			else if (UClass* AsCls = Cast<UClass>(AsObj))
			{
				Loaded = AsCls;
			}
		}
		if (!Loaded || !Loaded->IsChildOf(UGameplayEffect::StaticClass()))
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("parent_path is not a UGameplayEffect subclass: %s"), *ParentPath));
		}
		ParentClass = Loaded;
	}

	// Create package.
	UPackage* Package = CreatePackage(*FullPackageName);
	if (!Package)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("CreatePackage failed: %s"), *FullPackageName));
	}
	Package->FullyLoad();

	// Use FKismetEditorUtilities::CreateBlueprint — same path the asset menu takes.
	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		FName(*AssetName),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);

	if (!NewBP)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("FKismetEditorUtilities::CreateBlueprint returned null"));
	}

	FAssetRegistryModule::AssetCreated(NewBP);
	Package->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), NewBP->GetPathName());
	Result->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
	Result->SetStringField(TEXT("blueprint_class"), NewBP->GetClass()->GetName());
	Result->SetBoolField(TEXT("compiled"), false);
	Result->SetBoolField(TEXT("saved"), false);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// =============================================================================
// P1 implementations
// =============================================================================

// -----------------------------------------------------------------------------
// set_gameplay_effect_property — supported properties:
//   duration_policy                          (enum string)
//   duration_magnitude / max_duration_magnitude / period_magnitude
//                                            (magnitude object)
//   period                                   (scalable_float object — alias for period_magnitude.ScalableFloat)
//   execute_periodic_effect_on_application   (bool)
//   stacking_type                            (enum string)
//   stack_limit_count                        (int)
//   stack_duration_refresh_policy            (enum string)
//   stack_period_reset_policy                (enum string)
//   stack_expiration_policy                  (enum string)
//   factor_in_stack_count                    (bool)
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleSetGameplayEffectProperty(
	const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, PropName;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
	}
	if (!Params->TryGetStringField(TEXT("property"), PropName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property'"));
	}
	const TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("value"));
	if (!Value.IsValid())
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'value'"));
	}

	UBlueprint* BP = nullptr;
	UGameplayEffect* GE = GEFindCDO(AssetPath, &BP);
	if (!GE)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a GameplayEffect: %s"), *AssetPath));
	}

	const FString P = PropName;

	if (P.Equals(TEXT("duration_policy"), ESearchCase::IgnoreCase))
	{
		EGameplayEffectDurationType V;
		if (!GEParseEnum(Value->AsString(), V))
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Invalid duration_policy: %s"), *Value->AsString()));
		}
		GE->DurationPolicy = V;
	}
	else if (P.Equals(TEXT("duration_magnitude"), ESearchCase::IgnoreCase))
	{
		FGameplayEffectModifierMagnitude M; FString Err;
		if (!GEParseMagnitude(Value->AsObject(), M, Err))
			return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
		GE->DurationMagnitude = M;
	}
	else if (P.Equals(TEXT("max_duration_magnitude"), ESearchCase::IgnoreCase))
	{
		FGameplayEffectModifierMagnitude M; FString Err;
		if (!GEParseMagnitude(Value->AsObject(), M, Err))
			return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
		GE->MaxDurationMagnitude = M;
	}
	else if (P.Equals(TEXT("period"), ESearchCase::IgnoreCase))
	{
		GE->Period = GEParseScalableFloat(Value->AsObject());
	}
	else if (P.Equals(TEXT("execute_periodic_effect_on_application"), ESearchCase::IgnoreCase))
	{
		GE->bExecutePeriodicEffectOnApplication = Value->AsBool();
	}
	else if (P.Equals(TEXT("stacking_type"), ESearchCase::IgnoreCase))
	{
		EGameplayEffectStackingType V;
		if (!GEParseEnum(Value->AsString(), V))
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Invalid stacking_type: %s"), *Value->AsString()));
		}
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		GE->StackingType = V;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	else if (P.Equals(TEXT("stack_limit_count"), ESearchCase::IgnoreCase))
	{
		GE->StackLimitCount = static_cast<int32>(Value->AsNumber());
	}
	else if (P.Equals(TEXT("stack_duration_refresh_policy"), ESearchCase::IgnoreCase))
	{
		EGameplayEffectStackingDurationPolicy V;
		if (!GEParseEnum(Value->AsString(), V))
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Invalid stack_duration_refresh_policy: %s"), *Value->AsString()));
		}
		GE->StackDurationRefreshPolicy = V;
	}
	else if (P.Equals(TEXT("stack_period_reset_policy"), ESearchCase::IgnoreCase))
	{
		EGameplayEffectStackingPeriodPolicy V;
		if (!GEParseEnum(Value->AsString(), V))
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Invalid stack_period_reset_policy: %s"), *Value->AsString()));
		}
		GE->StackPeriodResetPolicy = V;
	}
	else if (P.Equals(TEXT("stack_expiration_policy"), ESearchCase::IgnoreCase))
	{
		EGameplayEffectStackingExpirationPolicy V;
		if (!GEParseEnum(Value->AsString(), V))
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Invalid stack_expiration_policy: %s"), *Value->AsString()));
		}
		GE->StackExpirationPolicy = V;
	}
	else if (P.Equals(TEXT("factor_in_stack_count"), ESearchCase::IgnoreCase))
	{
		GE->bFactorInStackCount = Value->AsBool();
	}
	else
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unsupported property: %s"), *P));
	}

	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetStringField(TEXT("property"), P);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// add_gameplay_effect_modifier(asset_path, attribute, modifier_op, magnitude)
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleAddGameplayEffectModifier(
	const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath, AttrStr, OpStr;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
	if (!Params->TryGetStringField(TEXT("attribute"), AttrStr))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'attribute' (e.g. 'KLAttributeSet.Health')"));
	if (!Params->TryGetStringField(TEXT("modifier_op"), OpStr))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'modifier_op' (Additive/Multiplicitive/Division/Override)"));

	const TSharedPtr<FJsonObject>* MagObj = nullptr;
	if (!Params->TryGetObjectField(TEXT("magnitude"), MagObj) || !MagObj || !(*MagObj).IsValid())
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'magnitude' object"));

	UBlueprint* BP = nullptr;
	UGameplayEffect* GE = GEFindCDO(AssetPath, &BP);
	if (!GE)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a GameplayEffect: %s"), *AssetPath));

	FGameplayModifierInfo Mod;
	FString Err;
	if (!GEParseAttribute(AttrStr, Mod.Attribute, Err))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
	if (!GEParseModifierOp(OpStr, Mod.ModifierOp))
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Invalid modifier_op: %s"), *OpStr));
	if (!GEParseMagnitude(*MagObj, Mod.ModifierMagnitude, Err))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Err);

	GE->Modifiers.Add(Mod);
	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetNumberField(TEXT("index"), GE->Modifiers.Num() - 1);
	Result->SetNumberField(TEXT("modifiers_count"), GE->Modifiers.Num());
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// remove_gameplay_effect_modifier(asset_path, index)
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleRemoveGameplayEffectModifier(
	const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
	int32 Index = -1;
	if (!Params->TryGetNumberField(TEXT("index"), Index))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'index'"));

	UBlueprint* BP = nullptr;
	UGameplayEffect* GE = GEFindCDO(AssetPath, &BP);
	if (!GE)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a GameplayEffect: %s"), *AssetPath));

	if (Index < 0 || Index >= GE->Modifiers.Num())
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Modifier index out of range: %d (count=%d)"), Index, GE->Modifiers.Num()));

	GE->Modifiers.RemoveAt(Index);
	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetNumberField(TEXT("modifiers_count"), GE->Modifiers.Num());
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// set_gameplay_effect_modifier(asset_path, index, attribute?, modifier_op?, magnitude?)
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleSetGameplayEffectModifier(
	const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));
	int32 Index = -1;
	if (!Params->TryGetNumberField(TEXT("index"), Index))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'index'"));

	UBlueprint* BP = nullptr;
	UGameplayEffect* GE = GEFindCDO(AssetPath, &BP);
	if (!GE)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a GameplayEffect: %s"), *AssetPath));

	if (Index < 0 || Index >= GE->Modifiers.Num())
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Modifier index out of range: %d (count=%d)"), Index, GE->Modifiers.Num()));

	FGameplayModifierInfo& Mod = GE->Modifiers[Index];

	FString AttrStr;
	if (Params->TryGetStringField(TEXT("attribute"), AttrStr))
	{
		FString Err;
		if (!GEParseAttribute(AttrStr, Mod.Attribute, Err))
			return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
	}
	FString OpStr;
	if (Params->TryGetStringField(TEXT("modifier_op"), OpStr))
	{
		if (!GEParseModifierOp(OpStr, Mod.ModifierOp))
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Invalid modifier_op: %s"), *OpStr));
	}
	const TSharedPtr<FJsonObject>* MagObj = nullptr;
	if (Params->TryGetObjectField(TEXT("magnitude"), MagObj) && MagObj && (*MagObj).IsValid())
	{
		FString Err;
		if (!GEParseMagnitude(*MagObj, Mod.ModifierMagnitude, Err))
			return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
	}

	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetNumberField(TEXT("index"), Index);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// set_gameplay_effect_inherited_tags(asset_path, granted?, asset?)
//   - Each channel takes { added: [...], removed: [...] }; missing channel = no change.
//   - Writes via UE 5.3+ Component model (FindOrAdd + SetAndApplyXxxTagChanges).
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleSetGameplayEffectInheritedTags(
	const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path'"));

	UBlueprint* BP = nullptr;
	UGameplayEffect* GE = GEFindCDO(AssetPath, &BP);
	if (!GE)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Asset is not a GameplayEffect: %s"), *AssetPath));

	auto ApplyChannel = [&](const TSharedPtr<FJsonObject>& ChannelObj, FInheritedTagContainer& OutITC) -> bool
	{
		if (!ChannelObj.IsValid()) return false;
		bool bAny = false;
		const TArray<TSharedPtr<FJsonValue>>* AddedArr = nullptr;
		if (ChannelObj->TryGetArrayField(TEXT("added"), AddedArr) && AddedArr)
		{
			GEParseTagArray(*AddedArr, OutITC.Added);
			bAny = true;
		}
		const TArray<TSharedPtr<FJsonValue>>* RemovedArr = nullptr;
		if (ChannelObj->TryGetArrayField(TEXT("removed"), RemovedArr) && RemovedArr)
		{
			GEParseTagArray(*RemovedArr, OutITC.Removed);
			bAny = true;
		}
		return bAny;
	};

	bool bChanged = false;

	const TSharedPtr<FJsonObject>* GrantedObj = nullptr;
	if (Params->TryGetObjectField(TEXT("granted"), GrantedObj) && GrantedObj && (*GrantedObj).IsValid())
	{
		UTargetTagsGameplayEffectComponent& Comp = GE->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
		FInheritedTagContainer ITC = Comp.GetConfiguredTargetTagChanges();
		if (ApplyChannel(*GrantedObj, ITC))
		{
			Comp.SetAndApplyTargetTagChanges(ITC);
			bChanged = true;
		}
	}

	const TSharedPtr<FJsonObject>* AssetObj = nullptr;
	if (Params->TryGetObjectField(TEXT("asset"), AssetObj) && AssetObj && (*AssetObj).IsValid())
	{
		UAssetTagsGameplayEffectComponent& Comp = GE->FindOrAddComponent<UAssetTagsGameplayEffectComponent>();
		FInheritedTagContainer ITC = Comp.GetConfiguredAssetTagChanges();
		if (ApplyChannel(*AssetObj, ITC))
		{
			Comp.SetAndApplyAssetTagChanges(ITC);
			bChanged = true;
		}
	}

	if (!bChanged)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Neither 'granted' nor 'asset' channels were provided"));
	}

	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetBoolField(TEXT("granted_changed"), GrantedObj != nullptr);
	Result->SetBoolField(TEXT("asset_changed"), AssetObj != nullptr);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}
