#include "Map/BattleMap/BattleUnitSelectionComponent.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "Map/BattleMap/BattleMapWidget.h"
#include "Object/Unit/UnitPawnBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Level.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "MTS_SubMapSubsystem.h"
#include "Misc/ScopeExit.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

class SBattleSelectionRectangle : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SBattleSelectionRectangle) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UBattleUnitSelectionComponent>, Selection)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Selection = Args._Selection; SetVisibility(EVisibility::HitTestInvisible); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle& Style, bool) const override
    {
        auto* Component = Selection.Get();
        FVector2D Start, End;
        if (!Component || !Component->GetSelectionRectangle(Start, End)) return Layer;
        auto* PC = Cast<APlayerController>(Component->GetOwner());
        int32 Width=0, Height=0;
        if (PC) PC->GetViewportSize(Width,Height);
        if (Width<=0 || Height<=0) return Layer;
        const FVector2D Scale = G.GetLocalSize()/FVector2D(Width,Height);
        const FVector2f Min(FMath::Min(Start.X,End.X)*Scale.X,FMath::Min(Start.Y,End.Y)*Scale.Y);
        const FVector2f Max(FMath::Max(Start.X,End.X)*Scale.X,FMath::Max(Start.Y,End.Y)*Scale.Y);
        FSlateDrawElement::MakeBox(Elements,Layer,G.ToPaintGeometry(Max-Min,FSlateLayoutTransform(Min)),
            FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,Component->FillColor*Style.GetColorAndOpacityTint());
        TArray<FVector2f> Points{Min,{Max.X,Min.Y},Max,{Min.X,Max.Y},Min};
        FSlateDrawElement::MakeLines(Elements,Layer+1,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,
            Component->BorderColor*Style.GetColorAndOpacityTint(),true,1.f);
        return Layer+1;
    }
private:
    TWeakObjectPtr<UBattleUnitSelectionComponent> Selection;
};

