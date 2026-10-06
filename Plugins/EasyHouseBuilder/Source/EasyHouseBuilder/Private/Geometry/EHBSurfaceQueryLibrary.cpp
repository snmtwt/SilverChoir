// Copyright Epic Games, Inc. All Rights Reserved.

#include "Geometry/EHBSurfaceQueryLibrary.h"

#include "Actors/EHBElementActorBase.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "Engine/HitResult.h"

namespace
{
	bool IsLikelyVerticalSideNormal(const FVector& WorldNormal)
	{
		const FVector SafeNormal = WorldNormal.GetSafeNormal();
		return SafeNormal.IsNearlyZero()
			|| FMath::Abs(FVector::DotProduct(SafeNormal, FVector::UpVector)) < 0.65f;
	}

	bool IsLikelyTopNormal(const FVector& WorldNormal)
	{
		const FVector SafeNormal = WorldNormal.GetSafeNormal();
		return !SafeNormal.IsNearlyZero()
			&& FVector::DotProduct(SafeNormal, FVector::UpVector) > 0.65f;
	}

	UEHBArchitecturalSurfaceComponent* ResolveSurfaceFromHit(const FHitResult& HitResult)
	{
		if (UPrimitiveComponent* Component = HitResult.GetComponent())
		{
			return Cast<UEHBArchitecturalSurfaceComponent>(Component);
		}
		return nullptr;
	}

	AEHBElementActorBase* ResolveElementFromHit(const FHitResult& HitResult)
	{
		if (AEHBElementActorBase* Element = Cast<AEHBElementActorBase>(HitResult.GetActor()))
		{
			return Element;
		}
		if (UPrimitiveComponent* Component = HitResult.GetComponent())
		{
			return Cast<AEHBElementActorBase>(Component->GetOwner());
		}
		return nullptr;
	}
}

bool FEHBSurfaceQueryLibrary::ResolveHitSurface(const FHitResult& HitResult, FEHBSurfaceHit& OutSurfaceHit)
{
	OutSurfaceHit = FEHBSurfaceHit();
	if (!HitResult.bBlockingHit)
	{
		return false;
	}

	UEHBArchitecturalSurfaceComponent* Surface = ResolveSurfaceFromHit(HitResult);
	AEHBElementActorBase* Element = nullptr;
	if (Surface)
	{
		Element = Surface->OwnerElement;
		if (!Element)
		{
			Element = Cast<AEHBElementActorBase>(Surface->GetOwner());
		}
	}
	if (!Element)
	{
		Element = ResolveElementFromHit(HitResult);
	}

	if (!Element && !Surface)
	{
		return false;
	}

	OutSurfaceHit.Element = Element;
	OutSurfaceHit.Surface = Surface;
	OutSurfaceHit.Endpoint = Surface
		? Surface->MakeRelationEndpoint()
		: FEHBElementRelationEndpoint::MakeElement(Element->ElementGuid);
	OutSurfaceHit.WorldPoint = HitResult.ImpactPoint;
	OutSurfaceHit.WorldNormal = HitResult.ImpactNormal;
	OutSurfaceHit.bLikelyTop = IsLikelyTopNormal(HitResult.ImpactNormal);
	OutSurfaceHit.bLikelyVerticalSide = IsLikelyVerticalSideNormal(HitResult.ImpactNormal);
	return true;
}

void FEHBSurfaceQueryLibrary::QuerySurfacesInBox(
	const FEHBSurfaceQueryParams& Params,
	TArray<UEHBArchitecturalSurfaceComponent*>& OutSurfaces)
{
	OutSurfaces.Reset();
	if (!Params.Building)
	{
		return;
	}

	const FBox QueryBox(
		Params.WorldCenter - Params.WorldExtent.GetAbs(),
		Params.WorldCenter + Params.WorldExtent.GetAbs());

	FEHBElementQuery ElementQuery;
	const TArray<AEHBElementActorBase*> Elements = Params.Building->QueryElements(ElementQuery);
	for (AEHBElementActorBase* Element : Elements)
	{
		if (!Element || Element == Params.IgnoredActor.Get() || Element->IsActorBeingDestroyed())
		{
			continue;
		}

		TArray<UEHBArchitecturalSurfaceComponent*> SurfaceComponents;
		Element->GetComponents(SurfaceComponents);
		for (UEHBArchitecturalSurfaceComponent* Surface : SurfaceComponents)
		{
			if (!Surface)
			{
				continue;
			}

			if (!Params.RequiredRoles.IsEmpty() && !Params.RequiredRoles.Contains(Surface->SurfaceRole))
			{
				continue;
			}
			if (!Params.RequiredSurfaceKinds.IsEmpty() && !Params.RequiredSurfaceKinds.Contains(Surface->GetElementSurfaceKind()))
			{
				continue;
			}
			if (!Surface->Bounds.GetBox().Intersect(QueryBox))
			{
				continue;
			}

			OutSurfaces.Add(Surface);
		}
	}
}

