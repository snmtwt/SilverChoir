#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBox.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSilverChoirButtonWidgetTest,
	"SilverChoir.UI.ButtonWidgetInputAndContent", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSilverChoirButtonWidgetTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	TStrongObjectPtr<UBasicButtonWidget> Button(CreateWidget<UBasicButtonWidget>(Instance.Get(), UBasicButtonWidget::StaticClass()));
	if (TestNotNull(TEXT("Native button can be created without a Blueprint asset"), Button.Get()))
	{
		TSharedPtr<SWidget> SlateWidget = Button->TakeWidget();
		TestNotNull(TEXT("Native button builds its label"), Button->Label.Get());
		TestNotNull(TEXT("Native button builds its icon"), Button->IconImage.Get());
		Button->SetButtonText(FText::FromString(TEXT("Test label")));
		TestEqual(TEXT("Text setter reaches UMG"), Button->Label->GetText().ToString(), FString(TEXT("Test label")));
		Button->SetContentMode(EBasicButtonContent::IconOnly);
		TestEqual(TEXT("Icon-only hides label"), Button->Label->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("Icon-only displays icon"), Button->IconSizeBox->GetVisibility(), ESlateVisibility::HitTestInvisible);
		Button->SetContentMode(EBasicButtonContent::TextOnly);
		TestEqual(TEXT("Text-only hides icon"), Button->IconSizeBox->GetVisibility(), ESlateVisibility::Collapsed);
		Button->SetContentMode(EBasicButtonContent::IconAndText);
		TestEqual(TEXT("Combined mode displays label"), Button->Label->GetVisibility(), ESlateVisibility::HitTestInvisible);
		TestEqual(TEXT("Combined mode displays icon"), Button->IconSizeBox->GetVisibility(), ESlateVisibility::HitTestInvisible);

		TestTrue(TEXT("Enabled button accepts input"), Button->BeginPress(EKeys::Enter));
		Button->SetIsEnabled(false);
		TestFalse(TEXT("Disabling widget cancels pending input"), Button->IsPressPending());
		TestFalse(TEXT("Disabled widget rejects input"), Button->BeginPress(EKeys::Enter));
		Button->SetIsEnabled(true);
		Button->BeginPress(EKeys::Enter);
		Button->SetVisibility(ESlateVisibility::Collapsed);
		TestFalse(TEXT("Hiding widget cancels pending input"), Button->IsPressPending());
		Button->SetVisibility(ESlateVisibility::Visible);
		Button->BeginPress(EKeys::Enter);
		Button->NativeOnFocusLost(FFocusEvent(EFocusCause::SetDirectly, 0));
		TestFalse(TEXT("Losing focus cancels pending input"), Button->IsPressPending());

		// A Blueprint sound/start listener may cancel or replace UI synchronously.
		Button->OnPressStarted.AddDynamic(Button.Get(), &UBasicButtonWidget::CancelPendingClick);
		TestFalse(TEXT("Reentrant cancellation in a start listener is respected"), Button->BeginPress(EKeys::Enter));
		TestFalse(TEXT("Cancelled start callback leaves no deferred click"), Button->IsPressPending());
		Button->OnPressStarted.Clear();

		// Hover notification (and its sound) is an entry transition, not per-frame feedback.
		const FPointerEvent PointerEvent;
		const FGeometry HoverGeometry;
		Button->OnHovered.AddDynamic(Button.Get(), &UBasicButtonWidget::CancelPendingClick);
		Button->BeginPress(EKeys::Enter);
		Button->NativeOnMouseEnter(HoverGeometry, PointerEvent);
		TestFalse(TEXT("Entering an enabled button emits hover"), Button->IsPressPending());
		Button->BeginPress(EKeys::Enter);
		Button->NativeOnMouseEnter(HoverGeometry, PointerEvent);
		TestTrue(TEXT("Duplicate enter does not replay hover"), Button->IsPressPending());
		Button->NativeOnMouseLeave(PointerEvent);
		Button->NativeOnMouseEnter(HoverGeometry, PointerEvent);
		TestFalse(TEXT("Leaving and reentering emits a new hover"), Button->IsPressPending());
		Button->SetIsEnabled(false);
		Button->NativeOnMouseEnter(HoverGeometry, PointerEvent);
		TestFalse(TEXT("Disabled buttons do not enter hover state"), Button->bPointerHovered);
		Button->SetIsEnabled(true);
		Button->SetVisibility(ESlateVisibility::Collapsed);
		Button->NativeOnMouseEnter(HoverGeometry, PointerEvent);
		TestFalse(TEXT("Hidden buttons do not enter hover state"), Button->bPointerHovered);
		Button->OnHovered.Clear();

		// Idle feedback must remain responsive to both parent state and live style edits.
		Button->SetVisibility(ESlateVisibility::Visible);
		Button->SetIsEnabled(true);
		TSharedPtr<SBox> Parent = SNew(SBox)[SlateWidget.ToSharedRef()];
		Button->NativeTick(HoverGeometry, 1.f);
		const FLinearColor IdleTint = Button->ButtonForegroundColor;
		TestTrue(TEXT("Idle label uses the configured tint"), Button->Label->GetColorAndOpacity() == FSlateColor(IdleTint));
		Button->NativeOnMouseEnter(HoverGeometry, PointerEvent);
		Button->NativeTick(HoverGeometry, 1.f);
		TestTrue(TEXT("Hover still brightens the label"), Button->Label->GetColorAndOpacity() == FSlateColor(FLinearColor::White));
		Button->NativeOnMouseLeave(PointerEvent);
		Button->NativeTick(HoverGeometry, 1.f);
		TestTrue(TEXT("Leaving hover restores the configured tint"), Button->Label->GetColorAndOpacity() == FSlateColor(IdleTint));

		Button->ButtonForegroundColor = FLinearColor(.2f, .4f, .6f, .8f);
		Button->NativeTick(HoverGeometry, 1.f);
		TestTrue(TEXT("Direct style changes reach label without a setter"), Button->Label->GetColorAndOpacity() == FSlateColor(Button->ButtonForegroundColor));
		TestTrue(TEXT("Direct style changes reach icon without a setter"), Button->IconImage->GetColorAndOpacity() == Button->ButtonForegroundColor);
		Button->Label->SetColorAndOpacity(FLinearColor::Red);
		Button->IconImage->SetColorAndOpacity(FLinearColor::Red);
		Button->NativeTick(HoverGeometry, 1.f);
		TestTrue(TEXT("Child style edits do not make the feedback cache stale"), Button->Label->GetColorAndOpacity() == FSlateColor(Button->ButtonForegroundColor));
		TestTrue(TEXT("Icon style edits are corrected on the next tick"), Button->IconImage->GetColorAndOpacity() == Button->ButtonForegroundColor);
		Button->SetSelected(true);
		Button->NativeTick(HoverGeometry, 1.f);
		Button->SynchronizeProperties();
		Button->NativeTick(HoverGeometry, 1.f);
		TestTrue(TEXT("Synchronizing a selected button retains selected feedback"), Button->Label->GetColorAndOpacity() == FSlateColor(FLinearColor::White));
		Button->SetSelected(false);

		TestTrue(TEXT("Press begins before parent disable"), Button->BeginPress(EKeys::Enter));
		Parent->SetEnabled(false);
		Button->NativeTick(HoverGeometry, 1.f);
		TestFalse(TEXT("Parent disable cancels the pending click on tick"), Button->IsPressPending());
		TestTrue(TEXT("Parent disable dims feedback"), Button->IconImage->GetColorAndOpacity() == Button->ButtonForegroundColor * FLinearColor(.4f,.4f,.4f,1.f));
		Parent->SetEnabled(true);
		TestTrue(TEXT("Re-enabled parent allows a new press"), Button->BeginPress(EKeys::Enter));
		Parent->SetVisibility(EVisibility::Collapsed);
		Button->NativeTick(HoverGeometry, 1.f);
		TestFalse(TEXT("Parent hide cancels the pending click on tick"), Button->IsPressPending());
		Parent->SetVisibility(EVisibility::Visible);
		Parent.Reset();
		SlateWidget.Reset();
		Button->ReleaseSlateResources(true);
	}
	Button.Reset();
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSilverChoirButtonIdleFeedbackTest,
	"SilverChoir.UI.ButtonIdleFeedback500", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSilverChoirButtonIdleFeedbackTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	Instance->InitializeStandalone();
	UWorld* World = Instance->GetWorld();
	TArray<TStrongObjectPtr<UBasicButtonWidget>> Buttons;
	TArray<TSharedPtr<SWidget>> SlateWidgets;
	TArray<TSharedPtr<SBox>> Parents;
	constexpr int32 ButtonCount = 500;
	constexpr int32 TickCount = 300;
	const FGeometry Geometry;
	Buttons.Reserve(ButtonCount); SlateWidgets.Reserve(ButtonCount); Parents.Reserve(ButtonCount);
	for (int32 Index = 0; Index < ButtonCount; ++Index)
	{
		UBasicButtonWidget* Button = CreateWidget<UBasicButtonWidget>(Instance.Get(), UBasicButtonWidget::StaticClass());
		if (!TestNotNull(TEXT("Idle benchmark button"), Button)) break;
		Buttons.Emplace(Button);
		SlateWidgets.Add(Button->TakeWidget());
		Parents.Add(SNew(SBox)[SlateWidgets.Last().ToSharedRef()]);
		// Half the rows are disabled through their parent, as in a locked/off-tile panel.
		Parents.Last()->SetEnabled(Index % 2 == 0);
		Button->NativeTick(Geometry, 1.f);
		Button->FeedbackColorWriteCount = Button->CaptureQueryCount = 0;
	}
	TestEqual(TEXT("Benchmark creates all 500 buttons"), Buttons.Num(), ButtonCount);
	const double Started = FPlatformTime::Seconds();
	for (int32 Frame = 0; Frame < TickCount; ++Frame)
		for (const auto& Button : Buttons) Button->NativeTick(Geometry, 1.f / 60.f);
	const double ElapsedMs = (FPlatformTime::Seconds() - Started) * 1000.;
	uint64 ColorWrites = 0, CaptureQueries = 0;
	for (const auto& Button : Buttons)
	{
		ColorWrites += Button->FeedbackColorWriteCount;
		CaptureQueries += Button->CaptureQueryCount;
	}
	TestEqual(TEXT("Stable feedback does not rewrite UMG colors"), ColorWrites, uint64(0));
	TestEqual(TEXT("Disabled idle rows do not query Slate capture"), CaptureQueries, uint64(0));
	// The old implementation unconditionally wrote both colors per tick and queried
	// capture for every disabled tick. These are source-derived counts, not an old FPS measurement.
	AddInfo(FString::Printf(TEXT("BUTTON_IDLE_PERF buttons=%d ticks=%d optimized_ms=%.3f ms_per_500_ticks=%.6f color_writes=%llu capture_queries=%llu source_baseline_color_writes=%llu source_baseline_capture_queries=%llu"),
		Buttons.Num(), TickCount, ElapsedMs, ElapsedMs / TickCount,
		static_cast<unsigned long long>(ColorWrites), static_cast<unsigned long long>(CaptureQueries),
		static_cast<unsigned long long>(uint64(Buttons.Num()) * TickCount * 2),
		static_cast<unsigned long long>(uint64(Buttons.Num() / 2) * TickCount)));
	// Actual state changes must still reach both controls after the entire idle run.
	if (!Buttons.IsEmpty())
	{
		auto* Button = Buttons[0].Get();
		Button->SetSelected(true);
		Button->NativeTick(Geometry, 1.f);
		TestTrue(TEXT("Idle optimization does not prevent selection feedback"), Button->Label->GetColorAndOpacity() == FSlateColor(FLinearColor::White));
		TestTrue(TEXT("Selection updates the icon too"), Button->IconImage->GetColorAndOpacity() == FLinearColor::White);
		TestEqual(TEXT("Only the changed button writes its two colors"), Button->FeedbackColorWriteCount, uint64(2));
	}
	Parents.Reset(); SlateWidgets.Reset();
	for (const auto& Button : Buttons) Button->ReleaseSlateResources(true);
	Buttons.Reset();
	Instance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}
#endif
