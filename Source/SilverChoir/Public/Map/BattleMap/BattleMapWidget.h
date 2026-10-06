#pragma once

#include "CoreMinimal.h"
#include "UIBasic/MapWidgetBase.h"
#include "Map/BattleMap/BattleHUDTypes.h"
#include "BattleMapWidget.generated.h"

class UCanvasPanel;
class UVerticalBox;
class UTextBlock;
class UImage;
class UBattleMemberCardWidget;
class UBattleHUDButton;
class UBattleHUDVisual;
class UBattleModeScrollBox;
class UBattleMemberModeWidget;
class UBattleSquadModeWidget;
class UBattleSquadEntryWidget;
class UBattlePersonnelCardWidget;

/** C++ owns presentation algorithms. WBP_BattleHUD owns initialization and command routing. */
UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API UBattleMapWidget : public UMapWidgetBase
{
	GENERATED_BODY()
public:
    /** Resolves existing player squads without spawning units or changing game state. */
    UFUNCTION(BlueprintCallable, Category="战斗UI|初始化", meta=(DisplayName="初始化战斗UI"))
    bool InitializeBattleUI(const TArray<FGuid>& SquadIds, FText& OutError);
    UFUNCTION(BlueprintImplementableEvent, Category="战斗UI|业务事件", meta=(DisplayName="战斗UI小队初始化完成"))
    void OnBattleUIInitialized(const TArray<FGuid>& SquadIds);
    UFUNCTION(BlueprintImplementableEvent, Category="战斗UI|业务事件", meta=(DisplayName="战斗UI选中小队已改变"))
    void OnBattleSquadSelected(FGuid PreviousSquadId, FGuid NewSquadId);
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") bool bRosterInitialized = false;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="战斗UI|组件") TSubclassOf<UBattleSquadEntryWidget> SquadEntryClass;
    /** Presentation-only mode change. Gameplay may react in OnControlModeChanged. */
    UFUNCTION(BlueprintCallable, Category="战斗UI|控制模式", meta=(DisplayName="切换战斗控制模式"))
    bool SetControlMode(EBattleControlMode Mode, bool bAnimate = true);
    UFUNCTION(BlueprintImplementableEvent, Category="战斗UI|业务事件", meta=(DisplayName="战斗控制模式已切换"))
    void OnControlModeChanged(EBattleControlMode PreviousMode, EBattleControlMode NewMode);
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|控制模式")
    EBattleControlMode CurrentControlMode = EBattleControlMode::Member;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="战斗UI|控制模式", meta=(ClampMin="1",ClampMax="60",DisplayName="模式切换动画速度"))
    float ModeSwitchInterpolationSpeed = 18.f;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|模式容器") TObjectPtr<UBattleModeScrollBox> ModePages;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|模式容器") TObjectPtr<UBattleMemberModeWidget> MemberModePanel;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|模式容器") TObjectPtr<UBattleSquadModeWidget> SquadModePanel;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|模式容器") TObjectPtr<UBattleHUDButton> MemberModeButton;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="战斗UI|模式容器") TObjectPtr<UBattleHUDButton> SquadModeButton;
    UFUNCTION(BlueprintCallable, Category="战斗UI|数据") bool InitializeTestHUD(UBattleHUDTestData* Data);
    UFUNCTION(BlueprintCallable, Category="战斗UI|数据") bool SetBattleSquads(const TArray<FBattleSquadView>& Data, bool bTestData = false);
    UFUNCTION(BlueprintCallable, Category="战斗UI|选择") bool SelectSquad(FGuid SquadId);
    UFUNCTION(BlueprintCallable, Category="战斗UI|选择") bool SelectMember(FGuid UnitId);
    UFUNCTION(BlueprintCallable, Category="战斗UI|选择") bool ToggleVehicleDrawer();
    UFUNCTION(BlueprintCallable, Category="战斗UI|选择") void CloseDrawer();
    UFUNCTION(BlueprintCallable, Category="战斗UI|输入") void RequestCommand(EBattleCommand Command);
    UFUNCTION(BlueprintCallable, Category="战斗UI|数据") bool SetDisplayedPosture(FGuid UnitId, EBattlePosture Posture);
    UFUNCTION(BlueprintCallable, Category="战斗UI|数据") bool ToggleDisplayedStealth(FGuid UnitId);
    UFUNCTION(BlueprintCallable, Category="战斗UI|数据") bool UpdateMemberView(const FBattleMemberView& Member);
    UFUNCTION(BlueprintCallable, Category="战斗UI|输入") void BeginTargeting(EBattleCommand Command);
    UFUNCTION(BlueprintCallable, Category="战斗UI|输入") void CancelTargeting();
    UFUNCTION(BlueprintCallable, Category="战斗UI|反馈") void ShowFeedback(FText Message);
    /** Test-only presentation. These functions never modify authoritative unit/inventory records. */
    UFUNCTION(BlueprintCallable, Category="战斗UI|测试") void ShowTestBackpack(FGuid UnitId);
    UFUNCTION(BlueprintCallable, Category="战斗UI|测试") void UseTestQuickItem(FGuid UnitId, int32 SlotIndex);
    UFUNCTION(BlueprintCallable, Category="战斗UI|测试") void ApplyTestVehicleCommand(EBattleCommand Command);
    UFUNCTION(BlueprintCallable, Category="战斗UI|小地图") void ApplyMinimapCommand(EBattleCommand Command);
    UFUNCTION(BlueprintCallable, Category="战斗UI|界面") void CloseModal();
    UFUNCTION(BlueprintPure, Category="战斗UI|数据") bool GetMemberView(FGuid UnitId, FBattleMemberView& Member) const;
    UFUNCTION(BlueprintImplementableEvent, Category="战斗UI|业务事件", meta=(DisplayName="战斗操作请求")) void OnBattleCommandRequested(FGuid UnitId, FGuid SquadId, EBattleCommand Command);
    UFUNCTION(BlueprintImplementableEvent, Category="战斗UI|业务事件", meta=(DisplayName="战斗交互目标确认")) void OnBattleTargetConfirmed(FGuid UnitId, EBattleCommand Command, const FHitResult& Hit);
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="战斗UI|组件") TSubclassOf<UBattleMemberCardWidget> MemberCardClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="战斗UI|组件") TSubclassOf<UBattleHUDButton> ButtonClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="战斗UI|布局") float DockHeight = 148.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="战斗UI|布局") float MaximumDockWidth = 1800.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="战斗UI|布局") float MemberDrawerWidth = 180.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="战斗UI|布局") float VehicleDrawerWidth = 290.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="战斗UI|布局") float DrawerAnimationSeconds = .18f;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") TArray<FBattleSquadView> Squads;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") FGuid SelectedSquadId;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") FGuid SelectedMemberId;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") EBattleDrawer ActiveDrawer = EBattleDrawer::None;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") bool bUsingTestData = false;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") bool bTargeting = false;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") EBattleCommand TargetingCommand = EBattleCommand::Interact;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") int32 RequestSerial = 0;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") EBattleCommand LastRequestedCommand = EBattleCommand::Stand;
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗UI|状态") TArray<TObjectPtr<UBattleMemberCardWidget>> MemberCards;
protected:
    virtual void NativePreConstruct() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UCanvasPanel> DockPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UVerticalBox> SquadRail;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UCanvasPanel> MemberDrawerPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UCanvasPanel> VehicleDrawerPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UCanvasPanel> ModeRail;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBattleHUDButton> VehicleButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CommandMemberName;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> VehicleName;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> VehicleStats;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> VehicleImage;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> FeedbackText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SquadNameText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBattleHUDVisual> TacticalMap;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UCanvasPanel> MinimapPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UCanvasPanel> ModalPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ModalTitle;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ModalBody;