bool FEHBSurfaceQueryLibrary::FindBestSnap(const FEHBSnapQuery& Query, FEHBSnapResult& OutResult)
{
	OutResult = FEHBSnapResult();
	if (!Query.Building || Query.MaxDistance <= 0.0f)
	{
		return false;
	}

	FEHBSurfaceQueryParams SurfaceQuery;
	SurfaceQuery.Building = Query.Building;
	SurfaceQuery.WorldCenter = Query.WorldReferencePoint;
	SurfaceQuery.WorldExtent = FVector(Query.MaxDistance);
	SurfaceQuery.IgnoredActor = Query.IgnoredActor;

	TArray<UEHBArchitecturalSurfaceComponent*> Surfaces;
	QuerySurfacesInBox(SurfaceQuery, Surfaces);
	if (Surfaces.IsEmpty())
	{
		return false;
	}

	float BestScore = TNumericLimits<float>::Max();
	FEHBSurfaceSnapCandidate BestCandidate;
	for (UEHBArchitecturalSurfaceComponent* Surface : Surfaces)
	{
		if (!Surface)
		{
			continue;
		}

		TArray<FEHBSurfaceSnapCandidate> Candidates;
		Surface->BuildSnapCandidates(Query.WorldReferencePoint, Candidates);
		for (const FEHBSurfaceSnapCandidate& Candidate : Candidates)
		{
			if (Candidate.Distance > Query.MaxDistance)
			{
				continue;
			}

			float Score = Candidate.Distance;
			if (!Query.WorldReferenceNormal.IsNearlyZero())
			{
				const float NormalAlignment = FVector::DotProduct(
					Query.WorldReferenceNormal.GetSafeNormal(),
					Candidate.WorldNormal.GetSafeNormal());
				Score += (1.0f - FMath::Abs(NormalAlignment)) * Query.MaxDistance;
			}

			if (Score < BestScore)
			{
				BestScore = Score;
				BestCandidate = Candidate;
			}
		}
	}

	if (BestScore == TNumericLimits<float>::Max())
	{
		return false;
	}

	OutResult.bSnapped = true;
	OutResult.TargetWorldTransform = Query.SourceWorldTransform;
	OutResult.TargetWorldTransform.SetLocation(BestCandidate.WorldLocation);
	OutResult.WorldPoint = BestCandidate.WorldLocation;
	OutResult.WorldTangent = BestCandidate.WorldTangent;
	OutResult.WorldNormal = BestCandidate.WorldNormal;
	OutResult.TargetEndpoint = BestCandidate.Endpoint.ToRelationEndpoint();
	OutResult.Score = BestScore;
	return true;
}

bool FEHBSurfaceQueryLibrary::ResolveYawSnap(
	float DesiredYaw,
	const TArray<FEHBSnapAxisFeature>& AxisFeatures,
	float AngleThreshold,
	float& OutSnappedYaw,
	FEHBSnapAxisFeature& OutFeature)
{
	const float SafeAngleThreshold = FMath::Max(0.0f, AngleThreshold);
	float BestDelta = TNumericLimits<float>::Max();
	float BestYaw = DesiredYaw;
	FEHBSnapAxisFeature BestFeature;

	for (const FEHBSnapAxisFeature& Feature : AxisFeatures)
	{
		const TArray<FVector> CandidateDirections = {
			Feature.WorldForward,
			-Feature.WorldForward,
			Feature.WorldRight,
			-Feature.WorldRight
		};

		for (const FVector& Direction : CandidateDirections)
		{
			const FVector HorizontalDirection(Direction.X, Direction.Y, 0.0f);
			if (HorizontalDirection.IsNearlyZero())
			{
				continue;
			}

			const float CandidateYaw = HorizontalDirection.Rotation().Yaw;
			const float Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(DesiredYaw, CandidateYaw));
			if (Delta < BestDelta)
			{
				BestDelta = Delta;
				BestYaw = CandidateYaw;
				BestFeature = Feature;
			}
		}
	}

	if (BestDelta > SafeAngleThreshold)
	{
		return false;
	}

	OutSnappedYaw = BestYaw;
	OutFeature = BestFeature;
	return true;
}
