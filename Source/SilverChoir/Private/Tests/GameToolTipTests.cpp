#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "UIBasic/BasicButtonWidget.h"
#include "UIBasic/SSilverChoirToolTip.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace GameToolTipTest
{
TSharedPtr<SWidget> FindFrame(const TSharedRef<SWidget>& Widget)
{
    if (Widget->GetType() == FName(TEXT("SBasicButtonFrame"))) return Widget;
    FChildren* Children = Widget->GetChildren();
    for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
        if (const auto Frame = FindFrame(Children->GetChildAt(Index))) return Frame;
    return nullptr;
}

class FStationaryDelay final : public IAutomationLatentCommand
{
public:
    explicit FStationaryDelay(FAutomationTestBase* InTest) : Test(InTest) {}

    virtual bool Update() override
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 45.)
        {
            Test->AddError(FString::Printf(TEXT("Stationary tooltip test timed out at phase %d"), Phase));
            return true;
        }
        if (Phase == 0)
        {
            if (!GEngine || !FSlateApplication::IsInitialized()) return false;
            APlayerController* PC = nullptr;
            for (const auto& Context : GEngine->GetWorldContexts())
                if (Context.WorldType == EWorldType::Game && Context.World())
                    PC = Context.World()->GetFirstPlayerController();
            if (!PC) return false;
            if (!CreateFixture(PC)) return true;
            Phase = 1;
            return false;
        }

        const FVector2D Position = FSlateApplication::Get().GetCursorPos();
        if (!Position.Equals(ObservedPosition, .01f))
        {
            // Real user/OS movement legitimately restarts both production timers.
            ObservedPosition = Position;
            PhaseStarted = DisabledStarted = Now;
        }
        Frame->OnMouseMove(FGeometry(), Pointer(Position, Position));

        // Disabled widgets receive no mouse enter/leave events. Closing their
        // pending tooltip must end the old hover even at the same screen point.
        if (Phase == 1 && !bClosedDisabledPending && Now - DisabledStarted >= .4)
        {
            DisabledTip->OnClosed();
            DisabledTip.Reset();
            DisabledStarted = FPlatformTime::Seconds();
            bClosedDisabledPending = true;
        }

        if (!Poll(Frame, Tip) || !Poll(DisabledFrame, DisabledTip)) return true;
        const double EnabledElapsed = Now - PhaseStarted;
        const double DisabledElapsed = Now - DisabledStarted;
        if ((EnabledElapsed < 1.4 && !IsWaiting(Tip))
            || (DisabledElapsed < 1.4 && !IsWaiting(DisabledTip)))
        {
            Test->AddError(FString::Printf(TEXT("Tooltip escaped its 1.5 second wait in phase %d"), Phase));
            return true;
        }
        // Avoid testing a boundary on the same frame the real-time deadline passes.
        if (EnabledElapsed < 1.6 || DisabledElapsed < 1.6) return false;
        if (!CheckTip(Frame, Tip, false, TEXT("Stationary enabled button"))
            || !CheckTip(DisabledFrame, DisabledTip, false, TEXT("Stationary disabled button"))) return true;

        if (Phase == 3)
        {
            Test->AddInfo(TEXT("GAME_TOOLTIP_STATIONARY_OK default 1.5 second wait, movement, re-entry and disabled tooltip closure verified without moving the OS cursor"));
            return true;
        }
        if (Phase == 1)
        {
            // Two real event positions catch motion that ends at its starting
            // point between tooltip queries, without warping the actual cursor.
            const FVector2D Away = Position + FVector2D(8., 0.);
            Frame->OnMouseMove(FGeometry(), Pointer(Away, Position));
            Frame->OnMouseMove(FGeometry(), Pointer(Position, Away));
        }
        else
        {
            Frame->OnMouseLeave(Pointer(Position, Position));
            Tip->OnClosed();
            Tip.Reset();
            Frame->OnMouseEnter(FGeometry(), Pointer(Position, Position));
        }
        // Simulate the disabled widget leaving the actual tooltip hit-test path.
        DisabledTip->OnClosed();
        DisabledTip.Reset();
        PhaseStarted = DisabledStarted = FPlatformTime::Seconds();
        ++Phase;
        if (!Poll(Frame, Tip) || !Poll(DisabledFrame, DisabledTip)) return true;
        return !CheckTip(Frame, Tip, true, TEXT("Enabled button restarts the full wait"))
            || !CheckTip(DisabledFrame, DisabledTip, true, TEXT("Disabled button restarts after close"));
    }

