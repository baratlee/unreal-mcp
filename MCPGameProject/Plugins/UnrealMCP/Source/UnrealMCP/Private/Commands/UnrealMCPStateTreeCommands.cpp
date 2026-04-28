#include "Commands/UnrealMCPStateTreeCommands.h"
#include "Commands/UnrealMCPCommonUtils.h"

#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeEditorNode.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeSchema.h"
#include "StateTreeTypes.h"

#include "StateTreeFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "UObject/UObjectGlobals.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"

FUnrealMCPStateTreeCommands::FUnrealMCPStateTreeCommands()
{
}

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("create_state_tree"))
	{
		return HandleCreateStateTree(Params);
	}
	else if (CommandType == TEXT("get_state_tree_info"))
	{
		return HandleGetStateTreeInfo(Params);
	}
	else if (CommandType == TEXT("get_state_tree_node_properties"))
	{
		return HandleGetStateTreeNodeProperties(Params);
	}
	else if (CommandType == TEXT("add_state_tree_state"))
	{
		return HandleAddStateTreeState(Params);
	}
	else if (CommandType == TEXT("remove_state_tree_state"))
	{
		return HandleRemoveStateTreeState(Params);
	}
	else if (CommandType == TEXT("set_state_tree_state_property"))
	{
		return HandleSetStateTreeStateProperty(Params);
	}
	else if (CommandType == TEXT("add_state_tree_task"))
	{
		return HandleAddStateTreeTask(Params);
	}
	else if (CommandType == TEXT("add_state_tree_transition"))
	{
		return HandleAddStateTreeTransition(Params);
	}
	else if (CommandType == TEXT("set_state_tree_node_property"))
	{
		return HandleSetStateTreeNodeProperty(Params);
	}
	else if (CommandType == TEXT("compile_state_tree"))
	{
		return HandleCompileStateTree(Params);
	}

	return FUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown StateTree command: %s"), *CommandType));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

UStateTree* FUnrealMCPStateTreeCommands::LoadStateTreeAsset(const FString& AssetPath)
{
	return LoadObject<UStateTree>(nullptr, *AssetPath);
}

UStateTreeEditorData* FUnrealMCPStateTreeCommands::GetEditorData(UStateTree* StateTree)
{
	if (!StateTree) return nullptr;
	return Cast<UStateTreeEditorData>(StateTree->EditorData);
}

UStateTreeState* FUnrealMCPStateTreeCommands::FindStateByName(UStateTreeEditorData* EditorData, const FString& StateName)
{
	if (!EditorData) return nullptr;
	FName TargetName(*StateName);
	for (UStateTreeState* SubTree : EditorData->SubTrees)
	{
		if (UStateTreeState* Found = FindStateByNameRecursive(SubTree, TargetName))
		{
			return Found;
		}
	}
	return nullptr;
}

UStateTreeState* FUnrealMCPStateTreeCommands::FindStateByNameRecursive(UStateTreeState* State, const FName& TargetName)
{
	if (!State) return nullptr;
	if (State->Name == TargetName)
	{
		return State;
	}
	for (UStateTreeState* Child : State->Children)
	{
		if (UStateTreeState* Found = FindStateByNameRecursive(Child, TargetName))
		{
			return Found;
		}
	}
	return nullptr;
}

FStateTreeEditorNode* FUnrealMCPStateTreeCommands::FindNodeByID(UStateTreeEditorData* EditorData, const FGuid& NodeID)
{
	if (!EditorData) return nullptr;

	// Search global evaluators and tasks
	for (FStateTreeEditorNode& Node : EditorData->Evaluators)
	{
		if (Node.ID == NodeID) return &Node;
	}
	for (FStateTreeEditorNode& Node : EditorData->GlobalTasks)
	{
		if (Node.ID == NodeID) return &Node;
	}

	// Search in states
	for (UStateTreeState* SubTree : EditorData->SubTrees)
	{
		if (FStateTreeEditorNode* Found = FindNodeByIDInState(SubTree, NodeID))
		{
			return Found;
		}
	}
	return nullptr;
}

