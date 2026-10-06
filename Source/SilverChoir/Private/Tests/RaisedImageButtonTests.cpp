#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RaisedImageButtonTestObserver.h"
#include "UIBasic/RaisedImageButtonWidget.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWindow.h"

namespace RaisedImageButtonTest
{
static APlayerController* FindPlayer()
{
    if (!GEngine || !GEngine->GameViewport || !FSlateApplication::IsInitialized()) return nullptr;
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game && Context.World())
            if (auto* Player = Context.World()->GetFirstPlayerController()) return Player;
    return nullptr;
}

/** A temporary viewport layer; no existing HUD instances, input modes, or assets are modified. */
class FFixture
{
public:
    TStrongObjectPtr<UCanvasPanel> Root;
    TStrongObjectPtr<UTexture2D> Up, Down;
    TArray<TStrongObjectPtr<URaisedImageButtonWidget>> Buttons;
    TArray<int32> States;
    TArray<float> Sizes;
    TSharedPtr<SWidget> Preview;

    ~FFixture() { Cleanup(); }

    bool Create(FAutomationTestBase* Test, APlayerController* Player, bool bGallery)
    {
        UClass* Class = LoadClass<URaisedImageButtonWidget>(nullptr,
            TEXT("/Game/System/UIBasic/WBP_RaisedImageButton.WBP_RaisedImageButton_C"));
        Up.Reset(LoadObject<UTexture2D>(nullptr, TEXT("/Game/System/UIBasic/Textures/T_ArrowUp.T_ArrowUp")));
        Down.Reset(LoadObject<UTexture2D>(nullptr, TEXT("/Game/System/UIBasic/Textures/T_ArrowDown.T_ArrowDown")));
        if (!Test->TestNotNull(TEXT("Authored raised-image Blueprint loads"), Class)
            || !Test->TestNotNull(TEXT("Up-arrow texture loads"), Up.Get())
            || !Test->TestNotNull(TEXT("Down-arrow texture loads"), Down.Get())) return false;
        Root.Reset(NewObject<UCanvasPanel>(Player));
        Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        UCanvasPanel* Board = Root.Get();
        if (bGallery)
        {
            auto* Background = NewObject<UBorder>(Root.Get());
            Background->SetBrushColor(FLinearColor(FColor::FromHex(TEXT("09131EFF"))));
            Background->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto* BackgroundSlot = Root->AddChildToCanvas(Background);
            BackgroundSlot->SetAnchors(FAnchors(0, 0, 1, 1));
            BackgroundSlot->SetOffsets(FMargin(0));
            Board = NewObject<UCanvasPanel>(Root.Get());
            Board->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            auto* BoardSlot = Root->AddChildToCanvas(Board);
            BoardSlot->SetAnchors(FAnchors(.5f, .5f));
            BoardSlot->SetAlignment(FVector2D(.5, .5));
            BoardSlot->SetPosition(FVector2D::ZeroVector);
            BoardSlot->SetSize(FVector2D(1280, 650));
            AddText(Board, TEXT("SILVER CHOIR  /  INTERFACE STUDY"), 56, 30, 12, FColor(58, 190, 214));
            AddText(Board, TEXT("Digital image button"), 56, 65, 32, FColor(224, 237, 244));
            AddText(Board, TEXT("Four corners, an upward scan, and a persistent selection marker."), 57, 113, 15, FColor(140, 166, 184));
            const TCHAR* Labels[] = {TEXT("NORMAL"), TEXT("HOVER"), TEXT("PRESSED"), TEXT("SELECTED"), TEXT("SELECTED + PRESS"), TEXT("DISABLED")};
            for (int32 Column = 0; Column < 6; ++Column)
                AddText(Board, Labels[Column], 220 + Column * 170, 170, 12, FColor(167, 195, 210));
            AddText(Board, TEXT("44 px"), 57, 239, 21, FColor(216, 229, 237));
            AddText(Board, TEXT("Compact"), 58, 270, 12, FColor(110, 139, 158));
            AddText(Board, TEXT("56 px"), 57, 358, 21, FColor(216, 229, 237));
            AddText(Board, TEXT("Comfortable"), 58, 389, 12, FColor(110, 139, 158));
            AddText(Board, TEXT("96 px"), 57, 505, 21, FColor(216, 229, 237));
            AddText(Board, TEXT("Detail view"), 58, 536, 12, FColor(110, 139, 158));
            AddText(Board, TEXT("44 px fixed hit area  /  Tintable white icons  /  Immediate sound event  /  Press feedback after two frames"),
                57, 618, 12, FColor(104, 139, 161));
        }
        for (int32 Row = 0; Row < (bGallery ? 3 : 1); ++Row)
        {
            const float Size = Row == 0 ? 44.f : Row == 1 ? 56.f : 96.f;
            for (int32 State = 0; State < (bGallery ? 6 : 1); ++State)
                for (int32 Direction = 0; Direction < (bGallery && Row < 2 ? 2 : 1); ++Direction)
                {
                    auto* Button = CreateWidget<URaisedImageButtonWidget>(Player, Class);
                    if (!Test->TestNotNull(TEXT("Real Blueprint button instance"), Button)) return false;
                    Buttons.Emplace(Button); States.Add(State); Sizes.Add(Size);
                    Button->MinimumSize = FVector2D(Size, Size);
                    Button->IconSize = Row == 2 ? FVector2D(42, 42) : Row == 1 ? FVector2D(24, 24) : FVector2D(20, 20);
                    Button->SetButtonImage(Direction == 0 ? Up.Get() : Down.Get());
                    Button->PressSound = nullptr; Button->HoverSound = nullptr;
                    auto* Slot = Board->AddChildToCanvas(Button);
                    Slot->SetAutoSize(true);
                    Slot->SetPosition(bGallery
                        ? FVector2D(220 + State * 170 + (Row == 2 ? 16 : Direction * (Size + 14)), Row == 0 ? 232 : Row == 1 ? 344 : 483)
                        : FVector2D(200, 200));
                }
        }
        Mount();
        return true;
    }

