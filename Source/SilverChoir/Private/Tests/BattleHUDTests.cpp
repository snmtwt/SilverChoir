#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Map/BattleMap/BattleHUDLibrary.h"
#include "Map/BattleMap/BattleHUDWidgets.h"
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "MTS_SubMapSubsystem.h"
#include "MTS_SubMapDataAsset.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Widgets/SWidget.h"
#include "InputCoreTypes.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattleHUDLayoutTest,"SilverChoir.BattleUI.LayoutAndHandOccupancy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FBattleHUDLayoutTest::RunTest(const FString&)
{
    for(int Selection=0;Selection<6;++Selection)for(auto Drawer:{EBattleDrawer::Member,EBattleDrawer::Vehicle})for(float Amount:{0.f,.25f,1.f})
    {
        const auto L=UBattleHUDLibrary::CalculateDockLayout(1800,6,Selection,Drawer,Amount);
        TestEqual(TEXT("Every frame keeps six members"),L.MemberX.Num(),6);
        TestTrue(TEXT("Readable member width on 1920 viewport"),L.MemberWidth>=200.f);
        for(int I=0;I<5;++I)TestTrue(TEXT("Cards do not overlap"),L.MemberX[I]+L.MemberWidth<=L.MemberX[I+1]);
        TestTrue(TEXT("Sixth member never overlaps vehicle button"),L.MemberX.Last()+L.MemberWidth<=L.VehicleButtonX);
        if(Amount>0&&Drawer==EBattleDrawer::Member)
        {TestTrue(TEXT("Drawer follows selected card"),L.DrawerX>=L.MemberX[Selection]+L.MemberWidth);if(Selection<5)TestTrue(TEXT("Drawer does not cover next member"),L.DrawerX+L.DrawerWidth<=L.MemberX[Selection+1]+.1f);}
        if(Drawer==EBattleDrawer::Vehicle)TestTrue(TEXT("Vehicle drawer at right end"),L.DrawerX+L.DrawerWidth<=L.VehicleButtonX);
    }
    TestEqual(TEXT("NaN percentage is sanitized"),UBattleHUDLibrary::SanitizeRatio(std::numeric_limits<float>::quiet_NaN()),0.f);
    TestEqual(TEXT("Ratio clamps high"),UBattleHUDLibrary::SanitizeRatio(8.f),1.f);
    TestFalse(TEXT("Two empty hands must remain split"),UBattleHUDLibrary::AreHandsLinked({},{}));
    const FGuid A=FGuid::NewGuid(),B=FGuid::NewGuid();TestTrue(TEXT("One item occupies both hands"),UBattleHUDLibrary::AreHandsLinked(A,A));TestFalse(TEXT("Distinct pistol instances stay split"),UBattleHUDLibrary::AreHandsLinked(A,B));
    return true;
}