private:
    FAutomationTestBase* Test;
    TStrongObjectPtr<UBasicButtonWidget> Owner, DisabledOwner;
    TSharedPtr<SWidget> Slate, DisabledSlate, Frame, DisabledFrame;
    TSharedPtr<IToolTip> Tip, DisabledTip;
    FVector2D ObservedPosition = FVector2D::ZeroVector;
    double Started = FPlatformTime::Seconds(), PhaseStarted = Started, DisabledStarted = Started;
    int32 Phase = 0;
    bool bClosedDisabledPending = false;

    static FPointerEvent Pointer(const FVector2D& Position, const FVector2D& Previous)
    {
        return FPointerEvent(FSlateApplication::CursorPointerIndex, Position, Previous,
            TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
    }

    static bool IsWaiting(const TSharedPtr<IToolTip>& Value)
    {
        return Value && Value->AsWidget()->GetType() == FName(TEXT("SBasicButtonWaitingToolTip"));
    }

    bool Poll(const TSharedPtr<SWidget>& InFrame, TSharedPtr<IToolTip>& Current)
    {
        const auto Next = InFrame->GetToolTip();
        if (!Next)
        {
            Test->AddError(TEXT("Tooltip selection became empty and would expose the outer native tooltip"));
            return false;
        }
        if (Current != Next)
        {
            // Match Slate's order: open the new selection, then close the old.
            Next->OnOpening();
            if (Current) Current->OnClosed();
            Current = Next;
            if (InFrame->GetToolTip() != Current)
            {
                Test->AddError(TEXT("Closing the previous tooltip incorrectly reset the new selection"));
                return false;
            }
        }
        return true;
    }

    bool CheckTip(const TSharedPtr<SWidget>& InFrame, const TSharedPtr<IToolTip>& Value,
        bool bWaiting, const TCHAR* Label)
    {
        const FString Prefix(Label);
        bool bResult = Test->TestEqual(*(Prefix + TEXT(": tooltip type")), Value->AsWidget()->GetType(),
            FName(bWaiting ? TEXT("SBasicButtonWaitingToolTip") : TEXT("SSilverChoirToolTip")));
        bResult &= Test->TestFalse(*(Prefix + TEXT(": occupies the tooltip path")), Value->IsEmpty());
        bResult &= Test->TestEqual(*(Prefix + TEXT(": only waiting is swallowed by the visualizer")),
            InFrame->OnVisualizeTooltip(Value->AsWidget()), bWaiting);
        return bResult;
    }

    bool CreateFixture(APlayerController* PC)
    {
        Owner.Reset(CreateWidget<UBasicButtonWidget>(PC, UBasicButtonWidget::StaticClass()));
        DisabledOwner.Reset(CreateWidget<UBasicButtonWidget>(PC, UBasicButtonWidget::StaticClass()));
        if (!Test->TestTrue(TEXT("Native delay fixtures created"), Owner.Get() && DisabledOwner.Get())) return false;
        Test->TestEqual(TEXT("Default tooltip delay is 1.5 seconds"), Owner->ToolTipStyle.HoverDelaySeconds, 1.5f);
        Owner->SetToolTipText(FText::FromString(TEXT("Stationary enabled tooltip")));
        DisabledOwner->SetToolTipText(FText::FromString(TEXT("Stationary disabled tooltip")));
        DisabledOwner->SetIsEnabled(false);
        Slate = Owner->TakeWidget();
        DisabledSlate = DisabledOwner->TakeWidget();
        Frame = FindFrame(Slate.ToSharedRef());
        DisabledFrame = FindFrame(DisabledSlate.ToSharedRef());
        if (!Test->TestTrue(TEXT("Both native button trees expose their frame"), Frame.IsValid() && DisabledFrame.IsValid())) return false;
        ObservedPosition = FSlateApplication::Get().GetCursorPos();
        Frame->OnMouseEnter(FGeometry(), Pointer(ObservedPosition, ObservedPosition));
        // Deliberately omit disabled OnMouseEnter: Slate omits it in this case.
        PhaseStarted = DisabledStarted = FPlatformTime::Seconds();
        if (!Poll(Frame, Tip) || !Poll(DisabledFrame, DisabledTip)) return false;
        return CheckTip(Frame, Tip, true, TEXT("Initial enabled wait"))
            && CheckTip(DisabledFrame, DisabledTip, true, TEXT("Initial disabled wait"));
    }
};

