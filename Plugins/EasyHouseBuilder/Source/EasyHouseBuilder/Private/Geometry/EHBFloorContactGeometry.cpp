#include "Geometry/EHBFloorContactGeometry.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Wall.h"
#include "Actors/EHB_Pillar.h"
#include "Core/EHBWallJunctionMesh.h"
#include "ThirdParty/clipper/clipper.h"
#include "Arrangement2d.h"
#include "ConstrainedDelaunay2.h"

namespace
{
 constexpr double EHBContactClipperScale=1000.0;
 bool Path(const TArray<FVector>& Points,bool bOuter,ClipperLib::Path& Out,double& Z)
 {
  if(Points.Num()<3)return false;
  Z=Points[0].Z;
  for(const auto& P:Points)
  {
   if(P.ContainsNaN() || P.GetAbsMax()>1.e8 || FMath::Abs(P.Z-Z)>0.001)return false;
   ClipperLib::IntPoint Q(FMath::RoundToInt64(P.X*EHBContactClipperScale),FMath::RoundToInt64(P.Y*EHBContactClipperScale));
   if(Out.empty() || Out.back()!=Q)Out.push_back(Q);
  }
  if(Out.size()>1 && Out.front()==Out.back())Out.pop_back();
  if(Out.size()<3 || FMath::Abs(ClipperLib::Area(Out))<0.5)return false;
  if(ClipperLib::Orientation(Out)!=bOuter)ClipperLib::ReversePath(Out);
  return true;
 }
 template<class T> bool Paths(const T& Region,ClipperLib::Paths& Out,double& Z)
 {
  ClipperLib::Path Outer;
  if(!Path(Region.OuterPolygon,true,Outer,Z))return false;
  ClipperLib::Paths Holes;
  for(const auto& H:Region.Holes){ClipperLib::Path Hole;double HoleZ;if(!Path(H.LocalPolygon,true,Hole,HoleZ)||FMath::Abs(HoleZ-Z)>0.001)return false;Holes.push_back(MoveTemp(Hole));}
  if(Holes.empty()){Out.push_back(MoveTemp(Outer));return true;}
  ClipperLib::Clipper C;C.AddPath(Outer,ClipperLib::ptSubject,true);C.AddPaths(Holes,ClipperLib::ptClip,true);
  return C.Execute(ClipperLib::ctDifference,Out,ClipperLib::pftNonZero,ClipperLib::pftNonZero);
 }
 bool Inside(const FVector2d& P,const ClipperLib::Path& Loop)
 {
  bool Result=false;
  for(size_t I=0,J=Loop.size()-1;I<Loop.size();J=I++)
  {
   const FVector2d A(Loop[I].X/EHBContactClipperScale,Loop[I].Y/EHBContactClipperScale),B(Loop[J].X/EHBContactClipperScale,Loop[J].Y/EHBContactClipperScale);
   if((A.Y>P.Y)!=(B.Y>P.Y) && P.X<(B.X-A.X)*(P.Y-A.Y)/(B.Y-A.Y)+A.X)Result=!Result;
  }
  return Result;
 }
 bool InteriorPoint(const ClipperLib::PolyNode* N,double Z,FVector& Point)
 {
  UE::Geometry::FArrangement2d A(0.001);
  auto Insert=[&](const ClipperLib::Path& P){for(size_t I=0;I<P.size();++I){const auto& Q=P[(I+1)%P.size()];A.Insert(FVector2d(P[I].X/EHBContactClipperScale,P[I].Y/EHBContactClipperScale),FVector2d(Q.X/EHBContactClipperScale,Q.Y/EHBContactClipperScale));}};
  Insert(N->Contour);for(const auto* H:N->Childs)if(H->IsHole())Insert(H->Contour);
  UE::Geometry::FConstrainedDelaunay2d T;T.FillRule=UE::Geometry::FConstrainedDelaunay2d::EFillRule::Odd;T.bOrientedEdges=false;T.Add(A.Graph);
  if(!T.Triangulate())return false;
  double Best=0;
  for(const auto& Tri:T.Triangles)
  {
   const auto P=(T.Vertices[Tri.A]+T.Vertices[Tri.B]+T.Vertices[Tri.C])/3;
   if(!Inside(P,N->Contour))continue;
   bool Hole=false;for(const auto* H:N->Childs)if(H->IsHole() && Inside(P,H->Contour))Hole=true;
   const auto U=T.Vertices[Tri.B]-T.Vertices[Tri.A],V=T.Vertices[Tri.C]-T.Vertices[Tri.A];
   const double Area=FMath::Abs(U.X*V.Y-U.Y*V.X)*0.5;
   if(!Hole && Area>Best){Best=Area;Point=FVector(P.X,P.Y,Z);}
  }
  return Best>0;
 }
}