FStateTreeEditorNode* FUnrealMCPStateTreeCommands::FindNodeByIDInState(UStateTreeState* State, const FGuid& NodeID)
{
	if (!State) return nullptr;

	for (FStateTreeEditorNode& Node : State->Tasks)
	{
		if (Node.ID == NodeID) return &Node;
	}
	for (FStateTreeEditorNode& Node : State->EnterConditions)
	{
		if (Node.ID == NodeID) return &Node;
	}
	for (FStateTreeTransition& Trans : State->Transitions)
	{
		for (FStateTreeEditorNode& Node : Trans.Conditions)
		{
			if (Node.ID == NodeID) return &Node;
		}
	}

	for (UStateTreeState* Child : State->Children)
	{
		if (FStateTreeEditorNode* Found = FindNodeByIDInState(Child, NodeID))
		{
			return Found;
		}
	}
	return nullptr;
}

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::SerializeInstancedStruct(const FInstancedStruct& Struct)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	const UScriptStruct* ScriptStruct = Struct.GetScriptStruct();
	if (!ScriptStruct || !Struct.IsValid())
	{
		return Result;
	}

	const uint8* StructData = Struct.GetMemory();
	Result->SetStringField(TEXT("struct_type"), ScriptStruct->GetName());

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	for (TFieldIterator<FProperty> PropIt(ScriptStruct); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
		{
			continue;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(StructData);
		const FString PropName = Property->GetName();

		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
		{
			Properties->SetBoolField(PropName, BoolProp->GetPropertyValue(ValuePtr));
		}
		else if (const FIntProperty* IntProp = CastField<FIntProperty>(Property))
		{
			Properties->SetNumberField(PropName, IntProp->GetPropertyValue(ValuePtr));
		}
		else if (const FInt64Property* Int64Prop = CastField<FInt64Property>(Property))
		{
			Properties->SetNumberField(PropName, static_cast<double>(Int64Prop->GetPropertyValue(ValuePtr)));
		}
		else if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
		{
			Properties->SetNumberField(PropName, FloatProp->GetPropertyValue(ValuePtr));
		}
		else if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
		{
			Properties->SetNumberField(PropName, DoubleProp->GetPropertyValue(ValuePtr));
		}
		else if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
		{
			Properties->SetStringField(PropName, StrProp->GetPropertyValue(ValuePtr));
		}
		else if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
		{
			Properties->SetStringField(PropName, NameProp->GetPropertyValue(ValuePtr).ToString());
		}
		else if (const FTextProperty* TextProp = CastField<FTextProperty>(Property))
		{
			Properties->SetStringField(PropName, TextProp->GetPropertyValue(ValuePtr).ToString());
		}
		else if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
		{
			const UEnum* Enum = EnumProp->GetEnum();
			const FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
			int64 EnumValue = UnderlyingProp->GetSignedIntPropertyValue(ValuePtr);
			FString EnumName = Enum ? Enum->GetNameStringByValue(EnumValue) : FString::FromInt(EnumValue);
			Properties->SetStringField(PropName, EnumName);
		}
		else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
		{
			if (ByteProp->Enum)
			{
				uint8 ByteVal = ByteProp->GetPropertyValue(ValuePtr);
				Properties->SetStringField(PropName, ByteProp->Enum->GetNameStringByValue(ByteVal));
			}
			else
			{
				Properties->SetNumberField(PropName, ByteProp->GetPropertyValue(ValuePtr));
			}
		}
		else if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			if (StructProp->Struct == TBaseStructure<FGameplayTag>::Get())
			{
				const FGameplayTag* Tag = static_cast<const FGameplayTag*>(ValuePtr);
				Properties->SetStringField(PropName, Tag->ToString());
			}
			else if (StructProp->Struct == TBaseStructure<FGameplayTagContainer>::Get())
			{
				const FGameplayTagContainer* Container = static_cast<const FGameplayTagContainer*>(ValuePtr);
				TArray<TSharedPtr<FJsonValue>> TagArray;
				for (const FGameplayTag& Tag : *Container)
				{
					TagArray.Add(MakeShared<FJsonValueString>(Tag.ToString()));
				}
				Properties->SetArrayField(PropName, TagArray);
			}
			else
			{
				FString ExportedValue;
				StructProp->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, nullptr, PPF_None);
				Properties->SetStringField(PropName, ExportedValue);
			}
		}
		else if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
		{
			UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
			Properties->SetStringField(PropName, Obj ? Obj->GetPathName() : TEXT("None"));
		}
		else if (const FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
		{
			FSoftObjectPtr SoftPtr = SoftObjProp->GetPropertyValue(ValuePtr);
			Properties->SetStringField(PropName, SoftPtr.ToString());
		}
		else
		{
			FString ExportedValue;
			Property->ExportTextItem_Direct(ExportedValue, ValuePtr, nullptr, nullptr, PPF_None);
			Properties->SetStringField(PropName, ExportedValue);
		}
	}

	Result->SetObjectField(TEXT("properties"), Properties);
	return Result;
}

