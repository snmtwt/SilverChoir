#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "H5UI_RuntimeView.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FH5UI_TacticalMapStableUpdateTest,
	"H5UIPlugin.Runtime.TacticalMapStableUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FH5UI_TacticalMapStableUpdateTest::RunTest(const FString& Parameters)
{
	FString LoadError;
	FH5UI_RuntimeView View(
		[]() {},
		[&LoadError](const FString& Error) { LoadError = Error; },
		[](const FH5UI_Event&) {});

	TestTrue(
		TEXT("Tactical Map V5 native-safe document loads"),
		View.LoadURL(TEXT("coui://uiresources/TacticalMap/V5/tactical-map.html")));
	TestTrue(TEXT("Tactical Map V5 reports no load error"), LoadError.IsEmpty());

	const FVector2D Viewport(1600.0f, 720.0f);
	const double StartTime = FPlatformTime::Seconds();
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		View.Update(Viewport, StartTime + static_cast<double>(Frame) / 60.0);
	}
	const double UpdateSeconds = FPlatformTime::Seconds() - StartTime;

	FString SetupResult;
	FString SetupError;
	TestTrue(
		TEXT("Tactical Map card geometry markers can be assigned"),
		View.ExecuteJavaScript(
			TEXT("var c=document.querySelector('.unit-card');")
				TEXT("c.id='test-first-unit-card';")
				TEXT("c.querySelector('.unit-portrait').id='test-unit-portrait';")
				TEXT("c.querySelector('.hand-slots').id='test-hand-slots';")
				TEXT("c.querySelector('.unit-summary').id='test-unit-summary';")
				TEXT("c.querySelector('.unit-control').id='test-unit-control';")
				TEXT("document.querySelectorAll('.unit-card')[1].id='test-second-unit-card';")
				TEXT("document.querySelector('.unit-card:last-child').id='test-last-unit-card';")
				TEXT("document.querySelector('.global-controls').id='test-global-controls';'ready'"),
			SetupResult,
			SetupError));
	View.Update(Viewport, StartTime + 2.1);

	TestTrue(
		TEXT("Tactical Map V5 has a laid-out unit roster"),
		View.GetElementBorderSizeForTesting(TEXT("unit-roster")).X > 100.0f);
	TestTrue(
		TEXT("Initial real viewport dispatches resize without manual window resizing"),
		View.GetElementBorderSizeForTesting(TEXT("tactical-hud")).X >= 1599.0f);
	const FVector2D RosterPosition =
		View.GetElementBorderPositionForTesting(TEXT("unit-roster"));
	const FVector2D RosterSize =
		View.GetElementBorderSizeForTesting(TEXT("unit-roster"));
	const FVector2D GlobalControlsPosition =
		View.GetElementBorderPositionForTesting(TEXT("test-global-controls"));
	const FVector2D GuardPosition =
		View.GetElementBorderPositionForTesting(TEXT("roster-right-guard"));
	const FVector2D GuardSize =
		View.GetElementBorderSizeForTesting(TEXT("roster-right-guard"));
	TestTrue(
		TEXT("Scrollable roster viewport ends before the fixed global controls"),
		RosterPosition.X + RosterSize.X <= GlobalControlsPosition.X + 0.1f);
	TestTrue(
		TEXT("Opaque right guard covers the complete reserved control gutter"),
		GuardSize.X >= 45.0f && GuardSize.Y >= 135.0f);
	TestTrue(
		TEXT("Scrollable roster ends at the opaque right guard"),
		RosterPosition.X + RosterSize.X <= GuardPosition.X + 0.1f);
	const FVector2D PortraitPosition =
		View.GetElementBorderPositionForTesting(TEXT("test-unit-portrait"));
	const FVector2D PortraitSize =
		View.GetElementBorderSizeForTesting(TEXT("test-unit-portrait"));
	const FVector2D HandSlotsPosition =
		View.GetElementBorderPositionForTesting(TEXT("test-hand-slots"));
	TestTrue(
		TEXT("Weapon slots are laid out below the portrait instead of beside it"),
		HandSlotsPosition.Y >= PortraitPosition.Y + PortraitSize.Y);
	TestTrue(
		TEXT("Expanded command panel keeps a readable width"),
		View.GetElementBorderSizeForTesting(TEXT("test-unit-control")).X >= 145.0f);

	FString AnimationResult;
	FString AnimationError;
	TestTrue(
		TEXT("Selecting another operator starts the native panel transition"),
		View.ExecuteJavaScript(TEXT("TacticalMap.selectOperator(1);'animating'"), AnimationResult, AnimationError));
	View.Update(Viewport, FPlatformTime::Seconds());
	FPlatformProcess::SleepNoStats(0.12f);
	View.Update(Viewport, FPlatformTime::Seconds());
	const float MidFirstCardWidth =
		View.GetElementBorderSizeForTesting(TEXT("test-first-unit-card")).X;
	const float MidSecondCardWidth =
		View.GetElementBorderSizeForTesting(TEXT("test-second-unit-card")).X;
	TestTrue(
		TEXT("Collapsing and expanding cards both expose an interpolated middle frame"),
		MidFirstCardWidth > 158.5f && MidFirstCardWidth < 309.5f &&
			MidSecondCardWidth > 158.5f && MidSecondCardWidth < 309.5f);
	// Animation properties and their resulting flex layout are resolved on
	// consecutive native updates, matching normal game frames.
	for (int32 AnimationFrame = 0; AnimationFrame < 8; ++AnimationFrame)
	{
		FPlatformProcess::SleepNoStats(0.05f);
		View.Update(Viewport, FPlatformTime::Seconds());
	}
	const float FinalFirstCardWidth =
		View.GetElementBorderSizeForTesting(TEXT("test-first-unit-card")).X;
	const float FinalSecondCardWidth =
		View.GetElementBorderSizeForTesting(TEXT("test-second-unit-card")).X;
	AddInfo(FString::Printf(
		TEXT("Native card transition widths: middle %.2f/%.2f, final %.2f/%.2f."),
		MidFirstCardWidth,
		MidSecondCardWidth,
		FinalFirstCardWidth,
		FinalSecondCardWidth));
	TestTrue(
		TEXT("Native card transition reaches the collapsed and expanded endpoints"),
		FMath::IsNearlyEqual(
			FinalFirstCardWidth,
			158.0f,
			0.5f) &&
			FMath::IsNearlyEqual(
				FinalSecondCardWidth,
				310.0f,
				0.5f));
	TestTrue(
		TEXT("Tactical Map V5 update loop completes without a layout/animation stall"),
		UpdateSeconds < 2.0);

	View.Update(FVector2D(1280.0f, 720.0f), StartTime + 2.2);
	const FVector2D DesignRosterPosition =
		View.GetElementBorderPositionForTesting(TEXT("unit-roster"));
	const FVector2D DesignRosterSize =
		View.GetElementBorderSizeForTesting(TEXT("unit-roster"));
	const FVector2D LastCardPosition =
		View.GetElementBorderPositionForTesting(TEXT("test-last-unit-card"));
	const FVector2D LastCardSize =
		View.GetElementBorderSizeForTesting(TEXT("test-last-unit-card"));
	const float RemainingRosterWidth =
		DesignRosterPosition.X + DesignRosterSize.X - LastCardPosition.X - LastCardSize.X;
	TestTrue(
		TEXT("Six cards plus one expanded control panel fill the 16:9 roster"),
		RemainingRosterWidth >= 6.0f && RemainingRosterWidth <= 8.0f);
	AddInfo(FString::Printf(
		TEXT("1280x720 logical roster width %.1f, remaining width after sixth card %.1f."),
		DesignRosterSize.X,
		RemainingRosterWidth));

	View.Update(Viewport, StartTime + 2.3);
	FSlateWindowElementList Elements(nullptr);
	View.Paint(
		FGeometry::MakeRoot(FVector2f(Viewport.X, Viewport.Y), FSlateLayoutTransform()),
		Elements,
		0);
	TestTrue(
		TEXT("Tactical Map submits clipped geometry to Slate"),
		View.GetPerformanceStats().DrawBatches > 0);
	TestTrue(
		TEXT("Overflowing tactical-card descendants are constrained before Slate submission"),
		View.GetPerformanceStats().ClampedVertices > 0);
	AddInfo(FString::Printf(
		TEXT("Tactical Map V5 completed 120 native updates in %.3f seconds."),
		UpdateSeconds));
	return true;
}

#endif