private:
    void ApplyControlMode(bool bAnimate);
    bool bSwitchingControlMode = false;
    const FBattleSquadView* CurrentSquad() const;
    FBattleMemberView* MutableMember(FGuid Id);
    void RebuildRoster(bool bRebuildSquads = true);
    bool bUpdatingRoster = false;
    bool bInitializingRoster = false;
    bool bRefreshingViews = false;
    uint64 PresentationRevision = 0;
    bool bEndingPresentation = false;
    void RefreshViews();
    void UpdateLayout(float Width);
    void SetDrawer(EBattleDrawer Drawer);
    UFUNCTION() void HandleChoice(FName Choice);
    UFUNCTION() void HandleMemberChoice(FName Choice);
    void HandleControlPanelOpening(FGuid UnitId);
    UFUNCTION() void HandleSquadChoice(FName Choice);
    UPROPERTY(Transient) TArray<TObjectPtr<UBattleHUDButton>> SquadButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UBattleSquadEntryWidget>> SquadEntries;
    UPROPERTY(Transient) TArray<TObjectPtr<UBattleHUDButton>> CommandButtons;
    float DrawerAmount = 0.f;
    float LastWidth = 0.f;
    float FeedbackRemaining = 0.f;
    bool bFollowMap = true;
    bool bMinimapCollapsed = false;
    TEnumAsByte<EMouseCursor::Type> SavedCursor = EMouseCursor::Default;
};
