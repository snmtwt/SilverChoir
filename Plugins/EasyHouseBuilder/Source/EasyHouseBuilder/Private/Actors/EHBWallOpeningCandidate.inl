#include "Core/EHBWallOpeningCandidate.h"
#include "Core/EHBSurfaceOpening.h"

bool FEHBWallOpeningCandidate::Build(const FEHBWallOpeningCandidateSource& Source,
 const TArray<FEHBCutOperation>& Candidate,FEHBPreparedWallOpening& Out,FName& Status)
{
 const auto Inputs=Candidate;Out={};auto Fail=[&](FName Why){Status=Why;return false;};
 const auto& H=Source.Host;const auto& Sides=H.Sides;const auto& ElementToBuilding=Sides.LocalTransform;
 const float Height=H.Height,Thickness=H.Thickness;
 TArray<FEHBLogicalSurfaceDefinition> Hosts;if(!FEHBWallSurfaceHosts::Build(H,Hosts,Status))return false;
 if(!H.BuildingGuid.IsValid())return Fail(TEXT("MissingOpeningLogicalHost"));
 FEHBWallResolvedGeometry Geometry;Geometry.ReferenceLength=FVector::Distance(Sides.LocalStart,Sides.LocalEnd);
 const FVector SL=ElementToBuilding.InverseTransformPosition(Sides.StartLeft),EL=ElementToBuilding.InverseTransformPosition(Sides.EndLeft),SR=ElementToBuilding.InverseTransformPosition(Sides.StartRight),ER=ElementToBuilding.InverseTransformPosition(Sides.EndRight);
 for(const auto& P:{SL,EL,SR,ER})if(P.ContainsNaN()||FMath::Abs(P.Z-Height)>0.001)return Fail(TEXT("InvalidOpeningHostGeometry"));
 if(FMath::Abs(SL.Y-Thickness*0.5)>0.001||FMath::Abs(EL.Y-Thickness*0.5)>0.001||FMath::Abs(SR.Y+Thickness*0.5)>0.001||FMath::Abs(ER.Y+Thickness*0.5)>0.001)return Fail(TEXT("InvalidOpeningHostGeometry"));
 Geometry.StartLeftX=SL.X;Geometry.EndLeftX=EL.X;Geometry.StartRightX=SR.X;Geometry.EndRightX=ER.X;
 TArray<TArray<FVector2d>> Openings;bool TouchesHeightBoundary=false;TSet<FGuid> Operations;
 for(const auto& Cut:Inputs)
 {
  if(Cut.SurfaceHost.Version!=1||!Cut.bEnabled)return Fail(TEXT("OpeningPreparationRequiresBoundSource"));
  if(Operations.Contains(Cut.OperationGuid))return Fail(TEXT("DuplicateSurfaceOpeningIdentity"));Operations.Add(Cut.OperationGuid);
  const auto* Host=Hosts.FindByPredicate([&](const auto& V){return V.SurfaceGuid==Cut.SurfaceHost.SurfaceGuid;});if(!Host)return Fail(TEXT("SurfaceOpeningHostUnavailable"));
  TArray<FVector> Points;if(!FEHBSurfaceOpening::Resolve(Cut,*Host,ElementToBuilding,Points,Status))return false;
  auto& Polygon=Openings.AddDefaulted_GetRef();for(const auto& P:Points){Polygon.Add({P.X,P.Z});TouchesHeightBoundary|=P.Z<=0.01||P.Z>=Height-0.01;}
 }
 for(const auto& Polygon:Source.ConnectionOpenings)
 {
  FEHBLogicalSurfaceDefinition Shape=Hosts[0];Shape.Regions.Reset();auto& Boundary=Shape.Regions.AddDefaulted_GetRef().Boundary;
  for(const auto& P:Polygon)
  {
   if(!FMath::IsFinite(P.X)||!FMath::IsFinite(P.Y)||P.X<FMath::Max(SL.X,SR.X)-0.001||P.X>FMath::Min(EL.X,ER.X)+0.001||P.Y<-.5001||P.Y>Height+0.001)return Fail(TEXT("NodeDoorWindowOutsideWall"));
   Boundary.Add(FVector2D(P.X,P.Y));TouchesHeightBoundary|=P.Y<=0.01||P.Y>=Height-0.01;
  }
  if(!Shape.Validate(Status))return false;Openings.Add(Polygon);
 }
 FEHBPreparedWallOpening Result;Result.BuildingGuid=H.BuildingGuid;Result.ElementGuid=Sides.WallGuid;Result.Sources=Inputs;Result.bTouchesHeightBoundary=TouchesHeightBoundary;
 TArray<FEHBFloorFinishRegion> Coverage;
 double Perimeter=0;TArray<TArray<FVector>> UnionInputs;TArray<TArray<FVector2d>> UnionBoundaries;
 for(const auto& Polygon:Openings)
 {
  auto& Region=Coverage.AddDefaulted_GetRef();
  for(int32 I=0;I<Polygon.Num();++I){Region.OuterPolygon.Add(FVector(Polygon[I].X,Polygon[I].Y,0));}
 }
 for(const auto& Region:Coverage)UnionInputs.Add(Region.OuterPolygon);
 if(!UnionInputs.IsEmpty())
 {
  FEHBPolygonClipResult Union;if(!FEHBPolygonClipper::UnionXY(UnionInputs,Union)||!Union.HasRegions())return Fail(TEXT("OpeningUnionFailed"));
  auto AddPerimeter=[&](const TArray<FVector>& Loop){auto& Boundary=UnionBoundaries.AddDefaulted_GetRef();for(int32 I=0;I<Loop.Num();++I){Perimeter+=FVector::Distance(Loop[I],Loop[(I+1)%Loop.Num()]);Boundary.Add(FVector2d(Loop[I].X,Loop[I].Y));}};
  for(const auto& Region:Union.Regions){AddPerimeter(Region.OuterLoop);for(const auto& Hole:Region.HoleLoops){TArray<FVector> Points;for(const auto& P:Hole.Points)Points.Add(P.LocalPosition);AddPerimeter(Points);}}
 }
 if(FailAt(1))return Fail(TEXT("OpeningCandidateGenerationFailed"));
 FEHBStraightWallOpeningMeshSource MeshSource;MeshSource.Geometry=Geometry;MeshSource.Height=Height;MeshSource.Thickness=Thickness;
 MeshSource.bGenerateStartCap=Source.bGenerateStartCap;MeshSource.bGenerateEndCap=Source.bGenerateEndCap;MeshSource.Openings=Openings;
 FEHBStraightWallOpeningMeshes Meshes;if(!FEHBStraightWallOpeningMesh::Build(MeshSource,Meshes,Status))return false;
 Result.Left=MoveTemp(Meshes.Left);Result.Right=MoveTemp(Meshes.Right);Result.Caps=MoveTemp(Meshes.Caps);
 for(int32 Side=0;Side<2;++Side)
 {
  auto& Mesh=Side==0?Result.Left:Result.Right;
  if(FailAt(2)){auto Uncut=MeshSource;Uncut.Openings.Reset();FEHBStraightWallOpeningMeshes Wrong;if(!FEHBStraightWallOpeningMesh::Build(Uncut,Wrong,Status))return false;Mesh=Side==0?MoveTemp(Wrong.Left):MoveTemp(Wrong.Right);}
  const double Start=Side==0?Geometry.StartLeftX:Geometry.StartRightX,End=Side==0?Geometry.EndLeftX:Geometry.EndRightX;
  FEHBFloorSupportSurface Domain;Domain.OuterPolygon={{Start,0,0},{End,0,0},{End,Height,0},{Start,Height,0}};
  double Removed=0;if(!Coverage.IsEmpty()&&!FEHBFloorContactGeometry::MeasureArea(Coverage,{Domain},Removed,Status))return false;
  double Actual=0;
  // Independent clipping oracle detects the legacy uncut fallback as failure.
  // Quantization has a perimeter-dependent area envelope (0.001 cm grid).
  const double Tolerance=0.01+Perimeter*0.001;
  if(!Area(Mesh,Actual)||FMath::Abs(Actual-((End-Start)*Height-Removed))>Tolerance)return Fail(TEXT("OpeningCandidateAreaMismatch"));
  Result.RemovedArea=Removed;
 }
 const double BaseCaps=Meshes.BoundaryCapArea;
 double WithReveals=0;
 FEHBWallOpeningMeasureDomain Domain;Domain.StartLeft=Geometry.StartLeftX;Domain.StartRight=Geometry.StartRightX;Domain.EndLeft=Geometry.EndLeftX;Domain.EndRight=Geometry.EndRightX;Domain.Height=Height;Domain.Thickness=Thickness;double RevealArea=0;
 if(!FEHBWallOpeningMeasure::RevealArea(Domain,UnionBoundaries,RevealArea,Status))return false;
 // Existing reveal renderer emits both triangle orientations.
 if(!Area(Result.Caps,WithReveals)||FMath::Abs(WithReveals-BaseCaps-2*RevealArea)>0.1+Perimeter*0.001)
  {UE_LOG(LogTemp,Warning,TEXT("Opening reveal mismatch: actual=%f base=%f perimeter=%f thickness=%f"),WithReveals,BaseCaps,Perimeter,Thickness);return Fail(TEXT("OpeningCandidateRevealMismatch"));}
 if(!FEHBFloorContactGeometry::CaptureHorizontalTopsFromMesh(Result.Caps,ElementToBuilding*Source.BuildingToWorld,Source.BuildingToWorld,Result.HorizontalTops,Status))return false;
 if(!FEHBFloorContactGeometry::CaptureHorizontalBottomsFromMesh(Result.Caps,ElementToBuilding*Source.BuildingToWorld,Source.BuildingToWorld,Result.HorizontalBottoms,Status))return false;
 Out=MoveTemp(Result);Status=TEXT("Ready");return true;
}
