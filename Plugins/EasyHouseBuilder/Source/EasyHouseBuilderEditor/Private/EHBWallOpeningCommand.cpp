#include "EHBWallOpeningCommand.h"
#include "Toolsets/EHBBuildingToolset.h"
#include "Actors/EHB_Wall.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Pillar.h"
#include "Core/EHBFloorAssignmentPlan.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "EHB_Building.h"
#include "Core/EHBPreparedWallOpening.h"
#include "Core/EHBChangeNotificationBatch.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace
{
 FEHBFloorAssignmentState FloorState(const AEHBElementActorBase* E)
 {
  FEHBFloorAssignmentState S;S.ElementGuid=E->ElementGuid;S.Type=E->ElementType;S.FloorIndex=E->FloorIndex;
  S.Role=E->FloorRole;S.Policy=E->FloorAssignmentPolicy;S.Source=E->FloorAssignmentSource;S.Candidates=E->ConflictingFloorCandidates;S.Conflict=E->bFloorAssignmentConflict;return S;
 }
 bool SameFloor(const FEHBFloorAssignmentState& A,const FEHBFloorAssignmentState& B)
 {return A.ElementGuid==B.ElementGuid&&A.FloorIndex==B.FloorIndex&&A.Role==B.Role&&A.Policy==B.Policy&&A.Source==B.Source&&A.Candidates==B.Candidates&&A.Conflict==B.Conflict;}
 struct FDerivedFloorEdit{AEHBElementActorBase* Actor=nullptr;FEHBFloorAssignmentState Next;bool Dirty=false;};
 bool RevalidateFloorChain(AEHBBuildingActorBase* B,TArray<FEHBElementRelation>& Replaced,const TArray<FGuid>& Removed,FName& Status)
 {
  TSet<FGuid> Seen;TArray<FGuid> Pending;
  for(const auto& R:B->ElementRelations)if(R.bEnabled&&R.bAffectsFloorAssignment
   &&(Removed.Contains(R.RelationGuid)||Replaced.ContainsByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;})))Pending.AddUnique(R.Target.ElementGuid);
  for(int32 I=0;I<Pending.Num();++I)
  {
   const FGuid Id=Pending[I];if(Seen.Contains(Id))continue;Seen.Add(Id);
   for(const auto& R:B->ElementRelations)if(R.bEnabled&&R.bAffectsFloorAssignment&&R.Source.Kind==EEHBRelationEndpointKind::BuildingElement&&R.Source.ElementGuid==Id&&R.Target.Kind==EEHBRelationEndpointKind::BuildingElement)
   {
    Pending.AddUnique(R.Target.ElementGuid);
    if(!B->IsElementRelationStale(R.RelationGuid)||Removed.Contains(R.RelationGuid)||Replaced.ContainsByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;}))continue;
    auto* Slab=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(R.Source.ElementGuid));auto* Pillar=Cast<AEHB_Pillar>(B->FindElementActorByGuid(R.Target.ElementGuid));
    TArray<FEHBFloorSupportSurface> Tops;TArray<FEHBFloorFinishRegion> Bottom;FEHBFloorContact Contact;
    if(R.Type!=EEHBElementRelationType::StructuralSupport||R.Source.SurfaceKind!=EEHBElementSurfaceKind::Top||R.Target.SurfaceKind!=EEHBElementSurfaceKind::Bottom
     ||!Slab||Slab->GetClass()!=AEHB_FloorSlab::StaticClass()||!Pillar
     ||!FEHBFloorContactGeometry::CapturePillarBottom(Pillar,Bottom,Status)||!FEHBFloorContactGeometry::CaptureHorizontalTops(Slab,Tops,Status)
     ||!FEHBFloorContactGeometry::Build(Bottom,Tops,Contact,Status)||Contact.Area<=0||FMath::Abs(Contact.Area-R.ContactArea)>0.01)
    {Status=TEXT("StaleOpeningFloorDependency");return false;}
    auto Fresh=R;Fresh.ContactArea=Contact.Area;Fresh.ContactPoint=Contact.Point;Fresh.ContactNormal=FVector::UpVector;Replaced.Add(MoveTemp(Fresh));
   }
  }
  return true;
 }
 bool PlanDerivedFloors(AEHBBuildingActorBase* B,const TArray<FEHBElementRelation>& Replaced,const TArray<FGuid>& Removed,TArray<FDerivedFloorEdit>& Out,FName& Status)
 {
  Out.Reset();TArray<FEHBFloorAssignmentState> Source,Before,After;
  for(auto* E:B->QueryElements(FEHBElementQuery()))Source.Add(FloorState(E));
  TArray<FEHBElementRelation> Prior,Next;
  for(const auto& R:B->ElementRelations)
  {
   const auto* Replacement=Replaced.FindByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;});
   // Replanned contacts have independently validated current geometry.
   if(!Replacement&&!Removed.Contains(R.RelationGuid)&&B->IsElementRelationStale(R.RelationGuid))continue;
   Prior.Add(R);if(!Removed.Contains(R.RelationGuid))Next.Add(Replacement?*Replacement:R);
  }
  if(!FEHBFloorAssignmentPlan::Build(Source,Prior,Before,Status)||!FEHBFloorAssignmentPlan::Build(Source,Next,After,Status))return false;
  for(const auto& S:After)
  {
   const auto* Old=Before.FindByPredicate([&](const auto& V){return V.ElementGuid==S.ElementGuid;});
   if(!Old){Status=TEXT("OpeningFloorPlanIdentityMismatch");return false;}if(SameFloor(*Old,S))continue;
   auto* E=B->FindElementActorByGuid(S.ElementGuid);if(!E||!SameFloor(FloorState(E),*Old)){Status=TEXT("OpeningFloorAssignmentSourceMismatch");return false;}
   bool Supported=false;
   if(auto* Slab=Cast<AEHB_FloorSlab>(E))Supported=E->GetClass()==AEHB_FloorSlab::StaticClass()&&!Slab->bHasRoomFillAnchor&&!Slab->RoomFillLoopGuid.IsValid();
   if(auto* Pillar=Cast<AEHB_Pillar>(E))Supported=E->GetClass()==AEHB_Pillar::StaticClass()&&!B->FindNodeForPhysicalPillar(Pillar->ElementGuid).IsValid()
    &&!B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.Type==EEHBElementRelationType::TopologyConnection&&R.InvolvesElement(E->ElementGuid);});
   if(!Supported){Status=TEXT("OpeningFloorTopologyRequiresPlan");return false;}
   auto& P=Out.AddDefaulted_GetRef();P.Actor=E;P.Next=S;P.Dirty=E->GetPackage()->IsDirty();
  }
  // A stale outgoing assignment edge cannot be silently omitted: its target
  // could otherwise retain a floor derived from the support being withdrawn.
  for(const auto& P:Out)for(const auto& R:B->ElementRelations)
   if(R.bEnabled&&R.bAffectsFloorAssignment&&R.Source.ElementGuid==P.Actor->ElementGuid
    &&B->IsElementRelationStale(R.RelationGuid)&&!Removed.Contains(R.RelationGuid)
    &&!Replaced.ContainsByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;}))
   {Status=TEXT("StaleOpeningFloorDependency");return false;}
  return true;
 }
 bool FailAt(int32 Phase)
 {
#if WITH_DEV_AUTOMATION_TESTS
  if(FEHBWallOpeningCommand::FailurePhase==Phase){FEHBWallOpeningCommand::FailurePhase=0;return true;}
#endif
  return false;
 }
}
FEHBToolsetOperationResult FEHBWallOpeningCommand::Execute(AEHB_Wall* W,const TArray<FEHBCutOperation>& Cuts,int32 Graph,int32 Geometry,bool Preview)
{
 FEHBPreparedWallOpening Candidate;
 // Reprepare every command; never accept a caller-supplied mutable preview token.
 auto Result=UEHBBuildingToolset::PreviewWallOpenings(W,Cuts,Graph,Geometry,Candidate);
 if(!Result.bSucceeded)return Result;
 Result.bSucceeded=false;auto Fail=[&](FName Why){Result.Message=Why.ToString();return Result;};
 auto* B=W->OwningBuilding.Get();if(B->GetClass()!=AEHB_Building::StaticClass())return Fail(TEXT("CustomOpeningBuildingRequiresPlan"));
 FEHBPreparedWallOpening Previous;FName Status;
 if(!W->PrepareSurfaceOpening(W->CutOperations,Previous,Status))return Fail(Status);
 // A Top relation can refer to a window sill, not only the wall crown.
 // Draft complete floor contacts before changing any geometry or graph state.
 struct FFinishPlan{AEHB_Floor* Floor=nullptr;TArray<FEHBFloorFinishRegion> Coverage;TArray<FEHBElementRelation> Relations;bool Dirty=false;};
 TArray<FFinishPlan> Finishes;TMap<FGuid,TArray<FEHBFloorSupportSurface>> CandidateTops;CandidateTops.Add(W->ElementGuid,Candidate.HorizontalTops);
 FBox ContactBounds(ForceInit);
 for(const auto* Tops:{&Previous.HorizontalTops,&Candidate.HorizontalTops})for(const auto& Top:*Tops)for(const auto& Point:Top.OuterPolygon)ContactBounds+=Point;
 for(auto* Element:B->QueryElements(FEHBElementQuery()))if(auto* Floor=Cast<AEHB_Floor>(Element))
 {
  FFinishPlan Plan;Plan.Floor=Floor;Plan.Dirty=Floor->GetPackage()->IsDirty();Plan.Coverage=Floor->FloorRegions;
  if(Plan.Coverage.IsEmpty()&&Floor->LocalFloorPolygon.Num()>=3)Plan.Coverage.AddDefaulted_GetRef().OuterPolygon=Floor->LocalFloorPolygon;
  const bool WasLinked=B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.Type==EEHBElementRelationType::SurfaceFinish&&R.Source.ElementGuid==W->ElementGuid&&R.Target.ElementGuid==Floor->ElementGuid;});
  // A disjoint authored footprint cannot gain or lose contact with this wall.
  // Existing graph dependencies are always validated, even outside the bounds.
  // Use actual frames and include holes; unknown/invalid bounds must not skip.
  FBox FloorBounds(ForceInit);bool Finite=true;
  auto Include=[&](const FVector& Point){const auto P=B->GetActorTransform().InverseTransformPosition(Floor->GetActorTransform().TransformPosition(Point));if(P.ContainsNaN())Finite=false;else FloorBounds+=P;};
  for(const auto& Region:Plan.Coverage){for(const auto& P:Region.OuterPolygon)Include(P);for(const auto& H:Region.Holes)for(const auto& P:H.LocalPolygon)Include(P);}
  if(!WasLinked&&Finite&&FloorBounds.IsValid&&ContactBounds.IsValid&&!FloorBounds.Intersect(ContactBounds.ExpandBy(1.001)))continue;
  if(Floor->GetClass()!=AEHB_Floor::StaticClass()||!Floor->BuildCurrentSurfaceFinishRelationPlan(Plan.Coverage,Plan.Relations,Status,&CandidateTops))return Fail(TEXT("OpeningFloorContactRequiresPlan"));
  if(WasLinked||Plan.Relations.ContainsByPredicate([&](const auto& R){return R.Source.ElementGuid==W->ElementGuid;}))Finishes.Add(MoveTemp(Plan));
 }
 // Remaining physical dependencies still require explicit host-specific plans.
 TArray<FEHBElementRelation> Relations;
 TArray<FGuid> LostContacts;
 bool ReplanFloors=false;
 for(const auto& R:B->ElementRelations)if(R.InvolvesElement(W->ElementGuid))
 {
  if((R.Type==EEHBElementRelationType::PhysicalContact||R.Type==EEHBElementRelationType::StructuralSupport)
   &&R.Target.Kind==EEHBRelationEndpointKind::BuildingElement&&R.Target.ElementGuid==W->ElementGuid&&R.Target.SurfaceKind==EEHBElementSurfaceKind::Bottom
   &&R.Source.Kind==EEHBRelationEndpointKind::BuildingElement&&R.Source.SurfaceKind==EEHBElementSurfaceKind::Top)
  {
   auto* Source=B->FindElementActorByGuid(R.Source.ElementGuid);TArray<FEHBFloorSupportSurface> Tops;FEHBFloorContact OldContact,NewContact;
   auto Solve=[&](const TArray<FEHBFloorFinishRegion>& Bottom,FEHBFloorContact& Contact){if(Bottom.IsEmpty()){Contact={};return true;}return FEHBFloorContactGeometry::Build(Bottom,Tops,Contact,Status);};
   const bool Supported=Source&&(Source->GetClass()==AEHB_FloorSlab::StaticClass()||(Source->GetClass()==AEHB_Pillar::StaticClass()&&CastChecked<AEHB_Pillar>(Source)->ShapeType==EEHBPillarShapeType::Polygon));
   if(!Supported||!FEHBFloorContactGeometry::CaptureHorizontalTops(Source,Tops,Status)
    ||!Solve(Previous.HorizontalBottoms,OldContact)||!Solve(Candidate.HorizontalBottoms,NewContact))
   {UE_LOG(LogTemp,Warning,TEXT("Opening bottom contact plan refused: %s (old=%d new=%d tops=%d)"),*Status.ToString(),Previous.HorizontalBottoms.Num(),Candidate.HorizontalBottoms.Num(),Tops.Num());return Fail(TEXT("OpeningBottomContactRequiresPlan"));}
   if(!R.bEnabled||FMath::Abs(OldContact.Area-R.ContactArea)>0.01)return Fail(TEXT("OpeningBottomContactSourceMismatch"));
   ReplanFloors|=R.Type==EEHBElementRelationType::StructuralSupport&&R.bAffectsFloorAssignment;
   if(NewContact.Area<=0)LostContacts.Add(R.RelationGuid);else{auto Next=R;Next.ContactArea=NewContact.Area;Next.ContactPoint=NewContact.Point;Next.ContactNormal=FVector::UpVector;Relations.Add(MoveTemp(Next));}
   continue;
  }
  // This adapter revalidates both live source domains, including after load-time
  // construction changes revision counters. Unknown dependencies remain strict.
  if((R.Type==EEHBElementRelationType::PhysicalContact||R.Type==EEHBElementRelationType::StructuralSupport)&&R.Source.Kind==EEHBRelationEndpointKind::BuildingElement&&R.Source.ElementGuid==W->ElementGuid&&R.Source.SurfaceKind==EEHBElementSurfaceKind::Top&&R.Target.Kind==EEHBRelationEndpointKind::BuildingElement&&R.Target.SurfaceKind==EEHBElementSurfaceKind::Bottom)
  {
   auto* Target=B->FindElementActorByGuid(R.Target.ElementGuid);
   TArray<FEHBFloorFinishRegion> Bottom;FEHBFloorContact OldContact,NewContact;
   const bool Captured=Cast<AEHB_FloorSlab>(Target)?FEHBFloorContactGeometry::CaptureSlabBottom(Cast<AEHB_FloorSlab>(Target),Bottom,Status)
    :FEHBFloorContactGeometry::CapturePillarBottom(Cast<AEHB_Pillar>(Target),Bottom,Status);
   if(!Captured
    ||!FEHBFloorContactGeometry::Build(Bottom,Previous.HorizontalTops,OldContact,Status)
    ||!FEHBFloorContactGeometry::Build(Bottom,Candidate.HorizontalTops,NewContact,Status))return Fail(TEXT("OpeningPhysicalContactRequiresPlan"));
   if(!R.bEnabled||FMath::Abs(OldContact.Area-R.ContactArea)>0.01)return Fail(TEXT("OpeningPhysicalContactSourceMismatch"));
   ReplanFloors|=R.Type==EEHBElementRelationType::StructuralSupport&&R.bAffectsFloorAssignment;
   if(NewContact.Area<=0)LostContacts.Add(R.RelationGuid);
   else{auto Next=R;Next.ContactArea=NewContact.Area;Next.ContactPoint=NewContact.Point;Next.ContactNormal=FVector::UpVector;Relations.Add(MoveTemp(Next));}
   continue;
  }
  if(B->IsElementRelationStale(R.RelationGuid))return Fail(TEXT("StaleOpeningDependency"));
  if(R.Type==EEHBElementRelationType::SurfaceFinish)
  {
   if(!Finishes.ContainsByPredicate([&](const auto& P){return R.Source.ElementGuid==W->ElementGuid&&R.Target.ElementGuid==P.Floor->ElementGuid;}))return Fail(TEXT("OpeningFinishDependencyRequiresPlan"));
   continue;
  }
  if(R.Type!=EEHBElementRelationType::TopologyConnection)
  {
   if(Previous.bTouchesHeightBoundary||Candidate.bTouchesHeightBoundary)return Fail(TEXT("OpeningHeightDependencyRequiresPlan"));
   if(R.Type!=EEHBElementRelationType::StructuralSupport&&R.Type!=EEHBElementRelationType::PhysicalContact&&R.Type!=EEHBElementRelationType::SurfaceFinish)return Fail(TEXT("OpeningDependencyRequiresPlan"));
   for(const auto* E:{&R.Source,&R.Target})if(E->Kind==EEHBRelationEndpointKind::BuildingElement&&E->ElementGuid==W->ElementGuid&&E->SurfaceKind!=EEHBElementSurfaceKind::Top&&E->SurfaceKind!=EEHBElementSurfaceKind::Bottom)return Fail(TEXT("OpeningSideDependencyRequiresPlan"));
  }
  Relations.Add(R);
 }
 TArray<FDerivedFloorEdit> FloorEdits;if(ReplanFloors&&(!RevalidateFloorChain(B,Relations,LostContacts,Status)||!PlanDerivedFloors(B,Relations,LostContacts,FloorEdits,Status)))return Fail(Status);
 TArray<AActor*> Children;W->GetAttachedActors(Children);if(!Children.IsEmpty())return Fail(TEXT("OpeningChildRequiresPlan"));
 TArray<UEHBGeneratedMeshComponent*> Meshes{W->LeftWallMeshComponent,W->RightWallMeshComponent,W->CapMeshComponent};
 for(auto* Mesh:Meshes)if(!IsValid(Mesh)||Mesh->GetOwner()!=W||!Mesh->GetRelativeTransform().Equals(FTransform::Identity)||Mesh->GetNumSections()!=1||!Mesh->GetProcMeshSection(0))return Fail(TEXT("OpeningComponentsRequirePlan"));
 TArray<FGuid> Rooms;for(const auto& R:B->GetClosedLoopsByWallGuid(W->ElementGuid))Rooms.AddUnique(R.LoopGuid);
 for(const auto& P:Finishes)if(P.Floor->RoomLoopGuid.IsValid())Rooms.AddUnique(P.Floor->RoomLoopGuid);
 bool Same=Cuts.Num()==W->CutOperations.Num();if(Same)for(int32 I=0;I<Cuts.Num();++I)Same&=FEHBCutOperation::StaticStruct()->CompareScriptStruct(&Cuts[I],&W->CutOperations[I],0);
 if(Preview||Same){Result.bSucceeded=true;Result.Message=Same?TEXT("NoChange"):TEXT("Ready");return Result;}
 const bool Dirty=B->GetPackage()->IsDirty(),WallDirty=W->GetPackage()->IsDirty();
 FEHBChangeNotificationBatch Batch(*B);if(!Batch.IsActive())return Fail(TEXT("ChangeNotificationBusy"));bool Applied=false;
 {
  FScopedTransaction Transaction(NSLOCTEXT("EasyHouseBuilder","EditWallOpenings","Edit Wall Openings"));
  B->SetFlags(RF_Transactional);B->Modify();W->SetFlags(RF_Transactional);W->Modify();
  for(const auto& P:Finishes){P.Floor->SetFlags(RF_Transactional);P.Floor->Modify();}
  for(const auto& P:FloorEdits){P.Actor->SetFlags(RF_Transactional);P.Actor->Modify();}
  for(auto* Mesh:Meshes){Mesh->SetFlags(RF_Transactional);Mesh->Modify();}
  Applied=W->ApplyPreparedSurfaceOpening(Candidate,Status);
  if(Applied)
  {
   W->NotifyElementGeometryChanged(true);
   for(FGuid Id:LostContacts)if(!B->RemoveElementRelation(Id)){Applied=false;Status=TEXT("OpeningContactRemovalFailed");break;}
   if(Applied)for(const auto& R:Relations)if(B->AddOrUpdateElementRelation(R)!=R.RelationGuid){Applied=false;Status=TEXT("OpeningDependencyPublicationFailed");break;}
  }
  for(const auto& P:Finishes)
  {
   if(!Applied)break;
   TArray<FEHBElementRelation> Actual;
   if(!P.Floor->BuildCurrentSurfaceFinishRelationPlan(P.Coverage,Actual,Status)||Actual.Num()!=P.Relations.Num()){Applied=false;Status=TEXT("OpeningFloorContactMismatch");break;}
   TArray<FGuid> Next;
   for(auto R:P.Relations)
   {
    const auto* A=Actual.FindByPredicate([&](const auto& V){return V.Source.IsEquivalentTo(R.Source)&&V.Target.IsEquivalentTo(R.Target);});
    if(!A||FMath::Abs(A->ContactArea-R.ContactArea)>0.01){Applied=false;Status=TEXT("OpeningFloorContactMismatch");break;}
    R.ContactPoint=A->ContactPoint;R.ContactNormal=A->ContactNormal;R.ContactArea=A->ContactArea;
    if(B->AddOrUpdateElementRelation(R,true)!=R.RelationGuid){Applied=false;Status=TEXT("OpeningFloorContactPublicationFailed");break;}Next.Add(R.RelationGuid);
   }
   if(!Applied)break;
   for(FGuid Id:P.Floor->SurfaceFinishRelationGuids)if(!Next.Contains(Id)&&!B->RemoveElementRelation(Id)){Applied=false;Status=TEXT("OpeningFloorContactRemovalFailed");break;}
   if(Applied){P.Floor->SurfaceFinishRelationGuids=MoveTemp(Next);P.Floor->MarkPackageDirty();}
  }
  if(Applied)for(const auto& P:FloorEdits)
  {
   if(P.Next.Source==EEHBFloorAssignmentSource::Unassigned)P.Actor->ClearDerivedFloorAssignment();
   else P.Actor->ApplyDerivedFloorAssignment(P.Next.FloorIndex,P.Next.Role,P.Next.Candidates);
   B->RegisterElementActor(P.Actor);
   if(!SameFloor(FloorState(P.Actor),P.Next)){Applied=false;Status=TEXT("OpeningFloorAssignmentPublicationMismatch");break;}
  }
  if(Applied&&FailAt(1)){Applied=false;Status=TEXT("InjectedOpeningDependencyFailure");}
  TArray<FGuid> Changed{W->ElementGuid};for(const auto& P:FloorEdits)Changed.AddUnique(P.Actor->ElementGuid);
  if(Applied){Applied=Batch.RecordCommittedEdit(TEXT("EditWallOpenings"),{},Rooms,Changed);if(!Applied)Status=TEXT("OpeningReceiptPublicationFailed");}
  if(Applied&&FailAt(2)){Applied=false;Status=TEXT("InjectedOpeningReceiptFailure");}
  if(Applied){W->SynchronizePlannedEditorMove();W->MarkPackageDirty();B->MarkPackageDirty();}
 }
 if(!Applied)
 {
  const bool Restored=GEditor->UndoTransaction(false);
  if(Restored){Batch.Rollback();for(const auto& P:Finishes)P.Floor->GetPackage()->SetDirtyFlag(P.Dirty);for(const auto& P:FloorEdits)P.Actor->GetPackage()->SetDirtyFlag(P.Dirty);B->GetPackage()->SetDirtyFlag(Dirty);W->GetPackage()->SetDirtyFlag(WallDirty);}
  else Batch.Publish();
  Result.Message=FString::Printf(TEXT("%s: %s"),Restored?TEXT("OpeningEditFailedRolledBack"):TEXT("OpeningEditRollbackFailed"),*Status.ToString());return Result;
 }
 Result.bSucceeded=true;Result.Message=TEXT("Committed");Result.CommittedEdit=B->LastCommittedEdit;
 Batch.Publish();GEditor->RedrawLevelEditingViewports();return Result;
}
