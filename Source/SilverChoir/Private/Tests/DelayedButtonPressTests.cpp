#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "UIBasic/DelayedButtonPress.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSilverChoirDelayedButtonTest,
	"SilverChoir.UI.DelayedButtonTiming", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSilverChoirDelayedButtonTest::RunTest(const FString& Parameters)
{
	FDelayedButtonPress Press;
	TestTrue(TEXT("First press accepted"), Press.Begin(100));
	TestFalse(TEXT("Repeated input cannot queue another click"), Press.Begin(100));
	Press.Release(true);
	TestFalse(TEXT("Quick release does not bypass the frame delay"), Press.Advance(100));
	TestFalse(TEXT("First next frame does not click"), Press.Advance(101));
	TestFalse(TEXT("No pressed feedback before two frames"), Press.bPressedVisual);
	TestFalse(TEXT("Second frame displays feedback before broadcasting"), Press.Advance(102));
	TestTrue(TEXT("Pressed feedback begins exactly two frames later"), Press.bPressedVisual);
	TestFalse(TEXT("Multiple ticks in one engine frame do not advance timing"), Press.Advance(102));
	TestTrue(TEXT("Valid release clicks after a frame of feedback"), Press.Advance(103));
	TestFalse(TEXT("Click emitted once"), Press.Advance(104));

	Press.Begin(200);
	Press.Advance(202);
	TestFalse(TEXT("Held press does not click"), Press.Advance(250));
	Press.Release(true);
	TestTrue(TEXT("Release after feedback clicks"), Press.Advance(251));

	Press.Begin(300);
	Press.Release(false);
	TestFalse(TEXT("Release outside cancels click"), Press.Advance(400));
	Press.Begin(500);
	Press.Release(true);
	Press.Cancel();
	TestFalse(TEXT("Disabled/removed/lost-focus cancellation clears pending clicks"), Press.Advance(600));
	Press.Begin(700);
	Press.Release(true);
	TestFalse(TEXT("A skipped tick still presents feedback before clicking"), Press.Advance(710));
	TestTrue(TEXT("Feedback is shown before a late click"), Press.Advance(711));
	return true;
}
#endif
