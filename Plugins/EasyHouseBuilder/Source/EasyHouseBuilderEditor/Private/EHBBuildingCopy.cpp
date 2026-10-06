#include "EHBBuildingCopy.h"
#include "EHBCopyOutlinePolicy.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Core/EHBActorImportScope.h"
#include "Core/EHBWallNodeCopy.h"
#include "Core/EHBWallNodeRooms.h"
#include "Core/EHBPreparedWallOpening.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Wall.h"
#include "EHB_Building.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "EngineUtils.h"
#include "LevelUtils.h"
#include "Components/SceneComponent.h"
#include "Components/EHBWallJunctionComponent.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "JsonObjectConverter.h"
#include "ScopedTransaction.h"
#include "Engine/Selection.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "LevelEditor.h"
#include "EditorModeManager.h"
#include "EasyHouseEditorMode.h"

namespace
{
	bool MatchesOpeningMesh(const UEHBGeneratedMeshComponent* Component, const FEHBWallJunctionMesh& Expected)
	{
		const auto* Mesh = Component ? Component->GetProcMeshSection(0) : nullptr;
		if (!Mesh || Component->GetNumSections() != 1 || Mesh->ProcVertexBuffer.Num() != Expected.Vertices.Num()
			|| Mesh->ProcIndexBuffer.Num() != Expected.Triangles.Num()) return false;
		for (int32 I = 0; I < Expected.Vertices.Num(); ++I)
			if (!Mesh->ProcVertexBuffer[I].Position.Equals(Expected.Vertices[I], 0.00001)) return false;
		for (int32 I = 0; I < Expected.Triangles.Num(); ++I)
			if (Mesh->ProcIndexBuffer[I] != static_cast<uint32>(Expected.Triangles[I])) return false;
		return true;
	}
	bool CopyWallOpeningSources(const AEHB_Wall* Source, AEHB_Wall* Target)
	{
		if (!Source || !Target || !Source->OwningBuilding || !Target->OwningBuilding) return false;
		auto Cuts = Source->CutOperations;
		for (auto& Cut : Cuts)
		{
			if (Cut.SurfaceHost.Version != 1 || Cut.SurfaceHost.BuildingGuid != Source->OwningBuilding->BuildingGuid
				|| Cut.SurfaceHost.ElementGuid != Source->ElementGuid) return false;
			FName Name;
			for (FName Side : {FName(TEXT("Wall.Left")), FName(TEXT("Wall.Right"))})
				if (Cut.SurfaceHost.SurfaceGuid.IsValid() && Cut.SurfaceHost.SurfaceGuid == Source->FindLogicalSurfaceIdentity(Side)) Name = Side;
			const FGuid Surface = Target->FindLogicalSurfaceIdentity(Name);
			if (Name.IsNone() || !Surface.IsValid()) return false;
			Cut.SurfaceHost.BuildingGuid = Target->OwningBuilding->BuildingGuid;
			Cut.SurfaceHost.ElementGuid = Target->ElementGuid;
			Cut.SurfaceHost.SurfaceGuid = Surface;
		}
		Target->CutOperations = MoveTemp(Cuts);
		return true;
	}
	// Geometry and its recorded partition source travel together. Never reconstruct
	// precise opening coordinates from actor text export, which rounds floats.
	bool CopySlabSources(const AEHB_FloorSlab* Source, AEHB_FloorSlab* Target)
	{
		if (!Source || !Target || !Source->OwningBuilding || !Target->OwningBuilding) return false;
		auto Cuts = Source->CutOperations;
		for (auto& Cut : Cuts)
		{
			if (Cut.SurfaceHost.Version == 0) continue;
			if (Cut.SurfaceHost.Version != 1
				|| Cut.SurfaceHost.BuildingGuid != Source->OwningBuilding->BuildingGuid
				|| Cut.SurfaceHost.ElementGuid != Source->ElementGuid
				|| Cut.SurfaceHost.SurfaceGuid != Source->FindLogicalSurfaceIdentity(TEXT("Slab.Surface"))) return false;
			const FGuid Surface = Target->FindLogicalSurfaceIdentity(TEXT("Slab.Surface"));
			if (!Surface.IsValid()) return false;
			Cut.SurfaceHost.BuildingGuid = Target->OwningBuilding->BuildingGuid;
			Cut.SurfaceHost.ElementGuid = Target->ElementGuid;
			Cut.SurfaceHost.SurfaceGuid = Surface;
		}
		Target->LocalTopPolygon = Source->LocalTopPolygon;
		Target->LocalHoles = Source->LocalHoles;
		Target->CutOperations = MoveTemp(Cuts);
		Target->VisualExpansion = Source->VisualExpansion;
		Target->DisplayPartition = Source->DisplayPartition;
		if (Target->DisplayPartition.SourceVersion == 1)
			Target->DisplayPartition.SourceCuts = Target->CutOperations;
		return true;
	}
	TWeakPtr<FUICommandList> DuplicateCommandList;
	TOptional<FUIAction> OriginalDuplicateAction;
	FDelegateHandle DuplicateExecuteHandle;

