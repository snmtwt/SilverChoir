#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MapWidgetBase.generated.h"

class AGameMainMapGameState;
class UPanelWidget;

/** 布局在 WBP 中制作；HUDLayer 与 ModalLayer 为可选绑定的 Designer 容器。 */
UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API UMapWidgetBase : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="主地图") TObjectPtr<UPanelWidget> HUDLayer;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="主地图") TObjectPtr<UPanelWidget> ModalLayer;
	UFUNCTION(BlueprintImplementableEvent, Category="主地图") void InitializeMapUI(AGameMainMapGameState* MapState);
};
