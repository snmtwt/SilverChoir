namespace
{
 FGuid AddSavedNodeSupport(AEHB_Wall* W)
 {
  auto* B=W->OwningBuilding.Get();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;auto* Slab=B->GetWorld()->SpawnActor<AEHB_FloorSlab>(Params);
  auto Frame=W->GetElementLocalTransform();Frame.AddToTranslation(FVector(0,0,W->Height+20));Slab->ConfigureDefaultSlab(B,Frame,100,20,false);Slab->SetFloorAssignment(W->FloorIndex,EEHBBuildingFloorElementRole::FloorCeiling);Slab->RoomFillFloorIndex=W->FloorIndex;Slab->RecordOutlineSource(EEHBOutlineSource::RetainedRegion);
  TArray<FEHBFloorSupportSurface> Top;TArray<FEHBFloorFinishRegion> Bottom;FEHBFloorContact Contact;FName Status;
  if(!FEHBFloorContactGeometry::CaptureHorizontalTops(W,Top,Status)||!FEHBFloorContactGeometry::CaptureSlabBottom(Slab,Bottom,Status)||!FEHBFloorContactGeometry::Build(Bottom,Top,Contact,Status)||Contact.Area<=0)return {};
  FEHBElementRelation R;R.Type=EEHBElementRelationType::StructuralSupport;R.bAffectsFloorAssignment=true;R.bGeometryDependent=true;R.Origin=EEHBRelationOrigin::UserAuthored;
  R.Source=FEHBElementRelationEndpoint::MakeElement(W->ElementGuid,EEHBElementSurfaceKind::Top);R.Target=FEHBElementRelationEndpoint::MakeElement(Slab->ElementGuid,EEHBElementSurfaceKind::Bottom);R.ContactArea=Contact.Area;R.ContactPoint=Contact.Point;R.ContactNormal=FVector::UpVector;return B->AddOrUpdateElementRelation(R);
 }
 bool MoveSavedOpeningWall(FAutomationTestBase& Test,AEHB_Wall* W,double Distance)
 {
  auto* B=W->OwningBuilding.Get();FEHBWallNodeModel Model;const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(B,Model);
  if(!Test.TestTrue(*Capture.Status.ToString(),Capture.bSucceeded))return false;
  const auto* Edge=Model.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==W->ElementGuid;});if(!Edge)return false;
  const auto* A=Model.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Edge->StartNodeGuid;});const auto* Z=Model.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Edge->EndNodeGuid;});if(!A||!Z)return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(B,true,false);
  const auto R=UEHBBuildingToolset::CommitWallMoveWithRoomFloors(B,W->ElementGuid,A->LocalTransform.GetLocation(),Z->LocalTransform.GetLocation(),FVector(Distance,0,0),false);
  return Test.TestTrue(*FString::Printf(TEXT("Persisted opening wall move: %s"),*R.Message),R.bSucceeded);
 }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallOpeningWriteTest,"EHBValidation.Persistence.WriteWallOpenings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallOpeningWriteTest::RunTest(const FString& Parameters)
{
 using namespace WallOpeningSources128;
 FString Map,Path;if(!PersistencePaths(Map,Path,TEXT("WallOpenings"))||FPackageName::DoesPackageExist(Map)||FPaths::FileExists(Path))return false;
 auto* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);TArray<TSharedPtr<FJsonValue>> Cases;
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(World,Mode,true))return false;auto* B=Setup.Building();B->SetActorLocation(FVector(Mode*2400,0,0));
  auto* W=Cast<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));if(!W)return false;
  TArray<FEHBLogicalSurfaceDefinition> Surfaces;FName Status;if(!W->QueryLogicalBaseSurfaces(Surfaces,Status)||Surfaces.Num()!=2)return false;
  TestTrue(TEXT("Persistence wall starts without external openings"),W->CutOperations.IsEmpty()&&W->DoorWindowConnections.IsEmpty());
  if(Mode==0)W->CutOperations.Add(MakeOpeningRectangle(-10,60,20,30));
  else
  {
   const auto& Host=Surfaces[Mode-1];const auto Cut=SurfaceOpening129::Make(Host,Mode==1?0:Host.Regions[0].Boundary[1].X*0.5-10,Mode==1?0:60);
   TArray<FEHBCutOperation> Cuts{Cut};if(Mode==2)Cuts.Add(SurfaceOpening129::Make(Host,Host.Regions[0].Boundary[1].X*0.5,60));
   const auto Applied=UEHBBuildingToolset::SetWallOpenings(W,Cuts,B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid),false);
   if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  }
  if(!TestTrue(TEXT("Writer validates binding before authoring rebuild"),W->ValidateSurfaceOpeningBindings(W->CutOperations,Status)))return false;
  if(Mode==0)W->RebuildWallMesh();
  AEHB_Floor* SillFloor=nullptr;
  FGuid PhysicalRelation;
  if(Mode==2)
  {
   FActorSpawnParameters P;P.ObjectFlags=RF_Transactional;SillFloor=World->SpawnActor<AEHB_Floor>(P);SillFloor->AttachToBuilding(B,FTransform::Identity);
   SillFloor->FloorIndex=W->FloorIndex;SillFloor->RoomFloorIndex=W->FloorIndex;SillFloor->OutlineSource=EEHBOutlineSource::RetainedRegion;
   FEHBFloorFinishRegion Region;const auto Frame=W->GetElementLocalTransform();const double Half=FVector::Distance(W->LocalStart,W->LocalEnd)*0.5,T=W->Thickness*0.5;
   for(const FVector V:{FVector(-Half,-T,60),FVector(Half,-T,60),FVector(Half,T,60),FVector(-Half,T,60)})Region.OuterPolygon.Add(Frame.TransformPosition(V));
   if(!SillFloor->SetFloorRegions({Region},false))return false;SillFloor->RecordOutlineSource(EEHBOutlineSource::RetainedRegion);
   const auto Original=W->CutOperations;auto Raised=Original;for(auto& Cut:Raised)Cut.Source.LocalTransform=FTransform(FVector(0,20,0));
   for(const auto& Cuts:{Raised,Original}){const auto R=UEHBBuildingToolset::SetWallOpenings(W,Cuts,B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid),false);if(!TestTrue(*R.Message,R.bSucceeded))return false;}
   if(!TestEqual(TEXT("Writer command creates sill contact"),SillFloor->SurfaceFinishRelationGuids.Num(),1))return false;
   auto* Slab=World->SpawnActor<AEHB_FloorSlab>(P);Slab->ConfigureDefaultSlab(B,FTransform(FVector(0,0,70))*Frame,1000,10,false);
   FEHBElementRelation R;R.Type=EEHBElementRelationType::PhysicalContact;R.bGeometryDependent=true;R.ContactArea=30*W->Thickness;
   R.Source=FEHBElementRelationEndpoint::MakeElement(W->ElementGuid,EEHBElementSurfaceKind::Top);R.Target=FEHBElementRelationEndpoint::MakeElement(Slab->ElementGuid,EEHBElementSurfaceKind::Bottom);
   PhysicalRelation=B->AddOrUpdateElementRelation(R);if(!PhysicalRelation.IsValid())return false;
   const auto Moved=UEHBBuildingToolset::SetWallOpenings(W,Raised,B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid),false);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;
   TestFalse(TEXT("Writer command removes physical contact"),B->ElementRelations.ContainsByPredicate([&](const auto& V){return V.RelationGuid==PhysicalRelation;}));
   if(!GEditor->UndoTransaction())return false;
   TestTrue(TEXT("Writer undo restores physical identity before save"),B->ElementRelations.ContainsByPredicate([&](const auto& V){return V.RelationGuid==PhysicalRelation;}));
  }
  const double RemovedArea=Mode==2?900:600;
  for(int32 Side=0;Side<2;++Side)if(!TestTrue(TEXT("Saved wall subtracts actual generic opening"),FMath::Abs(MeshArea(Side?W->RightWallMeshComponent.Get():W->LeftWallMeshComponent.Get())-Surfaces[Side].GetAreaCm2()+RemovedArea)<0.1))return false;
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());C->SetStringField(TEXT("wall"),W->ElementGuid.ToString());C->SetStringField(TEXT("source"),Source(W));
  C->SetNumberField(TEXT("removedArea"),RemovedArea);
  if(SillFloor){C->SetStringField(TEXT("sillFloor"),SillFloor->ElementGuid.ToString());C->SetStringField(TEXT("sillRelation"),SillFloor->SurfaceFinishRelationGuids[0].ToString());}
  if(PhysicalRelation.IsValid())C->SetStringField(TEXT("physicalRelation"),PhysicalRelation.ToString());
  TArray<TSharedPtr<FJsonValue>> Sources;for(const auto& Cut:W->CutOperations){FString Value;FJsonObjectConverter::UStructToJsonObjectString(Cut,Value);Sources.Add(MakeShared<FJsonValueString>(Value));}C->SetArrayField(TEXT("sources"),Sources);
  C->SetStringField(TEXT("leftSurface"),Surfaces[0].SurfaceGuid.ToString());C->SetStringField(TEXT("rightSurface"),Surfaces[1].SurfaceGuid.ToString());
  C->SetNumberField(TEXT("leftArea"),MeshArea(W->LeftWallMeshComponent));C->SetNumberField(TEXT("rightArea"),MeshArea(W->RightWallMeshComponent));C->SetNumberField(TEXT("capArea"),MeshArea(W->CapMeshComponent));
  if(Mode!=0)C->SetStringField(TEXT("openingCommandReceipt"),B->LastCommittedEdit.StateId.ToString());
  if(Mode==1)
  {
   const auto Before=OpeningRegionState(B),Original=Source(W);const auto Copy=EHBBuildingCopy::Execute(B,FVector(0,2500,0));if(!TestTrue(*FString::Printf(TEXT("Saved wall copy %s/%s"),*Copy.Status.ToString(),*Copy.FailureReason.ToString()),Copy.bSucceeded))return false;
   auto* Target=CastChecked<AEHB_Wall>(Copy.Building->FindElementActorByGuid(Copy.IdentityDraft.ElementGuids.FindChecked(W->ElementGuid)));
   const FGuid NodeSupport=AddSavedNodeSupport(Target);if(!TestTrue(TEXT("Writer adds real node movement support"),NodeSupport.IsValid())||!MoveSavedOpeningWall(*this,Target,25))return false;
   auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("building"),Copy.Building->BuildingGuid.ToString());V->SetStringField(TEXT("wall"),Target->ElementGuid.ToString());V->SetStringField(TEXT("source"),Source(Target));V->SetNumberField(TEXT("leftArea"),MeshArea(Target->LeftWallMeshComponent));V->SetNumberField(TEXT("rightArea"),MeshArea(Target->RightWallMeshComponent));V->SetNumberField(TEXT("capArea"),MeshArea(Target->CapMeshComponent));C->SetObjectField(TEXT("copiedWall"),V);
   V->SetBoolField(TEXT("nodeMoved"),true);
   V->SetStringField(TEXT("nodeSupport"),NodeSupport.ToString());
   TestEqual(TEXT("Writer wall copy leaves source"),Source(W),Original);TestEqual(TEXT("Writer wall copy leaves building"),OpeningRegionState(B),Before);
  }
  Cases.Add(MakeShared<FJsonValueObject>(C));Setup.Fixture.Actors.Reset();
 }
 TArray<TSharedPtr<FJsonValue>> LegacyHosts;
 for(bool Door:{false,true})
 {
  FStyledMergeFixture Setup;if(!Setup.Create(World,Door?0:2,true))return false;auto* B=Setup.Building();B->SetActorLocation(FVector(15000,Door?2000:0,0));auto* W=CastChecked<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));
  auto* D=CreateNodeHostedDoorWindow(W,Door);if(!D||!MoveSavedOpeningWall(*this,W,25))return false;
  const auto* Relation=B->ElementRelations.FindByPredicate([&](const auto& R){return R.Type==EEHBElementRelationType::HostedElement&&R.Target.ElementGuid==D->ElementGuid;});if(!Relation)return false;
  auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("building"),B->BuildingGuid.ToString());V->SetStringField(TEXT("wall"),W->ElementGuid.ToString());V->SetStringField(TEXT("actor"),D->ElementGuid.ToString());V->SetStringField(TEXT("relation"),Relation->RelationGuid.ToString());V->SetStringField(TEXT("operation"),W->CutOperations[0].OperationGuid.ToString());V->SetStringField(TEXT("transform"),D->GetElementLocalTransform().ToString());V->SetNumberField(TEXT("distance"),D->DistanceFromWallStart);
  V->SetNumberField(TEXT("leftArea"),MeshArea(W->LeftWallMeshComponent));V->SetNumberField(TEXT("rightArea"),MeshArea(W->RightWallMeshComponent));V->SetNumberField(TEXT("capArea"),MeshArea(W->CapMeshComponent));LegacyHosts.Add(MakeShared<FJsonValueObject>(V));Setup.Fixture.Actors.Reset();
 }
 TArray<TSharedPtr<FJsonValue>> Supports;
 for(bool Withdrawn:{false,true})
 {
  FActorSpawnParameters P;P.ObjectFlags=RF_Transactional;auto* B=World->SpawnActor<AEHB_Building>(P);B->SetActorLocation(FVector(9000,Withdrawn?2000:0,0));auto* W=World->SpawnActor<AEHB_Wall>(P);auto* Slab=World->SpawnActor<AEHB_FloorSlab>(P);
  W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);W->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
  TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;
  auto Cut=SurfaceOpening129::Make(Hosts[0],200,60);W->CutOperations={Cut};W->RebuildWallMesh();Slab->ConfigureDefaultSlab(B,FVector(0,0,370),1000,10,false);Slab->SetAutomaticFloorAssignment();
  const auto Id=B->SetStructuralSupportRelation(W,Slab,EEHBElementSurfaceKind::Top,EEHBElementSurfaceKind::Bottom,FVector(0,0,360),FVector::UpVector,480,EEHBRelationOrigin::UserAuthored);if(!Id.IsValid())return false;
  auto* Upper=World->SpawnActor<AEHB_Pillar>(P);Upper->AttachToBuilding(B,FTransform::Identity);Upper->ConfigureAsPolygonPillar(100,10,10,FTransform(FVector(0,0,370)));Upper->SetAutomaticFloorAssignment();
  const auto UpperId=B->SetStructuralSupportRelation(Slab,Upper,EEHBElementSurfaceKind::Top,EEHBElementSurfaceKind::Bottom,FVector(0,0,370),FVector::UpVector,100,EEHBRelationOrigin::UserAuthored);if(!UpperId.IsValid())return false;
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);
  auto Shifted=Cut;Shifted.Source.LocalTransform=FTransform(FVector(0,20,0));const auto R=UEHBBuildingToolset::SetWallOpenings(W,{Shifted},B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid),false);if(!TestTrue(*R.Message,R.bSucceeded))return false;
  if(!Withdrawn&&!GEditor->UndoTransaction())return false;
  TestEqual(TEXT("Writer support state before save"),Slab->FloorIndex,Withdrawn?0:1);
  TestEqual(TEXT("Writer upper support state before save"),Upper->FloorIndex,Withdrawn?0:2);
  auto C=MakeShared<FJsonObject>();C->SetStringField(TEXT("building"),B->BuildingGuid.ToString());C->SetStringField(TEXT("wall"),W->ElementGuid.ToString());C->SetStringField(TEXT("slab"),Slab->ElementGuid.ToString());C->SetStringField(TEXT("relation"),Id.ToString());C->SetStringField(TEXT("upper"),Upper->ElementGuid.ToString());C->SetStringField(TEXT("upperRelation"),UpperId.ToString());C->SetBoolField(TEXT("withdrawn"),Withdrawn);Supports.Add(MakeShared<FJsonValueObject>(C));
 }
 if(!UEditorLoadingAndSavingUtils::SaveMap(World,Map))return false;
 auto Data=MakeShared<FJsonObject>();Data->SetStringField(TEXT("schema"),TEXT("EHB.WallOpenings.v1"));Data->SetBoolField(TEXT("structuralBottomCaps"),true);Data->SetArrayField(TEXT("cases"),Cases);Data->SetArrayField(TEXT("structuralSupports"),Supports);Data->SetArrayField(TEXT("legacyHosts"),LegacyHosts);FString Json;FJsonSerializer::Serialize(Data,TJsonWriterFactory<>::Create(&Json));return FFileHelper::SaveStringToFile(Json,*Path);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallOpeningReadTest,"EHBValidation.Persistence.ReadWallOpenings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallOpeningReadTest::RunTest(const FString& Parameters)
{
 using namespace WallOpeningSources128;
 FString Map,Path,Json;if(!PersistencePaths(Map,Path,TEXT("WallOpenings"))||!FFileHelper::LoadFileToString(Json,*Path))return false;
 TSharedPtr<FJsonObject> Data;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Data)||Data->GetStringField(TEXT("schema"))!=TEXT("EHB.WallOpenings.v1"))return false;
 bool HasBottomCaps=false;Data->TryGetBoolField(TEXT("structuralBottomCaps"),HasBottomCaps);
 auto* World=GEditor->GetEditorWorldContext().World();TestEqual(TEXT("Cold opening map"),World->GetOutermost()->GetName(),Map);TestEqual(TEXT("All binding modes saved"),Data->GetArrayField(TEXT("cases")).Num(),3);
 for(const auto& Item:Data->GetArrayField(TEXT("cases")))
 {
  const auto C=Item->AsObject();FGuid BuildingGuid,WallGuid;FGuid::Parse(C->GetStringField(TEXT("building")),BuildingGuid);FGuid::Parse(C->GetStringField(TEXT("wall")),WallGuid);
  AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==BuildingGuid)B=*It;if(!B)return false;
  auto* W=Cast<AEHB_Wall>(B->FindElementActorByGuid(WallGuid));if(!W)return false;
  const TSharedPtr<FJsonObject>* CopyData=nullptr;
  if(C->TryGetObjectField(TEXT("copiedWall"),CopyData))
  {
   FGuid CopyId,Element;FGuid::Parse((*CopyData)->GetStringField(TEXT("building")),CopyId);FGuid::Parse((*CopyData)->GetStringField(TEXT("wall")),Element);AEHBBuildingActorBase* Copy=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==CopyId)Copy=*It;if(!Copy)return false;
   auto* Target=Cast<AEHB_Wall>(Copy->FindElementActorByGuid(Element));if(!Target||Target->CutOperations.Num()!=1)return false;TestEqual(TEXT("Cold copied wall source"),Source(Target),(*CopyData)->GetStringField(TEXT("source")));
   const auto& Host=Target->CutOperations[0].SurfaceHost;TestEqual(TEXT("Cold copied wall building"),Host.BuildingGuid,CopyId);TestEqual(TEXT("Cold copied wall element"),Host.ElementGuid,Element);TestEqual(TEXT("Cold copied wall surface"),Host.SurfaceGuid,Target->FindLogicalSurfaceIdentity(TEXT("Wall.Left")));TestNotEqual(TEXT("Cold copied wall independent surface"),Host.SurfaceGuid,W->CutOperations[0].SurfaceHost.SurfaceGuid);
   Target->RebuildWallMesh();for(const auto Pair:{TPair<const TCHAR*,UEHBGeneratedMeshComponent*>(TEXT("leftArea"),Target->LeftWallMeshComponent),TPair<const TCHAR*,UEHBGeneratedMeshComponent*>(TEXT("rightArea"),Target->RightWallMeshComponent),TPair<const TCHAR*,UEHBGeneratedMeshComponent*>(TEXT("capArea"),Target->CapMeshComponent)})TestTrue(TEXT("Cold copied actual opening geometry"),FMath::Abs(MeshArea(Pair.Value)-(*CopyData)->GetNumberField(Pair.Key))<0.02);
   const auto Original=Source(W),Before=Source(Target);GEditor->SelectNone(false,true,false);GEditor->SelectActor(Target,true,false);const auto R=UEHBBuildingToolset::SetWallOpenings(Target,{},Copy->RelationshipGraphRevision,Copy->GetElementGeometryRevision(Element));if(!TestTrue(*R.Message,R.bSucceeded))return false;TestTrue(TEXT("Cold copied wall deletion"),Target->CutOperations.IsEmpty());TestEqual(TEXT("Cold copied edit leaves original"),Source(W),Original);GEditor->UndoTransaction();TestEqual(TEXT("Cold copied wall edit undo"),Source(Target),Before);GEditor->RedoTransaction();TestTrue(TEXT("Cold copied wall edit redo"),Target->CutOperations.IsEmpty());GEditor->UndoTransaction();
  }
  bool NodeMoved=false;
  if(CopyData&&(*CopyData)->TryGetBoolField(TEXT("nodeMoved"),NodeMoved)&&NodeMoved)
  {
   FGuid CopyId,Element;FGuid::Parse((*CopyData)->GetStringField(TEXT("building")),CopyId);FGuid::Parse((*CopyData)->GetStringField(TEXT("wall")),Element);
   AEHBBuildingActorBase* Copy=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==CopyId)Copy=*It;
   auto* Target=Copy?Cast<AEHB_Wall>(Copy->FindElementActorByGuid(Element)):nullptr;if(!Target)return false;
   FString SupportText;FGuid SupportId;if((*CopyData)->TryGetStringField(TEXT("nodeSupport"),SupportText)){FGuid::Parse(SupportText,SupportId);TestTrue(TEXT("Cold support identity restored"),Copy->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==SupportId;}));}
   const auto BeforeMove=OpeningRegionState(Copy);if(!MoveSavedOpeningWall(*this,Target,5))return false;const auto AfterMove=OpeningRegionState(Copy);
   if(SupportId.IsValid())TestTrue(TEXT("Cold movement retains support identity"),Copy->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==SupportId;}));
   TestTrue(TEXT("Cold continuation moves loaded wall"),AfterMove!=BeforeMove);GEditor->UndoTransaction();TestEqual(TEXT("Cold move undo"),OpeningRegionState(Copy),BeforeMove);GEditor->RedoTransaction();TestEqual(TEXT("Cold move redo"),OpeningRegionState(Copy),AfterMove);GEditor->UndoTransaction();
  }
  AEHB_Floor* SillFloor=nullptr;FGuid SillRelation;FString SillId;
  if(C->TryGetStringField(TEXT("sillFloor"),SillId)){FGuid Id;FGuid::Parse(SillId,Id);SillFloor=Cast<AEHB_Floor>(B->FindElementActorByGuid(Id));FGuid::Parse(C->GetStringField(TEXT("sillRelation")),SillRelation);if(!SillFloor||!SillRelation.IsValid())return false;}
  FGuid PhysicalRelation;FString PhysicalId;if(C->TryGetStringField(TEXT("physicalRelation"),PhysicalId))FGuid::Parse(PhysicalId,PhysicalRelation);
  TestTrue(TEXT("Cold wall has real building attachment"),W->GetAttachParentActor()==B);
  FString SavedReceipt;if(C->TryGetStringField(TEXT("openingCommandReceipt"),SavedReceipt)){TestEqual(TEXT("Committed opening receipt saved and loaded"),B->LastCommittedEdit.StateId.ToString(),SavedReceipt);TestEqual(TEXT("Saved opening command"),B->LastCommittedEdit.Command,FName(TEXT("EditWallOpenings")));}
  double RemovedArea=600;C->TryGetNumberField(TEXT("removedArea"),RemovedArea);
  FString ExpectedSource;int32 ExpectedCount=1;const TArray<TSharedPtr<FJsonValue>>* Sources=nullptr;
  if(C->TryGetArrayField(TEXT("sources"),Sources)){ExpectedCount=Sources->Num();for(const auto& Value:*Sources)ExpectedSource+=CanonicalCutOperationManifest(*this,Value->AsString());}
  else ExpectedSource=CanonicalCutOperationManifest(*this,C->GetStringField(TEXT("source")));
  bool Rebuilt=false;
  auto Check=[&]()
  {
   if(PhysicalRelation.IsValid()){const auto* R=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==PhysicalRelation;});if(!TestTrue(TEXT("Cold physical relation identity"),R!=nullptr))return false;TestTrue(TEXT("Cold physical contact area"),FMath::Abs(R->ContactArea-30*W->Thickness)<0.01);TestTrue(TEXT("Cold supported slab exists"),Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(R->Target.ElementGuid))!=nullptr);}
   if(SillFloor){TestTrue(TEXT("Cold sill cache preserves identity"),SillFloor->SurfaceFinishRelationGuids==TArray<FGuid>{SillRelation});const auto* R=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==SillRelation;});if(!TestTrue(TEXT("Cold sill graph matches cache"),R!=nullptr))return false;TestTrue(TEXT("Cold overlapping opening sill area"),FMath::Abs(R->ContactArea-30*W->Thickness)<0.01);}
   TestEqual(TEXT("Authored opening count saved"),W->CutOperations.Num(),ExpectedCount);
   TestEqual(TEXT("Cold operation, host and point IDs"),Source(W),ExpectedSource);
   TArray<FEHBLogicalSurfaceDefinition> Surfaces;FName Status;if(!W->QueryLogicalBaseSurfaces(Surfaces,Status)||Surfaces.Num()!=2)return false;
   TestEqual(TEXT("Cold left surface ID matches writer"),Surfaces[0].SurfaceGuid.ToString(),C->GetStringField(TEXT("leftSurface")));
   TestEqual(TEXT("Cold right surface ID matches writer"),Surfaces[1].SurfaceGuid.ToString(),C->GetStringField(TEXT("rightSurface")));
   // Older versions of this fixture only authored height-interior cuts and
   // omitted the entire wall bottom cap. Its upgrade area is independently
   // derived from the average two side lengths, not measured from the cap mesh.
   const double SavedCap=C->GetNumberField(TEXT("capArea"));
   const double ExpectedCap=SavedCap+(HasBottomCaps?0:(Surfaces[0].GetAreaCm2()+Surfaces[1].GetAreaCm2())*W->Thickness/(2*W->Height));
   const bool CapMatches=FMath::Abs(MeshArea(W->CapMeshComponent)-ExpectedCap)<0.1||(!HasBottomCaps&&!Rebuilt&&FMath::Abs(MeshArea(W->CapMeshComponent)-SavedCap)<0.1);
   return TestTrue(TEXT("Cold geometry matches saved version or explicit structural-bottom upgrade"),FMath::Abs(MeshArea(W->LeftWallMeshComponent)-C->GetNumberField(TEXT("leftArea")))<0.1&&FMath::Abs(MeshArea(W->RightWallMeshComponent)-C->GetNumberField(TEXT("rightArea")))<0.1&&CapMatches);
  };
  if(!Check())return false;W->RebuildWallMesh();Rebuilt=true;if(!Check())return false;
  if(W->CutOperations[0].SurfaceHost.Version==1)
  {
   FEHBPreparedWallOpening Preview;FName Reason;
   if(!TestTrue(TEXT("Cold bound candidate prepares from authoritative sources"),W->PrepareSurfaceOpening(W->CutOperations,Preview,Reason)))return false;
   TestTrue(TEXT("Cold candidate opening area"),FMath::Abs(Preview.RemovedArea-RemovedArea)<0.1);
   if(!Check())return false;
  }

  if(W->CutOperations[0].SurfaceHost.Version==1)
  {
   GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);
   const auto Removed=UEHBBuildingToolset::SetWallOpenings(W,{},B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid),false);
   if(!TestTrue(*Removed.Message,Removed.bSucceeded))return false;
  }
  else
  {
   FScopedTransaction Transaction(NSLOCTEXT("EHBTests","ColdWallOpening","Remove cold generic wall opening"));W->Modify();
   TArray<UEHBGeneratedMeshComponent*> Meshes;W->GetGeneratedMeshComponents(Meshes);for(auto* Mesh:Meshes)Mesh->Modify();
   W->CutOperations.Reset();W->RebuildWallMesh();
  }
  TestTrue(TEXT("Cold removal restores wall material"),FMath::Abs(MeshArea(W->LeftWallMeshComponent)-C->GetNumberField(TEXT("leftArea"))-RemovedArea)<0.1);
  if(SillFloor)TestTrue(TEXT("Cold opening removal removes sill contact"),SillFloor->SurfaceFinishRelationGuids.IsEmpty());
  if(PhysicalRelation.IsValid())TestFalse(TEXT("Cold removal deletes physical relation"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==PhysicalRelation;}));
  GEditor->UndoTransaction();if(!Check())return false;GEditor->RedoTransaction();
  TestTrue(TEXT("Cold redo removes operation"),W->CutOperations.IsEmpty());
  if(SillFloor)TestTrue(TEXT("Cold redo removes sill contact"),SillFloor->SurfaceFinishRelationGuids.IsEmpty());
  if(PhysicalRelation.IsValid())TestFalse(TEXT("Cold redo deletes physical relation"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==PhysicalRelation;}));
  TestTrue(TEXT("Cold redo restores wall material"),FMath::Abs(MeshArea(W->LeftWallMeshComponent)-C->GetNumberField(TEXT("leftArea"))-RemovedArea)<0.1);
  GEditor->UndoTransaction();if(!Check())return false;
 }
 const TArray<TSharedPtr<FJsonValue>>* LegacyHosts=nullptr;
 if(Data->TryGetArrayField(TEXT("legacyHosts"),LegacyHosts))for(const auto& Item:*LegacyHosts)
 {
  auto C=Item->AsObject();FGuid Bid,Wid,Did,Rid,Op;FGuid::Parse(C->GetStringField(TEXT("building")),Bid);FGuid::Parse(C->GetStringField(TEXT("wall")),Wid);FGuid::Parse(C->GetStringField(TEXT("actor")),Did);FGuid::Parse(C->GetStringField(TEXT("relation")),Rid);FGuid::Parse(C->GetStringField(TEXT("operation")),Op);
  AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==Bid)B=*It;if(!B)return false;
  auto* W=Cast<AEHB_Wall>(B->FindElementActorByGuid(Wid));auto* D=Cast<AEHB_DoorWindow>(B->FindElementActorByGuid(Did));if(!W||!D)return false;FTransform Expected;if(!Expected.InitFromString(C->GetStringField(TEXT("transform"))))return false;
  auto Check=[&](){TestEqual(TEXT("Cold legacy actor still binds same wall"),D->OwningWallGuid,Wid);TestTrue(TEXT("Cold hosted relation identity"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Rid&&R.Source.ElementGuid==Wid&&R.Target.ElementGuid==Did;}));TestTrue(TEXT("Cold legacy operation identity"),W->CutOperations.ContainsByPredicate([&](const auto& R){return R.OperationGuid==Op&&R.Source.SourceElementGuid==Did;}));};Check();
  TestTrue(TEXT("Cold legacy local pose"),D->GetElementLocalTransform().Equals(Expected,0.001));TestTrue(TEXT("Cold legacy distance"),FMath::Abs(D->DistanceFromWallStart-C->GetNumberField(TEXT("distance")))<0.001);
  for(const auto Pair:{TPair<const TCHAR*,UEHBGeneratedMeshComponent*>(TEXT("leftArea"),W->LeftWallMeshComponent),TPair<const TCHAR*,UEHBGeneratedMeshComponent*>(TEXT("rightArea"),W->RightWallMeshComponent),TPair<const TCHAR*,UEHBGeneratedMeshComponent*>(TEXT("capArea"),W->CapMeshComponent)})TestTrue(TEXT("Cold legacy actual opening area"),FMath::Abs(MeshArea(Pair.Value)-C->GetNumberField(Pair.Key))<0.1);
  auto Snapshot=[&](){return OpeningRegionState(B)+D->GetActorTransform().ToString();};const auto Before=Snapshot();if(!MoveSavedOpeningWall(*this,W,5))return false;Check();const auto After=Snapshot();TestTrue(TEXT("Cold legacy actor continues moving"),After!=Before);GEditor->UndoTransaction();TestEqual(TEXT("Cold legacy move undo"),Snapshot(),Before);Check();GEditor->RedoTransaction();TestEqual(TEXT("Cold legacy move redo"),Snapshot(),After);Check();
 }
 const TArray<TSharedPtr<FJsonValue>>* Supports=nullptr;
 if(Data->TryGetArrayField(TEXT("structuralSupports"),Supports))for(const auto& V:*Supports)
 {
  auto C=V->AsObject();FGuid Bid,Wid,Sid,Rid;FGuid::Parse(C->GetStringField(TEXT("building")),Bid);FGuid::Parse(C->GetStringField(TEXT("wall")),Wid);FGuid::Parse(C->GetStringField(TEXT("slab")),Sid);FGuid::Parse(C->GetStringField(TEXT("relation")),Rid);const bool Withdrawn=C->GetBoolField(TEXT("withdrawn"));
  AEHBBuildingActorBase* B=nullptr;for(TActorIterator<AEHBBuildingActorBase> It(World);It;++It)if(It->BuildingGuid==Bid)B=*It;if(!B)return false;
  auto* W=Cast<AEHB_Wall>(B->FindElementActorByGuid(Wid));auto* Slab=Cast<AEHB_FloorSlab>(B->FindElementActorByGuid(Sid));if(!W||!Slab)return false;
  AEHB_Pillar* Upper=nullptr;FGuid UpperId;FString UpperText;if(C->TryGetStringField(TEXT("upper"),UpperText)){FGuid Id;FGuid::Parse(UpperText,Id);Upper=Cast<AEHB_Pillar>(B->FindElementActorByGuid(Id));FGuid::Parse(C->GetStringField(TEXT("upperRelation")),UpperId);if(!Upper||!UpperId.IsValid())return false;}
  auto Check=[&](bool Missing){TestEqual(TEXT("Cold structural derived floor"),Slab->FloorIndex,Missing?0:1);TestEqual(TEXT("Cold structural assignment source"),Slab->FloorAssignmentSource,Missing?EEHBFloorAssignmentSource::Unassigned:EEHBFloorAssignmentSource::DerivedFromSupport);TestEqual(TEXT("Cold structural relation identity"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Rid;}),!Missing);FEHBElementQuery Q;Q.FloorIndex=Missing?0:1;TestTrue(TEXT("Cold structural floor index"),B->QueryElements(Q).Contains(Slab));if(Upper){TestEqual(TEXT("Cold upper pillar propagated floor"),Upper->FloorIndex,Missing?0:2);TestEqual(TEXT("Cold upper assignment source"),Upper->FloorAssignmentSource,Missing?EEHBFloorAssignmentSource::Unassigned:EEHBFloorAssignmentSource::DerivedFromSupport);Q.FloorIndex=Missing?0:2;TestTrue(TEXT("Cold upper query index"),B->QueryElements(Q).Contains(Upper));TestTrue(TEXT("Cold upper relation identity retained"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==UpperId;}));}};Check(Withdrawn);
  if(!Withdrawn){GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);const auto R=UEHBBuildingToolset::SetWallOpenings(W,{},B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid),false);if(!TestTrue(*R.Message,R.bSucceeded))return false;Check(true);TestTrue(TEXT("Cold structural undo"),GEditor->UndoTransaction());Check(false);TestTrue(TEXT("Cold structural redo"),GEditor->RedoTransaction());Check(true);GEditor->UndoTransaction();}
 }
 return true;
}
