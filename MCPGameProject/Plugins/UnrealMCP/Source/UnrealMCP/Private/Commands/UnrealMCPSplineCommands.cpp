#include "Commands/UnrealMCPSplineCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"
#include "Components/SplineComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"

namespace
{
	// Reuses the same three-stage component search as set_component_property /
	// get_component_properties: own SCS → inherited SCS chain → native C++ CDO.
	// Returns the USplineComponent template suitable for editing; caller is
	// responsible for MarkBlueprintAsModified after a write.
	USplineComponent* FindSplineComponentTemplate(UBlueprint* Blueprint, const FString& ComponentName, FString& OutSource)
	{
		if (!Blueprint) return nullptr;

		// 1. This BP's own SCS
		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate &&
					Node->GetVariableName().ToString() == ComponentName)
				{
					if (USplineComponent* Spline = Cast<USplineComponent>(Node->ComponentTemplate))
					{
						OutSource = TEXT("scs");
						return Spline;
					}
				}
			}
		}

		// 2. GeneratedClass CDO covers both inherited SCS components and native C++ components
		if (Blueprint->GeneratedClass)
		{
			if (AActor* CDO = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()))
			{
				TInlineComponentArray<UActorComponent*> Components;
				CDO->GetComponents(Components);
				for (UActorComponent* Comp : Components)
				{
					if (Comp && Comp->GetName() == ComponentName)
					{
						if (USplineComponent* Spline = Cast<USplineComponent>(Comp))
						{
							OutSource = TEXT("inherited_or_native");
							return Spline;
						}
					}
				}
			}
		}

		return nullptr;
	}

	ESplineCoordinateSpace::Type ParseCoordinateSpace(const TSharedPtr<FJsonObject>& Params, ESplineCoordinateSpace::Type Default)
	{
		FString CoordStr;
		if (Params->TryGetStringField(TEXT("coordinate_space"), CoordStr))
		{
			if (CoordStr.Equals(TEXT("World"), ESearchCase::IgnoreCase))
			{
				return ESplineCoordinateSpace::World;
			}
		}
		return Default;
	}

	ESplinePointType::Type ParsePointType(const FString& Name, ESplinePointType::Type Default)
	{
		if (Name.Equals(TEXT("Linear"), ESearchCase::IgnoreCase)) return ESplinePointType::Linear;
		if (Name.Equals(TEXT("Curve"), ESearchCase::IgnoreCase)) return ESplinePointType::Curve;
		if (Name.Equals(TEXT("Constant"), ESearchCase::IgnoreCase)) return ESplinePointType::Constant;
		if (Name.Equals(TEXT("CurveClamped"), ESearchCase::IgnoreCase)) return ESplinePointType::CurveClamped;
		if (Name.Equals(TEXT("CurveCustomTangent"), ESearchCase::IgnoreCase)) return ESplinePointType::CurveCustomTangent;
		// Aliases — CurveAuto and Curve are both `Curve` in this enum
		if (Name.Equals(TEXT("CurveAuto"), ESearchCase::IgnoreCase)) return ESplinePointType::Curve;
		return Default;
	}

	const TCHAR* PointTypeToString(ESplinePointType::Type Type)
	{
		switch (Type)
		{
			case ESplinePointType::Linear: return TEXT("Linear");
			case ESplinePointType::Curve: return TEXT("Curve");
			case ESplinePointType::Constant: return TEXT("Constant");
			case ESplinePointType::CurveClamped: return TEXT("CurveClamped");
			case ESplinePointType::CurveCustomTangent: return TEXT("CurveCustomTangent");
		}
		return TEXT("Unknown");
	}

	bool ReadVector(const TSharedPtr<FJsonObject>& Obj, const FString& Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Obj->TryGetArrayField(Field, Arr) || !Arr || Arr->Num() < 3) return false;
		Out.X = (*Arr)[0]->AsNumber();
		Out.Y = (*Arr)[1]->AsNumber();
		Out.Z = (*Arr)[2]->AsNumber();
		return true;
	}

	bool ReadRotator(const TSharedPtr<FJsonObject>& Obj, const FString& Field, FRotator& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Obj->TryGetArrayField(Field, Arr) || !Arr || Arr->Num() < 3) return false;
		// Convention: [Pitch, Yaw, Roll] — same order as FRotator constructor
		Out.Pitch = (*Arr)[0]->AsNumber();
		Out.Yaw = (*Arr)[1]->AsNumber();
		Out.Roll = (*Arr)[2]->AsNumber();
		return true;
	}

	TSharedPtr<FJsonValue> VectorToJson(const FVector& V)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueNumber>(V.X));
		Arr.Add(MakeShared<FJsonValueNumber>(V.Y));
		Arr.Add(MakeShared<FJsonValueNumber>(V.Z));
		return MakeShared<FJsonValueArray>(Arr);
	}

	TSharedPtr<FJsonValue> RotatorToJson(const FRotator& R)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueNumber>(R.Pitch));
		Arr.Add(MakeShared<FJsonValueNumber>(R.Yaw));
		Arr.Add(MakeShared<FJsonValueNumber>(R.Roll));
		return MakeShared<FJsonValueArray>(Arr);
	}
}