namespace BattleHUDTests
{
class FRuntime : public IAutomationLatentCommand
{
    FAutomationTestBase* T;
    TWeakObjectPtr<AGameMainMapPlayerController> PC;
    TWeakObjectPtr<UBattleMapWidget> UI;
    TStrongObjectPtr<UMTS_SubMapDataAsset> Map;
    double Started=FPlatformTime::Seconds(),At=Started;
    int Step=0;
    FGuid MemberId;
    void Next(){++Step;At=FPlatformTime::Seconds();}
    void Capture(const TCHAR* Name)
    {const FString Dir=FPaths::ProjectSavedDir()/TEXT("Screenshots/BattleHUD");IFileManager::Get().MakeDirectory(*Dir,true);FScreenshotRequest::RequestScreenshot(Dir/Name,true,false);}
    void Click(const TCHAR* WidgetName)
    {auto* B=UI.IsValid()?Cast<UBattleHUDButton>(UI->GetWidgetFromName(WidgetName)):nullptr;if(T->TestNotNull(WidgetName,B))B->OnClicked.Broadcast();}
    void PointerClick(UWidget* Widget)
    {
        // Exercise the real Slate -> UUserWidget press/release path and delayed click.
        const auto Slate=Widget->TakeWidget();
        const auto Geometry=Widget->GetCachedGeometry();
        const auto Position=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
        TSet<FKey> Keys;Keys.Add(EKeys::LeftMouseButton);
        const FPointerEvent Down(0,Position,Position,Keys,EKeys::LeftMouseButton,0,FModifierKeysState());
        T->TestTrue(TEXT("Pointer press handled by battle control"),Slate->OnMouseButtonDown(Geometry,Down).IsEventHandled());
        Keys.Reset();const FPointerEvent Up(0,Position,Position,Keys,EKeys::LeftMouseButton,0,FModifierKeysState());
        T->TestTrue(TEXT("Pointer release handled by battle control"),Slate->OnMouseButtonUp(Geometry,Up).IsEventHandled());
    }
public:
    explicit FRuntime(FAutomationTestBase* Test):T(Test){}
    virtual bool Update() override
    {
        if(FPlatformTime::Seconds()-Started>90){T->AddError(FString::Printf(TEXT("Battle UI timeout at step %d"),Step));return true;}
        if(FPlatformTime::Seconds()-At<.7)return false;
        if(!PC.IsValid())for(const auto& C:GEngine->GetWorldContexts())if(C.WorldType==EWorldType::Game&&C.World())PC=Cast<AGameMainMapPlayerController>(C.World()->GetFirstPlayerController());
        if(!PC.IsValid())return false;
        auto* World=PC->GetWorld();auto* Sub=World->GetSubsystem<UMTS_SubMapSubsystem>();
        switch(Step)
        {
        case 0:
        {
            Map.Reset(NewObject<UMTS_SubMapDataAsset>());Map->MapID=TEXT("BattleHUDTest");Map->MapName=TEXT("BattleHUDTest");Map->MapAsset=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/System/Map/BattleMap/L_BattleTemplate.L_BattleTemplate")));
            FText Error;if(!T->TestTrue(TEXT("Load battle map using plugin"),Sub->LoadSubMap(Map.Get(),FVector::ZeroVector,FRotator::ZeroRotator,Error)))return true;Next();return false;
        }
        case 1:
        {
            FMTS_SubMapInfo Info;if(!Sub->GetSubMapByKey(TEXT("BattleHUDTest"),Info)||Info.State!=EMTS_SubMapState::Loaded)return false;
            auto* Mode=World->GetAuthGameMode<AGameMainMapGameMode>();if(!T->TestNotNull(TEXT("Existing main-map mode"),Mode))return true;
            T->TestTrue(TEXT("Map activation creates new Battle HUD"),Mode->ActivateLoadedMap(TEXT("BattleHUDTest"),FTransform::Identity,EGameMainMapType::Battle));UI=PC->BattleWidget;
            if(!T->TestNotNull(TEXT("Battle widget"),UI.Get()))return true;
            T->TestEqual(TEXT("Blueprint InitializeMapUI loaded three demo squads"),UI->Squads.Num(),3);
            T->TestEqual(TEXT("Six cards instantiated"),UI->MemberCards.Num(),6);if(UI->MemberCards.Num()!=6)return true;
            MemberId=UI->MemberCards[2]->Member.UnitId;
            // Layout is not ready in the frame the map UI is constructed.
            Step=10;At=FPlatformTime::Seconds();return false;
        }
        case 10:
            PointerClick(UI->MemberCards[2]);Step=2;At=FPlatformTime::Seconds();return false;
        case 2:
        {
            T->TestEqual(TEXT("Card click opens member drawer"),UI->ActiveDrawer,EBattleDrawer::Member);
            T->TestEqual(TEXT("Card selects its own unit"),UI->SelectedMemberId,MemberId);
            auto* Drawer=UI->GetWidgetFromName(TEXT("MemberDrawerPanel"));auto* D=Cast<UCanvasPanelSlot>(Drawer->Slot);auto* C=Cast<UCanvasPanelSlot>(UI->MemberCards[2]->Slot);auto* N=Cast<UCanvasPanelSlot>(UI->MemberCards[3]->Slot);
            T->TestTrue(TEXT("Actual UMG drawer lies between selected and following card"),D->GetPosition().X>=C->GetPosition().X+C->GetSize().X&&D->GetPosition().X+D->GetSize().X<=N->GetPosition().X+.5);
            for(auto Card:UI->MemberCards){T->TestNotNull(TEXT("ECG binding"),Card->Heartbeat.Get());T->TestNotNull(TEXT("Stamina binding"),Card->StaminaBar.Get());T->TestTrue(TEXT("Square portrait data"),Card->Member.Profile.PortraitTexture!=nullptr);}
            T->TestTrue(TEXT("Rifle uses merged hands"),UI->MemberCards[0]->MergedHands->IsVisible());T->TestFalse(TEXT("Pistol uses split hands"),UI->MemberCards[3]->MergedHands->IsVisible());
            Click(TEXT("Command_Crouch"));FBattleMemberView M;UI->GetMemberView(MemberId,M);T->TestEqual(TEXT("Blueprint stance branch updates selected snapshot"),M.Posture,EBattlePosture::Crouched);
            Click(TEXT("Command_Stealth"));UI->GetMemberView(MemberId,M);T->TestTrue(TEXT("Blueprint stealth branch"),M.bStealth);
            Click(TEXT("Command_Quick1"));UI->GetMemberView(MemberId,M);T->TestEqual(TEXT("Blueprint quick-item branch"),M.QuickItemCounts[0],1);
            const FKeyEvent QuickKey(EKeys::Two,FModifierKeysState(),0,false,0,0);
            T->TestTrue(TEXT("Quick-item keyboard input is handled"),UI->TakeWidget()->OnPreviewKeyDown(UI->GetCachedGeometry(),QuickKey).IsEventHandled());
            UI->GetMemberView(MemberId,M);T->TestEqual(TEXT("Keyboard quick item routes through Blueprint"),M.QuickItemCounts[1],1);
            Click(TEXT("Command_Interact"));T->TestTrue(TEXT("Blueprint interaction branch enters targeting"),UI->bTargeting);T->TestEqual(TEXT("Interaction cursor"),PC->CurrentMouseCursor.GetValue(),EMouseCursor::Hand);UI->CancelTargeting();
            Capture(TEXT("01_MemberDrawer.png"));Next();return false;
        }
        case 3:
            PointerClick(UI->GetWidgetFromName(TEXT("VehicleButton")));Next();return false;
        case 4:
        {
            T->TestEqual(TEXT("Vehicle drawer takes over"),UI->ActiveDrawer,EBattleDrawer::Vehicle);T->TestFalse(TEXT("Person drawer closed"),UI->GetWidgetFromName(TEXT("MemberDrawerPanel"))->IsVisible());T->TestTrue(TEXT("Vehicle drawer visible"),UI->GetWidgetFromName(TEXT("VehicleDrawerPanel"))->IsVisible());
            T->TestEqual(TEXT("Vehicle mode keeps all six cards"),UI->MemberCards.Num(),6);Click(TEXT("Vehicle_0"));T->TestEqual(TEXT("Blueprint demo boarding respects capacity"),UI->Squads[0].Vehicle.Occupants,6);Click(TEXT("Vehicle_1"));T->TestEqual(TEXT("Blueprint demo leaving"),UI->Squads[0].Vehicle.Occupants,0);
            Capture(TEXT("02_VehicleDrawer.png"));Next();return false;
        }
        case 5:
        {
            UI->MemberCards[5]->OnClicked.Broadcast();T->TestEqual(TEXT("Last member replaces vehicle drawer"),UI->ActiveDrawer,EBattleDrawer::Member);Click(TEXT("Command_Backpack"));T->TestTrue(TEXT("Blueprint backpack shows test preview"),UI->GetWidgetFromName(TEXT("ModalPanel"))->IsVisible());UI->CloseModal();
            UI->SelectSquad(UI->Squads[2].SquadId);T->TestFalse(TEXT("No-vehicle squad cannot open drawer"),UI->ToggleVehicleDrawer());T->TestFalse(TEXT("No-vehicle button disabled"),UI->GetWidgetFromName(TEXT("VehicleButton"))->GetIsEnabled());T->TestEqual(TEXT("Switch squad rebuilds exactly its members"),UI->MemberCards.Num(),3);
            auto Invalid=UI->Squads;Invalid[0].Members[1].UnitId=Invalid[0].Members[0].UnitId;T->TestFalse(TEXT("Duplicate IDs rejected atomically"),UI->SetBattleSquads(Invalid,true));T->TestEqual(TEXT("Rejected roster keeps selection"),UI->SelectedSquadId,UI->Squads[2].SquadId);
            Invalid=UI->Squads;Invalid[0].Members.SetNum(129);T->TestFalse(TEXT("Oversized roster rejected before layout indexing"),UI->SetBattleSquads(Invalid,true));
            UI->SelectSquad(UI->Squads[0].SquadId);UI->SelectMember(MemberId);Capture(TEXT("03_SelectionReturn.png"));Next();return false;
        }
        case 6:
        {
            Click(TEXT("Command_Pickup"));T->TestTrue(TEXT("Pickup targeting active before closing"),UI->bTargeting);auto* Before=UI.Get();PC->SwitchMapUI(EGameMainMapType::None);T->TestFalse(TEXT("UI teardown releases target cursor"),Before->bTargeting);T->TestTrue(TEXT("UI teardown restores cursor"),PC->CurrentMouseCursor!=EMouseCursor::Hand);
            T->TestTrue(TEXT("Battle UI can be recreated"),PC->SwitchMapUI(EGameMainMapType::Battle));T->TestEqual(TEXT("Demo data was not persisted or mutated"),PC->BattleWidget->Squads[0].Members[2].QuickItemCounts[0],2);Sub->UnloadSubMapByID(TEXT("BattleHUDTest"));
            T->AddInfo(TEXT("BATTLE_UI_RUNTIME_OK plugin activation, editable Blueprint initialization, six cards, inline drawer, Blueprint commands, vehicle replacement, no-vehicle case, duplicate rejection, cursor teardown and nonpersistent fixtures"));return true;
        }
        }
        return true;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattleHUDRuntimeTest,"SilverChoir.BattleUI.RuntimeFlow",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FBattleHUDRuntimeTest::RunTest(const FString&){ADD_LATENT_AUTOMATION_COMMAND(BattleHUDTests::FRuntime(this));return true;}
#endif
