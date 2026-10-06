#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/BattleUnitSelectionTestObserver.h"
#include "Map/BattleMap/BattleUnitSelectionComponent.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/ActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "MTS_SubMapSubsystem.h"
#include "Materials/Material.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace BattleUnitSelectionTest
{
class FRuntime final : public IAutomationLatentCommand
{
public:
    explicit FRuntime(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual ~FRuntime() override { Cleanup(); }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 40.)
        { Test->AddError(TEXT("Unit selection runtime timed out waiting for world/camera")); return Finish(); }
        if (!bInitialized)
        {
            if (!GEngine || !GEngine->GameViewport) return false;
            for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game && Context.World())
                    PC = Cast<AGameMainMapPlayerController>(Context.World()->GetFirstPlayerController());
            if (!PC.IsValid()) return false;
            auto* Sub = PC->GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>();
            if (Sub && Sub->IsSubMapTransitionInProgress()) return false;
            if (!Initialize()) return Finish();
            ReadyAt = FPlatformTime::Seconds() + .3;
            return false;
        }
        if (FPlatformTime::Seconds() < ReadyAt) return false;
        ValidateProjection();
        ValidateDecal();
        ValidateGestures();
        ValidateSelection();
        if (!Test->HasAnyErrors())
            Test->AddInfo(TEXT("BATTLE_UNIT_SELECTION_OK real gestures, realtime delta, rollback, UI pause, release position, reentrant clear, native card click, shared data and selection decal"));
        return Finish();
    }