static bool BuildContact(const TArray<FEHBFloorFinishRegion>& Coverage,
 const TArray<FEHBFloorSupportSurface>& Tops,FEHBFloorContact& Out,FName& Status,bool bComputePoint)
{
 Out={};auto Fail=[&](FName Why){Status=Why;return false;};
 if(Coverage.IsEmpty())return Fail(TEXT("EmptyContactCoverage"));
 struct FPlane {double Z;ClipperLib::Paths Polygons;};TArray<FPlane> Planes;
 for(const auto& Region:Coverage)
 {
  ClipperLib::Paths P;double Z;if(!Paths(Region,P,Z))return Fail(TEXT("InvalidContactCoverage"));
  auto* Plane=Planes.FindByPredicate([&](const auto& X){return FMath::Abs(X.Z-Z)<=0.001;});
  if(!Plane)Plane=&Planes.Add_GetRef({Z,{}});
  Plane->Polygons.insert(Plane->Polygons.end(),P.begin(),P.end());
 }
 TArray<FPlane> Hosts;
 for(const auto& Top:Tops){FPlane H;if(!Paths(Top,H.Polygons,H.Z))return Fail(TEXT("InvalidContactTop"));Hosts.Add(MoveTemp(H));}
 FEHBFloorContact Result;double Largest=0;
 for(const auto& Plane:Planes)
 {
  ClipperLib::Paths HostPaths;for(const auto& H:Hosts)if(FMath::Abs(H.Z-Plane.Z)<=1.0)HostPaths.insert(HostPaths.end(),H.Polygons.begin(),H.Polygons.end());
  if(HostPaths.empty())continue;
  // Remove internal triangulation edges before intersection. Clipping adjacent
  // triangles separately rounds their diagonal intersections on the integer
  // grid, introducing slivers that a single equivalent surface does not have.
  ClipperLib::Paths HostUnion,FloorUnion;
  ClipperLib::Clipper HostMerge;HostMerge.AddPaths(HostPaths,ClipperLib::ptSubject,true);
  ClipperLib::Clipper FloorMerge;FloorMerge.AddPaths(Plane.Polygons,ClipperLib::ptSubject,true);
  if(!HostMerge.Execute(ClipperLib::ctUnion,HostUnion,ClipperLib::pftNonZero,ClipperLib::pftNonZero)
   ||!FloorMerge.Execute(ClipperLib::ctUnion,FloorUnion,ClipperLib::pftNonZero,ClipperLib::pftNonZero))return Fail(TEXT("ContactUnionFailed"));
  ClipperLib::Clipper C;C.AddPaths(FloorUnion,ClipperLib::ptSubject,true);C.AddPaths(HostUnion,ClipperLib::ptClip,true);ClipperLib::PolyTree Tree;
  if(!C.Execute(ClipperLib::ctIntersection,Tree,ClipperLib::pftNonZero,ClipperLib::pftNonZero))return Fail(TEXT("ContactIntersectionFailed"));
  for(const auto* N=Tree.GetFirst();N;N=N->GetNext())
  {
   if(N->IsHole())continue;
   double Area=FMath::Abs(ClipperLib::Area(N->Contour));for(const auto* H:N->Childs)if(H->IsHole())Area-=FMath::Abs(ClipperLib::Area(H->Contour));Area/=EHBContactClipperScale*EHBContactClipperScale;
   if(Area<=0)continue;
   Result.Area+=Area;
   if(bComputePoint&&Area>Largest){FVector Point;if(!InteriorPoint(N,Plane.Z,Point))return Fail(TEXT("ContactInteriorPointFailed"));Result.Point=Point;Largest=Area;}
  }
 }
 Out=Result;Status=Result.Area>0?TEXT("Contact"):TEXT("NoContact");return true;
}

