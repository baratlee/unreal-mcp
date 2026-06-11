#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Read + write commands for UMaterial / UMaterialInstance / UMaterialParameterCollection.
 *
 * History:
 *   2026-05-22 P0: get_material_info / get_material_instance_info / get_material_parameter_collection_info
 *   2026-05-22 P1: get_material_graph
 *   2026-06-11 P0a+P0b+P1 (write):
 *     - set_material_expression_property        — reflect-write any UMaterialExpression UPROPERTY (locate by guid/name)
 *     - set_material_instance_scalar_parameter  — MI scalar override
 *     - set_material_instance_vector_parameter  — MI vector override (RGBA)
 *     - set_material_instance_texture_parameter — MI texture override (asset path)
 *     - set_material_property                   — reflect-write top-level UMaterial UPROPERTY (e.g. BlendMode, TwoSided)
 */
class UNREALMCP_API FUnrealMCPMaterialCommands
{
public:
	FUnrealMCPMaterialCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// Read
	TSharedPtr<FJsonObject> HandleGetMaterialInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialInstanceInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialParameterCollectionInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetMaterialGraph(const TSharedPtr<FJsonObject>& Params);

	// Write (2026-06-11 P0a+P0b+P1)
	TSharedPtr<FJsonObject> HandleSetMaterialExpressionProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMaterialInstanceScalarParameter(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMaterialInstanceVectorParameter(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMaterialInstanceTextureParameter(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetMaterialProperty(const TSharedPtr<FJsonObject>& Params);
};
