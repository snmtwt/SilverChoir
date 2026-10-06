#pragma once
#include "CoreMinimal.h"
#include "Map/BaseMap/SceneUI/BaseSceneWidget.h"
#include "OperationsCommandRoomWidget.generated.h"

class AGSMMap3D;
class UGSMMapSubsystem;
class UGSMTileData;
class UGTS_TimeManager;
class UPanelWidget;
class UProgressBar;
class USelectionButtonWidget;
class UTextBlock;
class UScrollBox;
class UOperationsSquadEntryWidget;
class UPlayerSquadManagerBase;
class UPlayerUnitManagerBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOperationsTileSelectionChanged);

/** 右侧时间控制和选中瓦片信息容器；布局及信息内容由子蓝图扩展。 */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="作战指挥室 UI"))
class SILVERCHOIR_API UOperationsCommandRoomWidget : public UBaseSceneWidget
{
    GENERATED_BODY()
public:
    UOperationsCommandRoomWidget(const FObjectInitializer& Initializer);

    /** Bind data silently. The Blueprint owns initial display, animation and completion notification. */
    UFUNCTION(BlueprintCallable, Category="作战指挥室|流程", meta=(DisplayName="准备作战室数据订阅"))
    void PrepareCommandRoomUI();
    /** Disable event delivery before releasing subscriptions; does not notify UI unload completion. */
    UFUNCTION(BlueprintCallable, Category="作战指挥室|流程", meta=(DisplayName="释放作战室数据订阅"))
    void ReleaseCommandRoomUI();
    UFUNCTION(BlueprintCallable, Category="作战指挥室|显示", meta=(DisplayName="刷新作战室时间显示"))
    void RefreshTimeDisplay();
    UFUNCTION(BlueprintCallable, Category="作战指挥室|显示", meta=(DisplayName="刷新作战室时间控制状态"))
    void RefreshTimeControlState();
    UFUNCTION(BlueprintCallable, Category="作战指挥室|显示", meta=(DisplayName="刷新时间按钮文字"))
    void RefreshSpeedLabels();

