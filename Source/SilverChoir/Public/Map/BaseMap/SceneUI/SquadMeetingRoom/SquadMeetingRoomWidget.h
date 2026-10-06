#pragma once
#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "Map/BaseMap/SceneUI/BaseSceneWidget.h"
#include "Data/Squads/SquadStructs.h"
#include "SquadMeetingRoomWidget.generated.h"

class UPlayerSquadManagerBase;
class UPlayerUnitManagerBase;
class USelectionButtonWidget;
class UBasicButtonWidget;
class UPersonnelListEntryWidget;
class USquadMemberCardWidget;
class USquadVehicleEntryWidget;
class UPanelWidget;
class UWidgetSwitcher;
class UScrollBox;
class UWrapBox;
class UTextBlock;
class UEditableTextBox;
class UImage;
class UTexture2D;
class UWidgetAnimation;

UENUM(BlueprintType)
enum class ESquadRoomLayer : uint8
{
    SquadSelection UMETA(DisplayName="选择小队"),
    SquadManagement UMETA(DisplayName="管理小队")
};

UENUM(BlueprintType)
enum class ESquadRoomPanel : uint8
{
    LeftList UMETA(DisplayName="当前左侧列表"),
    Management UMETA(DisplayName="小队管理面板"),
    Members UMETA(DisplayName="底部成员名单")
};

