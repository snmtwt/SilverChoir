#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/BattlePanelInteractionTestObserver.h"
#include "Map/BattleMap/BattleModePanels.h"
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/BattleMap/BattleVehiclePanelWidget.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/BattleMap/BattleUnitSelectionComponent.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Object/Unit/UnitPawnBase.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "MTS_SubMapSubsystem.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitSubsystem.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Widget/SlotContainerByType/SIS_MirrorSlotContainer.h"
#include "Widget/SlotContainer/SIS_SlotContainer.h"
#include "Widgets/SWidget.h"

namespace BattleAuthoredVehiclePanelTest
{
class FRuntime final : public IAutomationLatentCommand
{
public:
    explicit FRuntime(FAutomationTestBase* InTest):Test(InTest) {}
    virtual ~FRuntime() override { Cleanup(); }

    virtual bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>30.)
        {
            Test->AddError(TEXT("Authored vehicle-panel test timed out waiting for the viewport or selected-vehicle screenshot"));
            return Finish();
        }
        if(Stage==0)
        {
            if(!GEngine||!GEngine->GameViewport||!FSlateApplication::IsInitialized())return false;
            APlayerController* Player=nullptr;
            for(const FWorldContext& Context:GEngine->GetWorldContexts())
                if(Context.WorldType==EWorldType::Game&&Context.World())Player=Context.World()->GetFirstPlayerController();
            if(!Player)return false;
            if(!CreatePreview(Player))return Finish();
            Advance();
            return false;
        }
        if(FPlatformTime::Seconds()<ReadyAt)return false;
        if(Stage==1)
        {
            if(!VerifyAuthoredIdentity(TEXT("Initial construction")))return Finish();
            Test->TestFalse(TEXT("Authored vehicle panel starts closed"),Authored->bPanelOpen);
            Test->TestTrue(TEXT("Authored vehicle panel starts collapsed"),Authored->GetVisibility()==ESlateVisibility::Collapsed);
            ClickVehicle(TEXT("First vehicle click uses the authored panel"));
            ClickVehicle(TEXT("Second vehicle click closes the same authored panel"),false);
            ClickVehicle(TEXT("Third vehicle click reopens the same authored panel"));
            Test->TestTrue(TEXT("Explicit ShowVehiclePanel keeps the panel open"),Mode->ShowVehiclePanel());
            Test->TestTrue(TEXT("Explicit opening preserves the authored instance"),Mode->VehiclePanel==Authored.Get()&&Authored->bPanelOpen);
            AddPersonnelFixture();
            Advance();
            return false;
        }
        if(Stage==2)
        {
            // Stage 1 has rendered before capturing the selected vehicle button.
            if(ScreenshotPath.IsEmpty())
            {
                const FString Directory=FPaths::ProjectSavedDir()/TEXT("Screenshots/BattleVehiclePresentation");
                IFileManager::Get().MakeDirectory(*Directory,true);
                ScreenshotPath=Directory/TEXT("AuthoredVehicleSelected.png");
                PreviousScreenshotTime=IFileManager::Get().GetTimeStamp(*ScreenshotPath);
                FScreenshotRequest::RequestScreenshot(ScreenshotPath,true,false);
                return false;
            }
            if(FScreenshotRequest::IsScreenshotRequested()
                ||IFileManager::Get().FileSize(*ScreenshotPath)<=0
                ||IFileManager::Get().GetTimeStamp(*ScreenshotPath)==PreviousScreenshotTime)return false;
            Test->AddInfo(FString::Printf(TEXT("Authored selected vehicle screenshot: %s"),*ScreenshotPath));
            if(!Test->TestNotNull(TEXT("Personnel fixture exists"),Card.Get()))return Finish();
            Test->TestTrue(TEXT("Personnel fixture was constructed by Slate"),Card->IsPresentationConstructed());
            Test->TestTrue(TEXT("Personnel panel opens through the member-mode base"),Mode->ShowPersonnelPanel(Card.Get()));
            Test->TestTrue(TEXT("Personnel becomes the exclusive active panel"),Mode->ActivePersonnelCard==Card.Get()&&Card->bControlPanelOpen);
            Test->TestFalse(TEXT("Opening personnel closes the authored vehicle panel"),Authored->bPanelOpen);
            ClickVehicle(TEXT("Vehicle click closes personnel and reuses the authored panel"));
            Test->TestTrue(TEXT("Vehicle opening clears the active personnel reference"),Mode->ActivePersonnelCard==nullptr);
            Test->TestFalse(TEXT("Vehicle opening dispatches personnel close"),Card->bControlPanelOpen);
            Unmount();
            Advance();
            return false;
        }
        if(Stage==3)
        {
            Test->TestFalse(TEXT("Slate teardown closes the authored vehicle panel"),Authored->bPanelOpen);
            Test->TestFalse(TEXT("An unmounted member-mode instance rejects opening"),Mode->ShowVehiclePanel());
            if(!VerifyAuthoredIdentity(TEXT("After teardown")))return Finish();
            Mount();
            Advance();
            return false;
        }
        if(Stage==4)
        {
            if(!VerifyAuthoredIdentity(TEXT("After reattachment")))return Finish();
            Test->TestFalse(TEXT("Reattached authored panel starts closed"),Authored->bPanelOpen);
            ClickVehicle(TEXT("Reattached vehicle button has exactly one native listener"));
            Test->TestTrue(TEXT("Reattachment restores personnel-card registration"),Mode->ShowPersonnelPanel(Card.Get()));
            Test->TestFalse(TEXT("Reattached personnel opening closes the same authored panel"),Authored->bPanelOpen);
            Mode->CloseActivePanel();

            // The class fallback remains supported when a runtime layout intentionally has no authored child.
            Authored->RemoveFromParent();
            if(!Mode->VehiclePanelContainer)
            {
                // This host belongs only to the test preview; the real Designer layout needs no legacy container.
                auto* Host=NewObject<UCanvasPanel>(Root.Get());
                Root->AddChildToCanvas(Host);
                Mode->VehiclePanelContainer=Host;
            }
            Mode->VehiclePanelClass=FallbackClass;
            Test->TestTrue(TEXT("Missing authored child creates the configured fallback"),Mode->ShowVehiclePanel());
            Dynamic.Reset(Mode->VehiclePanel.Get());
            Test->TestTrue(TEXT("Fallback is a distinct runtime-created instance"),Dynamic.IsValid()&&Dynamic.Get()!=Authored.Get());
            Test->TestEqual(TEXT("Fallback creates only one vehicle child"),VehicleChildCount(),1);
            Unmount();
            Advance();
            return false;
        }
        if(Stage==5)
        {
            Test->TestEqual(TEXT("Teardown removes only the runtime-created child"),VehicleChildCount(),0);
            if(Dynamic.IsValid())
            {
                Test->TestNull(TEXT("Runtime-created child is detached during owner teardown"),Dynamic->GetParent());
                Test->TestFalse(TEXT("Runtime-created panel is closed before removal"),Dynamic->bPanelOpen);
            }
            if(!Test->HasAnyErrors())
                Test->AddInfo(TEXT("BATTLE_AUTHORED_VEHICLE_PANEL_OK actual WBP_成员模式 keeps one authored panel in its original scroll hierarchy; click toggles, exclusive personnel switching and Slate reattachment reuse it; dynamic fallback cleanup remains valid"));
            return Finish();
        }
        return Finish();
    }

