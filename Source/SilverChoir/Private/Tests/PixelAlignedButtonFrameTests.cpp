#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "UIBasic/PixelAlignedButtonFrame.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSilverChoirButtonBorderTest, "SilverChoir.UI.PixelAlignedBorders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSilverChoirButtonBorderTest::RunTest(const FString& Parameters)
{
	for (float Scale : {0.37f, 0.61f, 0.75f, 1.0f, 1.25f, 1.5f, 2.4f})
	{
		const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(365, 72), FSlateLayoutTransform(Scale, FVector2f(112.35f, 201.68f)));
		const auto Frame = FPixelAlignedButtonFrame::Make(Geometry, 1.0f);
		const FVector2D Min = Geometry.LocalToAbsolute(FVector2D(Frame.Min));
		const FVector2D Max = Geometry.LocalToAbsolute(FVector2D(Frame.Max));
		TestTrue(TEXT("Left border falls on a screen pixel"), FMath::IsNearlyEqual(Min.X, FMath::RoundToDouble(Min.X), 0.001));
		TestTrue(TEXT("Top border falls on a screen pixel"), FMath::IsNearlyEqual(Min.Y, FMath::RoundToDouble(Min.Y), 0.001));
		TestTrue(TEXT("Right border falls on a screen pixel"), FMath::IsNearlyEqual(Max.X, FMath::RoundToDouble(Max.X), 0.001));
		TestTrue(TEXT("Bottom border falls on a screen pixel"), FMath::IsNearlyEqual(Max.Y, FMath::RoundToDouble(Max.Y), 0.001));
		TestTrue(TEXT("Horizontal and vertical stroke thickness match"), FMath::IsNearlyEqual(Frame.Thickness.X, Frame.Thickness.Y));
		TestTrue(TEXT("Border cannot shrink below one physical pixel"), Frame.Thickness.X * Scale >= 0.999f);
	}
	return true;
}
#endif
