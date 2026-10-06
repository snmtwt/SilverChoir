#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"

#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GridStrategyMapSystem/Data/GSMMapSubsystem.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "GTS_TimeLibrary.h"
#include "GTS_TimeManager.h"
#include "Map/BaseMap/BaseSandboxMap.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsSquadEntryWidget.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "Components/ScrollBox.h"
#include "Kismet/GameplayStatics.h"

UOperationsCommandRoomWidget::UOperationsCommandRoomWidget(const FObjectInitializer& Initializer)
    : Super(Initializer)
{
    SceneTitle = NSLOCTEXT("BaseSceneUI", "OperationsCommandRoom", "作战指挥室");
    DateFormat = NSLOCTEXT("OperationsCommandRoom", "DateFormat", "{Year}.{Month}.{Day}");
    ClockFormat = FText::FromString(TEXT("{Hour}:{Minute}"));
}

void UOperationsCommandRoomWidget::RefreshSpeedLabels()
{
    if (NormalSpeedButton) NormalSpeedButton->SetButtonText(FText::GetEmpty());
    if (PauseButton) PauseButton->SetButtonText(FText::GetEmpty());
    if (FastForwardButton)
    {
        const auto Values = ValidFastMultipliers();
        const double Scale = TimeManager.IsValid() && NormalTimeScale > 0 ? TimeManager->GetTimeScale() / NormalTimeScale : 0.;
        const double Shown = Values.ContainsByPredicate([&](double V){ return FMath::IsNearlyEqual(V, Scale); }) ? Scale : (Values.IsEmpty() ? 4. : Values[0]);
        FastForwardButton->SetButtonText(FText::Format(FText::FromString(TEXT("{0}×")), FText::AsNumber(Shown)));
    }
}

void UOperationsCommandRoomWidget::NativeDestruct()
{
    ReleaseCommandRoomUI();
    Super::NativeDestruct();
}

void UOperationsCommandRoomWidget::PrepareCommandRoomUI()
{
    ReleaseCommandRoomUI();
    bSceneActive = true;
    BindRuntime();
    // Initial presentation is an explicit Blueprint flow after this node returns.
    bDispatchEvents = true;
}

void UOperationsCommandRoomWidget::ReleaseCommandRoomUI()
{
    bDispatchEvents = false;
    bSceneActive = false;
    if (NormalSpeedButton) NormalSpeedButton->CancelPendingClick();
    if (FastForwardButton) FastForwardButton->CancelPendingClick();
    if (PauseButton) PauseButton->CancelPendingClick();
    if (EnterMapButton) EnterMapButton->CancelPendingClick();
    const bool bResumeRoomPause = bOwnsWorldPause;
    ReleaseWorldPause();
    if (bResumeRoomPause && TimeManager.IsValid()) TimeManager->SetTimePaused(false);
    for (const auto& Row : SquadEntries) if (Row) Row->OnExpansionRequested.RemoveDynamic(this, &ThisClass::HandleExpandSquad);
    if (SquadList) SquadList->ClearChildren();
    SquadEntries.Reset(); ExpandedSquadId.Invalidate(); DisplayedTileId = NAME_None;
    UnbindRuntime();
}