private:
    FAutomationTestBase* Test;
    int32 Stage=0, VehicleRequests=0;
    double Started=FPlatformTime::Seconds(), ReadyAt=0.;
    TStrongObjectPtr<UBattleMemberModeWidget> Mode;
    TStrongObjectPtr<UBattleVehiclePanelWidget> Authored, Dynamic;
    TWeakObjectPtr<UPanelWidget> AuthoredParent;
    TStrongObjectPtr<UBattlePanelInteractionTestCard> Card;
    TStrongObjectPtr<UCanvasPanel> Root;
    TSubclassOf<UBattleVehiclePanelWidget> FallbackClass;
    TSharedPtr<SWidget> Preview;
    FDelegateHandle RequestHandle;
    FString ScreenshotPath;
    FDateTime PreviousScreenshotTime;

    bool CreatePreview(APlayerController* Player)
    {
        UClass* Class=LoadClass<UBattleMemberModeWidget>(nullptr,TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_成员模式.WBP_成员模式_C"));
        if(!Test->TestNotNull(TEXT("Actual authored member-mode Blueprint is loadable"),Class))return false;
        Mode.Reset(CreateWidget<UBattleMemberModeWidget>(Player,Class));
        if(!Test->TestNotNull(TEXT("Actual member-mode instance is created"),Mode.Get())
            ||!Test->TestNotNull(TEXT("Actual member-mode tree exists"),Mode->WidgetTree.Get())
            ||!Test->TestNotNull(TEXT("Authored vehicle button is bound"),Mode->VehicleButton.Get())
            ||!Test->TestNotNull(TEXT("Authored member list is bound"),Mode->MemberList.Get())
            ||!Test->TestNotNull(TEXT("Authored member scroll box is bound"),Mode->MemberScrollBox.Get()))return false;
        Authored.Reset(Mode->VehiclePanel.Get());
        if(!Test->TestNotNull(TEXT("Required VehiclePanel binding is populated before construction"),Authored.Get()))return false;
        if(!Test->TestTrue(TEXT("Fixed VehiclePanel binding matches the named Designer instance"),
            Authored.Get()==Mode->WidgetTree->FindWidget(TEXT("VehiclePanel"))))return false;
        AuthoredParent=Authored->GetParent();
        if(!Test->TestNotNull(TEXT("Authored vehicle panel has an actual Designer parent"),AuthoredParent.Get()))return false;
        if(!Test->TestTrue(TEXT("Moved vehicle panel is a descendant of MemberScrollBox"),IsWithinMemberScrollBox()))return false;
        FallbackClass=Authored->GetClass();
        // The fixed Designer binding must work independently of the fallback setting.
        Mode->VehiclePanelClass=nullptr;
        FBattleVehicleView Vehicle;
        Vehicle.VehicleId=FGuid::NewGuid();
        Mode->SetSquadContext(FGuid::NewGuid(),Vehicle);
        RequestHandle=Mode->OnPanelOpening.AddLambda([this](FGuid UnitId){if(!UnitId.IsValid())++VehicleRequests;});
        Root.Reset(NewObject<UCanvasPanel>(Player));
        auto* Slot=Root->AddChildToCanvas(Mode.Get());
        Slot->SetAnchors(FAnchors(0,1,1,1));
        Slot->SetOffsets(FMargin(20,-170,20,160));
        Mount();
        return true;
    }

    static int32 CountVehicleWidgets(UWidget* Widget)
    {
        if(!IsValid(Widget))return 0;
        int32 Count=Widget->IsA<UBattleVehiclePanelWidget>()?1:0;
        if(auto* Panel=Cast<UPanelWidget>(Widget))
            for(UWidget* Child:Panel->GetAllChildren())Count+=CountVehicleWidgets(Child);
        if(auto* UserWidget=Cast<UUserWidget>(Widget);UserWidget&&UserWidget->WidgetTree)
            Count+=CountVehicleWidgets(UserWidget->WidgetTree->RootWidget);
        return Count;
    }

    int32 VehicleChildCount() const { return CountVehicleWidgets(Root.Get()); }

    bool IsWithinMemberScrollBox() const
    {
        for(UWidget* Widget=Authored.IsValid()?Authored->GetParent():nullptr;Widget;Widget=Widget->GetParent())
            if(Widget==Mode->MemberScrollBox)return true;
        return false;
    }

    bool VerifyAuthoredIdentity(const TCHAR* Phase)
    {
        bool bOK=Test->TestEqual(FString::Printf(TEXT("%s keeps one vehicle child"),Phase),VehicleChildCount(),1);
        bOK&=Test->TestTrue(FString::Printf(TEXT("%s keeps the original Designer parent"),Phase),AuthoredParent.IsValid()&&Authored->GetParent()==AuthoredParent.Get());
        bOK&=Test->TestTrue(FString::Printf(TEXT("%s stays within MemberScrollBox"),Phase),IsWithinMemberScrollBox());
        bOK&=Test->TestTrue(FString::Printf(TEXT("%s stores the same authored instance"),Phase),Mode->VehiclePanel==Authored.Get());
        return bOK;
    }

    void ClickVehicle(const TCHAR* Description,bool bExpectedOpen=true)
    {
        const int32 Before=VehicleRequests;
        Mode->VehicleButton->OnClicked.Broadcast();
        Test->TestEqual(TEXT("Only an opening click emits one opening request"),VehicleRequests,Before+(bExpectedOpen?1:0));
        Test->TestTrue(Description,Mode->VehiclePanel==Authored.Get()&&Authored->bPanelOpen==bExpectedOpen);
        Test->TestEqual(TEXT("A vehicle click does not add another panel"),VehicleChildCount(),1);
    }

    void AddPersonnelFixture()
    {
        Card.Reset(CreateWidget<UBattlePanelInteractionTestCard>(Mode->GetOwningPlayer()));
        if(!Card.IsValid())return;
        FBattleMemberView Member;
        Member.UnitId=FGuid::NewGuid();
        Card->SetMember(Member);
        Mode->MemberList->AddChild(Card.Get());
        Mode->RegisterPersonnelCard(Card.Get());
    }

    void Mount()
    {
        Preview=Root->TakeWidget();
        GEngine->GameViewport->AddViewportWidgetContent(Preview.ToSharedRef(),150);
    }

    void Unmount()
    {
        if(Preview.IsValid()&&GEngine&&GEngine->GameViewport)
            GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
        Preview.Reset();
        if(Root.IsValid())Root->ReleaseSlateResources(true);
    }

    void Advance(){++Stage;ReadyAt=FPlatformTime::Seconds()+.15;}
    bool Finish(){Cleanup();return true;}
    void Cleanup()
    {
        if(Mode.IsValid())
        {
            Mode->OnPanelOpening.Remove(RequestHandle);
            Mode->CloseActivePanel();
        }
        RequestHandle.Reset();
        Unmount();
        if(Root.IsValid())Root->ClearChildren();
        Card.Reset();Dynamic.Reset();Authored.Reset();Mode.Reset();Root.Reset();
    }
};

