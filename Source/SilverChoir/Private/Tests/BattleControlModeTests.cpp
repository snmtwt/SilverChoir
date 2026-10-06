#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/BattleMap/BattleModePanels.h"
#include "Map/BattleMap/BattleModeScrollBox.h"
#include "Map/BattleMap/BattleHUDWidgets.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Widget/SlotContainerByType/SIS_MirrorSlotContainer.h"
#include "Widget/SlotContainer/SIS_SlotContainer.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/SizeBox.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace BattleControlModeTest
{
class FRuntime : public IAutomationLatentCommand
{
public:
    explicit FRuntime(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual ~FRuntime() override { Cleanup(); }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 60.)
        {
            Test->AddError(FString::Printf(TEXT("Battle mode runtime timed out at stage %d"), Stage));
            return true;
        }
        if (Stage == 0)
        {
            if (!GEngine || !GEngine->GameViewport) return false;
            APlayerController* PC = nullptr;
            for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game && Context.World())
                    PC = Context.World()->GetFirstPlayerController();
            if (!PC) return false;
            if (!CreatePreview(PC)) return true;
            Advance();
            return false;
        }
        if (FPlatformTime::Seconds() - StageStarted < .65) return false;
        switch (Stage)
        {
        case 1:
            if (!ValidateBindings()) return true;
            RememberFixtures();
            ValidateLayout();
            ValidatePage(EBattleControlMode::Member);
            ValidateInventoryHitPath();
            Capture(TEXT("01_Member.png"));
            Advance();
            return false;
        case 2:
            if (!CaptureFinished()) return false;
            ValidateOuterInput();
            AddOverflowFixtures();
            Advance();
            return false;
        case 3:
            Test->TestTrue(TEXT("Outer input cannot move the member page on a later frame"),
                FMath::IsNearlyEqual(UI->ModePages->GetScrollOffset(), OuterOffset, .1f));
            Wheel(UI->MemberModePanel->MemberScrollBox, -3.f);
            Wheel(UI->MemberModePanel->SquadScrollBox, -3.f);
            Advance();
            return false;
        case 4:
            MemberOffset = UI->MemberModePanel->MemberScrollBox->GetScrollOffset();
            SquadOffset = UI->MemberModePanel->SquadScrollBox->GetScrollOffset();
            Test->TestTrue(TEXT("Nested horizontal member list responds to wheel input"), MemberOffset > 1.f);
            Test->TestTrue(TEXT("Nested vertical squad list responds to wheel input"), SquadOffset > 1.f);
            UI->SetControlMode(EBattleControlMode::Member, true);
            Test->TestTrue(TEXT("Repeated current mode leaves member-list position intact"),
                FMath::IsNearlyEqual(UI->MemberModePanel->MemberScrollBox->GetScrollOffset(), MemberOffset, .1f));
            PointerClick(UI->SquadModeButton);
            Advance();
            return false;
        case 5:
            ValidatePage(EBattleControlMode::Squad);
            ValidateInnerOffsets();
            ValidateOuterInput();
            Capture(TEXT("02_Squad.png"));
            Advance();
            return false;
        case 6:
            if (!CaptureFinished()) return false;
            Test->TestTrue(TEXT("Outer input cannot move the squad page on a later frame"),
                FMath::IsNearlyEqual(UI->ModePages->GetScrollOffset(), OuterOffset, .1f));
            Test->TestTrue(TEXT("Empty presentation snapshot is accepted without gameplay initialization"),
                UI->SetBattleSquads(TArray<FBattleSquadView>()));
            Test->TestEqual(TEXT("Snapshot refresh keeps the chosen control mode"), UI->CurrentControlMode, EBattleControlMode::Squad);
            Test->TestTrue(TEXT("Snapshot refresh keeps squad mode button selected"), UI->SquadModeButton->IsSelected());
            PointerClick(UI->MemberModeButton);
            Advance();
            return false;
        case 7:
            ValidatePage(EBattleControlMode::Member);
            ValidateInnerOffsets();
            UI->SetControlMode(EBattleControlMode::Squad, true);
            UI->SetControlMode(EBattleControlMode::Member, true);
            UI->SetControlMode(EBattleControlMode::Squad, true);
            Advance();
            return false;
        case 8:
            ValidatePage(EBattleControlMode::Squad);
            UI->SetControlMode(EBattleControlMode::Member, false);
            Advance();
            return false;
        case 9:
            ValidatePage(EBattleControlMode::Member);
            RemoveOverflowFixtures();
            Advance();
            return false;
        case 10:
            ValidateFixturesPreserved();
            ValidateLayout();
            ValidateInventoryHitPath();
            if (!Test->HasAnyErrors())
                Test->AddInfo(TEXT("BATTLE_CONTROL_MODES_OK real buttons switch aligned pages; outer input cannot pan; nested lists and inventory remain interactive; authored children and layout preserved"));
            Cleanup();
            return true;
        default:
            return true;
        }
    }