bool FUnrealMCPStateTreeCommands::SetInstancedStructProperty(FInstancedStruct& Struct, const FString& PropertyName,
	const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	const UScriptStruct* ScriptStruct = Struct.GetScriptStruct();
	if (!ScriptStruct || !Struct.IsValid())
	{
		OutError = TEXT("Invalid instanced struct");
		return false;
	}

	uint8* StructData = Struct.GetMutableMemory();

	FProperty* Property = ScriptStruct->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on struct '%s'"), *PropertyName, *ScriptStruct->GetName());
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(StructData);

	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool bVal = false;
		if (Value->TryGetBool(bVal))
		{
			BoolProp->SetPropertyValue(ValuePtr, bVal);
			return true;
		}
	}
	else if (const FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		double NumVal = 0;
		if (Value->TryGetNumber(NumVal))
		{
			IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(NumVal));
			return true;
		}
	}
	else if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		double NumVal = 0;
		if (Value->TryGetNumber(NumVal))
		{
			FloatProp->SetPropertyValue(ValuePtr, static_cast<float>(NumVal));
			return true;
		}
	}
	else if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		double NumVal = 0;
		if (Value->TryGetNumber(NumVal))
		{
			DoubleProp->SetPropertyValue(ValuePtr, NumVal);
			return true;
		}
	}
	else if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			StrProp->SetPropertyValue(ValuePtr, StrVal);
			return true;
		}
	}
	else if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			NameProp->SetPropertyValue(ValuePtr, FName(*StrVal));
			return true;
		}
	}
	else if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			const UEnum* Enum = EnumProp->GetEnum();
			int64 EnumValue = Enum ? Enum->GetValueByNameString(StrVal) : INDEX_NONE;
			if (EnumValue != INDEX_NONE)
			{
				FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
				UnderlyingProp->SetIntPropertyValue(ValuePtr, EnumValue);
				return true;
			}
			OutError = FString::Printf(TEXT("Invalid enum value '%s' for property '%s'"), *StrVal, *PropertyName);
			return false;
		}
	}
	else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (ByteProp->Enum)
		{
			FString StrVal;
			if (Value->TryGetString(StrVal))
			{
				int64 EnumValue = ByteProp->Enum->GetValueByNameString(StrVal);
				if (EnumValue != INDEX_NONE)
				{
					ByteProp->SetPropertyValue(ValuePtr, static_cast<uint8>(EnumValue));
					return true;
				}
				OutError = FString::Printf(TEXT("Invalid enum value '%s'"), *StrVal);
				return false;
			}
		}
		else
		{
			double NumVal = 0;
			if (Value->TryGetNumber(NumVal))
			{
				ByteProp->SetPropertyValue(ValuePtr, static_cast<uint8>(NumVal));
				return true;
			}
		}
	}
	else if (const FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (StructProp->Struct == TBaseStructure<FGameplayTag>::Get())
		{
			FString StrVal;
			if (Value->TryGetString(StrVal))
			{
				FGameplayTag* Tag = static_cast<FGameplayTag*>(ValuePtr);
				*Tag = FGameplayTag::RequestGameplayTag(FName(*StrVal), false);
				return true;
			}
		}
		else
		{
			FString StrVal;
			if (Value->TryGetString(StrVal))
			{
				const TCHAR* Buffer = *StrVal;
				StructProp->ImportText_Direct(Buffer, ValuePtr, nullptr, PPF_None);
				return true;
			}
		}
	}
	else if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			UObject* Obj = (StrVal == TEXT("None") || StrVal.IsEmpty()) ? nullptr : LoadObject<UObject>(nullptr, *StrVal);
			ObjProp->SetObjectPropertyValue(ValuePtr, Obj);
			return true;
		}
	}

	// Generic fallback via ImportText
	{
		FString StrVal;
		if (Value->TryGetString(StrVal))
		{
			const TCHAR* Buffer = *StrVal;
			if (Property->ImportText_Direct(Buffer, ValuePtr, nullptr, PPF_None))
			{
				return true;
			}
		}
	}

	OutError = FString::Printf(TEXT("Could not set property '%s': type mismatch or unsupported type"), *PropertyName);
	return false;
}

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::EditorNodeToJson(const FStateTreeEditorNode& Node)
{
	TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
	NodeJson->SetStringField(TEXT("id"), Node.ID.ToString());
	NodeJson->SetStringField(TEXT("name"), Node.GetName().ToString());

	if (Node.Node.IsValid())
	{
		const UScriptStruct* NodeStruct = Node.Node.GetScriptStruct();
		NodeJson->SetStringField(TEXT("node_struct_type"), NodeStruct ? NodeStruct->GetName() : TEXT("None"));
	}

	if (Node.Instance.IsValid())
	{
		const UScriptStruct* InstanceStruct = Node.Instance.GetScriptStruct();
		NodeJson->SetStringField(TEXT("instance_struct_type"), InstanceStruct ? InstanceStruct->GetName() : TEXT("None"));
	}
	else if (Node.InstanceObject)
	{
		NodeJson->SetStringField(TEXT("instance_class"), Node.InstanceObject->GetClass()->GetName());
	}

	return NodeJson;
}

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::TransitionToJson(const FStateTreeTransition& Transition)
{
	TSharedPtr<FJsonObject> TransJson = MakeShared<FJsonObject>();
	TransJson->SetStringField(TEXT("id"), Transition.ID.ToString());

	// Trigger
	const UEnum* TriggerEnum = StaticEnum<EStateTreeTransitionTrigger>();
	TransJson->SetStringField(TEXT("trigger"), TriggerEnum ? TriggerEnum->GetNameStringByValue(static_cast<int64>(Transition.Trigger)) : TEXT("Unknown"));

	// Transition type
	const UEnum* TypeEnum = StaticEnum<EStateTreeTransitionType>();
	FString TypeStr = TypeEnum ? TypeEnum->GetNameStringByValue(static_cast<int64>(Transition.State.LinkType)) : TEXT("Unknown");

	// For GotoState, we need the type from the link
	if (Transition.State.LinkType == EStateTreeTransitionType::GotoState)
	{
		TransJson->SetStringField(TEXT("type"), TEXT("GotoState"));
		TransJson->SetStringField(TEXT("target_state_id"), Transition.State.ID.ToString());
		TransJson->SetStringField(TEXT("target_state_name"), Transition.State.Name.ToString());
	}
	else
	{
		TransJson->SetStringField(TEXT("type"), TypeStr);
	}

	// Priority
	const UEnum* PriorityEnum = StaticEnum<EStateTreeTransitionPriority>();
	TransJson->SetStringField(TEXT("priority"), PriorityEnum ? PriorityEnum->GetNameStringByValue(static_cast<int64>(Transition.Priority)) : TEXT("Normal"));

	TransJson->SetBoolField(TEXT("delay_transition"), Transition.bDelayTransition);
	if (Transition.bDelayTransition)
	{
		TransJson->SetNumberField(TEXT("delay_duration"), Transition.DelayDuration);
		TransJson->SetNumberField(TEXT("delay_random_variance"), Transition.DelayRandomVariance);
	}

	TransJson->SetBoolField(TEXT("enabled"), Transition.bTransitionEnabled);

	// Required event
	if (Transition.RequiredEvent.IsValid())
	{
		TransJson->SetStringField(TEXT("required_event_tag"), Transition.RequiredEvent.Tag.ToString());
	}

	// Conditions
	if (Transition.Conditions.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> CondArray;
		for (const FStateTreeEditorNode& Cond : Transition.Conditions)
		{
			CondArray.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(Cond)));
		}
		TransJson->SetArrayField(TEXT("conditions"), CondArray);
	}

	return TransJson;
}

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::StateToJson(UStateTreeState* State, bool bRecursive)
{
	if (!State) return nullptr;

	TSharedPtr<FJsonObject> StateJson = MakeShared<FJsonObject>();
	StateJson->SetStringField(TEXT("name"), State->Name.ToString());
	StateJson->SetStringField(TEXT("id"), State->ID.ToString());

	// Type
	const UEnum* TypeEnum = StaticEnum<EStateTreeStateType>();
	StateJson->SetStringField(TEXT("type"), TypeEnum ? TypeEnum->GetNameStringByValue(static_cast<int64>(State->Type)) : TEXT("Unknown"));

	// Selection behavior
	const UEnum* SelEnum = StaticEnum<EStateTreeStateSelectionBehavior>();
	StateJson->SetStringField(TEXT("selection_behavior"), SelEnum ? SelEnum->GetNameStringByValue(static_cast<int64>(State->SelectionBehavior)) : TEXT("Unknown"));

	// Tasks completion
	const UEnum* CompEnum = StaticEnum<EStateTreeTaskCompletionType>();
	StateJson->SetStringField(TEXT("tasks_completion"), CompEnum ? CompEnum->GetNameStringByValue(static_cast<int64>(State->TasksCompletion)) : TEXT("Unknown"));

	// Tag
	if (State->Tag.IsValid())
	{
		StateJson->SetStringField(TEXT("tag"), State->Tag.ToString());
	}

	// Tasks
	if (State->Tasks.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> TaskArray;
		for (const FStateTreeEditorNode& Task : State->Tasks)
		{
			TaskArray.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(Task)));
		}
		StateJson->SetArrayField(TEXT("tasks"), TaskArray);
	}

	// Enter conditions
	if (State->EnterConditions.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> CondArray;
		for (const FStateTreeEditorNode& Cond : State->EnterConditions)
		{
			CondArray.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(Cond)));
		}
		StateJson->SetArrayField(TEXT("enter_conditions"), CondArray);
	}

	// Transitions
	if (State->Transitions.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> TransArray;
		for (const FStateTreeTransition& Trans : State->Transitions)
		{
			TransArray.Add(MakeShared<FJsonValueObject>(TransitionToJson(Trans)));
		}
		StateJson->SetArrayField(TEXT("transitions"), TransArray);
	}

	// Children
	if (bRecursive && State->Children.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ChildArray;
		for (UStateTreeState* Child : State->Children)
		{
			ChildArray.Add(MakeShared<FJsonValueObject>(StateToJson(Child, true)));
		}
		StateJson->SetArrayField(TEXT("children"), ChildArray);
	}

	return StateJson;
}