bool FEHBFloorContactGeometry::Build(const TArray<FEHBFloorFinishRegion>& Coverage,
 const TArray<FEHBFloorSupportSurface>& Tops,FEHBFloorContact& Out,FName& Status)
{
 return BuildContact(Coverage,Tops,Out,Status,true);
}

bool FEHBFloorContactGeometry::MeasureArea(const TArray<FEHBFloorFinishRegion>& Coverage,
 const TArray<FEHBFloorSupportSurface>& Tops,double& OutArea,FName& Status)
{
 OutArea=0;FEHBFloorContact Contact;
 if(!BuildContact(Coverage,Tops,Contact,Status,false))return false;
 OutArea=Contact.Area;return true;
}

bool FEHBFloorContactGeometry::CaptureHorizontalTopsFromMesh(const FEHBWallJunctionMesh& Mesh,
  const FTransform& Transform,const FTransform& Building,
  TArray<FEHBFloorSupportSurface>& Out,FName& Status)
 {
  Out.Reset();
  if(Transform.ContainsNaN()||Building.ContainsNaN()||!Transform.IsValid()||!Building.IsValid()
   ||Transform.GetScale3D().GetAbsMin()<UE_SMALL_NUMBER||Building.GetScale3D().GetAbsMin()<UE_SMALL_NUMBER)
   {Status=TEXT("InvalidStructuralContactFrame");return false;}
  if(Mesh.Triangles.Num()%3!=0){Status=TEXT("InvalidStructuralContactMesh");return false;}
  TArray<FEHBFloorSupportSurface> Result;
  for(int32 I=0;I<Mesh.Triangles.Num();I+=3)
  {
   FEHBFloorSupportSurface Top;FVector Normal=FVector::ZeroVector;
   for(int32 K=0;K<3;++K)
   {
    const int32 Index=Mesh.Triangles[I+K];
    if(!Mesh.Vertices.IsValidIndex(Index)||!Mesh.Normals.IsValidIndex(Index)){Status=TEXT("InvalidStructuralContactMesh");return false;}
    if(Mesh.Vertices[Index].ContainsNaN()||Mesh.Normals[Index].ContainsNaN()){Status=TEXT("InvalidStructuralContactMesh");return false;}
    Top.OuterPolygon.Add(Building.InverseTransformPosition(Transform.TransformPosition(Mesh.Vertices[Index])));
    Normal+=Building.InverseTransformVectorNoScale(Transform.TransformVectorNoScale(Mesh.Normals[Index]));
   }
   if(Top.OuterPolygon.ContainsByPredicate([](const auto& P){return P.ContainsNaN();})){Status=TEXT("InvalidStructuralContactMesh");return false;}
   if(Normal.GetSafeNormal().Z<0.99||FMath::Abs(Top.OuterPolygon[0].Z-Top.OuterPolygon[1].Z)>0.001||FMath::Abs(Top.OuterPolygon[0].Z-Top.OuterPolygon[2].Z)>0.001)continue;
   FVector2d Q[3];for(int32 K=0;K<3;++K)Q[K]=FVector2d(FMath::RoundToDouble(Top.OuterPolygon[K].X*EHBContactClipperScale),FMath::RoundToDouble(Top.OuterPolygon[K].Y*EHBContactClipperScale));
   const auto U=Q[1]-Q[0],V=Q[2]-Q[0];if(FMath::Abs(U.X*V.Y-U.Y*V.X)<0.5)continue;
   Result.Add(MoveTemp(Top));
  }
  Out=MoveTemp(Result);Status=TEXT("CapturedStructural");return true;
 }

