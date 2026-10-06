#include "EHBSlabDisplayPlan.h"
#include "EHBRoomFinishMove.h"
#include "Core/EHBDisplayPartition.h"
#include "Geometry/EHBFloorContactGeometry.h"

bool FEHBSlabDisplayPlan::HasConstraintsInLayer(const AEHB_FloorSlab* Reference) const
{
 const auto Ref=Reference->GetElementLocalTransform();const auto Normal=Ref.GetUnitAxis(EAxis::Z);const auto Origin=Ref.TransformPosition(FVector(0,0,Reference->GetTopZ()));
 return Slabs.ContainsByPredicate([&](const auto& P)
 {
  if(P.Actor->FloorIndex!=Reference->FloorIndex||!P.Actor->DisplayPartition.IsActive())return false;
  const auto T=P.Actor->GetElementLocalTransform();return FVector::DotProduct(Normal,T.GetUnitAxis(EAxis::Z))>=1-1.e-10&&FMath::Abs(FVector::DotProduct(Normal,T.TransformPosition(FVector(0,0,P.Actor->GetTopZ()))-Origin))<=0.001;
 });
}

bool FEHBSlabDisplayPlan::Prepare(FName& Status)
  {
   auto Fail=[&](FName Why){Status=Why;return false;};TSet<int32> Processed;
   if(bPreserveSlabSources)for(auto& P:Slabs){if(P.bNew)return Fail(TEXT("OpeningSubdivisionRequiresPlan"));if(!P.bOverrideSources){P.Holes=P.Actor->LocalHoles;P.Cuts=P.Actor->CutOperations;}}
   for(auto* Reference:DisplayLayers)
   {
    TArray<FEHBLogicalSurfaceDefinition> ReferenceSurfaces;if(!Reference->QueryLogicalBaseSurfaces(ReferenceSurfaces,Status))return false;const auto& Ref=ReferenceSurfaces[0];
    TArray<int32> Indices;TMap<int32,FEHBLogicalSurfaceDefinition> Bases;
    for(int32 I=0;I<Slabs.Num();++I)
    {
     auto& P=Slabs[I];if(Processed.Contains(I)||P.Actor->FloorIndex!=Ref.FloorIndex)continue;
     TArray<FEHBLogicalSurfaceDefinition> Values;if(!P.Actor->QueryLogicalBaseSurfaces(Values,Status))return false;auto Base=Values[0];const auto N=Ref.PlaneToBuilding.GetUnitAxis(EAxis::Z);
     if(FVector::DotProduct(N,Base.PlaneToBuilding.GetUnitAxis(EAxis::Z))<1-1.e-10||FMath::Abs(FVector::DotProduct(N,Base.PlaneToBuilding.GetLocation()-Ref.PlaneToBuilding.GetLocation()))>0.001)continue;
     Base.ElementGuid=P.Identity();Base.Regions.Reset();
     if(bPreserveSlabSources){if(!P.Actor->BuildCandidateDisplayRegions(P.Polygon,P.Holes,P.Cuts,Base.Regions,Status,false))return false;}
     else {auto& R=Base.Regions.AddDefaulted_GetRef();for(const auto& V:P.Polygon)R.Boundary.Add({V.X,V.Y});}
     Bases.Add(I,MoveTemp(Base));Indices.Add(I);
    }
    if(Indices.IsEmpty())continue;
    auto Center=[&](int32 I){FVector V=FVector::ZeroVector;for(const auto& P:Slabs[I].Polygon)V+=Slabs[I].Actor->GetElementLocalTransform().TransformPosition(P);return V/Slabs[I].Polygon.Num();};
    Indices.Sort([&](int32 A,int32 B){const auto& PA=Slabs[A].Actor->DisplayPartition;const auto& PB=Slabs[B].Actor->DisplayPartition;const bool IA=Slabs[A].InheritsPriority(),IB=Slabs[B].InheritsPriority();if(IA!=IB)return IA;if(IA)return PA.Priority<PB.Priority;const auto CA=Center(A),CB=Center(B);return CA.X==CB.X?CA.Y<CB.Y:CA.X<CB.X;});
    int32 NextPriority=0;for(int32 I:Indices)if(Slabs[I].InheritsPriority()){const int32 P=Slabs[I].Actor->DisplayPartition.Priority;if(P==MAX_int32)return Fail(TEXT("DisplayPriorityExhausted"));NextPriority=FMath::Max(NextPriority,P+1);}
    TArray<FEHBDisplayPartitionInput> Inputs;
    for(int32 I:Indices)
    {
     auto& P=Slabs[I];auto& Input=Inputs.AddDefaulted_GetRef();Input.Base=Bases.FindChecked(I);
     if(!P.InheritsPriority()&&NextPriority==MAX_int32)return Fail(TEXT("DisplayPriorityExhausted"));
     Input.Priority=P.InheritsPriority()?P.Actor->DisplayPartition.Priority:NextPriority++;
     if(!(bPreserveSlabSources?P.Actor->BuildCandidateDisplayRegions(P.Polygon,P.Holes,P.Cuts,Input.RequestedDisplay,Status):P.Actor->BuildUnallocatedDisplayRegion(P.Polygon,Input.RequestedDisplay)))return Fail(TEXT("InvalidDisplayCandidate"));
    }
    TArray<FEHBLogicalSurfaceDefinition> Allocated;if(!FEHBDisplayPartition::Build(Inputs,Allocated,Status))return false;
    auto& Group=DisplayGroups.AddDefaulted_GetRef();Group.Indices=Indices;
    for(int32 I:Indices)
    {
     auto& P=Slabs[I];const auto* D=Allocated.FindByPredicate([&](const auto& V){return V.ElementGuid==P.Identity();});const auto* In=Inputs.FindByPredicate([&](const auto& V){return V.Base.ElementGuid==P.Identity();});if(!D||!In)return Fail(TEXT("MissingDisplayAllocation"));
     auto ToTops=[](const FEHBLogicalSurfaceDefinition& Surface){TArray<FEHBFloorSupportSurface> Result;for(const auto& R:Surface.Regions){auto& T=Result.AddDefaulted_GetRef();for(const auto& V:R.Boundary)T.OuterPolygon.Add(Surface.ToBuilding(V));for(const auto& H:R.Holes){auto& L=T.Holes.AddDefaulted_GetRef().LocalPolygon;for(const auto& V:H.Vertices)L.Add(Surface.ToBuilding(V));}}return Result;};
     auto Requested=In->Base;Requested.Regions=In->RequestedDisplay;FName CoverageStatus;
     // Verify unchanged neighbors with the group, without adding constraints
     // when their ordinary display already matches the allocated coverage.
     if(!P.bOverrideSources&&!P.Actor->DisplayPartition.IsActive()&&EHBRoomFinishMove::SameTopCoverage(ToTops(Requested),ToTops(*D),CoverageStatus))
     {
      Group.Expected.Append(ToTops(Requested));Processed.Add(I);continue;
     }
     if(!P.bOverrideSources&&!P.bNew&&P.Actor->DisplayPartition.IsActive()&&P.Polygon==P.Actor->LocalTopPolygon)
     {
      auto Existing=In->Base;Existing.Regions=P.Actor->DisplayPartition.Regions;
      if(EHBRoomFinishMove::SameTopCoverage(ToTops(Existing),ToTops(*D),CoverageStatus))
      {Group.Expected.Append(ToTops(Existing));Processed.Add(I);continue;}
     }
     P.Display.SourcePolygon=P.Polygon;P.Display.SourceExpansion=P.Actor->VisualExpansion;P.Display.Priority=In->Priority;P.Display.Regions=D->Regions;
     if(bPreserveSlabSources)P.Actor->CaptureDisplayPartitionSource(P.Polygon,P.Holes,P.Cuts,P.Display);
     if(!(bPreserveSlabSources?P.Actor->ValidatePartitionedSlabState(P.Polygon,P.Holes,P.Cuts,P.Display):P.Actor->ValidatePartitionedSlabOutline(P.Polygon,P.Display))){for(const auto& R:P.Display.Regions){FEHBLogicalSurfaceDefinition One;One.Regions={R};UE_LOG(LogTemp,Warning,TEXT("Invalid allocation region area %.9f vertices %d holes %d"),One.GetAreaCm2(),R.Boundary.Num(),R.Holes.Num());}UE_LOG(LogTemp,Warning,TEXT("Invalid allocated slab: regions %d source vertices %d expansion %.3f"),P.Display.Regions.Num(),P.Polygon.Num(),P.Display.SourceExpansion);return Fail(TEXT("InvalidAllocatedSlab"));}
     for(const auto& R:D->Regions){auto& T=Group.Expected.AddDefaulted_GetRef();for(const auto& V:R.Boundary)T.OuterPolygon.Add(D->ToBuilding(V));for(const auto& H:R.Holes){auto& Loop=T.Holes.AddDefaulted_GetRef().LocalPolygon;for(const auto& V:H.Vertices)Loop.Add(D->ToBuilding(V));}}
     Processed.Add(I);AuthoredElements.AddUnique(P.Identity());
    }
   }
   return true;
  }
