#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MTS_SubMapDataAsset.generated.h"

class UWorld;
class UMTS_SubMapDataAsset;
class UMTS_MapLoadingWidget;

/** 批量加载的一项；ID 同时作为实例查询 Key 和统一进度 Key。 */
USTRUCT(BlueprintType)
struct MAPTRANSITIONSYSTEM_API FMTS_SubMapLoadConfig
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="子地图") FName MapID;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="子地图", meta=(ClampMin="0.001")) float Weight = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="子地图") TSoftObjectPtr<UWorld> MapAsset;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="子地图") FVector Location = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="子地图") FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="子地图") FGameplayTagContainer MapTags;
};

/** Runtime streaming request; the parent World and existing unselected instances stay alive. */
USTRUCT(BlueprintType)
struct MAPTRANSITIONSYSTEM_API FMTS_SubMapLoadRequest
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="子地图", meta=(DisplayName="子地图资产")) TObjectPtr<UMTS_SubMapDataAsset> MapAsset;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="子地图", meta=(DisplayName="位置")) FVector Location = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="子地图", meta=(DisplayName="旋转")) FRotator Rotation = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="加载界面", meta=(DisplayName="加载界面类")) TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="加载界面", meta=(DisplayName="加载标题")) FText LoadingTitle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="加载界面", meta=(DisplayName="自动进度上限", ClampMin="0", ClampMax="1")) float AutomaticProgressMax = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="加载界面", meta=(DisplayName="进度平滑速度", ClampMin="0")) float ProgressSmoothSpeed = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="加载界面", meta=(DisplayName="完成后关闭延迟", ClampMin="0")) float CompletionDelay = .25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="生命周期", meta=(DisplayName="等待处理对象通知加载完成")) bool bRequireManualReady = true;
};

/** 子地图定义。同一 World 中 MapID 唯一；多个定义可以引用同一地图。 */
UCLASS(BlueprintType, meta=(DisplayName="MTS 子地图数据资产"))
class MAPTRANSITIONSYSTEM_API UMTS_SubMapDataAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="子地图", meta=(DisplayName="地图地图"))
	TSoftObjectPtr<UWorld> MapAsset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="子地图", meta=(DisplayName="地图ID"))
	FName MapID;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="子地图", meta=(DisplayName="地图名"))
	FName MapName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="子地图", meta=(DisplayName="地图Tags"))
	FGameplayTagContainer MapTags;
};