bool FEHBFloorContactGeometry::CaptureHorizontalBottomsFromMesh(const FEHBWallJunctionMesh& Source,
 const FTransform& Frame,const FTransform& Building,TArray<FEHBFloorFinishRegion>& Out,FName& Status)
{
 Out.Reset();auto Mesh=Source;for(auto& N:Mesh.Normals)N*=-1;
 TArray<FEHBFloorSupportSurface> Bottom;if(!CaptureHorizontalTopsFromMesh(Mesh,Frame,Building,Bottom,Status))return false;
 for(auto& Surface:Bottom){auto& R=Out.AddDefaulted_GetRef();R.OuterPolygon=MoveTemp(Surface.OuterPolygon);for(auto& H:Surface.Holes)R.Holes.AddDefaulted_GetRef().LocalPolygon=MoveTemp(H.LocalPolygon);}
 Status=TEXT("CapturedStructuralBottom");return true;
}

bool FEHBFloorContactGeometry::CapturePillarBottom(const AEHB_Pillar* Pillar,TArray<FEHBFloorFinishRegion>& Out,FName& Status)
{
 Out.Reset();auto Fail=[&](FName Why){Status=Why;return false;};
 if(!IsValid(Pillar)||!Pillar->OwningBuilding||Pillar->GetClass()!=AEHB_Pillar::StaticClass()||Pillar->ShapeType!=EEHBPillarShapeType::Polygon)return Fail(TEXT("UnsupportedPillarBottomHost"));
 if(!FMath::IsFinite(Pillar->Height)||!FMath::IsFinite(Pillar->Width)||!FMath::IsFinite(Pillar->Depth))return Fail(TEXT("InvalidPillarBottomSource"));
 const auto Frame=Pillar->GetActorTransform(),Building=Pillar->OwningBuilding->GetActorTransform();
 if(!Frame.IsValid()||!Building.IsValid()||Frame.GetScale3D().GetMin()<=0||Building.GetScale3D().GetMin()<=0)return Fail(TEXT("InvalidPillarBottomFrame"));
 const auto Up=Building.InverseTransformVectorNoScale(Frame.TransformVectorNoScale(FVector::UpVector));
 if(!Up.Equals(FVector::UpVector,0.000001))return Fail(TEXT("TiltedPillarBottomRequiresPlan"));
 FEHBWallJunctionMesh Mesh;if(!Pillar->BuildStructuralContactMesh(Mesh))return Fail(TEXT("InvalidPillarBottomSource"));
 // Invert only selection normals, leaving the structural positions untouched.
 // The shared horizontal-face adapter now selects the actual underside caps.
 for(auto& N:Mesh.Normals)N*=-1;
 TArray<FEHBFloorSupportSurface> Bottom;if(!CaptureHorizontalTopsFromMesh(Mesh,Frame,Building,Bottom,Status))return false;
 if(Bottom.IsEmpty())return Fail(TEXT("MissingPillarBottom"));
 for(auto& Surface:Bottom){auto& R=Out.AddDefaulted_GetRef();R.OuterPolygon=MoveTemp(Surface.OuterPolygon);for(auto& H:Surface.Holes)R.Holes.AddDefaulted_GetRef().LocalPolygon=MoveTemp(H.LocalPolygon);}
 Status=TEXT("CapturedPillarBottom");return true;
}

