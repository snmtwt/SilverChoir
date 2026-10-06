#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "FCS_FreeCameraPawn.h"
#include "GameFramework/WorldSettings.h"
#include "Framework/Application/SlateApplication.h"
#include "InputKeyEventArgs.h"
#include "Map/MainMenu/MainMenuPlayerController.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/BaseSandboxMap.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"
#include "SMS_SceneBase.h"
#include "UObject/UnrealType.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsSquadEntryWidget.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "Data/Vehicles/VehicleDataLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GTS_TimeSubsystem.h"
#include "GTS_TimeManager.h"
#include "SMS_SceneManager.h"
#include "SMS_SceneLibrary.h"
#include "MTS_MapTransitionSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Class.h"
#include "UnrealClient.h"

namespace OperationsRoomTests
{
template<class T> T* Field(UUserWidget* Widget, const TCHAR* Name)
{
    return Widget ? Cast<T>(Widget->GetWidgetFromName(Name)) : nullptr;
}

class FRoomFlow : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TWeakObjectPtr<UGameInstance> Instance;
    TWeakObjectPtr<AGameMainMapPlayerController> PC;
    TWeakObjectPtr<ABaseSandboxMap> Map;
    TWeakObjectPtr<UGTS_TimeManager> Clock;
    TWeakObjectPtr<UGTS_TimeSubsystem> TimeSubsystem;
    TWeakObjectPtr<USMS_SceneManager> Scenes;
    TWeakObjectPtr<AFCS_FreeCameraPawn> Camera;
    TStrongObjectPtr<UOperationsCommandRoomWidget> Room;
    TStrongObjectPtr<UOperationsCommandRoomWidget> PreviousRoom;
    FGameplayTag RoomTag = FGameplayTag::RequestGameplayTag(TEXT("GameScene.OperationsCommandRoom"));
    FGameplayTag OverviewTag;
    double Started = FPlatformTime::Seconds();
    double StepStarted = Started;
    double OriginalScale = 1.;
    bool bOriginalPaused = false;
    bool bClockSaved = false;
    bool bVisual = FParse::Param(FCommandLine::Get(), TEXT("OperationsVisualReview"));
    FVector2D PreviousCursor = FVector2D::ZeroVector;
    bool bCursorSaved = false;
    int32 Stage = 0;
    int32 Visit = 0;
    int32 InitialTextBindings = 0;
    FDateTime BeforeAdvance;
    FFCS_CameraState BaselineCameraState;
    FFCS_CameraState BeforeEntryCameraState;
    FFCS_CameraState OverheadCameraState;
    FFCS_CameraState ExitOverheadCameraState;
    FFCS_CameraState InteriorCameraState;
    float BaselineMinPitch = 0.f, BaselineMaxPitch = 0.f;
    float BeforeEntryMinPitch = 0.f, BeforeEntryMaxPitch = 0.f;
    bool bBaselineMovementDisabled = false, bBaselineRotationDisabled = false;
    bool bBeforeEntryMovementDisabled = false, bBeforeEntryRotationDisabled = false;
    bool bCameraBaselineSaved = false;
    int32 TranslationSamples = 0;
    float MaxArmDriftWhileTranslating = 0.f;
    float MaxYawDriftWhileTranslating = 0.f;
    float MaxPitchDriftWhileTranslating = 0.f;
    bool bObservedZoomAtDestination = false;
    float AppearanceReviewScale = 1.f;


    TArray<FGuid> FixtureSquads;
    double PausedWorldTime = 0;
    void SeedSquads()
    {
        auto* Squads = UPlayerSquadLibrary::GetPlayerSquadManager(Room.Get());
        auto* Units = UPlayerUnitLibrary::GetPlayerUnitManager(Room.Get());
        FText Error;
        TArray<FVehicleData> Vehicles;
        Test->TestTrue(TEXT("Create test-only vehicles from actual preview templates"), UVehicleDataLibrary::CreateVehicleDataFromTable(
            LoadObject<UDataTable>(nullptr,TEXT("/Game/System/Data/Vehicles/DT_VehicleTemplates")), {TEXT("Sedan_4Seat"),TEXT("SUV_6Seat")}, Vehicles, Error, TEXT("T5")));
        Test->TestTrue(TEXT("Load canonical fixture vehicles"),Units->LoadVehicleData(Vehicles,Error));
        for (int32 S = 0; S < 3; ++S)
        {
            FGuid Id;
            UTexture2D* Icon = LoadObject<UTexture2D>(nullptr, S==0 ? TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/Icons/T_Squad_Raven") : S==1 ? TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/Icons/T_Squad_Wolf") : TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/Icons/T_Squad_Northstar"));
            Test->TestTrue(TEXT("Create resident fixture"),Squads->CreateSquad(FText::FromString(S==0?TEXT("第一小队"):S==1?TEXT("第二小队"):TEXT("第三小队")),Icon,TEXT("T5"),Id,Error));
            FixtureSquads.Add(Id);
            for (int32 M=0;M<4-S;++M)
            {
                FUnitTemplate Template;
                Template.Profile.CodeName=FText::FromString(FString::Printf(TEXT("队员 %02d"),M+1));
                Template.Profile.PortraitTexture=LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/System/SubSystem/PlayerUnitSubSystem/Portraits/T_TestPortrait_%02d"),M%4+1));
                Template.Profile.PortraitCropFocus=FVector2D(.5,.4);Template.Profile.PortraitCropScale=1.f;
                auto Unit=UPlayerUnitLibrary::CreateUnitDataFromTemplate(Template);Unit.RuntimeData.TileId=TEXT("T5");
                Test->TestTrue(TEXT("Load member canonical data"),Units->LoadUnitData({Unit},Error));
                Test->TestTrue(TEXT("Assign member"),Squads->AddUnitToSquad(Unit.UnitId,Id,Error));
            }
            if(S!=1)for(const auto& Vehicle:Units->GetVehicleDataAtTile(TEXT("T5")))
            {
                FSquadData Occupant;
                if(Vehicle.Attributes.PassengerCapacity<4-S || Squads->GetVehicleSquad(Vehicle.VehicleId,Occupant))continue;
                TArray<FGuid> Removed;Test->TestTrue(TEXT("Assign real vehicle preview"),Squads->SetSquadVehicle(Id,Vehicle,Removed,Error));break;
            }
        }
    }
    void SlateClick(UWidget* Widget)
    {
        // Route real widget input; offscreen tests do not depend on OS window activation.
        const FKeyEvent Press(EKeys::Enter,FModifierKeysState(),0,false,0,0);
        const auto Slate=Widget->TakeWidget();
        Test->TestTrue(TEXT("Slate time control accepts input"),Slate->OnKeyDown(Widget->GetCachedGeometry(),Press).IsEventHandled());
        Slate->OnKeyUp(Widget->GetCachedGeometry(),Press);
    }

