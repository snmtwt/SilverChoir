#include "EHBFinishRegionCommand.h"
#include "EHBSlabDisplayPlan.h"
#include "EHBNodeAuthorityEditing.h"
#include "EHBCopyOutlinePolicy.h"
#include "Core/EHBSurfaceRoomCoverage.h"
#include "Geometry/EHBFloorContactGeometry.h"
#include "EasyHouseEditorMode.h"
#include "Intersection/IntrSegment2Segment2.h"

namespace EHBFinishRegionCommand
{
bool IsIndependent(const AEHBElementActorBase* E)
{
 if(!IsValid(E)||!E->OwningBuilding||E->OwningBuilding->WallNodeAuthority.Version!=2)return false;
 if(const auto* F=Cast<AEHB_Floor>(E))return F->OutlineSource==EEHBOutlineSource::RetainedRegion;
 if(const auto* S=Cast<AEHB_FloorSlab>(E))return S->OutlineSource==EEHBOutlineSource::RetainedRegion;
 return false;
}
TArray<FVector> GetPolygon(const AEHBElementActorBase* E)
{
 if(const auto* F=Cast<AEHB_Floor>(E))return F->LocalFloorPolygon;
 if(const auto* S=Cast<AEHB_FloorSlab>(E))return S->LocalTopPolygon;
 return {};
}
namespace
{
 struct FSlabSourceEdit {TArray<FEHBFloorSlabHole> Holes;TArray<FEHBCutOperation> Cuts;};
 bool FailAt(int32 Phase)
 {
#if WITH_DEV_AUTOMATION_TESTS
  if(FailurePhase==Phase){FailurePhase=0;return true;}
#endif
  return false;
 }
 struct FPlan
 {
  FEHBWallNodeModel Source;
  EHBRoomFinishMove::FNodeEditPlan Finishes;
  AEHBElementActorBase* Target=nullptr;
  FGuid Room,Anchor;
  EEHBFloorSlabWallSide Side=EEHBFloorSlabWallSide::None;
  TArray<FVector> Polygon;
  TArray<FGuid> ChangedRooms,AuthoredElements;
  FEHBSlabDisplayPlan Displays;
  bool bNoChange=false;
  TOptional<FSlabSourceEdit> OpeningEdit;

