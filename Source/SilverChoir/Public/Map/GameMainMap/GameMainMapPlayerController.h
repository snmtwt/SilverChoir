#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "GameMainMapPlayerController.generated.h"


class UBaseMapWidget;
class UBattleMapWidget;
class USIS_PlayerInventoryManager;
class UInputMappingContext;
class UBattleUnitSelectionComponent;
UCLASS(Blueprintable)
class SILVERCHOIR_API AGameMainMapPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	AGameMainMapPlayerController();
	/** Returns how many selected units accepted a full navigation path. Blueprint owns the click binding. */
	UFUNCTION(BlueprintCallable, Category="战斗|移动命令", meta=(DisplayName="通知选中单位移动到鼠标位置"))
	int32 MoveSelectedUnitsToCursor(FText& OutError);
	UFUNCTION(BlueprintCallable, Category="战斗|移动命令", meta=(DisplayName="通知选中单位移动到位置"))
	int32 MoveSelectedUnitsToLocation(FVector Destination, FText& OutError);
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="战斗|移动命令", meta=(ClampMin="100",Units="cm"))
	float MoveOrderSpacing = 140.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="玩家|战斗", meta=(DisplayName="单位框选组件"))
	TObjectPtr<UBattleUnitSelectionComponent> UnitSelection;
	/** 原生生命周期保持通用映射，并根据 GameState 地图类型切换模式映射。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="玩家|增强输入", meta=(DisplayName="通用输入映射"))
	TObjectPtr<UInputMappingContext> CommonInputMappingContext;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="玩家|增强输入", meta=(DisplayName="基地输入映射"))
	TObjectPtr<UInputMappingContext> BaseInputMappingContext;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="玩家|增强输入", meta=(DisplayName="战斗输入映射"))
	TObjectPtr<UInputMappingContext> BattleInputMappingContext;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="玩家|增强输入", meta=(DisplayName="通用输入优先级"))
	int32 CommonInputPriority = 100;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="玩家|增强输入", meta=(DisplayName="地图输入优先级"))
	int32 MapInputPriority = 10;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="玩家|背包")
	TObjectPtr<USIS_PlayerInventoryManager> PlayerInventoryManager;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="主地图|地图UI") TSubclassOf<UBaseMapWidget> BaseWidgetClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="主地图|地图UI") TSubclassOf<UBattleMapWidget> BattleWidgetClass;
	UPROPERTY(Transient, BlueprintReadOnly, Category="主地图|地图UI") TObjectPtr<UBaseMapWidget> BaseWidget;
	UPROPERTY(Transient, BlueprintReadOnly, Category="主地图|地图UI") TObjectPtr<UBattleMapWidget> BattleWidget;
	/** 移除并释放旧 UI 后创建目标 UI，仅切换本地界面，不修改 GameState；None 关闭两者。同类型重复调用复用实例。 */
	UFUNCTION(BlueprintCallable, Category="主地图|地图UI", meta=(DisplayName="切换地图UI"))
	bool SwitchMapUI(EGameMainMapType MapType);
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void FlushPressedKeys() override;
	/** 通用输入生命周期通知：失焦、切换地图类型或释放界面时取消蓝图中未完成的手势。 */
	UFUNCTION(BlueprintImplementableEvent, Category="玩家|输入事件", meta=(DisplayName="输入状态重置"))
	void OnInputStateReset();
	/** 默认移动已拥有的 Pawn；蓝图可覆盖为队伍生成、相机过渡等，不必调用父实现。 */
	UFUNCTION(BlueprintNativeEvent, Category="主地图") void EnterMap(FName MapID, const FTransform& EntryTransform);
	virtual void EnterMap_Implementation(FName MapID, const FTransform& EntryTransform);
private:
	void BindMapTypeState(AGameStateBase* State);
	UFUNCTION() void HandleMapTypeChanged(EGameMainMapType PreviousType, EGameMainMapType CurrentType);
	UFUNCTION() void HandleSelectionMapChanged(FName PreviousMapID, FName CurrentMapID);
	void ApplyMapInputContexts(EGameMainMapType MapType);
	void ReleaseMapInputContexts();
	TWeakObjectPtr<AGameMainMapGameState> InputMapState;
	void ClearMapUI();
	bool bSwitchingMapUI = false;
	bool bEndingPlay = false;
};