private:
    FAutomationTestBase* Test;
    double Started = FPlatformTime::Seconds(), ReadyAt = 0.;
    bool bInitialized = false, bManagerInstalled = false;
    TWeakObjectPtr<AGameMainMapPlayerController> PC;
    TWeakObjectPtr<AGameMainMapGameState> State;
    TWeakObjectPtr<AActor> OriginalView;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<UPlayerUnitSubsystem> UnitSystem;
    FObjectPropertyBase* ManagerProperty = nullptr;
    TStrongObjectPtr<UPlayerUnitManagerBase> OriginalManager, Manager;
    TStrongObjectPtr<UUnitDataReference> FirstReference, SecondReference;
    TStrongObjectPtr<UBattleUnitSelectionTestObserver> Observer;
    TStrongObjectPtr<UBattleUnitSelectionComponent> OriginalComponent;
    TStrongObjectPtr<UBattleSelectionTestComponent> PointerSelection;
    TArray<TWeakObjectPtr<ABattleSelectionTestPawn>> Fixtures;
    TArray<TWeakObjectPtr<AUnitPawnBase>> OriginalSelection;
    TWeakObjectPtr<ABattleSelectionTestPawn> First, Second, Hidden, NotSelectable, Behind;
    EGameMainMapType OriginalType = EGameMainMapType::None;
    FName OriginalMap;
    bool OriginalContainment = false;
    UBattleUnitSelectionComponent* Selection() const { return PC.IsValid() ? PC->UnitSelection.Get() : nullptr; }

    ABattleSelectionTestPawn* Spawn(FVector Location)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Pawn = PC->GetWorld()->SpawnActor<ABattleSelectionTestPawn>(Location, FRotator::ZeroRotator, Params);
        if (Pawn)
        {
            Fixtures.Add(Pawn);
            // This fixture measures screen projection and selection, not the Mover simulation.
            TInlineComponentArray<UActorComponent*> Components(Pawn);
            for (auto* Component : Components) { Component->Deactivate(); Component->SetComponentTickEnabled(false); }
            Pawn->SetActorEnableCollision(false);
        }
        return Pawn;
    }

    bool Initialize()
    {
        State = PC->GetWorld()->GetGameState<AGameMainMapGameState>();
        if (!Test->TestNotNull(TEXT("Main map selection component exists"), Selection()) ||
            !Test->TestNotNull(TEXT("Main map game state exists"), State.Get())) return false;
        OriginalType = State->GetCurrentMapType(); OriginalMap = State->ActiveMapID;
        OriginalView = PC->GetViewTarget(); OriginalContainment = Selection()->bRequireFullContainment;
        for (auto* Unit : Selection()->GetSelectedUnits()) OriginalSelection.Add(Unit);
        bInitialized = true;
        State->SetCurrentMapType(EGameMainMapType::Battle);
        State->ActiveMapID = NAME_None;
        Selection()->ClearUnitSelection();
        OriginalComponent.Reset(Selection());
        PointerSelection.Reset(NewObject<UBattleSelectionTestComponent>(PC.Get()));
        if (!Test->TestNotNull(TEXT("Native pointer fixture component created"),PointerSelection.Get())) return false;
        // Install on the actual local controller so map callbacks and card FindComponentByClass
        // exercise this same component. The original component is retained and restored at cleanup.
        PC->RemoveOwnedComponent(OriginalComponent.Get());
        PC->AddInstanceComponent(PointerSelection.Get());
        PointerSelection->RegisterComponent();
        PC->UnitSelection=PointerSelection.Get();
        if (!Test->TestTrue(TEXT("Native card component lookup reaches the pointer fixture"),
            PC->FindComponentByClass<UBattleUnitSelectionComponent>()==PointerSelection.Get())) return false;

        UnitSystem = UPlayerUnitLibrary::GetPlayerUnitSubsystem(PC.Get());
        if (!Test->TestNotNull(TEXT("Unit subsystem exists"), UnitSystem.Get())) return false;
        ManagerProperty = FindFProperty<FObjectPropertyBase>(UnitSystem->GetClass(), TEXT("Manager"));
        if (!Test->TestNotNull(TEXT("Canonical manager can be isolated"), ManagerProperty)) return false;
        OriginalManager.Reset(Cast<UPlayerUnitManagerBase>(ManagerProperty->GetObjectPropertyValue_InContainer(UnitSystem.Get())));
        Manager.Reset(NewObject<UPlayerUnitManagerBase>(UnitSystem.Get()));
        ManagerProperty->SetObjectPropertyValue_InContainer(UnitSystem.Get(), Manager.Get());
        bManagerInstalled = true;
        FUnitData FirstData, SecondData;
        FirstData.UnitId = FGuid::NewGuid(); SecondData.UnitId = FGuid::NewGuid();
        FText Error;
        if (!Test->TestTrue(TEXT("Isolated unit records load"), Manager->LoadUnitData({FirstData, SecondData}, Error))) return false;

        const FVector Origin(70000, 70000, 30000);
        Camera = PC->GetWorld()->SpawnActor<ACameraActor>(Origin, FRotator::ZeroRotator);
        if (!Test->TestNotNull(TEXT("Projection camera spawned"), Camera.Get())) return false;
        Camera->GetCameraComponent()->SetFieldOfView(90.f);
        Camera->GetCameraComponent()->bConstrainAspectRatio = false;
        PC->SetViewTarget(Camera.Get());
        First = Spawn(Origin + FVector(1000,-200,0));
        Second = Spawn(Origin + FVector(1000,200,0));
        Hidden = Spawn(Origin + FVector(1000,0,0));
        NotSelectable = Spawn(Origin + FVector(1000,0,0));
        Behind = Spawn(Origin + FVector(-1000,0,0));
        if (!Test->TestTrue(TEXT("Five subclass fixtures spawned"), First.IsValid() && Second.IsValid() && Hidden.IsValid() && NotSelectable.IsValid() && Behind.IsValid())) return false;
        Hidden->SetActorHiddenInGame(true); NotSelectable->bCanBeSelected = false;
        if (!Test->TestTrue(TEXT("First Pawn binds canonical record"), First->BindUnitData(FirstData.UnitId)) ||
            !Test->TestTrue(TEXT("Second Pawn binds canonical record"), Second->BindUnitData(SecondData.UnitId))) return false;
        FirstReference.Reset(UPlayerUnitLibrary::GetUnitDataReference(PC.Get(), FirstData.UnitId));
        SecondReference.Reset(UPlayerUnitLibrary::GetUnitDataReference(PC.Get(), SecondData.UnitId));
        if (!Test->TestNotNull(TEXT("UI reference for first Pawn exists"), FirstReference.Get()) ||
            !Test->TestNotNull(TEXT("UI reference for second Pawn exists"), SecondReference.Get())) return false;
        Observer.Reset(NewObject<UBattleUnitSelectionTestObserver>());
        Selection()->OnSelectionChanged.AddDynamic(Observer.Get(), &UBattleUnitSelectionTestObserver::RecordSelection);
        return true;
    }

    void ValidateProjection()
    {
        FVector2D A, B;
        if (!Test->TestTrue(TEXT("Camera projects first Pawn"), PC->ProjectWorldLocationToScreen(First->GetActorLocation(), A)) ||
            !Test->TestTrue(TEXT("Camera projects second Pawn"), PC->ProjectWorldLocationToScreen(Second->GetActorLocation(), B))) return;
        const FVector2D Min(FMath::Min(A.X,B.X)-150,FMath::Min(A.Y,B.Y)-150);
        const FVector2D Max(FMath::Max(A.X,B.X)+150,FMath::Max(A.Y,B.Y)+150);
        Selection()->bRequireFullContainment = false;
        const auto Forward = Selection()->FindUnitsInRectangle(Min, Max);
        const auto Reverse = Selection()->FindUnitsInRectangle(Max, Min);
        Test->TestTrue(TEXT("Rectangle includes UnitPawnBase subclasses"), Forward.Contains(First.Get()) && Forward.Contains(Second.Get()));
        Test->TestTrue(TEXT("Dragging in the reverse direction gives identical units"), Forward == Reverse);
        Test->TestFalse(TEXT("Hidden Pawn is excluded"), Forward.Contains(Hidden.Get()));
        Test->TestFalse(TEXT("Nonselectable Pawn is excluded"), Forward.Contains(NotSelectable.Get()));
        Test->TestFalse(TEXT("Pawn behind the camera is excluded"), Forward.Contains(Behind.Get()));
        Test->TestTrue(TEXT("Partial capsule intersection is selectable by default"), Selection()->FindUnitsInRectangle(A-FVector2D(4,4),A+FVector2D(4,4)).Contains(First.Get()));
        Selection()->bRequireFullContainment = true;
        Test->TestFalse(TEXT("Full containment rejects a tiny partial rectangle"), Selection()->FindUnitsInRectangle(A-FVector2D(4,4),A+FVector2D(4,4)).Contains(First.Get()));
        Test->TestTrue(TEXT("Full containment accepts a rectangle around both capsules"), Selection()->FindUnitsInRectangle(Min,Max).Contains(First.Get()));
        Selection()->bRequireFullContainment = false;
        State->SetCurrentMapType(EGameMainMapType::Base);
        Test->TestTrue(TEXT("Base map cannot query tactical selection"), Selection()->FindUnitsInRectangle(Min,Max).IsEmpty());
        Selection()->SetSelectedUnits({First.Get()});
        Test->TestFalse(TEXT("Base map cannot select units through the selection component"), First->IsUnitSelected());
        State->SetCurrentMapType(EGameMainMapType::Battle);
    }

    void CheckDecal(const TCHAR* Label,AUnitPawnBase* Pawn,bool bVisible)
    {
        if (!Test->TestNotNull(FString(Label)+TEXT(" has a decal"),Pawn?Pawn->SelectionDecal.Get():nullptr)) return;
        Test->TestEqual(FString(Label)+TEXT(" visibility"),Pawn->SelectionDecal->IsVisible(),bVisible);
        Test->TestEqual(FString(Label)+TEXT(" hidden-in-game"),bool(Pawn->SelectionDecal->bHiddenInGame),!bVisible);
    }

    void ValidateDecal()
    {
        const auto* Defaults=GetDefault<AUnitPawnBase>();
        if (!Test->TestNotNull(TEXT("Native Pawn owns a default selection decal"),Defaults->SelectionDecal.Get())) return;
        Test->TestFalse(TEXT("Default decal visibility starts off"),Defaults->SelectionDecal->IsVisible());
        Test->TestTrue(TEXT("Default decal starts hidden in game"),bool(Defaults->SelectionDecal->bHiddenInGame));
        auto* Material=Defaults->SelectionDecal->GetDecalMaterial();
        if (Test->TestNotNull(TEXT("Default selection decal has its authored material"),Material))
        {
            Test->TestEqual(TEXT("Selection decal material is the intended asset"),Material->GetPathName(),
                FString(TEXT("/Game/System/Object/Unit/Materials/M_UnitSelectionDecal.M_UnitSelectionDecal")));
            Test->TestTrue(TEXT("Selection material uses the decal domain"),Material->GetMaterial()->MaterialDomain==MD_DeferredDecal);
        }
        CheckDecal(TEXT("Unselected bound Pawn"),First.Get(),false);
        Selection()->SetSelectedUnits({First.Get()});
        CheckDecal(TEXT("Selected bound Pawn"),First.Get(),true);
        Selection()->ClearUnitSelection();
        CheckDecal(TEXT("Deselected bound Pawn"),First.Get(),false);
        Test->TestTrue(TEXT("UI reference selects for decal synchronization"),FirstReference->SetSelected(true));
        CheckDecal(TEXT("UI-selected Pawn"),First.Get(),true);

        // Bind before FinishSpawning to cover the deferred spawner path, including the first event.
        const auto Shared=First->GetUnitDataShared();
        const FTransform Transform(FRotator::ZeroRotator,First->GetActorLocation()+FVector(0,0,-1000));
        auto* Deferred=PC->GetWorld()->SpawnActorDeferred<ABattleSelectionTestPawn>(
            ABattleSelectionTestPawn::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Test->TestNotNull(TEXT("Deferred selected-unit fixture created"),Deferred))
        {
            Fixtures.Add(Deferred);
            Test->TestTrue(TEXT("Deferred Pawn accepts the already selected canonical record"),Deferred->BindUnitDataShared(Shared));
            Deferred->FinishSpawning(Transform);
            Test->TestTrue(TEXT("Deferred Pawn inherits selection immediately"),Deferred->IsUnitSelected());
            CheckDecal(TEXT("Deferred selected Pawn"),Deferred,true);
            Test->TestEqual(TEXT("Deferred binding notifies selected once after initialization"),Deferred->SelectedEvents,1);
            Test->TestEqual(TEXT("Deferred selected event sees the updated decal"),Deferred->IncorrectDecalEvents,0);
            Deferred->BindUnitDataShared(nullptr);
            CheckDecal(TEXT("Unbound former selected Pawn"),Deferred,false);
            Deferred->Destroy();
        }
        Selection()->ClearUnitSelection();
        Test->TestEqual(TEXT("Bound Pawn events always observe the updated decal"),First->IncorrectDecalEvents,0);
    }

    void TickPointer(FVector2D Position)
    {
        PointerSelection->Pointer=Position;
        PointerSelection->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
    }

    void ValidateGestures()
    {
        // Derive the input coordinates from the two actual capsule projections, avoiding
        // dependence on viewport size while testing the normal Begin/Tick/Complete route.
        auto BoundsFor=[this](AUnitPawnBase* Pawn)
        {
            FBox2D Screen(ForceInit);
            const FBox Box=Pawn->CollisionComponent->Bounds.GetBox();
            for (int32 Index=0;Index<8;++Index)
            {
                FVector2D Position;
                const FVector Point((Index&1)?Box.Max.X:Box.Min.X,(Index&2)?Box.Max.Y:Box.Min.Y,(Index&4)?Box.Max.Z:Box.Min.Z);
                if (PC->ProjectWorldLocationToScreen(Point,Position)) Screen+=Position;
            }
            return Screen;
        };
        FBox2D LeftBounds=BoundsFor(First.Get()),RightBounds=BoundsFor(Second.Get());
        ABattleSelectionTestPawn* Left=First.Get();
        ABattleSelectionTestPawn* Right=Second.Get();
        if (LeftBounds.Min.X>RightBounds.Min.X) { Swap(LeftBounds,RightBounds); Swap(Left,Right); }
        if (!Test->TestTrue(TEXT("Gesture fixtures have separated projected capsules"),LeftBounds.bIsValid && RightBounds.bIsValid && LeftBounds.Max.X<RightBounds.Min.X)) return;
        const FVector2D Start(FMath::Min(LeftBounds.Min.X,RightBounds.Min.X)-20,FMath::Min(LeftBounds.Min.Y,RightBounds.Min.Y)-20);
        const FVector2D BothEnd(FMath::Max(LeftBounds.Max.X,RightBounds.Max.X)+20,FMath::Max(LeftBounds.Max.Y,RightBounds.Max.Y)+20);
        const FVector2D OneEnd((LeftBounds.Max.X+RightBounds.Min.X)*.5,BothEnd.Y);
        const FVector2D EmptyEnd=Start+FVector2D(8,8);
        Test->TestTrue(TEXT("Empty gesture rectangle is outside the fixtures"),Selection()->FindUnitsInRectangle(Start,EmptyEnd).IsEmpty());
        auto Begin=[this,Start]()
        {
            PointerSelection->Pointer=Start;
            return Test->TestTrue(TEXT("World press starts the real selection gesture"),PointerSelection->BeginBoxSelection());
        };
        auto ExpectPair=[this,Left,Right](const TCHAR* Label,bool bLeft,bool bRight)
        {
            Test->TestEqual(FString(Label)+TEXT(" left shared state"),Left->IsUnitSelected(),bLeft);
            Test->TestEqual(FString(Label)+TEXT(" right shared state"),Right->IsUnitSelected(),bRight);
            CheckDecal(Label,Left,bLeft);
            CheckDecal(Label,Right,bRight);
        };

        Selection()->SetSelectedUnits({Right});
        const int32 BeforePress=Observer->Changes;
        if (!Begin()) return;
        TickPointer(Start+FVector2D(2,2));
        ExpectPair(TEXT("Movement below threshold keeps original selection"),false,true);
        Test->TestEqual(TEXT("Press and subthreshold movement do not notify"),Observer->Changes,BeforePress);
        TickPointer(BothEnd);
        ExpectPair(TEXT("Dragging into both capsules selects before release"),true,true);
        const int32 StableChanges=Observer->Changes,StableLeftEvents=Left->SelectedEvents,StableRightEvents=Right->SelectedEvents;
        TickPointer(BothEnd);
        Test->TestEqual(TEXT("Stationary drag emits no duplicate aggregate event"),Observer->Changes,StableChanges);
        Test->TestEqual(TEXT("Stationary drag emits no duplicate left selected event"),Left->SelectedEvents,StableLeftEvents);
        Test->TestEqual(TEXT("Stationary drag emits no duplicate right selected event"),Right->SelectedEvents,StableRightEvents);
        TickPointer(OneEnd);
        ExpectPair(TEXT("Shrinking rectangle deselects the unit outside it immediately"),true,false);
        TickPointer(EmptyEnd);
        ExpectPair(TEXT("An empty live rectangle deselects both units"),false,false);
        Selection()->CancelBoxSelection();
        ExpectPair(TEXT("Cancel restores the selection captured on press"),false,true);
        Test->TestFalse(TEXT("Cancel ends the gesture"),Selection()->bSelecting);
        const int32 CanceledChanges=Observer->Changes;
        Selection()->CancelBoxSelection();
        Test->TestEqual(TEXT("Repeated cancel has no selection side effects"),Observer->Changes,CanceledChanges);

        if (!Begin()) return;
        TickPointer(OneEnd);
        PointerSelection->bOverUI=true;
        const int32 BeforeUI=Observer->Changes;
        TickPointer(BothEnd);
        ExpectPair(TEXT("Hovering UI pauses live selection"),true,false);
        Test->TestEqual(TEXT("UI hover does not notify a new selection"),Observer->Changes,BeforeUI);
        Selection()->CompleteBoxSelection();
        ExpectPair(TEXT("Releasing over UI restores the initial selection"),false,true);
        Test->TestFalse(TEXT("A press over UI cannot start selection"),PointerSelection->BeginBoxSelection());
        PointerSelection->bOverUI=false;
        if (!Begin()) return;
        TickPointer(OneEnd);
        const int32 BeforeRelease=Observer->Changes,LeftEventsBeforeRelease=Left->SelectedEvents;
        PointerSelection->Pointer=BothEnd; // Deliberately no Tick at this final mouse position.
        Selection()->CompleteBoxSelection();
        ExpectPair(TEXT("Release commits the final pointer position"),true,true);
        Test->TestEqual(TEXT("Release only adds its actual final selection change"),Observer->Changes,BeforeRelease+1);
        Test->TestEqual(TEXT("Successful release does not deselect and reselect the retained unit"),Left->SelectedEvents,LeftEventsBeforeRelease);
        Test->TestFalse(TEXT("Successful release removes the gesture"),Selection()->bSelecting);
        Selection()->CompleteBoxSelection();
        Test->TestEqual(TEXT("Repeated completion has no effect"),Observer->Changes,BeforeRelease+1);

        Selection()->SetSelectedUnits({Right});
        if (!Begin()) return;
        TickPointer(OneEnd);
        PointerSelection->bPointerAvailable=false;
        TickPointer(BothEnd);
        ExpectPair(TEXT("Losing the pointer cancels and restores selection"),false,true);
        PointerSelection->bPointerAvailable=true;

        // Both callbacks execute during a real live preview, where setters and notifications are guarded.
        Selection()->ClearUnitSelection();
        const auto Ordered=Selection()->FindUnitsInRectangle(Start,BothEnd);
        if (Test->TestTrue(TEXT("Reentry rectangle has exactly the two fixtures"),Ordered.Num()==2))
        {
            auto* CallbackPawn=CastChecked<ABattleSelectionTestPawn>(Ordered[0]);
            auto* LaterPawn=CastChecked<ABattleSelectionTestPawn>(Ordered[1]);
            const int32 LaterEvents=LaterPawn->SelectedEvents;
            CallbackPawn->ClearOnNextSelected=Selection();
            if (!Begin()) return;
            TickPointer(BothEnd);
            ExpectPair(TEXT("Pawn callback clear stops this frame's selection"),false,false);
            Test->TestEqual(TEXT("Clear in first Pawn event prevents selecting the later Pawn"),LaterPawn->SelectedEvents,LaterEvents);
            Test->TestFalse(TEXT("Pawn callback clear also ends dragging"),Selection()->bSelecting);
        }
        Observer->ClearOnNextChange=Selection();
        if (!Begin()) return;
        TickPointer(BothEnd);
        ExpectPair(TEXT("Aggregate callback clear wins over the preview"),false,false);
        Test->TestEqual(TEXT("Aggregate listener finishes with empty selection"),Observer->LastCount,0);
        TickPointer(BothEnd);
        ExpectPair(TEXT("A cleared gesture cannot reselect on the following tick"),false,false);

        FUnitData DataOnly;
        DataOnly.UnitId=FGuid::NewGuid();
        FText Error;
        if (Test->TestTrue(TEXT("Data-only gesture fixture is loaded"),Manager->LoadUnitData({DataOnly},Error)))
        {
            Test->TestTrue(TEXT("Data-only record can be selected before a Pawn exists"),Selection()->SelectUnitById(DataOnly.UnitId));
            if (!Begin()) return;
            TickPointer(EmptyEnd);
            Test->TestFalse(TEXT("Live preview clears the prior data-only selection"),Manager->GetUnitDataShared(DataOnly.UnitId)->IsSelected());
            Selection()->CancelBoxSelection();
            Test->TestTrue(TEXT("Cancel restores data-only shared selection"),Manager->GetUnitDataShared(DataOnly.UnitId)->IsSelected());
            Selection()->ClearUnitSelection();
        }
        auto* Unbound=Spawn((Left->GetActorLocation()+Right->GetActorLocation())*.5);
        if (Test->TestNotNull(TEXT("Unbound gesture fixture spawned"),Unbound))
        {
            Selection()->SetSelectedUnits({Unbound});
            if (!Begin()) return;
            TickPointer(EmptyEnd);
            Selection()->CancelBoxSelection();
            Test->TestTrue(TEXT("Cancel restores an unbound Pawn's local selection"),Unbound->IsUnitSelected());
            CheckDecal(TEXT("Restored unbound Pawn"),Unbound,true);
            Selection()->ClearUnitSelection();
            Unbound->Destroy();
        }
        FUnitData DestroyedData;
        DestroyedData.UnitId=FGuid::NewGuid();
        auto* Doomed=Spawn((Left->GetActorLocation()+Right->GetActorLocation())*.5);
        if (Test->TestNotNull(TEXT("Destroy-during-drag fixture spawned"),Doomed)
            && Test->TestTrue(TEXT("Destroy-during-drag data loaded"),Manager->LoadUnitData({DestroyedData},Error)))
        {
            Doomed->BindUnitData(DestroyedData.UnitId);
            Selection()->SetSelectedUnits({Doomed});
            if (!Begin()) return;
            TickPointer(EmptyEnd);
            Doomed->Destroy();
            Selection()->CancelBoxSelection();
            Test->TestFalse(TEXT("Cancel cannot revive selection for a destroyed Pawn"),Manager->GetUnitDataShared(DestroyedData.UnitId)->IsSelected());
            Test->TestTrue(TEXT("Destroyed snapshot leaves no selected Pawns"),Selection()->GetSelectedUnits().IsEmpty());
        }

        Selection()->SetSelectedUnits({Right});
        if (!Begin()) return;
        TickPointer(OneEnd);
        const FName ChangedMap(TEXT("SELECTION_GESTURE_CHANGED_MAP"));
        State->ActiveMapID=ChangedMap;
        State->OnActiveMapChanged.Broadcast(NAME_None,ChangedMap);
        ExpectPair(TEXT("Changing active map clears both preview and snapshot"),false,false);
        Test->TestFalse(TEXT("Changing active map stops the gesture"),Selection()->bSelecting);
        Selection()->CancelBoxSelection();
        TickPointer(BothEnd);
        ExpectPair(TEXT("Canceled old-map rectangle never restores another map's units"),false,false);
        State->ActiveMapID=NAME_None;
        Test->TestEqual(TEXT("Every left Pawn selection event saw its decal already updated"),Left->IncorrectDecalEvents,0);
        Test->TestEqual(TEXT("Every right Pawn selection event saw its decal already updated"),Right->IncorrectDecalEvents,0);
    }

    void ValidateSelection()
    {
        const int32 FirstSelectedBefore = First->SelectedEvents;
        const int32 SecondSelectedBefore = Second->SelectedEvents;
        Selection()->SetSelectedUnits({First.Get(),Second.Get(),First.Get(),nullptr,Hidden.Get(),NotSelectable.Get()});
        Test->TestEqual(TEXT("Unique eligible Pawn selection count"), Selection()->GetSelectedUnits().Num(), 2);
        Test->TestTrue(TEXT("UI references observe the same selected canonical data"), FirstReference->IsSelected() && SecondReference->IsSelected());
        Test->TestEqual(TEXT("First Pawn selected event fires once"), First->SelectedEvents, FirstSelectedBefore+1);
        Test->TestEqual(TEXT("Second Pawn selected event fires once"), Second->SelectedEvents, SecondSelectedBefore+1);
        const int32 ChangesAfterFirst = Observer->Changes;
        Selection()->SetSelectedUnits({Second.Get(),First.Get(),Second.Get()});
        Test->TestEqual(TEXT("Unchanged selection does not emit selection-changed"), Observer->Changes, ChangesAfterFirst);
        Test->TestEqual(TEXT("Repeated selection does not emit Pawn selected event"), First->SelectedEvents, FirstSelectedBefore+1);

        const int32 FirstDeselectedBefore = First->DeselectedEvents;
        Selection()->SetSelectedUnits({Second.Get()});
        Test->TestFalse(TEXT("Replacement deselects the removed Pawn"), First->IsUnitSelected());
        Test->TestTrue(TEXT("Replacement retains selected Pawn"), Second->IsUnitSelected());
        Test->TestEqual(TEXT("Removed Pawn gets one deselection event"), First->DeselectedEvents, FirstDeselectedBefore+1);
        Test->TestFalse(TEXT("UI reference sees replacement deselection"), FirstReference->IsSelected());
        Test->TestTrue(TEXT("UI reference can select the same canonical unit"), FirstReference->SetSelected(true));
        Test->TestTrue(TEXT("Pawn listens to UI-originated shared selection"), First->IsUnitSelected());
        Test->TestTrue(TEXT("Component query sees externally selected unit"), Selection()->GetSelectedUnits().Contains(First.Get()));
        Selection()->ClearUnitSelection();
        Test->TestTrue(TEXT("Clear deselects all Pawn and UI representations"), Selection()->GetSelectedUnits().IsEmpty() && !FirstReference->IsSelected() && !SecondReference->IsSelected());
        const int32 ChangesAfterClear = Observer->Changes;
        Selection()->ClearUnitSelection();
        Test->TestEqual(TEXT("Repeated clear emits no redundant event"), Observer->Changes, ChangesAfterClear);

        Selection()->SetSelectedUnits({First.Get()});
        First->SetActorHiddenInGame(true);
        const int32 BeforeHiddenReplacement = Observer->Changes;
        Selection()->SetSelectedUnits({});
        Test->TestFalse(TEXT("Replacing hidden selection clears its retained canonical state"), FirstReference->IsSelected());
        Test->TestEqual(TEXT("Replacing hidden selection still notifies listeners"), Observer->Changes, BeforeHiddenReplacement+1);
        First->SetActorHiddenInGame(false);

        Selection()->SetSelectedUnits({Second.Get()});
        const auto SharedSecond = Second->GetUnitDataShared();
        const int32 BeforeDestroy = Observer->Changes;
        Second->Destroy();
        Test->TestFalse(TEXT("Destroyed selected Pawn clears retained shared data"), SharedSecond->IsSelected());
        Test->TestFalse(TEXT("UI sees destroyed Pawn deselected"), SecondReference->IsSelected());
        Test->TestTrue(TEXT("Destroyed Pawn no longer appears in selection"), Selection()->GetSelectedUnits().IsEmpty());
        Test->TestEqual(TEXT("Destroying selected Pawn notifies once"), Observer->Changes, BeforeDestroy+1);
        FUnitData PendingData;
        PendingData.UnitId=FGuid::NewGuid();
        FText Error;
        Test->TestTrue(TEXT("A UI-only unit record can be loaded"),Manager->LoadUnitData({PendingData},Error));
        UClass* CardClass=LoadClass<UBattlePersonnelCardWidget>(nullptr,TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_人员卡片.WBP_人员卡片_C"));
        if (!Test->TestNotNull(TEXT("Authored personnel card class"),CardClass)) return;
        TStrongObjectPtr<UBattlePersonnelCardWidget> Card(CreateWidget<UBattlePersonnelCardWidget>(PC.Get(),CardClass));
        if (!Test->TestNotNull(TEXT("Authored personnel card instance"),Card.Get())) return;
        FBattleMemberView View; View.UnitId=PendingData.UnitId;
        Card->SetMember(View);
        const TSharedPtr<SWidget> CardSlate=Card->TakeWidget();
        Test->TestNotNull(TEXT("Personnel card builds its actual Slate tree"),CardSlate.Get());
        Test->TestTrue(TEXT("Constructed personnel card owns its actual click delegate"),Card->OnClicked.IsBound());
        Card->OnClicked.Broadcast();
        Test->TestTrue(TEXT("Actual personnel card click selects before its Pawn exists"),Manager->GetUnitDataShared(PendingData.UnitId)->IsSelected());
        Test->TestTrue(TEXT("Personnel card follows shared selection"),Card->IsMemberSelected());
        auto* LaterPawn=Spawn(FVector(2000,0,200));
        Test->TestTrue(TEXT("Later Pawn binds the selected record"),LaterPawn && LaterPawn->BindUnitData(PendingData.UnitId));
        if (LaterPawn)
        {
            Test->TestTrue(TEXT("Later Pawn immediately inherits data selection"),LaterPawn->IsUnitSelected());
            CheckDecal(TEXT("Pawn created after personnel card click"),LaterPawn,true);
            Test->TestEqual(TEXT("Later Pawn's event sees the decal already shown"),LaterPawn->IncorrectDecalEvents,0);
        }
        Selection()->SetSelectedUnits({First.Get()});
        Test->TestFalse(TEXT("Next box selection clears data-only selection and card"),Card->IsMemberSelected());
        if (LaterPawn) Test->TestFalse(TEXT("Next box selection clears later Pawn as well"),LaterPawn->IsUnitSelected());
        Selection()->SetSelectedUnits({First.Get()});
        State->SetCurrentMapType(EGameMainMapType::Base);
        Test->TestFalse(TEXT("Leaving battle map clears selected unit data"), FirstReference->IsSelected());
        Test->TestFalse(TEXT("Leaving battle map cancels drag state"), Selection()->bSelecting);
    }

    bool Finish() { Cleanup(); return true; }
    void Cleanup()
    {
        if (!bInitialized) return;
        if (auto* Component = Selection())
        {
            if (Observer.IsValid()) Component->OnSelectionChanged.RemoveDynamic(Observer.Get(), &UBattleUnitSelectionTestObserver::RecordSelection);
            Component->ClearUnitSelection();
            Component->bRequireFullContainment = OriginalContainment;
        }
        FirstReference.Reset(); SecondReference.Reset(); Observer.Reset();
        for (auto Pawn : Fixtures) if (Pawn.IsValid()) Pawn->Destroy();
        Fixtures.Reset();
        if (PointerSelection.IsValid())
        {
            if (PC.IsValid()) PC->RemoveInstanceComponent(PointerSelection.Get());
            PointerSelection->DestroyComponent();
        }
        if (PC.IsValid() && OriginalComponent.IsValid())
        {
            PC->UnitSelection=OriginalComponent.Get();
            PC->AddOwnedComponent(OriginalComponent.Get());
        }
        PointerSelection.Reset(); OriginalComponent.Reset();
        if (PC.IsValid() && OriginalView.IsValid()) PC->SetViewTarget(OriginalView.Get());
        if (Camera.IsValid()) Camera->Destroy();
        if (bManagerInstalled && UnitSystem.IsValid()) ManagerProperty->SetObjectPropertyValue_InContainer(UnitSystem.Get(), OriginalManager.Get());
        bManagerInstalled = false;
        if (State.IsValid()) { State->ActiveMapID = OriginalMap; State->SetCurrentMapType(OriginalType); }
        for (auto Unit : OriginalSelection) if (Unit.IsValid()) Unit->SetUnitSelected(true);
        OriginalSelection.Reset(); bInitialized = false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattleUnitSelectionRuntimeTest,
    "SilverChoir.Units.Selection.RuntimeFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBattleUnitSelectionRuntimeTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(BattleUnitSelectionTest::FRuntime(this));
    return true;
}
#endif