/** 小队会议室：业务/共享数据绑定由基类提供，布局由 UMG 子蓝图编辑。 */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="小队会议室 UI"))
class SILVERCHOIR_API USquadMeetingRoomWidget : public UBaseSceneWidget
{
    GENERATED_BODY()
public:
    USquadMeetingRoomWidget(const FObjectInitializer& Initializer) : Super(Initializer)
    {
        SceneTitle = NSLOCTEXT("BaseSceneUI", "SquadMeetingRoom", "小队会议室");
    }
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|控件") TSubclassOf<USelectionButtonWidget> SquadRowClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|控件") TSubclassOf<UPersonnelListEntryWidget> PersonnelRowClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|控件") TSubclassOf<USquadMemberCardWidget> MemberCardClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|控件") TSubclassOf<USelectionButtonWidget> IconButtonClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|控件") TSubclassOf<USquadVehicleEntryWidget> VehicleEntryClass;
    /** 无匹配蓝图动画或动画被关闭打断时的平滑过渡时长；设为 0 可关闭全部过渡动画。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="小队|动画", meta=(ClampMin="0", DisplayName="回退过渡时长（0禁用动效）")) float TransitionDuration = 0.22f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="小队|动画", meta=(DisplayName="使用蓝图动画")) bool bUseWidgetAnimations = true;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="小队|动画", meta=(ClampMin="0.01", DisplayName="蓝图动画播放速度")) float WidgetAnimationPlaybackSpeed = 1.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|动画") FName SquadEnterAnimation = TEXT("小队列表滑入");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|动画") FName SquadExitAnimation = TEXT("小队列表滑出");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|动画") FName PersonnelEnterAnimation = TEXT("人员列表滑入");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|动画") FName PersonnelExitAnimation = TEXT("人员列表滑出");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|动画") FName ManagementEnterAnimation = TEXT("管理面板滑入");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|动画") FName ManagementExitAnimation = TEXT("管理面板滑出");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|动画") FName MembersEnterAnimation = TEXT("成员名单滑入");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="小队|动画") FName MembersExitAnimation = TEXT("成员名单滑出");
    UFUNCTION(BlueprintPure, Category="小队|动画", meta=(DisplayName="获取小队面板动画"))
    UWidgetAnimation* GetPanelTransitionAnimation(ESquadRoomPanel Panel, bool bEntering) const;
    UPROPERTY(BlueprintReadOnly, Transient, Category="小队") FName CurrentTileId;
    UPROPERTY(BlueprintReadOnly, Transient, Category="小队") FGuid SelectedSquadId;
    UPROPERTY(BlueprintReadOnly, Transient, Category="小队") ESquadRoomLayer CurrentLayer = ESquadRoomLayer::SquadSelection;
    UPROPERTY(BlueprintReadOnly, Transient, Category="小队") bool bShowAll = false;
    UPROPERTY(BlueprintReadOnly, Transient, Category="小队") bool bTransitioning = false;
    UPROPERTY(BlueprintReadOnly, Transient, Category="小队") FText LastError;
    UPROPERTY(BlueprintReadOnly, Transient, Category="小队|编辑") bool bEditingDraft = false;
    UPROPERTY(BlueprintReadOnly, Transient, Category="小队|编辑") bool bNewSquadDraft = false;

    /** 显式设置当前战略位置；“待命”仅显示此位置，“全部”显示异地但禁止操作。 */
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="展示小队列表"))
    bool LoadSquadList(FName TileId);
    /** 只建立界面草稿。保存成功前 OutSquadId/SelectedSquadId 无效，不创建权威小队。 */
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="在当前瓦片新建小队"))
    bool CreateSquadAtTile(FName TileId, const FText& SquadName, FGuid& OutSquadId);
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="选择小队并打开管理面板")) bool SelectSquad(FGuid SquadId);
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="返回小队列表")) void ReturnToSquadList();
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="设置小队会议室全部筛选")) void SetShowAll(bool bInShowAll);
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="将人员加入当前小队")) bool AddUnitToSelectedSquad(FGuid UnitId);
    /** 名册点击其他小队成员时要求确认；确认只编辑草稿，正式转队仍需保存。 */
    UFUNCTION(BlueprintCallable, Category="小队会议室|人员调动", meta=(DisplayName="确认人员转入当前小队")) bool ConfirmPendingUnitTransfer();
    UFUNCTION(BlueprintCallable, Category="小队会议室|人员调动", meta=(DisplayName="取消人员转队确认")) void CancelPendingUnitTransfer();
    UFUNCTION(BlueprintPure, Category="小队会议室|人员调动", meta=(DisplayName="是否正在确认人员转队")) bool IsUnitTransferPending() const { return PendingTransferUnitId.IsValid(); }
    UFUNCTION(BlueprintPure, Category="小队会议室|人员调动", meta=(DisplayName="获取待确认转队人员ID")) FGuid GetPendingUnitTransferId() const { return PendingTransferUnitId; }
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="从当前小队移出人员")) bool RemoveUnitFromSelectedSquad(FGuid UnitId);
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="将当前小队成员提升为队长")) bool PromoteMemberToCaptain(FGuid UnitId);
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="修改当前小队名称")) bool RenameSelectedSquad(const FText& Name);
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="选择当前小队预设图标")) bool SelectPresetIcon(int32 IconIndex);
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="使用当前队长头像作为图标")) bool UseCaptainPortrait();
    /** 仅修改草稿车辆；缩容优先保留队长和最早加入者，移出的 ID 在保存前不改变正式归属。 */
    UFUNCTION(BlueprintCallable, Category="小队会议室|车辆", meta=(DisplayName="设置当前小队车辆"))
    bool SetSelectedSquadVehicle(const FVehicleData& Vehicle, TArray<FGuid>& OutRemovedUnitIds);
    /** 仅清除草稿车辆，恢复四人容量；超出成员按相同规则移出草稿。 */
    UFUNCTION(BlueprintCallable, Category="小队会议室|车辆", meta=(DisplayName="清除当前小队车辆"))
    bool ClearSelectedSquadVehicle(TArray<FGuid>& OutRemovedUnitIds);
    /** 从玩家单位子系统读取指定瓦片的车辆。异地车辆不显示，被其他小队占用的同地车辆显示但不可选。 */
    UFUNCTION(BlueprintCallable, Category="小队会议室|车辆", meta=(DisplayName="加载当前瓦片车辆列表"))
    bool LoadAvailableVehicles(FName TileId);
    /** 旧蓝图兼容入口：只校验车辆已存在，然后重新读取当前瓦片；不写入或替换玩家车辆。 */
    UFUNCTION(BlueprintCallable, Category="小队会议室|车辆", meta=(DisplayName="设置可选车辆列表", DeprecatedFunction, DeprecationMessage="请先将车辆加载到玩家单位子系统，再调用加载当前瓦片车辆列表。"))
    bool SetAvailableVehicles(const TArray<FVehicleData>& Vehicles);
    UFUNCTION(BlueprintPure, Category="小队会议室|车辆", meta=(DisplayName="获取可选车辆列表"))
    TArray<FVehicleData> GetAvailableVehicles() const;
    /** 按 ID 重新查询权威车辆；仅改变草稿，正式分配仍由保存小队完成。 */
    UFUNCTION(BlueprintCallable, Category="小队会议室|车辆", meta=(DisplayName="选择列表中的小队车辆"))
    bool SelectAvailableVehicle(FGuid VehicleId);
    UFUNCTION(BlueprintCallable, Category="小队会议室|车辆", meta=(DisplayName="显示小队车辆选择框"))
    void SetVehiclePickerVisible(bool bVisible);
    UFUNCTION(BlueprintPure, Category="小队会议室|车辆", meta=(DisplayName="小队车辆选择框是否显示"))
    bool IsVehiclePickerVisible() const;
    /** 正在编辑的草稿容量，无草稿时返回 0；有车辆时包含驾驶人员的全部座位。 */
    UFUNCTION(BlueprintPure, Category="小队会议室|成员", meta=(DisplayName="获取当前小队人数上限"))
    int32 GetSelectedSquadCapacity() const;
    /** 一次提交队名、队徽、车辆、成员和队长；成功播放现有返回动画，失败保留草稿和编辑界面。 */
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="保存小队")) bool SaveSelectedSquad();
    UFUNCTION(BlueprintPure, Category="小队会议室", meta=(DisplayName="小队是否有未保存修改")) bool HasUnsavedSquadChanges() const;
    UFUNCTION(BlueprintCallable, Category="小队会议室", meta=(DisplayName="显示小队队徽选择框")) void SetIconPickerVisible(bool bVisible);
    /** 成功提交后通知保存ID；提交期间已关闭则不通知，已改选则保留新上下文且不自动返回。 */
    UFUNCTION(BlueprintImplementableEvent, Category="小队会议室", meta=(DisplayName="小队已保存")) void OnSquadSaved(FGuid SquadId);
    /** 管理界面返回当前草稿，成员仍只有ID，不复制单位数据。 */
    UFUNCTION(BlueprintPure, Category="小队会议室") bool GetSelectedSquad(FSquadData& OutSquad) const;
    /** 打开选择框前的通知；无需提供车辆数组。新建草稿的 SquadId 无效。 */
    UFUNCTION(BlueprintImplementableEvent, Category="小队会议室|车辆", meta=(DisplayName="请求选择小队车辆")) void OnVehicleSelectionRequested(FGuid SquadId);
    UFUNCTION(BlueprintImplementableEvent, Category="小队会议室", meta=(DisplayName="小队管理层级已切换")) void OnLayerChanged(ESquadRoomLayer Layer);
    UFUNCTION(BlueprintImplementableEvent, Category="小队会议室", meta=(DisplayName="小队操作失败")) void OnOperationFailed(const FText& Error);
    virtual void LoadSceneUI_Implementation() override;
    virtual void UnloadSceneUI_Implementation() override;
    virtual void BeginDestroy() override;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UPanelWidget> LeftPanel;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UPanelWidget> ManagementPanel;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UPanelWidget> MemberStripPanel;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UWidgetSwitcher> LeftPages;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UScrollBox> SquadList;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UScrollBox> PersonnelList;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UScrollBox> MemberList;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWrapBox> IconOptions;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> CreateSquadButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> BackToSquadsButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> SaveSquadNameButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> SaveSquadButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> ChooseSquadIconButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> CloseIconPickerButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> IconPickerPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> DraftStatusText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> UseCaptainPortraitButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> ChooseVehicleButton;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="小队|绑定") TObjectPtr<UPanelWidget> VehiclePickerPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UScrollBox> VehicleOptions;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> CloseVehiclePickerButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> ClearVehicleButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> VehiclePreviewImage;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> VehicleEmptyText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> VehiclePickerStatus;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> StandbyFilter;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> AllFilter;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UEditableTextBox> SquadNameInput;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> SquadIconImage;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ListTitle;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CurrentTileText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> EmptyListText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SquadLocationText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> MemberCountText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ErrorText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> VehicleText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> VehicleHint;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidget> SelectionPrompt;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> TransferConfirmPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TransferConfirmText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> ConfirmUnitTransferButton;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UBasicButtonWidget> CancelUnitTransferButton;