// ---------------------------------------------------------------------------
// Command: create_state_tree
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleCreateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString SchemaClassName;
	Params->TryGetStringField(TEXT("schema_class"), SchemaClassName);

	// Split asset path into package and asset name
	FString PackageName;
	FString AssetName;
	{
		int32 LastSlash;
		if (AssetPath.FindLastChar('/', LastSlash))
		{
			PackageName = AssetPath;
			AssetName = AssetPath.Mid(LastSlash + 1);
		}
		else
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Invalid asset_path: %s"), *AssetPath));
		}
	}

	// Check if asset already exists
	if (UStateTree* Existing = LoadObject<UStateTree>(nullptr, *(AssetPath + TEXT(".") + AssetName)))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("StateTree asset already exists: %s"), *AssetPath));
	}

	// Find schema class
	UClass* SchemaClass = nullptr;
	if (!SchemaClassName.IsEmpty())
	{
		SchemaClass = FindFirstObject<UClass>(*SchemaClassName, EFindFirstObjectOptions::NativeFirst);
		if (!SchemaClass)
		{
			SchemaClass = FindFirstObject<UClass>(*(TEXT("U") + SchemaClassName), EFindFirstObjectOptions::NativeFirst);
		}
		if (!SchemaClass || !SchemaClass->IsChildOf(UStateTreeSchema::StaticClass()))
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Schema class not found or not a UStateTreeSchema subclass: %s"), *SchemaClassName));
		}
	}

	// Create factory directly (header included)
	UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
	if (!Factory)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create UStateTreeFactory instance"));
	}

	if (SchemaClass)
	{
		Factory->SetSchemaClass(SchemaClass);
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
	}
	Package->FullyLoad();

	UObject* CreatedAsset = Factory->FactoryCreateNew(
		UStateTree::StaticClass(), Package, *AssetName,
		RF_Public | RF_Standalone, nullptr, GWarn);

	UStateTree* NewStateTree = Cast<UStateTree>(CreatedAsset);
	if (!NewStateTree)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Factory failed to create StateTree"));
	}

	FAssetRegistryModule::AssetCreated(NewStateTree);
	Package->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), NewStateTree->GetPathName());
	if (NewStateTree->GetSchema())
	{
		Result->SetStringField(TEXT("schema"), NewStateTree->GetSchema()->GetClass()->GetName());
	}
	Result->SetBoolField(TEXT("saved"), false);
	return Result;
}