bool FEHBFloorContactGeometry::CaptureSlabBottom(const AEHB_FloorSlab* Slab,TArray<FEHBFloorFinishRegion>& Out,FName& Status)
{
 Out.Reset();auto Fail=[&](FName Why){Status=Why;return false;};
 if(!IsValid(Slab)||!Slab->OwningBuilding||Slab->GetClass()!=AEHB_FloorSlab::StaticClass())return Fail(TEXT("UnsupportedSlabBottomHost"));
 if(Slab->bIsFoundation&&Slab->bKeepFoundationBottomOnGround)return Fail(TEXT("GroundConformingBottomRequiresPlan"));
 if(!FMath::IsFinite(Slab->Thickness)||!FMath::IsFinite(Slab->GetBottomZ()))return Fail(TEXT("InvalidSlabBottomSource"));
 const auto Frame=Slab->GetActorTransform(),Building=Slab->OwningBuilding->GetActorTransform();
 if(!Frame.IsValid()||!Building.IsValid()||Frame.GetScale3D().GetMin()<=0||Building.GetScale3D().GetMin()<=0)return Fail(TEXT("InvalidSlabBottomFrame"));
 const FVector Up=Building.InverseTransformVectorNoScale(Frame.TransformVectorNoScale(FVector::UpVector));
 if(!Up.Equals(FVector::UpVector,0.000001))return Fail(TEXT("TiltedSlabBottomRequiresPlan"));
 FEHBPolygonClipResult Clipped;if(!Slab->BuildLogicalTopRegions(Clipped))return Fail(TEXT("InvalidSlabBottomSource"));
 TArray<FEHBFloorFinishRegion> Result;
 auto Convert=[&](TArray<FVector>& Points){for(auto& P:Points){P.Z=Slab->GetBottomZ();P=Building.InverseTransformPosition(Frame.TransformPosition(P));}};
 for(const auto& R:Clipped.Regions)
 {
  auto& Bottom=Result.AddDefaulted_GetRef();Bottom.OuterPolygon=R.OuterLoop;Convert(Bottom.OuterPolygon);
  for(const auto& H:R.HoleLoops){auto& Hole=Bottom.Holes.AddDefaulted_GetRef();Hole.LocalPolygon=H.ToLocalPositions();Convert(Hole.LocalPolygon);}
 }
 for(const auto& R:Result){for(const auto& P:R.OuterPolygon)if(P.ContainsNaN())return Fail(TEXT("InvalidSlabBottomSource"));for(const auto& H:R.Holes)for(const auto& P:H.LocalPolygon)if(P.ContainsNaN())return Fail(TEXT("InvalidSlabBottomSource"));}
 Out=MoveTemp(Result);Status=TEXT("CapturedSlabBottom");return true;
}

bool FEHBFloorContactGeometry::CaptureHorizontalTops(const AEHBElementActorBase* Host,TArray<FEHBFloorSupportSurface>& Out,FName& Status)
{
 Out.Reset();if(!Host || !Host->OwningBuilding){Status=TEXT("MissingContactHost");return false;}
 // Slab display expansion is not a support surface. Use the effective authored
 // outline/cutters, without expanding its top for display.
 if(const auto* Slab=Cast<AEHB_FloorSlab>(Host))
 {
  FEHBPolygonClipResult Regions;
  if(!Slab->BuildLogicalTopRegions(Regions)){Status=TEXT("InvalidContactSlab");return false;}
  const auto Building=Host->OwningBuilding->GetActorTransform();const auto Transform=Host->GetActorTransform();
  auto Convert=[&](TArray<FVector>& Points){for(auto& P:Points){P.Z=Slab->GetTopZ();P=Building.InverseTransformPosition(Transform.TransformPosition(P));}};
  for(const auto& Region:Regions.Regions)
  {
   FEHBFloorSupportSurface Top;Top.OuterPolygon=Region.OuterLoop;Convert(Top.OuterPolygon);
   for(const auto& H:Region.HoleLoops){auto Hole=H.ToLocalPositions();Convert(Hole);Top.Holes.AddDefaulted_GetRef().LocalPolygon=MoveTemp(Hole);}
   Out.Add(MoveTemp(Top));
  }
  Status=TEXT("Captured");return true;
 }
 // Wall caps/reveals and polygon columns have explicit undecorated structural sources.
 // The compatibility route below remains for legacy host types without that contract.
 if(const auto* Wall=Cast<AEHB_Wall>(Host))
 {
  FEHBWallJunctionMesh Mesh;if(!Wall->BuildStructuralContactMesh(Mesh)){Status=TEXT("InvalidStructuralWall");return false;}
  return CaptureHorizontalTopsFromMesh(Mesh,Host->GetActorTransform(),Host->OwningBuilding->GetActorTransform(),Out,Status);
 }
 if(const auto* Pillar=Cast<AEHB_Pillar>(Host);Pillar&&Pillar->ShapeType==EEHBPillarShapeType::Polygon)
 {
  FEHBWallJunctionMesh Mesh;if(!Pillar->BuildStructuralContactMesh(Mesh)){Status=TEXT("InvalidStructuralPillar");return false;}
  return CaptureHorizontalTopsFromMesh(Mesh,Host->GetActorTransform(),Host->OwningBuilding->GetActorTransform(),Out,Status);
 }
 return CaptureGeneratedHorizontalTops(Host,Out,Status);
}

