#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Map/MainMenu/MainMenuPlayerController.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/BaseSandboxMap.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "SMS_SceneLibrary.h"
#include "SMS_SceneManager.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "FCS_FreeCameraPawn.h"
#include "MTS_MapTransitionSubsystem.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/PanelWidget.h"
#include "Components/SceneComponent.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GTS_TimeLibrary.h"
#include "GTS_TimeManager.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"

namespace CommandMapPointerTests
{
class FFlow : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TWeakObjectPtr<UGameInstance> GI;
    TWeakObjectPtr<AGameMainMapPlayerController> PC;
    TWeakObjectPtr<ABaseSandboxMap> Map;
    TWeakObjectPtr<AFCS_FreeCameraPawn> Camera;
    TWeakObjectPtr<USMS_SceneManager> Scenes;
    TWeakObjectPtr<AGSMTile3D> Pressed, Selected;
    FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(TEXT("GameScene.OperationsCommandRoom"));
    FGameplayTag OverviewTag = FGameplayTag::RequestGameplayTag(TEXT("GameScene.BaseOverview"));
    int32 Stage = 0, Frames = 0;
    double Start = FPlatformTime::Seconds();
    FVector2D Point, OldCursor;
    FVector ContentBefore, OriginalContent;
    FTransform BoardBefore;
    FFCS_CameraState CameraBefore;
    TWeakObjectPtr<AActor> SimulationProbe;
    FRotator PausedRotation;
    FDateTime PausedCalendar;
    double PausedWorldTime = 0;
    bool bSaved = false;
    float OriginalScale = 1.f;
    FVector ContentPosition() const
    {
        // Follow a real tile rather than reaching into the plugin's protected bounds helper.
        const AGSMTile3D* Tile = Map.IsValid() ? Map->GetTileById(TEXT("L7")) : nullptr;
        return Tile ? Tile->GetActorLocation() : FVector::ZeroVector;
    }
    void Next(int32 Value) { Stage = Value; Frames = 0; }
    bool Idle(FGameplayTag Tag) const
    { return Scenes.IsValid() && Scenes->GetCurrentSceneTag() == Tag && Scenes->GetTransitionState() == ESMS_SceneTransitionState::Idle; }
    void Move(const FVector2D& P) { PC->SetMouseLocation(FMath::RoundToInt(P.X), FMath::RoundToInt(P.Y)); }
    float WheelScale = 0.f;
    void SendKey(FKey Key, EInputEvent Event)
    {
        FInputKeyEventArgs Args; Args.ControllerId = 0; Args.Key = Key;
        Args.Event = Event; Args.AmountDepressed = Event == IE_Released ? 0.f : 1.f;
        Args.EventTimestamp = FPlatformTime::Cycles64(); PC->InputKey(Args);
    }
    void Key(EInputEvent Event) { SendKey(EKeys::LeftMouseButton,Event); }
    void Wheel()
    {
        const uint64 Timestamp = FPlatformTime::Cycles64();
        for (auto Event : {IE_Pressed,IE_Released,IE_Axis})
        {
            FInputKeyEventArgs Args; Args.ControllerId=0; Args.Key=Event==IE_Axis?EKeys::MouseWheelAxis:EKeys::MouseScrollUp;
            Args.Event=Event; Args.AmountDepressed=Event==IE_Released?0.f:1.f; Args.EventTimestamp=Timestamp; PC->InputKey(Args);
        }
    }
    bool Flag(FName Name) const
    {
        auto* Property = FindFProperty<FBoolProperty>(PC->GetClass(), Name);
        return Property && Property->GetPropertyValue_InContainer(PC.Get());
    }
    bool Query(ABaseSandboxMap*& HitMap, AGSMTile3D*& HitTile, FVector& Hit, FVector2D& Position)
    {
        HitMap = nullptr; HitTile = nullptr;
        UFunction* Function = PC->FindFunction(TEXT("CommandMap_QueryPointer"));
        if (!Function) { Test->AddError(TEXT("Missing Blueprint pointer query")); return false; }
        for (const TCHAR* Name : {TEXT("bResolveTile"),TEXT("OutMap"),TEXT("OutTile"),TEXT("OutWorldHit"),TEXT("OutScreenPosition"),TEXT("ReturnValue")})
            if (!Test->TestNotNull(FString(TEXT("Query parameter: "))+Name,FindFProperty<FProperty>(Function,Name))) return false;
        FStructOnScope Params(Function);
        FindFProperty<FBoolProperty>(Function,TEXT("bResolveTile"))->SetPropertyValue_InContainer(Params.GetStructMemory(),true);
        PC->ProcessEvent(Function,Params.GetStructMemory());
        HitMap = Cast<ABaseSandboxMap>(FindFProperty<FObjectPropertyBase>(Function,TEXT("OutMap"))->GetObjectPropertyValue_InContainer(Params.GetStructMemory()));
        HitTile = Cast<AGSMTile3D>(FindFProperty<FObjectPropertyBase>(Function,TEXT("OutTile"))->GetObjectPropertyValue_InContainer(Params.GetStructMemory()));
        Hit = *FindFProperty<FStructProperty>(Function,TEXT("OutWorldHit"))->ContainerPtrToValuePtr<FVector>(Params.GetStructMemory());
        Position = *FindFProperty<FStructProperty>(Function,TEXT("OutScreenPosition"))->ContainerPtrToValuePtr<FVector2D>(Params.GetStructMemory());
        return FindFProperty<FBoolProperty>(Function,TEXT("ReturnValue"))->GetPropertyValue_InContainer(Params.GetStructMemory());
    }
    bool Aim()
    {
        for (const TCHAR* Id : {TEXT("L7"), TEXT("M7"), TEXT("K7"), TEXT("N7")})
        {
            AGSMTile3D* Tile = Map->GetTileById(Id);
            FVector2D Screen;
            if (!Tile || !PC->ProjectWorldLocationToScreen(Tile->GetActorLocation(), Screen, true)) continue;
            Move(Screen);
            ABaseSandboxMap* HitMap = nullptr; AGSMTile3D* HitTile = nullptr; FVector Hit; FVector2D Position;
            if (Query(HitMap, HitTile, Hit, Position) && HitMap == Map.Get() && HitTile)
            { Point = Position; Pressed = HitTile; return true; }
            FHitResult Visible; PC->GetHitResultUnderCursor(ECC_Visibility, true, Visible);
            float MouseX = 0.f, MouseY = 0.f; const bool HasMouse = PC->GetMousePosition(MouseX, MouseY);
            FVector Groove; const bool InGroove = Map->GetMouseHitOnMapGroove(PC.Get(), Groove);
            const auto* Room = Cast<UOperationsCommandRoomWidget>(PC->BaseWidget->GetCurrentSceneUI());
            Test->AddInfo(FString::Printf(TEXT("COMMAND_MAP_AIM stage=%d tile=%s projected=%s mouse=%d(%.1f,%.1f) slate=%s hit=%s component=%s groove=%d uiLoaded=%d panel=%d queryMap=%s queryTile=%s"),
                Stage, Id, *Screen.ToString(), HasMouse, MouseX, MouseY, *FSlateApplication::Get().GetCursorPos().ToString(),
                *GetNameSafe(Visible.GetActor()), *GetNameSafe(Visible.GetComponent()), InGroove,
                Room && Room->IsSceneUILoaded(), Room && Room->IsPointerOverCommandPanel(), *GetNameSafe(HitMap), *GetNameSafe(HitTile)));
        }
        Test->AddError(TEXT("Cannot aim at a visible sandbox tile in the actual viewport")); return false;
    }
    TArray<TFunction<void()>> Steps;
    int32 StepIndex = 0;
    void Add(TFunction<void()> Action) { Steps.Add(MoveTemp(Action)); }
    void Begin()
    {
        if (!Aim()) return;
        ContentBefore = ContentPosition(); Selected = Map->GetSelectedTile(); Key(IE_Pressed);
    }
    void Holding()
    {
        Test->TestTrue(TEXT("Physical left press starts the Blueprint gesture"),Flag(TEXT("bCommandMapPointerPending")));
        CheckNoSelection(TEXT("Press alone never selects"));
    }
    void CheckNoSelection(const TCHAR* Context)
    { Test->TestTrue(Context, Map->GetSelectedTile() == Selected.Get()); }
    void FreshClick()
    {
        Add([this] { Map->ClearSelectedTile(); Begin(); });
        Add([this] { Holding(); Key(IE_Released); });
        Add([this] {
            Test->TestTrue(TEXT("Non-drag release selects its pressed tile"),Map->GetSelectedTile()==Pressed.Get());
            Test->TestFalse(TEXT("Release clears pending state"),Flag(TEXT("bCommandMapPointerPending")));
            Test->TestFalse(TEXT("Release ends plugin drag"),Map->IsDraggingMap());
        });
    }
    void MoveToPanel()
    {
        auto* Room=Cast<UOperationsCommandRoomWidget>(PC->BaseWidget->GetCurrentSceneUI());
        auto* Panel=Room->GetWidgetFromName(TEXT("TimeControlPanel"));
        FSlateApplication::Get().SetCursorPos(Panel->GetCachedGeometry().LocalToAbsolute(Panel->GetCachedGeometry().GetLocalSize()*.5));
    }
    void PrepareSteps()
    {
        Add([this] {
            auto* Probe=PC->GetWorld()->SpawnActor<AActor>();SimulationProbe=Probe;
            auto* Root=NewObject<USceneComponent>(Probe);Probe->SetRootComponent(Root);Probe->AddInstanceComponent(Root);Root->RegisterComponent();
            auto* Movement=NewObject<URotatingMovementComponent>(Probe);Movement->RotationRate=FRotator(0,90,0);Probe->AddInstanceComponent(Movement);Movement->SetUpdatedComponent(Root);Movement->RegisterComponent();
        });
        Add([this] {
            Test->TestFalse(TEXT("World simulation fixture moves before pause"),SimulationProbe->GetActorRotation().IsNearlyZero());
            auto* Room=CastChecked<UOperationsCommandRoomWidget>(PC->BaseWidget->GetCurrentSceneUI());
            Test->TestTrue(TEXT("Room pause is accepted"),Room->PauseTime());
            PausedRotation=SimulationProbe->GetActorRotation();PausedWorldTime=PC->GetWorld()->GetTimeSeconds();PausedCalendar=UGTS_TimeLibrary::GetTimeManager(PC.Get())->GetCurrentTime();
        });
        FreshClick();
        Add([this] { Aim(); WheelScale=Map->GetCurrentMapScale(); Wheel(); });
        Add([this] {
            Test->TestTrue(TEXT("Physical wheel event zooms sandbox"),Map->GetCurrentMapScale()>WheelScale);
            Test->TestEqual(TEXT("Sandbox wheel does not also zoom camera"),Camera->GetCurrentCameraState().TargetArmLength,CameraBefore.TargetArmLength);
            MoveToPanel(); WheelScale=Map->GetCurrentMapScale(); Wheel(); SendKey(EKeys::MiddleMouseButton,IE_Pressed);
        });
        Add([this] {
            Test->TestEqual(TEXT("Wheel over UI cannot zoom sandbox"),Map->GetCurrentMapScale(),WheelScale);
            Test->TestFalse(TEXT("Middle over UI cannot drag"),Map->IsDraggingMap());
            Test->TestFalse(TEXT("Middle over UI cannot rotate camera"),Camera->bMouseRotateMode);
            SendKey(EKeys::MiddleMouseButton,IE_Released);
        });
        Add([this] { Map->ClearSelectedTile(); Begin(); });
        Add([this] { Holding(); Move(Point+FVector2D(2,1)); });
        Add([this] {
            Test->TestTrue(TEXT("Sub-threshold jitter does not pan content"),ContentBefore.Equals(ContentPosition(),.01));
            Test->TestFalse(TEXT("Jitter remains a click"),Flag(TEXT("bCommandMapPointerDragged")));
            CheckNoSelection(TEXT("Holding left never selects before release")); Key(IE_Released);
        });
        Add([this] { Test->TestTrue(TEXT("Jitter release selects the same tile"),Map->GetSelectedTile()==Pressed.Get()); Begin(); });
        Add([this] { Holding(); Move(Point+FVector2D(80,25)); });
        Add([this] {
            Test->TestTrue(TEXT("Threshold crossing starts dragging"),Flag(TEXT("bCommandMapPointerDragged")));
            Test->TestFalse(TEXT("Mouse motion moves map content"),ContentBefore.Equals(ContentPosition(),.1));
            CheckNoSelection(TEXT("Dragging retains selection")); Move(Point);
        });
        Add([this] { Test->TestTrue(TEXT("Returning to origin remains a drag"),Flag(TEXT("bCommandMapPointerDragged"))); Key(IE_Released); });
        Add([this] {
            CheckNoSelection(TEXT("Drag release never selects"));
            Test->TestTrue(TEXT("Board remains fixed"),BoardBefore.Equals(Map->GetActorTransform(),.001));
            Test->TestTrue(TEXT("Camera remains fixed"),CameraBefore.Location.Equals(Camera->GetCurrentCameraState().Location,.001));
            Test->TestEqual(TEXT("Camera arm remains fixed"),Camera->GetCurrentCameraState().TargetArmLength,CameraBefore.TargetArmLength);
        });
        FreshClick();
        Add([this] { Begin(); });
        Add([this] { Holding(); Move(Point+FVector2D(60,15)); Key(IE_Released); });
        Add([this] {
            CheckNoSelection(TEXT("Movement and release in one frame do not become a click"));
            Test->TestFalse(TEXT("Fast release applies final displacement"),ContentBefore.Equals(ContentPosition(),.1));
        });
        FreshClick();
        Add([this] { Begin(); });
        Add([this] { Holding(); PC->FlushPressedKeys(); Key(IE_Released); });
        Add([this] { CheckNoSelection(TEXT("Focus loss cancels selection")); Test->TestFalse(TEXT("Focus loss ends dragging"),Map->IsDraggingMap()); });
        FreshClick();
        Add([this] { Begin(); });
        Add([this] { Holding(); MoveToPanel(); });
        Add([this] {
            Test->TestFalse(TEXT("Entering UI cancels pending gesture"),Flag(TEXT("bCommandMapPointerPending")));
            Test->TestFalse(TEXT("Entering UI ends dragging"),Map->IsDraggingMap());
            CheckNoSelection(TEXT("UI cancellation does not select")); Key(IE_Released);
        });
        FreshClick();
        Add([this] { Aim(); Selected=Map->GetSelectedTile(); MoveToPanel(); Key(IE_Pressed); });
        Add([this] { Test->TestFalse(TEXT("UI press does not arm world gesture"),Flag(TEXT("bCommandMapPointerPending"))); Move(Point); Key(IE_Released); });
        Add([this] {
            CheckNoSelection(TEXT("UI press then map release cannot select through UI"));
            Test->TestTrue(TEXT("Sandbox gestures never unpause simulation"),UGameplayStatics::IsGamePaused(PC.Get()));
            Test->TestTrue(TEXT("Other actors stay frozen throughout clicks, drag and zoom"),SimulationProbe->GetActorRotation().Equals(PausedRotation,.001));
            Test->TestEqual(TEXT("Paused world time stays fixed"),PC->GetWorld()->GetTimeSeconds(),PausedWorldTime);
            Test->TestEqual(TEXT("Paused calendar stays fixed"),UGTS_TimeLibrary::GetTimeManager(PC.Get())->GetCurrentTime(),PausedCalendar);
            CastChecked<UOperationsCommandRoomWidget>(PC->BaseWidget->GetCurrentSceneUI())->NormalSpeed();
        });
        Add([this] {
            Test->TestFalse(TEXT("Normal resumes world"),UGameplayStatics::IsGamePaused(PC.Get()));
            Test->TestFalse(TEXT("Other actors resume after normal"),SimulationProbe->GetActorRotation().Equals(PausedRotation,.001));Begin();
        });
        Add([this] { Holding(); Move(Point+FVector2D(40,12)); });
        Add([this] { Test->TestTrue(TEXT("Scene-exit fixture is dragging"),Flag(TEXT("bCommandMapPointerDragged"))); Scenes->SwitchScene(OverviewTag); });
        Add([this] {
            Test->TestFalse(TEXT("Scene departure cancels pending input"),Flag(TEXT("bCommandMapPointerPending")));
            Test->TestFalse(TEXT("Scene departure ends dragging"),Map->IsDraggingMap());
            Test->TestFalse(TEXT("No stale plugin click suppression"),Map->ConsumeTileClickSuppressionAfterDrag());
            Key(IE_Released);
        });
        Add([this] { CheckNoSelection(TEXT("Scene departure consumes stale release")); });
    }
public:
    FFlow(FAutomationTestBase* InTest, UGameInstance* InGI) : Test(InTest), GI(InGI)
    { OldCursor = FSlateApplication::Get().GetCursorPos(); }
    ~FFlow() override
    {
        if(SimulationProbe.IsValid())SimulationProbe->Destroy();
        if(PC.IsValid() && PC->BaseWidget)if(auto* Room=Cast<UOperationsCommandRoomWidget>(PC->BaseWidget->GetCurrentSceneUI()))Room->NormalSpeed();
        if (PC.IsValid()) PC->FlushPressedKeys();
        if (bSaved && Map.IsValid())
        {
            Map->SetMapScaleAtWorldLocation(OriginalContent, OriginalScale);
            Map->PanMapByWorldDelta(OriginalContent - ContentPosition());
        }
        if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetCursorPos(OldCursor);
    }
    bool Update() override
    {
        if (!GI.IsValid()) { Test->AddError(TEXT("Lost GameInstance")); return true; }
        if (FPlatformTime::Seconds() - Start > 90.)
        { Test->AddError(FString::Printf(TEXT("Command map pointer timed out at stage %d"), Stage)); return true; }
        ++Frames;
        UWorld* World = GI->GetWorld();
        switch (Stage)
        {
        case 0:
        {
            auto* State = World ? World->GetGameState<AGameMainMapGameState>() : nullptr;
            if (!State || !State->IsBaseMap() || GI->GetSubsystem<UMTS_MapTransitionSubsystem>()->IsTransitionInProgress()) return false;
            PC = Cast<AGameMainMapPlayerController>(World->GetFirstPlayerController());
            Scenes = USMS_SceneLibrary::GetSceneManager(World);
            Camera = UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(World);
            if (!PC.IsValid() || !Camera.IsValid() || !Idle(OverviewTag) || Camera->IsMovingToCameraState()) return false;
            for (TActorIterator<ABaseSandboxMap> It(World); It; ++It) { Map = *It; break; }
            if (!Test->TestNotNull(TEXT("Real sandbox"), Map.Get())) return true;
            if (!Test->TestNotNull(TEXT("Tracked content tile L7"), Map->GetTileById(TEXT("L7")))) return true;
            OriginalContent = ContentPosition(); OriginalScale = Map->GetCurrentMapScale(); bSaved = true;
            ABaseSandboxMap* HitMap; AGSMTile3D* HitTile; FVector Hit; FVector2D Position;
            Test->TestFalse(TEXT("Command gesture is disabled outside the room"), Query(HitMap, HitTile, Hit, Position));
            if (!Test->TestTrue(TEXT("Enter actual command room"), Scenes->SwitchScene(RoomTag))) return true;
            Next(1); return false;
        }
        case 1:
            if (!Idle(RoomTag)) return false;
            for (const TCHAR* Name : {TEXT("CommandMap_BeginPointer"),TEXT("CommandMap_UpdatePointer"),TEXT("CommandMap_ReleasePointer"),TEXT("CommandMap_CancelPointer")})
            {
                auto* F=PC->FindFunction(Name);
                Test->TestTrue(FString(Name)+TEXT(" is Blueprint-owned code"),F && F->GetOuterUClass()==PC->GetClass() && !F->HasAnyFunctionFlags(FUNC_Native) && !F->Script.IsEmpty());
            }
            BoardBefore=Map->GetActorTransform(); CameraBefore=Camera->GetCurrentCameraState();
            Map->SetMapScaleAtWorldLocation(OriginalContent,Map->GetEffectiveMinMapScale()*1.8f);
            Test->TestTrue(TEXT("Fixture has scrollable content"),Map->GetCurrentMapScale()>Map->GetEffectiveMinMapScale());
            PrepareSteps(); Next(2); return false;
        case 2:
            // Enhanced Input dispatches events during the next input tick. Wait for
            // real frames between physical press, motion and release assertions.
            if (Frames<3) return false;
            if (StepIndex<Steps.Num()) { Steps[StepIndex++](); Frames=0; return false; }
            Next(3); return false;
        case 3:
            if (!Idle(OverviewTag)) return false;
            Test->AddInfo(TEXT("COMMAND_MAP_POINTER_OK actual Enhanced Input + Blueprint: release click, threshold, sticky drag, fast release, UI, focus and scene exit"));
            return true;
        }
        return false;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCommandMapPointerTest, "SilverChoir.Input.CommandMapPointer",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FCommandMapPointerTest::RunTest(const FString& Parameters)
{
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game && Context.World())
            if (auto* Menu = Cast<AMainMenuPlayerController>(Context.World()->GetFirstPlayerController()))
            {
                UGameInstance* Instance = Context.World()->GetGameInstance();
                Menu->HandleMenuAction(EMainMenuAction::NewGame);
                ADD_LATENT_AUTOMATION_COMMAND(CommandMapPointerTests::FFlow(this, Instance)); return true;
            }
    AddError(TEXT("Run CommandMapPointer alone from MainMenu -game.")); return false;
}
#endif
