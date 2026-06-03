#include "Commands/UnrealMCPGameplayEffectCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"

#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffectComponent.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayEffectComponents/ChanceToApplyGameplayEffectComponent.h"
#include "GameplayEffectComponents/AbilitiesGameplayEffectComponent.h"
#include "GameplayEffectComponents/ImmunityGameplayEffectComponent.h"
#include "GameplayEffectComponents/RemoveOtherGameplayEffectComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "AttributeSet.h"
#include "ScalableFloat.h"
#include "GameplayTagContainer.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ObjectTools.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"

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
	// Forward declarations — definitions appear later in this TU but are
	// referenced from earlier helpers (ModifierToJson / ParseModifierMagnitude).
	// FAttributeBasedFloat itself is provided by GameplayEffectTypes.h.
	// ------------------------------------------------------------------
	bool GEParseAttributeBasedFloat(const TSharedPtr<FJsonObject>& Obj,
	                                FAttributeBasedFloat& Out, FString& OutError);
	TSharedPtr<FJsonObject> GEAttributeBasedFloatToJson(const FAttributeBasedFloat& A);

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
		{
			// [LEOCC] P2: read AttributeBasedMagnitude via reflection (the field
			// is `protected` on FGameplayEffectModifierMagnitude, no public getter).
			if (const FStructProperty* AttrProp = CastField<FStructProperty>(
				FGameplayEffectModifierMagnitude::StaticStruct()->FindPropertyByName(TEXT("AttributeBasedMagnitude"))))
			{
				const FAttributeBasedFloat* ABF =
					AttrProp->ContainerPtrToValuePtr<FAttributeBasedFloat>(&Mag);
				if (ABF)
				{
					Obj->SetObjectField(TEXT("attribute_based"), GEAttributeBasedFloatToJson(*ABF));
				}
			}
			break;
		}
		case EGameplayEffectMagnitudeCalculation::CustomCalculationClass:
		default:
			// CustomCalculationClass detail deferred to P3 (needs class reference + scoped mods).
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
		if (CalcStr.Equals(TEXT("AttributeBased"), ESearchCase::IgnoreCase))
		{
			// [LEOCC] P2: AttributeBased magnitude full-fielded parse.
			FAttributeBasedFloat ABF;
			const TSharedPtr<FJsonObject>* ABFObj = nullptr;
			TSharedPtr<FJsonObject> Root;
			if (Obj->TryGetObjectField(TEXT("attribute_based"), ABFObj) && ABFObj && ABFObj->IsValid())
			{
				Root = *ABFObj;
			}
			else
			{
				Root = Obj;
			}
			if (!GEParseAttributeBasedFloat(Root, ABF, OutError)) return false;
			OutMag = FGameplayEffectModifierMagnitude(ABF);
			return true;
		}
		OutError = FString::Printf(TEXT("Unsupported magnitude calculation '%s' (P1+P2 supports ScalableFloat / SetByCaller / AttributeBased; CustomCalculationClass is P3)"), *CalcStr);
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

	// ==================================================================
	// P2 helpers (2026-06-03 second pass)
	// ==================================================================

	// Parse { required: [tag strings], ignored: [tag strings] } → FGameplayTagRequirements.
	// Missing fields = keep existing on Out (caller seeds Out before calling).
	bool GEParseTagRequirements(const TSharedPtr<FJsonObject>& Obj, FGameplayTagRequirements& Out)
	{
		if (!Obj.IsValid()) return false;
		bool bChanged = false;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(TEXT("required"), Arr) && Arr)
		{
			GEParseTagArray(*Arr, Out.RequireTags);
			bChanged = true;
		}
		if (Obj->TryGetArrayField(TEXT("ignored"), Arr) && Arr)
		{
			GEParseTagArray(*Arr, Out.IgnoreTags);
			bChanged = true;
		}
		return bChanged;
	}

	TSharedPtr<FJsonObject> GETagRequirementsToJson(const FGameplayTagRequirements& Req)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetField(TEXT("required"), GETagContainerToJson(Req.RequireTags));
		Obj->SetField(TEXT("ignored"),  GETagContainerToJson(Req.IgnoreTags));
		return Obj;
	}

	// Parse a JSON ScalableFloat into an existing FScalableFloat (used by AttributeBased
	// nested fields). Same semantics as GEParseScalableFloat.
	FScalableFloat GEParseScalableFloatFromValue(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid()) return FScalableFloat();
		if (V->Type == EJson::Object) return GEParseScalableFloat(V->AsObject());
		// Number → flat scalar
		if (V->Type == EJson::Number)
		{
			FScalableFloat SF; SF.Value = static_cast<float>(V->AsNumber()); return SF;
		}
		return FScalableFloat();
	}

	// Parse a JSON AttributeBased magnitude object into FAttributeBasedFloat.
	// Schema:
	// {
	//   coefficient:                 ScalableFloat | number
	//   pre_multiply_additive_value: ScalableFloat | number
	//   post_multiply_additive_value:ScalableFloat | number
	//   backing_attribute: { attribute: "Class.Prop", source: "Source"|"Target", snapshot: bool }
	//   attribute_curve:    { curve_table, row_name }
	//   attribute_calculation_type: enum string
	//   final_channel:              enum string
	//   source_tag_filter:          [tag strings]
	//   target_tag_filter:          [tag strings]
	// }
	bool GEParseAttributeBasedFloat(const TSharedPtr<FJsonObject>& Obj,
	                                FAttributeBasedFloat& Out, FString& OutError)
	{
		if (!Obj.IsValid())
		{
			OutError = TEXT("attribute_based object missing");
			return false;
		}

		Out.Coefficient                 = GEParseScalableFloatFromValue(Obj->TryGetField(TEXT("coefficient")));
		Out.PreMultiplyAdditiveValue    = GEParseScalableFloatFromValue(Obj->TryGetField(TEXT("pre_multiply_additive_value")));
		Out.PostMultiplyAdditiveValue   = GEParseScalableFloatFromValue(Obj->TryGetField(TEXT("post_multiply_additive_value")));

		const TSharedPtr<FJsonObject>* BackingObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("backing_attribute"), BackingObj) && BackingObj && (*BackingObj).IsValid())
		{
			FString AttrStr;
			if ((*BackingObj)->TryGetStringField(TEXT("attribute"), AttrStr))
			{
				if (!GEParseAttribute(AttrStr, Out.BackingAttribute.AttributeToCapture, OutError)) return false;
			}
			FString SrcStr;
			if ((*BackingObj)->TryGetStringField(TEXT("source"), SrcStr))
			{
				EGameplayEffectAttributeCaptureSource Src;
				if (!GEParseEnum(SrcStr, Src))
				{
					OutError = FString::Printf(TEXT("Invalid backing_attribute.source: %s"), *SrcStr);
					return false;
				}
				Out.BackingAttribute.AttributeSource = Src;
			}
			bool Snapshot = false;
			if ((*BackingObj)->TryGetBoolField(TEXT("snapshot"), Snapshot))
			{
				Out.BackingAttribute.bSnapshot = Snapshot;
			}
		}

		const TSharedPtr<FJsonObject>* CurveObj = nullptr;
		if (Obj->TryGetObjectField(TEXT("attribute_curve"), CurveObj) && CurveObj && (*CurveObj).IsValid())
		{
			FString CT, RN;
			if ((*CurveObj)->TryGetStringField(TEXT("curve_table"), CT) && !CT.IsEmpty())
			{
				Out.AttributeCurve.CurveTable = Cast<UCurveTable>(GELoadAssetWithFallback(CT));
			}
			if ((*CurveObj)->TryGetStringField(TEXT("row_name"), RN) && !RN.IsEmpty())
			{
				Out.AttributeCurve.RowName = FName(*RN);
			}
		}

		FString CalcTypeStr;
		if (Obj->TryGetStringField(TEXT("attribute_calculation_type"), CalcTypeStr))
		{
			EAttributeBasedFloatCalculationType V;
			if (!GEParseEnum(CalcTypeStr, V))
			{
				OutError = FString::Printf(TEXT("Invalid attribute_calculation_type: %s"), *CalcTypeStr);
				return false;
			}
			Out.AttributeCalculationType = V;
		}

		FString ChannelStr;
		if (Obj->TryGetStringField(TEXT("final_channel"), ChannelStr))
		{
			EGameplayModEvaluationChannel V;
			if (!GEParseEnum(ChannelStr, V))
			{
				OutError = FString::Printf(TEXT("Invalid final_channel: %s"), *ChannelStr);
				return false;
			}
			Out.FinalChannel = V;
		}

		const TArray<TSharedPtr<FJsonValue>>* TagArr = nullptr;
		if (Obj->TryGetArrayField(TEXT("source_tag_filter"), TagArr) && TagArr) GEParseTagArray(*TagArr, Out.SourceTagFilter);
		if (Obj->TryGetArrayField(TEXT("target_tag_filter"), TagArr) && TagArr) GEParseTagArray(*TagArr, Out.TargetTagFilter);
		return true;
	}

	// Serialize FAttributeBasedFloat to JSON (full 7-field detail).
	TSharedPtr<FJsonObject> GEAttributeBasedFloatToJson(const FAttributeBasedFloat& A)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetObjectField(TEXT("coefficient"),                   GEScalableFloatToJson(A.Coefficient));
		Obj->SetObjectField(TEXT("pre_multiply_additive_value"),   GEScalableFloatToJson(A.PreMultiplyAdditiveValue));
		Obj->SetObjectField(TEXT("post_multiply_additive_value"),  GEScalableFloatToJson(A.PostMultiplyAdditiveValue));

		TSharedPtr<FJsonObject> Backing = MakeShared<FJsonObject>();
		Backing->SetStringField(TEXT("attribute"), GEAttributeToString(A.BackingAttribute.AttributeToCapture));
		Backing->SetStringField(TEXT("source"),    GEEnumToString<EGameplayEffectAttributeCaptureSource>(A.BackingAttribute.AttributeSource));
		Backing->SetBoolField  (TEXT("snapshot"),  A.BackingAttribute.bSnapshot);
		Obj->SetObjectField(TEXT("backing_attribute"), Backing);

		TSharedPtr<FJsonObject> Curve = MakeShared<FJsonObject>();
		if (A.AttributeCurve.CurveTable)   Curve->SetStringField(TEXT("curve_table"), A.AttributeCurve.CurveTable->GetPathName());
		if (!A.AttributeCurve.RowName.IsNone()) Curve->SetStringField(TEXT("row_name"), A.AttributeCurve.RowName.ToString());
		Obj->SetObjectField(TEXT("attribute_curve"), Curve);

		Obj->SetStringField(TEXT("attribute_calculation_type"), GEEnumToString<EAttributeBasedFloatCalculationType>(A.AttributeCalculationType));
		Obj->SetStringField(TEXT("final_channel"),              GEEnumToString<EGameplayModEvaluationChannel>(A.FinalChannel));
		Obj->SetField(TEXT("source_tag_filter"), GETagContainerToJson(A.SourceTagFilter));
		Obj->SetField(TEXT("target_tag_filter"), GETagContainerToJson(A.TargetTagFilter));
		return Obj;
	}

	// Parse a FGameplayEffectCue:
	//   { cue_tags: [...], min_level: 0, max_level: 0, magnitude_attribute?: "Class.Prop" }
	bool GEParseGameplayEffectCue(const TSharedPtr<FJsonObject>& Obj,
	                              FGameplayEffectCue& Out, FString& OutError)
	{
		if (!Obj.IsValid())
		{
			OutError = TEXT("cue object missing");
			return false;
		}
		double Min = Out.MinLevel, Max = Out.MaxLevel;
		Obj->TryGetNumberField(TEXT("min_level"), Min);
		Obj->TryGetNumberField(TEXT("max_level"), Max);
		Out.MinLevel = static_cast<float>(Min);
		Out.MaxLevel = static_cast<float>(Max);

		const TArray<TSharedPtr<FJsonValue>>* TagArr = nullptr;
		if (Obj->TryGetArrayField(TEXT("cue_tags"), TagArr) && TagArr)
		{
			GEParseTagArray(*TagArr, Out.GameplayCueTags);
		}

		FString AttrStr;
		if (Obj->TryGetStringField(TEXT("magnitude_attribute"), AttrStr) && !AttrStr.IsEmpty())
		{
			FString Err;
			if (!GEParseAttribute(AttrStr, Out.MagnitudeAttribute, Err)) { OutError = Err; return false; }
		}
		return true;
	}

	TSharedPtr<FJsonObject> GECueToJson(const FGameplayEffectCue& Cue)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetField(TEXT("cue_tags"), GETagContainerToJson(Cue.GameplayCueTags));
		Obj->SetNumberField(TEXT("min_level"), Cue.MinLevel);
		Obj->SetNumberField(TEXT("max_level"), Cue.MaxLevel);
		Obj->SetStringField(TEXT("magnitude_attribute"), GEAttributeToString(Cue.MagnitudeAttribute));
		return Obj;
	}

	// Parse FGameplayAbilitySpecConfig:
	//   { ability_class: "/Game/.../GA_Foo" | "/Script/Module.GA_Foo_C",
	//     level: ScalableFloat | number,
	//     input_id: int,
	//     removal_policy: enum string }
	bool GEParseAbilitySpecConfig(const TSharedPtr<FJsonObject>& Obj,
	                              FGameplayAbilitySpecConfig& Out, FString& OutError)
	{
		if (!Obj.IsValid())
		{
			OutError = TEXT("granted_ability object missing");
			return false;
		}
		FString ClassPath;
		if (Obj->TryGetStringField(TEXT("ability_class"), ClassPath) && !ClassPath.IsEmpty())
		{
			UClass* Loaded = LoadClass<UObject>(nullptr, *ClassPath);
			if (!Loaded)
			{
				UObject* AsObj = GELoadAssetWithFallback(ClassPath);
				if (UBlueprint* AsBP = Cast<UBlueprint>(AsObj))    Loaded = AsBP->GeneratedClass;
				else if (UClass* AsCls = Cast<UClass>(AsObj))      Loaded = AsCls;
			}
			if (!Loaded || !Loaded->IsChildOf(UGameplayAbility::StaticClass()))
			{
				OutError = FString::Printf(TEXT("ability_class is not a UGameplayAbility subclass: %s"), *ClassPath);
				return false;
			}
			Out.Ability = Loaded;
		}
		if (Obj->HasField(TEXT("level")))
		{
			Out.LevelScalableFloat = GEParseScalableFloatFromValue(Obj->TryGetField(TEXT("level")));
		}
		int32 Input;
		if (Obj->TryGetNumberField(TEXT("input_id"), Input)) Out.InputID = Input;
		FString PolicyStr;
		if (Obj->TryGetStringField(TEXT("removal_policy"), PolicyStr))
		{
			EGameplayEffectGrantedAbilityRemovePolicy P;
			if (!GEParseEnum(PolicyStr, P))
			{
				OutError = FString::Printf(TEXT("Invalid removal_policy: %s"), *PolicyStr);
				return false;
			}
			Out.RemovalPolicy = P;
		}
		return true;
	}

	TSharedPtr<FJsonObject> GEAbilitySpecConfigToJson(const FGameplayAbilitySpecConfig& C)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("ability_class"), C.Ability ? C.Ability->GetPathName() : FString());
		Obj->SetObjectField(TEXT("level"), GEScalableFloatToJson(C.LevelScalableFloat));
		Obj->SetNumberField(TEXT("input_id"), C.InputID);
		Obj->SetStringField(TEXT("removal_policy"),
			GEEnumToString<EGameplayEffectGrantedAbilityRemovePolicy>(C.RemovalPolicy));
		return Obj;
	}

	// Reflection access to UAbilitiesGEC's protected GrantAbilityConfigs array.
	// Returns the FScriptArrayHelper bound to it (caller checks IsValid via Prop).
	FArrayProperty* GEFindGrantAbilityConfigsProp()
	{
		return FindFProperty<FArrayProperty>(
			UAbilitiesGameplayEffectComponent::StaticClass(),
			TEXT("GrantAbilityConfigs"));
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
	// P2
	if (CommandType == TEXT("list_gameplay_effects"))            return HandleListGameplayEffects(Params);
	if (CommandType == TEXT("delete_gameplay_effect"))           return HandleDeleteGameplayEffect(Params);
	if (CommandType == TEXT("add_gameplay_effect_cue"))          return HandleAddGameplayEffectCue(Params);
	if (CommandType == TEXT("remove_gameplay_effect_cue"))       return HandleRemoveGameplayEffectCue(Params);
	if (CommandType == TEXT("set_gameplay_effect_cue"))          return HandleSetGameplayEffectCue(Params);
	if (CommandType == TEXT("set_gameplay_effect_tag_requirements")) return HandleSetGameplayEffectTagRequirements(Params);
	if (CommandType == TEXT("set_gameplay_effect_chance_to_apply"))  return HandleSetGameplayEffectChanceToApply(Params);
	if (CommandType == TEXT("add_gameplay_effect_granted_ability"))  return HandleAddGameplayEffectGrantedAbility(Params);
	if (CommandType == TEXT("remove_gameplay_effect_granted_ability")) return HandleRemoveGameplayEffectGrantedAbility(Params);
	if (CommandType == TEXT("set_gameplay_effect_granted_ability"))  return HandleSetGameplayEffectGrantedAbility(Params);

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

	// --- Executions (count only — full read deferred to P3) -----------------
	Result->SetNumberField(TEXT("executions_count"), GE->Executions.Num());

	// --- GameplayCues (full detail, P2) -------------------------------------
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FGameplayEffectCue& Cue : GE->GameplayCues)
		{
			Arr.Add(MakeShared<FJsonValueObject>(GECueToJson(Cue)));
		}
		Result->SetArrayField(TEXT("gameplay_cues"), Arr);
	}

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

	// --- P2: Tag Requirements (Application / Ongoing / Removal) -------------
	{
		TSharedPtr<FJsonObject> TR = MakeShared<FJsonObject>();
		if (const UTargetTagRequirementsGameplayEffectComponent* Comp =
			GE->FindComponent<UTargetTagRequirementsGameplayEffectComponent>())
		{
			TR->SetObjectField(TEXT("application"), GETagRequirementsToJson(Comp->ApplicationTagRequirements));
			TR->SetObjectField(TEXT("ongoing"),     GETagRequirementsToJson(Comp->OngoingTagRequirements));
			TR->SetObjectField(TEXT("removal"),     GETagRequirementsToJson(Comp->RemovalTagRequirements));
		}
		else
		{
			FGameplayTagRequirements Empty;
			TR->SetObjectField(TEXT("application"), GETagRequirementsToJson(Empty));
			TR->SetObjectField(TEXT("ongoing"),     GETagRequirementsToJson(Empty));
			TR->SetObjectField(TEXT("removal"),     GETagRequirementsToJson(Empty));
		}
		Result->SetObjectField(TEXT("tag_requirements"), TR);
	}

	// --- P2: Chance To Apply ------------------------------------------------
	if (const UChanceToApplyGameplayEffectComponent* Comp =
		GE->FindComponent<UChanceToApplyGameplayEffectComponent>())
	{
		Result->SetObjectField(TEXT("chance_to_apply"),
			GEScalableFloatToJson(Comp->GetChanceToApplyToTarget()));
	}

	// --- P2: Granted Abilities ---------------------------------------------
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		if (const UAbilitiesGameplayEffectComponent* Comp =
			GE->FindComponent<UAbilitiesGameplayEffectComponent>())
		{
			if (FArrayProperty* ArrayProp = GEFindGrantAbilityConfigsProp())
			{
				FScriptArrayHelper Helper(ArrayProp,
					ArrayProp->ContainerPtrToValuePtr<void>(Comp));
				for (int32 I = 0; I < Helper.Num(); ++I)
				{
					const FGameplayAbilitySpecConfig* C =
						reinterpret_cast<const FGameplayAbilitySpecConfig*>(Helper.GetRawPtr(I));
					Arr.Add(MakeShared<FJsonValueObject>(GEAbilitySpecConfigToJson(*C)));
				}
			}
		}
		Result->SetArrayField(TEXT("granted_abilities"), Arr);
	}

	// --- P2: Immunity / RemoveOther counts (full detail deferred to P3) -----
	{
		int32 ImmunityCount = 0, RemoveOtherCount = 0;
		if (const UImmunityGameplayEffectComponent* Comp = GE->FindComponent<UImmunityGameplayEffectComponent>())
		{
			ImmunityCount = Comp->ImmunityQueries.Num();
		}
		if (const URemoveOtherGameplayEffectComponent* Comp = GE->FindComponent<URemoveOtherGameplayEffectComponent>())
		{
			RemoveOtherCount = Comp->RemoveGameplayEffectQueries.Num();
		}
		Result->SetNumberField(TEXT("immunity_queries_count"), ImmunityCount);
		Result->SetNumberField(TEXT("remove_other_queries_count"), RemoveOtherCount);
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

	// [LEOCC] P2: optional source/target tag requirements at add time.
	const TSharedPtr<FJsonObject>* SrcTagsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("source_tags"), SrcTagsObj) && SrcTagsObj && (*SrcTagsObj).IsValid())
		GEParseTagRequirements(*SrcTagsObj, Mod.SourceTags);
	const TSharedPtr<FJsonObject>* TgtTagsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("target_tags"), TgtTagsObj) && TgtTagsObj && (*TgtTagsObj).IsValid())
		GEParseTagRequirements(*TgtTagsObj, Mod.TargetTags);

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
	// [LEOCC] P2: optional source/target tag requirements at set time (partial update).
	const TSharedPtr<FJsonObject>* SrcTagsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("source_tags"), SrcTagsObj) && SrcTagsObj && (*SrcTagsObj).IsValid())
		GEParseTagRequirements(*SrcTagsObj, Mod.SourceTags);
	const TSharedPtr<FJsonObject>* TgtTagsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("target_tags"), TgtTagsObj) && TgtTagsObj && (*TgtTagsObj).IsValid())
		GEParseTagRequirements(*TgtTagsObj, Mod.TargetTags);

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

