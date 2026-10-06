// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "SmartObjectComponent.h"
#include "SmartObjectSubsystem.h"
#include "HMS_SmartObjectBasic.generated.h"


enum class EHMS_Gait : uint8;
enum class EHMS_RotationMode : uint8;

/**
 * @brief HMS 插件专用 SmartObject 基础 Actor（UE5.7 Mover2.0 风格）
 */
UCLASS(Blueprintable, BlueprintType, Abstract)
class HYBRIDMOTIONSYSTEM_API AHMS_SmartObjectBasic : public AActor
{
	GENERATED_BODY()

public:
	AHMS_SmartObjectBasic();

	// ==================== 配置开关 ====================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HMS|SmartObject|Config")
	bool bAutoActivateOnBeginPlay = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|SmartObject|Runtime")
	bool bIsActivated = false;

public:
	UFUNCTION(BlueprintCallable, Category = "HMS|SmartObject")
	virtual void SetSmartObjectActive(bool bNewActive);

	UFUNCTION(BlueprintNativeEvent, Category = "HMS|SmartObject")
	void InitializeSmartObjectDefinition();

	UFUNCTION(BlueprintCallable, Category = "HMS|SmartObject")
	virtual void BroadcastUsageToHMS(const FGameplayTagContainer& UsageTags, EHMS_Gait SuggestedGait, EHMS_RotationMode SuggestedRotation);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HMS|SmartObject|Component")
	TObjectPtr<USmartObjectComponent> SmartObjectComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HMS|SmartObject")
	TObjectPtr<USmartObjectDefinition> SmartObjectDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HMS|SmartObject")
	FGameplayTag DefaultActivityTag;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RegisterToSubsystem();
	void UnregisterFromSubsystem();
};