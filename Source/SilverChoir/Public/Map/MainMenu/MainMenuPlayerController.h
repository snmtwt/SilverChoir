#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Map/MainMenu/MainMenuWidget.h"
#include "MTS_MapTransitionTypes.h"
#include "MainMenuPlayerController.generated.h"

class UGameViewportClient;

/** 创建主菜单并管理 UI 输入；业务流程在 HandleMenuAction 中扩展。 */
UCLASS(Blueprintable, meta = (DisplayName = "主菜单玩家控制器"))
class SILVERCHOIR_API AMainMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMainMenuPlayerController();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "主菜单")
	TSubclassOf<UMainMenuWidget> MainMenuWidgetClass;
	/** 纯 H5/UMG 菜单默认跳过世界渲染；将来使用 3D 菜单背景时启用。只在菜单显示期间生效。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "主菜单|渲染", meta = (DisplayName = "渲染菜单后方的世界"))
	bool bRenderWorldBehindMenu = false;
	UPROPERTY(BlueprintAssignable, Category = "主菜单")
	FMainMenuActionEvent OnMenuActionRequested;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="主菜单|切换") TSoftObjectPtr<UWorld> NewGameMap;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="主菜单|切换") TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="主菜单|切换") TSubclassOf<UMTS_MapTransitionHandler> NewGameTransitionHandlerClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="主菜单|切换", meta=(ClampMin="0", ClampMax="1")) float MainMapProgressShare = .3f;

	UFUNCTION(BlueprintCallable, Category = "主菜单")
	UMainMenuWidget* ShowMainMenu();
	UFUNCTION(BlueprintCallable, Category = "主菜单")
	void HideMainMenu();
	UFUNCTION(BlueprintPure, Category = "主菜单")
	UMainMenuWidget* GetMainMenuWidget() const { return MainMenuWidgetInstance; }
	/** C++ 生命周期通知；外部直接移除控件时也必须归还视口的世界渲染状态。 */
	void NotifyMainMenuDetached(const UMainMenuWidget* Menu);
	void NotifyMainMenuVisibilityChanged(const UMainMenuWidget* Menu);

	/** 开始行动默认先退场、再经 MTS 进入主地图；读档和设置留给蓝图扩展。 */
	UFUNCTION(BlueprintNativeEvent, Category = "主菜单")
	void HandleMenuAction(EMainMenuAction Action);
	virtual void HandleMenuAction_Implementation(EMainMenuAction Action);

private:
	UFUNCTION() void RouteMenuAction(EMainMenuAction Action);
	UFUNCTION() void HandleMenuExitFinished(EMainMenuAction Action);
	void UpdateMenuWorldRendering();
	void RestoreMenuWorldRendering();
	TWeakObjectPtr<UGameViewportClient> ManagedMenuViewport;
	bool bOwnsWorldRenderingState = false;
	bool bSavedDisableWorldRendering = false;
	bool bMenuEndingPlay = false;
	UPROPERTY(Transient) TObjectPtr<UMainMenuWidget> MainMenuWidgetInstance;
};
