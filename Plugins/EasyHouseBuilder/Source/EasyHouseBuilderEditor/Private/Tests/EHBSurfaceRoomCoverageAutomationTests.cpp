#include "Misc/AutomationTest.h"
#include "Algo/Reverse.h"
#include <limits>
#include "Core/EHBSurfaceRoomCoverage.h"
#include "Core/EHBWallNodeRooms.h"
#include "Geometry/EHBFloorContactGeometry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBSurfaceRoomCoverageTest,"EHB.Rooms.SurfaceCoverage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBSurfaceRoomCoverageTest::RunTest(const FString& Parameters)
{
 auto Rect=[](double X,double Y,double Z,double W,double H){return TArray<FVector>{{X,Y,Z},{X+W,Y,Z},{X+W,Y+H,Z},{X,Y+H,Z}};};
 const FGuid Surface(10,0,0,1),LeftId(20,0,0,1),RightId(20,0,0,2);
 FEHBNodeRoomBoundary Left,Right;Left.RoomGuid=LeftId;Left.FloorIndex=1;Left.Polygon=Rect(0,0,0,100,100);Left.Area=10000;Right=Left;Right.RoomGuid=RightId;Right.Polygon=Rect(100,0,0,100,100);
 auto Region=[&](double X,double Y,double W,double H){FEHBFloorFinishRegion R;R.OuterPolygon=Rect(X,Y,300,W,H);return R;};
 auto Run=[&](const TArray<FEHBFloorFinishRegion>& Regions,const TArray<FEHBNodeRoomBoundary>& Rooms,double Total,double Outside,const TArray<double>& Areas)
 {
  FEHBSurfaceRoomCoverage Out;FName Status;const auto Original=Regions;
  if(!TestTrue(*FString::Printf(TEXT("Coverage area %.0f"),Total),FEHBSurfaceRoomCoverageSolver::Build(Surface,1,Regions,Rooms,Out,Status)))return false;
  TestEqual(TEXT("Surface identity"),Out.ElementGuid,Surface);TestEqual(TEXT("Assigned floor"),Out.FloorIndex,1);TestTrue(TEXT("Union area measured in square centimeters"),FMath::IsNearlyEqual(Out.AreaCm2,Total,0.01));TestTrue(TEXT("Outside area is explicit"),FMath::IsNearlyEqual(Out.OutsideAreaCm2,Outside,0.01));TestEqual(TEXT("Room coverage count"),Out.Rooms.Num(),Areas.Num());
  for(int32 I=0;I<Areas.Num()&&I<Out.Rooms.Num();++I){TestTrue(TEXT("Room intersection area"),FMath::IsNearlyEqual(Out.Rooms[I].AreaCm2,Areas[I],0.01));TestTrue(TEXT("Surface share"),FMath::IsNearlyEqual(Out.Rooms[I].SurfaceFraction,Areas[I]/Total,0.00001));TestTrue(TEXT("Room share"),FMath::IsNearlyEqual(Out.Rooms[I].RoomFraction,Areas[I]/10000,0.00001));}
  for(int32 I=0;I<Regions.Num();++I)TestEqual(TEXT("No input coordinate mutation"),Regions[I].OuterPolygon,Original[I].OuterPolygon);
  auto Reordered=Rooms;Algo::Reverse(Reordered);FEHBSurfaceRoomCoverage Again;TestTrue(TEXT("Room input order independent"),FEHBSurfaceRoomCoverageSolver::Build(Surface,1,Regions,Reordered,Again,Status));TestEqual(TEXT("Same ordered result count"),Again.Rooms.Num(),Out.Rooms.Num());for(int32 I=0;I<Again.Rooms.Num();++I){TestEqual(TEXT("Stable room ordering"),Again.Rooms[I].RoomGuid,Out.Rooms[I].RoomGuid);TestEqual(TEXT("Same areas after reorder"),Again.Rooms[I].AreaCm2,Out.Rooms[I].AreaCm2);}
  // Shared clipping path: area-only mode must agree with contact mode, which
  // additionally triangulates an interior point. Host planes use the same Z.
  TArray<FEHBFloorSupportSurface> Tops;for(const auto& R:Regions){auto& T=Tops.AddDefaulted_GetRef();T.OuterPolygon=R.OuterPolygon;T.Holes=R.Holes;}
  double Area=0;FEHBFloorContact Contact;TestTrue(TEXT("Area-only solve"),FEHBFloorContactGeometry::MeasureArea(Regions,Tops,Area,Status));TestTrue(TEXT("Contact-point solve"),FEHBFloorContactGeometry::Build(Regions,Tops,Contact,Status));TestEqual(TEXT("Area-only and contact area exactly agree"),Area,Contact.Area);return true;
 };
 if(!Run({Region(0,0,100,100)},{Left,Right},10000,0,{10000}))return false;
 if(!Run({Region(50,0,100,100)},{Left,Right},10000,0,{5000,5000}))return false;
 if(!Run({Region(-50,0,100,100)},{Left,Right},10000,5000,{5000}))return false;
 if(!Run({Region(-100,0,100,100)},{Left,Right},10000,10000,{}))return false;
 if(!Run({Region(0,0,100,100)},{},10000,10000,{}))return false;
 if(!Run({Region(0,0,150,100),Region(50,0,150,100)},{Left,Right},20000,0,{10000,10000}))return false;
 auto Hole=Region(0,0,200,100);Hole.Holes.AddDefaulted_GetRef().LocalPolygon=Rect(50,25,300,100,50);
 if(!Run({Hole},{Left,Right},15000,0,{7500,7500}))return false;
 auto Upstairs=Left;Upstairs.RoomGuid=FGuid(20,0,0,3);Upstairs.FloorIndex=2;
 if(!Run({Region(0,0,100,100)},{Right,Upstairs},10000,10000,{}))return false;
 auto Reversed=Region(50,0,100,100);Algo::Reverse(Reversed.OuterPolygon);if(!Run({Reversed},{Right,Left},10000,0,{5000,5000}))return false;
 FEHBSurfaceRoomCoverage Out;FName Status;
 auto Reject=[&](TArray<FEHBFloorFinishRegion> Regions,TArray<FEHBNodeRoomBoundary> Rooms,FName Expected)
 {Out.ElementGuid=Surface;Out.AreaCm2=99;Out.Rooms.AddDefaulted();TestFalse(TEXT("Invalid coverage query refused"),FEHBSurfaceRoomCoverageSolver::Build(Surface,1,Regions,Rooms,Out,Status));TestEqual(TEXT("Specific coverage refusal"),Status,Expected);TestTrue(TEXT("Failure clears previous result"),!Out.ElementGuid.IsValid()&&Out.AreaCm2==0&&Out.Rooms.IsEmpty());};
 Reject({}, {Left},TEXT("InvalidSurfaceCoverageInput"));Reject({Region(0,0,100,100)},{Left,Left},TEXT("InvalidCoverageRoomIdentity"));auto Bad=Left;Bad.RoomGuid={};Reject({Region(0,0,100,100)},{Bad},TEXT("InvalidCoverageRoomIdentity"));Bad=Right;Bad.Polygon=Rect(90,0,0,100,100);Reject({Region(0,0,100,100)},{Left,Bad},TEXT("OverlappingCoverageRooms"));
 auto BadRegion=Region(0,0,100,100);BadRegion.OuterPolygon[0].X=std::numeric_limits<double>::infinity();Reject({BadRegion},{Left},TEXT("InvalidSurfaceFootprint"));
 return true;
}