/** Real Blueprint animations, with independently specified authored endpoints (never a first-open baseline). */
class FAnimationOwnership final : public IAutomationLatentCommand
{
public:
    explicit FAnimationOwnership(FAutomationTestBase* InTest):Test(InTest) {}
    virtual ~FAnimationOwnership() override { Cleanup(); }
    virtual bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>45.)
        {
            Test->AddError(TEXT("Authored panel animation regression timed out"));
            return Finish();
        }
        if(Stage==0)
        {
            if(!GEngine||!GEngine->GameViewport||!FSlateApplication::IsInitialized())return false;
            for(const FWorldContext& Context:GEngine->GetWorldContexts())
                if(Context.WorldType==EWorldType::Game&&Context.World())
                    PC=Cast<AGameMainMapPlayerController>(Context.World()->GetFirstPlayerController());
            if(!PC.IsValid())return false;
            const auto* Maps=PC->GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>();
            if((Maps&&Maps->IsSubMapTransitionInProgress())||(PC->BattleWidget&&PC->BattleWidget->bTargeting))return false;
            if(!CreatePreview())return Finish();
            Wait(1,.35);
            return false;
        }
        SampleAnimationOwnership(); // Sample during the animation, including waits, not just after settlement.
        if(FPlatformTime::Seconds()<ReadyAt)return false;
        if(!ScreenshotPath.IsEmpty())
        {
            if(FScreenshotRequest::IsScreenshotRequested()||IFileManager::Get().FileSize(*ScreenshotPath)<=0
                ||IFileManager::Get().GetTimeStamp(*ScreenshotPath)==PreviousScreenshotTime)return false;
            Test->AddInfo(FString::Printf(TEXT("Authored panel layout screenshot: %s"),*ScreenshotPath));
            ScreenshotPath.Reset();
        }
        switch(Stage)
        {
        case 1:
            VerifySettled(TEXT("Initial closed presentation"),INDEX_NONE,false);
            CaptureScreenshot(TEXT("PanelLayoutInitial.png"),20);break;
        case 20:ClickCard(0);Wait(2,.08);break;
        case 2:
            RepeatCardWithoutRestart(0,TEXT("Repeated click during opening"));Wait(3,.7);break;
        case 3:
            VerifySettled(TEXT("First personnel opening"),0,false);
            CaptureScreenshot(TEXT("PanelLayoutPersonnelOpened.png"),21);break;
        case 21:RepeatCardWithoutRestart(0,TEXT("Repeated click on settled open card"));Wait(4,.08);break;
        case 4:
            VerifySettled(TEXT("Repeated open card remains full width"),0,false);
            ClickCard(1);Wait(5,.07);break;
        case 5:
            ClickCard(0);Wait(6,.07);break;
        case 6:
            ClickCard(1);Wait(7,1.);break;
        case 7:
            VerifySettled(TEXT("Rapid A/B/A/B switching"),1,false);
            ClickVehicle(true);Wait(8,1.);break;
        case 8:
            VerifySettled(TEXT("Personnel to vehicle"),INDEX_NONE,true);
            RecordEquipmentClipping();
            CaptureScreenshot(TEXT("PanelLayoutVehicleFirstOpened.png"),22);break;
        case 22:ClickVehicle(false);Wait(9,.07);break;
        case 9:
            ClickVehicle(true);Wait(10,.07);break;
        case 10:
            Test->TestTrue(TEXT("Explicit show during vehicle opening is idempotent"),Mode->ShowVehiclePanel());
            Test->TestTrue(TEXT("Second explicit show during vehicle opening is idempotent"),Mode->ShowVehiclePanel());
            SampleAnimationOwnership();Wait(11,1.);break;
        case 11:
            VerifySettled(TEXT("Vehicle rapid close/reopen and repeated show"),INDEX_NONE,true);
            CaptureScreenshot(TEXT("PanelLayoutVehicleReopened.png"),23);break;
        case 23:ClickCard(0);Wait(12,.07);break;
        case 12:
            ClickVehicle(true);Wait(13,1.);break;
        case 13:
            VerifySettled(TEXT("Vehicle/personnel/vehicle reversal"),INDEX_NONE,true);
            CaptureScreenshot(TEXT("PanelLayoutAfterReversal.png"),24);break;
        case 24:
            Test->TestTrue(TEXT("Regression sampled real animations in flight"),AnimatedSamples>0);
            if(!Test->HasAnyErrors())Test->AddInfo(FString::Printf(TEXT("BATTLE_AUTHORED_PANEL_ANIMATION_OK sampled=%d; real Blueprint personnel endpoints 0/250 and vehicle endpoints 0/230; closed offscreen cards release expansion width, vehicle wrapper reserves no blank gutter, repeated clicks and rapid reversals keep one animation owner"),AnimatedSamples));
            return Finish();
        default:return Finish();
        }
        return false;
    }

