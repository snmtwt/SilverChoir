#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GameMainMapGameState.generated.h"


/** 当前玩家所在的逻辑地图类型，与已加载子地图的数量无关。 */
UENUM(BlueprintType)
enum class EGameMainMapType : uint8
{
	None UMETA(DisplayName="未指定"),
	Base UMETA(DisplayName="基地地图"),
	Battle UMETA(DisplayName="战斗地图")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGameMainMapChanged, FName, PreviousMapID, FName, ActiveMapID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGameMainMapFailure, const FText&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FGameMainMapTypeChanged, EGameMainMapType, PreviousType, EGameMainMapType, CurrentType);

/** 单机主地图状态。联网加载与同步需单独实现，当前不提供客户端 RPC。 */
UCLASS(Blueprintable)
class SILVERCHOIR_API AGameMainMapGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	/** 蓝图在实际地图切换成功后设置；UI 显示/隐藏不改变游戏状态。 */
	UFUNCTION(BlueprintCallable, Category="主地图|地图类型", meta=(DisplayName="设置当前地图类型"))
	void SetCurrentMapType(EGameMainMapType MapType);
	UFUNCTION(BlueprintPure, Category="主地图|地图类型", meta=(DisplayName="获取当前地图类型"))
	EGameMainMapType GetCurrentMapType() const { return CurrentMapType; }
	UPROPERTY(BlueprintAssignable, Category="主地图|地图类型", meta=(DisplayName="当前地图类型改变"))
	FGameMainMapTypeChanged OnCurrentMapTypeChanged;
	UFUNCTION(BlueprintPure, Category="主地图|地图类型", meta=(DisplayName="是否是基地地图"))
	bool IsBaseMap() const { return CurrentMapType == EGameMainMapType::Base; }
	UFUNCTION(BlueprintPure, Category="主地图|地图类型", meta=(DisplayName="是否是战斗地图"))
	bool IsBattleMap() const { return CurrentMapType == EGameMainMapType::Battle; }
	UPROPERTY(BlueprintReadOnly, Category="主地图") FName ActiveMapID;
	/** 仅保护同步激活期间的事件重入；异步加载进度查询地图切换插件。 */
	UPROPERTY(BlueprintReadOnly, Category="主地图") bool bSwitchingMap = false;
	UPROPERTY(BlueprintReadOnly, Category="主地图") FText LastError;
	UPROPERTY(BlueprintAssignable, Category="主地图") FGameMainMapChanged OnActiveMapChanged;
	UPROPERTY(BlueprintAssignable, Category="主地图") FGameMainMapFailure OnMapSwitchFailed;
private:
	UPROPERTY(Transient, BlueprintReadOnly, Category="主地图|地图类型", meta=(AllowPrivateAccess="true", DisplayName="当前地图类型"))
	EGameMainMapType CurrentMapType = EGameMainMapType::None;
};