bool FEHBSlabDisplayPlan::Verify(FName& Status) const
  {
   auto Regions=[](const TArray<FEHBFloorSupportSurface>& Surfaces){TArray<FEHBFloorFinishRegion> R;for(const auto& T:Surfaces){auto& V=R.AddDefaulted_GetRef();V.OuterPolygon=T.OuterPolygon;V.Holes=T.Holes;}return R;};
   for(const auto& Group:DisplayGroups)
   {
    TArray<FEHBFloorSupportSurface> Actual;double Sum=0;
    for(int32 I:Group.Indices){TArray<FEHBFloorSupportSurface> SlabTops;if(!FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(Slabs[I].Actor,SlabTops,Status))return false;double A=0;if(!FEHBFloorContactGeometry::MeasureArea(Regions(SlabTops),SlabTops,A,Status))return false;Sum+=A;Actual.Append(SlabTops);}
    double Union=0,Expected=0,Intersection=0;if(!FEHBFloorContactGeometry::MeasureArea(Regions(Actual),Actual,Union,Status)||!FEHBFloorContactGeometry::MeasureArea(Regions(Group.Expected),Group.Expected,Expected,Status)||!FEHBFloorContactGeometry::MeasureArea(Regions(Group.Expected),Actual,Intersection,Status))return false;
    if(FMath::Abs(Union-Sum)>0.01||FMath::Abs(Union-Expected)>0.01||FMath::Abs(Intersection-Expected)>0.01){UE_LOG(LogTemp,Warning,TEXT("Display coverage mismatch: sum %.9f union %.9f expected %.9f intersection %.9f"),Sum,Union,Expected,Intersection);Status=TEXT("AllocatedDisplayCoverageMismatch");return false;}
   }
   return true;
  }
