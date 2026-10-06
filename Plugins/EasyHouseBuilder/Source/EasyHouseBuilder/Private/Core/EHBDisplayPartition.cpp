#include "Core/EHBDisplayPartition.h"
#include "Core/EHBLogicalSurfaceAdjacency.h"
#include "ThirdParty/clipper/clipper.h"

namespace
{
 constexpr double Grid=1000.0;
 using namespace ClipperLib;
 Paths Convert(const FEHBLogicalSurfaceDefinition& S,const FTransform& Frame,bool OnlyHoles=false)
 {
  Paths Result;
  auto Add=[&](const TArray<FVector2D>& Loop,bool Outer)
  {
   Path P;for(const auto& V:Loop){const auto W=Frame.InverseTransformPosition(S.ToBuilding(V));P.emplace_back(FMath::RoundToInt64(W.X*Grid),FMath::RoundToInt64(W.Y*Grid));}
   if(Orientation(P)!=Outer)ReversePath(P);Result.push_back(MoveTemp(P));
  };
  for(const auto& R:S.Regions){if(!OnlyHoles)Add(R.Boundary,true);for(const auto& H:R.Holes)Add(H.Vertices,OnlyHoles);}
  return Result;
 }
 bool Clip(const Paths& A,const Paths& B,ClipType Type,Paths& Out)
 {
  Out.clear();if(A.empty())return true;Clipper C;C.AddPaths(A,ptSubject,true);C.AddPaths(B,ptClip,true);return C.Execute(Type,Out,pftNonZero,pftNonZero);
 }
 double AreaSum(const Paths& P){double A=0;for(const auto& L:P)A+=Area(L);return FMath::Abs(A)/(Grid*Grid);}
 bool Decode(const Paths& P,const FTransform& Frame,FEHBLogicalSurfaceDefinition& S)
 {
  Clipper C;C.AddPaths(P,ptSubject,true);PolyTree Tree;if(!C.Execute(ctUnion,Tree,pftNonZero,pftNonZero))return false;
  S.Regions.Reset();
  auto Loop=[&](const Path& L){Path Clean;CleanPolygon(L,Clean,1.415);TArray<FVector2D> V;for(const auto& Q:Clean){const auto W=S.PlaneToBuilding.InverseTransformPosition(Frame.TransformPosition(FVector(Q.X/Grid,Q.Y/Grid,0)));V.Add({W.X,W.Y});}return V;};
  for(auto* N=Tree.GetFirst();N;N=N->GetNext())if(!N->IsHole()&&N->Contour.size()>=3){auto& R=S.Regions.AddDefaulted_GetRef();R.Boundary=Loop(N->Contour);for(const auto* H:N->Childs)if(H->IsHole())R.Holes.AddDefaulted_GetRef().Vertices=Loop(H->Contour);}
  return true;
 }
}

bool FEHBDisplayPartition::MatchesWithinGrid(const FEHBLogicalSurfaceDefinition& A,const FEHBLogicalSurfaceDefinition& B,FName& Status)
{
 if(!A.Validate(Status)||!B.Validate(Status))return false;
 const auto Normal=A.PlaneToBuilding.GetUnitAxis(EAxis::Z);
 if(FVector::DotProduct(Normal,B.PlaneToBuilding.GetUnitAxis(EAxis::Z))<1-1.e-10||FMath::Abs(FVector::DotProduct(Normal,B.PlaneToBuilding.GetLocation()-A.PlaneToBuilding.GetLocation()))>0.001){Status=TEXT("DifferentDisplayPlanes");return false;}
 const auto AP=Convert(A,A.PlaneToBuilding),BP=Convert(B,A.PlaneToBuilding);
 auto Fits=[](const Paths& Subject,const Paths& Reference)
 {
  ClipperOffset Offset;Offset.AddPaths(Reference,jtMiter,etClosedPolygon);Paths Envelope,Outside;Offset.Execute(Envelope,2.0);
  return Clip(Subject,Envelope,ctDifference,Outside)&&AreaSum(Outside)<=0.01;
 };
 const bool Match=Fits(AP,BP)&&Fits(BP,AP);Status=Match?TEXT("Matched"):TEXT("DisplayCoverageOutsideClipTolerance");return Match;
}