void UOperationsCommandRoomWidget::BindRuntime()
{
    SquadManager = UPlayerSquadLibrary::GetPlayerSquadManager(this);
    UnitManager = UPlayerUnitLibrary::GetPlayerUnitManager(this);
    if (SquadManager) SquadManager->OnSquadChanged.AddUniqueDynamic(this, &ThisClass::HandleSquadDataChanged);
    if (UnitManager)
    {
        UnitManager->OnUnitDataChanged.AddUniqueDynamic(this, &ThisClass::HandleSquadDataChanged);
        UnitManager->OnVehicleDataChanged.AddUniqueDynamic(this, &ThisClass::HandleSquadDataChanged);
    }
    TimeManager = UGTS_TimeLibrary::GetTimeManager(this);
    if (UGTS_TimeManager* Manager = TimeManager.Get())
    {
        Manager->OnTimeChanged.AddUniqueDynamic(this, &ThisClass::HandleTimeChanged);
        Manager->OnTimeScaleChanged.AddUniqueDynamic(this, &ThisClass::HandleTimeScaleChanged);
        Manager->OnTimePausedChanged.AddUniqueDynamic(this, &ThisClass::HandleTimePausedChanged);
        UGTS_TimeLibrary::RegisterTimeTextBlock(this, DateText, DateFormat);
        UGTS_TimeLibrary::RegisterTimeTextBlock(this, ClockText, ClockFormat);
    }
    if (UGameInstance* Instance = GetGameInstance())
    {
        MapSubsystem = Instance->GetSubsystem<UGSMMapSubsystem>();
        if (UGSMMapSubsystem* Subsystem = MapSubsystem.Get())
        {
            Subsystem->OnActiveMapChanged.AddUniqueDynamic(this, &ThisClass::HandleActiveMapChanged);
            HandleActiveMapChanged(Subsystem->GetActiveGridStrategyMap());
        }
    }
    // A placed independent sandbox can already exist before the room opens.
    if (!CurrentMap.IsValid() && GetWorld())
    {
        for (TActorIterator<ABaseSandboxMap> It(GetWorld()); It; ++It)
        {
            if (!It->IsActorBeingDestroyed() && !It->IsHidden())
            {
                BindMap(*It);
                break;
            }
        }
    }
}

void UOperationsCommandRoomWidget::UnbindRuntime()
{
    if (SquadManager) SquadManager->OnSquadChanged.RemoveDynamic(this, &ThisClass::HandleSquadDataChanged);
    if (UnitManager)
    {
        UnitManager->OnUnitDataChanged.RemoveDynamic(this, &ThisClass::HandleSquadDataChanged);
        UnitManager->OnVehicleDataChanged.RemoveDynamic(this, &ThisClass::HandleSquadDataChanged);
    }
    SquadManager = nullptr; UnitManager = nullptr;
    if (UGTS_TimeManager* Manager = TimeManager.Get())
    {
        Manager->OnTimeChanged.RemoveDynamic(this, &ThisClass::HandleTimeChanged);
        Manager->OnTimeScaleChanged.RemoveDynamic(this, &ThisClass::HandleTimeScaleChanged);
        Manager->OnTimePausedChanged.RemoveDynamic(this, &ThisClass::HandleTimePausedChanged);
    }
    UGTS_TimeLibrary::UnregisterTimeTextBlock(this, DateText);
    UGTS_TimeLibrary::UnregisterTimeTextBlock(this, ClockText);
    TimeManager.Reset();
    if (UGSMMapSubsystem* Subsystem = MapSubsystem.Get())
        Subsystem->OnActiveMapChanged.RemoveDynamic(this, &ThisClass::HandleActiveMapChanged);
    MapSubsystem.Reset();
    BindMap(nullptr);
}

void UOperationsCommandRoomWidget::BindMap(AGSMMap3D* Map)
{
    if (AGSMMap3D* Previous = CurrentMap.Get())
        Previous->OnSelectedTileChanged.RemoveDynamic(this, &ThisClass::HandleSelectedTileChanged);
    CurrentMap = IsValid(Map) ? Map : nullptr;
    if (AGSMMap3D* BoundMap = CurrentMap.Get())
        BoundMap->OnSelectedTileChanged.AddUniqueDynamic(this, &ThisClass::HandleSelectedTileChanged);
    RefreshTileSelection();
}

void UOperationsCommandRoomWidget::HandleActiveMapChanged(AGSMMap3D* Map)
{
    if (!bSceneActive) return;
    ABaseSandboxMap* Sandbox = Cast<ABaseSandboxMap>(Map);
    BindMap(bSceneActive && IsValid(Sandbox) && Sandbox->GetWorld() == GetWorld()
        && !Sandbox->IsActorBeingDestroyed() ? Sandbox : nullptr);
}

