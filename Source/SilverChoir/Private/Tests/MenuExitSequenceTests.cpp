#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UIBasic/MenuExitSequence.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMenuExitSequenceTest, "SilverChoir.UI.MenuExitSequence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMenuExitSequenceTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Continue order is Quit, Settings, NewGame, LoadGame"), FMenuExitSequence::Order(1) == TArray<int32>({3,2,0,1}));
	for (int32 Selected = 0; Selected < 4; ++Selected)
	{
		const auto Order = FMenuExitSequence::Order(Selected);
		TestEqual(TEXT("Selected button exits last"), Order.Last(), Selected);
		TSet<int32> Unique(Order);
		TestEqual(TEXT("Every button occurs exactly once"), Unique.Num(), 4);
	}
	for (int32 Rank = 1; Rank < 4; ++Rank)
	{
		TestFalse(TEXT("No early start"), FMenuExitSequence::ShouldStart(100, 100 + Rank * 2 - 1, Rank));
		TestTrue(TEXT("Start exactly two frames apart"), FMenuExitSequence::ShouldStart(100, 100 + Rank * 2, Rank));
	}
	TestTrue(TEXT("Invalid action rejected"), FMenuExitSequence::Order(4).IsEmpty());
	return true;
}
#endif