    void ApplyGalleryStates()
    {
        for (int32 Index = 0; Index < Buttons.Num(); ++Index)
        {
            auto* Button = Buttons[Index].Get();
            const auto Slate = Button->TakeWidget();
            const auto& Geometry = Button->GetCachedGeometry();
            if (States[Index] == 1)
            {
                const FVector2D Position(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5f));
                Slate->OnMouseEnter(Geometry, FPointerEvent(0, Position, Position, TSet<FKey>(), FKey(), 0, FModifierKeysState()));
            }
            if (States[Index] == 3 || States[Index] == 4) Button->SetSelected(true);
            if (States[Index] == 2 || States[Index] == 4)
                Slate->OnKeyDown(Geometry, FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
            if (States[Index] == 5) Button->SetIsEnabled(false);
        }
    }

    void Mount()
    {
        Preview = Root->TakeWidget();
        GEngine->GameViewport->AddViewportWidgetContent(Preview.ToSharedRef(), 1000);
    }

    void Cleanup()
    {
        for (auto& Button : Buttons) if (Button.IsValid()) Button->CancelPendingClick();
        if (Preview.IsValid() && GEngine && GEngine->GameViewport)
            GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
        Preview.Reset();
        if (Root.IsValid()) { Root->ReleaseSlateResources(true); Root->ClearChildren(); }
        Buttons.Reset(); Root.Reset(); Up.Reset(); Down.Reset();
    }

private:
    static void AddText(UCanvasPanel* Canvas, const TCHAR* Text, float X, float Y, int32 Size, FColor Color)
    {
        auto* Label = NewObject<UTextBlock>(Canvas);
        Label->SetText(FText::FromString(Text));
        Label->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size));
        Label->SetColorAndOpacity(FLinearColor(Color));
        Label->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Slot = Canvas->AddChildToCanvas(Label);
        Slot->SetAutoSize(true); Slot->SetPosition(FVector2D(X, Y));
    }
};

