// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/EHBPlanarSurfaceComponent.h"
#include "Actors/EHB_FloorSlab.h"

#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Settings/EHBBuildingToolsetSettings.h"

namespace
{
	UMaterialInterface* ResolveConfiguredDefaultWhiteBoxMaterial()
	{
		if (const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>())
		{
			if (UMaterialInterface* Material = Settings->DefaultWhiteBoxMaterial.LoadSynchronous())
			{
				return Material;
			}
		}
		return UMaterial::GetDefaultMaterial(MD_Surface);
	}

	FVector GetSafeHorizontalVector(const FVector& Vector)
	{
		return FVector(Vector.X, Vector.Y, 0.0f).GetSafeNormal();
	}
}

UEHBPlanarSurfaceComponent::UEHBPlanarSurfaceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SurfaceRole = EEHBArchitecturalSurfaceRole::SlabTop;
	SurfaceName = TEXT("Planar.Surface");
	ComponentTags.AddUnique(TEXT("EHB_PlanarSurface"));
}

void UEHBPlanarSurfaceComponent::SetPlanarSurfaceData(
	const TArray<FVector>& InBoundaryLoop,
	const TArray<TArray<FVector>>& InHoleLoops,
	const FVector& InLocalPlaneNormal,
	float InUVWorldSize)
{
	LocalRegions.Reset();
	LocalBoundaryLoop = InBoundaryLoop;
	LocalHoleLoops.Reset();
	LocalHoleLoops.Reserve(InHoleLoops.Num());
	for (const TArray<FVector>& HoleLoop : InHoleLoops)
	{
		if (HoleLoop.Num() >= 3)
		{
			FEHBSurfaceHoleLoop& NewHoleLoop = LocalHoleLoops.AddDefaulted_GetRef();
			NewHoleLoop.LocalLoop = HoleLoop;
		}
	}
	LocalPlaneNormal = InLocalPlaneNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	UVWorldSize = FMath::Max(1.0f, InUVWorldSize);
}

TArray<FEHBPlanarSurfaceRegion> UEHBPlanarSurfaceComponent::GetPlanarSurfaceRegions() const
{
 if(!LocalRegions.IsEmpty())return LocalRegions;
 if(LocalBoundaryLoop.Num()<3)return {};
 FEHBPlanarSurfaceRegion R;R.BoundaryLoop=LocalBoundaryLoop;R.HoleLoops=LocalHoleLoops;return {R};
}
void UEHBPlanarSurfaceComponent::SetPlanarSurfaceRegions(const TArray<FEHBPlanarSurfaceRegion>& Regions,const FVector& Normal,float InUVWorldSize)
{
 TArray<TArray<FVector>> Holes;if(!Regions.IsEmpty())for(const auto& H:Regions[0].HoleLoops)Holes.Add(H.LocalLoop);
 SetPlanarSurfaceData(Regions.IsEmpty()?TArray<FVector>():Regions[0].BoundaryLoop,Holes,Normal,InUVWorldSize);
 if(Regions.Num()>1)LocalRegions=Regions;
}
bool UEHBPlanarSurfaceComponent::RebuildSurfaceMesh()
{
 // Slabs own their extrusion, collision and logical/display separation.
 if(auto* Slab=Cast<AEHB_FloorSlab>(GetOwner()))return Slab->RebuildSlabMesh();
 FEHBSurfaceMeshBuildResult Combined;
 for(const auto& R:GetPlanarSurfaceRegions())
 {
  FEHBPlanarSurfaceMeshBuildInput Input;Input.BoundaryLoop=R.BoundaryLoop;for(const auto& H:R.HoleLoops)Input.HoleLoops.Add(H.LocalLoop);
  Input.PlaneZ=R.BoundaryLoop.IsEmpty()?0:R.BoundaryLoop[0].Z;Input.PlaneNormal=LocalPlaneNormal;Input.UVWorldSize=UVWorldSize;
  FEHBSurfaceMeshBuildResult Part;if(!FEHBPlanarSurfaceGeometryBuilder::BuildPlanarSurface(Input,Part))return false;
  const int32 Base=Combined.Vertices.Num();Combined.Vertices.Append(Part.Vertices);Combined.Normals.Append(Part.Normals);Combined.UV0.Append(Part.UV0);for(int32 I:Part.Triangles)Combined.Triangles.Add(Base+I);
 }
 if(!Combined.IsValidMesh())return false;
 return SubmitMeshBuildResult(SurfaceName,Combined,ResolveConfiguredDefaultWhiteBoxMaterial(),bEnableSurfaceCollision);
}

