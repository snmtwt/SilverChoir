// Copyright Epic Games, Inc. All Rights Reserved.
#include "Core/EHBBuildingActorBase.h"
#include "Core/EHBActorImportScope.h"
#include "Core/EHBWallNodeGeometryDraft.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Wall.h"
#include "Components/EHBWallJunctionComponent.h"
#include "Materials/Material.h"
#include "Engine/World.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Misc/Base64.h"
#include "Core/EHBPreparedWallOpening.h"

namespace
{
	FString NodeModelText(const FEHBWallNodeModel& Model)
	{
		// Transient comparison only, not a saved format. Preserve full property
		// precision and names without adding a runtime JSON conversion dependency.
		TArray<uint8> Bytes;FMemoryWriter Writer(Bytes);FObjectAndNameAsStringProxyArchive Archive(Writer,false);
		auto Copy=Model;FEHBWallNodeModel::StaticStruct()->SerializeItem(Archive,&Copy,nullptr);
		return FBase64::Encode(Bytes);
	}
}

AEHB_Wall* AEHBBuildingActorBase::CreateWallFromNodePlan(const FEHBNodeConnectedWallDefinition& Definition,const FEHBWallJunctionWallSides& Geometry)
{
	if (WallNodeAuthority.Version!=2 || WallNodeOwnership.Version!=1 || !GetWorld()
		|| !Definition.WallGuid.IsValid() || Definition.WallGuid!=Geometry.WallGuid || FindElementActorByGuid(Definition.WallGuid)
		|| !FMath::IsFinite(Definition.Height) || Definition.Height<1 || !FMath::IsFinite(Definition.Thickness) || Definition.Thickness<1) return nullptr;
	const auto* Start=WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Definition.StartNodeGuid;});
	const auto* End=WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Definition.EndNodeGuid;});
	if (!Start || !End || Start==End || Start->FloorIndex!=End->FloorIndex || Start->FloorIndex<1) return nullptr;
	const int32 Floor=Start->FloorIndex;
	const FGuid FirstId=FindPhysicalPillarForNode(Start->NodeGuid),SecondId=FindPhysicalPillarForNode(End->NodeGuid);
	auto* First=Cast<AEHB_Pillar>(FindElementActorByGuid(FirstId));auto* Second=Cast<AEHB_Pillar>(FindElementActorByGuid(SecondId));
	if ((FirstId.IsValid()&&!First)||(SecondId.IsValid()&&!Second)) return nullptr;
	FActorSpawnParameters Params;Params.Owner=this;Params.OverrideLevel=GetLevel();Params.ObjectFlags=RF_Transactional;
	Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Wall=GetWorld()->SpawnActor<AEHB_Wall>(Params);if(!Wall)return nullptr;
	Wall->Modify();Wall->ElementGuid=Definition.WallGuid;Wall->ElementName=Wall->GetFName();
	Wall->ConfigureAsSimpleWall(this,First,Second,Geometry.LocalStart,Geometry.LocalEnd,Definition.Height,Definition.Thickness,true);
	Wall->SetFloorAssignment(Floor,EEHBBuildingFloorElementRole::FloorBody);
	RegisterElementActor(Wall);
	bool Valid=true;
	BeginRelationshipEdit();
	for (bool AtStart:{true,false})
	{
		FEHBElementRelation Relation;Relation.Type=EEHBElementRelationType::TopologyConnection;
		Relation.Source=FEHBElementRelationEndpoint::MakeElement(Wall->ElementGuid,AtStart?EEHBElementSurfaceKind::Start:EEHBElementSurfaceKind::End,AtStart?TEXT("Wall.Start"):TEXT("Wall.End"));
		Relation.Target=FEHBElementRelationEndpoint::MakeNode(AtStart?Definition.StartNodeGuid:Definition.EndNodeGuid);
		Relation.Origin=EEHBRelationOrigin::SystemGenerated;Relation.bAffectsFloorAssignment=false;
		Valid &= AddOrUpdateElementRelation(Relation,true).IsValid();
	}
	EndRelationshipEdit(false);
	if(!Valid){Wall->Destroy();return nullptr;}
	Wall->ApplyResolvedNodeGeometry(Geometry,true);MarkPackageDirty();return Wall;
}

