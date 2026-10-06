#include "Misc/AutomationTest.h"
#include "EHBRoomFinishPartition.h"
#include "Algo/Reverse.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBFinishPartitionValueTest,"EHB.Floors.MaterialPartitionValue",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBFinishPartitionValueTest::RunTest(const FString& Parameters)
{
 auto Rect=[](double X0,double Y0,double X1,double Y1,double Z=300){return TArray<FVector>{{X0,Y0,Z},{X1,Y0,Z},{X1,Y1,Z},{X0,Y1,Z}};};
 auto Area=[](const auto& P){double A=0;for(int32 I=0;I<P.Num();++I)A+=P[I].X*P[(I+1)%P.Num()].Y-P[I].Y*P[(I+1)%P.Num()].X;return FMath::Abs(A)*0.5;};
 const FGuid Left=FGuid::NewGuid(),Right=FGuid::NewGuid();const auto Domain=Rect(10,10,990,490);FName Status;TMap<FGuid,TArray<FVector>> Out;
 for(int32 Shape=0;Shape<3;++Shape)
 {
  TArray<EHBRoomFinishPartition::FSource> Sources;
  if(Shape==0)Sources={{Left,Rect(0,0,600,500,0),Rect(10,10,590,490)},{Right,Rect(600,0,1000,500,0),Rect(610,10,990,490)}};
  if(Shape==1)Sources={{Left,{{0,0,0},{500,0,0},{700,500,0},{0,500,0}},{{10,10,300},{494,10,300},{686,490,300},{10,490,300}}},{Right,{{500,0,0},{1000,0,0},{1000,500,0},{700,500,0}},{{514,10,300},{990,10,300},{990,490,300},{706,490,300}}}};
  if(Shape==2)Sources={{Left,{{0,0,0},{600,0,0},{600,200,0},{400,200,0},{400,500,0},{0,500,0}},Rect(10,10,390,490)},{Right,{{600,0,0},{1000,0,0},{1000,500,0},{400,500,0},{400,200,0},{600,200,0}},Rect(610,10,990,490)}};
  if(!TestTrue(*Status.ToString(),EHBRoomFinishPartition::Build(Domain,Sources,Out,Status)))return false;
  TestEqual(TEXT("Each actor owns one stable partition"),Out.Num(),2);TestTrue(TEXT("Independent shoelace sum covers domain"),FMath::IsNearlyEqual(Area(Out.FindChecked(Left))+Area(Out.FindChecked(Right)),470400.0,0.01));
  if(Shape<2){TestTrue(TEXT("Left receives original room side of removed strip"),FMath::IsNearlyEqual(Area(Out.FindChecked(Left)),283200.0,0.01));TestTrue(TEXT("Right receives its side of removed strip"),FMath::IsNearlyEqual(Area(Out.FindChecked(Right)),187200.0,0.01));}
  const auto Expected=Out;Algo::Reverse(Sources);for(auto& S:Sources){Algo::Reverse(S.OwnerRoomPolygon);Algo::Reverse(S.OriginalPolygon);}if(!TestTrue(TEXT("Actor order and winding do not change ownership"),EHBRoomFinishPartition::Build(Domain,Sources,Out,Status)))return false;for(const auto& P:Expected)TestEqual(TEXT("Deterministic partition vertices"),Out.FindChecked(P.Key),P.Value);
 }
 TArray<EHBRoomFinishPartition::FSource> Valid={{Left,Rect(0,0,600,500,0),Rect(10,10,590,490)},{Right,Rect(600,0,1000,500,0),Rect(610,10,990,490)}};
 auto Refuse=[&](const auto& Sources,const TCHAR* Reason){TestFalse(Reason,EHBRoomFinishPartition::Build(Domain,Sources,Out,Status));TestTrue(TEXT("Failed partition exposes no partial output"),Out.IsEmpty());};
 auto Bad=Valid;Bad[1].ElementGuid=Left;Refuse(Bad,TEXT("Duplicate identity"));
 Bad=Valid;for(auto& P:Bad[1].OriginalPolygon)P.Z+=2;Refuse(Bad,TEXT("Different physical plane"));
 Bad=Valid;Bad[0].OriginalPolygon=Rect(10,10,620,490);Refuse(Bad,TEXT("Would discard old surface"));
 Bad=Valid;Bad[1].OwnerRoomPolygon=Rect(500,0,1000,500);Refuse(Bad,TEXT("Overlapping ownership"));
 Bad=Valid;Bad[0].OwnerRoomPolygon=Rect(0,0,595,500);Refuse(Bad,TEXT("Unallocated wall strip"));
 return true;
}
#endif
