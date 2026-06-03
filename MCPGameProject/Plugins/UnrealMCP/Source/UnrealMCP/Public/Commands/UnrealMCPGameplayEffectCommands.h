#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * GameplayEffect read / create / write commands (LT14).
 *
 * P0 (2026-06-03):
 *   - get_gameplay_effect_info   : read CDO of a GE Blueprint (Duration / Modifiers /
 *                                   Stacking / inherited Asset+Granted tags via Components
 *                                   / counts of Cues / Executions / GrantedAbilities)
 *   - create_gameplay_effect     : create a new Blueprint subclass of UGameplayEffect
 *                                   (or any specified GE parent class).
 *
 * P1 (planned, same task):
 *   - set_gameplay_effect_property
 *   - add/remove/set_gameplay_effect_modifier
 *   - set_gameplay_effect_inherited_tags
 */
class UNREALMCP_API FUnrealMCPGameplayEffectCommands
{
public:
	FUnrealMCPGameplayEffectCommands();

	TSharedPtr<FJsonObject> HandleCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

private:
	// P0
	TSharedPtr<FJsonObject> HandleGetGameplayEffectInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCreateGameplayEffect(const TSharedPtr<FJsonObject>& Params);

	// P1
	TSharedPtr<FJsonObject> HandleSetGameplayEffectProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddGameplayEffectModifier(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveGameplayEffectModifier(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetGameplayEffectModifier(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetGameplayEffectInheritedTags(const TSharedPtr<FJsonObject>& Params);

	// P2 (2026-06-03 second pass)
	TSharedPtr<FJsonObject> HandleListGameplayEffects(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteGameplayEffect(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddGameplayEffectCue(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveGameplayEffectCue(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetGameplayEffectCue(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetGameplayEffectTagRequirements(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetGameplayEffectChanceToApply(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleAddGameplayEffectGrantedAbility(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleRemoveGameplayEffectGrantedAbility(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetGameplayEffectGrantedAbility(const TSharedPtr<FJsonObject>& Params);
};
