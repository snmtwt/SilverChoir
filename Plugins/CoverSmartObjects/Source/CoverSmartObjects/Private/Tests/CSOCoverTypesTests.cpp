#include "CSOCoverTypes.h"
#include "Misc/AutomationTest.h"
#include <limits>

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCSOLowCrouchProfileTest, "CoverSmartObjects.Geometry.LowCrouchProfileAndAnchors", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCSOLowCrouchProfileTest::RunTest(const FString& Parameters)
{
    FCSOAgentProfile Profile;
    Profile.Radius = 42.f;
    Profile.CrouchHalfHeight = 60.f;
    Profile.LowCrouchBodyHeight = 72.f;
    Profile.LowCrouchEyeHeight = 62.f;
    TestTrue(TEXT("Low animation silhouette can be shorter than the unchanged collision capsule"), Profile.IsValid());

    FCSOBakedCover Cover;
    Cover.Position = FVector(100, 200, 300);
    Cover.bLowCrouched = true;
    Cover.RightPeekDistance = 75.f;
    TestEqual(TEXT("Low crouch has an explicit stance"), Cover.GetStance(), ECSOCoverStance::LowCrouch);
    TestEqual(TEXT("Low body height describes the animation silhouette"), Cover.GetBodyHeight(Profile), 72.f);
    TestEqual(TEXT("Low eye height is measured above feet"), Cover.GetEyeHeight(Profile), 62.f);
    TestEqual(TEXT("Covered eye follows low stance"), Cover.GetEye(Profile), FVector(100, 200, 362));
    TestEqual(TEXT("Side peek preserves low stance and baked distance"), Cover.GetPeekEye(Profile, ECSOPeek::Right), FVector(100, 275, 362));
    TestEqual(TEXT("Rising from low cover still uses the full standing eye"), Cover.GetPeekEye(Profile, ECSOPeek::Stand), FVector(100, 200, 460));
    TestEqual(TEXT("Low stance leaves collision half height intact"), Profile.CrouchHalfHeight, 60.f);
    Cover.bLowCrouched = false;
    TestEqual(TEXT("Ordinary crouch retains its old stance"), Cover.GetStance(), ECSOCoverStance::Crouch);
    TestEqual(TEXT("Ordinary crouch retains full capsule silhouette"), Cover.GetBodyHeight(Profile), 120.f);
    TestEqual(TEXT("Ordinary crouch retains its original eye"), Cover.GetEye(Profile), FVector(100, 200, 400));

    FCSOAgentProfile Invalid = Profile;
    Invalid.LowCrouchBodyHeight = 0.f;
    TestFalse(TEXT("Zero low body height rejected"), Invalid.IsValid());
    Invalid = Profile;
    Invalid.LowCrouchEyeHeight = 0.f;
    TestFalse(TEXT("Zero low eye height rejected"), Invalid.IsValid());
    Invalid = Profile;
    Invalid.LowCrouchEyeHeight = Invalid.LowCrouchBodyHeight + 1.f;
    TestFalse(TEXT("Low eye above body rejected"), Invalid.IsValid());
    Invalid = Profile;
    Invalid.LowCrouchBodyHeight = 2.f * Invalid.CrouchHalfHeight + 1.f;
    TestFalse(TEXT("Low body outside the crouching capsule rejected"), Invalid.IsValid());
    Invalid = Profile;
    Invalid.LowCrouchBodyHeight = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Nonfinite low body height rejected"), Invalid.IsValid());
    Invalid = Profile;
    Invalid.LowCrouchEyeHeight = std::numeric_limits<float>::infinity();
    TestFalse(TEXT("Nonfinite low eye height rejected"), Invalid.IsValid());
    Invalid.bEnableLowCrouch = false;
    TestTrue(TEXT("Disabled low stance does not invalidate otherwise valid ordinary cover profiles"), Invalid.IsValid());
    return true;
}
#endif