bool FEHBFloorContactGeometry::CaptureGeneratedHorizontalTops(const AEHBElementActorBase* Host,TArray<FEHBFloorSupportSurface>& Out,FName& Status)
{
 Out.Reset();if(!Host||!Host->OwningBuilding){Status=TEXT("MissingContactHost");return false;}
 TArray<UEHBGeneratedMeshComponent*> Meshes;Host->GetGeneratedMeshComponents(Meshes);
 if(Meshes.IsEmpty()){Status=TEXT("UnsupportedContactHostGeometry");return false;}
 TArray<FEHBFloorSupportSurface> Result;const auto BuildingTransform=Host->OwningBuilding->GetActorTransform();
 for(const auto* Mesh:Meshes)for(int32 S=0;S<Mesh->GetNumSections();++S)if(const auto* Section=Mesh->GetProcMeshSection(S))
 {
  if(Section->ProcIndexBuffer.Num()%3!=0){Status=TEXT("InvalidContactMesh");return false;}
  for(int32 I=0;I<Section->ProcIndexBuffer.Num();I+=3)
  {
   FEHBFloorSupportSurface Top;FVector Normal=FVector::ZeroVector;
   for(int32 K=0;K<3;++K)
   {
    const uint32 Index=Section->ProcIndexBuffer[I+K];if(!Section->ProcVertexBuffer.IsValidIndex(Index)){Status=TEXT("InvalidContactMesh");return false;}
    const auto& V=Section->ProcVertexBuffer[Index];Top.OuterPolygon.Add(BuildingTransform.InverseTransformPosition(Mesh->GetComponentTransform().TransformPosition(V.Position)));
    Normal+=BuildingTransform.InverseTransformVectorNoScale(Mesh->GetComponentTransform().TransformVectorNoScale(V.Normal));
   }
   if(Top.OuterPolygon.ContainsByPredicate([](const auto& P){return P.ContainsNaN();})){Status=TEXT("InvalidContactMesh");return false;}
   if(Normal.GetSafeNormal().Z<0.99 || FMath::Abs(Top.OuterPolygon[0].Z-Top.OuterPolygon[1].Z)>0.001 || FMath::Abs(Top.OuterPolygon[0].Z-Top.OuterPolygon[2].Z)>0.001)continue;
   if(FVector::CrossProduct(Top.OuterPolygon[1]-Top.OuterPolygon[0],Top.OuterPolygon[2]-Top.OuterPolygon[0]).SizeSquared()<1.e-12)continue;
   // Generated caps may contain tiny sliver triangles that collapse on the contact
   // clipping grid. They contribute zero quantized area, not an invalid host.
   FVector2d Q[3];for(int32 K=0;K<3;++K)Q[K]=FVector2d(FMath::RoundToDouble(Top.OuterPolygon[K].X*EHBContactClipperScale),FMath::RoundToDouble(Top.OuterPolygon[K].Y*EHBContactClipperScale));
   const auto U=Q[1]-Q[0],V=Q[2]-Q[0];if(FMath::Abs(U.X*V.Y-U.Y*V.X)<0.5)continue;
   Result.Add(MoveTemp(Top));
  }
 }
 Out=MoveTemp(Result);Status=TEXT("Captured");return true;
}