	bool IsBuildingActor(const AActor* Actor)
	{
		return Actor && (Actor->IsA<AEHBBuildingActorBase>() || Actor->IsA<AEHBElementActorBase>());
	}

	bool SelectionTouchesBuilding()
	{
		if (!GEditor) return false;
		for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
			if (IsBuildingActor(Cast<AActor>(*It))) return true;
		for (FSelectionIterator It(*GEditor->GetSelectedComponents()); It; ++It)
			if (const auto* Component = Cast<UActorComponent>(*It); Component && IsBuildingActor(Component->GetOwner())) return true;
		return false;
	}

	void DuplicateBuildingSelection()
	{
		// The Level Editor's Typed Elements path bypasses FEdMode::ProcessEditDuplicate
		// when actors are selected. Intercept its actual FGenericCommands binding instead.
		if (!GEditor || !SelectionTouchesBuilding()) return;
		FText Message;
		AEHBBuildingActorBase* Source = nullptr;
		TArray<AActor*> Selected;
		for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
			if (auto* Actor = Cast<AActor>(*It)) Selected.Add(Actor);
		for (auto* Actor : Selected)
			if (auto* Building = Cast<AEHBBuildingActorBase>(Actor))
			{
				if (Source) { Source = nullptr; break; }
				Source = Building;
			}
		// USelection::Num returns the shared Typed Elements list size, including actors.
		const int32 SelectedComponentCount = GEditor->GetSelectedComponents()->CountSelections<UActorComponent>();
		bool bWholeBuilding = Source && SelectedComponentCount == 0
			&& GEditor->GetSelectedActors()->Num() == Selected.Num(); // Reject mixed non-actor Typed Elements too.
		for (auto* Actor : Selected)
			if (Actor != Source)
			{
				const auto* Element = Cast<AEHBElementActorBase>(Actor);
				bWholeBuilding &= Element && Element->OwningBuilding == Source && Element->GetAttachParentActor() == Source;
			}
		if (!bWholeBuilding)
		{
			UE_LOG(LogTemp, Log, TEXT("EHB duplicate selection rejected: actors=%d components=%d source=%s"), Selected.Num(), SelectedComponentCount, *GetNameSafe(Source));
			Message = NSLOCTEXT("EHBBuildingCopy", "WholeSelectionRequired", "请选中一栋建筑的主对象后复制。暂不支持只复制构件、同时复制多栋建筑或混合其他对象。");
		}
		else
		{
			const auto Result = EHBBuildingCopy::Execute(Source, EHBBuildingCopy::SuggestedWorldOffset(Source));
			if (!Result.bSucceeded)
				UE_LOG(LogTemp, Warning, TEXT("EHB Level Editor duplicate rejected: %s / %s"), *Result.Status.ToString(), *Result.FailureReason.ToString());
			if (Result.bSucceeded)
			{
				if (auto* Mode = GLevelEditorModeTools().GetActiveModeTyped<FEasyHouseEditorMode>(FEasyHouseEditorMode::EM_EasyHouseEditorModeId))
					Mode->AdoptCopiedBuilding(Result.Building);
				GEditor->SelectNone(false, true, false);
				GEditor->SelectActor(Result.Building, true, false);
				GEditor->NoteSelectionChange();
				GEditor->RedrawLevelEditingViewports();
				Message = NSLOCTEXT("EHBBuildingCopy", "WholeCopyDone", "已复制整栋建筑并选中新建筑；可用撤销恢复。");
			}
			else if (Result.Status == TEXT("RequiresTypedNodeOwnership"))
				Message = NSLOCTEXT("EHBBuildingCopy", "CopyNeedsMigration", "此建筑尚未准备墙柱节点连接，暂不能整栋复制。");
			else
				Message = FText::Format(NSLOCTEXT("EHBBuildingCopy", "CopyFailed", "建筑复制未完成：{0}。没有继续执行普通对象复制。"), FText::FromName(Result.FailureReason.IsNone() ? Result.Status : Result.FailureReason));
		}
		FNotificationInfo Notice(Message);
		Notice.ExpireDuration = 6;
		FSlateNotificationManager::Get().AddNotification(Notice);
	}
}