class FInput final : public IAutomationLatentCommand
{
public:
    explicit FInput(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual ~FInput() override { Cleanup(); }
    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 25.)
        {
            Test->AddError(FString::Printf(TEXT("Raised image button input test timed out at stage %d"), Stage));
            return Finish();
        }
        if (Stage == 0)
        {
            auto* Player = FindPlayer();
            if (!Player) return false;
            if (!Fixture.Create(Test, Player, false)) return Finish();
            Button = Fixture.Buttons[0].Get();
            Observer.Reset(NewObject<URaisedImageButtonTestObserver>());
            Observer->Button = Button;
            Button->OnClicked.AddDynamic(Observer.Get(), &URaisedImageButtonTestObserver::RecordClick);
            Button->OnPressStarted.AddDynamic(Observer.Get(), &URaisedImageButtonTestObserver::RecordPressStart);
            PreviousFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
            bSavedFocus = true;
            Advance(); return false;
        }
        if (bTimingPress && Button->IsButtonVisuallyPressed())
            Test->TestTrue(TEXT("Pressed feedback never precedes two engine frames"), GFrameCounter >= PressFrame + 2);
        if (bTimingPress && GFrameCounter < PressFrame + 2 && IconBox)
            Test->TestTrue(TEXT("The icon remains full size during the two-frame sound lead-in"),
                IconBox->GetRenderTransform().Scale.Equals(FVector2D(1, 1), .01));
        if (FPlatformTime::Seconds() < ReadyAt) return false;
        switch (Stage)
        {
        case 1:
            Image = Cast<UImage>(Button->GetWidgetFromName(TEXT("IconImage")));
            IconBox = Cast<USizeBox>(Button->GetWidgetFromName(TEXT("IconSizeBox")));
            Face = Button->GetWidgetFromName(TEXT("FaceContent"));
            if (!Test->TestNotNull(TEXT("Authored IconImage binding"), Image)
                || !Test->TestNotNull(TEXT("Authored IconSizeBox binding"), IconBox)
                || !Test->TestNotNull(TEXT("Authored FaceContent binding"), Face)) return Finish();
            Bounds = FVector2D(Button->GetCachedGeometry().GetLocalSize());
            Desired = Button->GetDesiredSize();
            RestIconCenter = IconCenter();
            Test->TestTrue(TEXT("Compact Blueprint has a 44 by 44 layout"), Bounds.Equals(FVector2D(44, 44), .1));
            ValidateGeometry();
            Test->TestTrue(TEXT("Default texture reaches the image"), Image->GetBrush().GetResourceObject() == Fixture.Up.Get());
            Button->SetButtonImage(Fixture.Down.Get());
            Test->TestTrue(TEXT("SetButtonImage swaps the actual UImage resource"), Image->GetBrush().GetResourceObject() == Fixture.Down.Get());
            WideTexture.Reset(UTexture2D::CreateTransient(80, 40));
            Button->SetButtonImage(WideTexture.Get());
            Advance(); break;
        case 2:
        {
            const FVector2D IconBounds(IconBox->GetCachedGeometry().GetLocalSize());
            Test->TestTrue(TEXT("Non-square texture keeps 2:1 aspect instead of stretching"),
                IconBounds.Y > 0 && FMath::IsNearlyEqual(IconBounds.X / IconBounds.Y, 2., .01));
            const FVector2D ImageBounds(Image->GetCachedGeometry().GetLocalSize());
            Test->TestTrue(TEXT("The rendered image itself retains the texture aspect"),
                ImageBounds.Y > 0 && FMath::IsNearlyEqual(ImageBounds.X / ImageBounds.Y, 2., .01));
            Test->TestTrue(TEXT("Fitted icon stays inside its configured extent"),
                IconBounds.X <= Button->IconSize.X + .1 && IconBounds.Y <= Button->IconSize.Y + .1);
            ValidateGeometry();
            Button->SetButtonImage(nullptr);
            Test->TestNull(TEXT("Null image clears the resource immediately"), Image->GetBrush().GetResourceObject());
            Test->TestTrue(TEXT("Null image draws no white placeholder quad"), Image->GetBrush().DrawAs == ESlateBrushDrawType::NoDrawType);
            Advance(); break;
        }
        case 3:
            ValidateGeometry();
            Button->SetButtonImage(Fixture.Down.Get());
            FSlateApplication::Get().SetKeyboardFocus(Button->TakeWidget(), EFocusCause::SetDirectly);
            Test->TestTrue(TEXT("Actual Slate button owns keyboard focus"), Button->HasKeyboardFocus());
            Press(); Advance(); break;
        case 4:
            Test->TestTrue(TEXT("Holding Enter reaches the pressed visual"), Button->IsButtonVisuallyPressed());
            Test->TestEqual(TEXT("Holding does not click"), Observer->Clicks, 0);
            Test->TestTrue(TEXT("Press squeezes the rendered icon without changing layout"),
                IconBox->GetRenderTransform().Scale.Equals(FVector2D(Button->PressedIconScale), .01));
            Test->TestTrue(TEXT("Flat frame keeps its content centered while pressed"),
                Face->GetRenderTransform().Translation.IsNearlyZero(.01) && IconCenter().Equals(RestIconCenter, .1));
            ValidateGeometry();
            FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, true, 0, 0));
            Test->TestEqual(TEXT("Keyboard repeat never replays the press-start sound event"), Observer->PressStarts, ExpectedPressStarts);
            Release(); Advance(); break;
        case 5:
            Test->TestEqual(TEXT("Hold and release emit exactly one public click"), Observer->Clicks, 1);
            ValidateRest();
            Press(); Release();
            Test->TestEqual(TEXT("Quick release still waits for rendered feedback"), Observer->Clicks, 1);
            Advance(); break;
        case 6:
            Test->TestEqual(TEXT("Quick tap also emits exactly one click"), Observer->Clicks, 2);
            ValidateRest(); Press(); Advance(); break;
        case 7:
            Test->TestTrue(TEXT("Disable fixture starts from a visibly held button"), Button->IsButtonVisuallyPressed());
            Button->SetIsEnabled(false);
            Test->TestFalse(TEXT("Disabling immediately cancels pending input"), Button->IsPressPending());
            Release(); Advance(); break;
        case 8:
            Test->TestEqual(TEXT("Release after disable cannot click"), Observer->Clicks, 2);
            ValidateRest(); Button->SetIsEnabled(true);
            FSlateApplication::Get().SetKeyboardFocus(Button->TakeWidget(), EFocusCause::SetDirectly);
            Press(); Advance(); break;
        case 9:
            FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
            FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
            Release(); Advance(); break;
        case 10:
            Test->TestEqual(TEXT("Escape cancels a held click"), Observer->Clicks, 2);
            ValidateRest(); Press(); Advance(); break;
        case 11:
            FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
            Release(); Advance(); break;
        case 12:
            Test->TestEqual(TEXT("Focus loss cancels without a late click"), Observer->Clicks, 2);
            ValidateRest();
            Button->SetSelected(true);
            FSlateApplication::Get().SetKeyboardFocus(Button->TakeWidget(), EFocusCause::SetDirectly);
            Press(); Advance(); break;
        case 13:
            Test->TestTrue(TEXT("A selected button still shows delayed press feedback"), Button->IsButtonVisuallyPressed());
            Test->TestTrue(TEXT("Press does not clear the persistent selection marker"), Button->IsButtonSelected());
            Test->TestTrue(TEXT("Transient pressed tint overrides selection while held"), Image->GetColorAndOpacity().Equals(Button->PressedIconColor, .01f));
            Test->TestTrue(TEXT("Selected press uses the same centered icon squeeze"),
                IconBox->GetRenderTransform().Scale.Equals(FVector2D(Button->PressedIconScale), .01));
            ValidateGeometry(); Release(); Advance(); break;
        case 14:
            Test->TestEqual(TEXT("Selected press emits one normal click"), Observer->Clicks, 3);
            Test->TestTrue(TEXT("Selection persists after release until the owner changes it"), Button->IsButtonSelected());
            ValidateRest();
            FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
            // Distinct, saturated tints catch accidental retention of the source texture's old blue tint.
            Button->SetIconTintColors(FLinearColor(.12f, .35f, .8f), FLinearColor(.8f, .4f, .1f),
                FLinearColor(.2f, .8f, .3f), FLinearColor(.95f, .9f, .85f));
            Advance(); break;
        case 15:
            Test->TestTrue(TEXT("Selected icon uses its separately configured tint"), Image->GetColorAndOpacity().Equals(Button->SelectedIconColor, .01f));
            Button->SetSelected(false);
            Advance(); break;
        case 16:
            Test->TestFalse(TEXT("Owner can explicitly clear selection"), Button->IsButtonSelected());
            Test->TestTrue(TEXT("Normal icon returns to its configured tint"), Image->GetColorAndOpacity().Equals(Button->NormalIconColor, .01f));
            Hover(true);
            Advance(); break;
        case 17:
            Test->TestTrue(TEXT("Pointer hover activates the upward scan"), Button->bScanVisible);
            Test->TestTrue(TEXT("Hover scan starts advancing independently of selection"), Button->HoverScanPhase > 0.f);
            Test->TestTrue(TEXT("Hovered image uses the configured hover tint"), Image->GetColorAndOpacity().Equals(Button->HoverIconColor, .01f));
            Test->TestFalse(TEXT("Hover alone does not select the button"), Button->IsButtonSelected());
            PreviousScanPhase = Button->HoverScanPhase;
            ValidateGeometry(); Advance(); break;
        case 18:
            Test->TestTrue(TEXT("Scan travels onward on subsequent frames"), Button->HoverScanPhase > PreviousScanPhase);
            Hover(false);
            Test->TestFalse(TEXT("Leaving immediately hides the scan"), Button->bScanVisible);
            Test->TestTrue(TEXT("Leaving resets the scan for the next hover"), FMath::IsNearlyZero(Button->HoverScanPhase));
            Advance(); break;
        case 19:
            Test->TestFalse(TEXT("The scan remains stopped after the pointer leaves"), Button->bScanVisible);
            Hover(true);
            Advance(); break;
        case 20:
            Test->TestTrue(TEXT("Re-entering restarts hover scan"), Button->bScanVisible);
            Button->SetIsEnabled(false);
            Test->TestFalse(TEXT("Disabling an actively hovered button hides the scan immediately"), Button->bScanVisible);
            Test->TestTrue(TEXT("Disable clears the scan phase"), FMath::IsNearlyZero(Button->HoverScanPhase));
            Advance(); break;
        case 21:
            Test->TestFalse(TEXT("Disabled hover cannot resume the scan"), Button->bScanVisible);
            Test->TestEqual(TEXT("Hover and disabled states never synthesize a click"), Observer->Clicks, 3);
            ValidateRest();
            if (!Test->HasAnyErrors()) Test->AddInfo(TEXT("RAISED_IMAGE_BUTTON_INPUT_OK actual Blueprint and Slate input; immediate press-start event before two-frame feedback; texture swap/null/aspect/tints; press squeeze and selected press; hover scan progression/reset; disable, Escape and focus cancellation; stable 44px layout and hit area"));
            return Finish();
        default: return Finish();
        }
        return false;
    }