bool UEHBPlanarSurfaceComponent::ProjectWorldPointToSurface(
	const FVector& WorldPoint,
	FVector& OutWorldPoint,
	FVector2D& OutSurfaceUV) const
{
	if (LocalBoundaryLoop.Num() < 3)
	{
		return false;
	}

	const FTransform WorldToLocal = GetComponentTransform().Inverse();
	const FVector LocalPoint = WorldToLocal.TransformPosition(WorldPoint);
	const FVector PlaneOrigin = LocalBoundaryLoop[0];
	const FVector Normal = LocalPlaneNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const float SignedDistance = FVector::DotProduct(LocalPoint - PlaneOrigin, Normal);
	const FVector ProjectedLocalPoint = LocalPoint - Normal * SignedDistance;

 if(!LocalRegions.IsEmpty())
 {
  const FVector2d P(ProjectedLocalPoint.X,ProjectedLocalPoint.Y);
  if(!LocalRegions.ContainsByPredicate([&](const auto& R){return FEHBSurfaceGeometryUtil::IsPointInsidePolygon2D(P,FEHBSurfaceGeometryUtil::To2DLoop(R.BoundaryLoop))&&!R.HoleLoops.ContainsByPredicate([&](const auto& H){return FEHBSurfaceGeometryUtil::IsPointInsidePolygon2D(P,FEHBSurfaceGeometryUtil::To2DLoop(H.LocalLoop));});}))return false;
 }
	OutWorldPoint = GetComponentTransform().TransformPosition(ProjectedLocalPoint);
	OutSurfaceUV = FVector2D(ProjectedLocalPoint.X / FMath::Max(1.0f, UVWorldSize), ProjectedLocalPoint.Y / FMath::Max(1.0f, UVWorldSize));
	return true;
}