UBattleUnitSelectionComponent::UBattleUnitSelectionComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.bTickEvenWhenPaused=true;
}
AGameMainMapPlayerController* UBattleUnitSelectionComponent::Controller() const
{ return Cast<AGameMainMapPlayerController>(GetOwner()); }
bool UBattleUnitSelectionComponent::CanIssueWorldCommand(bool bCheckPointer) const
{ return !bSelecting && CanSelectInWorld() && (!bCheckPointer || !IsPointerOverUI()); }
bool UBattleUnitSelectionComponent::CanSelectInWorld() const
{
    const auto* PC=Controller();
    const auto* State=GetWorld()?GetWorld()->GetGameState<AGameMainMapGameState>():nullptr;
    const auto* Sub=GetWorld()?GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>():nullptr;
    return PC && PC->IsLocalController() && State && State->IsBattleMap()
        && !(Sub && Sub->IsSubMapTransitionInProgress())
        && !(PC->BattleWidget && PC->BattleWidget->bTargeting);
}
bool UBattleUnitSelectionComponent::ReadSelectionPointer(FVector2D& OutPosition) const
{
    OutPosition=FVector2D::ZeroVector;
    const auto* PC=Controller();
    float X=0,Y=0;
    if (!PC || !PC->GetMousePosition(X,Y)) return false;
    OutPosition=FVector2D(X,Y);
    return !OutPosition.ContainsNaN();
}
bool UBattleUnitSelectionComponent::IsPointerOverUI() const
{
    const auto* PC=Controller();
    if (!PC || !FSlateApplication::IsInitialized()) return true;
    auto& Slate=FSlateApplication::Get();
    const FVector2D Position=Slate.GetCursorPos();
    const UBattleMapWidget* HUD=PC->BattleWidget;
    if (HUD && HUD->IsInViewport())
    {
        if (auto* Modal=HUD->GetWidgetFromName(TEXT("ModalPanel")); Modal && Modal->IsVisible()) return true;
        // Include authored mode pages as well as the older dock layout.
        for (const FName Name : {FName("DockPanel"),FName("MinimapPanel"),FName("ModePages"),FName("ModeRail"),FName("MemberModeButton"),FName("SquadModeButton")})
            if (const auto* Widget=HUD->GetWidgetFromName(Name); Widget && Widget->IsVisible()
                && Widget->GetCachedGeometry().IsUnderLocation(Position)) return true;
    }
    const FWidgetPath Path=Slate.LocateWindowUnderMouse(Position,Slate.GetInteractiveTopLevelWindows(),true);
    const auto HUDSlate=HUD?HUD->GetCachedWidget():TSharedPtr<SWidget>();
    for (int32 Index=0;Index<Path.Widgets.Num();++Index)
    {
        const auto& Item=Path.Widgets[Index];
        // Other user widgets (inventory, loading overlays, etc.) own their pointer.
        if (Item.Widget->GetType()==FName("SObjectWidget") && Item.Widget!=HUDSlate) return true;
    }
    return false;
}
bool UBattleUnitSelectionComponent::IsEligible(const AUnitPawnBase* Unit) const
{
    if (!IsValid(Unit) || Unit->GetWorld()!=GetWorld() || !Unit->bCanBeSelected || Unit->IsHidden()
        || Unit->IsActorBeingDestroyed() || !Unit->GetLevel() || !Unit->GetLevel()->bIsVisible) return false;
    // Persistent-level spawned units and the active submap are eligible; retained
    // base/other streamed maps must never be selected through the tactical map.
    const auto* State=GetWorld()->GetGameState<AGameMainMapGameState>();
    const auto* Sub=GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>();
    if (State && Sub && !State->ActiveMapID.IsNone() && Unit->GetLevel()!=GetWorld()->PersistentLevel)
    {
        FMTS_SubMapInfo Map;
        if (!Sub->GetSubMapByKey(State->ActiveMapID,Map) || Map.State!=EMTS_SubMapState::Loaded
            || !Map.StreamingLevel || Map.StreamingLevel->GetLoadedLevel()!=Unit->GetLevel()) return false;
    }
    return true;
}
bool UBattleUnitSelectionComponent::ProjectUnit(const AUnitPawnBase* Unit,FBox2D& Bounds) const
{
    Bounds=FBox2D(ForceInit);
    const auto* PC=Controller();
    if (!PC || !IsEligible(Unit) || !Unit->CollisionComponent) return false;
    const FBox Box=Unit->CollisionComponent->Bounds.GetBox();
    for (int32 I=0;I<8;++I)
    {
        const FVector Point((I&1)?Box.Max.X:Box.Min.X,(I&2)?Box.Max.Y:Box.Min.Y,(I&4)?Box.Max.Z:Box.Min.Z);
        FVector2D Screen;
        if (!PC->ProjectWorldLocationToScreen(Point,Screen,false) || Screen.ContainsNaN()) return false;
        Bounds+=Screen;
    }
    int32 W=0,H=0; PC->GetViewportSize(W,H);
    return W>0 && H>0 && Bounds.Intersect(FBox2D(FVector2D::ZeroVector,FVector2D(W,H)));
}
TArray<AUnitPawnBase*> UBattleUnitSelectionComponent::FindUnitsInRectangle(FVector2D Start,FVector2D End) const
{
    TArray<AUnitPawnBase*> Result;
    if (!CanSelectInWorld() || Start.ContainsNaN() || End.ContainsNaN()) return Result;
    FBox2D Rectangle(ForceInit); Rectangle+=Start; Rectangle+=End;
    for (TActorIterator<AUnitPawnBase> It(GetWorld());It;++It)
    {
        FBox2D UnitBounds;
        if (ProjectUnit(*It,UnitBounds) && (bRequireFullContainment
            ? Rectangle.IsInside(UnitBounds.Min) && Rectangle.IsInside(UnitBounds.Max) : Rectangle.Intersect(UnitBounds))) Result.Add(*It);
    }
    return Result;
}
bool UBattleUnitSelectionComponent::BeginBoxSelection()
{
    if (bChangingSelection || bProcessingDeferredChanges) return false;
    CancelBoxSelection();
    auto* PC=Controller();
    FVector2D Pointer;
    if (!CanSelectInWorld() || IsPointerOverUI() || !ReadSelectionPointer(Pointer) || Pointer.ContainsNaN()) return false;
    DragStartSelection=CaptureSelectionSnapshot();
    StartPosition=Pointer;
    EndPosition=StartPosition; bSelecting=true; bDragged=false; bPreviewApplied=false;
    Overlay=SNew(SBattleSelectionRectangle).Selection(this);
    if (auto* Viewport=PC->GetWorld()->GetGameViewport()) Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(),50);
    SetComponentTickEnabled(true);
    return true;
}
void UBattleUnitSelectionComponent::UpdatePointer(bool bApplyLiveSelection)
{
    if (!bSelecting || bChangingSelection) return;
    const auto* State=GetWorld()?GetWorld()->GetGameState<AGameMainMapGameState>():nullptr;
    if (!DragStartSelection.IsSet() || !State || State!=DragStartSelection->MapState.Get()
        || State->ActiveMapID!=DragStartSelection->MapID) { CancelBoxSelection(); return; }
    FVector2D Pointer;
    if (!CanSelectInWorld() || !ReadSelectionPointer(Pointer) || Pointer.ContainsNaN()) { CancelBoxSelection(); return; }
    EndPosition=Pointer;
    const float Threshold=FMath::IsFinite(DragThreshold)?FMath::Max(1.f,DragThreshold):6.f;
    bDragged |= FVector2D::DistSquared(StartPosition,EndPosition)>=FMath::Square(Threshold);
    if (Overlay) Overlay->Invalidate(EInvalidateWidgetReason::Paint);
    if (bApplyLiveSelection && bDragged && !IsPointerOverUI())
    {
        bPreviewApplied=true;
        // Use the current projected rectangle, including stationary-pointer frames when units move.
        // This changes the shared selection immediately; ApplySelection emits only actual differences.
        SetSelectedUnits(FindUnitsInRectangle(StartPosition,EndPosition));
    }
}
void UBattleUnitSelectionComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,TickType,Function);
    ProcessDeferredSelectionChanges();
    UpdatePointer();
    SetComponentTickEnabled(bSelecting || bPendingClear || PendingRestoreSelection.IsSet());
}
void UBattleUnitSelectionComponent::CompleteBoxSelection()
{
    if (bChangingSelection || !bSelecting) return;
    // Read the release position without applying a preview that could cancel this gesture from a callback.
    UpdatePointer(false);
    if (!bSelecting) return;
    if (IsPointerOverUI()) { CancelBoxSelection(); return; }
    TArray<AUnitPawnBase*> Result;
    if (bDragged) Result=FindUnitsInRectangle(StartPosition,EndPosition);
    else
    {
        // A short press picks the nearest projected capsule, without requiring
        // meshes to block Visibility traces. Empty ground clears selection.
        float Nearest=TNumericLimits<float>::Max();
        const FVector Camera=Controller()->PlayerCameraManager->GetCameraLocation();
        for (TActorIterator<AUnitPawnBase> It(GetWorld());It;++It)
        {
            AUnitPawnBase* Unit=*It;
            FBox2D Bounds;
            if (!ProjectUnit(Unit,Bounds) || !Bounds.IsInside(EndPosition)) continue;
            const float Distance=FVector::DistSquared(Camera,Unit->GetActorLocation());
            if (Distance<Nearest) { Nearest=Distance; Result={Unit}; }
        }
    }
    FinishGesture();
    SetSelectedUnits(Result);
}
void UBattleUnitSelectionComponent::RemoveOverlay()
{
    if (Overlay && GetWorld() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(Overlay.ToSharedRef());
    Overlay.Reset();
}
void UBattleUnitSelectionComponent::CancelBoxSelection()
{
    if (!bSelecting) return;
    ++SelectionRevision;
    if (bPreviewApplied && DragStartSelection.IsSet() && !bPendingClear)
        PendingRestoreSelection=MoveTemp(DragStartSelection);
    FinishGesture();
    // Unit/data delegates can cancel inside SetSelected. Their own guards must unwind before rollback.
    ProcessDeferredSelectionChanges();
}
void UBattleUnitSelectionComponent::FinishGesture()
{
    bSelecting=false; bDragged=false; bPreviewApplied=false;
    DragStartSelection.Reset();
    SetComponentTickEnabled(false);
    RemoveOverlay();
}
UBattleUnitSelectionComponent::FSelectionSnapshot UBattleUnitSelectionComponent::CaptureSelectionSnapshot() const
{
    FSelectionSnapshot Snapshot;
    if (auto* State=GetWorld()?GetWorld()->GetGameState<AGameMainMapGameState>():nullptr)
    { Snapshot.MapState=State; Snapshot.MapID=State->ActiveMapID; }
    for (AUnitPawnBase* Unit:GetSelectedUnits())
    {
        if (const auto Data=Unit->GetUnitDataShared()) Snapshot.SelectedData.AddUnique(Data);
        else Snapshot.UnboundUnits.Add(Unit);
    }
    if (SelectedDataWithoutPawn && SelectedDataWithoutPawn->IsSelected()) Snapshot.DataWithoutPawn=SelectedDataWithoutPawn;
    return Snapshot;
}
void UBattleUnitSelectionComponent::RestoreSelectionSnapshot(const FSelectionSnapshot& Snapshot)
{
    const auto* State=GetWorld()?GetWorld()->GetGameState<AGameMainMapGameState>():nullptr;
    if (!GetWorld() || GetWorld()->bIsTearingDown || !State || State!=Snapshot.MapState.Get()
        || !State->IsBattleMap() || State->ActiveMapID!=Snapshot.MapID)
    {
        // A canceled old-map gesture must never reselect units on a newly activated map.
        ClearUnitSelection();
        return;
    }
    TArray<AUnitPawnBase*> RestoredUnits;
    for (TActorIterator<AUnitPawnBase> It(GetWorld());It;++It)
    {
        if (!IsEligible(*It)) continue;
        const auto Data=It->GetUnitDataShared();
        if (Data ? Snapshot.SelectedData.Contains(Data) || Data==Snapshot.DataWithoutPawn
            : Snapshot.UnboundUnits.Contains(TWeakObjectPtr<AUnitPawnBase>(*It))) RestoredUnits.Add(*It);
    }
    // Pawn-backed data is restored only through surviving eligible representations. An explicitly
    // data-only selection is also retained, provided that record still belongs to the unit manager.
    const auto DataWithoutPawn=Snapshot.DataWithoutPawn
        && UPlayerUnitLibrary::GetUnitDataShared(this,Snapshot.DataWithoutPawn->UnitId)==Snapshot.DataWithoutPawn
        ? Snapshot.DataWithoutPawn : nullptr;
    ApplySelection(RestoredUnits,DataWithoutPawn,true);
}
void UBattleUnitSelectionComponent::ProcessDeferredSelectionChanges()
{
    if (bChangingSelection || bProcessingDeferredChanges) return;
    TGuardValue<bool> Guard(bProcessingDeferredChanges,true);
    // Restore followed by a callback-requested clear fits in two passes. Do not spin inside an
    // externally owned data notification if it keeps rejecting deselection and requesting clear.
    for (int32 Pass=0; Pass<2 && (bPendingClear || PendingRestoreSelection.IsSet()); ++Pass)
    {
        if (bPendingClear)
        {
            bPendingClear=false;
            PendingRestoreSelection.Reset();
            ClearUnitSelection();
        }
        else
        {
            const FSelectionSnapshot Snapshot=MoveTemp(PendingRestoreSelection.GetValue());
            PendingRestoreSelection.Reset();
            RestoreSelectionSnapshot(Snapshot);
        }
    }
    if (bPendingClear || PendingRestoreSelection.IsSet()) SetComponentTickEnabled(true);
}
bool UBattleUnitSelectionComponent::GetSelectionRectangle(FVector2D& Start,FVector2D& End) const
{ Start=StartPosition; End=EndPosition; return bSelecting && bDragged; }
TArray<AUnitPawnBase*> UBattleUnitSelectionComponent::GetSelectedUnits() const
{
    TArray<AUnitPawnBase*> Result;
    if (GetWorld()) for (TActorIterator<AUnitPawnBase> It(GetWorld());It;++It)
        if (IsEligible(*It) && It->IsUnitSelected()) Result.Add(*It);
    return Result;
}
void UBattleUnitSelectionComponent::SetSelectedUnits(const TArray<AUnitPawnBase*>& Units)
{ ApplySelection(Units,nullptr); }
bool UBattleUnitSelectionComponent::SelectUnitById(FGuid UnitId)
{
    if (bChangingSelection || !CanSelectInWorld()) return false;
    const auto Data=UPlayerUnitLibrary::GetUnitDataShared(this,UnitId);
    if (!Data) return false;
    TArray<AUnitPawnBase*> Units;
    for (TActorIterator<AUnitPawnBase> It(GetWorld());It;++It)
        if (IsEligible(*It) && It->GetUnitDataShared()==Data) Units.Add(*It);
    ApplySelection(Units,Units.IsEmpty()?Data:nullptr);
    return Data->IsSelected();
}
void UBattleUnitSelectionComponent::ApplySelection(const TArray<AUnitPawnBase*>& Units,const TSharedPtr<FUnitData>& DataWithoutPawn,bool bRestoringSelection)
{
    if (bChangingSelection || (!bRestoringSelection && !CanSelectInWorld())) return;
    // Scope-exit runs after Guard is destroyed, including early exits from a unit callback.
    ON_SCOPE_EXIT { ProcessDeferredSelectionChanges(); };
    TGuardValue<bool> Guard(bChangingSelection,true);
    const uint64 Revision=SelectionRevision;
    auto* InitialState=GetWorld()?GetWorld()->GetGameState<AGameMainMapGameState>():nullptr;
    const TWeakObjectPtr<AGameMainMapGameState> MapState=InitialState;
    const FName MapID=InitialState?InitialState->ActiveMapID:NAME_None;
    auto ContinueSelection=[this,Revision,MapState,MapID,bRestoringSelection]()
    {
        if (SelectionRevision!=Revision) return false;
        const auto* State=GetWorld()?GetWorld()->GetGameState<AGameMainMapGameState>():nullptr;
        if (!GetWorld() || GetWorld()->bIsTearingDown || !State || State!=MapState.Get()
            || !State->IsBattleMap() || State->ActiveMapID!=MapID)
        { ClearUnitSelection(); return false; }
        if (!bRestoringSelection && !CanSelectInWorld())
        {
            if (bSelecting) CancelBoxSelection();
            else ClearUnitSelection();
            return false;
        }
        return true;
    };
    TSet<AUnitPawnBase*> Desired;
    TSet<const FUnitData*> DesiredData;
    for (auto* Unit:Units) if (IsEligible(Unit))
    { Desired.Add(Unit); if (const auto Data=Unit->GetUnitDataShared()) DesiredData.Add(Data.Get()); }
    if (DataWithoutPawn) DesiredData.Add(DataWithoutPawn.Get());
    const auto Previous=GetSelectedUnits();
    bool bStateChanged=false;
    if (SelectedDataWithoutPawn && !DesiredData.Contains(SelectedDataWithoutPawn.Get()))
    {
        bStateChanged |= SelectedDataWithoutPawn->SetSelected(false);
        if (!ContinueSelection()) return;
    }
    SelectedDataWithoutPawn=DataWithoutPawn;
    if (SelectedDataWithoutPawn)
    {
        bStateChanged |= SelectedDataWithoutPawn->SetSelected(true);
        if (!ContinueSelection()) return;
    }
    // A previously selected Pawn may now be hidden or its level no longer active.
    // It still needs an explicit deselection through its retained shared data.
    const auto PreviousTracked=TrackedUnits;
    for (const auto& Pair:PreviousTracked)
        if (!Desired.Contains(Pair.Key.Get()) && !(Pair.Value && DesiredData.Contains(Pair.Value.Get())))
        {
            if (Pair.Value) bStateChanged |= Pair.Value->SetSelected(false);
            else if (Pair.Key.IsValid()) bStateChanged |= Pair.Key->SetUnitSelected(false);
            if (!ContinueSelection()) return;
        }
    // Include externally selected units; Blueprint/UI can use the same data API.
    for (TActorIterator<AUnitPawnBase> It(GetWorld());It;++It)
    {
        if (!IsEligible(*It)) continue;
        const auto Data=It->GetUnitDataShared();
        const bool Selected=Desired.Contains(*It) || (Data && DesiredData.Contains(Data.Get()));
        if (Selected)
        {
            // Preserve data and destruction tracking before Blueprint selection events run.
            TrackedUnits.Add(*It,Data);
            It->OnEndPlay.AddUniqueDynamic(this,&ThisClass::HandleSelectedUnitEndPlay);
        }
        AUnitPawnBase* Unit=*It;
        bStateChanged |= Unit->SetUnitSelected(Selected);
        if ((!IsValid(Unit) || Unit->IsActorBeingDestroyed()) && Data && Data->IsSelected())
        {
            bool bStillRepresented=false;
            for (TActorIterator<AUnitPawnBase> Other(GetWorld());Other;++Other)
                if (IsEligible(*Other) && Other->GetUnitDataShared()==Data) { bStillRepresented=true; break; }
            if (!bStillRepresented) bStateChanged |= Data->SetSelected(false);
        }
        if (!ContinueSelection()) return;
    }
    for (auto It=TrackedUnits.CreateIterator();It;++It)
        if (!It.Key().IsValid() || !It.Key()->IsUnitSelected())
        {
            if (It.Key().IsValid()) It.Key()->OnEndPlay.RemoveDynamic(this,&ThisClass::HandleSelectedUnitEndPlay);
            It.RemoveCurrent();
        }
    const auto Current=GetSelectedUnits();
    if (bStateChanged || Previous!=Current)
    {
        OnSelectionChanged.Broadcast(Current);
        ContinueSelection();
    }
}
void UBattleUnitSelectionComponent::ClearUnitSelection()
{
    ++SelectionRevision;
    FinishGesture();
    PendingRestoreSelection.Reset();
    if (bChangingSelection) { bPendingClear=true; return; }
    bPendingClear=false;
    ON_SCOPE_EXIT { ProcessDeferredSelectionChanges(); };
    TGuardValue<bool> Guard(bChangingSelection,true);
    bool Changed=false;
    const auto PreviousData=MoveTemp(SelectedDataWithoutPawn);
    SelectedDataWithoutPawn.Reset();
    if (PreviousData) { Changed |= PreviousData->IsSelected(); PreviousData->SetSelected(false); }
    const auto PreviousTracked=MoveTemp(TrackedUnits);
    TrackedUnits.Reset();
    for (const auto& Pair:PreviousTracked)
    {
        if (Pair.Key.IsValid()) Pair.Key->OnEndPlay.RemoveDynamic(this,&ThisClass::HandleSelectedUnitEndPlay);
        if (Pair.Value) { Changed |= Pair.Value->IsSelected(); Pair.Value->SetSelected(false); }
        else if (Pair.Key.IsValid()) { Changed |= Pair.Key->IsUnitSelected(); Pair.Key->SetUnitSelected(false); }
    }
    if (GetWorld()) for (TActorIterator<AUnitPawnBase> It(GetWorld());It;++It)
        if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->IsUnitSelected()) { Changed=true; It->SetUnitSelected(false); }
    if (Changed) OnSelectionChanged.Broadcast(TArray<AUnitPawnBase*>());
}
void UBattleUnitSelectionComponent::HandleSelectedUnitEndPlay(AActor* Actor,EEndPlayReason::Type)
{
    auto* Unit=Cast<AUnitPawnBase>(Actor);
    TSharedPtr<FUnitData> Data;
    TrackedUnits.RemoveAndCopyValue(Unit,Data);
    // A spawner creates its replacement before destroying the old Pawn. Transfer
    // tracking to that representation instead of clearing the shared selection.
    bool bStillRepresented=false;
    if (Data) for (TActorIterator<AUnitPawnBase> It(GetWorld());It;++It)
        if (*It!=Unit && IsEligible(*It) && It->GetUnitDataShared()==Data)
        {
            bStillRepresented=true;
            TrackedUnits.Add(*It,Data);
            It->OnEndPlay.AddUniqueDynamic(this,&ThisClass::HandleSelectedUnitEndPlay);
        }
    if (Data && !bStillRepresented) Data->SetSelected(false);
    if (!bChangingSelection) OnSelectionChanged.Broadcast(GetSelectedUnits());
}
void UBattleUnitSelectionComponent::EndPlay(const EEndPlayReason::Type Reason)
{ ClearUnitSelection(); Super::EndPlay(Reason); }