namespace
{
 template<class A,class B> bool SameContactRegions(const TArray<A>& Left,const TArray<B>& Right)
 {
  if(Left.Num()!=Right.Num())return false;
  for(int32 I=0;I<Left.Num();++I)
  {
   // Deliberately exact, including vertex order and holes. No tolerance or hash collision
   // can reuse an earlier surface after a small direct edit.
   if(Left[I].OuterPolygon!=Right[I].OuterPolygon || Left[I].Holes.Num()!=Right[I].Holes.Num())return false;
   for(int32 H=0;H<Left[I].Holes.Num();++H)
    if(Left[I].Holes[H].LocalPolygon!=Right[I].Holes[H].LocalPolygon)return false;
  }
  return true;
 }
 template<class T> int64 ContactPointCount(const TArray<T>& Regions)
 {
  int64 Count=0;
  for(const auto& R:Regions){Count+=R.OuterPolygon.Num();for(const auto& H:R.Holes)Count+=H.LocalPolygon.Num();}
  return Count;
 }
}

void FEHBFloorContactCache::Remove(FGuid Host)
{
 if(const auto* Old=Entries.Find(Host)){Stats.StoredPoints-=Old->Points;Entries.Remove(Host);++Stats.Evictions;}
 Stats.Entries=Entries.Num();
}

void FEHBFloorContactCache::RetainHosts(const TSet<FGuid>& Hosts)
{
 TArray<FGuid> Removed;
 for(const auto& Pair:Entries)if(!Hosts.Contains(Pair.Key))Removed.Add(Pair.Key);
 for(FGuid Id:Removed)Remove(Id);
}

bool FEHBFloorContactCache::Query(FGuid Host,const TArray<FEHBFloorFinishRegion>& Coverage,
 const TArray<FEHBFloorSupportSurface>& Tops,FEHBFloorContact& Out,FName& Status)
{
 Out={};
 if(!Host.IsValid()){Status=TEXT("InvalidContactCacheHost");return false;}
 if(const auto* Entry=Entries.Find(Host))
 {
  if(SameContactRegions(Coverage,Entry->Coverage)&&SameContactRegions(Tops,Entry->Tops))
  {Out=Entry->Contact;Status=Entry->Status;++Stats.Hits;return true;}
 }
 // Remove before solving: errors cannot leave a stale successful entry for this source.
 Remove(Host);++Stats.Solves;
 if(!FEHBFloorContactGeometry::Build(Coverage,Tops,Out,Status))return false;
 const int64 Points=ContactPointCount(Coverage)+ContactPointCount(Tops);
 if(MaxEntries==0 || Points>MaxPoints)return true;
 // Deterministic bounded retention. Clearing affects work only, never model semantics.
 if(Entries.Num()>=MaxEntries || Stats.StoredPoints+Points>MaxPoints)
 {Stats.Evictions+=Entries.Num();Entries.Reset();Stats.StoredPoints=0;}
 FEntry Entry;Entry.Coverage=Coverage;Entry.Tops=Tops;Entry.Contact=Out;Entry.Status=Status;Entry.Points=Points;
 Entries.Add(Host,MoveTemp(Entry));Stats.StoredPoints+=Points;Stats.Entries=Entries.Num();return true;
}

