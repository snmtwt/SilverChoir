// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "HMS_OverlayerDataAsset.generated.h"


USTRUCT(BlueprintType)
struct FHMS_OverlayerData
{
	GENERATED_BODY()

};
/**
 * 暂时无用
 */
UCLASS()
class HYBRIDMOTIONSYSTEM_API UHMS_OverlayerDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UpperBody", DisplayName = "动作查询列表")
	TMap<FGameplayTag, FHMS_OverlayerData> OverlayerDatas;
};
