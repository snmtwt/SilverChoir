bool AEHB_FloorSlab::ResolveSurfaceOpeningCuts(const TArray<FEHBCutOperation>& Cuts,const TArray<FVector>& Outer,
 const TArray<FEHBFloorSlabHole>& Holes,TArray<FEHBCutOperation>& Resolved) const
{
 Resolved.Reset();
 if(!Cuts.ContainsByPredicate([](const auto& C){return C.SurfaceHost.Version!=0;})){Resolved=Cuts;return true;}
 if(GetClass()!=StaticClass()||!OwningBuilding||GetAttachParentActor()!=OwningBuilding.Get()
  ||!FMath::IsFinite(Thickness)||Thickness<1||(bIsFoundation&&bKeepFoundationBottomOnGround))return false;
 FEHBLogicalSurfaceDefinition Host;Host.BuildingGuid=OwningBuilding->BuildingGuid;Host.ElementGuid=ElementGuid;
 Host.SourceName=TEXT("Slab.Surface");Host.SurfaceGuid=FindLogicalSurfaceIdentity(Host.SourceName);Host.FloorIndex=FloorIndex;Host.Thickness=Thickness;
 Host.PlaneToBuilding=FTransform(FQuat::Identity,FVector(0,0,GetTopZ()))*GetElementLocalTransform();
 FName Status;
 auto ValidateLoop=[&](const TArray<FVector>& Loop)
 {
  auto Shape=Host;Shape.Regions.Reset();auto& Boundary=Shape.Regions.AddDefaulted_GetRef().Boundary;
  for(const auto& P:Loop){if(P.ContainsNaN()||FMath::Abs(P.Z-GetTopZ())>0.001)return false;Boundary.Add(FVector2D(P.X,P.Y));}return Shape.Validate(Status);
 };
 if(!ValidateLoop(Outer))return false;TArray<TArray<FVector>> Cutters;
 for(const auto& Hole:Holes){if(!ValidateLoop(Hole.LocalPolygon))return false;Cutters.Add(Hole.LocalPolygon);}
 FEHBPolygonClipResult Domain;if(!FEHBPolygonClipper::DifferenceXY(Outer,Cutters,Domain))return false;
 for(const auto& R:Domain.Regions)
 {
  auto& Region=Host.Regions.AddDefaulted_GetRef();for(const auto& P:R.OuterLoop)Region.Boundary.Add(FVector2D(P.X,P.Y));
  for(const auto& H:R.HoleLoops){auto& Hole=Region.Holes.AddDefaulted_GetRef();for(const auto& P:H.Points)Hole.Vertices.Add(FVector2D(P.LocalPosition.X,P.LocalPosition.Y));}
 }
 if(!Host.Validate(Status))return false;
 TArray<FEHBCutOperation> Candidate;Candidate.Reserve(Cuts.Num());
 for(const auto& Cut:Cuts)
 {
  if(Cut.SurfaceHost.Version==0){Candidate.Add(Cut);continue;}if(Cut.SurfaceHost.Version!=1)return false;
  auto Copy=Cut;Copy.SurfaceHost={};Copy.Stage=EEHBCutStage::Profile;Copy.ProjectionMode=EEHBCutProjectionMode::HorizontalXY;
  if(Cut.bEnabled)
  {
   TArray<FVector> Polygon;if(!FEHBSurfaceOpening::Resolve(Cut,Host,GetElementLocalTransform(),Polygon,Status)||Polygon.Num()!=Copy.Source.ExplicitPolygon.Points.Num())return false;
   Copy.Source.LocalTransform=FTransform(FVector(0,0,(GetTopZ()+GetBottomZ())*0.5));Copy.Source.Height=Thickness;
   for(int32 I=0;I<Polygon.Num();++I){Polygon[I].Z=0;Copy.Source.ExplicitPolygon.Points[I].LocalPosition=Polygon[I];}
  }
  Candidate.Add(MoveTemp(Copy));
 }
 Resolved=MoveTemp(Candidate);return true;
}