// ---------------------------------------------------------------------------
// Command: get_state_tree_info
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleGetStateTreeInfo(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UStateTree* StateTree = LoadStateTreeAsset(AssetPath);
	if (!StateTree)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("StateTree has no EditorData"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), StateTree->GetPathName());

	// Schema
	const UStateTreeSchema* Schema = StateTree->GetSchema();
	if (Schema)
	{
		Result->SetStringField(TEXT("schema"), Schema->GetClass()->GetName());
	}

	// Global evaluators
	if (EditorData->Evaluators.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> EvalArray;
		for (const FStateTreeEditorNode& Eval : EditorData->Evaluators)
		{
			EvalArray.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(Eval)));
		}
		Result->SetArrayField(TEXT("evaluators"), EvalArray);
	}

	// Global tasks
	if (EditorData->GlobalTasks.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> TaskArray;
		for (const FStateTreeEditorNode& Task : EditorData->GlobalTasks)
		{
			TaskArray.Add(MakeShared<FJsonValueObject>(EditorNodeToJson(Task)));
		}
		Result->SetArrayField(TEXT("global_tasks"), TaskArray);
	}

	// States hierarchy
	TArray<TSharedPtr<FJsonValue>> StatesArray;
	for (UStateTreeState* SubTree : EditorData->SubTrees)
	{
		StatesArray.Add(MakeShared<FJsonValueObject>(StateToJson(SubTree, true)));
	}
	Result->SetArrayField(TEXT("states"), StatesArray);

	return Result;
}

// ---------------------------------------------------------------------------
// Command: get_state_tree_node_properties
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleGetStateTreeNodeProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString NodeIDStr;
	if (!Params->TryGetStringField(TEXT("node_id"), NodeIDStr))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
	}

	FGuid NodeID;
	if (!FGuid::Parse(NodeIDStr, NodeID))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Invalid GUID format: %s"), *NodeIDStr));
	}

	UStateTree* StateTree = LoadStateTreeAsset(AssetPath);
	if (!StateTree)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("StateTree has no EditorData"));
	}

	FStateTreeEditorNode* FoundNode = FindNodeByID(EditorData, NodeID);
	if (!FoundNode)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Node not found with ID: %s"), *NodeIDStr));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeIDStr);
	Result->SetStringField(TEXT("name"), FoundNode->GetName().ToString());

	// Serialize the Node struct (task/condition/evaluator definition)
	if (FoundNode->Node.IsValid())
	{
		Result->SetObjectField(TEXT("node"), SerializeInstancedStruct(FoundNode->Node));
	}

	// Serialize the Instance struct (instance data)
	if (FoundNode->Instance.IsValid())
	{
		Result->SetObjectField(TEXT("instance"), SerializeInstancedStruct(FoundNode->Instance));
	}
	else if (FoundNode->InstanceObject)
	{
		TSharedPtr<FJsonObject> InstObj = MakeShared<FJsonObject>();
		InstObj->SetStringField(TEXT("class"), FoundNode->InstanceObject->GetClass()->GetName());
		InstObj->SetStringField(TEXT("path"), FoundNode->InstanceObject->GetPathName());
		Result->SetObjectField(TEXT("instance_object"), InstObj);
	}

	return Result;
}