    void CheckTileBorder(const TCHAR* Id, bool bSelected)
    {
        AGSMTile3D* Tile = Map->GetTileById(Id);
        UDecalComponent* Decal = Tile ? Tile->FindComponentByClass<UDecalComponent>() : nullptr;
        if (!Test->TestNotNull(FString(Id) + TEXT(" has a border decal"), Decal)) return;
        auto* Material = Cast<UMaterialInstanceDynamic>(Decal->GetDecalMaterial());
        if (!Test->TestNotNull(FString(Id) + TEXT(" has an independent border material"), Material)) return;
        float Selected = -1.f;
        Test->TestTrue(TEXT("Border material implements the selection input"), Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SelectedAmount")), Selected));
        Test->TestEqual(FString(Id) + TEXT(" border selection matches map selection"), Selected, bSelected ? 1.f : 0.f);
        Test->TestEqual(FString(Id) + TEXT(" selected border draws above neighbours"), Decal->SortOrder, bSelected ? 10 : 0);
    }

    void PrepareEntryCamera()
    {
        // Only the second visit changes limits; restoration must use runtime
        // values rather than the camera asset's defaults.
        if (Visit == 1) Camera->SetPitchLimits(-72.f, -18.f);
        Camera->GetPitchLimits(BeforeEntryMinPitch, BeforeEntryMaxPitch);
        bBeforeEntryMovementDisabled = Camera->IsCameraMovementDisabled();
        bBeforeEntryRotationDisabled = Camera->IsCameraRotationDisabled();
        Camera->SetCameraMovementDisabled(true);
        Camera->SetCameraRotationDisabled(true);
        FFCS_CameraState Setup = InteriorCameraState;
        Setup.Location += FVector(1200., 600., 400.);
        Setup.Yaw += 35.f;
        Setup.Pitch = FMath::Clamp(InteriorCameraState.Pitch + 25.f, BeforeEntryMinPitch, BeforeEntryMaxPitch);
        Setup.TargetArmLength = FMath::Max(BaselineCameraState.TargetArmLength, InteriorCameraState.TargetArmLength + 1500.f);
        Setup.MoveSpeed = 8000.f; Setup.RotationSpeed = 720.f; Setup.ZoomSpeed = 8000.f;
        Camera->MoveToCameraState(Setup, FFCS_OnCameraMoveFinished());
        TranslationSamples = 0;
        MaxArmDriftWhileTranslating = MaxYawDriftWhileTranslating = MaxPitchDriftWhileTranslating = 0.f;
        bObservedZoomAtDestination = false;
    }
    void ObserveEntryCamera()
    {
        if (!Camera.IsValid() || Scenes->GetCurrentSceneTag() != RoomTag) return;
        const FFCS_CameraState Current = Camera->GetCurrentCameraState();
        const float ArmChange = FMath::Abs(Current.TargetArmLength - OverheadCameraState.TargetArmLength);
        if (FVector::Dist(Current.Location, OverheadCameraState.Location) > 5.f)
        {
            ++TranslationSamples;
            MaxArmDriftWhileTranslating = FMath::Max(MaxArmDriftWhileTranslating, ArmChange);
            MaxYawDriftWhileTranslating = FMath::Max(MaxYawDriftWhileTranslating,
                FMath::Abs(FMath::FindDeltaAngleDegrees(Current.Yaw, BeforeEntryCameraState.Yaw)));
            MaxPitchDriftWhileTranslating = FMath::Max(MaxPitchDriftWhileTranslating,
                FMath::Abs(Current.Pitch - BeforeEntryCameraState.Pitch));
        }
        else if (ArmChange > 1.f) bObservedZoomAtDestination = true;
    }
    void CheckCameraState(const FFCS_CameraState& Expected, const TCHAR* Phase)
    {
        const FFCS_CameraState Current = Camera->GetCurrentCameraState();
        Test->TestTrue(FString(Phase) + TEXT(": position matches"), Current.Location.Equals(Expected.Location, 5.f));
        Test->TestTrue(FString(Phase) + TEXT(": yaw matches"), FMath::Abs(FMath::FindDeltaAngleDegrees(Current.Yaw, Expected.Yaw)) < .2f);
        Test->TestTrue(FString(Phase) + TEXT(": pitch matches"), FMath::IsNearlyEqual(Current.Pitch, Expected.Pitch, .2f));
        Test->TestTrue(FString(Phase) + TEXT(": arm length matches"), FMath::IsNearlyEqual(Current.TargetArmLength, Expected.TargetArmLength, 1.f));
        Test->TestFalse(FString(Phase) + TEXT(": scripted camera move is finished"), Camera->IsMovingToCameraState());
    }
    void CheckEnteredCamera()
    {
        Test->TestTrue(TEXT("Entry observes a separate approach to authored overhead state"), TranslationSamples > 1);
        Test->TestTrue(TEXT("Entry zoom starts after position is reached"), bObservedZoomAtDestination);
        CheckCameraState(InteriorCameraState, TEXT("Entered room"));
        float CurrentMin = 0.f, CurrentMax = 0.f;
        Camera->GetPitchLimits(CurrentMin, CurrentMax);
        Test->TestEqual(TEXT("Room temporarily permits a -90 degree top-down pitch"), CurrentMin, FMath::Min(BeforeEntryMinPitch, -90.f));
        Test->TestEqual(TEXT("Room retains the previous maximum pitch"), CurrentMax, BeforeEntryMaxPitch);
        Test->TestTrue(TEXT("Room locks manual movement"), Camera->IsCameraMovementDisabled());
        Test->TestTrue(TEXT("Room locks manual rotation"), Camera->IsCameraRotationDisabled());
        // The public manual setter respects the rotation gate. Probe the new
        // bound and restore the authored view before the next rendered frame.
        Camera->SetCameraRotationDisabled(false);
        Camera->SetCameraPitch(-90.f);
        Test->TestTrue(TEXT("The room camera can actually reach -90 degrees"),
            FMath::IsNearlyEqual(Camera->GetCurrentCameraState().Pitch, -90.f, .05f));
        Camera->SetCameraPitch(InteriorCameraState.Pitch);
        Camera->SetCameraRotationDisabled(true);
        Test->AddInfo(FString::Printf(TEXT("OPERATIONS_CAMERA_ENTRY visit=%d positionSamples=%d armDrift=%.5f yawDrift=%.5f pitchDrift=%.5f priorLimits=(%.1f,%.1f)"),
            Visit + 1, TranslationSamples, MaxArmDriftWhileTranslating, MaxYawDriftWhileTranslating,
            MaxPitchDriftWhileTranslating, BeforeEntryMinPitch, BeforeEntryMaxPitch));
    }
    void CheckReturnedPitchLimits()
    {
        // The overview Blueprint starts its own camera move and control policy.
        // Pitch limits remain observable after that scene has finished entering.
        float CurrentMin = 0.f, CurrentMax = 0.f;
        Camera->GetPitchLimits(CurrentMin, CurrentMax);
        Test->TestEqual(TEXT("Exit restores the actual previous minimum pitch"), CurrentMin, BeforeEntryMinPitch);
        Test->TestEqual(TEXT("Exit restores the actual previous maximum pitch"), CurrentMax, BeforeEntryMaxPitch);
    }
    void RestoreCameraBaseline()
    {
        Camera->CancelCameraStateMove();
        Camera->SetCameraMovementDisabled(true);
        Camera->SetCameraRotationDisabled(true);
        Camera->SetPitchLimits(BaselineMinPitch, BaselineMaxPitch);
        FFCS_CameraState Restore = BaselineCameraState;
        Restore.MoveSpeed = 8000.f; Restore.RotationSpeed = 720.f; Restore.ZoomSpeed = 8000.f;
        Camera->MoveToCameraState(Restore, FFCS_OnCameraMoveFinished());
    }

    void Next(int32 NextStage)
    {
        Stage = NextStage; StepStarted = FPlatformTime::Seconds();
    }
    bool IdleAt(FGameplayTag Tag) const
    {
        return Scenes.IsValid() && Scenes->GetTransitionState() == ESMS_SceneTransitionState::Idle
            && Scenes->GetCurrentSceneTag() == Tag;
    }
    UBaseMapWidget* Shell() const { return PC.IsValid() ? PC->BaseWidget.Get() : nullptr; }
    void Cursor(FVector2D Position)
    {
        if (!bCursorSaved) { PreviousCursor = FSlateApplication::Get().GetCursorPos(); bCursorSaved = true; }
        FSlateApplication::Get().SetCursorPos(Position);
    }
    void Screenshot(const TCHAR* Name)
    {
        if (!bVisual) { return; }
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("OperationsCommandRoomUI");
        IFileManager::Get().MakeDirectory(*Directory, true);
        FScreenshotRequest::RequestScreenshot(Directory / Name, true, false);
    }
    bool CheckFields()
    {
        bool bOK = true;
        for (const TCHAR* Name : {TEXT("RightPanel"), TEXT("TimeControlPanel"), TEXT("TileInfoPanel"), TEXT("TileInfoContent")})
            bOK &= Test->TestNotNull(Name, Field<UPanelWidget>(Room.Get(), Name));
        for (const TCHAR* Name : {TEXT("DateText"), TEXT("ClockText")})
        {
            auto* Text = Field<UTextBlock>(Room.Get(), Name);
            bOK &= Test->TestNotNull(Name, Text);
            if (Text) { bOK &= Test->TestFalse(TEXT("Time text is populated"), Text->GetText().IsEmpty()); }
        }
        bOK &= Test->TestNull(TEXT("Progress bar removed"), Field<UProgressBar>(Room.Get(), TEXT("DayProgress")));
        bOK &= Test->TestNotNull(TEXT("Normal speed button"), Field<USelectionButtonWidget>(Room.Get(), TEXT("NormalSpeedButton")));
        bOK &= Test->TestNotNull(TEXT("Fast forward button"), Field<USelectionButtonWidget>(Room.Get(), TEXT("FastForwardButton")));
        for (const TCHAR* Name : {TEXT("PauseButton"), TEXT("NormalSpeedButton"), TEXT("FastForwardButton")})
        {
            auto* Button = Field<USelectionButtonWidget>(Room.Get(), Name);
            auto* Icon = Button ? Cast<UImage>(Button->GetWidgetFromName(TEXT("IconImage"))) : nullptr;
            bOK &= Test->TestNotNull(TEXT("Time button Designer contains its icon"), Icon);
            if (Icon) bOK &= Test->TestTrue(TEXT("Time icon is visible with a texture"), Icon->IsVisible() && Icon->GetBrush().GetResourceObject() != nullptr);
        }
        return bOK;
    }
    void CheckBlueprintEventFlow()
    {
        const auto CheckScript = [this](UObject* Object, const TCHAR* Name)
        {
            const UFunction* Function = Object ? Object->FindFunction(FName(Name)) : nullptr;
            bool bHasBlueprintScript = Function && !Function->Script.IsEmpty();
#if UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
            bHasBlueprintScript |= Function && Function->EventGraphFunction != nullptr;
#endif
            Test->TestTrue(FString::Printf(TEXT("%s.%s has an editable Blueprint implementation"), *GetNameSafe(Object), Name),
                Function && !Function->HasAnyFunctionFlags(FUNC_Native) && bHasBlueprintScript);
        };
        for (const TCHAR* Name : {TEXT("LoadSceneUI"), TEXT("UnloadSceneUI"), TEXT("OnCommandTimeChanged"),
            TEXT("OnCommandTimeScaleChanged"), TEXT("OnCommandTimePausedChanged"), TEXT("OnCommandTileSelectionChanged")})
            CheckScript(Room.Get(), Name);
        for (const TCHAR* Name : {TEXT("EnterScene"), TEXT("LeaveScene"), TEXT("Room_CameraPositioned"), TEXT("Room_CameraEntered"),
            TEXT("Room_CameraReturned"), TEXT("Room_UILoaded"), TEXT("Room_UIUnloaded"),
            TEXT("Room_TryFinishEnter"), TEXT("Room_TryFinishLeave"), TEXT("Room_DeferredLeave")})
            CheckScript(Scenes->GetCurrentScene(), Name);
    }
    void CheckPointerGeometry(bool bSelected)
    {
        if (!FSlateApplication::IsInitialized()) { Test->AddError(TEXT("Slate is required for command panel input regression")); return; }
        auto* Right = Field<UPanelWidget>(Room.Get(), TEXT("RightPanel"));
        auto* Time = Field<UPanelWidget>(Room.Get(), TEXT("TimeControlPanel"));
        const FGeometry& Content = Shell()->SceneContent->GetCachedGeometry();
        const FGeometry& RightGeometry = Right->GetCachedGeometry();
        const FVector2D ContentSize = Content.GetLocalSize();
        const FVector2D RightMin = Content.AbsoluteToLocal(RightGeometry.LocalToAbsolute(FVector2D::ZeroVector));
        Test->TestTrue(TEXT("Command panel stays on the right, leaving the map center clear"),
            ContentSize.X > 0 && RightMin.X > ContentSize.X * .5 && RightGeometry.GetLocalSize().X <= 361.);
        Cursor(Content.LocalToAbsolute(FVector2D(ContentSize.X * .25, ContentSize.Y * .5)));
        Test->TestFalse(TEXT("Central map area is not blocked by the room root"), Room->IsPointerOverCommandPanel());
        Cursor(RightGeometry.LocalToAbsolute(FVector2D(RightGeometry.GetLocalSize().X * .5, 300.)));
        Test->TestEqual(TEXT("Lower right blank area only blocks after selection"), Room->IsPointerOverCommandPanel(), bSelected);
        Cursor(Time->GetCachedGeometry().LocalToAbsolute(Time->GetCachedGeometry().GetLocalSize() * .5));
        Test->TestTrue(TEXT("Visible time panel blocks world input"), Room->IsPointerOverCommandPanel());
        // Input interactions are checked across real input frames by CommandMapPointer.
        Test->TestFalse(TEXT("Room disables Actor click dispatch so Blueprint release owns selection"),PC->bEnableClickEvents);
        Cursor(Content.LocalToAbsolute(FVector2D(ContentSize.X * .25, ContentSize.Y * .5)));
    }

public:
    FRoomFlow(FAutomationTestBase* InTest, UGameInstance* InInstance) : Test(InTest), Instance(InInstance) {}
    ~FRoomFlow() override
    {
        if (bCameraBaselineSaved && Camera.IsValid())
        {
            RestoreCameraBaseline();
            Camera->SetCameraMovementDisabled(bBaselineMovementDisabled);
            Camera->SetCameraRotationDisabled(bBaselineRotationDisabled);
        }
        if (Room.IsValid()) Room->ReleaseWorldPause();
        if (bClockSaved && Clock.IsValid()) { Clock->SetTimeScale(OriginalScale); Clock->SetTimePaused(bOriginalPaused); }
        if (bCursorSaved && FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetCursorPos(PreviousCursor); }
    }
    bool Update() override
    {
        if (!Instance.IsValid()) { Test->AddError(TEXT("Operations regression lost GameInstance")); return true; }
        if (FPlatformTime::Seconds() - Started > 180.)
        {
            Test->AddError(FString::Printf(TEXT("Operations room flow timed out at stage %d: current=%s pending=%s error=%s"), Stage,
                Scenes.IsValid() ? *Scenes->GetCurrentSceneTag().ToString() : TEXT("none"),
                Scenes.IsValid() ? *Scenes->GetPendingSceneTag().ToString() : TEXT("none"),
                Scenes.IsValid() ? *Scenes->GetLastError().ToString() : TEXT("none")));
            return true;
        }
        UWorld* World = Instance->GetWorld();
        const double Elapsed = FPlatformTime::Seconds() - StepStarted;
        switch (Stage)
        {
        case 0:
        {
            auto* State = World ? World->GetGameState<AGameMainMapGameState>() : nullptr;
            auto* Transition = Instance->GetSubsystem<UMTS_MapTransitionSubsystem>();
            if (!State || !State->IsBaseMap() || State->ActiveMapID != TEXT("Base") || Transition->IsTransitionInProgress()) { return false; }
            PC = Cast<AGameMainMapPlayerController>(World->GetFirstPlayerController());
            Scenes = USMS_SceneLibrary::GetSceneManager(World);
            Camera = UFCS_FreeCameraBlueprintLibrary::GetFreeCamera(World);
            TimeSubsystem = Instance->GetSubsystem<UGTS_TimeSubsystem>();
            Clock = TimeSubsystem.IsValid() ? TimeSubsystem->GetManager() : nullptr;
            if (!Test->TestNotNull(TEXT("Real base UI"), Shell()) || !Test->TestNotNull(TEXT("Global scene manager"), Scenes.Get())
                || !Test->TestNotNull(TEXT("Game clock"), Clock.Get())
                || !Test->TestNotNull(TEXT("Real free camera"), Camera.Get())) { return true; }
            if (Scenes->GetTransitionState() != ESMS_SceneTransitionState::Idle) { return false; }
            if (Camera->IsMovingToCameraState()) { return false; }
            for (TActorIterator<ABaseSandboxMap> It(World); It; ++It) { Map = *It; break; }
            if (!Test->TestNotNull(TEXT("Real streamed sandbox"), Map.Get())) { return true; }
            UClass* OverviewClass = LoadClass<USMS_SceneBase>(nullptr, TEXT("/Game/System/Map/BaseMap/Scene/BP_基地全景_Scene.BP_基地全景_Scene_C"));
            UClass* OperationsClass = LoadClass<USMS_SceneBase>(nullptr,
                TEXT("/Game/System/Map/BaseMap/Scene/BP_作战指挥室_Scene.BP_作战指挥室_Scene_C"));
            if (!Test->TestNotNull(TEXT("Base overview class"), OverviewClass)
                || !Test->TestNotNull(TEXT("Operations scene class"), OperationsClass)) { return true; }
            OverviewTag = OverviewClass->GetDefaultObject<USMS_SceneBase>()->SceneTag;
            const auto* Interior = FindFProperty<FStructProperty>(OperationsClass, TEXT("InteriorCameraState"));
            if (!Test->TestNotNull(TEXT("Blueprint owns the interior camera configuration"), Interior)) return true;
            InteriorCameraState = *Interior->ContainerPtrToValuePtr<FFCS_CameraState>(OperationsClass->GetDefaultObject());
            const auto* Overhead = FindFProperty<FStructProperty>(OperationsClass, TEXT("OverheadCameraState"));
            if (!Test->TestNotNull(TEXT("Blueprint owns the overhead camera configuration"), Overhead)) return true;
            OverheadCameraState = *Overhead->ContainerPtrToValuePtr<FFCS_CameraState>(OperationsClass->GetDefaultObject());
            const auto* ExitOverhead = FindFProperty<FStructProperty>(OperationsClass,TEXT("ExitOverheadCameraState"));
            if(!Test->TestNotNull(TEXT("Separate exit overhead camera configuration"),ExitOverhead))return true;
            ExitOverheadCameraState=*ExitOverhead->ContainerPtrToValuePtr<FFCS_CameraState>(OperationsClass->GetDefaultObject());
            BaselineCameraState = Camera->GetCurrentCameraState();
            Camera->GetPitchLimits(BaselineMinPitch, BaselineMaxPitch);
            bBaselineMovementDisabled = Camera->IsCameraMovementDisabled();
            bBaselineRotationDisabled = Camera->IsCameraRotationDisabled();
            bCameraBaselineSaved = true;
            OriginalScale = Clock->GetTimeScale(); bOriginalPaused = Clock->IsTimePaused(); bClockSaved = true;
            InitialTextBindings = TimeSubsystem->GetRegisteredTimeTextBlockCount();
            Map->ClearSelectedTile();
            PrepareEntryCamera();
            Next(9); return false;
        }
        case 9:
            if (Camera->IsMovingToCameraState()) { return false; }
            Camera->SetCameraMovementDisabled(bBeforeEntryMovementDisabled);
            Camera->SetCameraRotationDisabled(bBeforeEntryRotationDisabled);
            BeforeEntryCameraState = Camera->GetCurrentCameraState();
            Camera->GetPitchLimits(BeforeEntryMinPitch, BeforeEntryMaxPitch);
            if (!Test->TestTrue(TEXT("Camera fixture requires a visible translation before entry"),
                    FVector::Dist(BeforeEntryCameraState.Location, InteriorCameraState.Location) > 100.f)
                || !Test->TestTrue(TEXT("Camera fixture requires a visible zoom-in after translation"),
                    BeforeEntryCameraState.TargetArmLength > InteriorCameraState.TargetArmLength + 100.f)) { return true; }
            if (!Test->TestTrue(TEXT("Real manager accepts operations room entry"), Scenes->SwitchScene(RoomTag))) { return true; }
            Next(1); return false;
        case 1:
            ObserveEntryCamera();
            if (!IdleAt(RoomTag)) { return false; }
            Room.Reset(Cast<UOperationsCommandRoomWidget>(Shell()->GetCurrentSceneUI()));
            if (!Test->TestNotNull(TEXT("Actual scene entry mounts operations UI"), Room.Get()) || !CheckFields()) { return true; }
            CheckBlueprintEventFlow();
            CheckEnteredCamera();
            Test->TestTrue(TEXT("Scene lifecycle lives directly in Blueprint"), Scenes->GetCurrentScene()->GetClass()->GetSuperClass() == USMS_SceneBase::StaticClass());
            Test->TestTrue(TEXT("Room loading completed before scene is idle"), Room->IsSceneUILoaded());
            Test->TestTrue(TEXT("Return is available after camera and UI finish"), Shell()->bBackButtonVisible);
            Test->TestEqual(TEXT("Each visit binds date and clock exactly once"), TimeSubsystem->GetRegisteredTimeTextBlockCount(), InitialTextBindings + 2);
            if (Visit == 1)
            {
                Test->TestEqual(TEXT("Re-entry always defaults to T5"), Room->GetSelectedTile()->GetTileId(), FName(TEXT("T5")));
                Test->TestEqual(TEXT("Re-entry preserves the selected time speed"), Clock->GetTimeScale(), 240.);
                Test->TestFalse(TEXT("Re-entry does not pause the clock"), Clock->IsTimePaused());
                Test->TestTrue(TEXT("Re-entry creates a fresh widget"), Room.Get() != PreviousRoom.Get());
                Test->TestTrue(TEXT("Re-entry reads the map's existing selection"), Room->HasSelectedTile());
                Room->ClearSelectedTile();
                Test->TestNull(TEXT("Clear selection updates map"), Map->GetSelectedTile());
                Test->TestFalse(TEXT("Clear selection hides detail"), Room->HasSelectedTile());
                Next(7); return false;
            }
            Test->TestTrue(TEXT("Scene Blueprint selects T5 on entry"), Room->GetSelectedTile() && Room->GetSelectedTile()->GetTileId() == TEXT("T5"));
            SeedSquads();
            Test->TestEqual(TEXT("Only the selected tile squads are listed"), Room->GetSelectedTileSquadCount(), 3);
            Test->TestTrue(TEXT("Occupied tile enables entry"), Room->EnterMapButton->GetIsEnabled());
            Next(100); return false;

        case 100:
            if(Elapsed<1.)return false;
            Test->TestEqual(TEXT("Resident row count"),Room->SquadList->GetChildrenCount(),3);
            Screenshot(TEXT("operations_squads_collapsed.png"));
            Next(101);return false;
        case 101:
            if(Elapsed<.3)return false;
            CastChecked<UOperationsSquadEntryWidget>(Room->SquadList->GetChildAt(0))->HeaderButton->OnClicked.Broadcast();
            Next(102);return false;
        case 102:
        {
            if(Elapsed<.4)return false;
            auto* Row=CastChecked<UOperationsSquadEntryWidget>(Room->SquadList->GetChildAt(0));
            Test->TestEqual(TEXT("First squad expands by Blueprint click event"),Room->ExpandedSquadId,Row->SquadId);
            Test->TestEqual(TEXT("All members loaded"),Row->Members->GetChildrenCount(),4);
            Test->TestTrue(TEXT("Expansion reaches full height"),FMath::IsNearlyEqual(Row->MemberRevealSize->GetHeightOverride(),Row->MemberRowHeight*4.f,.1f));
            Screenshot(TEXT("operations_squads_expanded.png"));Next(103);return false;
        }
        case 103:
        {
            if(Elapsed<.4)return false;
            auto* Second=CastChecked<UOperationsSquadEntryWidget>(Room->SquadList->GetChildAt(1));
            Second->HeaderButton->OnClicked.Broadcast();
            Test->TestFalse(TEXT("Opening another row collapses previous"),CastChecked<UOperationsSquadEntryWidget>(Room->SquadList->GetChildAt(0))->bExpanded);
            Second->HeaderButton->OnClicked.Broadcast();Test->TestFalse(TEXT("Re-click collapses row"),Room->ExpandedSquadId.IsValid());
            Room->NormalSpeedButton->OnClicked.Broadcast();
            for(double Expected:{240.,480.,960.,240.}){Room->FastForwardButton->OnClicked.Broadcast();Test->TestEqual(TEXT("Blueprint fast button cycles array"),Clock->GetTimeScale(),Expected);}
            Room->FastForwardMultipliers={2.,6.};Room->NormalSpeedButton->OnClicked.Broadcast();Room->FastForwardButton->OnClicked.Broadcast();Test->TestEqual(TEXT("Authored array is honored"),Clock->GetTimeScale(),120.);
            Room->FastForwardMultipliers={4.,8.,16.};
            SlateClick(Room->PauseButton);Next(105);return false;
        }
        case 105:
            if(Elapsed<.3)return false;
            Test->TestTrue(TEXT("Real Slate pause click freezes world"),UGameplayStatics::IsGamePaused(World));
            Test->TestTrue(TEXT("Real Slate pause click freezes calendar"),Clock->IsTimePaused());
            PausedWorldTime=World->GetTimeSeconds();BeforeAdvance=Clock->GetCurrentTime();Next(106);return false;
        case 106:
            if(Elapsed<.4)return false;
            Test->TestEqual(TEXT("World simulation time stays frozen"),double(World->GetTimeSeconds()),PausedWorldTime);
            Test->TestTrue(TEXT("Calendar stays frozen"),Clock->GetCurrentTime()==BeforeAdvance);
            SlateClick(Room->NormalSpeedButton);Next(107);return false;
        case 107:
        {
            if(Elapsed<.3)return false;
            Test->TestFalse(TEXT("Real Slate control remains clickable while world paused"),UGameplayStatics::IsGamePaused(World));
            Test->TestEqual(TEXT("Real resume uses 60x calendar"),Clock->GetTimeScale(),60.);
            auto* Squads=UPlayerSquadLibrary::GetPlayerSquadManager(Room.Get());FText Error;
            for(FGuid Id:FixtureSquads)Test->TestTrue(TEXT("Move a resident squad away"),Squads->SetSquadTileId(Id,TEXT("L7"),Error));
            Test->TestEqual(TEXT("Real-time movement removes old tile rows"),Room->SquadList->GetChildrenCount(),0);
            Test->TestFalse(TEXT("Entry becomes unavailable when last squad leaves"),Room->RequestEnterSelectedTile());
            for(FGuid Id:FixtureSquads)Squads->SetSquadTileId(Id,TEXT("T5"),Error);
            Test->TestEqual(TEXT("Return movement restores list through data events"),Room->SquadList->GetChildrenCount(),3);
            // Actual entry now streams a battle map; covered by SubMapEntry.RuntimeFlow.
            Test->TestTrue(TEXT("Occupied tile enables entry"),Room->GetSelectedTileSquadCount()>0);
            Next(104);return false;
        }
        case 104:
            Room->ClearSelectedTile();
            Clock->SetTimeScale(2.);
            Clock->SetTimePaused(true);
            Test->TestTrue(TEXT("External pause keeps resume control enabled"), Room->NormalSpeedButton->GetIsEnabled());
            BeforeAdvance = Clock->GetCurrentTime();
            Next(6); return false;
        case 6:
            if (Elapsed < .3) { return false; }
            Test->TestTrue(TEXT("Real ticker respects external calendar pause"), Clock->GetCurrentTime() == BeforeAdvance);
            Field<USelectionButtonWidget>(Room.Get(), TEXT("NormalSpeedButton"))->OnClicked.Broadcast();
            Test->TestEqual(TEXT("Normal button sets 60 calendar seconds per real second"), Clock->GetTimeScale(), 60.);
            Test->TestFalse(TEXT("Normal button resumes the calendar"), Clock->IsTimePaused());

            BeforeAdvance = Clock->GetCurrentTime();
            Next(2); return false;
        case 2:
            if (Elapsed < .3) { return false; }
            Test->TestTrue(TEXT("Normal game clock advances"), Clock->GetCurrentTime() > BeforeAdvance);
            Field<USelectionButtonWidget>(Room.Get(), TEXT("FastForwardButton"))->OnClicked.Broadcast();
            Test->TestEqual(TEXT("Fast button sets 4x"), Clock->GetTimeScale(), 240.);
            Test->TestEqual(TEXT("Calendar speed does not accelerate world simulation"), World->GetWorldSettings()->TimeDilation, 1.f);
            BeforeAdvance = Clock->GetCurrentTime();
            Next(3); return false;
        case 3:
            if (Elapsed < (bVisual ? 3. : .3)) { return false; }
            Test->TestTrue(TEXT("Fast-forward game clock advances"), Clock->GetCurrentTime() > BeforeAdvance);
            CheckPointerGeometry(false);
            Screenshot(TEXT("operations_unselected.png"));
            Next(4); return false;
        case 4:
        {
            if (Elapsed < .3) { return false; }
            AGSMTile3D* Tile = Map->GetTileById(TEXT("L7"));
            if (!Test->TestNotNull(TEXT("Sandbox L7 exists"), Tile)) { return true; }
            Test->TestTrue(TEXT("Real map click pipeline accepts tile"), Map->HandleTileClickRequest(Tile, Tile->GetActorLocation()));
            Test->TestTrue(TEXT("Selected tile data reaches room"), Room->HasSelectedTile() && Room->GetSelectedTile() == Tile->GetTileData());
            Test->TestTrue(TEXT("Selected detail area is visible"), Field<UPanelWidget>(Room.Get(), TEXT("TileInfoPanel"))->IsVisible());
            Test->TestEqual(TEXT("Empty tile has no resident squads"), Room->GetSelectedTileSquadCount(), 0);
            Test->TestFalse(TEXT("Empty tile cannot enter"), Room->EnterMapButton->GetIsEnabled());
            Test->TestFalse(TEXT("Programmatic entry also checks live squad count"), Room->RequestEnterSelectedTile());
            CheckTileBorder(TEXT("L7"), true);
            CheckTileBorder(TEXT("M7"), false);
            Next(5); return false;
        }
        case 5:
            if (Elapsed < (bVisual ? 2. : .3)) { return false; }
            CheckPointerGeometry(true);
            Screenshot(TEXT("operations_selected.png"));
            Next(60); return false;
        case 60:
            if (Elapsed < .3) return false;
            Map->SwitchSelectedTile(Map->GetTileById(TEXT("M7")));
            AppearanceReviewScale = Map->GetCurrentMapScale();
            Map->SetMapScaleAtWorldLocation(Map->GetTileById(TEXT("M7"))->GetActorLocation(), AppearanceReviewScale * 1.8f);
            CheckTileBorder(TEXT("L7"), false);
            CheckTileBorder(TEXT("M7"), true);
            Next(61); return false;
        case 61:
            if (Elapsed < (bVisual ? 2. : .3)) return false;
            Screenshot(TEXT("operations_selected_zoom.png"));
            Next(62); return false;
        case 62:
            if (Elapsed < .3) return false;
            Map->ClearSelectedTile();
            Map->SetMapScaleAtWorldLocation(Map->GetActorLocation(), AppearanceReviewScale);
            CheckTileBorder(TEXT("L7"), false);
            CheckTileBorder(TEXT("M7"), false);
            Next(63); return false;
        case 63:
            if (Elapsed < (bVisual ? 2. : .3)) return false;
            Screenshot(TEXT("operations_selection_cleared.png"));
            Next(bVisual ? 64 : 7); return false;
        case 64:
            if (Elapsed < .3) return false;
            Map->SwitchSelectedTile(Map->GetTileById(TEXT("B2")));
            CheckTileBorder(TEXT("B2"), true);
            Next(65); return false;
        case 65:
            if (Elapsed < 2.) return false;
            Screenshot(TEXT("operations_selected_mountain.png"));
            Next(66); return false;
        case 66:
            if (Elapsed < .3) return false;
            Map->SwitchSelectedTile(Map->GetTileById(TEXT("L14")));
            CheckTileBorder(TEXT("B2"), false);
            CheckTileBorder(TEXT("L14"), true);
            Next(67); return false;
        case 67:
            if (Elapsed < 2.) return false;
            Screenshot(TEXT("operations_selected_water.png"));
            Next(68); return false;
        case 68:
            if (Elapsed < .3) return false;
            Map->ClearSelectedTile();
            CheckTileBorder(TEXT("L14"), false);
            Next(7); return false;
        case 7:
        {
            if (Elapsed < .3) { return false; }
            if (Visit == 1) Room->PauseButton->OnClicked.Broadcast(); // Exit must work even while paused.
            PreviousRoom.Reset(Room.Get());
            auto* Back = Field<UBasicButtonWidget>(Shell(), TEXT("BackButton"));
            if (!Test->TestNotNull(TEXT("Actual shell return button"), Back)) { return true; }
            Back->OnClicked.Broadcast();
            Test->TestTrue(TEXT("Real return button initiates leaving operations room"), Scenes->GetTransitionState() == ESMS_SceneTransitionState::Leaving
                && Scenes->GetPendingSceneTag() == OverviewTag);
            Next(8); return false;
        }
        case 8:
            if (!IdleAt(OverviewTag)) { return false; }
            CheckReturnedPitchLimits();
            CheckCameraState(ExitOverheadCameraState,TEXT("Overview preserves operations exit position"));
            Test->TestFalse(TEXT("Return camera completes after releasing panel-owned world pause"), UGameplayStatics::IsGamePaused(World));
            Test->TestNull(TEXT("Returning removes room from live shell"), Shell()->GetCurrentSceneUI());
            Test->TestNull(TEXT("Unloaded room releases host"), PreviousRoom->BaseUI.Get());
            Test->TestFalse(TEXT("Map no longer retains old UI selection binding"), Map->OnSelectedTileChanged.GetAllObjects().Contains(PreviousRoom.Get()));
            Test->TestFalse(TEXT("Clock no longer retains old UI callback"), Clock->OnTimeChanged.GetAllObjects().Contains(PreviousRoom.Get()));
            Test->TestFalse(TEXT("Speed event no longer retains old UI"), Clock->OnTimeScaleChanged.GetAllObjects().Contains(PreviousRoom.Get()));
            Test->TestFalse(TEXT("Pause event no longer retains old UI"), Clock->OnTimePausedChanged.GetAllObjects().Contains(PreviousRoom.Get()));
            Test->TestEqual(TEXT("Leaving preserves calendar speed"), Clock->GetTimeScale(), 240.);
            Test->TestEqual(TEXT("Date and clock text bindings are removed"), TimeSubsystem->GetRegisteredTimeTextBlockCount(), InitialTextBindings);
            Test->TestFalse(TEXT("Detached UI releases its selected data"), PreviousRoom->HasSelectedTile());
            Field<USelectionButtonWidget>(PreviousRoom.Get(), TEXT("NormalSpeedButton"))->OnClicked.Broadcast();
            Test->TestEqual(TEXT("A detached Blueprint button cannot change calendar speed"), Clock->GetTimeScale(), 240.);
            if (Visit++ == 0)
            {
                Map->SwitchSelectedTile(Map->GetTileById(TEXT("M7")));
                Test->TestFalse(TEXT("Detached UI ignores subsequent selection events"), PreviousRoom->HasSelectedTile());
                PrepareEntryCamera();
                Next(9); return false;
            }
            RestoreCameraBaseline();
            Next(10); return false;
        case 10:
            if (Camera->IsMovingToCameraState()) { return false; }
            Camera->SetCameraMovementDisabled(bBaselineMovementDisabled);
            Camera->SetCameraRotationDisabled(bBaselineRotationDisabled);
            CheckCameraState(BaselineCameraState, TEXT("Test restores its original overview camera"));
            bCameraBaselineSaved = false;
            Test->AddInfo(TEXT("OPERATIONS_COMMAND_ROOM_FLOW_OK real NewGame, two-stage camera entry, runtime pitch-limit restoration, scene return/re-enter, time, selection, geometry, input guards"));
            return true;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOperationsCommandRoomFlowTest, "SilverChoir.BaseUI.OperationsCommandRoom.RuntimeFlow",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FOperationsCommandRoomFlowTest::RunTest(const FString& Parameters)
{
    for (const auto& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType != EWorldType::Game || !Context.World()) { continue; }
        auto* Menu = Cast<AMainMenuPlayerController>(Context.World()->GetFirstPlayerController());
        if (!Menu) { continue; }
        UGameInstance* Instance = Context.World()->GetGameInstance();
        Menu->HandleMenuAction(EMainMenuAction::NewGame);
        ADD_LATENT_AUTOMATION_COMMAND(OperationsRoomTests::FRoomFlow(this, Instance));
        return true;
    }
    AddError(TEXT("Run OperationsCommandRoom.RuntimeFlow alone from MainMenu -game; use -OperationsVisualReview for two screenshots."));
    return false;
}
#endif