// =============================================================================
// P2 implementations (2026-06-03 second pass)
// =============================================================================

// -----------------------------------------------------------------------------
// list_gameplay_effects(path_filter?)
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleListGameplayEffects(
	const TSharedPtr<FJsonObject>& Params)
{
	FString PathFilter;
	Params->TryGetStringField(TEXT("path_filter"), PathFilter);

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(*PathFilter);
		Filter.bRecursivePaths = true;
	}

	TArray<FAssetData> Hits;
	AR.GetAssets(Filter, Hits);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetData& AD : Hits)
	{
		// Use the asset registry's NativeParentClass tag to filter without loading.
		const FString ParentClass = AD.GetTagValueRef<FString>(TEXT("ParentClass"));
		const FString NativeParent = AD.GetTagValueRef<FString>(TEXT("NativeParentClass"));
		const bool bIsGE =
			ParentClass.Contains(TEXT("GameplayEffect")) ||
			NativeParent.Contains(TEXT("GameplayEffect"));
		if (!bIsGE) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_path"), AD.GetObjectPathString());
		Entry->SetStringField(TEXT("name"), AD.AssetName.ToString());
		Entry->SetStringField(TEXT("package_path"), AD.PackagePath.ToString());
		Entry->SetStringField(TEXT("parent_class"), ParentClass.IsEmpty() ? NativeParent : ParentClass);
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("gameplay_effects"), Arr);
	Result->SetNumberField(TEXT("count"), Arr.Num());
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// delete_gameplay_effect(asset_path)
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleDeleteGameplayEffect(
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
	if (!BP)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Refusing to delete a native UGameplayEffect — only blueprint GE assets are deletable"));

	const FString DeletedPath = BP->GetPathName();
	TArray<UObject*> Targets{ BP };
	const int32 NumDeleted = ObjectTools::ForceDeleteObjects(Targets, /*bShowConfirmation=*/false);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), DeletedPath);
	Result->SetNumberField(TEXT("num_deleted"), NumDeleted);
	Result->SetBoolField(TEXT("success"), NumDeleted > 0);
	return NumDeleted > 0
		? FUnrealMCPCommonUtils::CreateSuccessResponse(Result)
		: FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Delete failed for %s"), *DeletedPath));
}

