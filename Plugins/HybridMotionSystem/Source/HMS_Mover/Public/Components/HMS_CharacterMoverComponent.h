#pragma once

#include "CoreMinimal.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "HMS_CharacterMoverComponent.generated.h"

/**
 * Drop-in CharacterMoverComponent configured with the GASP-style HMS movement modes.
 * It keeps Mover's network-prediction backend, stance handling and NavMover compatibility.
 */
UCLASS(ClassGroup = (HMS), BlueprintType, Blueprintable,
	meta = (BlueprintSpawnableComponent, DisplayName = "HMS Mover 角色组件"))
class HMS_MOVER_API UHMS_CharacterMoverComponent : public UCharacterMoverComponent
{
	GENERATED_BODY()

public:
	UHMS_CharacterMoverComponent();
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;

	/** Keep the shared Mover settings aligned with SandboxCharacter_Mover in UE 5.8 GASP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|Mover",
		meta = (DisplayName = "使用 GASP 5.8 移动默认值"))
	bool bUseGameAnimationSampleMovementDefaults = true;

protected:
	virtual void OnRegister() override;

private:
	/** 升级旧蓝图中序列化保存的引擎默认 Walking/Falling，不覆盖用户自定义模式。 */
	void UpgradeDefaultMovementModes();

	/** Apply the UE 5.8 sample's shared ground/crouch settings after modes are refreshed. */
	void ApplyGameAnimationSampleMovementDefaults();
};
