#pragma once

#include "Actors/EHB_Railing.h"
#include "Actors/EHB_Pillar.h"
#include "Core/EHBBuildingActorBase.h"

// Only a native linear branch anchored to one retained building column is
// preserved here. Gates, sampled geometry and references to posts need plans.
struct FEHBPreservedRailing
{
	AEHB_Railing* Actor = nullptr;
	FTransform Transform;
	FVector Start, End;
	bool bHasStartWallCut = false;
	FVector CutPoint, CutNormal;
	FGuid StartPost, EndPost;
	TArray<FEHBRailingPost> Posts;
	FEHBElementRelation Relation;

	static bool Supports(const AEHB_Railing* R)
	{
		if (!R || R->GetClass() != AEHB_Railing::StaticClass() || !R->OwningBuilding
			|| R->PathMode != EEHBRailingPathMode::Linear || R->HostedStair || R->HostedStairGuid.IsValid()
			|| !R->GateConnections.IsEmpty() || !R->PostMeshOverrides.IsEmpty()
			|| R->SampledRailingRow.DataTable || !R->SampledRailingRow.RowName.IsNone()
			|| R->PostMesh != GetDefault<AEHB_Railing>()->PostMesh || R->RailMesh != GetDefault<AEHB_Railing>()->RailMesh
			|| R->FillMode != EEHBRailingFillMode::PostsAndRails || !R->bOmitStartPost || R->bOmitEndPost
			|| R->StartAnchor.PostGuid.IsValid() || R->EndAnchor.ElementGuid.IsValid() || R->EndAnchor.PostGuid.IsValid()
			|| R->RailingRelationGuids.Num() != 1 || R->GeneratedPosts.IsEmpty()
			|| R->LinearStart.ContainsNaN() || R->LinearEnd.ContainsNaN()
			|| !FMath::IsNearlyEqual(R->LinearStart.Z, R->LinearEnd.Z)
			|| !FMath::IsFinite(R->PostWidth) || !FMath::IsFinite(R->RailThickness) || R->PostWidth <= 0 || R->RailThickness <= 0)
			return false;
		if (R->bHasStartWallCut && (R->StartWallCutPoint.ContainsNaN() || R->StartWallCutNormal.ContainsNaN()
			|| !R->StartWallCutNormal.IsNormalized() || FVector::DotProduct(R->StartWallCutNormal, (R->LinearEnd-R->LinearStart).GetSafeNormal2D()) <= 0.001)) return false;
		const auto* P = Cast<AEHB_Pillar>(R->OwningBuilding->FindElementActorByGuid(R->StartAnchor.ElementGuid));
		return P && !P->IsActorBeingDestroyed() && P->OwningBuilding == R->OwningBuilding && P->FloorIndex == R->FloorIndex
			&& R->StartAnchor.LocalPoint.Equals(R->LinearStart, 0.001)
			&& R->GetActorTransform().TransformPosition(R->LinearStart).Equals(P->GetActorLocation(), 0.001);
	}
	static bool MatchesRelation(const AEHB_Railing* R, const FEHBElementRelation& Link)
	{
		return Link.bEnabled && Link.RelationGuid.IsValid() && R->RailingRelationGuids.Contains(Link.RelationGuid)
			&& Link.Type == EEHBElementRelationType::BoundaryAttachment && Link.Origin == EEHBRelationOrigin::UserAuthored
			&& Link.Source.RefersToElement(R->StartAnchor.ElementGuid) && Link.Target.RefersToElement(R->ElementGuid)
			&& Link.Source.SurfaceKind == EEHBElementSurfaceKind::Custom && Link.Target.SurfaceKind == EEHBElementSurfaceKind::Start
			&& Link.bGeometryDependent && !Link.bAffectsFloorAssignment;
	}
	void Capture(AEHB_Railing* R, const FEHBElementRelation& Link)
	{
		Actor = R; Transform = R->GetActorTransform(); Start = R->LinearStart; End = R->LinearEnd;
		bHasStartWallCut = R->bHasStartWallCut; CutPoint = R->StartWallCutPoint; CutNormal = R->StartWallCutNormal;
		StartPost = R->LinearStartPostGuid; EndPost = R->LinearEndPostGuid; Posts = R->GeneratedPosts; Relation = Link;
	}
	bool IsPreserved(bool bAllowRefreshedRevision = false) const
	{
		if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || !Actor->GetActorTransform().Equals(Transform, 0.001)
			|| !Actor->LinearStart.Equals(Start) || !Actor->LinearEnd.Equals(End)
			|| Actor->bHasStartWallCut != bHasStartWallCut || !Actor->StartWallCutPoint.Equals(CutPoint) || !Actor->StartWallCutNormal.Equals(CutNormal)
			|| Actor->LinearStartPostGuid != StartPost || Actor->LinearEndPostGuid != EndPost
			|| Actor->GeneratedPosts.Num() != Posts.Num() || !Supports(Actor)) return false;
		for (int32 I = 0; I < Posts.Num(); ++I)
			if (!FEHBRailingPost::StaticStruct()->CompareScriptStruct(&Posts[I], &Actor->GeneratedPosts[I], 0)) return false;
		const auto* Link = Actor->OwningBuilding->ElementRelations.FindByPredicate([&](const auto& R) { return R.RelationGuid == Relation.RelationGuid; });
		if (!Link) return false;
		FEHBElementRelation Expected = Relation;
		if (bAllowRefreshedRevision && (Link->SourceGeometryRevision != Relation.SourceGeometryRevision
			|| Link->TargetGeometryRevision != Relation.TargetGeometryRevision))
		{
			// Undo reconstructs the building's transient geometry-revision cache.
			// Refreshed validation counters must match that cache; identity and all
			// semantic fields are still compared exactly below.
			if (Link->SourceGeometryRevision != Actor->OwningBuilding->GetElementGeometryRevision(Link->Source.ElementGuid)
				|| Link->TargetGeometryRevision != Actor->OwningBuilding->GetElementGeometryRevision(Link->Target.ElementGuid)) return false;
			Expected.SourceGeometryRevision = Link->SourceGeometryRevision;
			Expected.TargetGeometryRevision = Link->TargetGeometryRevision;
		}
		return FEHBElementRelation::StaticStruct()->CompareScriptStruct(&Expected, Link, 0);
	}
};
