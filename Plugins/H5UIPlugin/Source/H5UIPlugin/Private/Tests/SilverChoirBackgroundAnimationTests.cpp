#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "H5UI_RuntimeView.h"
#include "HAL/PlatformProcess.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Property.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSilverChoirBackgroundAnimationTest,
	"SilverChoir.UI.BackgroundAnimationWithoutInputOrJavaScript",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSilverChoirBackgroundAnimationTest::RunTest(const FString& Parameters)
{
	FH5UI_RuntimeView View([] {}, [this](const FString& Error) { AddError(Error); }, [](const FH5UI_Event&) {});
	View.SetJavaScriptEnabled(false);
	if (!TestTrue(TEXT("Actual menu background loads"), View.LoadURL(TEXT("coui://uiresources/MainMenu/background.html")))) { return false; }
	View.Update({1600, 900}, FPlatformTime::Seconds());
	Rml::Element* Outer = View.Document->GetElementById("orbit-outer");
	Rml::Element* Mid = View.Document->GetElementById("orbit-mid");
	if (!TestNotNull(TEXT("Outer orbit exists"), Outer) || !TestNotNull(TEXT("Inner orbit exists"), Mid)) { return false; }
	auto TransformValue = [](Rml::Element* Element)
	{
		const Rml::Property* Transform = Element->GetProperty("transform");
		return Transform ? FString(UTF8_TO_TCHAR(Transform->ToString().c_str())) : FString();
	};
	const FString OuterBefore = TransformValue(Outer);
	const FString MidBefore = TransformValue(Mid);
	const FVector2D ScanBefore = View.GetElementBorderPositionForTesting(TEXT("scan-line"));
	// Cross the plugin idle threshold, without any pointer events or script jobs.
	for (int32 Index = 0; Index < 8; ++Index)
	{
		FPlatformProcess::Sleep(0.1f);
		View.Update({1600, 900}, FPlatformTime::Seconds());
	}
	// Render-time projection matrices are unavailable under NullRHI; inspect the animated CSS values.
	TestTrue(TEXT("Outer orbit moves without JS/input"), !TransformValue(Outer).IsEmpty() && OuterBefore != TransformValue(Outer));
	TestTrue(TEXT("Inner orbit moves without JS/input"), !TransformValue(Mid).IsEmpty() && MidBefore != TransformValue(Mid));
	TestTrue(TEXT("Scan line advances after idle threshold"), FVector2D::Distance(ScanBefore, View.GetElementBorderPositionForTesting(TEXT("scan-line"))) > 1.0);
	// Reuse the live document across aspect-ratio changes, including portrait.
	for (const FVector2D Size : {FVector2D(1920, 1080), FVector2D(1920, 1200), FVector2D(3440, 1440), FVector2D(3840, 1080), FVector2D(2132, 728),
		FVector2D(1024, 768), FVector2D(720, 1280), FVector2D(1280, 720)})
	{
		View.Update(Size, FPlatformTime::Seconds());
		const FVector2D FrameSize = View.GetElementBorderSizeForTesting(TEXT("viewport-frame"));
		const FVector2D CoreSize = View.GetElementBorderSizeForTesting(TEXT("signal-core"));
		TestTrue(*FString::Printf(TEXT("Background fills %.0fx%.0f viewport"), Size.X, Size.Y), FrameSize.Equals(Size, 1.0));
		TestTrue(TEXT("Orbit stays circular when aspect ratio changes"), FMath::Abs(CoreSize.X - CoreSize.Y) < 1.0);
		const double ExpectedDiameter = Size.X / Size.Y <= 1.5 ? Size.X * 0.27 : Size.Y * 0.46;
		TestTrue(TEXT("Orbit reserves space for the side columns"), FMath::Abs(CoreSize.X - ExpectedDiameter) < 2.0);
	}
	View.Close();
	return true;
}
#endif