UEHBWallJunctionComponent* AEHBBuildingActorBase::FindWallNodeJunction(FGuid NodeGuid) const
{
	const auto* Found=WallNodeJunctions.Find(NodeGuid);return Found&&IsValid(*Found)?Found->Get():nullptr;
}

bool AEHBBuildingActorBase::RebuildWallNodeAuthorityGeometry()
{
	return RebuildWallNodeAuthorityGeometryImpl(nullptr,nullptr,nullptr);
}

FEHBConnectedWallRefreshStats AEHBBuildingActorBase::RefreshWallNodeMoveGeometry(const FEHBWallNodeModel& Source,const FEHBWallNodeModelEditDraft& Draft)
{
	FEHBConnectedWallRefreshStats Stats;
	auto Fail=[&](FName Status){Stats.bSucceeded=false;Stats.Status=Status;return Stats;};
	if(WallNodeAuthority.Version!=2||!Draft.bSucceeded||!Draft.UpdatePlan.bSucceeded||!Draft.RemovedPhysicalPillarGuids.IsEmpty())return Fail(TEXT("RequiresPositionMoveDraft"));
	TArray<FEHBNodeMoveRequest> Requests;
	for(FGuid Id:Draft.UpdatePlan.MovedNodeGuids)
	{
		const auto* Before=Source.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;});
		const auto* After=Draft.Definitions.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;});
		if(!Before||!After)return Fail(TEXT("UnknownMovedNode"));
		auto& R=Requests.AddDefaulted_GetRef();R.NodeGuid=Id;R.ExpectedPosition=Before->LocalTransform.GetLocation();R.TargetPosition=After->LocalTransform.GetLocation();
	}
	const auto Expected=UEHBWallTopologyLibrary::BuildWallNodeModelMoveDraft(Source,Requests);
	if(!Expected.bSucceeded||Expected.bWouldChange!=Draft.bWouldChange||NodeModelText(Expected.Definitions)!=NodeModelText(Draft.Definitions)
		||Expected.UpdatePlan.MovedNodeGuids!=Draft.UpdatePlan.MovedNodeGuids||Expected.UpdatePlan.JunctionNodeGuids!=Draft.UpdatePlan.JunctionNodeGuids||Expected.UpdatePlan.WallGuids!=Draft.UpdatePlan.WallGuids)return Fail(TEXT("ChangedMoveDraft"));
	FEHBWallNodeModel Current;
	if(!UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(this,Current).bSucceeded||NodeModelText(Current)!=NodeModelText(Draft.Definitions))return Fail(TEXT("MovePoseNotApplied"));
	if(!Draft.bWouldChange){Stats.Status=TEXT("NoChange");return Stats;}
	const auto SourceText=NodeModelText(Source);
	Stats.bSucceeded=RebuildWallNodeAuthorityGeometryImpl(&Draft.UpdatePlan,&SourceText,&Stats);
	Stats.Status=Stats.bSucceeded?(Stats.bUsedScopedUpdate?TEXT("AppliedNeighborhood"):TEXT("AppliedFullFallback")):TEXT("InvalidNodeAuthoritySource");return Stats;
}