  TArray<FEHBFloorFinishRegion> Footprint(const AEHBElementActorBase* E,const TArray<FVector>& P,bool Candidate=false) const
  {
   if(const auto* S=Cast<AEHB_FloorSlab>(E))
   {
    TArray<FEHBLogicalSurfaceRegion> SourceRegions;FName Status;TArray<FEHBFloorFinishRegion> Result;
    if(!S->BuildCandidateDisplayRegions(P,Candidate&&OpeningEdit.IsSet()?OpeningEdit->Holes:S->LocalHoles,Candidate&&OpeningEdit.IsSet()?OpeningEdit->Cuts:S->CutOperations,SourceRegions,Status,false))return Result;
    for(const auto& R:SourceRegions){auto& Region=Result.AddDefaulted_GetRef();for(const auto& V:R.Boundary)Region.OuterPolygon.Add(S->GetElementLocalTransform().TransformPosition(FVector(V.X,V.Y,S->GetTopZ())));for(const auto& H:R.Holes){auto& Hole=Region.Holes.AddDefaulted_GetRef().LocalPolygon;for(const auto& V:H.Vertices)Hole.Add(S->GetElementLocalTransform().TransformPosition(FVector(V.X,V.Y,S->GetTopZ())));}}
    return Result;
   }
   FEHBFloorFinishRegion R;for(auto V:P){if(const auto* S=Cast<AEHB_FloorSlab>(E))V.Z=S->GetTopZ();R.OuterPolygon.Add(E->GetElementLocalTransform().TransformPosition(V));}return {R};
  }
  bool Prepare(AEHBBuildingActorBase* B,AEHBElementActorBase* E,const TArray<AEHBElementActorBase*>& Elements,const TArray<FVector>& Requested,FGuid RequestedRoom,FName& Status,const FSlabSourceEdit* Edit)
  {
   auto Fail=[&](FName Why){Status=Why;return false;};Target=E;Room=RequestedRoom;
   if(Edit){OpeningEdit=*Edit;for(const auto& Cut:Edit->Cuts)if(Cut.Source.SourceElement||Cut.Source.SourceElementGuid.IsValid()||(Cut.TransformPolicy!=EEHBCutTransformPolicy::TargetLocal&&!(Cut.SurfaceHost.Version==1&&Cut.TransformPolicy==EEHBCutTransformPolicy::FollowTargetElement)))return Fail(TEXT("LinkedOpeningRequiresPlan"));}
   const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Source);if(!Capture.bSucceeded)return Fail(Capture.Status);
   if(!Finishes.Prepare(B,Source,Source,Elements,{},Status,true))return false;
   for(const auto& R:B->ElementRelations)if(R.Type!=EEHBElementRelationType::TopologyConnection&&!(R.Type==EEHBElementRelationType::SurfaceFinish&&Finishes.OwnedRelations.Contains(R.RelationGuid)))return Fail(TEXT("RequiresRegionDependencyPlan"));
   const auto Original=GetPolygon(E);if(Original.Num()<3)return Fail(TEXT("InvalidRegionPolygon"));Polygon=Requested;
   FEHBSurfaceRoomCoverage OldCoverage;
   if(!FEHBSurfaceRoomCoverageSolver::Build(E->ElementGuid,E->FloorIndex,Footprint(E,Original),Finishes.GetRooms(),OldCoverage,Status))return false;
   for(const auto& Share:OldCoverage.Rooms)ChangedRooms.AddUnique(Share.RoomGuid);
   if(Room.IsValid())
   {
    if(!Requested.IsEmpty())return Fail(TEXT("AmbiguousRegionEdit"));
    const auto* R=Finishes.GetRooms().FindByPredicate([&](const auto& V){return V.RoomGuid==Room&&V.FloorIndex==E->FloorIndex;});
    if(!R||!OldCoverage.Rooms.ContainsByPredicate([&](const auto& V){return V.RoomGuid==Room;}))return Fail(TEXT("RegionRoomDoesNotOverlap"));
    if(auto* F=Cast<AEHB_Floor>(E)){Polygon=R->Polygon;for(auto& V:Polygon)V.Z=Original[0].Z;}
    else if(auto* S=Cast<AEHB_FloorSlab>(E))
    {
     if(!FEasyHouseEditorMode::BuildRoomSlabOutlineFromDefinition(S,*R,Source,Finishes.GetCandidateSides().GetWallSides(),Polygon))return Fail(TEXT("InvalidRegionRoomOutline"));
     Anchor=R->WallGuids[0];const auto* W=Source.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Anchor;});if(!W)return Fail(TEXT("MissingRegionRoomAnchor"));
     Side=W->StartNodeGuid==R->NodeGuids[0]?EEHBFloorSlabWallSide::Left:EEHBFloorSlabWallSide::Right;
    }
   }
   if(Polygon.Num()<3)return Fail(TEXT("InvalidRegionPolygon"));
   // Generator validation may sanitize repeated points; an edit command must
   // reject them rather than silently applying a different authored contour.
   for(int32 I=0;I<Polygon.Num();++I)for(int32 J=I+1;J<Polygon.Num();++J)
    if(FVector2D(Polygon[I]).Equals(FVector2D(Polygon[J]),0.001))return Fail(TEXT("InvalidRegionPolygon"));
   for(const auto& V:Polygon)if(!FMath::IsFinite(V.X)||!FMath::IsFinite(V.Y)||!FMath::IsFinite(V.Z)||!FMath::IsNearlyEqual(V.Z,Original[0].Z,0.001))return Fail(TEXT("RegionElevationMustStayFixed"));
   // Check adjacent reversal directly: normalized-segment intersection can miss
   // a collinear overlap after rotating the authored slab coordinates.
   for(int32 I=0;I<Polygon.Num();++I)
   {
    const FVector2d A(Polygon[(I+1)%Polygon.Num()]-Polygon[I]);
    const FVector2d C(Polygon[(I+2)%Polygon.Num()]-Polygon[(I+1)%Polygon.Num()]);
    if(A.X*C.X+A.Y*C.Y<0&&FMath::Abs(A.X*C.Y-A.Y*C.X)<=1.e-8*FMath::Sqrt(A.SquaredLength()*C.SquaredLength()))return Fail(TEXT("SelfIntersectingRegionPolygon"));
   }
   for(int32 I=0;I<Polygon.Num();++I)for(int32 J=I+1;J<Polygon.Num();++J)
   {
    const UE::Geometry::FSegment2d EdgeA{FVector2d(Polygon[I]),FVector2d(Polygon[(I+1)%Polygon.Num()])};
    const UE::Geometry::FSegment2d EdgeB{FVector2d(Polygon[J]),FVector2d(Polygon[(J+1)%Polygon.Num()])};
    UE::Geometry::FIntrSegment2Segment2d Intersection{EdgeA,EdgeB};
    const bool Adjacent=J==I+1||(I==0&&J==Polygon.Num()-1);
    if(Intersection.Find()&&(!Adjacent||(!Intersection.IsSimpleIntersection()&&(Intersection.Point1-Intersection.Point0).SquaredLength()>0.000001)))return Fail(TEXT("SelfIntersectingRegionPolygon"));
   }
   if(auto* F=Cast<AEHB_Floor>(E))
   {
    auto* P=Finishes.Floors.FindByPredicate([&](const auto& V){return V.Actor==F;});if(!P)return Fail(TEXT("MissingRegionFloorPlan"));P->Regions[0].OuterPolygon=Polygon;
    if(!F->ValidateFloorRegions(P->Regions))return Fail(TEXT("InvalidRegionPolygon"));
   }
   else if(auto* S=Cast<AEHB_FloorSlab>(E))
   {
    auto* P=Finishes.Slabs.FindByPredicate([&](const auto& V){return V.Actor==S;});TArray<FEHBLogicalSurfaceRegion> RequestedDisplay;if(!P||!S->BuildCandidateDisplayRegions(Polygon,OpeningEdit.IsSet()?OpeningEdit->Holes:S->LocalHoles,OpeningEdit.IsSet()?OpeningEdit->Cuts:S->CutOperations,RequestedDisplay,Status,false))return Fail(TEXT("InvalidRegionPolygon"));P->Polygon=Polygon;
    TArray<FEHBFloorSupportSurface> Tops;for(const auto& R:Footprint(S,Polygon,true)){auto& Top=Tops.AddDefaulted_GetRef();Top.OuterPolygon=R.OuterPolygon;Top.Holes=R.Holes;}Finishes.Tops.Add(E->ElementGuid,MoveTemp(Tops));
   }
   const auto Next=Footprint(E,Polygon,true);
   // Same-kind co-planar coverage must never overwrite a different authored
   // region. Floor finish over a structural slab is intentional and allowed.
   for(auto* Other:Elements)if(Other!=E&&Other->GetClass()==E->GetClass()&&Other->FloorIndex==E->FloorIndex)
   {
    const auto OtherFootprint=Footprint(Other,GetPolygon(Other));TArray<FEHBFloorSupportSurface> OtherTops;for(const auto& R:OtherFootprint){auto& Top=OtherTops.AddDefaulted_GetRef();Top.OuterPolygon=R.OuterPolygon;Top.Holes=R.Holes;}double Area=0;
    if(!FEHBFloorContactGeometry::MeasureArea(Next,OtherTops,Area,Status))return false;
    if(Area>0.01)return Fail(TEXT("RegionOverlapsAnotherSurface"));
   }
   AuthoredElements.AddUnique(E->ElementGuid);
   if(auto* S=Cast<AEHB_FloorSlab>(E))
   {
    Displays.bPreserveSlabSources=true;
    for(const auto& P:Finishes.Slabs){auto& C=Displays.Slabs.AddDefaulted_GetRef();C.Actor=P.Actor;C.Polygon=P.Polygon;if(OpeningEdit.IsSet()&&P.Actor==S){C.bOverrideSources=true;C.Holes=OpeningEdit->Holes;C.Cuts=OpeningEdit->Cuts;}}
    if(OpeningEdit.IsSet()||Displays.HasConstraintsInLayer(S)){Displays.DisplayLayers.Add(S);if(!Displays.Prepare(Status))return false;}else Displays.Slabs.Reset();
    for(FGuid Id:Displays.AuthoredElements)AuthoredElements.AddUnique(Id);
   }
   FEHBSurfaceRoomCoverage NewCoverage;if(!FEHBSurfaceRoomCoverageSolver::Build(E->ElementGuid,E->FloorIndex,Next,Finishes.GetRooms(),NewCoverage,Status))return false;
   for(const auto& Share:NewCoverage.Rooms)ChangedRooms.AddUnique(Share.RoomGuid);ChangedRooms.Sort();
   for(auto& P:Finishes.Floors)
   {
    FEHBFloorFinishContactDraft Draft;Draft.FloorGuid=P.Actor->ElementGuid;Draft.RoomGuid=P.Actor==E?Room:P.Actor->RoomLoopGuid;
    Draft.bIndependentRegion=P.Actor==E?!Room.IsValid():P.Actor->OutlineSource==EEHBOutlineSource::RetainedRegion;Draft.FloorIndex=P.Actor->RoomFloorIndex;Draft.Regions=P.Regions;Draft.Hosts=Finishes.Tops;
    for(const auto& R:B->ElementRelations)if(P.Actor->SurfaceFinishRelationGuids.Contains(R.RelationGuid))Draft.Previous.Add(R);
    if(!Draft.Build(P.Relations,Status))return false;
   }
   bNoChange=!Room.IsValid()&&Original==Polygon;
   if(OpeningEdit.IsSet()){const auto* S=CastChecked<AEHB_FloorSlab>(E);bNoChange=S->LocalHoles.Num()==OpeningEdit->Holes.Num()&&S->CutOperations.Num()==OpeningEdit->Cuts.Num();if(bNoChange){for(int32 I=0;I<S->LocalHoles.Num();++I)bNoChange&=FEHBFloorSlabHole::StaticStruct()->CompareScriptStruct(&S->LocalHoles[I],&OpeningEdit->Holes[I],0);for(int32 I=0;I<S->CutOperations.Num();++I)bNoChange&=FEHBCutOperation::StaticStruct()->CompareScriptStruct(&S->CutOperations[I],&OpeningEdit->Cuts[I],0);}}Status=TEXT("Ready");return true;
  }
  bool Apply(AEHBBuildingActorBase* B,FName& Status)
  {
   auto Fail=[&](FName Why){Status=Why;return false;};bool Changed=false;
   if(auto* S=Cast<AEHB_FloorSlab>(Target))
   {
    if(!OpeningEdit.IsSet()){S->RoomFillLoopGuid=Room;S->bHasRoomFillAnchor=Anchor.IsValid();S->RoomFillAnchorWallGuid=Anchor;S->RoomFillAnchorWallSide=Side;}
    if(Displays.Slabs.IsEmpty()){if(!S->SetSlabOutlineIfNeeded(Polygon,Changed))return Fail(TEXT("RegionSlabApplyFailed"));S->RecordOutlineSource(Room.IsValid()?EEHBOutlineSource::RoomBoundary:EEHBOutlineSource::RetainedRegion);}
    for(const auto& P:Displays.Slabs)
    {
     const auto SourceKind=P.Actor==S&&!OpeningEdit.IsSet()?(Room.IsValid()?EEHBOutlineSource::RoomBoundary:EEHBOutlineSource::RetainedRegion):P.Actor->OutlineSource;
     if(P.Display.IsActive()){if(!P.Actor->SetPartitionedSlabState(P.Polygon,P.Holes,P.Cuts,P.Display))return Fail(TEXT("RegionDisplayApplyFailed"));P.Actor->RecordOutlineSource(SourceKind);}
     else if(P.Actor==S){if(!S->SetSlabOutlineIfNeeded(Polygon,Changed))return Fail(TEXT("RegionSlabApplyFailed"));S->RecordOutlineSource(SourceKind);}
    }
    if(!Displays.Verify(Status))return false;
   }
   if(auto* F=Cast<AEHB_Floor>(Target))
   {
    F->RoomLoopGuid=Room;auto Regions=F->FloorRegions;Regions[0].OuterPolygon=Polygon;
    if(!F->SetFloorRegionsIfNeeded(Regions,Changed))return Fail(TEXT("RegionFloorApplyFailed"));F->RecordOutlineSource(Room.IsValid()?EEHBOutlineSource::RoomBoundary:EEHBOutlineSource::RetainedRegion);
   }
   if(GetPolygon(Target)!=Polygon)return Fail(TEXT("RegionOutlineApplyMismatch"));
   if(FailAt(1))return Fail(TEXT("InjectedRegionOutlineFailure"));
   for(const auto& Pair:Finishes.Tops){TArray<FEHBFloorSupportSurface> Actual;if(!FEHBFloorContactGeometry::CaptureHorizontalTops(B->FindElementActorByGuid(Pair.Key),Actual,Status)||!EHBRoomFinishMove::SameTopCoverage(Pair.Value,Actual,Status))return false;}
   for(const auto& P:Finishes.Floors)
   {
    TArray<FEHBElementRelation> Actual;if(!P.Actor->BuildCurrentSurfaceFinishRelationPlan(P.Regions,Actual,Status)||Actual.Num()!=P.Relations.Num())return Fail(TEXT("RegionContactMismatch"));
    const auto Previous=P.Actor->SurfaceFinishRelationGuids;TArray<FGuid> Next;
    for(auto R:P.Relations)
    {
     const auto* A=Actual.FindByPredicate([&](const auto& V){return V.Source.IsEquivalentTo(R.Source)&&V.Target.IsEquivalentTo(R.Target);});if(!A||FMath::Abs(A->ContactArea-R.ContactArea)>0.01)return Fail(TEXT("RegionContactMismatch"));
     R.ContactPoint=A->ContactPoint;R.ContactNormal=A->ContactNormal;R.ContactArea=A->ContactArea;R.SourceGeometryRevision=B->GetElementGeometryRevision(R.Source.ElementGuid);R.TargetGeometryRevision=B->GetElementGeometryRevision(R.Target.ElementGuid);
     const auto* Saved=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;});
     if(!Saved||!FEHBElementRelation::StaticStruct()->CompareScriptStruct(Saved,&R,0)){if(B->AddOrUpdateElementRelation(R,true)!=R.RelationGuid)return Fail(TEXT("RegionContactPublishFailed"));if(P.Actor->RoomLoopGuid.IsValid())ChangedRooms.AddUnique(P.Actor->RoomLoopGuid);}
     Next.Add(R.RelationGuid);
    }
    for(FGuid Id:Previous)if(!Next.Contains(Id)){if(!B->RemoveElementRelation(Id))return Fail(TEXT("RegionContactRemovalFailed"));if(P.Actor->RoomLoopGuid.IsValid())ChangedRooms.AddUnique(P.Actor->RoomLoopGuid);}
    if(P.Actor->SurfaceFinishRelationGuids!=Next)P.Actor->SurfaceFinishRelationGuids=MoveTemp(Next);
   }
   if(FailAt(2))return Fail(TEXT("InjectedRegionContactFailure"));
   FEHBWallNodeModel Actual;const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(B,Actual);if(!Capture.bSucceeded)return Fail(Capture.Status);
   FString Before,After;FJsonObjectConverter::UStructToJsonObjectString(Source,Before);FJsonObjectConverter::UStructToJsonObjectString(Actual,After);if(Before!=After)return Fail(TEXT("RegionChangedStructuralModel"));
   const auto Policy=FEHBCopyOutlinePolicy::Prepare(B,Actual,B->QueryElements(FEHBElementQuery()),true,true);if(!Policy.bSucceeded)return Fail(Policy.Status);
   if(FailAt(3))return Fail(TEXT("InjectedRegionValidationFailure"));ChangedRooms.Sort();return true;
  }
 };
}
static FEHBToolsetOperationResult ExecuteInternal(AEHBBuildingActorBase* B,FGuid ElementGuid,int32 GraphRevision,int32 GeometryRevision,const TArray<FVector>& Polygon,FGuid Room,bool Preview,const FSlabSourceEdit* Edit)
{
 FEHBToolsetOperationResult Result;auto Fail=[&](FName Why){Result.Message=Why.ToString();return Result;};
 if(!GEditor||GEditor->PlayWorld||GEditor->IsTransactionActive()||GIsTransacting)return Fail(TEXT("RequiresIndependentEditorTransaction"));
 if(!IsValid(B)||B->GetClass()!=AEHB_Building::StaticClass()||B->GetWorld()!=GEditor->GetEditorWorldContext().World()||B->GetAttachParentActor()||B->IsActorBeingDestroyed())return Fail(TEXT("RequiresNativeEditorBuilding"));
 if(B->IsChangeNotificationBusy())return Fail(TEXT("ChangeNotificationBusy"));
 auto* E=B->FindElementActorByGuid(ElementGuid);
 const auto* Slab=Cast<AEHB_FloorSlab>(E);
 const bool OpeningTarget=Edit&&Slab&&B->WallNodeAuthority.Version==2&&(Slab->OutlineSource==EEHBOutlineSource::RetainedRegion||Slab->OutlineSource==EEHBOutlineSource::RoomBoundary);
 if(Edit?!OpeningTarget:!IsIndependent(E))return Fail(TEXT("RequiresIndependentRegion"));
 if(B->RelationshipGraphRevision!=GraphRevision||B->GetElementGeometryRevision(ElementGuid)!=GeometryRevision)return Fail(TEXT("StaleRegionRevision"));
 if(FLevelUtils::IsLevelLocked(B->GetLevel())||!FLevelUtils::IsLevelVisible(B->GetLevel()))return Fail(TEXT("RequiresVisibleUnlockedLevel"));
 if(GEditor->GetSelectedActors()->Num()!=1||(!GEditor->GetSelectedActors()->IsSelected(B)&&!GEditor->GetSelectedActors()->IsSelected(E)))return Fail(TEXT("TargetNotSelected"));
 const auto Elements=B->QueryElements(FEHBElementQuery());TArray<AActor*> Attached;B->GetAttachedActors(Attached,true,true);if(Attached.Num()!=Elements.Num())return Fail(TEXT("IncompleteNativeGroup"));
 for(auto* A:Elements)if(!Attached.Contains(A)||A->GetAttachParentActor()!=B||A->GetLevel()!=B->GetLevel()||(A->GetClass()!=AEHB_Pillar::StaticClass()&&A->GetClass()!=AEHB_Wall::StaticClass()&&A->GetClass()!=AEHB_Floor::StaticClass()&&A->GetClass()!=AEHB_FloorSlab::StaticClass())||!A->GetInstanceComponents().IsEmpty())return Fail(TEXT("RequiresRegionDependencyPlan"));
 for(auto* C:B->GetInstanceComponents())if(IsValid(C)&&(!Cast<UEHBWallJunctionComponent>(C)||!C->ComponentHasTag(TEXT("EHB.NodeAuthorityDerived"))))return Fail(TEXT("CustomBuildingComponentRequiresPlan"));
 FPlan Plan;FName Reason;if(!Plan.Prepare(B,E,Elements,Polygon,Room,Reason,Edit))return Fail(Reason);
 if(Preview||Plan.bNoChange){Result.bSucceeded=true;Result.Message=Plan.bNoChange?TEXT("NoChange"):TEXT("Ready");return Result;}
 FEHBChangeNotificationBatch Notifications(*B);if(!Notifications.IsActive())return Fail(TEXT("ChangeNotificationBusy"));bool Applied;
 {
  FScopedTransaction Transaction(NSLOCTEXT("EasyHouseBuilder","EditIndependentRegion","Edit Independent Floor Region"));B->SetFlags(RF_Transactional);B->Modify();
  for(auto* A:Elements){A->SetFlags(RF_Transactional);A->Modify();TInlineComponentArray<UActorComponent*> Components(A);for(auto* C:Components){C->SetFlags(RF_Transactional);C->Modify();}}
  Applied=Plan.Apply(B,Reason);
  if(Applied){E->SynchronizePlannedEditorMove();Applied=Notifications.RecordCommittedEdit(Edit?TEXT("EditSlabOpenings"):Room.IsValid()?TEXT("RebindFinishRegion"):TEXT("EditFinishRegion"),{},Plan.ChangedRooms,Plan.AuthoredElements);}
  if(Applied&&FailAt(4)){Applied=false;Reason=TEXT("InjectedRegionReceiptFailure");}B->MarkPackageDirty();
 }
 if(!Applied)
 {
  UE_LOG(LogTemp,Warning,TEXT("EHB region edit rejected during apply: %s"),*Reason.ToString());const bool Restored=GEditor->UndoTransaction(false);
  if(Restored){B->RebuildElementAndRelationshipIndexes();Notifications.Rollback();}else Notifications.Publish();return Fail(Restored?TEXT("RegionEditFailedRolledBack"):TEXT("RegionEditRollbackFailed"));
 }
 Result.bSucceeded=true;Result.Message=TEXT("Committed");Result.CommittedEdit=B->LastCommittedEdit;Notifications.Publish();GEditor->RedrawLevelEditingViewports();return Result;
}
FEHBToolsetOperationResult Execute(AEHBBuildingActorBase* B,FGuid Id,int32 Graph,int32 Geometry,const TArray<FVector>& P,FGuid Room,bool Preview){return ExecuteInternal(B,Id,Graph,Geometry,P,Room,Preview,nullptr);}
FEHBToolsetOperationResult EditSlabSources(AEHB_FloorSlab* S,int32 Graph,int32 Geometry,const TArray<FEHBFloorSlabHole>& Holes,const TArray<FEHBCutOperation>& Cuts,bool Preview)
{
 if(!IsValid(S)){FEHBToolsetOperationResult R;R.Message=TEXT("InvalidSlab");return R;}FSlabSourceEdit Edit{Holes,Cuts};for(auto& C:Edit.Cuts)C.EnsureGuids();return ExecuteInternal(S->OwningBuilding,S->ElementGuid,Graph,Geometry,S->LocalTopPolygon,{},Preview,&Edit);
}
FText DescribeResult(const FEHBToolsetOperationResult& R)
{
 if(R.bSucceeded)return NSLOCTEXT("EasyHouseBuilder","RegionEditDone","地面区域已更新，可撤销恢复。");
 if(R.Message==TEXT("InvalidOpeningHandle"))return NSLOCTEXT("EasyHouseBuilder","OpeningHandleInvalid","开口控制点已变化，请重新选择角点或相邻边后重试。");
 if(R.Message==TEXT("OverlappingLogicalSurfaceHoles"))return NSLOCTEXT("EasyHouseBuilder","OpeningBoundaryConflict","开口边界出现交叉、重叠或接触，请调整角点后重试。原建筑已保留。");
 if(R.Message==TEXT("StaleRegionRevision")||R.Message==TEXT("StaleRegionDrag")||R.Message==TEXT("StaleCutterDraft"))return NSLOCTEXT("EasyHouseBuilder","OpeningSourceChanged","建筑在操作期间发生了变化，请重新选择构件并开始编辑。原结果已保留。");
 if(R.Message==TEXT("RegionOverlapsAnotherSurface"))return NSLOCTEXT("EasyHouseBuilder","RegionEditOverlap","该区域会覆盖同一高度的其他地面，请缩小范围或先调整分区。");
 if(R.Message==TEXT("RegionRoomDoesNotOverlap"))return NSLOCTEXT("EasyHouseBuilder","RegionEditRoomMissing","请选择该地面实际覆盖且位于同一楼层的房间。");
 return FText::Format(NSLOCTEXT("EasyHouseBuilder","RegionEditRefused","地面调整未应用，请检查轮廓及关联构件。原因：{0}"),FText::FromString(R.Message));
}
}
