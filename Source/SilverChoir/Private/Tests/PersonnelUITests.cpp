#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UIBasic/ContentFitBox.h"
#include "Components/SizeBox.h"
#include "Layout/ArrangedChildren.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/PersonnelPreparationRoomWidget.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Animation/WidgetAnimation.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "Engine/DataTable.h"
#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContentFitLayoutTest,"SilverChoir.BaseUI.ContentFitLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FContentFitLayoutTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UContentFitBox> Fit(NewObject<UContentFitBox>());
    auto* Content=NewObject<USizeBox>(Fit.Get());
    Content->SetWidthOverride(200); Content->SetHeightOverride(600);
    Fit->AddChild(Content);
    auto Widget=Fit->TakeWidget(); Widget->SlatePrepass(1.f);
    for (float Height:{1200.f,600.f,300.f})
    {
        FArrangedChildren Children(EVisibility::All);
        Widget->ArrangeChildren(FGeometry::MakeRoot(FVector2D(200,Height),FSlateLayoutTransform()),Children);
        if (!TestEqual(TEXT("Single content"),Children.Num(),1)) return false;
        const auto& G=Children[0].Geometry;
        const float ExpectedScale=FMath::Min(1.f,Height/600.f);
        TestTrue(TEXT("Only shrink below natural height"),FMath::IsNearlyEqual(G.GetAccumulatedLayoutTransform().GetScale(),ExpectedScale));
        TestTrue(TEXT("Tall space is available to Fill slots"),FMath::IsNearlyEqual(float(G.GetLocalSize().Y),Height/ExpectedScale));
    }
    return true;
}