bool AEHBBuildingActorBase::RebuildWallNodeAuthorityGeometryImpl(const FEHBWallMoveUpdatePlan* Plan,const FString* ExpectedSource,FEHBConnectedWallRefreshStats* Stats)
{
	if(FEHBActorImportScope::IsActive()||bRebuildingWallNodeGeometry)return false;
	TGuardValue<bool> Guard(bRebuildingWallNodeGeometry,true);
	TInlineComponentArray<UEHBWallJunctionComponent*> Existing(this);
	const FName DerivedTag(TEXT("EHB.NodeAuthorityDerived"));
	// Undo can restore an InstanceComponents entry after the corresponding
	// transient fill was destroyed. Reconcile that registry as well as the live
	// owned-component set; a dead entry must not be exported by a later copy.
	// Saving transient references also leaves null slots on cold load.
	auto RemoveDeadInstanceEntries=[&]()
	{
		const auto Instances=GetInstanceComponents();
		for(auto* C:Instances)
			if(!C||(C->IsA<UEHBWallJunctionComponent>()&&C->ComponentHasTag(DerivedTag)&&!IsValid(C)))RemoveInstanceComponent(C);
	};
	if(WallNodeAuthority.Version!=2)
	{
		LastRenderedWallNodeSource.Reset();LastRenderedWallNodeElementRevisions.Reset();LastRenderedWallNodeWallTransforms.Reset();
		RemoveDeadInstanceEntries();
		for(auto* C:Existing)if(IsValid(C)&&C->ComponentHasTag(DerivedTag))C->DestroyComponent();
		WallNodeJunctions.Reset();return true;
	}
	FEHBWallNodeModel Model;const auto Capture=UEHBWallTopologyLibrary::CaptureWallNodeModelWithSurfaceOpenings(this,Model);if(!Capture.bSucceeded)return false;
	TMap<FGuid,AEHB_Wall*> Walls;TSet<FGuid> Bound,Needed;
	TMap<FGuid,const FEHBWallNodeDefinition*> Nodes;for(const auto& N:Model.Nodes)Nodes.Add(N.NodeGuid,&N);
	for(const auto& Binding:Model.PillarBindings)Bound.Add(Binding.NodeGuid);
	for(const auto& W:Model.Walls)
	{
		auto* Wall=Cast<AEHB_Wall>(FindElementActorByGuid(W.WallGuid));if(!Wall||!Wall->CanApplyNodeDefinition(W,true))return false;Walls.Add(W.WallGuid,Wall);
		for(FGuid Id:{W.StartNodeGuid,W.EndNodeGuid})if(!Bound.Contains(Id))Needed.Add(Id);
	}
	TSet<FGuid> AffectedWalls,AffectedNodes,AffectedPillars;
	bool Scoped=Plan&&ExpectedSource&&*ExpectedSource==LastRenderedWallNodeSource;
	if(Scoped)
	{
		for(FGuid Id:Plan->WallGuids)AffectedWalls.Add(Id);
		for(FGuid Id:Plan->JunctionNodeGuids)AffectedNodes.Add(Id);
		// Physical columns read staged wall frames. Include the far bound endpoint
		// of every affected wall, beyond the one-hop logical junction neighborhood.
		TSet<FGuid> PhysicalNodes=AffectedNodes;
		for(const auto& W:Model.Walls)if(AffectedWalls.Contains(W.WallGuid)){PhysicalNodes.Add(W.StartNodeGuid);PhysicalNodes.Add(W.EndNodeGuid);}
		for(const auto& Binding:Model.PillarBindings)if(PhysicalNodes.Contains(Binding.NodeGuid))AffectedPillars.Add(Binding.PhysicalPillarGuid);
		for(const auto& W:Model.Walls)if(!AffectedWalls.Contains(W.WallGuid))
		{
			const auto* Revision=LastRenderedWallNodeElementRevisions.Find(W.WallGuid);Scoped&=Revision&&*Revision==GetElementGeometryRevision(W.WallGuid);
			const auto* Wall=Walls.FindChecked(W.WallGuid);
			Scoped&=Wall->LeftWallMeshComponent&&Wall->LeftWallMeshComponent->GetProcMeshSection(0)
				&&Wall->RightWallMeshComponent&&Wall->RightWallMeshComponent->GetProcMeshSection(0)
				&&Wall->CapMeshComponent&&Wall->CapMeshComponent->GetProcMeshSection(0);
			const auto* Transform=LastRenderedWallNodeWallTransforms.Find(W.WallGuid);
			Scoped&=Transform&&Wall->GetElementLocalTransform().Equals(*Transform,1.e-8);
		}
		for(const auto& Binding:Model.PillarBindings)if(!AffectedPillars.Contains(Binding.PhysicalPillarGuid))
		{
			const auto* Revision=LastRenderedWallNodeElementRevisions.Find(Binding.PhysicalPillarGuid);Scoped&=Revision&&*Revision==GetElementGeometryRevision(Binding.PhysicalPillarGuid);
			const auto* P=Cast<AEHB_Pillar>(FindElementActorByGuid(Binding.PhysicalPillarGuid));Scoped&=P&&P->PillarMeshComponent&&P->PillarMeshComponent->GetProcMeshSection(0);
		}
		TSet<FGuid> Seen;
		TSet<UActorComponent*> Instances,Owned;
		for(auto* C:GetInstanceComponents())Instances.Add(C);
		for(auto* C:Existing)Owned.Add(C);
		for(auto* C:Existing)if(IsValid(C)&&C->ComponentHasTag(DerivedTag))
		{
			Scoped&=C->IsRegistered()&&Needed.Contains(C->NodeGuid)&&!Seen.Contains(C->NodeGuid)&&FindWallNodeJunction(C->NodeGuid)==C&&Instances.Contains(C)&&C->GetProcMeshSection(0);
			Seen.Add(C->NodeGuid);
			if(!AffectedNodes.Contains(C->NodeGuid))
			{
				const auto* N=Nodes.FindRef(C->NodeGuid);
				Scoped&=N&&C->SourceGeometryRevision==N->GeometryRevision&&C->GetRelativeTransform().Equals(N->LocalTransform,1.e-8);
			}
		}
		Scoped&=Seen.Num()==Needed.Num()&&WallNodeJunctions.Num()==Needed.Num();
		for(auto* C:GetInstanceComponents())if(!IsValid(C)||(C->IsA<UEHBWallJunctionComponent>()&&C->ComponentHasTag(DerivedTag)&&!Owned.Contains(C)))Scoped=false;
	}
	// Select only after proving the outside region still has a valid full-output
	// baseline. A selection proves its own geometry, never the entire building.
	FName Reason;FEHBWallNodeGeometryDraft Geometry;FEHBWallNodeGeometrySelection Selection;
	TSet<FGuid> SelectedJunctions;
	if(Scoped)
	{
		for(FGuid Id:Needed)if(AffectedNodes.Contains(Id))SelectedJunctions.Add(Id);
		Selection=FEHBWallNodeGeometrySelection::Build(Model,AffectedWalls,SelectedJunctions,Reason);
		if(!Selection.IsReady())return false;
	}
	else{Geometry=FEHBWallNodeGeometryDraft::Build(Model,Reason);if(!Geometry.IsReady())return false;}
	const auto& Sides=Scoped?Selection.GetWallSides():Geometry.GetWallSides();
	const auto& Junctions=Scoped?Selection.GetUnboundJunctions():Geometry.GetUnboundJunctions();
	TMap<FGuid,FEHBPreparedWallOpening> Openings;
	for(const auto& Side:Sides)if(auto* W=Walls.FindChecked(Side.WallGuid);!W->CutOperations.IsEmpty())
	{
		FEHBPreparedWallOpening Candidate;if(!W->PrepareNodeSurfaceOpening(Side,Candidate,Reason))return false;
		Openings.Add(Side.WallGuid,MoveTemp(Candidate));
	}
	for(FGuid Id:Needed)if((!Scoped||SelectedJunctions.Contains(Id))&&!Junctions.Contains(Id))return false;
	if(Stats)
	{
		Stats->bUsedScopedUpdate=Scoped;++Stats->DefinitionSolveCalls;
		const auto& Solved=Scoped?Selection.GetSolveStats():Geometry.GetSolveStats();
		Stats->WallSidesSolved=Solved.WallSidesSolved;Stats->NodeFootprintsSolved=Solved.NodeFootprintsSolved;Stats->UnboundMeshesBuilt=Junctions.Num();
	}
	// Invalidate before any writes: failed/partial application cannot authorize a
	// later narrow update. The outer editor transaction restores source on failure.
	LastRenderedWallNodeSource.Reset();LastRenderedWallNodeElementRevisions.Reset();LastRenderedWallNodeWallTransforms.Reset();
	// Reconcile actual owned components as well as the derived index. Transaction
	// restoration and registration order must not leave an unindexed visible fill.
	RemoveDeadInstanceEntries();
	WallNodeJunctions.Reset();
	for(auto* C:Existing)if(IsValid(C)&&C->ComponentHasTag(DerivedTag))
	{
		if(Needed.Contains(C->NodeGuid)&&!WallNodeJunctions.Contains(C->NodeGuid))WallNodeJunctions.Add(C->NodeGuid,C);
		else C->DestroyComponent();
	}
	for(const auto& Side:Sides)if(!Scoped||AffectedWalls.Contains(Side.WallGuid))Walls.FindChecked(Side.WallGuid)->StageResolvedNodeGeometry(Side,true);
	for(const auto& Binding:Model.PillarBindings)if(!Scoped||AffectedPillars.Contains(Binding.PhysicalPillarGuid))if(auto* P=Cast<AEHB_Pillar>(FindElementActorByGuid(Binding.PhysicalPillarGuid))){P->RebuildPillarMesh();if(Stats)++Stats->PillarsVisited;}
	for(const auto& Side:Sides)if(!Scoped||AffectedWalls.Contains(Side.WallGuid)){Walls.FindChecked(Side.WallGuid)->ApplyResolvedNodeGeometry(Side,true);if(Stats)++Stats->WallRefreshCalls;}
	// Legacy mesh rebuilds may decline a cut. Never accept an uncut fallback as
	// a successful node edit; the enclosing editor transaction will roll it back.
	for(const auto& Pair:Openings)
	{
		auto* W=Walls.FindChecked(Pair.Key);
		auto Matches=[](const UEHBGeneratedMeshComponent* C,const FEHBWallJunctionMesh& M)
		{
			const auto* S=C?C->GetProcMeshSection(0):nullptr;if(!S||S->ProcVertexBuffer.Num()!=M.Vertices.Num()||S->ProcIndexBuffer.Num()!=M.Triangles.Num())return false;
			for(int32 I=0;I<M.Vertices.Num();++I)if(!S->ProcVertexBuffer[I].Position.Equals(M.Vertices[I],0.001))return false;
			for(int32 I=0;I<M.Triangles.Num();++I)if(S->ProcIndexBuffer[I]!=static_cast<uint32>(M.Triangles[I]))return false;return true;
		};
		if(!Matches(W->LeftWallMeshComponent,Pair.Value.Left)||!Matches(W->RightWallMeshComponent,Pair.Value.Right)||!Matches(W->CapMeshComponent,Pair.Value.Caps))
		{UE_LOG(LogTemp,Warning,TEXT("Node opening mesh mismatch: left=%d right=%d caps=%d"),Matches(W->LeftWallMeshComponent,Pair.Value.Left),Matches(W->RightWallMeshComponent,Pair.Value.Right),Matches(W->CapMeshComponent,Pair.Value.Caps));return false;}
	}
	for(FGuid Id:Needed)
	{
		if(Scoped&&!AffectedNodes.Contains(Id))continue;
		auto* Junction=FindWallNodeJunction(Id);
		if(!Junction)
		{
			Junction=NewObject<UEHBWallJunctionComponent>(this,NAME_None,RF_Transient);
			Junction->ComponentTags.Add(DerivedTag);
			Junction->SetupAttachment(GetRootComponent());AddInstanceComponent(Junction);Junction->RegisterComponent();
			Junction->SetMaterial(0,UMaterial::GetDefaultMaterial(MD_Surface));WallNodeJunctions.Add(Id,Junction);
		}
		if(!(Scoped?Junction->RebuildFromGeometrySelection(Selection,Id):Junction->RebuildFromGeometryDraft(Geometry,Id)))return false;
		if(Stats)++Stats->JunctionRefreshCalls;
	}
	for(auto It=WallNodeJunctions.CreateIterator();It;++It)if(!Needed.Contains(It.Key())){if(IsValid(It.Value()))It.Value()->DestroyComponent();It.RemoveCurrent();}
	RebuildClosedLoops();
	LastRenderedWallNodeSource=NodeModelText(Model);
	for(const auto& W:Model.Walls)
	{
		LastRenderedWallNodeElementRevisions.Add(W.WallGuid,GetElementGeometryRevision(W.WallGuid));
		LastRenderedWallNodeWallTransforms.Add(W.WallGuid,Walls.FindChecked(W.WallGuid)->GetElementLocalTransform());
	}
	for(const auto& Binding:Model.PillarBindings)LastRenderedWallNodeElementRevisions.Add(Binding.PhysicalPillarGuid,GetElementGeometryRevision(Binding.PhysicalPillarGuid));
	return true;
}