// -----------------------------------------------------------------------------
// add_gameplay_effect_cue(asset_path, cue_tags, min_level?, max_level?, magnitude_attribute?)
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleAddGameplayEffectCue(
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

	FGameplayEffectCue Cue;
	FString Err;
	if (!GEParseGameplayEffectCue(Params, Cue, Err))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Err);

	GE->GameplayCues.Add(Cue);
	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetNumberField(TEXT("index"), GE->GameplayCues.Num() - 1);
	Result->SetNumberField(TEXT("cues_count"), GE->GameplayCues.Num());
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// remove_gameplay_effect_cue(asset_path, index)
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleRemoveGameplayEffectCue(
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
	if (Index < 0 || Index >= GE->GameplayCues.Num())
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Cue index out of range: %d (count=%d)"), Index, GE->GameplayCues.Num()));

	GE->GameplayCues.RemoveAt(Index);
	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetNumberField(TEXT("cues_count"), GE->GameplayCues.Num());
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// set_gameplay_effect_cue(asset_path, index, [cue_tags?, min_level?, max_level?, magnitude_attribute?])
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleSetGameplayEffectCue(
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
	if (Index < 0 || Index >= GE->GameplayCues.Num())
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Cue index out of range: %d (count=%d)"), Index, GE->GameplayCues.Num()));

	FString Err;
	if (!GEParseGameplayEffectCue(Params, GE->GameplayCues[Index], Err))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetNumberField(TEXT("index"), Index);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// set_gameplay_effect_tag_requirements(asset_path, application?, ongoing?, removal?)
//   - Each channel is { required: [...], ignored: [...] }; missing channel = no change.
//   - Missing `required` or `ignored` inside a channel = keep existing value.
//   - Uses UTargetTagRequirementsGameplayEffectComponent (UE 5.3+).
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleSetGameplayEffectTagRequirements(
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

	UTargetTagRequirementsGameplayEffectComponent& Comp =
		GE->FindOrAddComponent<UTargetTagRequirementsGameplayEffectComponent>();

	bool bChanged = false;
	const TSharedPtr<FJsonObject>* AppObj = nullptr;
	if (Params->TryGetObjectField(TEXT("application"), AppObj) && AppObj && (*AppObj).IsValid())
		bChanged |= GEParseTagRequirements(*AppObj, Comp.ApplicationTagRequirements);
	const TSharedPtr<FJsonObject>* OngObj = nullptr;
	if (Params->TryGetObjectField(TEXT("ongoing"), OngObj) && OngObj && (*OngObj).IsValid())
		bChanged |= GEParseTagRequirements(*OngObj, Comp.OngoingTagRequirements);
	const TSharedPtr<FJsonObject>* RemObj = nullptr;
	if (Params->TryGetObjectField(TEXT("removal"), RemObj) && RemObj && (*RemObj).IsValid())
		bChanged |= GEParseTagRequirements(*RemObj, Comp.RemovalTagRequirements);

	if (!bChanged)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("None of 'application' / 'ongoing' / 'removal' channels were provided"));

	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetBoolField(TEXT("application_changed"), AppObj != nullptr);
	Result->SetBoolField(TEXT("ongoing_changed"),     OngObj != nullptr);
	Result->SetBoolField(TEXT("removal_changed"),     RemObj != nullptr);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// set_gameplay_effect_chance_to_apply(asset_path, scalable_float | number)
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleSetGameplayEffectChanceToApply(
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

	// Accept either {scalable_float:{...}}, {value:...}, or a top-level number.
	TSharedPtr<FJsonValue> ValField = Params->TryGetField(TEXT("scalable_float"));
	if (!ValField.IsValid()) ValField = Params->TryGetField(TEXT("value"));
	FScalableFloat SF = GEParseScalableFloatFromValue(ValField);
	if (!ValField.IsValid())
	{
		// Allow flat: { asset_path, calculation?, value? }
		SF = GEParseScalableFloat(Params);
	}

	UChanceToApplyGameplayEffectComponent& Comp =
		GE->FindOrAddComponent<UChanceToApplyGameplayEffectComponent>();
	Comp.SetChanceToApplyToTarget(SF);
	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetObjectField(TEXT("chance_to_apply"), GEScalableFloatToJson(SF));
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// add_gameplay_effect_granted_ability(asset_path, ability_class, level?, input_id?, removal_policy?)
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleAddGameplayEffectGrantedAbility(
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

	FGameplayAbilitySpecConfig Cfg;
	FString Err;
	if (!GEParseAbilitySpecConfig(Params, Cfg, Err))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
	if (!Cfg.Ability)
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'ability_class'"));

	UAbilitiesGameplayEffectComponent& Comp =
		GE->FindOrAddComponent<UAbilitiesGameplayEffectComponent>();
	Comp.AddGrantedAbilityConfig(Cfg);
	GEMarkBlueprintModified(BP);

	int32 NewCount = 0;
	if (FArrayProperty* ArrayProp = GEFindGrantAbilityConfigsProp())
	{
		FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(&Comp));
		NewCount = Helper.Num();
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetNumberField(TEXT("granted_abilities_count"), NewCount);
	Result->SetNumberField(TEXT("index"), NewCount - 1);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// remove_gameplay_effect_granted_ability(asset_path, index)
//   Removes via FArrayProperty reflection (GrantAbilityConfigs is protected).
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleRemoveGameplayEffectGrantedAbility(
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

	UAbilitiesGameplayEffectComponent* Comp =
		const_cast<UAbilitiesGameplayEffectComponent*>(GE->FindComponent<UAbilitiesGameplayEffectComponent>());
	if (!Comp)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("No AbilitiesGameplayEffectComponent attached"));

	FArrayProperty* ArrayProp = GEFindGrantAbilityConfigsProp();
	if (!ArrayProp)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("GrantAbilityConfigs property reflection failed"));

	FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Comp));
	if (Index < 0 || Index >= Helper.Num())
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Granted ability index out of range: %d (count=%d)"), Index, Helper.Num()));

	Helper.RemoveValues(Index, 1);
	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetNumberField(TEXT("granted_abilities_count"), Helper.Num());
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// -----------------------------------------------------------------------------
// set_gameplay_effect_granted_ability(asset_path, index, [ability_class?, level?, input_id?, removal_policy?])
// -----------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPGameplayEffectCommands::HandleSetGameplayEffectGrantedAbility(
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

	UAbilitiesGameplayEffectComponent* Comp =
		const_cast<UAbilitiesGameplayEffectComponent*>(GE->FindComponent<UAbilitiesGameplayEffectComponent>());
	if (!Comp)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("No AbilitiesGameplayEffectComponent attached"));

	FArrayProperty* ArrayProp = GEFindGrantAbilityConfigsProp();
	if (!ArrayProp)
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("GrantAbilityConfigs property reflection failed"));

	FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Comp));
	if (Index < 0 || Index >= Helper.Num())
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Granted ability index out of range: %d (count=%d)"), Index, Helper.Num()));

	FGameplayAbilitySpecConfig* Existing =
		reinterpret_cast<FGameplayAbilitySpecConfig*>(Helper.GetRawPtr(Index));
	FString Err;
	if (!GEParseAbilitySpecConfig(Params, *Existing, Err))
		return FUnrealMCPCommonUtils::CreateErrorResponse(Err);
	GEMarkBlueprintModified(BP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), GE->GetPathName());
	Result->SetNumberField(TEXT("index"), Index);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}