class FPersonnelCapture : public IAutomationLatentCommand
{
    TWeakObjectPtr<UBaseMapWidget> Shell;
    TWeakObjectPtr<UPersonnelPreparationRoomWidget> Room;
    double Started=FPlatformTime::Seconds();
    int32 Step=0;
    int32 ExitStep=0;
    FAutomationTestBase* Test;
public:
    FPersonnelCapture(UBaseMapWidget* S,UPersonnelPreparationRoomWidget* R,FAutomationTestBase* T):Shell(S),Room(R),Test(T){}
    bool Update() override
    {
        if (!Shell.IsValid()) return true;
        const double Elapsed=FPlatformTime::Seconds()-Started;
        if (Step<3 && Elapsed>2+Step*2)
        {
            FString Tag=TEXT("Personnel");
            FParse::Value(FCommandLine::Get(),TEXT("PersonnelCaptureTag="),Tag);
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/PersonnelUI")/FString::Printf(TEXT("%s_%d.png"),*FPaths::MakeValidFileName(Tag),Step),true,false);
            ++Step;
        }
        else if (Step>0 && Step<3 && Elapsed>1+Step*2) Room->SetDetailPage(EPersonnelDetailPage(Step));
        if (Elapsed>8 && ExitStep==0)
        {
            Room->SetDetailPage(EPersonnelDetailPage::Equipment);
            Test->TestTrue(TEXT("Old list retained during slide out"),Room->CurrentListDisplay==EPersonnelListDisplay::Personnel);
            Test->TestTrue(TEXT("Sequential transition active"),Room->bListTransitioning);
            ExitStep=1;
        }
        if (Elapsed>10 && ExitStep==1)
        {
            Test->TestTrue(TEXT("Slide out then slide in reaches warehouse"),Room->CurrentListDisplay==EPersonnelListDisplay::Warehouse);
            Test->TestFalse(TEXT("List transition finished"),Room->bListTransitioning);
            Test->TestTrue(TEXT("Warehouse exit selected"),Room->GetCurrentListAnimation(false) && Room->GetCurrentListAnimation(false)->GetName().Contains(TEXT("仓库滑出")));
            Shell->UnloadCurrentSceneUI();
            Test->TestTrue(TEXT("Exit keeps warehouse mounted"),Shell->GetCurrentSceneUI()==Room.Get());
            Room->SetDetailPage(EPersonnelDetailPage::Profile);
            Test->TestTrue(TEXT("Closing locks list mode"),Room->CurrentListDisplay==EPersonnelListDisplay::Warehouse);
            ExitStep=2;
        }
        if (Elapsed>13 && ExitStep==2)
        {
            Test->TestNull(TEXT("Warehouse Blueprint animation completes unload"),Shell->GetCurrentSceneUI());
            Room=Cast<UPersonnelPreparationRoomWidget>(Shell->CreateSceneUIByTag(FGameplayTag::RequestGameplayTag(TEXT("GameScene.PersonnelPreparationRoom"))));
            ExitStep=3;
        }
        if (Elapsed>16 && ExitStep==3)
        {
            if (!Test->TestNotNull(TEXT("Reopen personnel room"),Room.Get())) { Shell->RemoveFromParent(); return true; }
            Test->TestTrue(TEXT("Reopen defaults to personnel"),Room->CurrentListDisplay==EPersonnelListDisplay::Personnel);
            Room->SetDetailPage(EPersonnelDetailPage::Equipment);
            Shell->UnloadCurrentSceneUI();
            Test->TestFalse(TEXT("Close cancels pending list transition"),Room->bListTransitioning);
            Test->TestTrue(TEXT("Close retains actually visible personnel list"),Room->CurrentListDisplay==EPersonnelListDisplay::Personnel);
            ExitStep=4;
        }
        if (Elapsed>19)
        {
            Test->TestNull(TEXT("Personnel Blueprint animation completes unload"),Shell->GetCurrentSceneUI());
            Shell->RemoveFromParent(); return true;
        }
        return false;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersonnelUITest,"SilverChoir.BaseUI.Personnel",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FPersonnelUITest::RunTest(const FString& Parameters)
{
    UWorld* World=nullptr;
    for (auto& Context:GEngine->GetWorldContexts()) if (Context.WorldType==EWorldType::Game) { World=Context.World(); break; }
    if (!TestNotNull(TEXT("World"),World)) return false;
    auto* Class=LoadClass<UBaseMapWidget>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget.BP_BaseMapWidget_C"));
    if (!TestNotNull(TEXT("Shell class"),Class)) return false;
    auto* Shell=CreateWidget<UBaseMapWidget>(World->GetFirstPlayerController(),Class);
    Shell->SetCurrencyAmount(128450); Shell->AddToViewport(10000);
    auto* Room=Cast<UPersonnelPreparationRoomWidget>(Shell->CreateSceneUIByTag(FGameplayTag::RequestGameplayTag(TEXT("GameScene.PersonnelPreparationRoom"))));
    if (!TestNotNull(TEXT("Personnel room mounted by tag"),Room)) return false;
    TestEqual(TEXT("No automatically generated mock roster"),Room->PersonnelData.Num(),0);
    auto Units=UPlayerUnitLibrary::CreateUnitDataBatch(FUnitTemplate(),8);
    auto* Templates=LoadObject<UDataTable>(nullptr,TEXT("/Game/System/SubSystem/PlayerUnitSubSystem/DT_UnitTemplates"));
    if(!TestNotNull(TEXT("Authored portrait templates"),Templates))return false;
    const auto TemplateNames=Templates->GetRowNames();
    for(int32 I=0;I<Units.Num();++I)Units[I].Profile=Templates->FindRow<FUnitTemplate>(TemplateNames[I%TemplateNames.Num()],TEXT("PersonnelUI"))->Profile;
    for (int32 I=0;I<Units.Num();++I) Units[I].RuntimeData.TileId=(I==2 || I==5)?FName(TEXT("Remote")):FName(TEXT("Base"));
    FText Error;
    TestTrue(TEXT("Load authoritative unit records"),UPlayerUnitLibrary::LoadPlayerUnitData(World,Units,Error));
    TestTrue(TEXT("Load left roster"),Room->LoadPersonnelList(TEXT("Base")));
    TestEqual(TEXT("Standby filter matches tile"),Room->GetVisiblePersonnelCount(),6);
    const FName RemoteID(*Units[2].UnitId.ToString());
    auto* Heading=Cast<UTextBlock>(Room->GetWidgetFromName(TEXT("SelectedName")));
    const FString OriginalHeading=Heading?Heading->GetText().ToString():FString();
    auto Click=[&](const TCHAR* Name)
    {
        auto* B=Cast<USelectionButtonWidget>(Room->GetWidgetFromName(Name));
        if (TestNotNull(Name,B)) B->OnClicked.Broadcast();
    };
    Click(TEXT("AllFilter"));
    TestEqual(TEXT("All button is wired"),Room->GetVisiblePersonnelCount(),8);
    TestFalse(TEXT("Remote personnel disabled even in All"),Room->SelectPersonnel(RemoteID));
    const FName LocalID(*Units[0].UnitId.ToString());
    TestTrue(TEXT("Local personnel selectable"),Room->SelectPersonnel(LocalID));
    auto* List=Cast<UScrollBox>(Room->GetWidgetFromName(TEXT("PersonnelList")));
    if (TestNotNull(TEXT("Roster scroll box"),List))
    {
        int32 Disabled=0;
        for (auto* Child:List->GetAllChildren())
        {
            auto* Row=Cast<USelectionButtonWidget>(Child);
            if (!Row) continue;
            if (!Row->GetIsEnabled()) ++Disabled;
            TestTrue(TEXT("New row measured immediately, before next frame"),Row->GetDesiredSize().Y>=Row->MinimumSize.Y);
        }
        TestEqual(TEXT("Two remote rows visually disabled"),Disabled,2);
    }
    Click(TEXT("EquipmentTab"));
    TestTrue(TEXT("Equipment waits for roster slide out"),Room->CurrentListDisplay==EPersonnelListDisplay::Personnel);
    auto* Warehouse=Room->GetWidgetFromName(TEXT("WarehousePanel"));
    if (TestNotNull(TEXT("Warehouse placeholder bound"),Warehouse)) TestTrue(TEXT("Warehouse hidden until old panel exits"),Warehouse->GetVisibility()==ESlateVisibility::Collapsed);
    TestTrue(TEXT("Roster remains visible during exit"),Room->GetWidgetFromName(TEXT("RosterPanel"))->GetVisibility()!=ESlateVisibility::Collapsed);
    TestTrue(TEXT("Equipment tab is wired"),Room->CurrentPage==EPersonnelDetailPage::Equipment);
    Click(TEXT("TrainingTab"));
    TestTrue(TEXT("Training restores personnel"),Room->CurrentListDisplay==EPersonnelListDisplay::Personnel);
    TestTrue(TEXT("Training tab is wired"),Room->CurrentPage==EPersonnelDetailPage::Training);
    TestEqual(TEXT("Tabs preserve selection"),Room->SelectedPersonnelID,LocalID);
    Click(TEXT("StandbyFilter"));
    FPersonnelViewData Current; TestTrue(TEXT("Selection resolves after filter"),Room->GetSelectedPersonnel(Current));
    TestTrue(TEXT("Filtered-out selection replaced with standby"),Current.bStandby);
    TestFalse(TEXT("Filtered personnel cannot be selected"),Room->SelectPersonnel(RemoteID));
    TestFalse(TEXT("Unknown ID rejected"),Room->SelectPersonnel(TEXT("Missing")));
    if (Heading) TestEqual(TEXT("Left selection does not update right panel"),Heading->GetText().ToString(),OriginalHeading);
    Units[2].RuntimeData.TileId=TEXT("Base");
    TestTrue(TEXT("Move unit through manager"),UPlayerUnitLibrary::UpdatePlayerUnitData(World,Units[2].UnitId,Units[2]));
    TestEqual(TEXT("Live position update refreshes standby"),Room->GetVisiblePersonnelCount(),7);
    Room->LoadPersonnelList(NAME_None);
    TestEqual(TEXT("Unset tile has no standby"),Room->GetVisiblePersonnelCount(),0);
    Room->SetRosterFilter(EPersonnelRosterFilter::All);
    TestEqual(TEXT("All ignores position"),Room->GetVisiblePersonnelCount(),8);
    TestTrue(TEXT("No auto-selection without a current tile"),Room->SelectedPersonnelID.IsNone());
    TestFalse(TEXT("Unset tile cannot select anyone"),Room->SelectPersonnel(LocalID));
    Room->LoadPersonnelList(TEXT("Base"));
    Room->SetRosterFilter(EPersonnelRosterFilter::Standby); Room->SetDetailPage(EPersonnelDetailPage::Profile);
    TestTrue(TEXT("Bind shared profile by ID"),Room->BindPersonnelProfile(Units[0].UnitId));
    auto Shared=UPlayerUnitLibrary::GetUnitDataShared(World,Units[0].UnitId);
    Shared->Modify([](FUnitData& D){ D.Profile.PersonnelResume=FText::FromString(TEXT("Live resume")); D.Evaluation.Content=FText::FromString(TEXT("Reliable")); D.ServiceRecord.BattleCount=7; });
    auto* Resume=Cast<UTextBlock>(Room->GetWidgetFromName(TEXT("BiographyText")));
    auto* Evaluation=Cast<UTextBlock>(Room->GetWidgetFromName(TEXT("EvaluationText")));
    auto* Battle=Cast<UTextBlock>(Room->GetWidgetFromName(TEXT("BattleRecordText")));
    if (TestNotNull(TEXT("Resume field"),Resume)) TestEqual(TEXT("Shared event refreshes resume"),Resume->GetText().ToString(),FString(TEXT("Live resume")));
    if (TestNotNull(TEXT("Evaluation field"),Evaluation)) TestEqual(TEXT("Shared event refreshes evaluation"),Evaluation->GetText().ToString(),FString(TEXT("Reliable")));
    if (TestNotNull(TEXT("Battle field"),Battle)) TestTrue(TEXT("Shared event refreshes service"),Battle->GetText().ToString().Contains(TEXT("7")));
    TestTrue(TEXT("Switch profile"),Room->BindPersonnelProfile(Units[1].UnitId));
    const FString OtherResume=Resume?Resume->GetText().ToString():FString();
    Shared->Modify([](FUnitData& D){ D.Profile.PersonnelResume=FText::FromString(TEXT("Old unit")); });
    if (Resume) TestEqual(TEXT("Old record no longer drives profile"),Resume->GetText().ToString(),OtherResume);
    TestFalse(TEXT("Invalid profile ID clears binding"),Room->BindPersonnelProfile(FGuid()));
    if (Resume) TestEqual(TEXT("Invalid ID clears stale content"),Resume->GetText().ToString(),FString(TEXT("暂无简历")));
    if (FParse::Param(FCommandLine::Get(),TEXT("PersonnelDataOnly")))
    {
        Shell->RemoveFromParent();
        return true;
    }
    Shared->Modify([&](FUnitData& D){D.Profile=Units[0].Profile;D.Evaluation.Content=FText::GetEmpty();});
    Room->SelectPersonnel(FName(*Units[0].UnitId.ToString()));
    Room->BindPersonnelProfile(Units[0].UnitId);
    auto* Portrait=Cast<UPersonnelPortraitWidget>(Room->GetWidgetFromName(TEXT("DetailPortrait")));
    if(TestNotNull(TEXT("Personnel record portrait"),Portrait))
    {
        TestTrue(TEXT("Only detail portrait uses complete vertical view"),Portrait->PortraitFormat==EPortraitFormat::Full);
        auto* PortraitSlot=Cast<UCanvasPanelSlot>(Portrait->Slot);
        if(TestNotNull(TEXT("Personnel record portrait frame"),PortraitSlot))
        {
            const FVector2D Size=PortraitSlot->GetSize();
            TestTrue(TEXT("Personnel record portrait frame is 3:4"),FMath::IsNearlyEqual(Size.X/Size.Y,.75));
        }
    }
    TestNull(TEXT("Old square backing removed"),Room->GetWidgetFromName(TEXT("PortraitBacking")));
    TestNull(TEXT("Redundant caption removed"),Room->GetWidgetFromName(TEXT("IdentityCaption")));
    ADD_LATENT_AUTOMATION_COMMAND(FPersonnelCapture(Shell,Room,this));
    return true;
}
#endif