FVector EHBBuildingCopy::SuggestedWorldOffset(const AEHBBuildingActorBase* Source)
{
	if (!Source) return FVector(1000, 0, 0);
	FBox Bounds = Source->GetComponentsBoundingBox(true);
	for (const auto* Element : Source->QueryElements(FEHBElementQuery())) Bounds += Element->GetComponentsBoundingBox(true);
	return FVector(Bounds.IsValid ? FMath::Max(200.0, Bounds.GetSize().X + 200.0) : 1000.0, 0, 0);
}

void EHBBuildingCopy::RegisterDuplicateCommand()
{
	if (OriginalDuplicateAction.IsSet() || !GEditor || IsRunningCommandlet() || !FGenericCommands::IsRegistered()) return;
	auto Commands = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor")).GetGlobalLevelEditorActions();
	const auto* Original = Commands->GetActionForCommand(FGenericCommands::Get().Duplicate);
	if (!Original) return;
	OriginalDuplicateAction = *Original;
	// Keep the original full action for all unrelated selections, including its enable state.
	FUIAction Replacement = *Original;
	Replacement.ExecuteAction = FExecuteAction::CreateLambda([]
	{
		if (SelectionTouchesBuilding()) DuplicateBuildingSelection();
		else if (OriginalDuplicateAction.IsSet()) OriginalDuplicateAction->Execute();
	});
	Replacement.CanExecuteAction = FCanExecuteAction::CreateLambda([]
	{
		if (SelectionTouchesBuilding()) return GEditor && !GEditor->PlayWorld && !GEditor->IsTransactionActive();
		return OriginalDuplicateAction.IsSet() && OriginalDuplicateAction->CanExecute();
	});
	DuplicateExecuteHandle = Replacement.ExecuteAction.GetHandle();
	Commands->UnmapAction(FGenericCommands::Get().Duplicate);
	Commands->MapAction(FGenericCommands::Get().Duplicate, Replacement);
	DuplicateCommandList = Commands;
}

void EHBBuildingCopy::UnregisterDuplicateCommand()
{
	// Generic commands can be destroyed before editor plugins during engine shutdown.
	// Get() dereferences a weak singleton, so only use it while its context is registered.
	if (auto Commands = DuplicateCommandList.Pin(); Commands && OriginalDuplicateAction.IsSet() && FGenericCommands::IsRegistered())
	{
		const auto* Current = Commands->GetActionForCommand(FGenericCommands::Get().Duplicate);
		if (Current && Current->ExecuteAction.GetHandle() == DuplicateExecuteHandle)
		{
			Commands->UnmapAction(FGenericCommands::Get().Duplicate);
			Commands->MapAction(FGenericCommands::Get().Duplicate, OriginalDuplicateAction.GetValue());
		}
	}
	OriginalDuplicateAction.Reset();
	DuplicateCommandList.Reset();
	DuplicateExecuteHandle.Reset();
}