void UOperationsCommandRoomWidget::RefreshTileSelection()
{
    AGSMMap3D* Map = bSceneActive ? CurrentMap.Get() : nullptr;
    AGSMTile3D* Tile = Map ? Map->GetSelectedTile() : nullptr;
    UGSMTileData* NewData = IsValid(Tile) && !Tile->IsActorBeingDestroyed() ? Tile->GetTileData() : nullptr;
    const bool bChanged = SelectedTileData.Get() != NewData;
    SelectedTileData = NewData;
    if (bChanged && bSceneActive && bDispatchEvents)
    {
        OnCommandTileSelectionChanged();
        if (bSceneActive && bDispatchEvents) OnSelectedTileChanged.Broadcast();
    }
}

void UOperationsCommandRoomWidget::HandleSelectedTileChanged()
{
    if (bSceneActive) RefreshTileSelection();
}
bool UOperationsCommandRoomWidget::HasSelectedTile() const { return IsValid(SelectedTileData); }
UGSMTileData* UOperationsCommandRoomWidget::GetSelectedTile() const { return IsValid(SelectedTileData) ? SelectedTileData.Get() : nullptr; }

void UOperationsCommandRoomWidget::ClearSelectedTile()
{
    if (bSceneActive)
        if (AGSMMap3D* Map = CurrentMap.Get()) Map->ClearSelectedTile();
    RefreshTileSelection();
}

bool UOperationsCommandRoomWidget::ApplyTimeScale(double Scale)
{
    UGTS_TimeManager* Manager = TimeManager.Get();
    if (!bSceneActive || !GetIsEnabled() || !Manager || !Manager->IsInitialized()
        || !FMath::IsFinite(Scale) || Scale <= 0.0 || !Manager->SetTimeScale(Scale)) return false;
    Manager->SetTimePaused(false);
    ReleaseWorldPause();
    return true;
}

bool UOperationsCommandRoomWidget::NormalSpeed() { return ApplyTimeScale(NormalTimeScale); }
bool UOperationsCommandRoomWidget::FastForward()
{
    const auto Values = ValidFastMultipliers();
    if (Values.IsEmpty() || !TimeManager.IsValid() || !FMath::IsFinite(NormalTimeScale) || NormalTimeScale <= 0) return false;
    int32 Index = INDEX_NONE;
    if (!TimeManager->IsTimePaused())
        Index = Values.IndexOfByPredicate([&](double V){return FMath::IsNearlyEqual(TimeManager->GetTimeScale(), NormalTimeScale * V);});
    return ApplyTimeScale(NormalTimeScale * Values[(Index + 1) % Values.Num()]);
}
TArray<double> UOperationsCommandRoomWidget::ValidFastMultipliers() const
{
    TArray<double> Values;
    for (double V : FastForwardMultipliers) if (FMath::IsFinite(V) && V > 1.) Values.AddUnique(V);
    return Values;
}
bool UOperationsCommandRoomWidget::PauseTime()
{
    if (!bSceneActive || !GetIsEnabled() || !TimeManager.IsValid() || !TimeManager->IsInitialized()) return false;
    TimeManager->SetTimePaused(true);
    if (!UGameplayStatics::IsGamePaused(this)) bOwnsWorldPause = UGameplayStatics::SetGamePaused(this, true);
    return true;
}
void UOperationsCommandRoomWidget::ReleaseWorldPause()
{
    if (bOwnsWorldPause) UGameplayStatics::SetGamePaused(this, false);
    bOwnsWorldPause = false;
}
void UOperationsCommandRoomWidget::HandleTimeScaleChanged(double Scale)
{
    if (bSceneActive && bDispatchEvents) OnCommandTimeScaleChanged(Scale);
}
void UOperationsCommandRoomWidget::HandleTimePausedChanged(bool bPaused)
{
    if (bSceneActive && bDispatchEvents) OnCommandTimePausedChanged(bPaused);
}

void UOperationsCommandRoomWidget::HandleTimeChanged(FDateTime CurrentTime)
{
    if (bSceneActive && bDispatchEvents) OnCommandTimeChanged(CurrentTime);
}

