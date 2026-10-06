#include "EHBWallOpeningCommand.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningHeightContactTest,"EHB.Surfaces.OpeningHeightContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningHeightContactTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* W=World->SpawnActor<AEHB_Wall>(Params);auto* Base=World->SpawnActor<AEHB_FloorSlab>(Params);auto* Upper=World->SpawnActor<AEHB_FloorSlab>(Params);
 ON_SCOPE_EXIT{FEHBWallOpeningCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);Upper->Destroy();Base->Destroy();W->Destroy();B->Destroy();};
 Base->ConfigureDefaultSlab(B,FVector(0,0,300),1000,10,false);Base->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorCeiling);W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);W->SetFloorAssignment(2,EEHBBuildingFloorElementRole::FloorBody);Upper->ConfigureDefaultSlab(B,FVector(0,0,570),1000,10,false);
 const auto BottomId=B->SetStructuralSupportRelation(Base,W,EEHBElementSurfaceKind::Top,EEHBElementSurfaceKind::Bottom,FVector(0,0,300),FVector::UpVector,12000,EEHBRelationOrigin::UserAuthored);if(!BottomId.IsValid())return false;
 FEHBElementRelation R;R.Type=EEHBElementRelationType::PhysicalContact;R.bGeometryDependent=true;R.ContactArea=12000;R.Source=FEHBElementRelationEndpoint::MakeElement(W->ElementGuid,EEHBElementSurfaceKind::Top);R.Target=FEHBElementRelationEndpoint::MakeElement(Upper->ElementGuid,EEHBElementSurfaceKind::Bottom);const auto TopId=B->AddOrUpdateElementRelation(R);if(!TopId.IsValid())return false;
 TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);auto Edit=[&](const TArray<FEHBCutOperation>& Cuts){return UEHBBuildingToolset::SetWallOpenings(W,Cuts,B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid));};
 auto Area=[&](FGuid Id){const auto* Edge=B->ElementRelations.FindByPredicate([&](const auto& V){return V.RelationGuid==Id;});return Edge?double(Edge->ContactArea):-1.0;};
 for(bool Top:{false,true})
 {
  const auto Cut=SurfaceOpening129::Make(Hosts[0],200,Top?230:0);
  FEHBWallOpeningCommand::FailurePhase=1;const auto Failed=Edit({Cut});TestFalse(TEXT("Height contact injected failure"),Failed.bSucceeded);if(!TestTrue(*Failed.Message,Failed.Message.StartsWith(TEXT("OpeningEditFailedRolledBack"))))return false;
  TestTrue(TEXT("Rollback keeps both contact domains"),FMath::Abs(Area(BottomId)-12000)<0.01&&FMath::Abs(Area(TopId)-12000)<0.01);
  const auto Applied=Edit({Cut});if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  TestTrue(TEXT("Only touched height contact loses480cm2"),FMath::Abs(Area(Top?TopId:BottomId)-11520)<0.01&&FMath::Abs(Area(Top?BottomId:TopId)-12000)<0.01);TestEqual(TEXT("Authored wall floor preserved"),W->FloorIndex,2);
  TestTrue(TEXT("Height contact undo"),GEditor->UndoTransaction());TestTrue(TEXT("Undo restores bottom area"),FMath::Abs(Area(BottomId)-12000)<0.01);TestTrue(TEXT("Undo restores crown area"),FMath::Abs(Area(TopId)-12000)<0.01);TestTrue(TEXT("Height contact redo"),GEditor->RedoTransaction());TestTrue(TEXT("Redo contact clipping"),FMath::Abs(Area(Top?TopId:BottomId)-11520)<0.01);GEditor->UndoTransaction();
 }
 W->SetAutomaticFloorAssignment();B->ResolveAutomaticFloorAssignments();TestEqual(TEXT("Supported automatic wall begins floor two"),W->FloorIndex,2);
 auto Whole=SurfaceOpening129::Make(Hosts[0],0,0);for(auto& P:Whole.Source.ExplicitPolygon.Points)P.LocalPosition.X*=25;
 const auto Refused=Edit({Whole});TestFalse(TEXT("Automatic wall floor withdrawal needs topology plan"),Refused.bSucceeded);TestEqual(TEXT("Automatic wall migration refuses explicitly"),Refused.Message,FString(TEXT("OpeningFloorTopologyRequiresPlan")));TestTrue(TEXT("Refusal preserves support and wall source"),FMath::Abs(Area(BottomId)-12000)<0.01&&W->CutOperations.IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningPillarSupportTest,"EHB.Surfaces.OpeningPillarSupport",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningPillarSupportTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* W=World->SpawnActor<AEHB_Wall>(Params);auto* P=World->SpawnActor<AEHB_Pillar>(Params);
 ON_SCOPE_EXIT{FEHBWallOpeningCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);P->Destroy();W->Destroy();B->Destroy();};
 W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);W->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
 TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;
 auto Cut=SurfaceOpening129::Make(Hosts[0],200,60);W->CutOperations={Cut};W->RebuildWallMesh();P->AttachToBuilding(B,FTransform::Identity);FVector Center=Hosts[0].PlaneToBuilding.TransformPosition(FVector(210,60,0));Center.Y=0;P->ConfigureAsPolygonPillar(100,20,24,FTransform(Center));P->SetAutomaticFloorAssignment();
 const auto Id=B->SetStructuralSupportRelation(W,P,EEHBElementSurfaceKind::Top,EEHBElementSurfaceKind::Bottom,FVector(-40,0,360),FVector::UpVector,480,EEHBRelationOrigin::UserAuthored);if(!Id.IsValid())return false;TestEqual(TEXT("Wall supported pillar floor"),P->FloorIndex,2);
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);auto Shifted=Cut;Shifted.Source.LocalTransform=FTransform(FVector(0,20,0));auto Edit=[&](){return UEHBBuildingToolset::SetWallOpenings(W,{Shifted},B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid));};
 FEHBWallOpeningCommand::FailurePhase=1;const auto Failed=Edit();TestFalse(TEXT("Pillar support rollback injected"),Failed.bSucceeded);if(!TestTrue(*Failed.Message,Failed.Message.StartsWith(TEXT("OpeningEditFailedRolledBack"))))return false;TestEqual(TEXT("Pillar support rollback floor"),P->FloorIndex,2);
 const auto Applied=Edit();if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;TestEqual(TEXT("Lost sill withdraws pillar floor"),P->FloorIndex,0);TestFalse(TEXT("Lost sill removes pillar support"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Id;}));
 TestTrue(TEXT("Pillar opening undo"),GEditor->UndoTransaction());TestEqual(TEXT("Pillar undo floor"),P->FloorIndex,2);TestTrue(TEXT("Pillar undo same relation"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Id;}));TestTrue(TEXT("Pillar opening redo"),GEditor->RedoTransaction());TestEqual(TEXT("Pillar redo floor"),P->FloorIndex,0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningStructuralFloorTest,"EHB.Surfaces.OpeningStructuralFloor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningStructuralFloorTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* W=World->SpawnActor<AEHB_Wall>(Params);auto* Slab=World->SpawnActor<AEHB_FloorSlab>(Params);auto* Upper=World->SpawnActor<AEHB_Pillar>(Params);
 ON_SCOPE_EXIT{FEHBWallOpeningCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);Upper->Destroy();Slab->Destroy();W->Destroy();B->Destroy();};
 W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);W->SetFloorAssignment(1,EEHBBuildingFloorElementRole::FloorBody);
 TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;
 auto Cut=SurfaceOpening129::Make(Hosts[0],200,60);W->CutOperations={Cut};W->RebuildWallMesh();
 Slab->ConfigureDefaultSlab(B,FVector(0,0,370),1000,10,false);Slab->SetAutomaticFloorAssignment();Upper->AttachToBuilding(B,FTransform(FVector(0,0,370)));Upper->SetAutomaticFloorAssignment();
 const auto First=B->SetStructuralSupportRelation(W,Slab,EEHBElementSurfaceKind::Top,EEHBElementSurfaceKind::Bottom,FVector(0,0,360),FVector::UpVector,480,EEHBRelationOrigin::UserAuthored);
 const auto Second=B->SetStructuralSupportRelation(Slab,Upper,EEHBElementSurfaceKind::Top,EEHBElementSurfaceKind::Bottom,FVector(0,0,370),FVector::UpVector,100,EEHBRelationOrigin::UserAuthored);
 if(!First.IsValid()||!Second.IsValid())return false;
 auto Shifted=Cut;Shifted.Source.LocalTransform=FTransform(FVector(0,20,0));GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);
 auto Edit=[&](bool Preview=false){return UEHBBuildingToolset::SetWallOpenings(W,{Shifted},B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid),Preview);};
 auto Present=[&](){TestEqual(TEXT("Slab floor restored"),Slab->FloorIndex,1);TestEqual(TEXT("Upper floor restored"),Upper->FloorIndex,2);TestTrue(TEXT("Support identity restored"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==First;}));};
 Present();const auto Graph=B->RelationshipGraphRevision,Geometry=B->GetElementGeometryRevision(W->ElementGuid);const auto Receipt=B->LastCommittedEdit.StateId;
 const auto Preview=Edit(true);if(!TestTrue(*Preview.Message,Preview.bSucceeded))return false;Present();TestEqual(TEXT("Preview keeps graph"),B->RelationshipGraphRevision,Graph);
 for(int32 Phase:{1,2}){FEHBWallOpeningCommand::FailurePhase=Phase;const auto Failed=Edit();TestFalse(TEXT("Injected structural edit fails"),Failed.bSucceeded);if(!TestTrue(*Failed.Message,Failed.Message.StartsWith(TEXT("OpeningEditFailedRolledBack"))))return false;Present();TestEqual(TEXT("Graph rollback"),B->RelationshipGraphRevision,Graph);TestEqual(TEXT("Geometry rollback"),B->GetElementGeometryRevision(W->ElementGuid),Geometry);TestEqual(TEXT("Receipt rollback"),B->LastCommittedEdit.StateId,Receipt);}
 Slab->bHasRoomFillAnchor=true;const auto Refused=Edit();TestFalse(TEXT("Room-bound migration needs plan"),Refused.bSucceeded);TestEqual(TEXT("Room migration refusal"),Refused.Message,FString(TEXT("OpeningFloorTopologyRequiresPlan")));Present();Slab->bHasRoomFillAnchor=false;
 const auto Applied=Edit();if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
 auto Missing=[&](){for(auto* E:TArray<AEHBElementActorBase*>{Slab,Upper}){TestEqual(TEXT("Lost support withdraws derived floor"),E->FloorAssignmentSource,EEHBFloorAssignmentSource::Unassigned);TestEqual(TEXT("Lost support clears index"),E->FloorIndex,0);TestTrue(TEXT("Lost support clears candidates"),E->ConflictingFloorCandidates.IsEmpty());}TestEqual(TEXT("Explicit wall remains floor one"),W->FloorIndex,1);TestFalse(TEXT("Lost contact removes support edge"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==First;}));};Missing();
 TestTrue(TEXT("Receipt includes affected slab"),Applied.CommittedEdit.Elements.ContainsByPredicate([&](const auto& E){return E.ElementGuid==Slab->ElementGuid;}));TestTrue(TEXT("Receipt includes upper pillar"),Applied.CommittedEdit.Elements.ContainsByPredicate([&](const auto& E){return E.ElementGuid==Upper->ElementGuid;}));
 TestTrue(TEXT("Structural opening undo"),GEditor->UndoTransaction());Present();TestTrue(TEXT("Structural opening redo"),GEditor->RedoTransaction());Missing();
 FEHBElementQuery Q;Q.FloorIndex=2;TestFalse(TEXT("Old floor index no longer lists upper pillar"),B->QueryElements(Q).Contains(Upper));Q.FloorIndex=0;TestTrue(TEXT("Unassigned index lists upper pillar"),B->QueryElements(Q).Contains(Upper));
 GEditor->UndoTransaction();Present();Upper->NotifyElementGeometryChanged(true);const auto Stale=Edit();TestFalse(TEXT("Stale outgoing support cannot leave old upper floor"),Stale.bSucceeded);TestEqual(TEXT("Stale dependency refusal"),Stale.Message,FString(TEXT("StaleOpeningFloorDependency")));Present();
 auto SavedSecond=*B->ElementRelations.FindByPredicate([&](const auto& R){return R.RelationGuid==Second;});B->AddOrUpdateElementRelation(SavedSecond);
 auto* Alternate=World->SpawnActor<AEHB_Pillar>(Params);ON_SCOPE_EXIT{Alternate->Destroy();};Alternate->AttachToBuilding(B,FTransform::Identity);Alternate->SetFloorAssignment(3,EEHBBuildingFloorElementRole::FloorBody);
 const auto Other=B->SetStructuralSupportRelation(Alternate,Slab,EEHBElementSurfaceKind::Top,EEHBElementSurfaceKind::Bottom,FVector(0,0,360),FVector::UpVector,100,EEHBRelationOrigin::UserAuthored);if(!Other.IsValid())return false;
 TestTrue(TEXT("Alternate support starts with conflict"),Slab->bFloorAssignmentConflict);const auto Reassigned=Edit();if(!TestTrue(*Reassigned.Message,Reassigned.bSucceeded))return false;
 TestEqual(TEXT("Alternate support selects floor three"),Slab->FloorIndex,3);TestEqual(TEXT("Alternate support propagates floor four"),Upper->FloorIndex,4);TestFalse(TEXT("Resolved alternate clears conflict"),Slab->bFloorAssignmentConflict);
 TestTrue(TEXT("Alternate floor undo"),GEditor->UndoTransaction());Present();TestTrue(TEXT("Alternate floor redo"),GEditor->RedoTransaction());TestEqual(TEXT("Redo alternate upper floor"),Upper->FloorIndex,4);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBOpeningFloorContactTest,"EHB.Surfaces.OpeningFloorContact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBOpeningFloorContactTest::RunTest(const FString& Parameters)
{
 auto* World=GEditor->GetEditorWorldContext().World();FActorSpawnParameters Params;Params.ObjectFlags=RF_Transactional;
 auto* B=World->SpawnActor<AEHB_Building>(Params);auto* W=World->SpawnActor<AEHB_Wall>(Params);auto* F=World->SpawnActor<AEHB_Floor>(Params);
 ON_SCOPE_EXIT{FEHBWallOpeningCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);F->Destroy();W->Destroy();B->Destroy();};
 W->ConfigureAsSimpleWall(B,nullptr,nullptr,FVector(-250,0,300),FVector(250,0,300),260,24);
 auto* Unrelated=World->SpawnActor<AEHB_Floor>(Params);ON_SCOPE_EXIT{Unrelated->Destroy();};
 Unrelated->AttachToBuilding(B,FTransform(FVector(10000,0,360)));Unrelated->LocalFloorPolygon={{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0}};
 // A distant manual floor has no room/contact plan and must not block this edit.
 TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;
 auto Cut=SurfaceOpening129::Make(Hosts[0],200,60);W->CutOperations={Cut};W->RebuildWallMesh();
 F->AttachToBuilding(B,FTransform::Identity);F->FloorIndex=0;F->RoomFloorIndex=0;F->OutlineSource=EEHBOutlineSource::RetainedRegion;
 FEHBFloorFinishRegion Region;Region.OuterPolygon={{-250,-12,360},{250,-12,360},{250,12,360},{-250,12,360}};
 if(!F->SetFloorRegions({Region},false))return false;F->RecordOutlineSource(EEHBOutlineSource::RetainedRegion);
 TArray<FEHBElementRelation> Initial;if(!F->BuildCurrentSurfaceFinishRelationPlan(F->FloorRegions,Initial,Status))return false;
 if(!TestEqual(TEXT("Sill initially supports floor finish"),Initial.Num(),1))return false;
 for(const auto& R:Initial)F->SurfaceFinishRelationGuids.Add(B->AddOrUpdateElementRelation(R,true));const FGuid Original=F->SurfaceFinishRelationGuids[0];
 TestTrue(TEXT("Initial sill area"),FMath::Abs(Initial[0].ContactArea-480)<0.01);
 auto Shifted=Cut;Shifted.Source.LocalTransform=FTransform(FVector(0,20,0));
 GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);
 auto Edit=[&](const FEHBCutOperation& Source){return UEHBBuildingToolset::SetWallOpenings(W,{Source},B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid));};
 FEHBWallOpeningCommand::FailurePhase=1;const auto Failed=Edit(Shifted);TestFalse(TEXT("Failure after floor publication refuses edit"),Failed.bSucceeded);
 TestTrue(TEXT("Failure restores floor cache identity"),F->SurfaceFinishRelationGuids==TArray<FGuid>{Original});
 TestTrue(TEXT("Failure restores graph contact"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Original;}));
 const auto Moved=Edit(Shifted);if(!TestTrue(*Moved.Message,Moved.bSucceeded))return false;
 TestTrue(TEXT("Moving sill removes old floor contact cache"),F->SurfaceFinishRelationGuids.IsEmpty());
 TestFalse(TEXT("Moving sill removes old graph edge"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==Original;}));
 TestTrue(TEXT("Undo succeeds"),GEditor->UndoTransaction());TestTrue(TEXT("Undo restores relation identity"),F->SurfaceFinishRelationGuids==TArray<FGuid>{Original});
 TestTrue(TEXT("Redo succeeds"),GEditor->RedoTransaction());TestTrue(TEXT("Redo removes contact again"),F->SurfaceFinishRelationGuids.IsEmpty());
 const auto Returned=Edit(Cut);if(!TestTrue(*Returned.Message,Returned.bSucceeded))return false;
 TestEqual(TEXT("Previously unlinked floor gains contact"),F->SurfaceFinishRelationGuids.Num(),1);
 auto* Slab=World->SpawnActor<AEHB_FloorSlab>(Params);ON_SCOPE_EXIT{Slab->Destroy();};Slab->ConfigureDefaultSlab(B,FVector(0,0,370),1000,10,false);
 FEHBElementRelation Physical;Physical.Type=EEHBElementRelationType::PhysicalContact;Physical.bGeometryDependent=true;
 Physical.Source=FEHBElementRelationEndpoint::MakeElement(W->ElementGuid,EEHBElementSurfaceKind::Top);Physical.Target=FEHBElementRelationEndpoint::MakeElement(Slab->ElementGuid,EEHBElementSurfaceKind::Bottom);Physical.ContactArea=480;
 const auto PhysicalId=B->AddOrUpdateElementRelation(Physical);if(!TestTrue(TEXT("Sill physical contact fixture"),PhysicalId.IsValid()))return false;
 Slab->NotifyElementGeometryChanged(true);TestTrue(TEXT("Physical source requires fresh domain solve"),B->IsElementRelationStale(PhysicalId));
 const auto PhysicalMoved=Edit(Shifted);if(!TestTrue(*PhysicalMoved.Message,PhysicalMoved.bSucceeded))return false;
 TestFalse(TEXT("Missing slab bottom contact removes physical edge"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==PhysicalId;}));
 GEditor->UndoTransaction();TestTrue(TEXT("Physical contact undo preserves identity"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==PhysicalId;}));
 GEditor->RedoTransaction();TestFalse(TEXT("Physical contact redo removes edge"),B->ElementRelations.ContainsByPredicate([&](const auto& R){return R.RelationGuid==PhysicalId;}));GEditor->UndoTransaction();
 Unrelated->SetElementLocalTransform(FTransform(FVector(0,0,360)));
 const auto Unsupported=Edit(Shifted);TestFalse(TEXT("Potentially affected unclassified floor still requires plan"),Unsupported.bSucceeded);
 TestEqual(TEXT("Affected unsupported floor has explicit refusal"),Unsupported.Message,FString(TEXT("OpeningFloorContactRequiresPlan")));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEHBWallOpeningCommandTest,"EHB.Surfaces.WallOpeningCommand",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEHBWallOpeningCommandTest::RunTest(const FString& Parameters)
{
 using namespace WallOpeningSources128;
 ON_SCOPE_EXIT{EHBWallOpeningPreparation::FailurePhase=0;FEHBWallOpeningCommand::FailurePhase=0;GEditor->SelectNone(false,true,false);};
 for(int32 Mode=0;Mode<3;++Mode)
 {
  FStyledMergeFixture Setup;if(!Setup.Create(GEditor->GetEditorWorldContext().World(),Mode,true))return false;
  auto* B=Setup.Building();auto* W=Cast<AEHB_Wall>(B->FindElementActorByGuid(Setup.Shared));if(!W)return false;
  TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;if(!W->QueryLogicalBaseSurfaces(Hosts,Status))return false;
  auto Cut=SurfaceOpening129::Make(Hosts[0],Hosts[0].Regions[0].Boundary[1].X*0.5-10,60);
  auto Edit=[&](const TArray<FEHBCutOperation>& Sources,bool Preview=false){return UEHBBuildingToolset::SetWallOpenings(W,Sources,B->RelationshipGraphRevision,B->GetElementGeometryRevision(W->ElementGuid),Preview);};
  const double BaseArea=MeshArea(W->LeftWallMeshComponent);
  auto* Mat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));if(!Mat)return false;
  W->LeftWallMeshComponent->SetMaterial(0,Mat);W->LeftWallMeshComponent->SetMaterial(2,Mat);W->LeftWallMeshComponent->SetMeshSectionVisible(0,false);
  const auto Materials=W->LeftWallMeshComponent->OverrideMaterials;
  auto Attributes=[&](){TestTrue(TEXT("Actual material slots preserved"),W->LeftWallMeshComponent->OverrideMaterials==Materials);TestFalse(TEXT("Section visibility preserved"),W->LeftWallMeshComponent->GetProcMeshSection(0)->bSectionVisible);};
  auto Sections=[&](){FString Value;for(auto* M:{W->LeftWallMeshComponent.Get(),W->RightWallMeshComponent.Get(),W->CapMeshComponent.Get()}){FString Part;FJsonObjectConverter::UStructToJsonObjectString(*M->GetProcMeshSection(0),Part);Value+=Part;}return Value;};
  auto Caches=[&](){FString Result;for(const FName Name:{FName(TEXT("CachedLeftWallVertices")),FName(TEXT("CachedRightWallVertices")),FName(TEXT("CachedLeftWallTriangles")),FName(TEXT("CachedRightWallTriangles"))}){const auto* Property=FindFProperty<FProperty>(W->GetClass(),Name);if(Property)Property->ExportText_InContainer(0,Result,W,W,W,PPF_None);}return Result;};
  const bool PriorDirty=B->GetPackage()->IsDirty();B->GetPackage()->SetDirtyFlag(false);ON_SCOPE_EXIT{B->GetPackage()->SetDirtyFlag(PriorDirty);};
  const auto OriginalCaches=Caches();const auto OriginalSections=Sections();const auto OriginalSource=Source(W);const int32 Graph=B->RelationshipGraphRevision,Geometry=B->GetElementGeometryRevision(W->ElementGuid);const auto Receipt=B->LastCommittedEdit.StateId;const bool Dirty=B->GetPackage()->IsDirty();
  int32 Commits=0,Changes=0;bool CallbackComplete=false;
  auto* Observer=NewObject<UEHBChangeNotificationTestObserver>();TStrongObjectPtr<UEHBChangeNotificationTestObserver> Keep(Observer);
  Observer->ObserveCommit=[&](const auto&){++Commits;TArray<FEHBLogicalSurfaceDefinition> Defined;FName Why;CallbackComplete=!B->HasUnpublishedEdit()&&W->QueryLogicalBaseSurfaces(Defined,Why)&&FMath::Abs(MeshArea(W->LeftWallMeshComponent)-(W->CutOperations.IsEmpty()?BaseArea:BaseArea-600))<0.1;};Observer->Observe=[&](FGuid,FName,bool){++Changes;};
  B->OnEditCommitted.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Committed);B->OnElementGeometryChanged.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Geometry);B->OnElementRelationAdded.AddDynamic(Observer,&UEHBChangeNotificationTestObserver::Added);
  ON_SCOPE_EXIT{B->OnEditCommitted.RemoveAll(Observer);B->OnElementGeometryChanged.RemoveAll(Observer);B->OnElementRelationAdded.RemoveAll(Observer);};
  const auto Preview=Edit({Cut},true);if(!TestTrue(*Preview.Message,Preview.bSucceeded))return false;
  TestEqual(TEXT("Command preview has no geometry writes"),Sections(),OriginalSections);TestEqual(TEXT("Command preview emits no receipt"),Commits,0);
  for(int32 Phase=3;Phase<=8;++Phase)
  {
   EHBWallOpeningPreparation::FailurePhase=Phase<=6?Phase:0;FEHBWallOpeningCommand::FailurePhase=Phase>6?Phase-6:0;
   const auto Failed=Edit({Cut});TestFalse(TEXT("Partial publication failure rolls back"),Failed.bSucceeded);
   if(!TestTrue(*Failed.Message,Failed.Message.StartsWith(TEXT("OpeningEditFailedRolledBack"))))return false;
   TestTrue(TEXT("Exact component sections rolled back"),Sections()==OriginalSections);TestEqual(TEXT("Source rolled back"),Source(W),OriginalSource);TestTrue(TEXT("Straight caches restored"),Caches()==OriginalCaches);
   TestEqual(TEXT("Graph revision rolled back"),B->RelationshipGraphRevision,Graph);TestEqual(TEXT("Geometry revision rolled back"),B->GetElementGeometryRevision(W->ElementGuid),Geometry);
   TestEqual(TEXT("Receipt rolled back"),B->LastCommittedEdit.StateId,Receipt);TestEqual(TEXT("Package dirtiness rolled back"),B->GetPackage()->IsDirty(),Dirty);TestEqual(TEXT("No failed commit event"),Commits,0);TestEqual(TEXT("No partial change event"),Changes,0);Attributes();
  }
  GEditor->SelectNone(false,true,false);GEditor->SelectActor(W,true,false);
  const auto Applied=Edit({Cut});if(!TestTrue(*Applied.Message,Applied.bSucceeded))return false;
  TestEqual(TEXT("One committed receipt"),Commits,1);TestTrue(TEXT("Callback sees complete source and meshes"),CallbackComplete);TestTrue(TEXT("Actual cut applied"),FMath::Abs(MeshArea(W->LeftWallMeshComponent)-BaseArea+600)<0.1);Attributes();
  TestEqual(TEXT("Command receipt name"),Applied.CommittedEdit.Command,FName(TEXT("EditWallOpenings")));TestTrue(TEXT("Receipt includes target despite unchanged bounds"),Applied.CommittedEdit.Elements.ContainsByPredicate([&](const auto& E){return E.ElementGuid==W->ElementGuid;}));TestEqual(TEXT("Both room identities in receipt"),Applied.CommittedEdit.RoomGuids.Num(),2);
  for(const auto& R:B->ElementRelations)if(R.InvolvesElement(W->ElementGuid))TestFalse(TEXT("Preserved dependency versions refreshed"),B->IsElementRelationStale(R.RelationGuid));
  const auto AppliedCaches=Caches();const auto AppliedSections=Sections();const auto AppliedSource=Source(W);
  TestTrue(TEXT("No-op succeeds"),Edit({Cut}).bSucceeded);TestEqual(TEXT("No duplicate receipt for no-op"),Commits,1);
  GEditor->UndoTransaction();TestTrue(TEXT("Undo exact sections"),Sections()==OriginalSections);TestEqual(TEXT("Undo source"),Source(W),OriginalSource);TestTrue(TEXT("Undo caches"),Caches()==OriginalCaches);Attributes();
  GEditor->RedoTransaction();TestTrue(TEXT("Redo exact sections"),Sections()==AppliedSections);TestEqual(TEXT("Redo source"),Source(W),AppliedSource);TestTrue(TEXT("Redo caches"),Caches()==AppliedCaches);Attributes();
  TestTrue(TEXT("Remove opening through same command"),Edit({}).bSucceeded);TestTrue(TEXT("Removal restores wall material area"),FMath::Abs(MeshArea(W->LeftWallMeshComponent)-BaseArea)<0.1);Attributes();
  FEHBElementRelation Side;Side.Type=EEHBElementRelationType::PhysicalContact;Side.bGeometryDependent=true;
  Side.Source=FEHBElementRelationEndpoint::MakeElement(W->ElementGuid,EEHBElementSurfaceKind::LeftSide);
  Side.Target=FEHBElementRelationEndpoint::MakeElement(Setup.Floors[0],EEHBElementSurfaceKind::Bottom);
  const FGuid SideId=B->AddOrUpdateElementRelation(Side);if(!TestTrue(TEXT("Side dependency fixture valid"),SideId.IsValid()))return false;
  const auto Refused=Edit({Cut});TestFalse(TEXT("Unplanned side dependency refuses source edit"),Refused.bSucceeded);TestEqual(TEXT("Side plan refusal"),Refused.Message,FString(TEXT("OpeningSideDependencyRequiresPlan")));TestTrue(TEXT("Refusal preserves source"),W->CutOperations.IsEmpty());B->RemoveElementRelation(SideId);
 }
 return true;
}
