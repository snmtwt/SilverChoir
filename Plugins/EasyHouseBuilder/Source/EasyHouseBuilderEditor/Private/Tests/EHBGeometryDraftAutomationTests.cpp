// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS
#include "Core/EHBWallNodeGeometryDraft.h"
#include "Core/EHBWallTopology.h"
#include "EHBRoomFinishMove.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonSerializer.h"
#include "Components/EHBWallJunctionComponent.h"
#include "EHB_Building.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "JsonObjectConverter.h"

namespace
{
 FEHBWallNodeModel MakeGeometryDraftRooms(int32 Rooms,int32 BindingMode,float Yaw)
 {
  FEHBWallNodeModel Model;Model.Version=1;
  for(int32 R=0;R<Rooms;++R)
  {
   const FTransform Pose(FRotator(0,Yaw,0),FVector(R*2000,0,(R%3)*350));
   const int32 First=Model.Nodes.Num();
   for(const FVector P:{FVector(0,0,0),FVector(600,0,0),FVector(600,500,0),FVector(0,500,0)})
   {
    auto& N=Model.Nodes.AddDefaulted_GetRef();N.NodeGuid=FGuid::NewGuid();N.FloorIndex=R%3+1;
    N.LocalTransform=FTransform(Pose.GetRotation(),Pose.TransformPosition(P));N.JunctionDimensions=FVector(20,20,300);
    if(BindingMode==2||(BindingMode==1&&Model.Nodes.Num()%2==0))Model.PillarBindings.Add({N.NodeGuid,FGuid::NewGuid()});
   }
   for(int32 I=0;I<4;++I){auto& W=Model.Walls.AddDefaulted_GetRef();W.WallGuid=FGuid::NewGuid();W.StartNodeGuid=Model.Nodes[First+I].NodeGuid;W.EndNodeGuid=Model.Nodes[First+(I+1)%4].NodeGuid;}
  }
  return Model;
 }
 bool SameSides(const TArray<FEHBWallJunctionWallSides>& A,const TArray<FEHBWallJunctionWallSides>& B)
 {
  if(A.Num()!=B.Num())return false;
  for(int32 I=0;I<A.Num();++I)if(A[I].WallGuid!=B[I].WallGuid||!A[I].LocalTransform.Equals(B[I].LocalTransform,1.e-5)
   ||!A[I].LocalStart.Equals(B[I].LocalStart,1.e-5)||!A[I].LocalEnd.Equals(B[I].LocalEnd,1.e-5)
   ||!A[I].StartLeft.Equals(B[I].StartLeft,1.e-5)||!A[I].EndLeft.Equals(B[I].EndLeft,1.e-5)
   ||!A[I].StartRight.Equals(B[I].StartRight,1.e-5)||!A[I].EndRight.Equals(B[I].EndRight,1.e-5))return false;
  return true;
 }
 bool SameMesh(const FEHBWallJunctionMesh& A,const FEHBWallJunctionMesh& B)
 {
  auto Vectors=[](const auto& X,const auto& Y){if(X.Num()!=Y.Num())return false;for(int32 I=0;I<X.Num();++I)if(!X[I].Equals(Y[I],1.e-5))return false;return true;};
  return A.NodeGuid==B.NodeGuid&&A.LocalTransform.Equals(B.LocalTransform,1.e-5)&&A.Triangles==B.Triangles
   &&Vectors(A.Footprint,B.Footprint)&&Vectors(A.Vertices,B.Vertices)&&Vectors(A.Normals,B.Normals)&&Vectors(A.UVs,B.UVs);
 }
 bool LegacyCandidateGeometry(const FEHBWallNodeModel& Model,FName& Reason)
 {
  for(const auto& N:Model.Nodes)if(!Model.PillarBindings.ContainsByPredicate([&](const auto& P){return P.NodeGuid==N.NodeGuid;}))
  {FEHBWallJunctionMesh Mesh;if(!UEHBWallTopologyLibrary::BuildWallNodeModelJunctionMesh(Model,N.NodeGuid,Mesh,Reason))return false;}
  TArray<FEHBWallJunctionWallSides> Sides;TMap<FGuid,TArray<FEHBFloorSupportSurface>> Tops;
  // Former candidate side solve, finish-top solve, and renderer side solve.
  return UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Reason)
   &&EHBRoomFinishMove::BuildTopologyTops(Model,Tops,Reason)
   &&UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Reason);
 }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBGeometrySideDraftTest,"EHB.Topology.GeometrySideDraft",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBGeometrySideDraftTest::RunTest(const FString& Parameters)
{
 FName Reason;
 for(int32 Bound=0;Bound<3;++Bound)for(float Yaw:{0.f,17.f,45.f,117.f})
 {
  auto Model=MakeGeometryDraftRooms(3,Bound,Yaw);const auto Draft=FEHBWallNodeSideDraft::Build(Model,Reason);
  if(!TestTrue(*Reason.ToString(),Draft.IsReady()))return false;
  TArray<FEHBWallJunctionWallSides> Sides;if(!UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Reason))return false;
  TestEqual(TEXT("All candidate sides are owned"),Draft.GetWallSides().Num(),Sides.Num());
  for(int32 I=0;I<Sides.Num();++I)
  {
   const auto& A=Sides[I];const auto& B=Draft.GetWallSides()[I];
   TestTrue(TEXT("Owned side geometry exactly matches direct solve"),A.WallGuid==B.WallGuid&&A.LocalStart==B.LocalStart&&A.LocalEnd==B.LocalEnd
    &&A.LocalTransform.GetLocation()==B.LocalTransform.GetLocation()&&A.LocalTransform.GetRotation()==B.LocalTransform.GetRotation()&&A.LocalTransform.GetScale3D()==B.LocalTransform.GetScale3D()
    &&A.StartLeft==B.StartLeft&&A.EndLeft==B.EndLeft&&A.StartRight==B.StartRight&&A.EndRight==B.EndRight);
  }
  TMap<FGuid,TArray<FEHBFloorSupportSurface>> Expected,Actual;
  if(!EHBRoomFinishMove::BuildTopologyTops(Model,Expected,Reason)||!EHBRoomFinishMove::BuildTopologyTops(Draft,Actual,Reason))return false;
  TestEqual(TEXT("Side draft preserves exact physical host domain"),Actual.Num(),Expected.Num());
  for(const auto& Pair:Expected){const auto* Top=Actual.Find(Pair.Key);TestTrue(TEXT("Side draft yields exact top polygon"),Top&&Top->Num()==Pair.Value.Num()&&(*Top)[0].OuterPolygon==Pair.Value[0].OuterPolygon);}
  const auto Original=Model.Nodes[0].LocalTransform;Model.Nodes[0].LocalTransform.AddToTranslation(FVector(-20,-10,0));
  TestTrue(TEXT("Later source edit cannot mutate prepared side model"),Draft.GetModel().Nodes[0].LocalTransform.Equals(Original,0.0));
  const auto Fresh=FEHBWallNodeSideDraft::Build(Model,Reason);TestTrue(TEXT("Next candidate is independently solved"),Fresh.IsReady()&&!SameSides(Fresh.GetWallSides(),Draft.GetWallSides()));
 }
 auto Model=MakeGeometryDraftRooms(1,0,0);
 for(int32 Case=0;Case<3;++Case)
 {
  auto Bad=Model;if(Case==0)Bad.Walls[0].EndNodeGuid=FGuid::NewGuid();else if(Case==1){const auto Duplicate=Bad.Nodes[0];Bad.Nodes.Add(Duplicate);}else for(auto& N:Bad.Nodes)N.LocalTransform.SetLocation(N.LocalTransform.GetLocation()*.03);
  const auto Draft=FEHBWallNodeSideDraft::Build(Bad,Reason);TestFalse(TEXT("Invalid source cannot yield ready sides"),Draft.IsReady());TestTrue(TEXT("Failure publishes no source sides or counters"),Draft.GetModel().Nodes.IsEmpty()&&Draft.GetWallSides().IsEmpty()&&Draft.GetSolveStats().WallSidesSolved==0);
  TMap<FGuid,TArray<FEHBFloorSupportSurface>> Tops;Tops.Add(FGuid::NewGuid(),{});TestFalse(TEXT("Invalid sides cannot be used for planned hosts"),EHBRoomFinishMove::BuildTopologyTops(Draft,Tops,Reason));TestTrue(TEXT("Failed adapter clears previous output"),Tops.IsEmpty());
 }
 FEHBWallNodeModel Empty;Empty.Version=1;TestTrue(TEXT("Versioned empty model retains empty ready sides"),FEHBWallNodeSideDraft::Build(Empty,Reason).IsReady());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBGeometryDraftTest,"EHB.Topology.GeometryDraft",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBGeometryDraftTest::RunTest(const FString& Parameters)
{
 FName Reason;
 for(int32 Bound=0;Bound<3;++Bound)for(float Yaw:{0.f,17.f,45.f,117.f})
 {
  auto Model=MakeGeometryDraftRooms(3,Bound,Yaw);const auto Draft=FEHBWallNodeGeometryDraft::Build(Model,Reason);
  if(!TestTrue(*Reason.ToString(),Draft.IsReady()))return false;
  TestEqual(TEXT("Each wall-side node footprint solved once"),Draft.GetSolveStats().NodeFootprintsSolved,Model.Nodes.Num());
  TestEqual(TEXT("Each wall side solved once"),Draft.GetSolveStats().WallSidesSolved,Model.Walls.Num());
  TArray<FEHBWallJunctionWallSides> Sides;TestTrue(TEXT("Independent side reference succeeds"),UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Reason));
  TestTrue(TEXT("All side corners and transforms match independent reference"),SameSides(Sides,Draft.GetWallSides()));
  TestEqual(TEXT("Derived mesh cannot become a bound physical host"),Draft.GetUnboundJunctions().Num(),Model.Nodes.Num()-Model.PillarBindings.Num());
  for(const auto& Pair:Draft.GetUnboundJunctions())
  {
   FEHBWallJunctionMesh Reference;TestTrue(TEXT("Independent junction reference succeeds"),UEHBWallTopologyLibrary::BuildWallNodeModelJunctionMesh(Model,Pair.Key,Reference,Reason));
   TestTrue(TEXT("Triangles vertices normals UV and footprint match independent reference"),SameMesh(Reference,Pair.Value));
  }
  TMap<FGuid,TArray<FEHBFloorSupportSurface>> Expected,Actual;
  TestTrue(TEXT("Independent tops succeed"),EHBRoomFinishMove::BuildTopologyTops(Model,Expected,Reason));
  TestTrue(TEXT("Draft tops succeed"),EHBRoomFinishMove::BuildTopologyTops(Draft,Actual,Reason));
  TestEqual(TEXT("Only wall and physical-pillar host identities retained"),Actual.Num(),Expected.Num());
  for(const auto& Pair:Expected){const auto* Found=Actual.Find(Pair.Key);TestTrue(TEXT("Every physical top has identical coverage"),Found&&EHBRoomFinishMove::SameTopCoverage(Pair.Value,*Found,Reason));}
  const FVector Old=Model.Nodes[0].LocalTransform.GetLocation();Model.Nodes[0].LocalTransform.AddToTranslation(FVector(-20,-10,0));
  TestTrue(TEXT("Draft owns source, later edits cannot alter it"),Draft.GetModel().Nodes[0].LocalTransform.GetLocation()==Old);
  const auto Fresh=FEHBWallNodeGeometryDraft::Build(Model,Reason);TestTrue(TEXT("Later edit is independently solved without receipt/cache key"),Fresh.IsReady()&&!SameSides(Fresh.GetWallSides(),Draft.GetWallSides()));
 }
 auto Model=MakeGeometryDraftRooms(1,0,0);auto Reject=[&](const FEHBWallNodeModel& Bad)
 {
  const auto Draft=FEHBWallNodeGeometryDraft::Build(Bad,Reason);TestFalse(TEXT("Invalid source never gives a ready draft"),Draft.IsReady());
  TestTrue(TEXT("No partial geometry or solve stats escape a failed draft"),Draft.GetModel().Nodes.IsEmpty()&&Draft.GetWallSides().IsEmpty()&&Draft.GetUnboundJunctions().IsEmpty()&&Draft.GetSolveStats().NodeFootprintsSolved==0);
  TMap<FGuid,TArray<FEHBFloorSupportSurface>> Tops;Tops.Add(FGuid::NewGuid(),{});TestFalse(TEXT("Unprepared draft cannot make finish hosts"),EHBRoomFinishMove::BuildTopologyTops(Draft,Tops,Reason));TestTrue(TEXT("Rejected output resets prior tops"),Tops.IsEmpty());
 };
 auto Bad=Model;const auto Duplicate=Bad.Nodes[0];Bad.Nodes.Add(Duplicate);Reject(Bad);
 Bad=Model;Bad.Walls[0].EndNodeGuid=FGuid::NewGuid();Reject(Bad);
 Bad=Model;for(auto& N:Bad.Nodes)N.LocalTransform.SetLocation(N.LocalTransform.GetLocation()*.03);Reject(Bad);
 FEHBWallNodeModel Empty;Empty.Version=1;TestTrue(TEXT("Versioned empty building has a ready empty solve"),FEHBWallNodeGeometryDraft::Build(Empty,Reason).IsReady());
 Model.Nodes.AddDefaulted();Model.Nodes.Last().NodeGuid=FGuid::NewGuid();Model.Nodes.Last().FloorIndex=1;Model.Nodes.Last().JunctionDimensions=FVector(20,20,300);Model.Nodes.Last().LocalTransform.SetLocation(FVector(2000,0,0));
 auto Isolated=FEHBWallNodeGeometryDraft::Build(Model,Reason);TestTrue(TEXT("Isolated valid node has no fabricated fill"),Isolated.IsReady()&&!Isolated.GetUnboundJunctions().Contains(Model.Nodes.Last().NodeGuid));
 // A failure after some footprints have been computed must clear every optional output.
 TArray<FEHBWallJunctionWallSides> Sides;TMap<FGuid,TArray<FVector>> Footprints;FEHBWallJunctionSolveStats Stats;
 TestTrue(TEXT("Populate footprint outputs"),UEHBWallTopologyLibrary::BuildWallNodeModelSides(Model,Sides,Reason,nullptr,&Stats,&Footprints));
 TestFalse(TEXT("Reject collapsed sides after valid footprints"),UEHBWallTopologyLibrary::BuildWallNodeModelSides(Bad,Sides,Reason,nullptr,&Stats,&Footprints));
 TestTrue(TEXT("Failure clears previous footprints sides and stats"),Footprints.IsEmpty()&&Sides.IsEmpty()&&Stats.NodeFootprintsSolved==0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBGeometryDraftCostTest,"EHB.Topology.GeometryDraftCost",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBGeometryDraftCostTest::RunTest(const FString& Parameters)
{
 TArray<TSharedPtr<FJsonValue>> Rows;FName Reason;
 for(int32 Rooms:{4,16,64})
 {
  const auto Model=MakeGeometryDraftRooms(Rooms,0,17);TArray<double> Before,After;
  for(int32 I=0;I<7;++I)
  {
   auto Old=[&](){const double Start=FPlatformTime::Seconds();const bool Ok=LegacyCandidateGeometry(Model,Reason);if(I)Before.Add((FPlatformTime::Seconds()-Start)*1000);return Ok;};
   auto New=[&](){const double Start=FPlatformTime::Seconds();const auto Draft=FEHBWallNodeGeometryDraft::Build(Model,Reason);TMap<FGuid,TArray<FEHBFloorSupportSurface>> Tops;const bool Ok=Draft.IsReady()&&EHBRoomFinishMove::BuildTopologyTops(Draft,Tops,Reason);if(I)After.Add((FPlatformTime::Seconds()-Start)*1000);return Ok;};
   const bool Ok=I%2?(New()&&Old()):(Old()&&New());if(!TestTrue(*Reason.ToString(),Ok))return false;
  }
  Before.Sort();After.Sort();const double OldMs=(Before[2]+Before[3])*.5,NewMs=(After[2]+After[3])*.5;
  auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("nodes"),Model.Nodes.Num());Row->SetNumberField(TEXT("walls"),Model.Walls.Num());Row->SetNumberField(TEXT("legacyMedianMs"),OldMs);Row->SetNumberField(TEXT("draftMedianMs"),NewMs);Row->SetNumberField(TEXT("ratio"),OldMs/NewMs);Row->SetNumberField(TEXT("samples"),Before.Num());Rows.Add(MakeShared<FJsonValueObject>(Row));
  AddInfo(FString::Printf(TEXT("Geometry only %d nodes: legacy %.3f ms, shared draft %.3f ms (%.2fx)"),Model.Nodes.Num(),OldMs,NewMs,OldMs/NewMs));
 }
 auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("scope"),TEXT("Candidate geometry and finish tops only; not full preview, viewport, GPU, or phase acceptance"));Root->SetArrayField(TEXT("rows"),Rows);Root->SetStringField(TEXT("time"),FDateTime::UtcNow().ToIso8601());FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));
 FString ReportFolder;FParse::Value(FCommandLine::Get(),TEXT("ReportExportPath="),ReportFolder);
 if(ReportFolder.IsEmpty())ReportFolder=FPaths::ProjectSavedDir()/TEXT("EHB-Refactor/GeometryCost")/FGuid::NewGuid().ToString();
 const FString File=ReportFolder/TEXT("GeometryDraftCost.json");IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);TestTrue(TEXT("Write measured geometry cost evidence"),FFileHelper::SaveStringToFile(Json,*File));AddInfo(FString::Printf(TEXT("Geometry cost evidence: %s"),*File));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBGeometryDraftRenderTest,"EHB.Topology.GeometryDraftRender",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBGeometryDraftRenderTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!World)return false;
 FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;auto* Owner=World->SpawnActor<AEHB_Building>(Params);if(!Owner)return false;
 ON_SCOPE_EXIT{Owner->Destroy();};
 auto MakeComponent=[&](){auto* C=NewObject<UEHBWallJunctionComponent>(Owner,NAME_None,RF_Transient);C->SetupAttachment(Owner->GetRootComponent());Owner->AddInstanceComponent(C);C->RegisterComponent();return C;};
 auto* Legacy=MakeComponent();auto* Shared=MakeComponent();FName Reason;
 auto Snapshot=[](UEHBWallJunctionComponent* C)
 {
  const auto* Section=C->GetProcMeshSection(0);if(!Section)return FString();auto Value=*Section;Value.Revision=0;
  FString Text;FJsonObjectConverter::UStructToJsonObjectString(Value,Text);return Text;
 };
 int32 Compared=0;
 for(int32 Bound=0;Bound<2;++Bound)for(float Yaw:{0.f,17.f,45.f,117.f})
 {
  const auto Model=MakeGeometryDraftRooms(3,Bound,Yaw);const auto Draft=FEHBWallNodeGeometryDraft::Build(Model,Reason);if(!TestTrue(*Reason.ToString(),Draft.IsReady()))return false;
  for(const auto& Pair:Draft.GetUnboundJunctions())
  {
   if(!TestTrue(TEXT("Legacy component generation succeeds"),Legacy->RebuildFromNodeModel(Model,Pair.Key))
    ||!TestTrue(TEXT("Shared component generation succeeds"),Shared->RebuildFromGeometryDraft(Draft,Pair.Key)))return false;
   TestEqual(*FString::Printf(TEXT("Exact component geometry and hash B%d yaw%.0f node%s"),Bound,Yaw,*Pair.Key.ToString()),Snapshot(Shared),Snapshot(Legacy));
   TestTrue(TEXT("Component frame preserved exactly"),Shared->GetRelativeTransform().Equals(Legacy->GetRelativeTransform(),0.0));
   TestEqual(TEXT("Logical identity preserved"),Shared->NodeGuid,Legacy->NodeGuid);TestEqual(TEXT("Geometry source revision preserved"),Shared->SourceGeometryRevision,Legacy->SourceGeometryRevision);++Compared;
   const auto Before=Snapshot(Shared);const auto Id=Shared->NodeGuid;const int32 Revision=Shared->GetProcMeshSection(0)->Revision;
   TestFalse(TEXT("Unprepared draft refuses before touching component"),Shared->RebuildFromGeometryDraft(FEHBWallNodeGeometryDraft(),Id));
   TestFalse(TEXT("Unknown node refuses before touching component"),Shared->RebuildFromGeometryDraft(Draft,FGuid::NewGuid()));
   TestTrue(TEXT("Failure preserves mesh, logical identity and section revision"),Snapshot(Shared)==Before&&Shared->NodeGuid==Id&&Shared->GetProcMeshSection(0)->Revision==Revision);
  }
 }
 TestEqual(TEXT("All requested components compared"),Compared,72);
 const auto Bound=MakeGeometryDraftRooms(1,2,0);const auto Draft=FEHBWallNodeGeometryDraft::Build(Bound,Reason);const auto Before=Snapshot(Shared);
 TestFalse(TEXT("Bound column cannot be fabricated as a derived unbound mesh"),Shared->RebuildFromGeometryDraft(Draft,Bound.Nodes[0].NodeGuid));TestEqual(TEXT("Bound rejection preserves existing component"),Snapshot(Shared),Before);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBGeometrySelectionTest,"EHB.Topology.GeometrySelection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBGeometrySelectionTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;if(!World)return false;
 FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;auto* Owner=World->SpawnActor<AEHB_Building>(Params);if(!Owner)return false;ON_SCOPE_EXIT{Owner->Destroy();};
 auto Make=[&](){auto* C=NewObject<UEHBWallJunctionComponent>(Owner,NAME_None,RF_Transient);C->SetupAttachment(Owner->GetRootComponent());Owner->AddInstanceComponent(C);C->RegisterComponent();return C;};
 auto* Reference=Make();auto* Selected=Make();FName Reason;int32 Meshes=0;
 auto Snapshot=[](UEHBWallJunctionComponent* C){FString Text;if(const auto* Section=C->GetProcMeshSection(0)){auto Copy=*Section;Copy.Revision=0;FJsonObjectConverter::UStructToJsonObjectString(Copy,Text);}return Text;};
 auto ExactSide=[](const auto& A,const auto& B){return A.WallGuid==B.WallGuid&&A.LocalTransform.Equals(B.LocalTransform,0.0)&&A.LocalStart==B.LocalStart&&A.LocalEnd==B.LocalEnd&&A.StartLeft==B.StartLeft&&A.EndLeft==B.EndLeft&&A.StartRight==B.StartRight&&A.EndRight==B.EndRight;};
 for(int32 BindingMode:{0,1,2})for(float Yaw:{0.f,17.f,45.f,117.f})
 {
  const auto Model=MakeGeometryDraftRooms(3,BindingMode,Yaw);const auto Full=FEHBWallNodeGeometryDraft::Build(Model,Reason);if(!Full.IsReady())return false;
  for(int32 Pattern:{0,1,2})
  {
   const int32 First=Pattern*4;TSet<FGuid> Walls,Junctions;Walls.Add(Model.Walls[First].WallGuid);
   if(Pattern>=1)Walls.Add(Model.Walls[First+3].WallGuid);
   if(Pattern==2){Walls.Add(Model.Walls[First+1].WallGuid);Walls.Add(Model.Walls[First+2].WallGuid);}
   for(int32 N=First;N<First+(Pattern==2?4:1);++N)if(Pattern>=1&&!Model.PillarBindings.ContainsByPredicate([&](const auto& P){return P.NodeGuid==Model.Nodes[N].NodeGuid;}))Junctions.Add(Model.Nodes[N].NodeGuid);
   const auto Part=FEHBWallNodeGeometrySelection::Build(Model,Walls,Junctions,Reason);if(!TestTrue(*Reason.ToString(),Part.IsReady()))return false;
   TestEqual(TEXT("Selection computes exactly requested wall sides"),Part.GetSolveStats().WallSidesSolved,Walls.Num());TestEqual(TEXT("Selection computes only required endpoint footprints"),Part.GetSolveStats().NodeFootprintsSolved,Pattern==0?2:Pattern==1?3:4);TestEqual(TEXT("Selection computes only requested meshes"),Part.GetUnboundJunctions().Num(),Junctions.Num());
   for(const auto& Side:Part.GetWallSides()){const auto* Expected=Full.GetWallSides().FindByPredicate([&](const auto& V){return V.WallGuid==Side.WallGuid;});TestTrue(TEXT("Every selected side value equals full solve exactly"),Expected&&ExactSide(Side,*Expected));}
   for(FGuid Id:Junctions)
   {
    if(!Reference->RebuildFromGeometryDraft(Full,Id)||!Selected->RebuildFromGeometrySelection(Part,Id))return false;
    TestEqual(TEXT("Selected actual component exact vertices UVs indices normals and hash"),Snapshot(Selected),Snapshot(Reference));TestTrue(TEXT("Selected actual component exact pose"),Selected->GetRelativeTransform().Equals(Reference->GetRelativeTransform(),0.0));++Meshes;
   }
   const auto Before=Snapshot(Selected);const auto* OldSection=Selected->GetProcMeshSection(0);const int32 Revision=OldSection?OldSection->Revision:0;
   TestFalse(TEXT("Unrequested mesh cannot leak out of selection"),Selected->RebuildFromGeometrySelection(Part,Model.Nodes[(First+4)%12].NodeGuid));TestFalse(TEXT("Unprepared selection cannot touch component"),Selected->RebuildFromGeometrySelection({},Model.Nodes[First].NodeGuid));
   const auto* Current=Selected->GetProcMeshSection(0);TestTrue(TEXT("Rejected component application preserves existing output/revision"),Snapshot(Selected)==Before&&(Current?Current->Revision:0)==Revision);
  }
 }
 TestEqual(TEXT("All selected mesh cases checked"),Meshes,32);
 auto Model=MakeGeometryDraftRooms(3,0,17);TSet<FGuid> Walls;Walls.Add(Model.Walls[0].WallGuid);Walls.Add(Model.Walls[3].WallGuid);TSet<FGuid> Junctions;Junctions.Add(Model.Nodes[0].NodeGuid);
 auto Reject=[&](const auto& Source,const TSet<FGuid>& W,const TSet<FGuid>& N){const auto Part=FEHBWallNodeGeometrySelection::Build(Source,W,N,Reason);TestFalse(TEXT("Invalid source or incomplete selection rejected"),Part.IsReady());TestTrue(TEXT("Rejected selection has no partial geometry or work counts"),Part.GetWallSides().IsEmpty()&&Part.GetUnboundJunctions().IsEmpty()&&Part.GetSolveStats().WallSidesSolved==0&&Part.GetSolveStats().NodeFootprintsSolved==0);};
 auto Missing=Walls;Missing.Remove(Model.Walls[3].WallGuid);Reject(Model,Missing,Junctions);TestEqual(TEXT("All incident walls required for selected junction"),Reason,FName(TEXT("IncompleteJunctionWallSelection")));
 auto Unknown=Walls;Unknown.Add(FGuid::NewGuid());Reject(Model,Unknown,Junctions);auto UnknownNode=Junctions;UnknownNode.Add(FGuid::NewGuid());Reject(Model,Walls,UnknownNode);
 auto Bad=Model;Bad.Walls.Last().EndNodeGuid=FGuid::NewGuid();Reject(Bad,Walls,Junctions);
 Bad=Model;Bad.Nodes.Last().JunctionDimensions.X=-1;Reject(Bad,Walls,Junctions);
 Bad=Model;Bad.PillarBindings.Add({Bad.Nodes[0].NodeGuid,FGuid::NewGuid()});Reject(Bad,Walls,Junctions);TestEqual(TEXT("Selection cannot generate physical binding as logical fill"),Reason,FName(TEXT("RequestedJunctionHasPhysicalBinding")));
 Bad=Model;auto Isolated=Bad.Nodes[0];Isolated.NodeGuid=FGuid::NewGuid();Isolated.LocalTransform.SetLocation(FVector(9000,0,0));Bad.Nodes.Add(Isolated);TSet<FGuid> IsolatedOnly;IsolatedOnly.Add(Isolated.NodeGuid);Reject(Bad,{},IsolatedOnly);TestEqual(TEXT("No isolated artificial mesh"),Reason,FName(TEXT("RequestedJunctionHasNoWalls")));
 const auto Empty=FEHBWallNodeGeometrySelection::Build(Model,{},{},Reason);TestTrue(TEXT("Explicit empty selection validates model and does zero geometry work"),Empty.IsReady()&&Empty.GetWallSides().IsEmpty()&&Empty.GetUnboundJunctions().IsEmpty()&&Empty.GetSolveStats().NodeFootprintsSolved==0);
 // A selected result is intentionally not proof of untouched outside geometry.
 // Runtime requires the previous complete-output baseline before selecting it.
 Bad=Model;const auto Origin=Bad.Nodes[8].LocalTransform.GetLocation();for(int32 N=8;N<12;++N)Bad.Nodes[N].LocalTransform.SetLocation(Origin+(Bad.Nodes[N].LocalTransform.GetLocation()-Origin)*.03);
 TestFalse(TEXT("Outside collapsed resolved wall fails complete geometry"),FEHBWallNodeGeometryDraft::Build(Bad,Reason).IsReady());TestTrue(TEXT("Valid selected region has explicitly limited geometry proof"),FEHBWallNodeGeometrySelection::Build(Bad,Walls,Junctions,Reason).IsReady());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBGeometrySelectionCostTest,"EHB.Topology.GeometrySelectionCost",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBGeometrySelectionCostTest::RunTest(const FString& Parameters)
{
 FName Reason;TArray<TSharedPtr<FJsonValue>> Rows;
 for(int32 Rooms:{16,64,256})
 {
  const auto Model=MakeGeometryDraftRooms(Rooms,0,17);TSet<FGuid> Walls,Junctions;Walls.Add(Model.Walls[0].WallGuid);Walls.Add(Model.Walls[3].WallGuid);Junctions.Add(Model.Nodes[0].NodeGuid);TArray<double> Before,After;
  for(int32 I=0;I<7;++I)
  {
   auto All=[&](){const double Start=FPlatformTime::Seconds();const auto G=FEHBWallNodeGeometryDraft::Build(Model,Reason);if(I)Before.Add((FPlatformTime::Seconds()-Start)*1000);return G.IsReady();};
   auto Part=[&](){const double Start=FPlatformTime::Seconds();const auto G=FEHBWallNodeGeometrySelection::Build(Model,Walls,Junctions,Reason);if(I)After.Add((FPlatformTime::Seconds()-Start)*1000);return G.IsReady()&&G.GetSolveStats().WallSidesSolved==2&&G.GetSolveStats().NodeFootprintsSolved==3&&G.GetUnboundJunctions().Num()==1;};
   if(!TestTrue(*Reason.ToString(),I%2?(Part()&&All()):(All()&&Part())))return false;
  }
  Before.Sort();After.Sort();auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("nodes"),Model.Nodes.Num());Row->SetNumberField(TEXT("walls"),Model.Walls.Num());Row->SetNumberField(TEXT("fullMedianMs"),(Before[2]+Before[3])*.5);Row->SetNumberField(TEXT("selectedMedianMs"),(After[2]+After[3])*.5);Row->SetNumberField(TEXT("selectedWalls"),2);Row->SetNumberField(TEXT("selectedSideFootprints"),3);Row->SetNumberField(TEXT("selectedMeshes"),1);Row->SetNumberField(TEXT("samples"),6);Rows.Add(MakeShared<FJsonValueObject>(Row));
 }
 auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("scope"),TEXT("Fresh whole-model value validation plus full/selected geometry only. Not source capture, room/finish plans, component rebuild, collision, viewport, GPU or full phase acceptance."));Root->SetArrayField(TEXT("rows"),Rows);FString Json;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json));FString Folder;FParse::Value(FCommandLine::Get(),TEXT("ReportExportPath="),Folder);if(Folder.IsEmpty())Folder=FPaths::ProjectSavedDir()/TEXT("EHB-Refactor/GeometrySelectionCost")/FGuid::NewGuid().ToString();const FString File=Folder/TEXT("GeometrySelectionCost.json");IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);return TestTrue(TEXT("Write independent selection cost evidence"),FFileHelper::SaveStringToFile(Json,*File));
}
#endif