void UOperationsCommandRoomWidget::RefreshTimeDisplay()
{
    UGTS_TimeManager* Manager = TimeManager.Get();
    if (bSceneActive && Manager && Manager->IsInitialized())
    {
        // DateText and ClockText stay registered with GameTimeSystem; only the progress needs a UI action.
        if (DayProgress) DayProgress->SetPercent(static_cast<float>(Manager->GetCurrentTime().GetTimeOfDay().GetTotalSeconds() / 86400.0));
    }
    else
    {
        if (DateText) DateText->SetText(NSLOCTEXT("OperationsCommandRoom", "UnavailableDate", "----.--.--"));
        if (ClockText) ClockText->SetText(NSLOCTEXT("OperationsCommandRoom", "UnavailableClock", "--:--:--"));
        if (DayProgress) DayProgress->SetPercent(0.0f);
    }
}

void UOperationsCommandRoomWidget::RefreshTimeControlState()
{
    RefreshSpeedLabels();
    UGTS_TimeManager* Manager = TimeManager.Get();
    const bool bReady = bSceneActive && Manager && Manager->IsInitialized();
    const bool bPaused = bReady && Manager->IsTimePaused();
    const double Scale = bReady ? Manager->GetTimeScale() : 0.0;
    if (NormalSpeedButton)
    {
        NormalSpeedButton->SetIsEnabled(bReady);
        NormalSpeedButton->SetSelected(bReady && !bPaused && FMath::IsNearlyEqual(Scale, NormalTimeScale));
    }
    if (FastForwardButton)
    {
        FastForwardButton->SetIsEnabled(bReady && !ValidFastMultipliers().IsEmpty());
        FastForwardButton->SetSelected(bReady && !bPaused && ValidFastMultipliers().ContainsByPredicate([&](double V){return FMath::IsNearlyEqual(Scale, NormalTimeScale * V);}));
    }
    if (PauseButton) { PauseButton->SetIsEnabled(bReady); PauseButton->SetSelected(bPaused); }
    if (TimeStateText)
    {
        FText State;
        if (!bReady) State = NSLOCTEXT("OperationsCommandRoom", "ClockUnavailable", "时间不可用");
        else if (bPaused) State = NSLOCTEXT("OperationsCommandRoom", "ClockPaused", "已暂停");
        else if (Scale == 0.0) State = NSLOCTEXT("OperationsCommandRoom", "ClockStopped", "时间停滞");
        else State = FText::Format(NSLOCTEXT("OperationsCommandRoom", "ClockSpeed", "时间流速 ×{0}"), FText::AsNumber(Scale));
        TimeStateText->SetText(State);
    }
}