namespace
{
	bool MapId(FGuid& Id, const TMap<FGuid, FGuid>& Map)
	{
		const FGuid* NewId = Map.Find(Id);
		if (!NewId) return false;
		Id = *NewId;
		return true;
	}

	bool MapPhysicalId(FGuid& Id, const TMap<FGuid, FGuid>& Map, bool bOptional)
	{
		return !Id.IsValid() ? bOptional : MapId(Id, Map);
	}

	// Historical snapshots retain their old dimensions/poses. Unknown historical IDs
	// require an explicit policy; never silently replace a snapshot with current geometry.
	bool MapSnapshots(const FEHBWallNodeCopyDraft& Draft, FEHBPersistedWallTopology& Baseline,
		FEHBPreparedWallNodeDefinitions& Prepared, bool bOptional)
	{
		if (Baseline.Version < 0 || Baseline.Version > 1 || Prepared.Version < 0 || Prepared.Version > 1) return false;
		for (auto& Node : Baseline.Nodes)
			if (!MapId(Node.NodeGuid, Draft.NodeGuids) || !MapPhysicalId(Node.SourcePillarGuid, Draft.ElementGuids, bOptional)) return false;
		for (auto& Wall : Baseline.Walls)
			if (!MapId(Wall.WallGuid, Draft.ElementGuids) || !MapId(Wall.StartNodeGuid, Draft.NodeGuids) || !MapId(Wall.EndNodeGuid, Draft.NodeGuids)) return false;
		for (auto& Node : Prepared.Nodes) if (!MapId(Node.NodeGuid, Draft.NodeGuids)) return false;
		for (auto& Binding : Prepared.PillarBindings)
			if (!MapId(Binding.NodeGuid, Draft.NodeGuids) || !MapId(Binding.PhysicalPillarGuid, Draft.ElementGuids)) return false;
		for (auto& Wall : Prepared.Walls)
			if (!MapId(Wall.WallGuid, Draft.ElementGuids) || !MapId(Wall.StartNodeGuid, Draft.NodeGuids) || !MapId(Wall.EndNodeGuid, Draft.NodeGuids)) return false;
		return true;
	}
}