bool FEHBFloorFinishContactDraft::Build(TArray<FEHBElementRelation>& Out,FName& Status) const
{
 Out.Reset();auto Fail=[&](FName Why){Status=Why;return false;};
 if(!FloorGuid.IsValid()||(bIndependentRegion?RoomGuid.IsValid():!RoomGuid.IsValid())||FloorIndex<1||Regions.IsEmpty())return Fail(TEXT("InvalidFinishDestination"));
 TMap<FGuid,const FEHBElementRelation*> Old;TSet<FGuid> Ids;
 for(const auto& R:Previous)
 {
  if(!R.RelationGuid.IsValid()||!R.Source.IsValid()||!R.Target.IsValid()||Ids.Contains(R.RelationGuid)||Old.Contains(R.Source.ElementGuid)
   ||R.Type!=EEHBElementRelationType::SurfaceFinish||!R.bEnabled||!R.bGeometryDependent||R.bAffectsFloorAssignment
   ||R.Origin!=EEHBRelationOrigin::SystemGenerated||R.Source.Kind!=EEHBRelationEndpointKind::BuildingElement||!R.Source.ElementGuid.IsValid()
   ||R.Target.Kind!=EEHBRelationEndpointKind::BuildingElement||R.Target.ElementGuid!=FloorGuid
   ||R.Source.SurfaceKind!=EEHBElementSurfaceKind::Top||R.Target.SurfaceKind!=EEHBElementSurfaceKind::Bottom
   ||R.Source.SurfaceName!=TEXT("FloorFinish.SourceTop")||R.Target.SurfaceName!=TEXT("FloorFinish.Bottom")
   ||R.Source.SubIndex!=INDEX_NONE||R.Target.SubIndex!=INDEX_NONE)return Fail(TEXT("InvalidPreviousFinishDraft"));
  Ids.Add(R.RelationGuid);Old.Add(R.Source.ElementGuid,&R);
 }
 // Validate coverage even when no host intersects it.
 FEHBFloorContact Empty;if(!FEHBFloorContactGeometry::Build(Regions,{},Empty,Status))return false;
 TArray<FGuid> Keys;Hosts.GetKeys(Keys);Keys.Sort();TArray<FEHBElementRelation> Planned;
 for(FGuid Host:Keys)
 {
  if(!Host.IsValid()||Host==FloorGuid)return Fail(TEXT("InvalidFinishDraftHost"));
  FEHBFloorContact Contact;if(!FEHBFloorContactGeometry::Build(Regions,Hosts[Host],Contact,Status))return false;if(Contact.Area<=0)continue;
  const auto* PreviousRelation=Old.FindRef(Host);FEHBElementRelation R=PreviousRelation?*PreviousRelation:FEHBElementRelation();
  if(!PreviousRelation){R.RelationGuid=FGuid::NewGuid();R.Type=EEHBElementRelationType::SurfaceFinish;R.Origin=EEHBRelationOrigin::SystemGenerated;R.bGeometryDependent=true;R.bAffectsFloorAssignment=false;R.Source=FEHBElementRelationEndpoint::MakeElement(Host,EEHBElementSurfaceKind::Top,TEXT("FloorFinish.SourceTop"));R.Target=FEHBElementRelationEndpoint::MakeElement(FloorGuid,EEHBElementSurfaceKind::Bottom,TEXT("FloorFinish.Bottom"));}
  R.ContactArea=Contact.Area;R.ContactPoint=Contact.Point;R.ContactNormal=FVector::UpVector;
  R.StringMetadata.Reset();
  if(bIndependentRegion)R.StringMetadata.Add(TEXT("OutlineSource"),TEXT("RetainedRegion"));
  else R.StringMetadata.Add(TEXT("RoomLoopGuid"),RoomGuid.ToString(EGuidFormats::DigitsWithHyphens));
  R.NumericMetadata.Add(TEXT("RoomFloorIndex"),FloorIndex);Planned.Add(MoveTemp(R));
 }
 Out=MoveTemp(Planned);Status=TEXT("Ready");return true;
}