int32 UOperationsCommandRoomWidget::GetSelectedTileSquadCount() const
{
    if (!bSceneActive || !SelectedTileData || !SquadManager) return 0;
    int32 Count = 0;
    for (const auto& Squad : SquadManager->GetAllSquads()) if (Squad.TileId == SelectedTileData->GetTileId()) ++Count;
    return Count;
}
void UOperationsCommandRoomWidget::RefreshSquadList()
{
    const FName TileId = bSceneActive && SelectedTileData ? SelectedTileData->GetTileId() : NAME_None;
    if (TileId != DisplayedTileId)
    {
        ExpandedSquadId.Invalidate(); DisplayedTileId = TileId;
        if (SquadList) SquadList->ScrollToStart();
    }
    TArray<FSquadData> Squads;
    if (!TileId.IsNone() && SquadManager)
        for (const auto& Squad : SquadManager->GetAllSquads()) if (Squad.TileId == TileId) Squads.Add(Squad);
    if (!Squads.ContainsByPredicate([&](const auto& S){return S.SquadId == ExpandedSquadId;})) ExpandedSquadId.Invalidate();
    if (TileIdText) TileIdText->SetText(FText::FromName(TileId));
    if (SquadCountText) SquadCountText->SetText(FText::FromString(FString::Printf(TEXT("%d 支"), Squads.Num())));
    if (EmptySquadsText) EmptySquadsText->SetVisibility(Squads.IsEmpty() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    if (EnterMapButton) EnterMapButton->SetIsEnabled(bSceneActive && !Squads.IsEmpty());
    if (!SquadList || !SquadEntryClass) return;
    // Reuse live rows so data notifications do not restart an in-progress expansion.
    const auto Old = SquadEntries;
    SquadEntries.Reset();
    for (const auto& Squad : Squads)
    {
        const auto* Existing = Old.FindByPredicate([&](const auto& Row){return Row && Row->SquadId == Squad.SquadId;});
        auto* Row = Existing ? Existing->Get() : CreateWidget<UOperationsSquadEntryWidget>(GetOwningPlayer(), SquadEntryClass);
        if (!Row) continue;
        if (Row->GetParent() != SquadList) SquadList->AddChild(Row);
        Row->SetSquad(SquadManager, Squad);
        Row->SetExpanded(ExpandedSquadId == Squad.SquadId, Existing != nullptr);
        Row->OnExpansionRequested.AddUniqueDynamic(this, &ThisClass::HandleExpandSquad);
        SquadEntries.Add(Row);
    }
    for (const auto& Row : Old) if (Row && !SquadEntries.Contains(Row))
    { Row->OnExpansionRequested.RemoveDynamic(this, &ThisClass::HandleExpandSquad); Row->RemoveFromParent(); }
}
void UOperationsCommandRoomWidget::HandleSquadDataChanged(FGuid Id)
{ if (bSceneActive && bDispatchEvents) OnCommandSquadsChanged(); }
void UOperationsCommandRoomWidget::HandleExpandSquad(FGuid Id)
{
    if (!bSceneActive || !GetIsEnabled()) return;
    ExpandedSquadId = ExpandedSquadId == Id ? FGuid() : Id;
    for (const auto& Row : SquadEntries) if (Row) Row->SetExpanded(Row->SquadId == ExpandedSquadId);
}
bool UOperationsCommandRoomWidget::RequestEnterSelectedTile()
{
    if (!bSceneActive || !GetIsEnabled() || !SelectedTileData || !SquadManager) return false;
    const FName Tile = SelectedTileData->GetTileId();
    TArray<FGuid> Ids;
    for (const auto& Squad : SquadManager->GetAllSquads()) if (Squad.TileId == Tile) Ids.Add(Squad.SquadId);
    if (Ids.IsEmpty()) { RefreshSquadList(); return false; }
    OnEnterTileMapRequested(Tile, Ids);
    return true;
}

bool UOperationsCommandRoomWidget::IsScreenPositionOverCommandPanel(const FVector2D& Position) const
{
    if (!bSceneActive || !GetIsEnabled() || !IsVisible() || !RightPanel) return false;
    const auto ContainsPosition = [&Position](const UPanelWidget* Panel)
    {
        if (!Panel) return false;
        for (const UWidget* Widget = Panel; Widget; Widget = Widget->GetParent())
        {
            if (!Widget->GetIsEnabled() || !Widget->IsVisible()) return false;
        }
        const FGeometry& Geometry = Panel->GetCachedGeometry();
        return Geometry.GetLocalSize().X > 0.0f && Geometry.GetLocalSize().Y > 0.0f
            && Geometry.IsUnderLocation(Position);
    };
    return ContainsPosition(TimeControlPanel) || ContainsPosition(TileInfoPanel);
}

bool UOperationsCommandRoomWidget::IsPointerOverCommandPanel() const
{
    return FSlateApplication::IsInitialized()
        && IsScreenPositionOverCommandPanel(FSlateApplication::Get().GetCursorPos());
}

FReply UOperationsCommandRoomWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    return IsScreenPositionOverCommandPanel(Event.GetScreenSpacePosition()) ? FReply::Handled() : FReply::Unhandled();
}
FReply UOperationsCommandRoomWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
    return IsScreenPositionOverCommandPanel(Event.GetScreenSpacePosition()) ? FReply::Handled() : FReply::Unhandled();
}
FReply UOperationsCommandRoomWidget::NativeOnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event)
{
    return IsScreenPositionOverCommandPanel(Event.GetScreenSpacePosition()) ? FReply::Handled() : FReply::Unhandled();
}
FReply UOperationsCommandRoomWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
    return IsScreenPositionOverCommandPanel(Event.GetScreenSpacePosition()) ? FReply::Handled() : FReply::Unhandled();
}