class FRuntime final : public IAutomationLatentCommand
{
public:
    explicit FRuntime(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual ~FRuntime() override { Cleanup(); }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 60.)
        {
            Test->AddError(FString::Printf(TEXT("Game tooltip runtime timed out at stage %d"), Stage));
            Cleanup();
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
            Viewport = GEngine->GameViewport;
            if (!ValidateAndPreview(PC)) { Cleanup(); return true; }
            Stage = 1;
            StageStarted = FPlatformTime::Seconds();
            return false;
        }
        if (Stage == 1)
        {
            if (FPlatformTime::Seconds() - StageStarted < .65) return false;
            const FString Directory = FPaths::ProjectSavedDir() / TEXT("Screenshots/GameToolTip");
            IFileManager::Get().MakeDirectory(*Directory, true);
            ScreenshotPath = Directory / TEXT("01_GameToolTips.png");
            PreviousScreenshotTime = IFileManager::Get().GetTimeStamp(*ScreenshotPath);
            FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
            Stage = 2;
            return false;
        }
        // Do not remove the overlay until this request has written a fresh image.
        if (FScreenshotRequest::IsScreenshotRequested()
            || IFileManager::Get().FileSize(*ScreenshotPath) <= 0
            || IFileManager::Get().GetTimeStamp(*ScreenshotPath) == PreviousScreenshotTime) return false;
        Test->AddInfo(FString::Printf(TEXT("Game tooltip Slate preview: %s"), *ScreenshotPath));
        if (!Test->HasAnyErrors())
            Test->AddInfo(TEXT("GAME_TOOLTIP_OK native text updates retain cached game style; empty, cleared, custom and disabled tooltips preserve native behavior"));
        Cleanup();
        return true;
    }