private:
    FAutomationTestBase* Test;
    FFixture Fixture;
    URaisedImageButtonWidget* Button = nullptr;
    UImage* Image = nullptr;
    USizeBox* IconBox = nullptr;
    UWidget* Face = nullptr;
    TStrongObjectPtr<URaisedImageButtonTestObserver> Observer;
    TStrongObjectPtr<UTexture2D> WideTexture;
    TSharedPtr<SWidget> PreviousFocus;
    FVector2D Bounds, Desired;
    FVector2D RestIconCenter;
    float PreviousScanPhase = 0.f;
    int32 Stage = 0;
    int32 ExpectedPressStarts = 0;
    uint64 PressFrame = 0;
    bool bSavedFocus = false, bTimingPress = false, bHeld = false;
    double Started = FPlatformTime::Seconds(), ReadyAt = 0.;

    void Advance() { ++Stage; ReadyAt = FPlatformTime::Seconds() + .18; }
    void Press()
    {
        bTimingPress = true; bHeld = true; PressFrame = GFrameCounter;
        Test->TestTrue(TEXT("Slate handles Enter down"),
            FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0)));
        Test->TestTrue(TEXT("Keyboard down begins a pending press"), Button->IsPressPending());
        Test->TestFalse(TEXT("The initial input frame is not visually pressed"), Button->IsButtonVisuallyPressed());
        Test->TestEqual(TEXT("Press-start sound hook fires once before input handling returns"), Observer->PressStarts, ++ExpectedPressStarts);
        Test->TestEqual(TEXT("Press-start sound hook runs on the input frame"), Observer->LastPressStartFrame, PressFrame);
        Test->TestFalse(TEXT("Press-start sound hook precedes visible feedback"), Observer->bPressedVisualAtStart);
    }
    void Release()
    {
        bHeld = false;
        FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
    }
    void ValidateGeometry()
    {
        Test->TestTrue(TEXT("Input and image changes preserve desired size"), Button->GetDesiredSize().Equals(Desired, .1));
        Test->TestTrue(TEXT("Input and image changes preserve outer geometry"),
            FVector2D(Button->GetCachedGeometry().GetLocalSize()).Equals(Bounds, .1));
        const auto Slate = Button->TakeWidget();
        const auto Window = FSlateApplication::Get().FindWidgetWindow(Slate);
        if (!Test->TestTrue(TEXT("Button belongs to a real Slate window"), Window.IsValid())) return;
        const auto& Geometry = Button->GetCachedGeometry();
        for (const FVector2D Local : {FVector2D(1, 1), Bounds * .5, Bounds - FVector2D(1, 1)})
        {
            const auto Path = Window->GetHittestGrid().GetBubblePath(Geometry.LocalToAbsolute(Local), 0.f, true);
            Test->TestTrue(TEXT("Entire fixed hit area remains reachable"),
                Path.ContainsByPredicate([&](const FWidgetAndPointer& Entry) { return Entry.Widget == Slate; }));
        }
        for (const FVector2D Local : {FVector2D(-1, Bounds.Y * .5), FVector2D(Bounds.X + 1, Bounds.Y * .5)})
        {
            const auto Path = Window->GetHittestGrid().GetBubblePath(Geometry.LocalToAbsolute(Local), 0.f, true);
            Test->TestFalse(TEXT("Digital painting does not expand the hit area"),
                Path.ContainsByPredicate([&](const FWidgetAndPointer& Entry) { return Entry.Widget == Slate; }));
        }
    }
    FVector2D IconCenter() const
    {
        const auto& Geometry = Image->GetCachedGeometry();
        return Button->GetCachedGeometry().AbsoluteToLocal(
            Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5f));
    }
    void Hover(bool bEnter)
    {
        const auto& Geometry = Button->GetCachedGeometry();
        const FVector2D Position(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5f));
        const FPointerEvent Event(0, Position, Position, TSet<FKey>(), FKey(), 0, FModifierKeysState());
        if (bEnter) Button->TakeWidget()->OnMouseEnter(Geometry, Event);
        else Button->TakeWidget()->OnMouseLeave(Event);
    }
    void ValidateRest()
    {
        bTimingPress = false;
        Test->TestFalse(TEXT("Settled button has no pending press"), Button->IsPressPending());
        Test->TestFalse(TEXT("Settled button has no pressed visual"), Button->IsButtonVisuallyPressed());
        Test->TestTrue(TEXT("Flat content never retains a translated physical face"), Face->GetRenderTransform().Translation.IsNearlyZero(.01));
        Test->TestTrue(TEXT("Icon returns to full scale after release or cancellation"), IconBox->GetRenderTransform().Scale.Equals(FVector2D(1, 1), .01));
        ValidateGeometry();
    }
    bool Finish() { Cleanup(); return true; }
    void Cleanup()
    {
        if (Button) Button->CancelPendingClick();
        if (FSlateApplication::IsInitialized() && bSavedFocus)
        {
            if (bHeld) Release();
            if (PreviousFocus.IsValid()) FSlateApplication::Get().SetKeyboardFocus(PreviousFocus, EFocusCause::SetDirectly);
            else FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::SetDirectly);
        }
        bSavedFocus = false; PreviousFocus.Reset();
        if (Button && Observer.IsValid()) Button->OnClicked.RemoveDynamic(Observer.Get(), &URaisedImageButtonTestObserver::RecordClick);
        if (Button && Observer.IsValid()) Button->OnPressStarted.RemoveDynamic(Observer.Get(), &URaisedImageButtonTestObserver::RecordPressStart);
        Button = nullptr; Image = nullptr; IconBox = nullptr; Face = nullptr;
        Observer.Reset(); WideTexture.Reset(); Fixture.Cleanup();
    }
};