bool UEHBPlanarSurfaceComponent::BuildSnapCandidates(
	const FVector& WorldPoint,
	TArray<FEHBSurfaceSnapCandidate>& OutCandidates) const
{
	if (!bCanBeSnapTarget)
	{
		return false;
	}

	const int32 InitialCount = OutCandidates.Num();
	FVector ProjectedWorldPoint;
	FVector2D SurfaceUV;
	if (ProjectWorldPointToSurface(WorldPoint, ProjectedWorldPoint, SurfaceUV))
	{
		FEHBSurfaceSnapCandidate& PlaneCandidate = OutCandidates.AddDefaulted_GetRef();
		PlaneCandidate.SnapKind = EEHBSurfaceSnapKind::Plane;
		PlaneCandidate.WorldLocation = ProjectedWorldPoint;
		PlaneCandidate.WorldNormal = GetComponentTransform().TransformVectorNoScale(LocalPlaneNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		PlaneCandidate.Distance = FVector::Distance(WorldPoint, ProjectedWorldPoint);
		PlaneCandidate.Endpoint = MakeSurfaceEndpoint();
	}

	FEHBSurfaceBoundaryHit BoundaryHit;
	if (FindClosestBoundaryPoint(WorldPoint, BoundaryHit))
	{
		FEHBSurfaceSnapCandidate& EdgeCandidate = OutCandidates.AddDefaulted_GetRef();
		EdgeCandidate.SnapKind = EEHBSurfaceSnapKind::Edge;
		EdgeCandidate.WorldLocation = BoundaryHit.WorldPoint;
		EdgeCandidate.WorldNormal = BoundaryHit.WorldNormal;
		EdgeCandidate.WorldTangent = BoundaryHit.WorldTangent;
		EdgeCandidate.Distance = BoundaryHit.Distance;
		EdgeCandidate.Endpoint = MakeSurfaceEndpoint();
	}

	return OutCandidates.Num() > InitialCount;
}

bool UEHBPlanarSurfaceComponent::BuildSideSnapEdges(TArray<FEHBSurfaceSideSnapEdge>& OutEdges) const
{
	if (!bCanBeSnapTarget)
	{
		return false;
	}

	const int32 InitialCount = OutEdges.Num();
	int32 RegionIndex=0,HoleOrdinal=0;
 for(const auto& Region:GetPlanarSurfaceRegions())
 {
 const int32 CurrentRegion=RegionIndex++;
 const TArray<FVector2d> Loop2D = FEHBSurfaceGeometryUtil::To2DLoop(Region.BoundaryLoop);
	if (Loop2D.Num() < 3)
	{
		continue;
	}

	const double SignedArea = FEHBSurfaceGeometryUtil::CalculateSignedArea2D(Loop2D);
	if (FMath::Abs(SignedArea) <= UE_DOUBLE_SMALL_NUMBER)
	{
		continue;
	}

	const float LocalZ = Region.BoundaryLoop[0].Z;
	const FTransform ComponentTransform = GetComponentTransform();
	const FEHBSurfaceEndpoint SurfaceEndpoint = MakeSurfaceEndpoint();

	auto AppendLoopEdges =
		[&](
			const TArray<FVector>& LocalLoop,
			bool bInnerLoop,
			int32 LoopSubIndex,
			const FEHBSurfaceEndpoint& BaseEndpoint)
	{
		const TArray<FVector2d> CurrentLoop2D = FEHBSurfaceGeometryUtil::To2DLoop(LocalLoop);
		if (CurrentLoop2D.Num() < 3)
		{
			return;
		}

		const double CurrentSignedArea = FEHBSurfaceGeometryUtil::CalculateSignedArea2D(CurrentLoop2D);
		if (FMath::Abs(CurrentSignedArea) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return;
		}

		const bool bLoopCounterClockwise = CurrentSignedArea > 0.0;
		for (int32 Index = 0; Index < LocalLoop.Num(); ++Index)
		{
			FVector LocalStart = LocalLoop[Index];
			FVector LocalEnd = LocalLoop[(Index + 1) % LocalLoop.Num()];
			LocalStart.Z = LocalZ;
			LocalEnd.Z = LocalZ;

			const FVector LocalDirection = GetSafeHorizontalVector(LocalEnd - LocalStart);
			if (LocalDirection.IsNearlyZero())
			{
				continue;
			}

			const FVector RightNormal(LocalDirection.Y, -LocalDirection.X, 0.0f);
			const FVector LeftNormal(-LocalDirection.Y, LocalDirection.X, 0.0f);
			const FVector LocalOutwardNormal = bLoopCounterClockwise
				? (bInnerLoop ? LeftNormal : RightNormal)
				: (bInnerLoop ? RightNormal : LeftNormal);
			const FVector WorldStart = ComponentTransform.TransformPosition(LocalStart);
			const FVector WorldEnd = ComponentTransform.TransformPosition(LocalEnd);
			const FVector WorldDirection = GetSafeHorizontalVector(WorldEnd - WorldStart);
			const FVector WorldOutwardNormal = GetSafeHorizontalVector(ComponentTransform.TransformVectorNoScale(LocalOutwardNormal));
			if (WorldDirection.IsNearlyZero() || WorldOutwardNormal.IsNearlyZero())
			{
				continue;
			}

			FEHBSurfaceSideSnapEdge& Edge = OutEdges.AddDefaulted_GetRef();
			Edge.WorldStart = WorldStart;
			Edge.WorldEnd = WorldEnd;
			Edge.WorldDirection = WorldDirection;
			Edge.WorldOutwardNormal = WorldOutwardNormal;
			Edge.WorldMidpoint = (WorldStart + WorldEnd) * 0.5f;
			Edge.MinWorldZ = FMath::Min(WorldStart.Z, WorldEnd.Z);
			Edge.MaxWorldZ = FMath::Max(WorldStart.Z, WorldEnd.Z);
			Edge.Endpoint = BaseEndpoint;
			Edge.Endpoint.SurfaceKind = bInnerLoop ? EEHBElementSurfaceKind::Opening : EEHBElementSurfaceKind::Side;
			Edge.Endpoint.SurfaceName = bInnerLoop ? TEXT("Slab.HoleSide") : TEXT("Slab.OuterSide");
			Edge.Endpoint.SubIndex = LoopSubIndex;
			Edge.SourceSurface = const_cast<UEHBPlanarSurfaceComponent*>(this);
		}
	};

	AppendLoopEdges(Region.BoundaryLoop, false, LocalRegions.IsEmpty()?INDEX_NONE:CurrentRegion, SurfaceEndpoint);

	for (int32 HoleLoopIndex = 0; HoleLoopIndex < Region.HoleLoops.Num(); ++HoleLoopIndex)
	{
		const FEHBSurfaceHoleLoop& HoleLoop = Region.HoleLoops[HoleLoopIndex];
		AppendLoopEdges(HoleLoop.LocalLoop, true, HoleOrdinal++, SurfaceEndpoint);
	}

 }
	return OutEdges.Num() > InitialCount;
}

bool UEHBPlanarSurfaceComponent::FindClosestBoundaryPoint(const FVector& WorldPoint,FEHBSurfaceBoundaryHit& OutBoundaryHit) const
{
 bool Found=false;double Best=TNumericLimits<double>::Max();
 for(const auto& R:GetPlanarSurfaceRegions())
 {
  if(R.BoundaryLoop.IsEmpty())continue;
  auto Try=[&](const TArray<FVector>& Loop){FEHBSurfaceBoundaryHit Hit;if(FEHBSurfaceGeometryUtil::FindClosestBoundaryPointXY(Loop,GetComponentTransform(),WorldPoint,R.BoundaryLoop[0].Z,true,Hit)&&Hit.Distance<Best){Found=true;Best=Hit.Distance;OutBoundaryHit=Hit;}};
  Try(R.BoundaryLoop);for(const auto& H:R.HoleLoops)Try(H.LocalLoop);
 }
 return Found;
}
