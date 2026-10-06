#pragma once
#include "CoreMinimal.h"
#include "Data/Units/UnitStructs.h"
#include "Map/BaseMap/SceneUI/BaseSceneWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/PersonnelViewData.h"
#include "PersonnelPreparationRoomWidget.generated.h"

class UPlayerUnitManagerBase;
class UWarehousePageSession;
class USIS_InventorySlotContainer;
class USelectionButtonWidget;
class UTextBlock;
class UScrollBox;
class UWidgetSwitcher;
class UProgressBar;
class UPanelWidget;
class UPersonnelPortraitWidget;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPersonnelSelected, FName, PersonnelID);

/** 人员整备室：在子蓝图中制作内容布局与业务逻辑。 */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="人员整备室 UI"))
class SILVERCHOIR_API UPersonnelPreparationRoomWidget : public UBaseSceneWidget
{
    GENERATED_BODY()
public:
    /** 初次加载当前页；已有会话时不覆盖实时库存。C++ 切页流程的准备入口。 */
    UFUNCTION(BlueprintCallable, Category="人员|仓库", meta=(DisplayName="初始化仓库会话")) bool InitializeWarehouseSession();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="人员|仓库|拖拽", meta=(ClampMin="0.1")) float DragPageHoverSeconds=0.5f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="人员|仓库|拖拽", meta=(ClampMin="0")) float DragPageCursorTolerance=4.f;
    /** 页索引 0—4 对应按钮 01—05。C++ 保存旧页并加载新页，成功后通知蓝图；相同页/失败返回 false。 */
    UFUNCTION(BlueprintCallable, Category="人员|仓库", meta=(DisplayName="切换库存页")) bool SelectInventoryPage(int32 PageIndex);
    UPROPERTY(BlueprintReadOnly, Transient, Category="人员|仓库") int32 CurrentInventoryPage=0;
    /** 仅供表现层响应。此时数据已切换，不能在此重复保存旧页或加载新页。 */
    UFUNCTION(BlueprintImplementableEvent, Category="人员|仓库", meta=(DisplayName="库存页已切换")) void OnInventoryPageChanged(int32 PageIndex, USIS_InventorySlotContainer* InventoryContainer, int32 PreviousPageIndex);
    /** 初始化/恢复选择，不发出切换事件，也不读写物品。 */
    UFUNCTION(BlueprintCallable, Category="人员|仓库", meta=(DisplayName="恢复库存页选择")) bool RestoreInventoryPageSelection(int32 PageIndex);
    UFUNCTION(BlueprintPure, Category="人员|仓库", meta=(DisplayName="获取仓库库存容器")) USIS_InventorySlotContainer* GetWarehouseInventoryContainer() const;
    UPersonnelPreparationRoomWidget(const FObjectInitializer& Initializer) : Super(Initializer)
    {
        SceneTitle = NSLOCTEXT("BaseSceneUI", "PersonnelPreparationRoom", "人员整备室");
    }
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="人员") TSubclassOf<USelectionButtonWidget> PersonnelRowClass;
    UPROPERTY(Transient, BlueprintReadOnly, Category="人员") TArray<FPersonnelViewData> PersonnelData;
    UPROPERTY(Transient, BlueprintReadOnly, Category="人员") FName CurrentTileId;
    /** None 不匹配待命单位；全部仍显示所有位置。只加载左侧列表。 */
    UFUNCTION(BlueprintCallable, Category="人员", meta=(DisplayName="加载人员列表"))
    bool LoadPersonnelList(FName TileId);
    /** 查询并绑定共享单位数据；失败时清空旧档案。 */
    UFUNCTION(BlueprintCallable, Category="人员|档案", meta=(DisplayName="绑定人员档案"))
    bool BindPersonnelProfile(FGuid UnitId);
    UFUNCTION(BlueprintPure, Category="人员", meta=(DisplayName="获取选中单位ID"))
    FGuid GetSelectedUnitId() const;
    UPROPERTY(BlueprintReadOnly, Transient, Category="人员") FName SelectedPersonnelID;
    UPROPERTY(BlueprintReadOnly, Transient, Category="人员") EPersonnelRosterFilter CurrentFilter = EPersonnelRosterFilter::Standby;
    UPROPERTY(BlueprintReadOnly, Transient, Category="人员") EPersonnelDetailPage CurrentPage = EPersonnelDetailPage::Profile;
    UPROPERTY(BlueprintReadOnly, Transient, Category="人员|列表") EPersonnelListDisplay CurrentListDisplay = EPersonnelListDisplay::Personnel;
    UPROPERTY(BlueprintReadOnly, Transient, Category="人员|列表") bool bListTransitioning = false;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="人员|动画") FName PersonnelEnterAnimation = TEXT("人员列表滑入");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="人员|动画") FName PersonnelExitAnimation = TEXT("人员列表滑出");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="人员|动画") FName WarehouseEnterAnimation = TEXT("仓库滑入");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="人员|动画") FName WarehouseExitAnimation = TEXT("仓库滑出");
    UFUNCTION(BlueprintPure, Category="人员|动画", meta=(DisplayName="获取当前列表动画"))
    UWidgetAnimation* GetCurrentListAnimation(bool bEntering) const;
    /** Called by each panel animation's Finished pin; true once both panels have finished. */
    UFUNCTION(BlueprintCallable, Category="人员|动画", meta=(DisplayName="记录面板动画完成"))
    bool RecordPanelAnimationFinished(bool bListPanel, bool bEntering);
    UPROPERTY(BlueprintAssignable, Category="人员") FPersonnelSelected OnPersonnelSelected;
    UFUNCTION(BlueprintImplementableEvent, Category="人员", meta=(DisplayName="人员被点击"))
    void OnPersonnelClicked(FGuid UnitId);
    /** Empty, input-transparent center slot reserved for a future model preview. */
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="人员") TObjectPtr<UPanelWidget> ModelDisplayArea;
    UFUNCTION(BlueprintCallable, Category="人员") void SetPersonnelData(const TArray<FPersonnelViewData>& Data);
    UFUNCTION(BlueprintCallable, Category="人员") void SetRosterFilter(EPersonnelRosterFilter Filter);
    UFUNCTION(BlueprintCallable, Category="人员") bool SelectPersonnel(FName ID);
    /** 与点击人员列表一致：校验当前瓦片/筛选条件，更新选择后触发“人员被点击”。需先加载人员列表。 */
    UFUNCTION(BlueprintCallable, Category="人员", meta=(DisplayName="根据单位ID选中人员")) bool SelectUnitById(FGuid UnitId);
    UFUNCTION(BlueprintCallable, Category="人员") void SetDetailPage(EPersonnelDetailPage Page);
    UFUNCTION(BlueprintPure, Category="人员") bool GetSelectedPersonnel(FPersonnelViewData& Data) const;
    UFUNCTION(BlueprintPure, Category="人员") int32 GetVisiblePersonnelCount() const;
    virtual void LoadSceneUI_Implementation() override;
    virtual void UnloadSceneUI_Implementation() override;