FEHBBuildingCopyResult EHBBuildingCopy::Execute(AEHBBuildingActorBase* Source, const FVector& WorldOffset)
{
	FEHBBuildingCopyResult Result;
	auto Fail = [&](FName Status) { Result.Status = Status; return Result; };
	if (!GEditor || !GUnrealEd || GEditor->PlayWorld || GEditor->IsTransactionActive() || FEHBActorImportScope::IsActive())
		return Fail(TEXT("RequiresIndependentEditorTransaction"));
	if (!IsValid(Source) || Source->GetClass() != AEHB_Building::StaticClass() || Source->GetAttachParentActor()
		|| Source->GetWorld() != GEditor->GetEditorWorldContext().World() || Source->IsActorBeingDestroyed())
		return Fail(TEXT("RequiresNativeEditorBuilding"));
	if (FLevelUtils::IsLevelLocked(Source->GetLevel()) || !FLevelUtils::IsLevelVisible(Source->GetLevel()))
		return Fail(TEXT("RequiresVisibleUnlockedLevel"));
	if (WorldOffset.ContainsNaN()) return Fail(TEXT("InvalidWorldOffset"));
	if (Source->WallNodeOwnership.Version != 1) return Fail(TEXT("RequiresTypedNodeOwnership"));
	const bool bOptional = Source->WallNodeAuthority.Version == 2;
	TSet<FGuid> OpeningWalls;
	for (auto* Element : Source->QueryElements(FEHBElementQuery()))
		if (const auto* Wall = Cast<AEHB_Wall>(Element); Wall && !Wall->CutOperations.IsEmpty())
		{
			FEHBPreparedWallOpening Candidate; FName Status;
			if (!Wall->PrepareSurfaceOpening(Wall->CutOperations, Candidate, Status)) return Fail(Status);
			OpeningWalls.Add(Wall->ElementGuid);
		}
	FEHBWallNodeModel Model;
	const auto Capture = UEHBWallTopologyLibrary::CaptureWallNodeModelSource(Source, Model, &OpeningWalls);
	if (!Capture.bSucceeded) return Fail(Capture.Status);

	TArray<AActor*> Actors;
	Source->GetAttachedActors(Actors, true, true);
	const auto Elements = Source->QueryElements(FEHBElementQuery());
	if (Actors.Num() != Elements.Num() || Actors.IsEmpty()) return Fail(TEXT("IncompleteCopyBoundary"));
	TArray<FGuid> Ids;
	for (AActor* Actor : Actors)
	{
		auto* Element = Cast<AEHBElementActorBase>(Actor);
		if (!Element || !Elements.Contains(Element) || Element->OwningBuilding != Source || Actor->GetAttachParentActor() != Source
			|| Actor->GetLevel() != Source->GetLevel() || Actor->IsActorBeingDestroyed()
			|| (Actor->GetClass() != AEHB_Pillar::StaticClass() && Actor->GetClass() != AEHB_Wall::StaticClass()
				&& Actor->GetClass() != AEHB_Floor::StaticClass() && Actor->GetClass() != AEHB_FloorSlab::StaticClass()))
			return Fail(TEXT("RequiresCompleteNativeWallPillarGroup"));
		Ids.Add(Element->ElementGuid);
	}
	for (TActorIterator<AEHBElementActorBase> It(Source->GetWorld()); It; ++It)
		if (It->OwningBuilding == Source && !It->IsActorBeingDestroyed() && !Actors.Contains(*It))
			return Fail(TEXT("UnattachedOwnedElement"));
	Actors.Insert(Source, 0);
	for (const AActor* Actor : Actors)
		for (const auto* Component : Actor->GetInstanceComponents())
		{
			const auto* Junction = Cast<UEHBWallJunctionComponent>(Component);
			// Derived fills are regenerated from mapped logical IDs. They are never
			// copied as authored physical elements or permitted as arbitrary extensions.
			if (!bOptional || Actor != Source || !Junction || Junction->GetClass() != UEHBWallJunctionComponent::StaticClass()
				|| !Junction->HasAnyFlags(RF_Transient) || !Junction->ComponentHasTag(TEXT("EHB.NodeAuthorityDerived"))
				|| Junction->GetOwner() != Source || Junction->GetAttachParent() != Source->GetRootComponent()
				|| Source->FindWallNodeJunction(Junction->NodeGuid) != Junction
				|| Source->FindPhysicalPillarForNode(Junction->NodeGuid).IsValid()
				|| !Model.Nodes.ContainsByPredicate([&](const auto& N){return N.NodeGuid == Junction->NodeGuid && N.GeometryRevision == Junction->SourceGeometryRevision;}))
				return Fail(TEXT("CustomComponentCopyPolicyRequired"));
		}
	const auto OutlinePolicy = FEHBCopyOutlinePolicy::Prepare(Source, Model, Elements, false, true);
	if (!OutlinePolicy.bSucceeded) return Fail(OutlinePolicy.Status);
	auto Draft = FEHBWallNodeCopy::BuildDraft(Source->BuildingGuid, Model, Ids, OutlinePolicy.RelationsWithoutRoomMetadata);
	if (!Draft.bSucceeded) return Fail(Draft.Status);
	if (!OutlinePolicy.RestoreMappedMetadata(Draft)) return Fail(TEXT("UnmappedFinishRoom"));
	auto Baseline = Source->TopologyMigrationBaseline;
	auto Prepared = Source->PreparedWallNodeDefinitions;
	if (!MapSnapshots(Draft, Baseline, Prepared, bOptional)) return Fail(TEXT("HistoricalIdentityCopyPolicyRequired"));

	TArray<AActor*> Copies;
	AEHBBuildingActorBase* Copy = nullptr;
	TMap<FGuid, AEHBElementActorBase*> ImportedByOldId;
	bool bValid = true;
	auto Verify = [&](bool bCondition, FName Reason)
	{
		if (!bCondition && Result.FailureReason.IsNone()) Result.FailureReason = Reason;
		bValid &= bCondition;
	};
	{
		FScopedTransaction Transaction(NSLOCTEXT("EasyHouseBuilder", "CopyWallNodeBuilding", "Copy Whole Wall And Pillar Building"));
		{
			const FEHBActorImportScope ImportScope;
			// UE's exporter detaches/reattaches source actors in world space. Restore the
			// exact stored relative properties, preserving authored outline signatures.
			struct FSourcePose { USceneComponent* Root; FVector Location; FRotator Rotation; FVector Scale; };
			TArray<FSourcePose> SourcePoses;
			for (auto* Actor : Actors) if (auto* Root = Actor->GetRootComponent())
				SourcePoses.Add({Root, Root->GetRelativeLocation(), Root->GetRelativeRotation(), Root->GetRelativeScale3D()});
			GUnrealEd->DuplicateActors(Actors, Copies, Source->GetLevel(), WorldOffset);
			for (const auto& Pose : SourcePoses) if (IsValid(Pose.Root))
			{
				Pose.Root->SetRelativeLocation_Direct(Pose.Location);
				Pose.Root->SetRelativeRotationExact(Pose.Rotation);
				Pose.Root->SetRelativeScale3D_Direct(Pose.Scale);
				Pose.Root->UpdateComponentToWorld();
			}

		}
		if (Copies.IsEmpty()) { Transaction.Cancel(); return Fail(TEXT("NoActorsImported")); }
		bValid = Copies.Num() == Actors.Num();
		for (AActor* Actor : Copies)
		{
			if (auto* Building = Cast<AEHBBuildingActorBase>(Actor))
			{
				bValid &= !Copy && Building->BuildingGuid == Source->BuildingGuid;
				Copy = Building;
			}
			else if (auto* Element = Cast<AEHBElementActorBase>(Actor))
			{
				bValid &= Draft.ElementGuids.Contains(Element->ElementGuid) && !ImportedByOldId.Contains(Element->ElementGuid);
				ImportedByOldId.Add(Element->ElementGuid, Element);
			}
			else bValid = false;
		}
		bValid &= Copy && Copy != Source && ImportedByOldId.Num() == Ids.Num();
		for (const auto& Pair : ImportedByOldId)
			bValid &= Pair.Value->OwningBuilding == Copy && Pair.Value->GetAttachParentActor() == Copy;
		Verify(bValid, TEXT("ImportedGroupMismatch"));
#if WITH_DEV_AUTOMATION_TESTS
		if (FailAfterImport) { FailAfterImport = false; bValid = false; }
#endif
		if (bValid)
		{
			// The creation transaction already recorded imported actors. Modify before
			// publication makes subsequent undo/redo retain the final mapped identities.
			for (AActor* Actor : Copies) { Actor->SetFlags(RF_Transactional); Actor->Modify(); }
			Copy->BuildingGuid = Draft.BuildingGuid;
			Copy->LastCommittedEdit = FEHBCommittedEdit();
			Copy->WallNodeOwnership.Bindings = Draft.Model.PillarBindings;
			if(Copy->HasWallNodeAuthority())Copy->WallNodeAuthority.Nodes=Draft.Model.Nodes;
			Copy->ElementRelations = Draft.Relations;
			Copy->TopologyMigrationBaseline = Baseline;
			Copy->PreparedWallNodeDefinitions = Prepared;
			for (const auto& Pair : ImportedByOldId)
			{
				Pair.Value->ElementGuid = Draft.ElementGuids.FindChecked(Pair.Key);
				Pair.Value->RefreshLogicalSurfaceIdentities();
				if (auto* Wall = Cast<AEHB_Wall>(Pair.Value))
				{
					Verify(CopyWallOpeningSources(Cast<AEHB_Wall>(Source->FindElementActorByGuid(Pair.Key)), Wall), TEXT("CopiedWallOpeningHostMappingFailed"));
					bValid &= MapPhysicalId(Wall->StartPillarGuid, Draft.ElementGuids, bOptional);
					bValid &= MapPhysicalId(Wall->EndPillarGuid, Draft.ElementGuids, bOptional);
				}
			}
			Verify(bValid, TEXT("PhysicalEndpointMappingFailed"));
			Verify(FEHBCopyOutlinePolicy::ApplyActorReferences(Draft, ImportedByOldId), TEXT("RoomReferenceMappingFailed"));
			Copy->RebuildElementAndRelationshipIndexes();
			Copy->RebuildClosedLoops();
			for (const auto& Pair : ImportedByOldId)
				if (auto* Pillar = Cast<AEHB_Pillar>(Pair.Value)) Pillar->RebuildPillarMesh();
			TMap<FGuid, TArray<FEHBCutOperation>> StagedWallOpenings;
			for (const auto& Pair : ImportedByOldId)
				if (auto* Wall = Cast<AEHB_Wall>(Pair.Value)) StagedWallOpenings.Add(Pair.Key, MoveTemp(Wall->CutOperations));
			// Junctions use the uncut structural node footprint. Restore all mapped
			// opening sources below before final geometry and source verification.
			if (bOptional) Verify(Copy->RebuildWallNodeAuthorityGeometry(), TEXT("CopiedJunctionGeometryFailed"));
			for (const auto& Pair : ImportedByOldId)
				if (auto* Wall = Cast<AEHB_Wall>(Pair.Value))
				{
					// Only the newly imported actor is staged. Ordinary node edits still
					// reject openings until their dependency changes are separately planned.
					Verify(Wall->RefreshFromNodeModel(Draft.Model), TEXT("CopiedWallGeometryFailed"));
					Wall->CutOperations = MoveTemp(StagedWallOpenings.FindChecked(Pair.Key));
					if (OpeningWalls.Contains(Pair.Key))
					{
						FEHBPreparedWallOpening Candidate; FName Status;
						const bool bPreparedOpening = Wall->PrepareSurfaceOpening(Wall->CutOperations, Candidate, Status);
						Verify(bPreparedOpening, Status);
						if (bPreparedOpening)
						{
							Wall->RebuildWallMesh();
							Verify(MatchesOpeningMesh(Wall->LeftWallMeshComponent, Candidate.Left)
								&& MatchesOpeningMesh(Wall->RightWallMeshComponent, Candidate.Right)
								&& MatchesOpeningMesh(Wall->CapMeshComponent, Candidate.Caps), TEXT("CopiedWallOpeningGeometryMismatch"));
						}
					}
				}
			for (const auto& Pair : ImportedByOldId)
			{
				if (auto* Floor = Cast<AEHB_Floor>(Pair.Value))
				{
					bValid &= Floor->RebuildFloorMesh(); Floor->UpdateCollisionSettings();
					Floor->RecordOutlineSource(Floor->OutlineSource);
				}
				else if (auto* Slab = Cast<AEHB_FloorSlab>(Pair.Value))
				{
					Verify(CopySlabSources(Cast<AEHB_FloorSlab>(Source->FindElementActorByGuid(Pair.Key)), Slab), TEXT("CopiedOpeningHostMappingFailed"));
					bValid &= Slab->RebuildSlabMesh();
					Slab->RecordOutlineSource(Slab->OutlineSource);
				}
			}
			Verify(bValid, TEXT("CopiedFinishGeometryFailed"));
			const auto FinishPolicy = FEHBCopyOutlinePolicy::Prepare(Copy, Draft.Model, Copy->QueryElements(FEHBElementQuery()), false, true);
			Verify(FinishPolicy.bSucceeded, FinishPolicy.Status);
			Verify(UEHBWallTopologyLibrary::CaptureWallTopology(Copy).Issues.IsEmpty(), TEXT("CopiedTopologyInvalid"));
			if (bOptional)
			{
				FEHBWallNodeModel Actual;
				TSet<FGuid> MappedOpeningWalls;
				for (FGuid Id : OpeningWalls) MappedOpeningWalls.Add(Draft.ElementGuids.FindChecked(Id));
				Verify(Copy->WallNodeAuthority.Version == 2, TEXT("CopiedAuthorityModeChanged"));
				const auto ActualCapture=UEHBWallTopologyLibrary::CaptureWallNodeModelSource(Copy, Actual, &MappedOpeningWalls);
				Verify(ActualCapture.bSucceeded, ActualCapture.Status);
				// Capture sorts by the new GUIDs; remapping preserves source array order.
				// Compare identity-keyed records, keeping every value exact and leaving
				// authored polygon/control-point ordering untouched.
				auto Canonicalize = [](FEHBWallNodeModel& Value)
				{
					Value.Nodes.Sort([](const auto& A,const auto& B){return A.NodeGuid<B.NodeGuid;});
					Value.Walls.Sort([](const auto& A,const auto& B){return A.WallGuid<B.WallGuid;});
					Value.PillarBindings.Sort([](const auto& A,const auto& B){return A.NodeGuid<B.NodeGuid;});
				};
				FEHBWallNodeModel Expected=Draft.Model;Canonicalize(Expected);Canonicalize(Actual);
				FString ExpectedJson, ActualJson;
				FJsonObjectConverter::UStructToJsonObjectString(Expected, ExpectedJson);
				FJsonObjectConverter::UStructToJsonObjectString(Actual, ActualJson);
				Verify(ExpectedJson == ActualJson, TEXT("CopiedNodeModelMismatch"));
			}
			TArray<FEHBNodeRoomBoundary> Rooms;
			FName RoomStatus;
			const bool bRoomsReady=FEHBWallNodeRooms::Build(Copy->BuildingGuid, Draft.Model, Rooms, RoomStatus);
			Verify(bRoomsReady, RoomStatus);
			for (const auto& Room : Rooms)
				Verify(Copy->GetClosedLoopsByFloor(Room.FloorIndex).ContainsByPredicate([&](const auto& Loop) { return Loop.LoopGuid == Room.RoomGuid; }), TEXT("CopiedRoomIdentityMismatch"));
#if WITH_DEV_AUTOMATION_TESTS
			if (FailAfterApply) { FailAfterApply = false; bValid = false; }
#endif
		}
	}
	if (!bValid)
	{
		if(!Result.FailureReason.IsNone())UE_LOG(LogTemp, Warning, TEXT("EHB building copy rejected during apply: %s"), *Result.FailureReason.ToString());
		// End our transaction first; a failed paste must not leave a redoable partial copy.
		const bool bRestored = GEditor->UndoTransaction(false);
		if (!bRestored) { Result.Building = Copy; Result.Actors = Copies; }
		return Fail(bRestored ? TEXT("BuildingCopyFailedRolledBack") : TEXT("BuildingCopyRollbackFailed"));
	}
	Result.bSucceeded = true;
	Result.Status = TEXT("Copied");
	Result.Building = Copy;
	Result.Actors = MoveTemp(Copies);
	Result.IdentityDraft = MoveTemp(Draft);
	return Result;
}