private:
    FAutomationTestBase* Test;
    TStrongObjectPtr<UCanvasPanel> Root;
    TStrongObjectPtr<UBattleMapWidget> UI;
    TSharedPtr<SWidget> Preview;
    TArray<UWidget*> OriginalMembers, OriginalMemberSquads, OriginalSquadSquads, OriginalSquadCards;
    USizeBox* MemberOverflow = nullptr;
    USizeBox* SquadOverflow = nullptr;
    float OuterOffset = 0.f, MemberOffset = 0.f, SquadOffset = 0.f;
    float OriginalMemberOffset = 0.f, OriginalSquadOffset = 0.f;
    FVector2D MemberButtonSize, SquadButtonSize;
    FString ScreenshotPath;
    FDateTime PreviousScreenshotTime;
    double Started = FPlatformTime::Seconds(), StageStarted = Started;
    int32 Stage = 0;

    void Advance() { ++Stage; StageStarted = FPlatformTime::Seconds(); }
    void Cleanup()
    {
        if (UI.IsValid())
        {
            if (UI->MemberModeButton) UI->MemberModeButton->CancelPendingClick();
            if (UI->SquadModeButton) UI->SquadModeButton->CancelPendingClick();
        }
        if (Preview.IsValid() && GEngine && GEngine->GameViewport)
            GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
        Preview.Reset();
        if (Root.IsValid()) Root->ClearChildren();
        UI.Reset(); Root.Reset();
        MemberOverflow = nullptr; SquadOverflow = nullptr;
    }

    bool CreatePreview(APlayerController* PC)
    {
        UClass* Class = LoadClass<UBattleMapWidget>(nullptr,
            TEXT("/Game/System/Map/BattleMap/UI/WBP_BattleHUD.WBP_BattleHUD_C"));
        if (!Test->TestNotNull(TEXT("Actual battle HUD Blueprint exists"), Class)) return false;
        UI.Reset(CreateWidget<UBattleMapWidget>(PC, Class));
        if (!Test->TestNotNull(TEXT("Battle HUD instance"), UI.Get())) return false;
        Root.Reset(NewObject<UCanvasPanel>(PC));
        auto* Slot = Root->AddChildToCanvas(UI.Get());
        Slot->SetAnchors(FAnchors(0, 0, 1, 1));
        Slot->SetOffsets(FMargin(0));
        // Deliberately skip InitializeMapUI/InitializeTestHUD. Designer child
        // fixtures must survive native construction without business population.
        Preview = Root->TakeWidget();
        GEngine->GameViewport->AddViewportWidgetContent(Preview.ToSharedRef(), 100);
        return true;
    }

    bool ValidateBindings()
    {
        bool Valid = true;
        Valid &= Test->TestNotNull(TEXT("Programmatic mode viewport"), UI->ModePages.Get());
        Valid &= Test->TestNotNull(TEXT("Member-mode panel"), UI->MemberModePanel.Get());
        Valid &= Test->TestNotNull(TEXT("Squad-mode panel"), UI->SquadModePanel.Get());
        Valid &= Test->TestNotNull(TEXT("Member-mode button"), UI->MemberModeButton.Get());
        Valid &= Test->TestNotNull(TEXT("Squad-mode button"), UI->SquadModeButton.Get());
        if (!Valid) return false;
        auto* Member = UI->MemberModePanel.Get(); auto* Squad = UI->SquadModePanel.Get();
        Valid &= Test->TestNotNull(TEXT("Member mode squad scroll"), Member->SquadScrollBox.Get());
        Valid &= Test->TestNotNull(TEXT("Member mode squad list"), Member->SquadList.Get());
        Valid &= Test->TestNotNull(TEXT("Member mode member scroll"), Member->MemberScrollBox.Get());
        Valid &= Test->TestNotNull(TEXT("Member mode member list"), Member->MemberList.Get());
        Valid &= Test->TestNotNull(TEXT("Squad mode squad scroll"), Squad->SquadScrollBox.Get());
        Valid &= Test->TestNotNull(TEXT("Squad mode squad list"), Squad->SquadList.Get());
        Valid &= Test->TestNotNull(TEXT("Squad mode card scroll"), Squad->SquadCardScrollBox.Get());
        Valid &= Test->TestNotNull(TEXT("Squad mode card list"), Squad->SquadCardList.Get());
        return Valid;
    }

    void RememberFixtures()
    {
        OriginalMembers = UI->MemberModePanel->MemberList->GetAllChildren();
        OriginalMemberSquads = UI->MemberModePanel->SquadList->GetAllChildren();
        OriginalSquadSquads = UI->SquadModePanel->SquadList->GetAllChildren();
        OriginalSquadCards = UI->SquadModePanel->SquadCardList->GetAllChildren();
        Test->TestEqual(TEXT("Six authored personnel cards survive Construct"), OriginalMembers.Num(), 6);
        Test->TestEqual(TEXT("Member-mode six authored squad rows survive Construct"), OriginalMemberSquads.Num(), 6);
        Test->TestEqual(TEXT("Squad-mode six authored squad rows survive Construct"), OriginalSquadSquads.Num(), 6);
        MemberButtonSize = FVector2D(UI->MemberModeButton->GetCachedGeometry().GetLocalSize());
        SquadButtonSize = FVector2D(UI->SquadModeButton->GetCachedGeometry().GetLocalSize());
    }

    void ValidateLayout()
    {
        auto* Dock = UI->GetWidgetFromName(TEXT("Border_132"));
        auto* Rail = Cast<USizeBox>(UI->GetWidgetFromName(TEXT("SizeBox_1")));
        if (Test->TestNotNull(TEXT("Authored dock border remains"), Dock))
            Test->TestTrue(TEXT("Dock retains its 160-unit height"),
                FMath::IsNearlyEqual(Dock->GetCachedGeometry().GetLocalSize().Y, 160.f, .5f));
        if (Test->TestNotNull(TEXT("Authored mode-button rail remains"), Rail))
        {
            Test->TestEqual(TEXT("Mode rail width override remains 26"), Rail->GetWidthOverride(), 26.f);
            Test->TestTrue(TEXT("Mode rail actual width remains 26"),
                FMath::IsNearlyEqual(Rail->GetCachedGeometry().GetLocalSize().X, 26.f, .5f));
        }
        Test->TestTrue(TEXT("Member button bounds are unchanged"),
            FVector2D(UI->MemberModeButton->GetCachedGeometry().GetLocalSize()).Equals(MemberButtonSize, .1));
        Test->TestTrue(TEXT("Squad button bounds are unchanged"),
            FVector2D(UI->SquadModeButton->GetCachedGeometry().GetLocalSize()).Equals(SquadButtonSize, .1));
        Test->TestEqual(TEXT("Mode pager never displays a scrollbar"), UI->ModePages->GetScrollBarVisibility(), ESlateVisibility::Collapsed);
        Test->TestTrue(TEXT("Vertical squad scrollbar stays compact"), UI->MemberModePanel->SquadScrollBox->GetScrollbarThickness().X <= 4.f);
        Test->TestTrue(TEXT("Horizontal member scrollbar stays compact"), UI->MemberModePanel->MemberScrollBox->GetScrollbarThickness().Y <= 4.f);
    }

    void ValidatePage(EBattleControlMode Expected)
    {
        const bool Member = Expected == EBattleControlMode::Member;
        Test->TestEqual(TEXT("Selected control mode matches the requested page"), UI->CurrentControlMode, Expected);
        Test->TestEqual(TEXT("Member page enabled only while active"), UI->MemberModePanel->GetIsEnabled(), Member);
        Test->TestEqual(TEXT("Squad page enabled only while active"), UI->SquadModePanel->GetIsEnabled(), !Member);
        Test->TestEqual(TEXT("Member mode selected appearance is exclusive"), UI->MemberModeButton->IsSelected(), Member);
        Test->TestEqual(TEXT("Squad mode selected appearance is exclusive"), UI->SquadModeButton->IsSelected(), !Member);
        UWidget* Page = Member ? static_cast<UWidget*>(UI->MemberModePanel.Get()) : static_cast<UWidget*>(UI->SquadModePanel.Get());
        const auto& ViewGeometry = UI->ModePages->GetCachedGeometry();
        const FVector2f LocalTop = ViewGeometry.AbsoluteToLocal(Page->GetCachedGeometry().LocalToAbsolute(FVector2f::ZeroVector));
        Test->TestTrue(FString::Printf(TEXT("Active page aligns to viewport top (offset %.2f)"), LocalTop.Y), FMath::Abs(LocalTop.Y) < 1.5f);
        if (Member) Test->TestTrue(TEXT("Member page uses the first scroll position"), UI->ModePages->GetScrollOffset() < 1.f);
        else Test->TestTrue(TEXT("Squad page uses the second scroll position"), UI->ModePages->GetScrollOffset() > 1.f);
        Test->TestFalse(TEXT("Completed member button has no pending press"), UI->MemberModeButton->IsPressPending());
        Test->TestFalse(TEXT("Completed squad button has no pending press"), UI->SquadModeButton->IsPressPending());
    }

    bool IsInHitPath(UWidget* Widget, const FVector2D& Position) const
    {
        const auto Slate = Widget->TakeWidget();
        const auto Window = FSlateApplication::Get().FindWidgetWindow(Slate);
        if (!Window.IsValid()) return false;
        const auto Path = Window->GetHittestGrid().GetBubblePath(Position, 0.f, false);
        return Path.ContainsByPredicate([&](const FWidgetAndPointer& Entry) { return Entry.Widget == Slate; });
    }

    void PointerClick(UBattleHUDButton* Button)
    {
        const auto& Geometry = Button->GetCachedGeometry();
        const FVector2D Position(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5f));
        Test->TestTrue(TEXT("Mode button is reachable through actual Slate hit grid"), IsInHitPath(Button, Position));
        TSet<FKey> Keys; Keys.Add(EKeys::LeftMouseButton);
        const FPointerEvent Down(0, Position, Position, Keys, EKeys::LeftMouseButton, 0, FModifierKeysState());
        Test->TestTrue(TEXT("Real mode button handles mouse press"), Button->TakeWidget()->OnMouseButtonDown(Geometry, Down).IsEventHandled());
        Keys.Reset();
        const FPointerEvent Up(0, Position, Position, Keys, EKeys::LeftMouseButton, 0, FModifierKeysState());
        Test->TestTrue(TEXT("Real mode button handles mouse release"), Button->TakeWidget()->OnMouseButtonUp(Geometry, Up).IsEventHandled());
    }

    void ValidateOuterInput()
    {
        auto* Pages = UI->ModePages.Get();
        const auto Slate = Pages->TakeWidget();
        const auto& G = Pages->GetCachedGeometry();
        const FVector2D P(G.LocalToAbsolute(G.GetLocalSize() * .5f)), Moved = P + FVector2D(0, 70);
        const FModifierKeysState Modifiers;
        TSet<FKey> Keys;
        OuterOffset = Pages->GetScrollOffset();
        const FPointerEvent Scroll(0, P, P, Keys, FKey(), -3.f, Modifiers);
        Test->TestFalse(TEXT("Mode pager does not consume mouse wheel"), Slate->OnMouseWheel(G, Scroll).IsEventHandled());
        Keys.Add(EKeys::RightMouseButton);
        const FPointerEvent Down(0, P, P, Keys, EKeys::RightMouseButton, 0, Modifiers);
        const FPointerEvent Move(0, Moved, P, Keys, EKeys::RightMouseButton, 0, Modifiers);
        Test->TestFalse(TEXT("Mode pager does not capture preview input"), Slate->OnPreviewMouseButtonDown(G, Down).IsEventHandled());
        Test->TestFalse(TEXT("Mode pager does not start right-drag scrolling"), Slate->OnMouseButtonDown(G, Down).IsEventHandled());
        Test->TestFalse(TEXT("Mode pager ignores right-drag motion"), Slate->OnMouseMove(G, Move).IsEventHandled());
        Slate->OnMouseButtonUp(G, Move);
        const FPointerEvent TouchStart(0u, 1u, P, P, 1.f, true);
        const FPointerEvent TouchMove(0u, 1u, Moved, P, 1.f, true);
        const FPointerEvent TouchEnd(0u, 1u, Moved, Moved, 0.f, false);
        Test->TestFalse(TEXT("Mode pager does not capture touch start"), Slate->OnTouchStarted(G, TouchStart).IsEventHandled());
        Test->TestFalse(TEXT("Mode pager does not pan on touch move"), Slate->OnTouchMoved(G, TouchMove).IsEventHandled());
        Test->TestFalse(TEXT("Mode pager does not consume touch end"), Slate->OnTouchEnded(G, TouchEnd).IsEventHandled());
        const FAnalogInputEvent Analog(EKeys::Gamepad_RightY, Modifiers, 0u, false, 0u, 0u, 1.f);
        Test->TestFalse(TEXT("Mode pager does not scroll from analog input"), Slate->OnAnalogValueChanged(G, Analog).IsEventHandled());
        Test->TestFalse(TEXT("Mode pager cannot take keyboard focus"), Slate->SupportsKeyboardFocus());
        Slate->OnNavigation(G, FNavigationEvent(Modifiers, 0, EUINavigation::Down, ENavigationGenesis::Keyboard));
        // Deliver a focus-change notification without changing application/user
        // focus. It must not automatically reveal an inactive descendant.
        FWidgetPath Destination;
        UWidget* Inactive = UI->CurrentControlMode == EBattleControlMode::Member
            ? static_cast<UWidget*>(UI->SquadModePanel.Get()) : static_cast<UWidget*>(UI->MemberModePanel.Get());
        FSlateApplication::Get().GeneratePathToWidgetUnchecked(Inactive->TakeWidget(), Destination);
        Slate->OnFocusChanging(FWeakWidgetPath(), Destination, FFocusEvent(EFocusCause::Navigation, 0));
        Test->TestTrue(TEXT("All user input leaves the outer page offset unchanged"),
            FMath::IsNearlyEqual(Pages->GetScrollOffset(), OuterOffset, .1f));
    }

    void AddOverflowFixtures()
    {
        auto* Panel = UI->MemberModePanel.Get();
        OriginalMemberOffset = Panel->MemberScrollBox->GetScrollOffset();
        OriginalSquadOffset = Panel->SquadScrollBox->GetScrollOffset();
        MemberOverflow = NewObject<USizeBox>(Panel);
        MemberOverflow->SetWidthOverride(1800.f); MemberOverflow->SetHeightOverride(100.f);
        Panel->MemberList->AddChild(MemberOverflow);
        SquadOverflow = NewObject<USizeBox>(Panel);
        SquadOverflow->SetHeightOverride(600.f); SquadOverflow->SetWidthOverride(50.f);
        Panel->SquadList->AddChild(SquadOverflow);
        Panel->MemberScrollBox->SetScrollOffset(0.f);
        Panel->SquadScrollBox->SetScrollOffset(0.f);
    }

    void Wheel(UScrollBox* Scroll, float Delta)
    {
        const auto& G = Scroll->GetCachedGeometry();
        const FVector2D P(G.LocalToAbsolute(G.GetLocalSize() * .5f));
        const TSet<FKey> Keys;
        const FPointerEvent Event(0, P, P, Keys, FKey(), Delta, FModifierKeysState());
        Test->TestTrue(TEXT("Nested scrollbox handles its own wheel input"), Scroll->TakeWidget()->OnMouseWheel(G, Event).IsEventHandled());
    }

    void ValidateInnerOffsets()
    {
        Test->TestTrue(TEXT("Switching mode preserves nested member list position"),
            FMath::IsNearlyEqual(UI->MemberModePanel->MemberScrollBox->GetScrollOffset(), MemberOffset, .5f));
        Test->TestTrue(TEXT("Switching mode preserves nested squad list position"),
            FMath::IsNearlyEqual(UI->MemberModePanel->SquadScrollBox->GetScrollOffset(), SquadOffset, .5f));
    }

    void RemoveOverflowFixtures()
    {
        if (MemberOverflow) MemberOverflow->RemoveFromParent();
        if (SquadOverflow) SquadOverflow->RemoveFromParent();
        MemberOverflow = nullptr; SquadOverflow = nullptr;
        UI->MemberModePanel->MemberScrollBox->SetScrollOffset(OriginalMemberOffset);
        UI->MemberModePanel->SquadScrollBox->SetScrollOffset(OriginalSquadOffset);
    }

    void ValidateFixturesPreserved()
    {
        Test->TestTrue(TEXT("Switching/refreshing keeps the original six member instances and their order"),
            UI->MemberModePanel->MemberList->GetAllChildren() == OriginalMembers);
        Test->TestTrue(TEXT("Member squad rows are never cleared or regenerated"),
            UI->MemberModePanel->SquadList->GetAllChildren() == OriginalMemberSquads);
        Test->TestTrue(TEXT("Squad mode squad rows are never cleared or regenerated"),
            UI->SquadModePanel->SquadList->GetAllChildren() == OriginalSquadSquads);
        Test->TestTrue(TEXT("Squad card container contents are preserved"),
            UI->SquadModePanel->SquadCardList->GetAllChildren() == OriginalSquadCards);
    }

    void ValidateInventoryHitPath()
    {
        auto* Card = UI->MemberModePanel->MemberList->GetChildrenCount() > 0
            ? Cast<UBattlePersonnelCardWidget>(UI->MemberModePanel->MemberList->GetChildAt(0)) : nullptr;
        if (!Test->TestNotNull(TEXT("First authored member is the real personnel card"), Card)) return;
        auto* Mirror = Card->HandEquipment.Get();
        if (!Test->TestNotNull(TEXT("Personnel card retains original inventory mirror"), Mirror)) return;
        Test->TestTrue(TEXT("Nested inventory still accepts drag/drop"), Mirror->bAcceptDraggedItems);
        int32 HitSlots = 0;
        for (auto* Container : Mirror->SlotContainerList)
            if (Container) for (const auto& Pair : Container->SlotMap)
                if (Pair.Value)
                {
                    const auto& G = Pair.Value->GetCachedGeometry();
                    if (IsInHitPath(Pair.Value, FVector2D(G.LocalToAbsolute(G.GetLocalSize() * .5f)))) ++HitSlots;
                }
        Test->TestTrue(TEXT("Inventory slot remains in actual hit grid through nested mode/list containers"), HitSlots > 0);
    }

    void Capture(const TCHAR* Name)
    {
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Screenshots/BattleModes");
        IFileManager::Get().MakeDirectory(*Directory, true);
        ScreenshotPath = Directory / Name;
        PreviousScreenshotTime = IFileManager::Get().GetTimeStamp(*ScreenshotPath);
        FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
    }
    bool CaptureFinished() const
    {
        return !FScreenshotRequest::IsScreenshotRequested()
            && IFileManager::Get().FileSize(*ScreenshotPath) > 0
            && IFileManager::Get().GetTimeStamp(*ScreenshotPath) != PreviousScreenshotTime;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattleControlModesRuntimeTest,
    "SilverChoir.BattleUI.ControlModes.RuntimeFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBattleControlModesRuntimeTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(BattleControlModeTest::FRuntime(this));
    return true;
}
#endif