class FStates final : public IAutomationLatentCommand
{
public:
    explicit FStates(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 25.)
        { Test->AddError(TEXT("Raised image button state screenshot timed out")); Fixture.Cleanup(); return true; }
        if (Stage == 0)
        {
            auto* Player = FindPlayer();
            if (!Player) return false;
            if (!Fixture.Create(Test, Player, true)) return true;
            Advance(); return false;
        }
        if (FPlatformTime::Seconds() < ReadyAt) return false;
        if (Stage == 1) { Fixture.ApplyGalleryStates(); Advance(); return false; }
        if (Stage == 2)
        {
            for (int32 Index = 0; Index < Fixture.Buttons.Num(); ++Index)
            {
                auto* Button = Fixture.Buttons[Index].Get();
                const FVector2D Expected(Fixture.Sizes[Index], Fixture.Sizes[Index]);
                Test->TestTrue(TEXT("Gallery uses actual 44, 56 and 96 pixel button geometry"),
                    FVector2D(Button->GetCachedGeometry().GetLocalSize()).Equals(Expected, .1));
                Test->TestEqual(TEXT("Pressed and selected-pressed columns have active feedback"),
                    Button->IsButtonVisuallyPressed(), Fixture.States[Index] == 2 || Fixture.States[Index] == 4);
                Test->TestEqual(TEXT("Only selected columns retain the persistent marker"),
                    Button->IsButtonSelected(), Fixture.States[Index] == 3 || Fixture.States[Index] == 4);
            }
            RequestScreenshot(TEXT("States.png"));
            Advance(); return false;
        }
        if (Stage == 3)
        {
            if (!ScreenshotReady()) return false;
            Test->AddInfo(FString::Printf(TEXT("Actual Blueprint state preview: %s"), *Screenshot));
            for (int32 Index = 0; Index < Fixture.Buttons.Num(); ++Index)
                if (Fixture.States[Index] == 1)
                    FirstScanPhases.Add(Fixture.Buttons[Index]->HoverScanPhase);
            Advance(); return false;
        }
        if (Stage == 4)
        {
            int32 HoverIndex = 0;
            for (int32 Index = 0; Index < Fixture.Buttons.Num(); ++Index)
                if (Fixture.States[Index] == 1)
                {
                    Test->TestTrue(TEXT("Hover remains animated between visual captures"), Fixture.Buttons[Index]->bScanVisible);
                    Test->TestFalse(TEXT("Hover scan moved between visual captures"),
                        FMath::IsNearlyEqual(Fixture.Buttons[Index]->HoverScanPhase, FirstScanPhases[HoverIndex++], .01f));
                }
            RequestScreenshot(TEXT("StatesScanLater.png"));
            Advance(); return false;
        }
        if (!ScreenshotReady()) return false;
        Test->AddInfo(FString::Printf(TEXT("RAISED_IMAGE_BUTTON_STATES_OK two real Blueprint captures with progressing hover scan, selected press and 44/56/96px sizes: %s"), *Screenshot));
        Fixture.Cleanup(); return true;
    }