private:
    struct FPresentation
    {
        TWeakObjectPtr<UUserWidget> Widget;
        TWeakObjectPtr<USizeBox> WidthBox;
        TWeakObjectPtr<UWidgetAnimation> OpenAnimation,CloseAnimation;
        float OpenWidth=0.f;
    };
    FAutomationTestBase* Test;
    int32 Stage=0,AnimatedSamples=0;
    double Started=FPlatformTime::Seconds(),ReadyAt=0.;
    bool bManagerInstalled=false,bStateChanged=false;
    TWeakObjectPtr<AGameMainMapPlayerController> PC;
    TWeakObjectPtr<AGameMainMapGameState> State;
    TWeakObjectPtr<UPlayerUnitSubsystem> UnitSystem;
    FObjectPropertyBase* ManagerProperty=nullptr;
    TStrongObjectPtr<UPlayerUnitManagerBase> OriginalManager,Manager;
    TArray<TWeakObjectPtr<AUnitPawnBase>> OriginalSelectedPawns;
    TArray<FGuid> OriginalSelectedRecords;
    EGameMainMapType OriginalMapType=EGameMainMapType::None;
    FName OriginalMapId;
    TStrongObjectPtr<UBattleMapWidget> HUD;
    TStrongObjectPtr<UBattleMemberModeWidget> Mode;
    TStrongObjectPtr<UCanvasPanel> Root;
    TArray<TWeakObjectPtr<UBattlePersonnelCardWidget>> Cards;
    TArray<FPresentation> Presentations;
    TSet<int32> ReportedAnimationConflicts;
    TSharedPtr<SWidget> Preview;
    TWeakObjectPtr<USizeBox> MemberListSizeBox;
    float ClosedMemberListDesiredWidth=0.f;
    FString ScreenshotPath;
    FDateTime PreviousScreenshotTime;

    UBattleUnitSelectionComponent* Selection() const {return PC.IsValid()?PC->UnitSelection.Get():nullptr;}
    bool BindPresentation(UUserWidget* Widget,const TCHAR* BoxName,const TCHAR* OpenName,const TCHAR* CloseName,float OpenWidth)
    {
        FPresentation P;
        P.Widget=Widget;P.OpenWidth=OpenWidth;
        P.WidthBox=Widget&&Widget->WidgetTree?Cast<USizeBox>(Widget->WidgetTree->FindWidget(BoxName)):nullptr;
        // Resolve assets once for diagnostics only; production uses its fixed widget bindings.
        for(UClass* Class=Widget?Widget->GetClass():nullptr;Class;Class=Class->GetSuperClass())
            if(const auto* Generated=Cast<UWidgetBlueprintGeneratedClass>(Class))
                for(UWidgetAnimation* Animation:Generated->Animations)
                {
                    if(Animation&&Animation->GetName().StartsWith(OpenName))P.OpenAnimation=Animation;
                    if(Animation&&Animation->GetName().StartsWith(CloseName))P.CloseAnimation=Animation;
                }
        const FString Label=Widget?Widget->GetName():TEXT("MissingWidget");
        if(!Test->TestNotNull(Label+TEXT(" authored width box exists"),P.WidthBox.Get())
            ||!Test->TestNotNull(Label+TEXT(" authored open animation exists"),P.OpenAnimation.Get())
            ||!Test->TestNotNull(Label+TEXT(" authored close animation exists"),P.CloseAnimation.Get()))return false;
        Presentations.Add(P);
        return true;
    }
    bool CreatePreview()
    {
        State=PC->GetWorld()->GetGameState<AGameMainMapGameState>();
        UnitSystem=UPlayerUnitLibrary::GetPlayerUnitSubsystem(PC.Get());
        if(!Test->TestNotNull(TEXT("Animation fixture has game state"),State.Get())
            ||!Test->TestNotNull(TEXT("Animation fixture has unit subsystem"),UnitSystem.Get())
            ||!Test->TestNotNull(TEXT("Animation fixture has selection component"),Selection()))return false;
        ManagerProperty=FindFProperty<FObjectPropertyBase>(UnitSystem->GetClass(),TEXT("Manager"));
        if(!Test->TestNotNull(TEXT("Animation fixture can isolate unit records"),ManagerProperty))return false;
        OriginalManager.Reset(Cast<UPlayerUnitManagerBase>(ManagerProperty->GetObjectPropertyValue_InContainer(UnitSystem.Get())));
        if(OriginalManager.IsValid())for(FGuid Id:OriginalManager->GetUnitDataIDs())
            if(const auto Data=OriginalManager->GetUnitDataShared(Id);Data&&Data->IsSelected())OriginalSelectedRecords.Add(Id);
        for(AUnitPawnBase* Pawn:Selection()->GetSelectedUnits())OriginalSelectedPawns.Add(Pawn);
        OriginalMapType=State->GetCurrentMapType();OriginalMapId=State->ActiveMapID;bStateChanged=true;
        Selection()->CancelBoxSelection();Selection()->ClearUnitSelection();
        State->SetCurrentMapType(EGameMainMapType::Battle);State->ActiveMapID=NAME_None;
        Manager.Reset(NewObject<UPlayerUnitManagerBase>(UnitSystem.Get()));
        ManagerProperty->SetObjectPropertyValue_InContainer(UnitSystem.Get(),Manager.Get());bManagerInstalled=true;
        UClass* HUDClass=LoadClass<UBattleMapWidget>(nullptr,TEXT("/Game/System/Map/BattleMap/UI/WBP_BattleHUD.WBP_BattleHUD_C"));
        UClass* CardClass=LoadClass<UBattlePersonnelCardWidget>(nullptr,TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_人员卡片.WBP_人员卡片_C"));
        if(!Test->TestTrue(TEXT("Animation regression loads real HUD and personnel Blueprints"),HUDClass&&CardClass))return false;
        HUD.Reset(CreateWidget<UBattleMapWidget>(PC.Get(),HUDClass));
        if(!Test->TestNotNull(TEXT("Real animation HUD exists"),HUD.Get()))return false;
        Mode.Reset(HUD->MemberModePanel.Get());
        if(!Test->TestTrue(TEXT("Real animation HUD fixed mode bindings exist"),Mode.IsValid()&&Mode->MemberList&&Mode->MemberScrollBox&&Mode->VehiclePanel&&Mode->VehicleButton))return false;
        MemberListSizeBox=Mode->WidgetTree?Cast<USizeBox>(Mode->WidgetTree->FindWidget(TEXT("SizeBox_2"))):nullptr;
        HUD->MemberCardClass=CardClass;
        Root.Reset(NewObject<UCanvasPanel>(PC.Get()));
        auto* Slot=Root->AddChildToCanvas(HUD.Get());
        Slot->SetAnchors(FAnchors(0,0,0,1));Slot->SetOffsets(FMargin(20,0,1600,0));
        Preview=Root->TakeWidget();GEngine->GameViewport->AddViewportWidgetContent(Preview.ToSharedRef(),150);
        HUD->SetControlMode(EBattleControlMode::Member,false);
        FBattleSquadView Squad;Squad.SquadId=FGuid::NewGuid();Squad.Vehicle.VehicleId=FGuid::NewGuid();
        Squad.Name=FText::FromString(TEXT("Animation regression squad"));
        TArray<FUnitData> Records;
        for(int32 Index=0;Index<6;++Index)
        {
            FUnitData& Record=Records.AddDefaulted_GetRef();Record.UnitId=FGuid::NewGuid();
            Record.Profile.CodeName=FText::FromString(FString::Printf(TEXT("Animation unit %d"),Index));
            FBattleMemberView& Member=Squad.Members.AddDefaulted_GetRef();Member.UnitId=Record.UnitId;Member.Profile=Record.Profile;
        }
        FText Error;
        if(!Test->TestTrue(TEXT("Animation fixture loads canonical unit IDs"),Manager->LoadUnitData(Records,Error)))return false;
        HUD->bRosterInitialized=true;
        if(!Test->TestTrue(TEXT("Actual HUD builds the six Blueprint cards"),HUD->SetBattleSquads({Squad})))return false;
        if(!Test->TestEqual(TEXT("Animation fixture has six real personnel cards"),HUD->MemberCards.Num(),6))return false;
        for(UBattleMemberCardWidget* Entry:HUD->MemberCards)
        {
            auto* Card=Cast<UBattlePersonnelCardWidget>(Entry);
            if(!Test->TestTrue(TEXT("Roster card is the actual animated Blueprint"),Card&&Card->GetClass()==CardClass))return false;
            Cards.Add(Card);
            if(!BindPresentation(Card,TEXT("SizeBox_200"),TEXT("打开控制面板"),TEXT("关闭控制面板"),250.f))return false;
        }
        return BindPresentation(Mode->VehiclePanel,TEXT("SizeBox_32"),TEXT("打开面板"),TEXT("关闭面板"),230.f);
    }
    void SampleAnimationOwnership()
    {
        for(int32 Index=0;Index<Presentations.Num();++Index)
        {
            const FPresentation& P=Presentations[Index];
            if(!P.Widget.IsValid())continue;
            const bool bOpen=P.Widget->IsAnimationPlaying(P.OpenAnimation.Get());
            const bool bClose=P.Widget->IsAnimationPlaying(P.CloseAnimation.Get());
            if(bOpen||bClose)++AnimatedSamples;
            if(bOpen&&bClose&&!ReportedAnimationConflicts.Contains(Index))
            {
                ReportedAnimationConflicts.Add(Index);
                Test->AddError(FString::Printf(TEXT("Stage %d %s simultaneously plays its authored opening and closing animations; width=%.2f openTime=%.3f closeTime=%.3f"),
                    Stage,*P.Widget->GetName(),P.WidthBox->GetWidthOverride(),P.Widget->GetAnimationCurrentTime(P.OpenAnimation.Get()),P.Widget->GetAnimationCurrentTime(P.CloseAnimation.Get())));
            }
        }
    }
    void ClickCard(int32 Index)
    {
        if(!Cards.IsValidIndex(Index)||!Cards[Index].IsValid()){Test->AddError(TEXT("Animation fixture personnel card was lost"));return;}
        UBattlePersonnelCardWidget* Card=Cards[Index].Get();
        Card->OnClicked.Broadcast();
        Test->TestTrue(TEXT("Real personnel click opens the requested card"),Mode->ActivePersonnelCard==Card&&Card->bControlPanelOpen);
        SampleAnimationOwnership();
    }
    void RepeatCardWithoutRestart(int32 Index,const TCHAR* Scenario)
    {
        const FPresentation& P=Presentations[Index];
        const bool bWasPlaying=P.Widget->IsAnimationPlaying(P.OpenAnimation.Get());
        const float Before=P.Widget->GetAnimationCurrentTime(P.OpenAnimation.Get());
        ClickCard(Index);
        Test->TestEqual(FString(Scenario)+TEXT(" preserves playback state"),P.Widget->IsAnimationPlaying(P.OpenAnimation.Get()),bWasPlaying);
        Test->TestTrue(FString(Scenario)+TEXT(" preserves animation time"),FMath::IsNearlyEqual(P.Widget->GetAnimationCurrentTime(P.OpenAnimation.Get()),Before,.001f));
        Test->TestFalse(FString(Scenario)+TEXT(" does not start closing"),P.Widget->IsAnimationPlaying(P.CloseAnimation.Get()));
    }
    void ClickVehicle(bool bExpectedOpen)
    {
        Mode->VehicleButton->OnClicked.Broadcast();
        Test->TestEqual(TEXT("Vehicle toggle commits requested logical state"),Mode->VehiclePanel->bPanelOpen,bExpectedOpen);
        SampleAnimationOwnership();
    }
    void VerifySettled(const TCHAR* Scenario,int32 OpenCard,bool bVehicleOpen)
    {
        for(int32 Index=0;Index<Presentations.Num();++Index)
        {
            const FPresentation& P=Presentations[Index];
            const bool bExpectedOpen=Index<Cards.Num()?Index==OpenCard:bVehicleOpen;
            const float ExpectedWidth=bExpectedOpen?P.OpenWidth:0.f;
            const FString Label=FString::Printf(TEXT("%s / %s"),Scenario,*P.Widget->GetName());
            // Closed personnel cards remain in horizontal layout even when Slate stops painting them.
            // The vehicle collapses its whole UserWidget, so a stale hidden internal width reserves no space.
            if(Index<Cards.Num()||bExpectedOpen)
            {
                Test->TestTrue(Label+TEXT(" reaches the authored width endpoint, including closed offscreen personnel"),FMath::IsNearlyEqual(P.WidthBox->GetWidthOverride(),ExpectedWidth,.05f));
                Test->TestFalse(Label+TEXT(" has no opening animation left"),P.Widget->IsAnimationPlaying(P.OpenAnimation.Get()));
                Test->TestFalse(Label+TEXT(" has no closing animation left"),P.Widget->IsAnimationPlaying(P.CloseAnimation.Get()));
            }
            else Test->TestTrue(Label+TEXT(" closed vehicle is collapsed and reserves no layout space"),P.Widget->GetVisibility()==ESlateVisibility::Collapsed);
            Test->AddInfo(FString::Printf(TEXT("AUTHORED_PANEL_WIDTH %s width=%.3f expected=%.3f boxCached=%s boxDesired=%s cardCached=%s cardDesired=%s cardLeft=%.2f cardRight=%.2f padding=(%.2f,%.2f,%.2f,%.2f) openPlaying=%d closePlaying=%d visibility=%d"),
                *Label,P.WidthBox->GetWidthOverride(),ExpectedWidth,*P.WidthBox->GetCachedGeometry().GetLocalSize().ToString(),*P.WidthBox->GetDesiredSize().ToString(),
                *P.Widget->GetCachedGeometry().GetLocalSize().ToString(),*P.Widget->GetDesiredSize().ToString(),
                P.Widget->GetCachedGeometry().LocalToAbsolute(FVector2D::ZeroVector).X,P.Widget->GetCachedGeometry().LocalToAbsolute(FVector2D(P.Widget->GetCachedGeometry().GetLocalSize().X,0)).X,
                P.Widget->GetPadding().Left,P.Widget->GetPadding().Top,P.Widget->GetPadding().Right,P.Widget->GetPadding().Bottom,
                P.Widget->IsAnimationPlaying(P.OpenAnimation.Get()),P.Widget->IsAnimationPlaying(P.CloseAnimation.Get()),int32(P.Widget->GetVisibility())));
            if(Index<Cards.Num())Test->TestEqual(Label+TEXT(" has the correct logical open state"),Cards[Index]->bControlPanelOpen,bExpectedOpen);
        }
        Test->TestTrue(FString(Scenario)+TEXT(" parent tracks the correct personnel"),Mode->ActivePersonnelCard==(Cards.IsValidIndex(OpenCard)?Cards[OpenCard].Get():nullptr));
        Test->TestEqual(FString(Scenario)+TEXT(" parent tracks vehicle state"),Mode->VehiclePanel->bPanelOpen,bVehicleOpen);
        if(Stage==1)ClosedMemberListDesiredWidth=Mode->MemberList->GetDesiredSize().X;
        const float ExpectedMemberListWidth=ClosedMemberListDesiredWidth+(OpenCard!=INDEX_NONE?250.f:0.f);
        Test->TestTrue(FString(Scenario)+TEXT(" member list reserves only the active personnel expansion"),
            FMath::IsNearlyEqual(static_cast<float>(Mode->MemberList->GetDesiredSize().X),ExpectedMemberListWidth,1.f));
        Test->AddInfo(FString::Printf(TEXT("AUTHORED_MEMBER_LIST_LAYOUT %s cached=%s desired=%s closedDesiredWidth=%.2f expectedDesiredWidth=%.2f scrollCached=%s scrollDesired=%s offset=%.2f end=%.2f"),
            Scenario,*Mode->MemberList->GetCachedGeometry().GetLocalSize().ToString(),*Mode->MemberList->GetDesiredSize().ToString(),ClosedMemberListDesiredWidth,ExpectedMemberListWidth,
            *Mode->MemberScrollBox->GetCachedGeometry().GetLocalSize().ToString(),*Mode->MemberScrollBox->GetDesiredSize().ToString(),Mode->MemberScrollBox->GetScrollOffset(),Mode->MemberScrollBox->GetScrollOffsetOfEnd()));
        if(MemberListSizeBox.IsValid())
            Test->AddInfo(FString::Printf(TEXT("AUTHORED_MEMBER_LIST_SIZEBOX %s name=%s cached=%s desired=%s widthOverride=%.2f hasOverride=%d"),
                Scenario,*MemberListSizeBox->GetName(),*MemberListSizeBox->GetCachedGeometry().GetLocalSize().ToString(),*MemberListSizeBox->GetDesiredSize().ToString(),MemberListSizeBox->GetWidthOverride(),MemberListSizeBox->IsWidthOverride()));
        if(bVehicleOpen)
        {
            const auto* Panel=Mode->VehiclePanel.Get();
            const auto* Box=Presentations.Last().WidthBox.Get();
            const FGeometry& PG=Panel->GetCachedGeometry();
            const FGeometry& BG=Box->GetCachedGeometry();
            const FGeometry& SG=Mode->MemberScrollBox->GetCachedGeometry();
            const double ContentLeft=BG.LocalToAbsolute(FVector2D::ZeroVector).X;
            const double ContentRight=BG.LocalToAbsolute(FVector2D(BG.GetLocalSize().X,0)).X;
            const double ScrollLeft=SG.LocalToAbsolute(FVector2D::ZeroVector).X;
            const double ScrollRight=SG.LocalToAbsolute(FVector2D(SG.GetLocalSize().X,0)).X;
            const double VisibleContent=FMath::Max(0.,FMath::Min(ContentRight,ScrollRight)-FMath::Max(ContentLeft,ScrollLeft));
            Test->AddInfo(FString::Printf(TEXT("AUTHORED_VEHICLE_WRAPPER_LAYOUT %s wrapperCached=%s wrapperDesired=%s wrapperLeft=%.2f contentCached=%s contentDesired=%s contentLeft=%.2f right=%.2f visibleContent=%.2f padding=(%.2f,%.2f,%.2f,%.2f)"),
                Scenario,*PG.GetLocalSize().ToString(),*Panel->GetDesiredSize().ToString(),PG.LocalToAbsolute(FVector2D::ZeroVector).X,
                *BG.GetLocalSize().ToString(),*Box->GetDesiredSize().ToString(),ContentLeft,ContentRight,VisibleContent,
                Panel->GetPadding().Left,Panel->GetPadding().Top,Panel->GetPadding().Right,Panel->GetPadding().Bottom));
            Test->TestTrue(FString(Scenario)+TEXT(" vehicle wrapper has no extra horizontal blank gutter"),FMath::IsNearlyEqual(static_cast<float>(Panel->GetDesiredSize().X),static_cast<float>(Box->GetDesiredSize().X),1.f));
            Test->TestTrue(FString(Scenario)+TEXT(" actual vehicle content is fully inside the scroll viewport"),VisibleContent>=FMath::Min(ContentRight-ContentLeft,ScrollRight-ScrollLeft)-2.);
        }
    }
    void CaptureScreenshot(const TCHAR* Filename,int32 NextStage)
    {
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("Screenshots/BattleVehiclePresentation");
        IFileManager::Get().MakeDirectory(*Directory,true);
        ScreenshotPath=Directory/Filename;
        PreviousScreenshotTime=IFileManager::Get().GetTimeStamp(*ScreenshotPath);
        FScreenshotRequest::RequestScreenshot(ScreenshotPath,true,false);
        Wait(NextStage,0.);
    }
    void RecordEquipmentClipping()
    {
        auto* Scroll=Mode->MemberScrollBox.Get();
        const TSharedPtr<SWidget> ScrollSlate=Scroll->GetCachedWidget();
        Test->TestTrue(TEXT("Member scroll viewport has a hard UMG clip boundary"),Scroll->GetClipping()==EWidgetClipping::ClipToBoundsAlways);
        Test->TestTrue(TEXT("Member scroll viewport has a hard live Slate clip boundary"),ScrollSlate.IsValid()&&ScrollSlate->GetClipping()==EWidgetClipping::ClipToBoundsAlways);
        int32 LabelCount=0;
        for(const auto& Card:Cards)
        {
            if(!Card.IsValid()||!Card->HandEquipment)continue;
            for(auto* Container:Card->HandEquipment->SlotContainerList)
            {
                UTextBlock* Label=Container?Container->ContainerName:nullptr;
                if(!Label)continue;
                ++LabelCount;
                const TSharedPtr<SWidget> LabelSlate=Label->GetCachedWidget();
                FString UMGChain,SlateChain;
                for(UWidget* Parent=Label;Parent;Parent=Parent->GetParent())
                {
                    if(!UMGChain.IsEmpty())UMGChain+=TEXT(" > ");
                    UMGChain+=FString::Printf(TEXT("%s[%d]"),*Parent->GetName(),int32(Parent->GetClipping()));
                }
                int32 Depth=0;
                for(TSharedPtr<SWidget> Parent=LabelSlate;Parent.IsValid()&&Depth++<48;Parent=Parent->GetParentWidget())
                {
                    if(!SlateChain.IsEmpty())SlateChain+=TEXT(" > ");
                    SlateChain+=FString::Printf(TEXT("%s[%d]"),*Parent->GetTypeAsString(),int32(Parent->GetClipping()));
                    if(Parent==ScrollSlate)break;
                }
                Test->AddInfo(FString::Printf(TEXT("AUTHORED_EQUIPMENT_LABEL_CLIP card=%s container=%s text=%s umg=%d slate=%d scrollUMG=%d scrollSlate=%d umgParents={%s} slateParents={%s}"),
                    *Card->GetName(),*Container->GetName(),*Label->GetText().ToString(),int32(Label->GetClipping()),LabelSlate.IsValid()?int32(LabelSlate->GetClipping()):-1,
                    int32(Scroll->GetClipping()),ScrollSlate.IsValid()?int32(ScrollSlate->GetClipping()):-1,*UMGChain,*SlateChain));
            }
        }
        Test->TestTrue(TEXT("Clipping regression inspects real generated equipment labels"),LabelCount>0);
    }
    void Wait(int32 Next,double Delay){Stage=Next;ReadyAt=FPlatformTime::Seconds()+Delay;}
    bool Finish(){Cleanup();return true;}
    void Cleanup()
    {
        if(Mode.IsValid())Mode->CloseActivePanel();
        if(Preview.IsValid()&&GEngine&&GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
        Preview.Reset();
        if(Root.IsValid()){Root->ReleaseSlateResources(true);Root->ClearChildren();}
        Presentations.Reset();Cards.Reset();Mode.Reset();HUD.Reset();Root.Reset();
        if(bStateChanged&&Selection())Selection()->ClearUnitSelection();
        if(bManagerInstalled&&UnitSystem.IsValid())ManagerProperty->SetObjectPropertyValue_InContainer(UnitSystem.Get(),OriginalManager.Get());
        bManagerInstalled=false;
        if(bStateChanged&&State.IsValid()){State->ActiveMapID=OriginalMapId;State->SetCurrentMapType(OriginalMapType);}
        if(bStateChanged)
        {
            for(const auto& Pawn:OriginalSelectedPawns)if(Pawn.IsValid())Pawn->SetUnitSelected(true);
            if(OriginalManager.IsValid())for(FGuid Id:OriginalSelectedRecords)
                if(const auto Data=OriginalManager->GetUnitDataShared(Id))Data->SetSelected(true);
        }
        bStateChanged=false;OriginalSelectedPawns.Reset();OriginalSelectedRecords.Reset();Manager.Reset();OriginalManager.Reset();
    }
};

class FReopenGeometry final : public IAutomationLatentCommand
{
public:
    explicit FReopenGeometry(FAutomationTestBase* InTest):Test(InTest) {}
    virtual ~FReopenGeometry() override { Cleanup(); }
    virtual bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>45.)
        {
            Test->AddError(TEXT("Vehicle reopen geometry timed out waiting for viewport, animation or screenshot"));
            return Finish();
        }
        if(Stage==0)
        {
            if(!GEngine||!GEngine->GameViewport||!FSlateApplication::IsInitialized())return false;
            APlayerController* Player=nullptr;
            for(const FWorldContext& Context:GEngine->GetWorldContexts())
                if(Context.WorldType==EWorldType::Game&&Context.World())Player=Context.World()->GetFirstPlayerController();
            if(!Player)return false;
            if(!CreatePreview(Player))return Finish();
            Wait(1,.25);
            return false;
        }
        if(FPlatformTime::Seconds()<ReadyAt)return false;
        if(Stage==1)
        {
            Click(true);
            Wait(2,1.);
        }
        else if(Stage==2)
        {
            if(Round==0&&!bCheckedOverflow)
            {
                bCheckedOverflow=true;
                if(Mode->MemberScrollBox->GetScrollOffsetOfEnd()<=1.f)
                {
                    Test->AddInfo(FString::Printf(TEXT("VEHICLE_REOPEN_NARROW fullHudModeWidth=%.2f scrollWidth=%.2f end=%.2f; constrain HUD to 1600px and reopen after settled close"),
                        Mode->GetCachedGeometry().GetLocalSize().X,Mode->MemberScrollBox->GetCachedGeometry().GetLocalSize().X,Mode->MemberScrollBox->GetScrollOffsetOfEnd()));
                    Click(false);
                    HUDSlot->SetAnchors(FAnchors(0,0,0,1));
                    HUDSlot->SetOffsets(FMargin(20,0,1600,0));
                    Wait(1,1.);
                    return false;
                }
            }
            RecordGeometry(true);
            if(Round<2)
            {
                const FString Directory=FPaths::ProjectSavedDir()/TEXT("Screenshots/BattleVehiclePresentation");
                IFileManager::Get().MakeDirectory(*Directory,true);
                ScreenshotPath=Directory/(Round==0?TEXT("VehicleFirstOpened.png"):TEXT("VehicleReopened.png"));
                PreviousScreenshotTime=IFileManager::Get().GetTimeStamp(*ScreenshotPath);
                FScreenshotRequest::RequestScreenshot(ScreenshotPath,true,false);
                Wait(3,0.);
            }
            else return NextRound();
        }
        else if(Stage==3)
        {
            if(FScreenshotRequest::IsScreenshotRequested()||IFileManager::Get().FileSize(*ScreenshotPath)<=0
                ||IFileManager::Get().GetTimeStamp(*ScreenshotPath)==PreviousScreenshotTime)return false;
            Test->AddInfo(FString::Printf(TEXT("Vehicle reopen screenshot: %s"),*ScreenshotPath));
            return NextRound();
        }
        else if(Stage==4)
        {
            RecordGeometry(false);
            Click(true);
            Wait(2,1.);
        }
        return false;
    }

