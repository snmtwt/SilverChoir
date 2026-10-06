#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Map/BattleMap/BattleHUDTypes.h"
#include "BattleModePanels.generated.h"

class UBattleHUDButton;
class UHorizontalBox;
class UScrollBox;
class UVerticalBox;
class UPanelWidget;
class UBattlePersonnelCardWidget;
class UBattleVehiclePanelWidget;
DECLARE_MULTICAST_DELEGATE_OneParam(FBattleControlPanelOpening, FGuid);

/** Shared presentation settings. List contents and selection remain owned by Blueprint. */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FBattleListScrollStyle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="滚动条", meta=(DisplayName="轨道颜色"))
    FLinearColor TrackColor = FLinearColor(FColor(9, 24, 35, 220));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="滚动条", meta=(DisplayName="滑块颜色"))
    FLinearColor ThumbColor = FLinearColor(FColor(65, 108, 128, 235));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="滚动条", meta=(DisplayName="悬浮颜色"))
    FLinearColor HoverColor = FLinearColor(FColor(43, 172, 196));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="滚动条", meta=(DisplayName="拖动颜色"))
    FLinearColor DragColor = FLinearColor(FColor(40, 220, 237));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="滚动条", meta=(DisplayName="纵向滑块宽度", ClampMin="2", ClampMax="8"))
    float VerticalThickness = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="滚动条", meta=(DisplayName="横向滑块高度", ClampMin="2", ClampMax="8"))
    float HorizontalThickness = 3.f;
};

/** Authored member-mode panel: styling and typed access, without constructing roster data. */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="战斗成员模式面板"))
class SILVERCHOIR_API UBattleMemberModeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UBattleMemberModeWidget(const FObjectInitializer& ObjectInitializer);

    UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="战斗UI|绑定")
    TObjectPtr<UScrollBox> SquadScrollBox;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="战斗UI|绑定")
    TObjectPtr<UVerticalBox> SquadList;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="战斗UI|绑定")
    TObjectPtr<UScrollBox> MemberScrollBox;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="战斗UI|绑定")
    TObjectPtr<UHorizontalBox> MemberList;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定")
    TObjectPtr<UBattleHUDButton> VehicleButton;
    /** Optional host used only when the current layout has no preplaced vehicle panel. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定")
    TObjectPtr<UPanelWidget> VehiclePanelContainer;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="战斗UI|控制面板")
    TSubclassOf<UBattleVehiclePanelWidget> VehiclePanelClass;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|控制面板")
    TObjectPtr<UBattlePersonnelCardWidget> ActivePersonnelCard;
    /** Required Designer reference. Keep the widget name VehiclePanel; its parent may change. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="战斗UI|控制面板")
    TObjectPtr<UBattleVehiclePanelWidget> VehiclePanel;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|控制面板") FGuid CurrentSquadId;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|控制面板") FBattleVehicleView CurrentVehicle;
    UFUNCTION(BlueprintCallable, Category="战斗UI|控制面板", meta=(DisplayName="设置成员模式小队"))
    void SetSquadContext(FGuid SquadId, const FBattleVehicleView& Vehicle);
    UFUNCTION(BlueprintCallable, Category="战斗UI|控制面板", meta=(DisplayName="注册人员卡片"))
    void RegisterPersonnelCard(UBattlePersonnelCardWidget* Card);
    UFUNCTION(BlueprintCallable, Category="战斗UI|控制面板", meta=(DisplayName="重置人员卡片绑定"))
    void ResetPersonnelCards();
    UFUNCTION(BlueprintCallable, Category="战斗UI|控制面板", meta=(DisplayName="展开人员控制面板"))
    bool ShowPersonnelPanel(UBattlePersonnelCardWidget* Card);
    UFUNCTION(BlueprintCallable, Category="战斗UI|控制面板", meta=(DisplayName="展开小队车辆面板"))
    bool ShowVehiclePanel();
    UFUNCTION(BlueprintCallable, Category="战斗UI|控制面板", meta=(DisplayName="关闭当前控制面板"))
    void CloseActivePanel();
    /** HUD bookkeeping runs before the Blueprint open event. Invalid UnitId denotes the vehicle panel. */
    FBattleControlPanelOpening OnPanelOpening;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗UI|样式", meta=(DisplayName="列表滚动条样式"))
    FBattleListScrollStyle ListStyle;
    UFUNCTION(BlueprintCallable, Category="战斗UI|样式", meta=(DisplayName="刷新列表样式"))
    void RefreshListStyle();

protected:
    virtual void NativePreConstruct() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    UPROPERTY(Transient) TArray<TObjectPtr<UBattlePersonnelCardWidget>> PersonnelCards;
    bool bPresentationActive=false;
    bool bEndingPresentation=false;
    bool bChangingPanel=false;
    bool bOwnsVehiclePanel=false;
    bool bVehicleInMemberScrollBox=false;
    uint64 PanelRevision=0;
    bool CloseActivePanelInternal(uint64 Revision);
    bool EnsureVehiclePanel(uint64 Revision);
    void BindVehiclePanel(UBattleVehiclePanelWidget* Panel, bool bOwned);
    void CacheVehicleScrollContext();
    void HandlePersonnelReleased(UBattlePersonnelCardWidget* Card);
    void HandlePersonnelContextChanged(UBattlePersonnelCardWidget* Card);
    void HandleVehicleReleased(UBattleVehiclePanelWidget* Panel);
    void HandleVehicleLayoutAnimationFinished(UBattleVehiclePanelWidget* Panel);
    UFUNCTION() void HandlePersonnelPanelRequested(UBattlePersonnelCardWidget* Card, FGuid UnitId);
    UFUNCTION() void HandleVehicleClicked();
};

/** Authored squad-mode panel; Blueprint supplies squad summaries and button behavior. */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="战斗小队模式面板"))
class SILVERCHOIR_API UBattleSquadModeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="战斗UI|绑定")
    TObjectPtr<UScrollBox> SquadScrollBox;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="战斗UI|绑定")
    TObjectPtr<UVerticalBox> SquadList;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="战斗UI|绑定")
    TObjectPtr<UScrollBox> SquadCardScrollBox;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="战斗UI|绑定")
    TObjectPtr<UHorizontalBox> SquadCardList;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|绑定")
    TObjectPtr<UBattleHUDButton> VehicleButton;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗UI|样式", meta=(DisplayName="列表滚动条样式"))
    FBattleListScrollStyle ListStyle;
    UFUNCTION(BlueprintCallable, Category="战斗UI|样式", meta=(DisplayName="刷新列表样式"))
    void RefreshListStyle();

protected:
    virtual void NativePreConstruct() override;
    virtual void NativeConstruct() override;
};