    UFUNCTION(BlueprintImplementableEvent, Category="作战指挥室|事件", meta=(DisplayName="作战室游戏时间变化"))
    void OnCommandTimeChanged(FDateTime CurrentTime);
    UFUNCTION(BlueprintImplementableEvent, Category="作战指挥室|事件", meta=(DisplayName="作战室时间倍率变化"))
    void OnCommandTimeScaleChanged(double Scale);
    UFUNCTION(BlueprintImplementableEvent, Category="作战指挥室|事件", meta=(DisplayName="作战室时间暂停变化"))
    void OnCommandTimePausedChanged(bool bPaused);
    UFUNCTION(BlueprintImplementableEvent, Category="作战指挥室|事件", meta=(DisplayName="作战室选中瓦片变化"))
    void OnCommandTileSelectionChanged();
    UFUNCTION(BlueprintImplementableEvent, Category="作战指挥室|事件") void OnCommandSquadsChanged();
    /** Extension point only: the project's tile -> battle-map routing is authored in Blueprint. */
    UFUNCTION(BlueprintImplementableEvent, Category="作战指挥室|事件", meta=(DisplayName="请求进入当前瓦片地图"))
    void OnEnterTileMapRequested(FName TileId, const TArray<FGuid>& SquadIds);
    UFUNCTION(BlueprintCallable, Category="作战指挥室|地图") void RefreshSquadList();
    UFUNCTION(BlueprintCallable, Category="作战指挥室|地图") bool RequestEnterSelectedTile();
    UFUNCTION(BlueprintPure, Category="作战指挥室|地图") int32 GetSelectedTileSquadCount() const;
    UFUNCTION(BlueprintCallable, Category="作战指挥室|时间") bool PauseTime();
    /** Releases only a world pause acquired by this panel, so scene camera transitions can finish. */
    UFUNCTION(BlueprintCallable, Category="作战指挥室|时间") void ReleaseWorldPause();
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="作战指挥室|时间", meta=(ClampMin="1", DisplayName="正常时间流速")) double NormalTimeScale = 60.0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="作战指挥室|时间", meta=(DisplayName="快进循环倍率（相对正常）")) TArray<double> FastForwardMultipliers = {4.,8.,16.};
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="作战指挥室|小队") TSubclassOf<UOperationsSquadEntryWidget> SquadEntryClass;
    UPROPERTY(BlueprintReadOnly, Transient, Category="作战指挥室|小队") FGuid ExpandedSquadId;

    /** Legacy serialized value; current controls use FastForwardMultipliers. */
    UPROPERTY()
    double FastForwardScale = 4.0;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="作战指挥室|时间", meta=(DisplayName="日期格式"))
    FText DateFormat;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="作战指挥室|时间", meta=(DisplayName="时钟格式"))
    FText ClockFormat;

    UFUNCTION(BlueprintCallable, Category="作战指挥室|时间", meta=(DisplayName="正常推进时间"))
    bool NormalSpeed();
    UFUNCTION(BlueprintCallable, Category="作战指挥室|时间", meta=(DisplayName="快进时间"))
    bool FastForward();
    UFUNCTION(BlueprintCallable, Category="作战指挥室|地图", meta=(DisplayName="清除选中瓦片"))
    void ClearSelectedTile();
    UFUNCTION(BlueprintPure, Category="作战指挥室|地图")
    bool HasSelectedTile() const;
    UFUNCTION(BlueprintPure, Category="作战指挥室|地图")
    UGSMTileData* GetSelectedTile() const;
    UFUNCTION(BlueprintPure, Category="作战指挥室|输入")
    bool IsPointerOverCommandPanel() const;

    /** Read GetSelectedTile when notified; the container intentionally supplies no placeholder data. */
    UPROPERTY(BlueprintAssignable, Category="作战指挥室|地图")
    FOperationsTileSelectionChanged OnSelectedTileChanged;

    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> RightPanel;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> TimeControlPanel;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> TileInfoPanel;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> TileInfoContent;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> DateText;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ClockText;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TimeStateText;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UProgressBar> DayProgress;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> NormalSpeedButton;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> FastForwardButton;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> PauseButton;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<USelectionButtonWidget> EnterMapButton;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UScrollBox> SquadList;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TileIdText;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SquadCountText;
    UPROPERTY(BlueprintReadOnly, Category="作战指挥室|绑定", meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> EmptySquadsText;

protected:
    virtual void NativeDestruct() override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;

private:
    void BindRuntime();
    void UnbindRuntime();
    void BindMap(AGSMMap3D* Map);
    void RefreshTileSelection();
    bool ApplyTimeScale(double Scale);
    bool IsScreenPositionOverCommandPanel(const FVector2D& Position) const;
    UFUNCTION() void HandleTimeChanged(FDateTime CurrentTime);
    UFUNCTION() void HandleTimeScaleChanged(double Scale);
    UFUNCTION() void HandleTimePausedChanged(bool bPaused);
    UFUNCTION() void HandleActiveMapChanged(AGSMMap3D* Map);
    UFUNCTION() void HandleSelectedTileChanged();
    UFUNCTION() void HandleSquadDataChanged(FGuid Id);
    UFUNCTION() void HandleExpandSquad(FGuid Id);
    TArray<double> ValidFastMultipliers() const;
    UPROPERTY(Transient) TObjectPtr<UPlayerSquadManagerBase> SquadManager;
    UPROPERTY(Transient) TObjectPtr<UPlayerUnitManagerBase> UnitManager;
    UPROPERTY(Transient) TArray<TObjectPtr<UOperationsSquadEntryWidget>> SquadEntries;
    FName DisplayedTileId;
    bool bOwnsWorldPause = false;

    UPROPERTY(Transient) TWeakObjectPtr<UGTS_TimeManager> TimeManager;
    UPROPERTY(Transient) TWeakObjectPtr<UGSMMapSubsystem> MapSubsystem;
    UPROPERTY(Transient) TWeakObjectPtr<AGSMMap3D> CurrentMap;
    UPROPERTY(Transient) TObjectPtr<UGSMTileData> SelectedTileData;
    bool bSceneActive = false;
    bool bDispatchEvents = false;
};
