bool EHBRoomFinishMove::FNodeEditPlan::CaptureSupportBottom(AEHBElementActorBase* E,bool Candidate,TArray<FEHBFloorFinishRegion>& Out,FName& Status) const
{
 Out.Reset();if(!E){Status=TEXT("MissingNodeSupportElement");return false;}
 if(Candidate&&RemovedHosts.Contains(E->ElementGuid))return true;
 if(auto* W=Cast<AEHB_Wall>(E))
 {
  FEHBPreparedWallOpening Prepared;
  if(Candidate){const auto* Side=CandidateSides.GetWallSides().FindByPredicate([&](const auto& S){return S.WallGuid==E->ElementGuid;});if(!Side||!W->PrepareNodeSurfaceOpening(*Side,Prepared,Status))return false;}
  else
  {
   const auto* Section=W->CapMeshComponent?W->CapMeshComponent->GetProcMeshSection(0):nullptr;if(!Section){Status=TEXT("MissingNodeSupportCaps");return false;}
   FEHBWallJunctionMesh Mesh;for(const auto& V:Section->ProcVertexBuffer){Mesh.Vertices.Add(V.Position);Mesh.Normals.Add(V.Normal);Mesh.UVs.Add(V.UV0);}for(uint32 I:Section->ProcIndexBuffer)Mesh.Triangles.Add(static_cast<int32>(I));
   return FEHBFloorContactGeometry::CaptureHorizontalBottomsFromMesh(Mesh,W->GetActorTransform(),W->OwningBuilding->GetActorTransform(),Out,Status);
  }
  Out=MoveTemp(Prepared.HorizontalBottoms);return true;
 }
 if(auto* S=Cast<AEHB_FloorSlab>(E))
 {
  const auto* P=Candidate?Slabs.FindByPredicate([&](const auto& V){return V.Actor==S;}):nullptr;
  if(!P)return FEHBFloorContactGeometry::CaptureSlabBottom(S,Out,Status);
  if(!S->CutOperations.IsEmpty()||!S->LocalHoles.IsEmpty()){Status=TEXT("NodeSupportSlabOpeningRequiresPlan");return false;}
  auto& R=Out.AddDefaulted_GetRef();for(auto V:P->Polygon){V.Z=S->GetBottomZ();R.OuterPolygon.Add(S->GetElementLocalTransform().TransformPosition(V));}return true;
 }
 if(auto* P=Cast<AEHB_Pillar>(E))
 {
  if(!Candidate)return FEHBFloorContactGeometry::CapturePillarBottom(P,Out,Status);
  const auto* Surfaces=Tops.Find(P->ElementGuid);if(!Surfaces){Status=TEXT("MissingCandidatePillarSupport");return false;}
  for(const auto& T:*Surfaces){auto& R=Out.AddDefaulted_GetRef();R.OuterPolygon=T.OuterPolygon;R.Holes=T.Holes;for(auto& V:R.OuterPolygon)V.Z-=P->Height;for(auto& H:R.Holes)for(auto& V:H.LocalPolygon)V.Z-=P->Height;}return true;
 }
 Status=TEXT("UnsupportedNodeSupportBottom");return false;
}

bool EHBRoomFinishMove::FNodeEditPlan::PrepareSupportRelations(AEHBBuildingActorBase* B,const TArray<AEHBElementActorBase*>& Elements,FName& Status)
{
 for(const auto& R:B->ElementRelations)
 {
  if(R.Type!=EEHBElementRelationType::StructuralSupport&&R.Type!=EEHBElementRelationType::PhysicalContact)continue;
  if(!R.bEnabled||R.Source.Kind!=EEHBRelationEndpointKind::BuildingElement||R.Target.Kind!=EEHBRelationEndpointKind::BuildingElement
   ||R.Source.SurfaceKind!=EEHBElementSurfaceKind::Top||R.Target.SurfaceKind!=EEHBElementSurfaceKind::Bottom)
  {Status=TEXT("UnsupportedNodeSupportRelation");return false;}
  auto* Source=B->FindElementActorByGuid(R.Source.ElementGuid);auto* Target=B->FindElementActorByGuid(R.Target.ElementGuid);
  TArray<FEHBFloorSupportSurface> OldTop,NextTop;TArray<FEHBFloorFinishRegion> OldBottom,NextBottom;FEHBFloorContact Old,Next;
  if(!Source||!Target||!FEHBFloorContactGeometry::CaptureHorizontalTops(Source,OldTop,Status)||!CaptureSupportBottom(Target,false,OldBottom,Status))return false;
  auto Solve=[&](const auto& Bottom,const auto& Top,FEHBFloorContact& C){if(Bottom.IsEmpty()||Top.IsEmpty()){C={};return true;}return FEHBFloorContactGeometry::Build(Bottom,Top,C,Status);};
  if(!Solve(OldBottom,OldTop,Old)||Old.Area<=0||FMath::Abs(Old.Area-R.ContactArea)>0.01){Status=TEXT("StaleNodeSupportContact");return false;}
  if(!RemovedHosts.Contains(Source->ElementGuid)){const auto* Planned=Tops.Find(Source->ElementGuid);NextTop=Planned?*Planned:OldTop;}
  if(!CaptureSupportBottom(Target,true,NextBottom,Status)||!Solve(NextBottom,NextTop,Next))return false;
  SupportRelationIds.Add(R.RelationGuid);
  OriginalSupportRelations.Add(R);
  if(Next.Area<=0)RemovedSupportRelations.Add(R.RelationGuid);
  else {auto& V=SupportRelations.Add_GetRef(R);V.ContactArea=Next.Area;V.ContactPoint=Next.Point;V.ContactNormal=FVector::UpVector;}
 }
 // Floor propagation is solved against the complete candidate graph before any
 // writes. A topology-owned room cannot silently migrate to another floor.
 TArray<FEHBFloorAssignmentState> Source,Before,After;TArray<FEHBElementRelation> Next;
 for(auto* E:Elements){auto& V=Source.AddDefaulted_GetRef();V.ElementGuid=E->ElementGuid;V.Type=E->ElementType;V.FloorIndex=E->FloorIndex;V.Role=E->FloorRole;V.Policy=E->FloorAssignmentPolicy;V.Source=E->FloorAssignmentSource;V.Candidates=E->ConflictingFloorCandidates;V.Conflict=E->bFloorAssignmentConflict;}
 for(const auto& R:B->ElementRelations){if(RemovedSupportRelations.Contains(R.RelationGuid))continue;const auto* Replacement=SupportRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==R.RelationGuid;});Next.Add(Replacement?*Replacement:R);}
 if(!FEHBFloorAssignmentPlan::Build(Source,B->ElementRelations,Before,Status)||!FEHBFloorAssignmentPlan::Build(Source,Next,After,Status))return false;
 for(const auto& V:After)
 {
  const auto* P=Before.FindByPredicate([&](const auto& S){return S.ElementGuid==V.ElementGuid;});
  const auto* Live=Source.FindByPredicate([&](const auto& S){return S.ElementGuid==V.ElementGuid;});
  if(!P||!Live||P->FloorIndex!=Live->FloorIndex||P->Role!=Live->Role||P->Source!=Live->Source||P->Candidates!=Live->Candidates||P->Conflict!=Live->Conflict)
  {Status=TEXT("StaleNodeSupportFloorSource");return false;}
  if(!P||P->FloorIndex!=V.FloorIndex||P->Role!=V.Role||P->Source!=V.Source||P->Candidates!=V.Candidates||P->Conflict!=V.Conflict)
  {Status=TEXT("NodeSupportFloorMigrationRequired");return false;}
 }
 return true;
}