bool FEHBDisplayPartition::Build(const TArray<FEHBDisplayPartitionInput>& Input,TArray<FEHBLogicalSurfaceDefinition>& Output,FName& Status,bool bAllowGridRoundTrip)
{
 Output.Reset();auto Fail=[&](FName Why){Status=Why;return false;};
 if(Input.IsEmpty())return Fail(TEXT("EmptyDisplayPartition"));
 TArray<const FEHBDisplayPartitionInput*> Sorted;TArray<FEHBLogicalSurfaceDefinition> Bases;
 for(const auto& I:Input){Sorted.Add(&I);Bases.Add(I.Base);}
 Sorted.Sort([](const auto& A,const auto& B){return A.Priority<B.Priority;});
 for(int32 I=1;I<Sorted.Num();++I)if(Sorted[I]->Priority==Sorted[I-1]->Priority)return Fail(TEXT("DuplicateDisplayPriority"));
 TArray<FEHBLogicalSurfaceSharedEdge> Shared;if(!FEHBLogicalSurfaceAdjacency::Build(Bases,Shared,Status))return false;
 const auto& First=Sorted[0]->Base;auto Frame=First.PlaneToBuilding;const auto Normal=Frame.GetUnitAxis(EAxis::Z);
 // Horizontal display layers use the same building grid as generated coverage
 // checks. An actor-rotated grid can turn a shared boundary into overlapping
 // slivers when each result is quantized again in building space.
 if(Normal.Equals(FVector::UpVector,1.e-10))Frame=FTransform(FVector(0,0,Frame.GetLocation().Z));
 Paths AllBase,ProtectedHoles;
 for(const auto* I:Sorted)
 {
  const auto& B=I->Base;
  if(B.BuildingGuid!=First.BuildingGuid||B.FloorIndex!=First.FloorIndex||FVector::DotProduct(Normal,B.PlaneToBuilding.GetUnitAxis(EAxis::Z))<1-1.e-10||FMath::Abs(FVector::DotProduct(Normal,B.PlaneToBuilding.GetLocation()-Frame.GetLocation()))>0.001)return Fail(TEXT("MixedDisplayLayer"));
  auto P=Convert(B,Frame),H=Convert(B,Frame,true);AllBase.insert(AllBase.end(),P.begin(),P.end());ProtectedHoles.insert(ProtectedHoles.end(),H.begin(),H.end());
 }
 Paths Claimed;TArray<FEHBLogicalSurfaceDefinition> Result;
 for(const auto* I:Sorted)
 {
  auto Display=I->Base;Display.Regions=I->RequestedDisplay;if(!Display.Validate(Status))return false;
  const auto Base=Convert(I->Base,Frame),Wanted=Convert(Display,Frame);Paths Missing,Extra,OutsideVoids,Available,Merged;
  if(!Clip(Base,Wanted,ctDifference,Missing))return Fail(TEXT("DisplayPartitionClipFailed"));
  if(AreaSum(Missing)>0.01)
  {
   // Revalidation after conversion from the shared clipping plane to an actor
   // frame can round each boundary twice. Bound that error in distance, never
   // by a growing area allowance. Normal planning remains strict.
   if(!bAllowGridRoundTrip)return Fail(TEXT("DisplayWouldDiscardBase"));
   ClipperOffset Offset;Offset.AddPaths(Wanted,jtMiter,etClosedPolygon);Paths Envelope;Offset.Execute(Envelope,2.0);
   if(!Clip(Base,Envelope,ctDifference,Missing)||AreaSum(Missing)>0.01)return Fail(TEXT("DisplayWouldDiscardBase"));
  }
  if(!Clip(Wanted,AllBase,ctDifference,Extra)||!Clip(Extra,ProtectedHoles,ctDifference,OutsideVoids)||!Clip(OutsideVoids,Claimed,ctDifference,Available))return Fail(TEXT("DisplayPartitionClipFailed"));
  Claimed.insert(Claimed.end(),Available.begin(),Available.end());Merged=Base;Merged.insert(Merged.end(),Available.begin(),Available.end());
  if(!Decode(Merged,Frame,Display))return Fail(TEXT("DisplayPartitionDecodeFailed"));
  if(!Display.Validate(Status))return false;
  // A display allocation is never authority for structural support.
  Display.bStructural=false;Display.bCanSupport=false;Display.Thickness=0;
  Result.Add(MoveTemp(Display));
 }
 Output=MoveTemp(Result);Status=TEXT("Ready");return true;
}
