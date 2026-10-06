#pragma once

#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Wall.h"
#include "Core/EHBBuildingActorBase.h"

namespace EHBRailingCreationSnap
{
	// A wall hit selects an existing endpoint column, never a replacement post.
	// Compare in world centimeters and choose the nearer end on short walls.
	inline AEHB_Pillar* FindWallEndpointPillar(
		AEHB_Wall* Wall, AEHBBuildingActorBase* Building,
		const FVector& HitWorldLocation, float SnapDistance)
	{
		if (!Wall || !Building || Wall->OwningBuilding != Building
			|| Wall->IsActorBeingDestroyed() || SnapDistance < 0) return nullptr;
		const float Length = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
		if (Length <= UE_SMALL_NUMBER) return nullptr;
		const float Distance = FMath::Clamp(Wall->CalculateDistanceFromStartForWorldLocation(HitWorldLocation), 0.0f, Length);
		const FVector AxisHit = Wall->GetWorldLocationOnCenterAxisAtDistance(Distance, 0);
		AEHB_Pillar* Best = nullptr;
		double BestDistanceSquared = FMath::Square(static_cast<double>(SnapDistance));
		const FGuid EndpointGuids[] = { Wall->StartPillarGuid, Wall->EndPillarGuid };
		const float EndpointDistances[] = { 0.0f, Length };
		for (int32 Index = 0; Index < 2; ++Index)
		{
			AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Building->FindElementActorByGuid(EndpointGuids[Index]));
			if (!Pillar || Pillar->OwningBuilding != Building || Pillar->IsActorBeingDestroyed()) continue;
			const double DistanceSquared = FVector::DistSquared2D(
				AxisHit, Wall->GetWorldLocationOnCenterAxisAtDistance(EndpointDistances[Index], 0));
			if (DistanceSquared <= BestDistanceSquared && (!Best || DistanceSquared < BestDistanceSquared))
			{
				Best = Pillar;
				BestDistanceSquared = DistanceSquared;
			}
		}
		return Best;
	}
}