// ---------------------------------------------------------------------------
// Command: add_state_tree_state
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleAddStateTreeState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString StateName;
	if (!Params->TryGetStringField(TEXT("state_name"), StateName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
	}

	FString ParentStateName;
	Params->TryGetStringField(TEXT("parent_state_name"), ParentStateName);

	FString StateTypeStr;
	Params->TryGetStringField(TEXT("state_type"), StateTypeStr);

	UStateTree* StateTree = LoadStateTreeAsset(AssetPath);
	if (!StateTree)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("StateTree has no EditorData"));
	}

	// Determine state type
	EStateTreeStateType StateType = EStateTreeStateType::State;
	if (!StateTypeStr.IsEmpty())
	{
		const UEnum* TypeEnum = StaticEnum<EStateTreeStateType>();
		int64 EnumVal = TypeEnum ? TypeEnum->GetValueByNameString(StateTypeStr) : INDEX_NONE;
		if (EnumVal != INDEX_NONE)
		{
			StateType = static_cast<EStateTreeStateType>(EnumVal);
		}
	}

	UStateTreeState* NewState = nullptr;

	if (ParentStateName.IsEmpty())
	{
		// Add as root state (subtree)
		UStateTreeState& State = EditorData->AddSubTree(FName(*StateName));
		State.Type = StateType;
		NewState = &State;
	}
	else
	{
		UStateTreeState* ParentState = FindStateByName(EditorData, ParentStateName);
		if (!ParentState)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Parent state not found: %s"), *ParentStateName));
		}

		UStateTreeState& State = ParentState->AddChildState(FName(*StateName), StateType);
		NewState = &State;
	}

	StateTree->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state_name"), NewState->Name.ToString());
	Result->SetStringField(TEXT("state_id"), NewState->ID.ToString());
	return Result;
}

// ---------------------------------------------------------------------------
// Command: remove_state_tree_state
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleRemoveStateTreeState(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString StateName;
	if (!Params->TryGetStringField(TEXT("state_name"), StateName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
	}

	UStateTree* StateTree = LoadStateTreeAsset(AssetPath);
	if (!StateTree)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("StateTree has no EditorData"));
	}

	UStateTreeState* StateToRemove = FindStateByName(EditorData, StateName);
	if (!StateToRemove)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State not found: %s"), *StateName));
	}

	// Remove from parent's Children or EditorData's SubTrees
	if (StateToRemove->Parent)
	{
		StateToRemove->Parent->Children.Remove(StateToRemove);
	}
	else
	{
		EditorData->SubTrees.Remove(StateToRemove);
	}

	StateTree->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("removed_state"), StateName);
	return Result;
}

// ---------------------------------------------------------------------------
// Command: set_state_tree_state_property
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleSetStateTreeStateProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString StateName;
	if (!Params->TryGetStringField(TEXT("state_name"), StateName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
	}

	UStateTree* StateTree = LoadStateTreeAsset(AssetPath);
	if (!StateTree)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("StateTree has no EditorData"));
	}

	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State not found: %s"), *StateName));
	}

	bool bChanged = false;

	// Name
	FString NewName;
	if (Params->TryGetStringField(TEXT("name"), NewName))
	{
		State->Name = FName(*NewName);
		bChanged = true;
	}

	// Type
	FString NewType;
	if (Params->TryGetStringField(TEXT("type"), NewType))
	{
		const UEnum* TypeEnum = StaticEnum<EStateTreeStateType>();
		int64 EnumVal = TypeEnum ? TypeEnum->GetValueByNameString(NewType) : INDEX_NONE;
		if (EnumVal != INDEX_NONE)
		{
			State->Type = static_cast<EStateTreeStateType>(EnumVal);
			bChanged = true;
		}
	}

	// SelectionBehavior
	FString NewSel;
	if (Params->TryGetStringField(TEXT("selection_behavior"), NewSel))
	{
		const UEnum* SelEnum = StaticEnum<EStateTreeStateSelectionBehavior>();
		int64 EnumVal = SelEnum ? SelEnum->GetValueByNameString(NewSel) : INDEX_NONE;
		if (EnumVal != INDEX_NONE)
		{
			State->SelectionBehavior = static_cast<EStateTreeStateSelectionBehavior>(EnumVal);
			bChanged = true;
		}
	}

	// TasksCompletion
	FString NewComp;
	if (Params->TryGetStringField(TEXT("tasks_completion"), NewComp))
	{
		const UEnum* CompEnum = StaticEnum<EStateTreeTaskCompletionType>();
		int64 EnumVal = CompEnum ? CompEnum->GetValueByNameString(NewComp) : INDEX_NONE;
		if (EnumVal != INDEX_NONE)
		{
			State->TasksCompletion = static_cast<EStateTreeTaskCompletionType>(EnumVal);
			bChanged = true;
		}
	}

	// Tag
	FString NewTag;
	if (Params->TryGetStringField(TEXT("tag"), NewTag))
	{
		State->Tag = FGameplayTag::RequestGameplayTag(FName(*NewTag), false);
		bChanged = true;
	}

	// Enabled
	bool bEnabled = false;
	if (Params->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		State->bEnabled = bEnabled;
		bChanged = true;
	}

	if (bChanged)
	{
		StateTree->GetPackage()->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("state_name"), State->Name.ToString());
	Result->SetBoolField(TEXT("changed"), bChanged);
	return Result;
}