private:
    FAutomationTestBase* Test;
    FFixture Fixture;
    int32 Stage = 0;
    double Started = FPlatformTime::Seconds(), ReadyAt = 0.;
    FString Screenshot;
    FDateTime PreviousTime;
    TArray<float> FirstScanPhases;
    void Advance() { ++Stage; ReadyAt = FPlatformTime::Seconds() + .4; }
    void RequestScreenshot(const TCHAR* Name)
    {
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Screenshots/RaisedImageButton");
        IFileManager::Get().MakeDirectory(*Directory, true);
        Screenshot = Directory / Name;
        PreviousTime = IFileManager::Get().GetTimeStamp(*Screenshot);
        FScreenshotRequest::RequestScreenshot(Screenshot, true, false);
    }
    bool ScreenshotReady() const
    {
        return !FScreenshotRequest::IsScreenshotRequested() && IFileManager::Get().FileSize(*Screenshot) > 0
            && IFileManager::Get().GetTimeStamp(*Screenshot) != PreviousTime;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaisedImageButtonInputTest, "SilverChoir.UI.RaisedImageButton.RuntimeInput",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRaisedImageButtonInputTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(RaisedImageButtonTest::FInput(this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaisedImageButtonStatesTest, "SilverChoir.UI.RaisedImageButton.StatePreview",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FRaisedImageButtonStatesTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(RaisedImageButtonTest::FStates(this));
    return true;
}
#endif