FUnrealMCPSplineCommands::FUnrealMCPSplineCommands() = default;

TSharedPtr<FJsonObject> FUnrealMCPSplineCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_spline_info")) return HandleGetSplineInfo(Params);
	if (CommandType == TEXT("set_spline_points")) return HandleSetSplinePoints(Params);
	if (CommandType == TEXT("set_spline_point")) return HandleSetSplinePoint(Params);
	if (CommandType == TEXT("clear_spline_points")) return HandleClearSplinePoints(Params);
	return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Unknown spline command: %s"), *CommandType));
}

// ---------------------------------------------------------------------------
// get_spline_info
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPSplineCommands::HandleGetSplineInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
	}

	FString ComponentName;
	if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
	}

	UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
	if (!Blueprint)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
	}

	FString Source;
	USplineComponent* Spline = FindSplineComponentTemplate(Blueprint, ComponentName, Source);
	if (!Spline)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("SplineComponent not found: %s"), *ComponentName));
	}

	const ESplineCoordinateSpace::Type CoordSpace = ParseCoordinateSpace(Params, ESplineCoordinateSpace::Local);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("component_name"), ComponentName);
	Result->SetStringField(TEXT("source"), Source);
	Result->SetStringField(TEXT("coordinate_space"), CoordSpace == ESplineCoordinateSpace::World ? TEXT("World") : TEXT("Local"));

	const int32 NumPoints = Spline->GetNumberOfSplinePoints();
	Result->SetNumberField(TEXT("num_points"), NumPoints);
	Result->SetBoolField(TEXT("closed_loop"), Spline->IsClosedLoop());

	TArray<TSharedPtr<FJsonValue>> Points;
	for (int32 i = 0; i < NumPoints; ++i)
	{
		TSharedPtr<FJsonObject> Pt = MakeShared<FJsonObject>();
		Pt->SetNumberField(TEXT("index"), i);
		Pt->SetField(TEXT("location"), VectorToJson(Spline->GetLocationAtSplinePoint(i, CoordSpace)));
		Pt->SetField(TEXT("arrive_tangent"), VectorToJson(Spline->GetArriveTangentAtSplinePoint(i, CoordSpace)));
		Pt->SetField(TEXT("leave_tangent"), VectorToJson(Spline->GetLeaveTangentAtSplinePoint(i, CoordSpace)));
		Pt->SetField(TEXT("rotation"), RotatorToJson(Spline->GetRotationAtSplinePoint(i, CoordSpace)));
		Pt->SetField(TEXT("scale"), VectorToJson(Spline->GetScaleAtSplinePoint(i)));
		Pt->SetStringField(TEXT("type"), PointTypeToString(Spline->GetSplinePointType(i)));
		Points.Add(MakeShared<FJsonValueObject>(Pt));
	}
	Result->SetArrayField(TEXT("points"), Points);

	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// ---------------------------------------------------------------------------
