#pragma once

#include "Actors/EHB_Wall.h"
#include "Core/EHBBuildingActorBase.h"
#include "Toolsets/EHBBuildingToolset.h"
#include "EHBDragAngleSnap.h"

// Value snapshot: a deleted source must remain an invalid wall request, never
// fall through to ordinary free railing creation on mouse release.
struct FEHBRailingWallDrag
{
	FGuid WallGuid;
	int32 Revision = 0;
	FVector Start = FVector::ZeroVector, End = FVector::ZeroVector;
	float Height = 0, Thickness = 0, Distance = 0;
	FTransform BuildingTransform;
	FVector WorldStart = FVector::ZeroVector;

	bool IsSet() const { return WallGuid.IsValid(); }
	void Capture(const AEHB_Wall* Wall, const FVector& Hit)
	{
		*this = {};
		if (!Wall || !Wall->OwningBuilding || Wall->IsActorBeingDestroyed()) return;
		WallGuid = Wall->ElementGuid;
		Revision = Wall->OwningBuilding->RelationshipGraphRevision;
		Start = Wall->LocalStart; End = Wall->LocalEnd;
		Height = Wall->Height; Thickness = Wall->Thickness;
		BuildingTransform = Wall->OwningBuilding->GetActorTransform();
		Distance = Wall->CalculateDistanceFromStartForWorldLocation(Hit);
		WorldStart = Wall->GetWorldLocationOnCenterAxisAtDistance(Distance, 0);
	}
	FVector ProjectEnd(const FVector& Mouse) const
	{
		return EHBDragAngleSnap::Snap(WorldStart, FVector(Mouse.X, Mouse.Y, WorldStart.Z));
	}
	FEHBToolsetOperationResult Execute(AEHBBuildingActorBase* Building, FVector WorldEnd,
		float RailHeight, float RailThickness, float Spacing, bool bPreviewOnly) const
	{
		if (!IsSet() || !Building || !Building->GetActorTransform().Equals(BuildingTransform, 0.001))
		{
			FEHBToolsetOperationResult Result; Result.Message = TEXT("StaleSource"); return Result;
		}
		return UEHBBuildingToolset::CommitWallSplitAndRailing(Building, WallGuid, Distance, Revision,
			Start, End, Height, Thickness, WorldEnd, RailHeight, RailThickness, Spacing, bPreviewOnly);
	}
};