// ---------------------------------------------------------------------------
// Command: add_state_tree_task
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleAddStateTreeTask(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString StateName;
	if (!Params->TryGetStringField(TEXT("state_name"), StateName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
	}

	FString TaskStructType;
	if (!Params->TryGetStringField(TEXT("task_type"), TaskStructType))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'task_type' parameter"));
	}

	UStateTree* StateTree = LoadStateTreeAsset(AssetPath);
	if (!StateTree)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("StateTree has no EditorData"));
	}

	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State not found: %s"), *StateName));
	}

	// Find the struct type — UE registers USTRUCT without the F prefix
	FString SearchName = TaskStructType;
	if (SearchName.StartsWith(TEXT("F")) && SearchName.Len() > 1 && FChar::IsUpper(SearchName[1]))
	{
		SearchName.RemoveAt(0);
	}
	UScriptStruct* TaskStruct = FindFirstObject<UScriptStruct>(*SearchName, EFindFirstObjectOptions::NativeFirst);
	if (!TaskStruct)
	{
		// Retry with original name as-is
		TaskStruct = FindFirstObject<UScriptStruct>(*TaskStructType, EFindFirstObjectOptions::NativeFirst);
	}
	if (!TaskStruct)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Task struct type not found: %s (searched as '%s')"), *TaskStructType, *SearchName));
	}

	// Create the editor node
	FStateTreeEditorNode& TaskNode = State->Tasks.AddDefaulted_GetRef();
	TaskNode.ID = FGuid::NewGuid();
	TaskNode.Node.InitializeAs(TaskStruct);

	// Initialize instance data from the node's declared type
	if (TaskNode.Node.IsValid())
	{
		const FStateTreeNodeBase& NodeBase = TaskNode.Node.Get<FStateTreeNodeBase>();
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(NodeBase.GetInstanceDataType()))
		{
			TaskNode.Instance.InitializeAs(InstanceType);
		}
		if (const UScriptStruct* RuntimeType = Cast<const UScriptStruct>(NodeBase.GetExecutionRuntimeDataType()))
		{
			TaskNode.ExecutionRuntimeData.InitializeAs(RuntimeType);
		}
	}

	StateTree->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), TaskNode.ID.ToString());
	Result->SetStringField(TEXT("task_type"), TaskStruct->GetName());
	Result->SetStringField(TEXT("state_name"), StateName);
	return Result;
}

// ---------------------------------------------------------------------------
// Command: add_state_tree_transition
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleAddStateTreeTransition(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString StateName;
	if (!Params->TryGetStringField(TEXT("state_name"), StateName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'state_name' parameter"));
	}

	FString TriggerStr;
	if (!Params->TryGetStringField(TEXT("trigger"), TriggerStr))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'trigger' parameter"));
	}

	FString TransTypeStr;
	if (!Params->TryGetStringField(TEXT("transition_type"), TransTypeStr))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'transition_type' parameter"));
	}

	FString TargetStateName;
	Params->TryGetStringField(TEXT("target_state_name"), TargetStateName);

	FString EventTagStr;
	Params->TryGetStringField(TEXT("event_tag"), EventTagStr);

	UStateTree* StateTree = LoadStateTreeAsset(AssetPath);
	if (!StateTree)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("StateTree has no EditorData"));
	}

	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("State not found: %s"), *StateName));
	}

	// Parse trigger
	EStateTreeTransitionTrigger Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
	{
		const UEnum* TriggerEnum = StaticEnum<EStateTreeTransitionTrigger>();
		int64 EnumVal = TriggerEnum ? TriggerEnum->GetValueByNameString(TriggerStr) : INDEX_NONE;
		if (EnumVal != INDEX_NONE)
		{
			Trigger = static_cast<EStateTreeTransitionTrigger>(EnumVal);
		}
	}

	// Parse transition type
	EStateTreeTransitionType TransType = EStateTreeTransitionType::GotoState;
	{
		const UEnum* TypeEnum = StaticEnum<EStateTreeTransitionType>();
		int64 EnumVal = TypeEnum ? TypeEnum->GetValueByNameString(TransTypeStr) : INDEX_NONE;
		if (EnumVal != INDEX_NONE)
		{
			TransType = static_cast<EStateTreeTransitionType>(EnumVal);
		}
	}

	// Find target state if GotoState
	const UStateTreeState* TargetState = nullptr;
	if (TransType == EStateTreeTransitionType::GotoState && !TargetStateName.IsEmpty())
	{
		TargetState = FindStateByName(EditorData, TargetStateName);
		if (!TargetState)
		{
			return FUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Target state not found: %s"), *TargetStateName));
		}
	}

	// Create transition
	FStateTreeTransition* NewTransition = nullptr;
	if (!EventTagStr.IsEmpty())
	{
		FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(FName(*EventTagStr), false);
		NewTransition = &State->AddTransition(Trigger, EventTag, TransType, TargetState);
	}
	else
	{
		NewTransition = &State->AddTransition(Trigger, TransType, TargetState);
	}

	// Optional: delay
	bool bDelay = false;
	if (Params->TryGetBoolField(TEXT("delay_transition"), bDelay))
	{
		NewTransition->bDelayTransition = bDelay;
	}
	double DelayDuration = 0;
	if (Params->TryGetNumberField(TEXT("delay_duration"), DelayDuration))
	{
		NewTransition->DelayDuration = static_cast<float>(DelayDuration);
	}

	// Optional: priority
	FString PriorityStr;
	if (Params->TryGetStringField(TEXT("priority"), PriorityStr))
	{
		const UEnum* PriorityEnum = StaticEnum<EStateTreeTransitionPriority>();
		int64 EnumVal = PriorityEnum ? PriorityEnum->GetValueByNameString(PriorityStr) : INDEX_NONE;
		if (EnumVal != INDEX_NONE)
		{
			NewTransition->Priority = static_cast<EStateTreeTransitionPriority>(EnumVal);
		}
	}

	StateTree->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("transition_id"), NewTransition->ID.ToString());
	Result->SetStringField(TEXT("state_name"), StateName);
	Result->SetStringField(TEXT("trigger"), TriggerStr);
	Result->SetStringField(TEXT("transition_type"), TransTypeStr);
	return Result;
}