private:
    enum class ETransition : uint8 { None, EnterRoom, ExitRoom, ChangeLayerOut, ChangeLayerIn };
    ETransition Transition = ETransition::None;
    ESquadRoomLayer TargetLayer = ESquadRoomLayer::SquadSelection;
    float TransitionTime = 0;
    FVector2D ExitOffsets[3];
    float ExitOpacities[3] = {1.f, 1.f, 1.f};
    bool bClosing = false;
    bool bRefreshPending = false;
    bool bVehicleOptionsDirty = true;
    bool bNameDirty = false;
    bool bWritingName = false;
    // Invalidates continuations after synchronous manager/delegate callbacks replace or close a draft.
    uint64 DraftGeneration = 0;
    FGuid PendingTransferUnitId;
    FGuid PendingTransferSourceSquadId;
    FName PendingTransferTileId;
    uint64 PendingTransferGeneration = 0;
    TMap<FGuid, FGuid> ConfirmedTransferSources;
    // These snapshots contain member IDs only; rows/cards always bind canonical shared FUnitData.
    UPROPERTY(Transient) FSquadData DraftSquad;
    UPROPERTY(Transient) FSquadData OriginalSquad;
    bool bPlayingWidgetTransition = false;
    int32 PendingPanelAnimations = 0;
    UPROPERTY(Transient) TArray<TObjectPtr<UWidgetAnimation>> PlayingPanelAnimations;
    FWidgetAnimationDynamicEvent PanelAnimationFinishedDelegate;
    UPROPERTY(Transient) TObjectPtr<UPlayerSquadManagerBase> SquadManager;
    UPROPERTY(Transient) TObjectPtr<UPlayerUnitManagerBase> UnitManager;
    UPROPERTY(Transient) TArray<TObjectPtr<USelectionButtonWidget>> SquadRows;
    UPROPERTY(Transient) TArray<TObjectPtr<UPersonnelListEntryWidget>> PersonnelRows;
    UPROPERTY(Transient) TArray<TObjectPtr<USquadMemberCardWidget>> MemberCards;
    UPROPERTY(Transient) TArray<TObjectPtr<USelectionButtonWidget>> IconButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> PresetIcons;
    // Runtime choices point to the player unit manager's canonical vehicle allocations.
    TArray<TSharedPtr<FVehicleData>> AvailableVehicles;
    UPROPERTY(Transient) FName VehicleListTileId;
    TMap<FGuid, int32> AvailableVehicleIndices;
    UPROPERTY(Transient) TArray<TObjectPtr<USquadVehicleEntryWidget>> VehicleRows;
    UPROPERTY(Transient) TArray<FGuid> CapacityRemovedUnitIds;
    bool BindManagers();
    void ReleaseManagers();
    void ReleaseRows();
    void RefreshView();
    void RefreshIcons();
    void RefreshVehicleOptions();
    void RefreshAvailableVehicleData();
    void ReleaseVehicleRows();
    void CancelVehiclePendingActions();
    bool CanSelectVehicle(const FVehicleData& Vehicle, const TSet<FGuid>& OccupiedVehicles, FText& OutStatus) const;
    FText GetVehicleCapacityNotice() const;
    void RefreshDraftHeader();
    void UpdateIconPickerPlacement();
    void DiscardDraft();
    void CancelDraftPendingActions();
    UTexture2D* GetDraftIcon() const;
    bool CanEditSelectedSquad(FSquadData& OutSquad);
    bool RequestUnitTransfer(FGuid UnitId, const FSquadData& SourceSquad);
    bool ValidatePendingUnitTransfer(FText& OutError) const;
    void RefreshPendingUnitTransfer();
    void DismissPendingUnitTransfer(bool bRestoreFocus);
    bool Fail(const FText& Error);
    void ClearError();
    void BeginLayerChange(ESquadRoomLayer Layer);
    void UpdateTransition(float DeltaTime);
    void FinishTransition();
    bool TryPlayWidgetTransition(bool bEntering);
    void CancelPanelAnimations(bool bStop = true);
    UFUNCTION() void HandlePanelAnimationFinished();
    void ApplyLayerVisibility();
    void SetPanelsEnabled(bool bEnabled);
    UFUNCTION() void HandleSquadChanged(FGuid SquadId);
    UFUNCTION() void HandleUnitChanged(FGuid UnitId);
    UFUNCTION() void HandleVehicleChanged(FGuid VehicleId);
    UFUNCTION() void HandleSquadChoice(FName Id);
    UFUNCTION() void HandlePersonnelChoice(FName Id);
    UFUNCTION() void HandleConfirmUnitTransfer();
    UFUNCTION() void HandleCancelUnitTransfer();
    UFUNCTION() void HandleFilter(FName Id);
    UFUNCTION() void HandleIcon(FName Id);
    UFUNCTION() void HandleCreateSquad();
    UFUNCTION() void HandleBack();
    UFUNCTION() void HandleSaveName();
    UFUNCTION() void HandleSaveSquad();
    UFUNCTION() void HandleChooseIcon();
    UFUNCTION() void HandleCloseIconPicker();
    UFUNCTION() void HandleUseCaptain();
    UFUNCTION() void HandleChooseVehicle();
    UFUNCTION() void HandleVehicleChoice(FName Id);
    UFUNCTION() void HandleCloseVehiclePicker();
    UFUNCTION() void HandleClearVehicle();
    UFUNCTION() void HandleRemove(FGuid UnitId);
    UFUNCTION() void HandlePromote(FGuid UnitId);
    UFUNCTION() void HandleNameChanged(const FText& Text);
    UFUNCTION() void HandleNameCommitted(const FText& Text, ETextCommit::Type CommitMethod);
};