protected:
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> RosterPanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> WarehousePanel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UPersonnelPortraitWidget> DetailPortrait;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UScrollBox> PersonnelList;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UWidgetSwitcher> DetailPages;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> StandbyFilter;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> AllFilter;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> ProfileTab;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> EquipmentTab;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> TrainingTab;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> RosterCount;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> EmptyRoster;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> DataSourceLabel;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SelectedName;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SelectedMeta;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SelectedStatus;
    UPROPERTY(BlueprintReadOnly, Category="人员|档案", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SelectedAge;
    UPROPERTY(BlueprintReadOnly, Category="人员|档案", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SelectedGender;
    UPROPERTY(BlueprintReadOnly, Category="人员|共用档案", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SelectedLocation;
    UPROPERTY(BlueprintReadOnly, Category="人员|档案", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SelectedNationality;
    UPROPERTY(BlueprintReadOnly, Category="人员|档案", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ActionLogText;
    UPROPERTY(BlueprintReadOnly, Category="人员|共用档案", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SelectedSquad;
    UPROPERTY(BlueprintReadOnly, Category="人员|档案", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> EvaluationText;
    UPROPERTY(BlueprintReadOnly, Category="人员|档案", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ServiceDurationText;
    UPROPERTY(BlueprintReadOnly, Category="人员|档案", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BattleRecordText;
    UPROPERTY(BlueprintReadOnly, Category="人员|档案", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> InjuryRecordText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ProfileText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> BiographyText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> EquipmentText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TrainingText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> HealthText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> MoraleText;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> HealthBar;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> MoraleBar;
private:
    UPROPERTY(Transient) TObjectPtr<UWarehousePageSession> WarehouseSession;
    UFUNCTION() void HandleInventoryPage(FName ChoiceID);
    UPROPERTY(Transient) TArray<TObjectPtr<USelectionButtonWidget>> Rows;
    bool bInitialized = false;
    bool bClosing = false;
    bool bListAnimationFinished = false;
    bool bDetailAnimationFinished = false;
    bool bPanelCompletionReported = false;
    EPersonnelListDisplay RequestedListDisplay = EPersonnelListDisplay::Personnel;
    bool bListSlidingOut = false;
    UPROPERTY(Transient) TObjectPtr<UWidgetAnimation> ListTransitionAnimation;
    void StartListTransition();
    void PlayListTransitionAnimation(bool bEntering);
    void CancelListTransition();
    void ApplyListVisibility();
    UFUNCTION() void HandleListTransitionFinished();
    UPROPERTY(Transient) TObjectPtr<UPlayerUnitManagerBase> UnitManager;
    bool bUseUnitStore = false;
    TMap<FGuid,TSharedPtr<FUnitData>> SharedUnits;
    TMap<FGuid,FDelegateHandle> UnitBindings;
    void ReleaseUnitBindings();
    void HandleSharedUnitChanged(FGuid UnitId);
    virtual void BeginDestroy() override;
    void RebuildUnitViews();
    UFUNCTION() void HandleUnitChanged(FGuid UnitId);
    void RefreshRoster();
    TSharedPtr<FUnitData> ProfileUnitData;
    FDelegateHandle ProfileDataHandle;
    void ReleaseProfileData();
    void HandleProfileDataChanged(FGuid UnitId);
    void RefreshDetails();
    bool MatchesFilter(const FPersonnelViewData& Person) const;
    UFUNCTION() void HandleFilter(FName ID);
    UFUNCTION() void HandlePage(FName ID);
    UFUNCTION() void HandlePerson(FName ID);
};