private:
    FAutomationTestBase* Test;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TStrongObjectPtr<UBasicButtonWidget> ShortOwner, LongOwner;
    TStrongObjectPtr<UTextBlock> CustomContent;
    TSharedPtr<SWidget> ShortSlate, LongSlate, Preview;
    TSharedPtr<SSilverChoirToolTip> ShortTip, LongTip;
    FString ScreenshotPath;
    FDateTime PreviousScreenshotTime;
    double Started = FPlatformTime::Seconds(), StageStarted = Started;
    int32 Stage = 0;

    TSharedPtr<SSilverChoirToolTip> ReadGameTip(const TSharedPtr<SWidget>& Frame)
    {
        const auto Tip = Frame->GetToolTip();
        if (!Test->TestTrue(TEXT("Frame supplies a tooltip"), Tip.IsValid())) return nullptr;
        const auto SlateTip = Tip->AsWidget();
        if (!Test->TestEqual(TEXT("Frame supplies the actual game tooltip class"),
            SlateTip->GetType(), FName(TEXT("SSilverChoirToolTip")))) return nullptr;
        return StaticCastSharedRef<SSilverChoirToolTip>(SlateTip);
    }

    bool ValidateAndPreview(APlayerController* PC)
    {
        ShortOwner.Reset(CreateWidget<UBasicButtonWidget>(PC, UBasicButtonWidget::StaticClass()));
        if (!Test->TestNotNull(TEXT("Native tooltip owner"), ShortOwner.Get())) return false;
        ShortOwner->ToolTipStyle.HoverDelaySeconds = 0.f; // Presentation/UMG compatibility checks below.
        ShortSlate = ShortOwner->TakeWidget();
        const auto Frame = FindFrame(ShortSlate.ToSharedRef());
        if (!Test->TestTrue(TEXT("Actual button tree contains its Slate frame"), Frame.IsValid())) return false;
        Test->TestFalse(TEXT("Unset text does not create a game tooltip"), Frame->GetToolTip().IsValid());
        Test->TestFalse(TEXT("Unset text has no native tooltip"), ShortSlate->GetToolTip().IsValid());

        // Call through UWidget to exercise the nonvirtual engine setter.
        UWidget* NativeOwner = ShortOwner.Get();
        const FText ShortText = FText::FromString(TEXT("切换到小队模式"));
        const FText Multiline = FText::FromString(TEXT("阿尔法小队 · ALPHA\n4 名成员 · 已准备就绪，点击查看成员与当前小队的任务、生命状态及可用装备。"));
        NativeOwner->SetToolTipText(ShortText);
        ShortTip = ReadGameTip(Frame);
        if (!ShortTip) return false;
        Test->TestEqual(TEXT("Initial Chinese text is retained"), ShortTip->GetDisplayedText().ToString(), ShortText.ToString());
        NativeOwner->SetToolTipText(Multiline);
        const auto Updated = ReadGameTip(Frame);
        if (!Updated) return false;
        Test->TestTrue(TEXT("Runtime setter reuses the same styled tooltip"), Updated == ShortTip);
        Test->TestEqual(TEXT("Updated Chinese, English and newline survive intact"), Updated->GetDisplayedText().ToString(), Multiline.ToString());

        NativeOwner->SetToolTipText(FText::GetEmpty());
        Test->TestFalse(TEXT("Empty runtime text suppresses the styled tooltip"), Frame->GetToolTip().IsValid());
        Test->TestFalse(TEXT("Empty runtime text also clears the native tooltip"), ShortSlate->GetToolTip().IsValid());
        NativeOwner->SetToolTipText(ShortText);
        NativeOwner->SetToolTip(nullptr);
        Test->TestFalse(TEXT("SetToolTip(nullptr) does not revive stale text"), Frame->GetToolTip().IsValid());
        Test->TestFalse(TEXT("SetToolTip(nullptr) clears the native tooltip"), ShortSlate->GetToolTip().IsValid());

        CustomContent.Reset(NewObject<UTextBlock>(PC));
        CustomContent->SetText(FText::FromString(TEXT("Custom authored tooltip")));
        NativeOwner->SetToolTip(CustomContent.Get());
        Test->TestFalse(TEXT("Explicit tooltip widget bypasses the game presentation"), Frame->GetToolTip().IsValid());
        const auto CustomTip = ShortSlate->GetToolTip();
        if (!Test->TestTrue(TEXT("Explicit tooltip stays attached to the native wrapper"), CustomTip.IsValid())) return false;
        Test->TestTrue(TEXT("Explicit custom content is preserved"), CustomTip->GetContentWidget() == CustomContent->TakeWidget());

        NativeOwner->SetToolTip(nullptr);
        NativeOwner->SetToolTipText(ShortText);
        ShortOwner->bUseGameToolTipStyle = false;
        Test->TestFalse(TEXT("Disabling game style leaves the frame out of tooltip selection"), Frame->GetToolTip().IsValid());
        const auto NativeTip = ShortSlate->GetToolTip();
        if (!Test->TestTrue(TEXT("Disabling game style preserves the native tooltip"), NativeTip.IsValid())) return false;
        Test->TestEqual(TEXT("Disabled style exposes the original SToolTip"), NativeTip->AsWidget()->GetType(), FName(TEXT("SToolTip")));
        ShortOwner->bUseGameToolTipStyle = true;
        const auto Restored = ReadGameTip(Frame);
        if (!Restored) return false;
        Test->TestTrue(TEXT("Re-enabling style retains its existing cache"), Restored == ShortTip);

        LongOwner.Reset(CreateWidget<UBasicButtonWidget>(PC, UBasicButtonWidget::StaticClass()));
        if (!Test->TestNotNull(TEXT("Multiline tooltip owner"), LongOwner.Get())) return false;
        LongOwner->ToolTipStyle.HoverDelaySeconds = 0.f;
        LongOwner->SetToolTipText(Multiline);
        LongSlate = LongOwner->TakeWidget();
        const auto LongFrame = FindFrame(LongSlate.ToSharedRef());
        if (!Test->TestTrue(TEXT("Second button contains its Slate frame"), LongFrame.IsValid())) return false;
        LongTip = ReadGameTip(LongFrame);
        if (!LongTip) return false;
        Test->TestEqual(TEXT("Text set before Slate construction is retained"), LongTip->GetDisplayedText().ToString(), Multiline.ToString());
        ShortTip->OnOpening();
        LongTip->OnOpening();

        FSlateFontInfo TitleFont = ShortOwner->Font, LabelFont = TitleFont;
        TitleFont.Size = 22.f;
        LabelFont.Size = 12.f;
        Preview = SNew(SBorder)
            .Visibility(EVisibility::HitTestInvisible)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(FColor(3, 8, 14)))
            .Padding(FMargin(80.f, 70.f))
            .HAlign(HAlign_Left).VAlign(VAlign_Top)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 28.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("悬浮提示 · GAME TOOLTIP"))).Font(TitleFont).ColorAndOpacity(FLinearColor(FColor(177, 215, 229))) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("简短提示"))).Font(LabelFont).ColorAndOpacity(FLinearColor(FColor(91, 129, 150))) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
                [ ShortTip->AsWidget() ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 30.f, 0.f, 8.f)
                [ SNew(STextBlock).Text(FText::FromString(TEXT("多行文本与自动换行"))).Font(LabelFont).ColorAndOpacity(FLinearColor(FColor(91, 129, 150))) ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
                [ LongTip->AsWidget() ]
            ];
        Viewport->AddViewportWidgetContent(Preview.ToSharedRef(), 100);
        return true;
    }

    void Cleanup()
    {
        if (Preview && Viewport.IsValid()) Viewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
        Preview.Reset();
        ShortTip.Reset(); LongTip.Reset();
        ShortSlate.Reset(); LongSlate.Reset();
        ShortOwner.Reset(); LongOwner.Reset(); CustomContent.Reset();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameToolTipRuntimeTest,
    "SilverChoir.UI.GameToolTip.Runtime", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGameToolTipRuntimeTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(GameToolTipTest::FRuntime(this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameToolTipStationaryDelayTest,
    "SilverChoir.UI.GameToolTip.StationaryDelay", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGameToolTipStationaryDelayTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(GameToolTipTest::FStationaryDelay(this));
    return true;
}
#endif
