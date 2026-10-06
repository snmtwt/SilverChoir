#include "CSOCoverTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOPartitionTest, "CoverSmartObjects.Partition.NegativeAndBoundaryCoordinates", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOPartitionTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Negative position uses floor"), CSOCover::CellFor(FVector(-1, -1000, -1001), 1000), FIntVector(-1, -1, -2));
    TestEqual(TEXT("Positive cell boundary"), CSOCover::CellFor(FVector(0, 999.9, 1000), 1000), FIntVector(0, 0, 1));
    TestNotEqual(TEXT("Vertical floors remain distinct"), CSOCover::CellFor(FVector(0, 0, 999), 1000), CSOCover::CellFor(FVector(0, 0, 1000), 1000));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOPeekGeometryTest, "CoverSmartObjects.Geometry.PeekAndProfile", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOPeekGeometryTest::RunTest(const FString& Parameters)
{
    FCSOAgentProfile Profile;
    TestTrue(TEXT("Default profile physically valid"), Profile.IsValid());
    FCSOBakedCover Cover;
    Cover.Position = FVector(100, 200, 300);
    TestEqual(TEXT("Standing head measured from feet"), Cover.GetPeekEye(Profile, ECSOPeek::Stand), FVector(100, 200, 460));
    TestEqual(TEXT("Left lean exactly 20cm"), Cover.GetPeekEye(Profile, ECSOPeek::Left), FVector(100, 180, 400));
    TestEqual(TEXT("Right lean exactly 20cm"), Cover.GetPeekEye(Profile, ECSOPeek::Right), FVector(100, 220, 400));
    Cover.bCrouched = false;
    Cover.WallDirection = FVector(0, 1, 0);
    TestEqual(TEXT("Tall wall rotated right peek"), Cover.GetPeekEye(Profile, ECSOPeek::Right), FVector(80, 200, 460));
    Cover.RightPeekDistance = 60.f;
    TestEqual(TEXT("Baked wall edge plus exposure controls actual anchor"), Cover.GetPeekEye(Profile, ECSOPeek::Right), FVector(40, 200, 460));
    Profile.CrouchHalfHeight = Profile.Radius - 1;
    TestFalse(TEXT("Impossible capsule rejected"), Profile.IsValid());
    Profile = FCSOAgentProfile();
    Profile.StandEyeHeight = 181;
    TestFalse(TEXT("Eye above capsule rejected"), Profile.IsValid());
    return true;
}
#endif