private:
    FAutomationTestBase* Test;
    int32 Stage=0,Round=0;
    bool bCheckedOverflow=false;
    double Started=FPlatformTime::Seconds(),ReadyAt=0.;
    TStrongObjectPtr<UBattleMapWidget> HUD;
    TStrongObjectPtr<UBattleMemberModeWidget> Mode;
    TStrongObjectPtr<UCanvasPanel> Root;
    UCanvasPanelSlot* HUDSlot=nullptr;
    TArray<TWeakObjectPtr<USizeBox>> SizeBoxes;
    TArray<FVector3d> FirstBoxWidths;
    FVector2D FirstPanelWidths=FVector2D::ZeroVector;
    TSharedPtr<SWidget> Preview;
    FString ScreenshotPath;
    FDateTime PreviousScreenshotTime;

    bool CreatePreview(APlayerController* Player)
    {
        UClass* Class=LoadClass<UBattleMapWidget>(nullptr,TEXT("/Game/System/Map/BattleMap/UI/WBP_BattleHUD.WBP_BattleHUD_C"));
        if(!Test->TestNotNull(TEXT("Geometry test loads the actual battle HUD Blueprint"),Class))return false;
        HUD.Reset(CreateWidget<UBattleMapWidget>(Player,Class));
        if(!Test->TestNotNull(TEXT("Geometry test creates the actual battle HUD"),HUD.Get()))return false;
        Mode.Reset(HUD->MemberModePanel.Get());
        if(!Mode.IsValid()||!Test->TestNotNull(TEXT("Geometry test uses the bound vehicle panel"),Mode->VehiclePanel.Get())
            ||!Test->TestNotNull(TEXT("Geometry test has the authored vehicle button"),Mode->VehicleButton.Get())
            ||!Test->TestNotNull(TEXT("Geometry test has the authored scroll box"),Mode->MemberScrollBox.Get())
            ||!Test->TestNotNull(TEXT("Geometry test preserves the authored member list"),Mode->MemberList.Get()))return false;
        Test->TestEqual(TEXT("Geometry fixture preserves the six authored personnel cards"),Mode->MemberList->GetChildrenCount(),6);
        if(Mode->VehiclePanel->WidgetTree)
            Mode->VehiclePanel->WidgetTree->ForEachWidget([this](UWidget* Widget)
            {
                if(auto* Box=Cast<USizeBox>(Widget))SizeBoxes.Add(Box);
            });
        if(!Test->TestTrue(TEXT("Vehicle geometry has an authored SizeBox to inspect"),!SizeBoxes.IsEmpty()))return false;
        Root.Reset(NewObject<UCanvasPanel>(Player));
        HUDSlot=Root->AddChildToCanvas(HUD.Get());
        HUDSlot->SetAnchors(FAnchors(0,0,1,1));
        HUDSlot->SetOffsets(FMargin(0));
        Preview=Root->TakeWidget();
        GEngine->GameViewport->AddViewportWidgetContent(Preview.ToSharedRef(),150);
        HUD->SetControlMode(EBattleControlMode::Member,false);
        // HUD construction refreshes roster context. Supply the fixture context only after it is mounted.
        FBattleVehicleView Vehicle;
        Vehicle.VehicleId=FGuid::NewGuid();
        Mode->SetSquadContext(FGuid::NewGuid(),Vehicle);
        Test->TestEqual(TEXT("HUD mounting retains the six authored personnel cards"),Mode->MemberList->GetChildrenCount(),6);
        const FIntPoint ViewSize=GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetSizeXY():FIntPoint::ZeroValue;
        Test->AddInfo(FString::Printf(TEXT("VEHICLE_REOPEN_VIEWPORT actual=%dx%d target=1920x1080 fixture=actualBattleHUD authoredMembers=6"),ViewSize.X,ViewSize.Y));
        return true;
    }

    void RecordGeometry(bool bOpened)
    {
        auto* Panel=Mode->VehiclePanel.Get();
        auto* Scroll=Mode->MemberScrollBox.Get();
        const FGeometry& PG=Panel->GetCachedGeometry();
        const FGeometry& SG=Scroll->GetCachedGeometry();
        const double PanelLeft=PG.LocalToAbsolute(FVector2D::ZeroVector).X;
        const double PanelRight=PG.LocalToAbsolute(FVector2D(PG.GetLocalSize().X,0)).X;
        const double ScrollLeft=SG.LocalToAbsolute(FVector2D::ZeroVector).X;
        const double ScrollRight=SG.LocalToAbsolute(FVector2D(SG.GetLocalSize().X,0)).X;
        const double VisibleWidth=FMath::Max(0.,FMath::Min(PanelRight,ScrollRight)-FMath::Max(PanelLeft,ScrollLeft));
        const FVector2D PanelWidths(PG.GetLocalSize().X,Panel->GetDesiredSize().X);
        Test->AddInfo(FString::Printf(TEXT("VEHICLE_REOPEN_GEOMETRY round=%d state=%s panelCached=%s panelDesired=%s panelLeft=%.2f right=%.2f scrollCached=%s scrollLeft=%.2f right=%.2f visibleWidth=%.2f offset=%.2f end=%.2f viewFraction=%.4f viewOffsetFraction=%.4f panelAnimation=%d modeAnimation=%d visibility=%d open=%d"),
            Round,bOpened?TEXT("open"):TEXT("closed"),*PG.GetLocalSize().ToString(),*Panel->GetDesiredSize().ToString(),PanelLeft,PanelRight,
            *SG.GetLocalSize().ToString(),ScrollLeft,ScrollRight,VisibleWidth,Scroll->GetScrollOffset(),Scroll->GetScrollOffsetOfEnd(),
            Scroll->GetViewFraction(),Scroll->GetViewOffsetFraction(),Panel->IsAnyAnimationPlaying(),Mode->IsAnyAnimationPlaying(),int32(Panel->GetVisibility()),Panel->bPanelOpen));
        for(int32 Index=0;Index<SizeBoxes.Num();++Index)
        {
            USizeBox* Box=SizeBoxes[Index].Get();
            if(!Test->TestNotNull(TEXT("Authored SizeBox survives repeated opening"),Box))continue;
            const FVector3d Widths(Box->GetCachedGeometry().GetLocalSize().X,Box->GetDesiredSize().X,Box->GetWidthOverride());
            Test->AddInfo(FString::Printf(TEXT("VEHICLE_REOPEN_SIZEBOX round=%d state=%s name=%s cached=%s desired=%s widthOverride=%.2f hasOverride=%d visibility=%d"),
                Round,bOpened?TEXT("open"):TEXT("closed"),*Box->GetName(),*Box->GetCachedGeometry().GetLocalSize().ToString(),
                *Box->GetDesiredSize().ToString(),Box->GetWidthOverride(),Box->IsWidthOverride(),int32(Box->GetVisibility())));
            if(bOpened&&Box->GetFName()==TEXT("SizeBox_32"))
                Test->TestTrue(TEXT("Vehicle opening reaches the authored 230px endpoint, including the first opening"),FMath::IsNearlyEqual(Box->GetWidthOverride(),230.f,.05f));
            if(bOpened&&Round==0)FirstBoxWidths.Add(Widths);
            else if(bOpened&&FirstBoxWidths.IsValidIndex(Index))
                Test->TestTrue(FString::Printf(TEXT("Round %d %s cached/desired/override widths match first opening"),Round,*Box->GetName()),Widths.Equals(FirstBoxWidths[Index],1.));
        }
        Test->TestEqual(TEXT("Vehicle logical state matches the completed click"),Panel->bPanelOpen,bOpened);
        if(!bOpened)return;
        Test->TestTrue(TEXT("Vehicle reopen fixture exercises actual horizontal overflow"),Scroll->GetScrollOffsetOfEnd()>1.f);
        if(Round==0)FirstPanelWidths=PanelWidths;
        else Test->TestTrue(FString::Printf(TEXT("Round %d panel cached and desired widths match first opening"),Round),PanelWidths.Equals(FirstPanelWidths,1.));
        Test->TestTrue(TEXT("Opened vehicle has positive geometry and visible state"),PanelWidths.X>1.&&Panel->IsVisible());
        Test->TestFalse(TEXT("Vehicle opening animation completes within one second"),Panel->IsAnyAnimationPlaying());
        Test->TestTrue(FString::Printf(TEXT("Round %d vehicle fills its available scroll viewport without partial clipping"),Round),
            VisibleWidth>=FMath::Min(PanelRight-PanelLeft,ScrollRight-ScrollLeft)-2.);
    }

    void Click(bool bOpen)
    {
        Mode->VehicleButton->OnClicked.Broadcast();
        Test->TestEqual(TEXT("Vehicle button toggles the same bound panel"),Mode->VehiclePanel->bPanelOpen,bOpen);
    }
    bool NextRound()
    {
        if(Round==4)
        {
            if(!Test->HasAnyErrors())Test->AddInfo(TEXT("BATTLE_VEHICLE_REOPEN_GEOMETRY_OK first opening, three settled reopenings and one rapid close/open reversal keep width and visibility"));
            return Finish();
        }
        Click(false);
        ++Round;
        Wait(4,Round==4?.1:1.);
        return false;
    }
    void Wait(int32 Next,double Delay){Stage=Next;ReadyAt=FPlatformTime::Seconds()+Delay;}
    bool Finish(){Cleanup();return true;}
    void Cleanup()
    {
        if(Mode.IsValid())Mode->CloseActivePanel();
        if(Preview.IsValid()&&GEngine&&GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
        Preview.Reset();
        if(Root.IsValid()){Root->ReleaseSlateResources(true);Root->ClearChildren();}
        SizeBoxes.Reset();Mode.Reset();HUD.Reset();Root.Reset();HUDSlot=nullptr;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattleAuthoredVehiclePanelRuntimeTest,
    "SilverChoir.BattleUI.Panels.AuthoredVehicle", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBattleAuthoredVehiclePanelRuntimeTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(BattleAuthoredVehiclePanelTest::FRuntime(this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattleVehicleReopenGeometryRuntimeTest,
    "SilverChoir.BattleUI.Panels.VehicleReopenGeometry", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBattleVehicleReopenGeometryRuntimeTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(BattleAuthoredVehiclePanelTest::FReopenGeometry(this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattleAuthoredPanelAnimationRuntimeTest,
    "SilverChoir.BattleUI.Panels.AuthoredAnimationOwnership", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBattleAuthoredPanelAnimationRuntimeTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(BattleAuthoredVehiclePanelTest::FAnimationOwnership(this));
    return true;
}
#endif