// set_spline_points (full replace: clear + AddN)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPSplineCommands::HandleSetSplinePoints(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
	}

	FString ComponentName;
	if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
	}

	const TArray<TSharedPtr<FJsonValue>>* PointsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("points"), PointsArr) || !PointsArr)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'points' array parameter"));
	}

	UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
	if (!Blueprint)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
	}

	FString Source;
	USplineComponent* Spline = FindSplineComponentTemplate(Blueprint, ComponentName, Source);
	if (!Spline)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("SplineComponent not found: %s"), *ComponentName));
	}

	const ESplineCoordinateSpace::Type CoordSpace = ParseCoordinateSpace(Params, ESplineCoordinateSpace::Local);

	// Optional closed_loop override
	bool bClosedLoop = Spline->IsClosedLoop();
	bool bClosedLoopSpecified = Params->TryGetBoolField(TEXT("closed_loop"), bClosedLoop);

	Spline->ClearSplinePoints(false);

	for (int32 i = 0; i < PointsArr->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* PtObj = nullptr;
		if (!(*PointsArr)[i]->TryGetObject(PtObj) || !PtObj) continue;

		FVector Location(0);
		if (!ReadVector(*PtObj, TEXT("location"), Location))
		{
			// Allow points to be a bare [x,y,z] array entry (less typing)
			const TArray<TSharedPtr<FJsonValue>>* MaybeBare = nullptr;
			// (Skip — point objects must have explicit 'location'. Bare-array fallback is intentionally not supported.)
		}

		Spline->AddSplinePoint(Location, CoordSpace, /*bUpdateSpline*/ false);

		const int32 NewIndex = Spline->GetNumberOfSplinePoints() - 1;

		FString TypeStr;
		if ((*PtObj)->TryGetStringField(TEXT("type"), TypeStr))
		{
			Spline->SetSplinePointType(NewIndex, ParsePointType(TypeStr, ESplinePointType::Curve), false);
		}

		FVector ArriveTangent, LeaveTangent;
		const bool bHasArrive = ReadVector(*PtObj, TEXT("arrive_tangent"), ArriveTangent);
		const bool bHasLeave = ReadVector(*PtObj, TEXT("leave_tangent"), LeaveTangent);
		if (bHasArrive && bHasLeave)
		{
			Spline->SetTangentsAtSplinePoint(NewIndex, ArriveTangent, LeaveTangent, CoordSpace, false);
		}
		else if (bHasArrive)
		{
			Spline->SetTangentAtSplinePoint(NewIndex, ArriveTangent, CoordSpace, false);
		}

		FRotator Rotation;
		if (ReadRotator(*PtObj, TEXT("rotation"), Rotation))
		{
			Spline->SetRotationAtSplinePoint(NewIndex, Rotation, CoordSpace, false);
		}

		FVector Scale;
		if (ReadVector(*PtObj, TEXT("scale"), Scale))
		{
			Spline->SetScaleAtSplinePoint(NewIndex, Scale, false);
		}
	}

	if (bClosedLoopSpecified)
	{
		Spline->SetClosedLoop(bClosedLoop, false);
	}

	Spline->UpdateSpline();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("component_name"), ComponentName);
	Result->SetNumberField(TEXT("num_points"), Spline->GetNumberOfSplinePoints());
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// ---------------------------------------------------------------------------
// set_spline_point (in-place modify single point; only specified fields change)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPSplineCommands::HandleSetSplinePoint(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
	}

	FString ComponentName;
	if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
	}

	int32 Index = -1;
	if (!Params->TryGetNumberField(TEXT("index"), Index) || Index < 0)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing or invalid 'index' parameter"));
	}

	UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
	if (!Blueprint)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
	}

	FString Source;
	USplineComponent* Spline = FindSplineComponentTemplate(Blueprint, ComponentName, Source);
	if (!Spline)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("SplineComponent not found: %s"), *ComponentName));
	}

	if (Index >= Spline->GetNumberOfSplinePoints())
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Index %d out of range (num_points=%d)"), Index, Spline->GetNumberOfSplinePoints()));
	}

	const ESplineCoordinateSpace::Type CoordSpace = ParseCoordinateSpace(Params, ESplineCoordinateSpace::Local);

	FVector Location;
	if (ReadVector(Params, TEXT("location"), Location))
	{
		Spline->SetLocationAtSplinePoint(Index, Location, CoordSpace, false);
	}

	FVector ArriveTangent, LeaveTangent;
	const bool bHasArrive = ReadVector(Params, TEXT("arrive_tangent"), ArriveTangent);
	const bool bHasLeave = ReadVector(Params, TEXT("leave_tangent"), LeaveTangent);
	if (bHasArrive && bHasLeave)
	{
		Spline->SetTangentsAtSplinePoint(Index, ArriveTangent, LeaveTangent, CoordSpace, false);
	}
	else if (bHasArrive)
	{
		Spline->SetTangentAtSplinePoint(Index, ArriveTangent, CoordSpace, false);
	}
	else if (bHasLeave)
	{
		// Only leave tangent — mirror to arrive to keep them symmetric (UE single-set behavior)
		Spline->SetTangentAtSplinePoint(Index, LeaveTangent, CoordSpace, false);
	}

	FRotator Rotation;
	if (ReadRotator(Params, TEXT("rotation"), Rotation))
	{
		Spline->SetRotationAtSplinePoint(Index, Rotation, CoordSpace, false);
	}

	FVector Scale;
	if (ReadVector(Params, TEXT("scale"), Scale))
	{
		Spline->SetScaleAtSplinePoint(Index, Scale, false);
	}

	FString TypeStr;
	if (Params->TryGetStringField(TEXT("type"), TypeStr))
	{
		Spline->SetSplinePointType(Index, ParsePointType(TypeStr, ESplinePointType::Curve), false);
	}

	Spline->UpdateSpline();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("component_name"), ComponentName);
	Result->SetNumberField(TEXT("index"), Index);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

// ---------------------------------------------------------------------------
// clear_spline_points
// ---------------------------------------------------------------------------
TSharedPtr<FJsonObject> FUnrealMCPSplineCommands::HandleClearSplinePoints(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path' parameter"));
	}

	FString ComponentName;
	if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'component_name' parameter"));
	}

	UBlueprint* Blueprint = FUnrealMCPCommonUtils::FindBlueprintByPath(BlueprintPath);
	if (!Blueprint)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
	}

	FString Source;
	USplineComponent* Spline = FindSplineComponentTemplate(Blueprint, ComponentName, Source);
	if (!Spline)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(TEXT("SplineComponent not found: %s"), *ComponentName));
	}

	Spline->ClearSplinePoints(true);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint_path"), Blueprint->GetPathName());
	Result->SetStringField(TEXT("component_name"), ComponentName);
	Result->SetNumberField(TEXT("num_points"), 0);
	return FUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}
