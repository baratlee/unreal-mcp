#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class UStateTree;
class UStateTreeState;
class UStateTreeEditorData;
struct FStateTreeEditorNode;
struct FInstancedStruct;

class UNREALMCP_API FUnrealMCPStateTreeCommands
{
public:
	FUnrealMCPStateTreeCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Create
	TSharedPtr<FJsonObject> HandleCreateStateTree(const TSharedPtr<FJsonObject>& Params);

	// Read
	TSharedPtr<FJsonObject> HandleGetStateTreeInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetStateTreeNodeProperties(const TSharedPtr<FJsonObject>& Params);

	// Write - State manipulation
	TSharedPtr<FJsonObject> HandleAddStateTreeState(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveStateTreeState(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetStateTreeStateProperty(const TSharedPtr<FJsonObject>& Params);

	// Write - Node manipulation
	TSharedPtr<FJsonObject> HandleAddStateTreeTask(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddStateTreeTransition(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetStateTreeNodeProperty(const TSharedPtr<FJsonObject>& Params);

	// Compile
	TSharedPtr<FJsonObject> HandleCompileStateTree(const TSharedPtr<FJsonObject>& Params);

	// Helpers
	UStateTree* LoadStateTreeAsset(const FString& AssetPath);
	UStateTreeEditorData* GetEditorData(UStateTree* StateTree);
	UStateTreeState* FindStateByName(UStateTreeEditorData* EditorData, const FString& StateName);
	UStateTreeState* FindStateByNameRecursive(UStateTreeState* State, const FName& TargetName);
	FStateTreeEditorNode* FindNodeByID(UStateTreeEditorData* EditorData, const FGuid& NodeID);
	FStateTreeEditorNode* FindNodeByIDInState(UStateTreeState* State, const FGuid& NodeID);
	TSharedPtr<FJsonObject> StateToJson(UStateTreeState* State, bool bRecursive = true);
	TSharedPtr<FJsonObject> EditorNodeToJson(const FStateTreeEditorNode& Node);
	TSharedPtr<FJsonObject> TransitionToJson(const struct FStateTreeTransition& Transition);
	TSharedPtr<FJsonObject> SerializeInstancedStruct(const FInstancedStruct& Struct);
	bool SetInstancedStructProperty(FInstancedStruct& Struct, const FString& PropertyName,
		const TSharedPtr<FJsonValue>& Value, FString& OutError);
};
