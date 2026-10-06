#if WITH_DEV_AUTOMATION_TESTS
#include "EHBDragAngleSnap.h"
#include "EHBWallRailingJunction.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWorldOctantSnapTest, "EHB.Editor.WorldOctantDragSnap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBWorldOctantSnapTest::RunTest(const FString& Parameters)
{
	const FVector Start(123.25, -89.75, 42);
	for (int32 Octant = 0; Octant < 8; ++Octant)
	{
		for (double Offset : {-2.9, 2.9})
		{
			const double Angle = FMath::DegreesToRadians(Octant * 45.0 + Offset);
			const FVector End = Start + FVector(FMath::Cos(Angle)*250, FMath::Sin(Angle)*250, 75);
			const FVector Result = EHBDragAngleSnap::Snap(Start, End);
			const double Expected = FMath::DegreesToRadians(Octant * 45.0);
			const FVector Direction(FMath::Cos(Expected), FMath::Sin(Expected), 0);
			TestTrue(TEXT("Both sides snap to the world octant"), (Result-Start).GetSafeNormal2D().Equals(Direction, 0.00001));
			TestEqual(TEXT("Endpoint elevation survives snapping"), Result.Z, End.Z);
			TestTrue(TEXT("Repeated preview/commit snap is stable"), EHBDragAngleSnap::Snap(Start, Result).Equals(Result, 0.00001));
		}
		const double Angle = FMath::DegreesToRadians(Octant * 45.0 + 3.1);
		const FVector Free = Start + FVector(FMath::Cos(Angle)*250, FMath::Sin(Angle)*250, 0);
		TestTrue(TEXT("Outside capture cone remains free"), EHBDragAngleSnap::Snap(Start, Free).Equals(Free, 0.00001));
	}
	TestTrue(TEXT("Zero XY length remains stable"), EHBDragAngleSnap::Snap(Start, Start+FVector(0,0,10)).Equals(Start+FVector(0,0,10)));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallRailingJunctionTest, "EHB.Editor.WallRailingJunctionClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEHBWallRailingJunctionTest::RunTest(const FString& Parameters)
{
	for (double Rotation : {0.0, 37.0, 135.0})
	{
		const FQuat Transform(FVector::UpVector, FMath::DegreesToRadians(Rotation));
		for (double Side : {-1.0, 1.0})
		{
			for (double Along : {-1.0, 1.0})
			{
				auto Check = [&](double Angle, double Width)
				{
					const double Radians = FMath::DegreesToRadians(Angle);
					return EHBWallRailingJunction::ClearsRetainedWall(
						Transform.RotateVector(FVector(Along*FMath::Cos(Radians), Side*FMath::Sin(Radians),0)),
						Transform.RotateVector(FVector::ForwardVector),20,20,Width);
				};
				TestTrue(TEXT("Perpendicular remains supported"), Check(90,5));
				TestTrue(TEXT("Clear oblique junction supported on both sides"), Check(60,5));
				TestFalse(TEXT("45 degree corner overlap refused"), Check(45,5));
				TestFalse(TEXT("Wider rail needs larger departure angle"), Check(60,8));
				TestFalse(TEXT("Parallel rail refused"), Check(0,5));
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBPreciseDrawingAssistTest,"EHB.Editor.PreciseDrawingAssist",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBPreciseDrawingAssistTest::RunTest(const FString& Parameters)
{
 const FVector Start(500,-700,42),End=Start+FVector(100,3,19);
 EHBDragAngleSnap::FOptions Options;
 TestTrue(TEXT("Default preserves octant behavior"),EHBDragAngleSnap::Resolve(Start,End,Options).Equals(EHBDragAngleSnap::Snap(Start,End)));
 Options.bAngleSnap=false;TestTrue(TEXT("Disabled assist preserves free endpoint"),EHBDragAngleSnap::Resolve(Start,End,Options).Equals(End));
 Options.bFixedLength=true;Options.LengthCm=237.5;
 for(double Angle:{0.0,45.0,90.0,180.0,270.0,-45.0,33.7})
 {
  Options.bFixedDirection=true;Options.DirectionDegrees=Angle;
  const auto Result=EHBDragAngleSnap::Resolve(Start,End,Options);
  TestTrue(TEXT("Centimeter length is exact in XY"),FMath::IsNearlyEqual(FVector::Dist2D(Start,Result),237.5,0.00001));
  TestTrue(TEXT("Requested world direction"),(Result-Start).GetSafeNormal2D().Equals(FRotator(0,Angle,0).Vector(),0.00001));
  TestEqual(TEXT("Elevation unchanged"),Result.Z,End.Z);
  TestTrue(TEXT("Preview/commit repeated resolve is stable"),EHBDragAngleSnap::Resolve(Start,Result,Options).Equals(Result,0.00001));
  TestTrue(TEXT("Temporary bypass ignores all manual constraints"),EHBDragAngleSnap::Resolve(Start,End,Options,true).Equals(End));
 }
 Options.bFixedDirection=false;Options.LengthCm=-1;
 TestTrue(TEXT("Invalid configured length cannot create endpoint"),EHBDragAngleSnap::Resolve(Start,End,Options).Equals(End));
 Options.LengthCm=300;TestTrue(TEXT("No arbitrary direction at coincident cursor"),EHBDragAngleSnap::Resolve(Start,Start,Options).Equals(Start));
 Options.bFixedLength=false;Options.bFixedDirection=true;Options.DirectionDegrees=90;
 const auto DirectionOnly=EHBDragAngleSnap::Resolve(Start,End,Options);
 TestTrue(TEXT("Direction lock without length keeps cursor radius"),FMath::IsNearlyEqual(FVector::Dist2D(Start,DirectionOnly),FVector::Dist2D(Start,End),0.00001));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBRectangleDrawingAssistTest,"EHB.Editor.RectangleDrawingAssist",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBRectangleDrawingAssistTest::RunTest(const FString& Parameters)
{
 EHBDragAngleSnap::FOptions O;O.bFixedRectangle=true;O.RectangleWidthCm=313.25;O.RectangleDepthCm=427.75;
 for(double Angle:{0.0,37.0,90.0})for(double Scale:{1.0,2.0,-1.0})for(int X:{-1,1})for(int Y:{-1,1})
 {
  const FTransform Frame(FRotator(0,Angle,0),FVector(400,-100,80),FVector(Scale,1.5,1));
  const FVector A=Frame.TransformPosition(FVector(10,20,30)),Cursor=Frame.TransformPosition(FVector(10+X*50,20+Y*80,900));
  const auto End=EHBDragAngleSnap::ResolveRectangle(A,Cursor,Frame,O);
  const FVector LocalA=Frame.InverseTransformPosition(A),LocalZ=Frame.InverseTransformPosition(End);
  const FVector Corner=Frame.TransformPosition(FVector(LocalZ.X,LocalA.Y,LocalA.Z));
  TestTrue(TEXT("Width in actual centimeters"),FMath::IsNearlyEqual(FVector::Distance(A,Corner),313.25,0.00001));
  TestTrue(TEXT("Depth in actual centimeters"),FMath::IsNearlyEqual(FVector::Distance(Corner,End),427.75,0.00001));
  TestTrue(TEXT("Drag quadrant retained"),(LocalZ.X-LocalA.X)*X>0&&(LocalZ.Y-LocalA.Y)*Y>0);
  TestTrue(TEXT("Same base plane"),FMath::IsNearlyEqual(LocalZ.Z,LocalA.Z,0.00001));
  TestTrue(TEXT("Repeated endpoint resolution stable"),EHBDragAngleSnap::ResolveRectangle(A,End,Frame,O).Equals(End,0.00001));
  TestTrue(TEXT("Ctrl bypass preserves cursor"),EHBDragAngleSnap::ResolveRectangle(A,Cursor,Frame,O,true).Equals(Cursor));
 }
 return true;
}
#endif