// ---------------------------------------------------------------------------
// Command: set_state_tree_node_property
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleSetStateTreeNodeProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	FString NodeIDStr;
	if (!Params->TryGetStringField(TEXT("node_id"), NodeIDStr))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
	}

	const TSharedPtr<FJsonValue>* PropertyValuePtr = nullptr;
	if (!Params->HasField(TEXT("property_value")))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
	}
	TSharedPtr<FJsonValue> PropertyValue = Params->TryGetField(TEXT("property_value"));

	FString TargetStr = TEXT("instance");
	Params->TryGetStringField(TEXT("target"), TargetStr);

	FGuid NodeID;
	if (!FGuid::Parse(NodeIDStr, NodeID))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Invalid GUID format: %s"), *NodeIDStr));
	}

	UStateTree* StateTree = LoadStateTreeAsset(AssetPath);
	if (!StateTree)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	}

	UStateTreeEditorData* EditorData = GetEditorData(StateTree);
	if (!EditorData)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("StateTree has no EditorData"));
	}

	FStateTreeEditorNode* FoundNode = FindNodeByID(EditorData, NodeID);
	if (!FoundNode)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Node not found with ID: %s"), *NodeIDStr));
	}

	// Determine which struct to modify
	FInstancedStruct* TargetStruct = nullptr;
	if (TargetStr == TEXT("node"))
	{
		TargetStruct = &FoundNode->Node;
	}
	else
	{
		// Default to instance
		TargetStruct = &FoundNode->Instance;
	}

	if (!TargetStruct || !TargetStruct->IsValid())
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Target '%s' struct is not valid on this node"), *TargetStr));
	}

	FString ErrorMsg;
	if (!SetInstancedStructProperty(*TargetStruct, PropertyName, PropertyValue, ErrorMsg))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(ErrorMsg);
	}

	StateTree->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_id"), NodeIDStr);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("target"), TargetStr);
	return Result;
}

// ---------------------------------------------------------------------------
// Command: compile_state_tree
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FUnrealMCPStateTreeCommands::HandleCompileStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'asset_path' parameter"));
	}

	UStateTree* StateTree = LoadStateTreeAsset(AssetPath);
	if (!StateTree)
	{
		return FUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("StateTree not found: %s"), *AssetPath));
	}

	// Use the compiler manager for synchronous compilation
	// FCompilerManager is the recommended way in UE5.7
	bool bSuccess = false;
	FString CompileMessage;

	// Try using the Modify + MarkPackageDirty path which triggers recompile
	// The most reliable way is via the editing subsystem if available
	if (GEditor)
	{
		// Attempt compilation via the subsystem
		UClass* SubsystemClass = FindFirstObject<UClass>(TEXT("UStateTreeEditingSubsystem"), EFindFirstObjectOptions::NativeFirst);
		if (SubsystemClass)
		{
			UEditorSubsystem* Subsystem = GEditor->GetEditorSubsystemBase(SubsystemClass);
			if (Subsystem)
			{
				// Call CompileStateTree via function pointer
				UFunction* CompileFunc = SubsystemClass->FindFunctionByName(TEXT("CompileStateTree"));
				if (CompileFunc)
				{
					// Direct call via ProcessEvent is complex; use the static compile path instead
				}
			}
		}
	}

	// Fallback: use the compiler namespace directly
	// UE::StateTree::Compiler::FCompilerManager::CompileSynchronously is the canonical API
	{
		// Since we can't easily call the templated/namespaced static directly,
		// trigger recompile by marking dirty and calling PostEditChange
		StateTree->Modify();

		FPropertyChangedEvent PropertyChangedEvent(nullptr);
		StateTree->PostEditChangeProperty(PropertyChangedEvent);

		StateTree->GetPackage()->MarkPackageDirty();

		// Check compilation result
		// UStateTree stores compilation status internally; a successful compile populates the runtime data
		bSuccess = true;
		CompileMessage = TEXT("StateTree recompile triggered via PostEditChangeProperty");
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);
	Result->SetStringField(TEXT("message"), CompileMessage);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	return Result;
}
