// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/EHB_FloorSlab.h"
#include "Core/EHBDisplayPartition.h"
#include "Core/EHBActorImportScope.h"
#include "Core/EHBOutlineSignature.h"
#include "Core/EHBSurfaceOpening.h"

#include "Arrangement2d.h"
#include "ConstrainedDelaunay2.h"
#include "Core/EHBBuildingActorBase.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Components/EHBArchitecturalSurfaceComponent.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Components/EHBPlanarSurfaceComponent.h"
#include "Cutting/EHBPolygonClipper.h"
#include "Geometry/EHBSurfaceGeometryTypes.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "EngineUtils.h"
#include "ThirdParty/clipper/clipper.h"

#include <algorithm>
#include "EHBSlabSurfaceOpening.inl"

namespace
{
	constexpr double EHBFloorSlabPointTolerance = 0.01;
	constexpr float EHBFloorSlabUVWorldSize = 100.0f;
	constexpr double EHBFloorSlabClipperScale = 1000.0;

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

	bool ArePointsNearlyEqual2D(const FVector2d& A, const FVector2d& B)
	{
		const double DX = A.X - B.X;
		const double DY = A.Y - B.Y;
		return DX * DX + DY * DY <= EHBFloorSlabPointTolerance * EHBFloorSlabPointTolerance;
	}

	ClipperLib::Path ToFloorSlabClipperPath(const TArray<FVector>& Polygon)
	{
		ClipperLib::Path Result;
		Result.reserve(Polygon.Num());
		for (const FVector& Point : Polygon)
		{
			Result.push_back(ClipperLib::IntPoint(
				static_cast<ClipperLib::cInt>(FMath::RoundToDouble(Point.X * EHBFloorSlabClipperScale)),
				static_cast<ClipperLib::cInt>(FMath::RoundToDouble(Point.Y * EHBFloorSlabClipperScale))));
		}
		return Result;
	}

	TArray<FVector> FromFloorSlabClipperPath(const ClipperLib::Path& Path, float Z)
	{
		TArray<FVector> Result;
		Result.Reserve(static_cast<int32>(Path.size()));
		for (const ClipperLib::IntPoint& Point : Path)
		{
			Result.Add(FVector(
				static_cast<double>(Point.X) / EHBFloorSlabClipperScale,
				static_cast<double>(Point.Y) / EHBFloorSlabClipperScale,
				Z));
		}
		return Result;
	}

	double GetAbsClipperArea(const ClipperLib::Path& Path)
	{
		return FMath::Abs(static_cast<double>(ClipperLib::Area(Path)));
	}

	bool OffsetFloorSlabPolygon(const TArray<FVector>& Polygon, float OffsetDistance, TArray<FVector>& OutPolygon)
	{
		OutPolygon.Reset();
		if (Polygon.Num() < 3 || OffsetDistance <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}

		ClipperLib::Path SourcePath = ToFloorSlabClipperPath(Polygon);
		if (SourcePath.size() < 3 || GetAbsClipperArea(SourcePath) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		if (!ClipperLib::Orientation(SourcePath))
		{
			std::reverse(SourcePath.begin(), SourcePath.end());
		}

		ClipperLib::ClipperOffset Offsetter;
		Offsetter.MiterLimit = 4.0;
		Offsetter.AddPath(SourcePath, ClipperLib::jtMiter, ClipperLib::etClosedPolygon);

		ClipperLib::Paths OffsetPaths;
		Offsetter.Execute(OffsetPaths, static_cast<double>(OffsetDistance) * EHBFloorSlabClipperScale);
		if (OffsetPaths.empty())
		{
			return false;
		}

		const ClipperLib::Path* BestPath = nullptr;
		double BestArea = 0.0;
		for (const ClipperLib::Path& CandidatePath : OffsetPaths)
		{
			if (CandidatePath.size() < 3)
			{
				continue;
			}

			const double CandidateArea = GetAbsClipperArea(CandidatePath);
			if (CandidateArea > BestArea)
			{
				BestArea = CandidateArea;
				BestPath = &CandidatePath;
			}
		}

		if (!BestPath)
		{
			return false;
		}

		OutPolygon = FromFloorSlabClipperPath(*BestPath, Polygon[0].Z);
		return OutPolygon.Num() >= 3;
	}

	TArray<FVector2d> To2DLoop(const TArray<FVector>& Loop)
	{
		TArray<FVector2d> Result;
		Result.Reserve(Loop.Num());
		for (const FVector& Point : Loop)
		{
			if (Result.IsEmpty() || !ArePointsNearlyEqual2D(Result.Last(), FVector2d(Point.X, Point.Y)))
			{
				Result.Add(FVector2d(Point.X, Point.Y));
			}
		}
		if (Result.Num() >= 2 && ArePointsNearlyEqual2D(Result[0], Result.Last()))
		{
			Result.Pop(EAllowShrinking::No);
		}
		return Result;
	}

	double CalculateTwiceArea(const TArray<FVector2d>& Loop)
	{
		double Area = 0.0;
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector2d& A = Loop[Index];
			const FVector2d& B = Loop[(Index + 1) % Loop.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		return Area;
	}

	bool IsPointInsidePolygon(const FVector2d& Point, const TArray<FVector2d>& Polygon)
	{
		bool bInside = false;
		for (int32 CurrentIndex = 0, PreviousIndex = Polygon.Num() - 1;
			CurrentIndex < Polygon.Num();
			PreviousIndex = CurrentIndex++)
		{
			const FVector2d& Current = Polygon[CurrentIndex];
			const FVector2d& Previous = Polygon[PreviousIndex];
			const bool bCrosses = (Current.Y > Point.Y) != (Previous.Y > Point.Y);
			if (!bCrosses)
			{
				continue;
			}

			const double IntersectionX =
				(Previous.X - Current.X) * (Point.Y - Current.Y) / (Previous.Y - Current.Y) + Current.X;
			if (Point.X < IntersectionX)
			{
				bInside = !bInside;
			}
		}
		return bInside;
	}

	double Cross2D(const FVector2d& A, const FVector2d& B, const FVector2d& C)
	{
		return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	}

	bool IsPointOnSegment2D(const FVector2d& Point, const FVector2d& A, const FVector2d& B)
	{
		constexpr double Tolerance = 0.01;
		if (FMath::Abs(Cross2D(A, B, Point)) > Tolerance)
		{
			return false;
		}

		return Point.X >= FMath::Min(A.X, B.X) - Tolerance
			&& Point.X <= FMath::Max(A.X, B.X) + Tolerance
			&& Point.Y >= FMath::Min(A.Y, B.Y) - Tolerance
			&& Point.Y <= FMath::Max(A.Y, B.Y) + Tolerance;
	}

	bool DoSegmentsIntersect2D(const FVector2d& A0, const FVector2d& A1, const FVector2d& B0, const FVector2d& B1)
	{
		const double CrossA0 = Cross2D(A0, A1, B0);
		const double CrossA1 = Cross2D(A0, A1, B1);
		const double CrossB0 = Cross2D(B0, B1, A0);
		const double CrossB1 = Cross2D(B0, B1, A1);

		if ((CrossA0 > 0.0 && CrossA1 < 0.0 || CrossA0 < 0.0 && CrossA1 > 0.0)
			&& (CrossB0 > 0.0 && CrossB1 < 0.0 || CrossB0 < 0.0 && CrossB1 > 0.0))
		{
			return true;
		}

		return IsPointOnSegment2D(B0, A0, A1)
			|| IsPointOnSegment2D(B1, A0, A1)
			|| IsPointOnSegment2D(A0, B0, B1)
			|| IsPointOnSegment2D(A1, B0, B1);
	}

	bool DoPolygonsOverlap2D(const TArray<FVector>& A, const TArray<FVector>& B)
	{
		const TArray<FVector2d> LoopA = To2DLoop(A);
		const TArray<FVector2d> LoopB = To2DLoop(B);
		if (LoopA.Num() < 3 || LoopB.Num() < 3)
		{
			return false;
		}

		for (int32 AIndex = 0; AIndex < LoopA.Num(); ++AIndex)
		{
			const FVector2d& A0 = LoopA[AIndex];
			const FVector2d& A1 = LoopA[(AIndex + 1) % LoopA.Num()];
			for (int32 BIndex = 0; BIndex < LoopB.Num(); ++BIndex)
			{
				if (DoSegmentsIntersect2D(A0, A1, LoopB[BIndex], LoopB[(BIndex + 1) % LoopB.Num()]))
				{
					return true;
				}
			}
		}

		return IsPointInsidePolygon(LoopA[0], LoopB)
			|| IsPointInsidePolygon(LoopB[0], LoopA);
	}

	float DotXY(const FVector& A, const FVector& B)
	{
		return A.X * B.X + A.Y * B.Y;
	}

	float CrossXY(const FVector& A, const FVector& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	FVector RotatePointAroundPivotXY(const FVector& Point, const FVector& Pivot, float YawRadians)
	{
		const float SinYaw = FMath::Sin(YawRadians);
		const float CosYaw = FMath::Cos(YawRadians);
		const FVector Offset = Point - Pivot;
		return FVector(
			Pivot.X + Offset.X * CosYaw - Offset.Y * SinYaw,
			Pivot.Y + Offset.X * SinYaw + Offset.Y * CosYaw,
			Point.Z);
	}

	float GetIntervalOverlap(float AMin, float AMax, float BMin, float BMax)
	{
		if (AMin > AMax)
		{
			Swap(AMin, AMax);
		}
		if (BMin > BMax)
		{
			Swap(BMin, BMax);
		}
		return FMath::Min(AMax, BMax) - FMath::Max(AMin, BMin);
	}

	struct FEHBFloorSlabWorldEdge
	{
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		FVector OutwardNormal = FVector::RightVector;
		FVector Midpoint = FVector::ZeroVector;
	};

	struct FEHBFloorSlabSnapCandidate
	{
		float Score = TNumericLimits<float>::Max();
		float YawDeltaRadians = 0.0f;
		FVector WorldDelta = FVector::ZeroVector;
		bool bAlignsCorner = false;
	};

	bool BuildFloorSlabWorldEdges(const TArray<FVector>& WorldPolygon, TArray<FEHBFloorSlabWorldEdge>& OutEdges)
	{
		OutEdges.Reset();
		const TArray<FVector2d> Loop2D = To2DLoop(WorldPolygon);
		if (Loop2D.Num() < 3)
		{
			return false;
		}

		const double SignedArea = CalculateTwiceArea(Loop2D);
		if (FMath::Abs(SignedArea) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return false;
		}

		const bool bCounterClockwise = SignedArea > 0.0;
		for (int32 Index = 0; Index < WorldPolygon.Num(); ++Index)
		{
			const FVector& Start = WorldPolygon[Index];
			const FVector& End = WorldPolygon[(Index + 1) % WorldPolygon.Num()];
			FVector Direction(End.X - Start.X, End.Y - Start.Y, 0.0f);
			if (!Direction.Normalize())
			{
				continue;
			}

			const FVector OutwardNormal = bCounterClockwise
				? FVector(Direction.Y, -Direction.X, 0.0f).GetSafeNormal()
				: FVector(-Direction.Y, Direction.X, 0.0f).GetSafeNormal();
			if (OutwardNormal.IsNearlyZero())
			{
				continue;
			}

			FEHBFloorSlabWorldEdge& Edge = OutEdges.AddDefaulted_GetRef();
			Edge.Start = Start;
			Edge.End = End;
			Edge.Direction = Direction;
			Edge.OutwardNormal = OutwardNormal;
			Edge.Midpoint = (Start + End) * 0.5f;
		}

		return OutEdges.Num() > 0;
	}

	bool DoVerticalRangesOverlap(
		float FirstMinZ,
		float FirstMaxZ,
		float SecondMinZ,
		float SecondMaxZ,
		float Tolerance)
	{
		if (FirstMinZ > FirstMaxZ)
		{
			Swap(FirstMinZ, FirstMaxZ);
		}
		if (SecondMinZ > SecondMaxZ)
		{
			Swap(SecondMinZ, SecondMaxZ);
		}
		return FMath::Max(FirstMinZ, SecondMinZ) <= FMath::Min(FirstMaxZ, SecondMaxZ) + FMath::Max(0.0f, Tolerance);
	}

	void GatherSurfaceSideSnapEdgesForSlab(
		const AEHB_FloorSlab* SourceSlab,
		TArray<FEHBSurfaceSideSnapEdge>& OutEdges)
	{
		OutEdges.Reset();
		if (!SourceSlab)
		{
			return;
		}

		UWorld* World = SourceSlab->GetWorld();
		if (!World)
		{
			return;
		}

		for (TActorIterator<AEHBElementActorBase> It(World); It; ++It)
		{
			AEHBElementActorBase* Element = *It;
			if (!Element
				|| Element == SourceSlab
				|| Element->IsActorBeingDestroyed()
				|| !Element->GetActorEnableCollision())
			{
				continue;
			}

			if ((SourceSlab->OwningBuilding || Element->OwningBuilding)
				&& Element->OwningBuilding != SourceSlab->OwningBuilding)
			{
				continue;
			}

			TArray<UEHBArchitecturalSurfaceComponent*> SurfaceComponents;
			Element->GetComponents(SurfaceComponents);
			for (UEHBArchitecturalSurfaceComponent* SurfaceComponent : SurfaceComponents)
			{
				if (!SurfaceComponent || !SurfaceComponent->bCanBeSnapTarget)
				{
					continue;
				}

				SurfaceComponent->BuildSideSnapEdges(OutEdges);
			}
		}
	}

	bool DoesTargetEdgeOwnerOverlapCandidatePolygon(
		const FEHBSurfaceSideSnapEdge& TargetEdge,
		const TArray<FVector>& CandidatePolygon)
	{
		const UEHBArchitecturalSurfaceComponent* SourceSurface = TargetEdge.SourceSurface.Get();
		const AEHB_FloorSlab* TargetSlab = SourceSurface
			? Cast<AEHB_FloorSlab>(SourceSurface->GetOwner())
			: nullptr;
		if (!TargetSlab)
		{
			return false;
		}

		TArray<TArray<FVector>> TargetPolygons;
		if (!TargetSlab->BuildEffectiveOuterWorldPolygons(TargetPolygons, true, true)) return false;
		return TargetPolygons.ContainsByPredicate([&](const TArray<FVector>& Polygon)
		{ return DoPolygonsOverlap2D(CandidatePolygon, Polygon); });
	}

	void InsertClosedLoop(UE::Geometry::FArrangement2d& Arrangement, const TArray<FVector2d>& Loop)
	{
		for (int32 Index = 0; Index < Loop.Num(); ++Index)
		{
			const FVector2d& A = Loop[Index];
			const FVector2d& B = Loop[(Index + 1) % Loop.Num()];
			if (!ArePointsNearlyEqual2D(A, B))
			{
				Arrangement.Insert(A, B);
			}
		}
	}

	void AppendQuad(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UVs,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& DesiredNormal,
		bool bDoubleSided = false)
	{
		const int32 BaseIndex = Vertices.Num();
		Vertices.Append({ A, B, C, D });
		const FVector SafeNormal = DesiredNormal.GetSafeNormal();
		const int32 TriangleStartIndex = Triangles.Num();
		const FVector TriangleNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
		if (FVector::DotProduct(TriangleNormal, SafeNormal) <= 0.0f)
		{
			Triangles.Append({ BaseIndex, BaseIndex + 1, BaseIndex + 2, BaseIndex, BaseIndex + 2, BaseIndex + 3 });
		}
		else
		{
			Triangles.Append({ BaseIndex, BaseIndex + 2, BaseIndex + 1, BaseIndex, BaseIndex + 3, BaseIndex + 2 });
		}
		Normals.Append({ SafeNormal, SafeNormal, SafeNormal, SafeNormal });
		const float EdgeU = FMath::Max(FVector::Dist2D(A, B), 1.0) / EHBFloorSlabUVWorldSize;
		const float HeightV = FMath::Max((FMath::Abs(C.Z - B.Z) + FMath::Abs(D.Z - A.Z)) * 0.5, 1.0) / EHBFloorSlabUVWorldSize;
		UVs.Append({
			FVector2D(0.0f, HeightV),
			FVector2D(EdgeU, HeightV),
			FVector2D(EdgeU, 0.0f),
			FVector2D(0.0f, 0.0f)
		});

		if (bDoubleSided)
		{
			const int32 BackBaseIndex = Vertices.Num();
			Vertices.Append({ A, B, C, D });
			Normals.Append({ -SafeNormal, -SafeNormal, -SafeNormal, -SafeNormal });
			UVs.Append({
				FVector2D(0.0f, HeightV),
				FVector2D(EdgeU, HeightV),
				FVector2D(EdgeU, 0.0f),
				FVector2D(0.0f, 0.0f)
			});

			for (int32 TriangleIndex = TriangleStartIndex; TriangleIndex < TriangleStartIndex + 6; TriangleIndex += 3)
			{
				const int32 LocalA = Triangles[TriangleIndex] - BaseIndex;
				const int32 LocalB = Triangles[TriangleIndex + 1] - BaseIndex;
				const int32 LocalC = Triangles[TriangleIndex + 2] - BaseIndex;
				Triangles.Append({
					BackBaseIndex + LocalA,
					BackBaseIndex + LocalC,
					BackBaseIndex + LocalB
				});
			}
		}
	}

	FVector GetLoopSideNormal(const FVector& TopA, const FVector& TopB, bool bInnerSide, bool bLoopCounterClockwise)
	{
		const FVector Edge = (TopB - TopA).GetSafeNormal2D();
		if (Edge.IsNearlyZero())
		{
			return FVector::ForwardVector;
		}

		const FVector RightNormal = FVector::CrossProduct(Edge, FVector::UpVector).GetSafeNormal();
		const FVector LeftNormal = FVector::CrossProduct(FVector::UpVector, Edge).GetSafeNormal();
		if (bLoopCounterClockwise)
		{
			return bInnerSide ? LeftNormal : RightNormal;
		}
		return bInnerSide ? RightNormal : LeftNormal;
	}

	FVector RoundFloorSlabLocalCoordinates(const FVector& LocalLocation)
	{
		return FVector(
			FMath::RoundToDouble(LocalLocation.X),
			FMath::RoundToDouble(LocalLocation.Y),
			LocalLocation.Z);
	}

	bool SnapFoundationSlabLocalLocationToInteger(AEHB_FloorSlab& Slab)
	{
		if (!Slab.bIsFoundation)
		{
			return false;
		}

		FTransform LocalTransform = Slab.GetElementLocalTransform();
		const FVector SnappedLocation = RoundFloorSlabLocalCoordinates(LocalTransform.GetLocation());
		if (LocalTransform.GetLocation().Equals(SnappedLocation))
		{
			return false;
		}

		Slab.Modify();
		LocalTransform.SetLocation(SnappedLocation);
		Slab.SetActorRelativeTransform(LocalTransform);
		return true;
	}
}

AEHB_FloorSlab::AEHB_FloorSlab()
{
	ElementType = EEHBBuildingElementType::FoundationAndFloor;
	ElementCapabilities = static_cast<int32>(
		EEHBElementCapability::Structural
		| EEHBElementCapability::CanSupport
		| EEHBElementCapability::RequiresSupport);
	SemanticTags.AddUnique(TEXT("Structure.Horizontal"));

	MeshComponent = CreateDefaultSubobject<UEHBPlanarSurfaceComponent>(TEXT("FloorSlabMesh"));
	MeshComponent->SetupAttachment(SceneRoot);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MeshComponent->ComponentTags.AddUnique(TEXT("EHB_FloorSlab"));
	if (UEHBArchitecturalSurfaceComponent* SurfaceComponent = Cast<UEHBArchitecturalSurfaceComponent>(MeshComponent))
	{
		SurfaceComponent->InitializeSurface(this, EEHBArchitecturalSurfaceRole::SlabTop, TEXT("Slab.Surface"), INDEX_NONE);
	}
}

void AEHB_FloorSlab::OnConstruction(const FTransform& Transform)
{
	if (FEHBActorImportScope::IsActive()) { Super::OnConstruction(Transform); return; }
	Super::OnConstruction(Transform);
	RebuildSlabMesh();
}

void AEHB_FloorSlab::Destroyed()
{
	Super::Destroyed();
}

void AEHB_FloorSlab::OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished)
{
	Super::OnElementActorMoved_Implementation(OldLocalTransform, NewLocalTransform, bFinished);

	const bool bSnappedToInteger = SnapFoundationSlabLocalLocationToInteger(*this);
	if (!bFinished)
	{
		if (bSnappedToInteger && bIsFoundation && bKeepFoundationBottomOnGround)
		{
			RebuildSlabMesh();
		}
		return;
	}

	bool bChanged = bSnappedToInteger;
	bool bNeedsMeshRebuild = bSnappedToInteger && bIsFoundation && bKeepFoundationBottomOnGround;
	if (bIsFoundation && bKeepFoundationBottomOnGround)
	{
		if (SnapFoundationBottomToGround())
		{
			bChanged = true;
		}
		bNeedsMeshRebuild = true;
	}

	bChanged |= SnapSideToAdjacentFloorSlab(15.0f);
	if (SnapFoundationSlabLocalLocationToInteger(*this))
	{
		bChanged = true;
		if (bIsFoundation && bKeepFoundationBottomOnGround)
		{
			bNeedsMeshRebuild = true;
		}
	}

	if (bIsFoundation && bKeepFoundationBottomOnGround)
	{
		if (SnapFoundationBottomToGround())
		{
			bChanged = true;
		}
		bNeedsMeshRebuild = true;
	}

	if (bChanged || bNeedsMeshRebuild)
	{
		RebuildSlabMesh();
	}
	if (bChanged)
	{
		NotifyElementGeometryChanged(true);
		MarkPackageDirty();
	}
}

#if WITH_EDITOR
void AEHB_FloorSlab::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (FEHBActorImportScope::IsActive()) { Super::PostEditChangeProperty(PropertyChangedEvent); return; }
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RebuildSlabMesh();
}
#endif

void AEHB_FloorSlab::ConfigureDefaultSlab(AEHBBuildingActorBase* InBuilding, const FVector& LocalCenter, float InSize, float InThickness, bool bInIsFoundation)
{
	ConfigureDefaultSlab(InBuilding, FTransform(LocalCenter), InSize, InThickness, bInIsFoundation);
}

void AEHB_FloorSlab::ConfigureDefaultSlab(AEHBBuildingActorBase* InBuilding, const FTransform& LocalTransform, float InSize, float InThickness, bool bInIsFoundation)
{
	RecordOutlineSource(EEHBOutlineSource::ManualOrUnclassified);
	RoomFillLoopGuid.Invalidate();
	RoomFillFloorIndex=INDEX_NONE;
	bIsFoundation = bInIsFoundation;
	bKeepFoundationBottomOnGround = bInIsFoundation;
	Thickness = FMath::Max(1.0f, InThickness);
	ElementType = EEHBBuildingElementType::FoundationAndFloor;

	const float HalfSize = FMath::Max(10.0f, InSize) * 0.5f;
	LocalTopPolygon = {
		FVector(-HalfSize, -HalfSize, 0.0f),
		FVector(-HalfSize, HalfSize, 0.0f),
		FVector(HalfSize, HalfSize, 0.0f),
		FVector(HalfSize, -HalfSize, 0.0f)
	};
	CutOperations.Reset();
	LocalHoles.Reset();
	PreviewCutters.Reset();

	AttachToBuilding(InBuilding, LocalTransform);
	RebuildSlabMesh();
}

bool AEHB_FloorSlab::BuildPreparedSlabMesh(const TArray<FVector>& OuterPolygon, const TArray<FEHBFloorSlabHole>& Holes,
	bool bIncludePreviewCutters, bool bValidateCandidate, FEHBSurfaceMeshBuildResult& OutMesh,
	TArray<FVector>& OutRenderPolygon, TArray<TArray<FVector>>& OutRenderHoles, const FEHBSlabDisplayPartition* OverrideDisplay, TArray<FEHBPlanarSurfaceRegion>* OutPreparedRegions, const TArray<FEHBCutOperation>* OverrideCuts, const TArray<FEHBFloorSlabCutterData>* OverridePreview, bool bIncludeVisualExpansion) const
{
	if (OuterPolygon.Num() < 3 || !MeshComponent) return false;
 const auto& CandidateCuts=OverrideCuts?*OverrideCuts:CutOperations;
 TArray<FEHBCutOperation> ResolvedCuts;if(!ResolveSurfaceOpeningCuts(CandidateCuts,OuterPolygon,Holes,ResolvedCuts))return false;
 const auto& CandidatePreview=OverridePreview?*OverridePreview:PreviewCutters;
 if((OverrideCuts||OverridePreview)&&!ValidateCutStateSources(ResolvedCuts,CandidatePreview))return false;

	if (!CheckDisplayPartition(OuterPolygon,Holes,OverrideDisplay ? *OverrideDisplay : DisplayPartition,CandidateCuts,CandidatePreview)) return false;
	if (bValidateCandidate)
	{
		if (!FMath::IsFinite(Thickness) || Thickness <= 0 || !FMath::IsFinite(VisualExpansion) || !FMath::IsFinite(FoundationGroundTraceSpacing)) return false;
		auto ValidLoop = [&](const TArray<FVector>& Loop)
		{
			if (Loop.Num() < 3 || Loop.ContainsByPredicate([&](const FVector& P)
			{ return P.ContainsNaN() || FMath::Abs(P.Z-GetTopZ()) > 0.001 || FMath::Abs(P.X)>1.e8 || FMath::Abs(P.Y)>1.e8; })) return false;
			const auto Points=To2DLoop(Loop);
			if(Points.Num()<3 || FMath::Abs(CalculateTwiceArea(Points))<=UE_DOUBLE_SMALL_NUMBER)return false;
			for(int32 I=0;I<Points.Num();++I)for(int32 J=I+1;J<Points.Num();++J)
			{
				if(J==(I+1)%Points.Num() || I==(J+1)%Points.Num())continue;
				if(DoSegmentsIntersect2D(Points[I],Points[(I+1)%Points.Num()],Points[J],Points[(J+1)%Points.Num()]))return false;
			}
			return true;
		};
		if (!ValidLoop(OuterPolygon)) return false;
		for (const auto& Hole : Holes) if (!ValidLoop(Hole.LocalPolygon)) return false;
		// A valid single author region may be split by cuts. A disconnected
		// self-intersecting author outline is still not a valid source region.
		FEHBPolygonClipResult SourceRegion;
		if (!FEHBPolygonClipper::UnionXY({OuterPolygon}, SourceRegion)
			|| SourceRegion.Regions.Num() != 1) return false;
	}
	FEHBPolygonClipResult ClipResult;
	if (!BuildCutGeometry(ClipResult, bIncludePreviewCutters, &OuterPolygon, &Holes, OverrideCuts, OverridePreview) || ClipResult.Regions.IsEmpty())
	{
		return false;
	}

 TArray<FEHBPlanarSurfaceRegion> Regions;
 if(!BuildDisplayRegions(ClipResult,bIncludeVisualExpansion,Regions,OverrideDisplay))return false;
 if(Regions.IsEmpty())return false;
 OutRenderPolygon=Regions[0].BoundaryLoop;OutRenderHoles.Reset();for(const auto& H:Regions[0].HoleLoops)OutRenderHoles.Add(H.LocalLoop);
 OutMesh.Reset();
 for(const auto& Region:Regions)
 {
 const auto& RenderTopPolygon=Region.BoundaryLoop;TArray<TArray<FVector>> HolePolygons;for(const auto& H:Region.HoleLoops)HolePolygons.Add(H.LocalLoop);

	FEHBSlabSurfaceMeshBuildInput BuildInput;
	BuildInput.TopBoundaryLoop = RenderTopPolygon;
	BuildInput.TopHoleLoops = HolePolygons;
	BuildInput.TopZ = GetTopZ();
	BuildInput.DefaultBottomZ = GetBottomZ();
	BuildInput.UVWorldSize = EHBFloorSlabUVWorldSize;
	BuildInput.bBuildTop = true;
	BuildInput.bBuildBottom = !bIsFoundation;
	BuildInput.bBuildSides = true;
	BuildInput.bDoubleSideInnerLoops = false;
	BuildInput.SideSegmentLength = FMath::Max(10.0f, FoundationGroundTraceSpacing);
	if (bIsFoundation && bKeepFoundationBottomOnGround)
	{
		BuildInput.ResolveBottomZ = [this](const FVector& LocalTopPoint)
		{
			return ResolveGroundLocalZAt(LocalTopPoint);
		};
	}

	FEHBSurfaceMeshBuildResult Part;
	if (!FEHBPlanarSurfaceGeometryBuilder::BuildSlabSurface(BuildInput, Part))
	{
		return false;
	}

 const int32 Base=OutMesh.Vertices.Num();OutMesh.Vertices.Append(Part.Vertices);OutMesh.Normals.Append(Part.Normals);OutMesh.UV0.Append(Part.UV0);for(int32 I:Part.Triangles)OutMesh.Triangles.Add(Base+I);
 }

 if(OutPreparedRegions)*OutPreparedRegions=MoveTemp(Regions);
	return true;
}

void AEHB_FloorSlab::ApplyPreparedSlabMesh(const FEHBSurfaceMeshBuildResult& Mesh, const TArray<FEHBPlanarSurfaceRegion>& Regions)
{
 if(auto* Surface=Cast<UEHBPlanarSurfaceComponent>(MeshComponent))
 {
  Surface->InitializeSurface(this,EEHBArchitecturalSurfaceRole::SlabTop,TEXT("Slab.Surface"),INDEX_NONE);
  Surface->SetPlanarSurfaceRegions(Regions,FVector::UpVector,EHBFloorSlabUVWorldSize);
 }
 ApplyMesh(Mesh);
}

bool AEHB_FloorSlab::RebuildSlabMesh()
{
	TArray<FEHBPlanarSurfaceRegion> PreparedRegions;FEHBSurfaceMeshBuildResult Mesh; TArray<FVector> Polygon; TArray<TArray<FVector>> Holes;
	if (!BuildPreparedSlabMesh(LocalTopPolygon,LocalHoles,true,false,Mesh,Polygon,Holes,nullptr,&PreparedRegions)) return false;
	ApplyPreparedSlabMesh(Mesh,PreparedRegions); return true;
}

bool AEHB_FloorSlab::ValidateSlabOutline(const TArray<FVector>& OuterPolygon, const TArray<FEHBFloorSlabHole>& Holes, bool bDiscardPreviewCutters) const
{
	FEHBSurfaceMeshBuildResult Mesh; TArray<FVector> Polygon; TArray<TArray<FVector>> RenderHoles;
	return BuildPreparedSlabMesh(OuterPolygon,Holes,!bDiscardPreviewCutters,true,Mesh,Polygon,RenderHoles);
}

bool AEHB_FloorSlab::CheckDisplayPartition(const TArray<FVector>& OuterPolygon,const TArray<FEHBFloorSlabHole>& Holes,const FEHBSlabDisplayPartition& Partition,const TArray<FEHBCutOperation>& Cuts,const TArray<FEHBFloorSlabCutterData>& Preview) const
{
 if(!Partition.IsActive())return true;
 if(Partition.SourcePolygon!=OuterPolygon||Partition.SourceExpansion!=VisualExpansion||!Preview.IsEmpty()||bIsFoundation)return false;
 if(Partition.SourceVersion==0){if(!Holes.IsEmpty()||!Cuts.IsEmpty())return false;}
 else if(Partition.SourceVersion==1)
 {
  if(Partition.SourceThickness!=Thickness||Partition.SourceHoles.Num()!=Holes.Num()||Partition.SourceCuts.Num()!=Cuts.Num())return false;
  for(int32 I=0;I<Holes.Num();++I)if(Partition.SourceHoles[I].LocalPolygon!=Holes[I].LocalPolygon)return false;
  for(int32 I=0;I<Cuts.Num();++I)if(!FEHBCutOperation::StaticStruct()->CompareScriptStruct(&Partition.SourceCuts[I],&Cuts[I],0))return false;
 }
 else return false;
 FEHBDisplayPartitionInput Input;Input.Base.ElementGuid=ElementGuid;Input.Base.SurfaceGuid=FindLogicalSurfaceIdentity(TEXT("Slab.Surface"));Input.Base.SourceName=TEXT("Slab.Surface");
 FName Status;
 if(Partition.SourceVersion==0)
 {
  auto& Region=Input.Base.Regions.AddDefaulted_GetRef();for(const auto& P:OuterPolygon){if(P.ContainsNaN()||FMath::Abs(P.Z-GetTopZ())>0.001)return false;Region.Boundary.Add({P.X,P.Y});}
 }
 else if(!BuildCandidateDisplayRegions(OuterPolygon,Holes,Cuts,Input.Base.Regions,Status,false))return false;
 Input.RequestedDisplay=Partition.Regions;Input.Priority=Partition.Priority;TArray<FEHBLogicalSurfaceDefinition> Result;
 if(!FEHBDisplayPartition::Build({Input},Result,Status,true))return false;
 // Revalidation must not silently repair a supplied allocation by removing holes.
 auto Requested=Input.Base;Requested.Regions=Partition.Regions;
 return Partition.SourceVersion==0||FEHBDisplayPartition::MatchesWithinGrid(Requested,Result[0],Status);
}

void AEHB_FloorSlab::CaptureDisplayPartitionSource(const TArray<FVector>& Outer,const TArray<FEHBFloorSlabHole>& Holes,const TArray<FEHBCutOperation>& Cuts,FEHBSlabDisplayPartition& Partition) const
{
 Partition.SourceVersion=1;Partition.SourcePolygon=Outer;Partition.SourceHoles=Holes;Partition.SourceCuts=Cuts;
 Partition.SourceThickness=Thickness;Partition.SourceExpansion=VisualExpansion;
}

bool AEHB_FloorSlab::SetPartitionedSlabState(const TArray<FVector>& Outer,const TArray<FEHBFloorSlabHole>& Holes,const TArray<FEHBCutOperation>& Cuts,const FEHBSlabDisplayPartition& Partition)
{
 if(Partition.SourceVersion!=1||!Partition.IsActive()||!PreviewCutters.IsEmpty())return false;
 FEHBSurfaceMeshBuildResult Mesh;TArray<FVector> Polygon;TArray<TArray<FVector>> RenderHoles;TArray<FEHBPlanarSurfaceRegion> Prepared;
 const TArray<FEHBFloorSlabCutterData> NoPreview;
 if(!BuildPreparedSlabMesh(Outer,Holes,false,true,Mesh,Polygon,RenderHoles,&Partition,&Prepared,&Cuts,&NoPreview))return false;
 SetFlags(RF_Transactional);Modify();for(auto* Component:GetComponents()){Component->SetFlags(RF_Transactional);Component->Modify();}
 LocalTopPolygon=Outer;LocalHoles=Holes;CutOperations=Cuts;DisplayPartition=Partition;
 ApplyPreparedSlabMesh(Mesh,Prepared);RecordOutlineSource(EEHBOutlineSource::ManualOrUnclassified);MarkPackageDirty();return true;
}

bool AEHB_FloorSlab::ValidatePartitionedSlabState(const TArray<FVector>& Outer,const TArray<FEHBFloorSlabHole>& Holes,const TArray<FEHBCutOperation>& Cuts,const FEHBSlabDisplayPartition& Partition) const
{
 if(Partition.SourceVersion!=1||!Partition.IsActive()||!PreviewCutters.IsEmpty())return false;
 FEHBSurfaceMeshBuildResult Mesh;TArray<FVector> Polygon;TArray<TArray<FVector>> RenderHoles;
 const TArray<FEHBFloorSlabCutterData> NoPreview;
 return BuildPreparedSlabMesh(Outer,Holes,false,true,Mesh,Polygon,RenderHoles,&Partition,nullptr,&Cuts,&NoPreview);
}

bool AEHB_FloorSlab::ValidatePartitionedSlabOutline(const TArray<FVector>& OuterPolygon,const FEHBSlabDisplayPartition& Partition) const
{
 if(!Partition.IsActive())return false;
 FEHBSurfaceMeshBuildResult Mesh;TArray<FVector> Polygon;TArray<TArray<FVector>> Holes;
 return BuildPreparedSlabMesh(OuterPolygon,{},false,true,Mesh,Polygon,Holes,&Partition);
}

bool AEHB_FloorSlab::BuildUnallocatedDisplayRegion(const TArray<FVector>& OuterPolygon,TArray<FEHBLogicalSurfaceRegion>& Regions) const
{
 Regions.Reset();if(!CutOperations.IsEmpty()||!PreviewCutters.IsEmpty()||!LocalHoles.IsEmpty()||bIsFoundation)return false;
 FName Status;return BuildCandidateDisplayRegions(OuterPolygon,{}, {},Regions,Status);
}

bool AEHB_FloorSlab::BuildCandidateDisplayRegions(const TArray<FVector>& OuterPolygon,
 const TArray<FEHBFloorSlabHole>& SourceHoles,const TArray<FEHBCutOperation>& Cuts,
 TArray<FEHBLogicalSurfaceRegion>& Regions,FName& Status,bool bIncludeVisualExpansion) const
{
 Regions.Reset();
 if(bIsFoundation){Status=TEXT("FoundationDisplayCandidateRequiresDefinition");return false;}
 FEHBSlabDisplayPartition Unallocated;FEHBSurfaceMeshBuildResult Mesh;TArray<FVector> Polygon;TArray<TArray<FVector>> Holes;
 TArray<FEHBPlanarSurfaceRegion> Prepared;const TArray<FEHBFloorSlabCutterData> NoPreview;
 if(!BuildPreparedSlabMesh(OuterPolygon,SourceHoles,false,true,Mesh,Polygon,Holes,&Unallocated,&Prepared,&Cuts,&NoPreview,bIncludeVisualExpansion))
 {Status=TEXT("InvalidSlabDisplayCandidate");return false;}
 for(const auto& Part:Prepared)
 {
  auto& R=Regions.AddDefaulted_GetRef();for(const auto& V:Part.BoundaryLoop)R.Boundary.Add({V.X,V.Y});
  for(const auto& H:Part.HoleLoops){auto& L=R.Holes.AddDefaulted_GetRef().Vertices;for(const auto& V:H.LocalLoop)L.Add({V.X,V.Y});}
 }
 Status=TEXT("Ready");return true;
}

bool AEHB_FloorSlab::SetPartitionedSlabOutline(const TArray<FVector>& OuterPolygon,const FEHBSlabDisplayPartition& Partition)
{
 if(!Partition.IsActive())return false;
 TArray<FEHBPlanarSurfaceRegion> PreparedRegions;FEHBSurfaceMeshBuildResult Mesh;TArray<FVector> Polygon;TArray<TArray<FVector>> Holes;
 if(!BuildPreparedSlabMesh(OuterPolygon,{},false,true,Mesh,Polygon,Holes,&Partition,&PreparedRegions))return false;
 SetFlags(RF_Transactional);Modify();for(auto* Component:GetComponents()){Component->SetFlags(RF_Transactional);Component->Modify();}
 LocalTopPolygon=OuterPolygon;LocalHoles.Reset();DisplayPartition=Partition;
 ApplyPreparedSlabMesh(Mesh,PreparedRegions);RecordOutlineSource(EEHBOutlineSource::ManualOrUnclassified);MarkPackageDirty();return true;
}

bool AEHB_FloorSlab::SetSlabOutline(const TArray<FVector>& OuterPolygon, const TArray<FEHBFloorSlabHole>& Holes, bool bDiscardPreviewCutters)
{
	TArray<FEHBPlanarSurfaceRegion> PreparedRegions;FEHBSurfaceMeshBuildResult Mesh; TArray<FVector> Polygon; TArray<TArray<FVector>> RenderHoles;
	if (!BuildPreparedSlabMesh(OuterPolygon,Holes,!bDiscardPreviewCutters,true,Mesh,Polygon,RenderHoles,nullptr,&PreparedRegions)) return false;
	SetFlags(RF_Transactional); Modify();
	for (auto* Component : GetComponents()) { Component->SetFlags(RF_Transactional); Component->Modify(); }
	LocalTopPolygon = OuterPolygon; LocalHoles = Holes;
	if (bDiscardPreviewCutters) PreviewCutters.Reset();
	ApplyPreparedSlabMesh(Mesh,PreparedRegions);
	RecordOutlineSource(EEHBOutlineSource::ManualOrUnclassified);
	MarkPackageDirty(); return true;
}

bool AEHB_FloorSlab::SetSlabOutlineIfNeeded(const TArray<FVector>& OuterPolygon,bool& bOutChanged)
{
 bOutChanged=false;TArray<FEHBPlanarSurfaceRegion> PreparedRegions;FEHBSurfaceMeshBuildResult Mesh;TArray<FVector> Polygon;TArray<TArray<FVector>> Holes;
 if(!BuildPreparedSlabMesh(OuterPolygon,{},false,true,Mesh,Polygon,Holes,nullptr,&PreparedRegions))return false;
 UMaterialInterface* Material=SurfaceMaterial.LoadSynchronous();if(!Material)Material=ResolveConfiguredDefaultWhiteBoxMaterial();
 bool Collision=true,Metadata=true;const auto* Surface=Cast<UEHBPlanarSurfaceComponent>(MeshComponent);
 if(Surface)
 {
  if(auto* Override=Surface->SurfaceMaterialOverride.LoadSynchronous())Material=Override;
  Collision=Surface->bEnableSurfaceCollision;
  Metadata=Surface->SurfaceGuid.IsValid()&&Surface->OwnerElement==this&&Surface->OwningBuilding==OwningBuilding&&Surface->OwnerElementGuid==ElementGuid
   &&Surface->SurfaceRole==EEHBArchitecturalSurfaceRole::SlabTop&&Surface->SurfaceName==TEXT("Slab.Surface")&&Surface->SurfaceSubIndex==INDEX_NONE
   &&Surface->LocalBoundaryLoop==Polygon&&Surface->LocalHoleLoops.Num()==Holes.Num()&&Surface->LocalPlaneNormal==FVector::UpVector&&Surface->UVWorldSize==EHBFloorSlabUVWorldSize;
  for(int32 I=0;Metadata&&I<Holes.Num();++I)Metadata=Surface->LocalHoleLoops[I].LocalLoop==Holes[I];
  const auto ActualRegions=Surface->GetPlanarSurfaceRegions();Metadata=Metadata&&ActualRegions.Num()==PreparedRegions.Num();
  for(int32 I=0;Metadata&&I<PreparedRegions.Num();++I){Metadata=ActualRegions[I].BoundaryLoop==PreparedRegions[I].BoundaryLoop&&ActualRegions[I].HoleLoops.Num()==PreparedRegions[I].HoleLoops.Num();for(int32 J=0;Metadata&&J<PreparedRegions[I].HoleLoops.Num();++J)Metadata=ActualRegions[I].HoleLoops[J].LocalLoop==PreparedRegions[I].HoleLoops[J].LocalLoop;}
 }
 const bool Matches=LocalTopPolygon==OuterPolygon&&LocalHoles.IsEmpty()&&PreviewCutters.IsEmpty()&&Metadata&&Mesh.TriangleMaterialIndices.IsEmpty()
  &&MeshComponent->GetNumSections()==1&&MeshComponent->GetProcMeshSection(0)->SectionName==TEXT("FloorSlabSurface")&&MeshComponent->GetMaterial(0)==Material
  &&MeshComponent->GetCollisionEnabled()==(Collision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision)
  &&MeshComponent->MatchesMeshSection(0,Mesh.Vertices,Mesh.Triangles,Mesh.Normals,Mesh.UV0,{},{},{},{},{},Collision);
 if(Matches)return true;
 SetFlags(RF_Transactional);Modify();for(auto* Component:GetComponents()){Component->SetFlags(RF_Transactional);Component->Modify();}
 LocalTopPolygon=OuterPolygon;LocalHoles.Reset();PreviewCutters.Reset();ApplyPreparedSlabMesh(Mesh,PreparedRegions);
 RecordOutlineSource(EEHBOutlineSource::ManualOrUnclassified);MarkPackageDirty();bOutChanged=true;return true;
}

bool AEHB_FloorSlab::AddSquarePreviewCutterInEditor()
{
	return AddPreviewCutterWithShape(EEHBFloorSlabCutterShape::Square);
}

bool AEHB_FloorSlab::AddCirclePreviewCutterInEditor()
{
	return AddPreviewCutterWithShape(EEHBFloorSlabCutterShape::Circle);
}

bool AEHB_FloorSlab::CommitPreviewCutters()
{
	if (PreviewCutters.IsEmpty())
	{
		return false;
	}

	auto Candidate=CutOperations;
	for (const FEHBFloorSlabCutterData& CutterData : PreviewCutters)
	{
		FEHBCutOperation Operation;
		Operation.OperationGuid = FGuid::NewGuid();
		Operation.bEnabled = true;
		Operation.OperationType = EEHBCutOperationType::Subtract;
		Operation.Stage = EEHBCutStage::Profile;
		Operation.ProjectionMode = EEHBCutProjectionMode::HorizontalXY;
		Operation.TransformPolicy = EEHBCutTransformPolicy::TargetLocal;
		Operation.Source.SourceType = EEHBCutSourceType::ExplicitPrism;
		Operation.Source.PrimitiveShape = CutterData.Shape == EEHBFloorSlabCutterShape::Circle
			? EEHBCutPrimitiveShape::Circle
			: EEHBCutPrimitiveShape::Square;
		Operation.Source.LocalTransform = CutterData.LocalTransform;
		Operation.Source.Size = CutterData.Size;
		Operation.Source.Height = CutterData.Height;
		Operation.Source.CircleSideCount = CutterData.CircleSideCount;
		Operation.EnsureGuids();
		Candidate.Add(MoveTemp(Operation));
	}
	return SetCutState(Candidate,{});
}

bool AEHB_FloorSlab::SetEditedOutlineLoop(int32 LoopIndex,const TArray<FVector>& Loop)
{
 auto Outline=LocalTopPolygon;auto Holes=LocalHoles;
 if(LoopIndex==INDEX_NONE)Outline=Loop;
 else{if(!Holes.IsValidIndex(LoopIndex))return false;if(Loop.IsEmpty())Holes.RemoveAt(LoopIndex);else Holes[LoopIndex].LocalPolygon=Loop;}
 if(!SetSlabOutline(Outline,Holes))return false;NotifyElementGeometryChanged(true);return true;
}

bool AEHB_FloorSlab::InsertCornerOnEdge(int32 FirstPointIndex, int32 SecondPointIndex)
{
	return InsertCornerOnEdge(INDEX_NONE, FirstPointIndex, SecondPointIndex);
}

bool AEHB_FloorSlab::InsertCornerOnEdge(int32 LoopIndex, int32 FirstPointIndex, int32 SecondPointIndex)
{
	if (IsCutOperationLoopIndex(LoopIndex))
	{
		const FEHBCutOperation* Original = GetEditableCutOperationByLoopIndex(LoopIndex);
		if(!Original)return false;FEHBCutOperation Candidate=*Original;auto* Operation=&Candidate;
		TArray<FVector> Loop;
		if (!Operation
			|| !ResolveCutOperationToLocalPolygon(*Operation, Loop)
			|| Loop.Num() < 3
			|| !Loop.IsValidIndex(FirstPointIndex)
			|| !Loop.IsValidIndex(SecondPointIndex)
			|| (FirstPointIndex + 1) % Loop.Num() != SecondPointIndex)
		{
			return false;
		}

		if (!EnsureCutOperationEditablePolygon(*Operation)
			|| !Operation->Source.ExplicitPolygon.Points.IsValidIndex(FirstPointIndex)
			|| !Operation->Source.ExplicitPolygon.Points.IsValidIndex(SecondPointIndex))
		{
			return false;
		}

		const FVector NewPoint =
			(Operation->Source.ExplicitPolygon.Points[FirstPointIndex].LocalPosition
				+ Operation->Source.ExplicitPolygon.Points[SecondPointIndex].LocalPosition) * 0.5f;
		FEHBCutPolygonPoint NewPolygonPoint;
		NewPolygonPoint.LocalPosition = NewPoint;
		NewPolygonPoint.EnsureGuid();
		Operation->Source.ExplicitPolygon.Points.Insert(NewPolygonPoint, SecondPointIndex);

		return UpdateCutOperation(Candidate);
	}

 TArray<FVector> Loop;if(!GetEditableLoopCopy(LoopIndex,Loop)||Loop.Num()<3||!Loop.IsValidIndex(FirstPointIndex)||!Loop.IsValidIndex(SecondPointIndex)||(FirstPointIndex+1)%Loop.Num()!=SecondPointIndex)return false;
 const FVector Point=(Loop[FirstPointIndex]+Loop[SecondPointIndex])*0.5;Loop.Insert(Point,SecondPointIndex);return SetEditedOutlineLoop(LoopIndex,Loop);
}

bool AEHB_FloorSlab::RemoveCorner(int32 PointIndex)
{
	return RemoveCorner(INDEX_NONE, PointIndex);
}

bool AEHB_FloorSlab::RemoveCorner(int32 LoopIndex, int32 PointIndex)
{
	if (IsCutOperationLoopIndex(LoopIndex))
	{
		const FEHBCutOperation* Original = GetEditableCutOperationByLoopIndex(LoopIndex);
		if(!Original)return false;FEHBCutOperation Candidate=*Original;auto* Operation=&Candidate;
		TArray<FVector> Loop;
		if (!Operation
			|| !ResolveCutOperationToLocalPolygon(*Operation, Loop)
			|| !Loop.IsValidIndex(PointIndex))
		{
			return false;
		}

		if (!EnsureCutOperationEditablePolygon(*Operation)
			|| !Operation->Source.ExplicitPolygon.Points.IsValidIndex(PointIndex))
		{
			return false;
		}

		if (Operation->Source.ExplicitPolygon.Points.Num() <= 3)
		{
			const int32 CutOperationIndex = GetCutOperationIndexFromLoopIndex(LoopIndex);
			if (!CutOperations.IsValidIndex(CutOperationIndex))
			{
				return false;
			}
			return RemoveCutOperation(Operation->OperationGuid);
		}
		else
		{
			Operation->Source.ExplicitPolygon.Points.RemoveAt(PointIndex);
		}

		return UpdateCutOperation(Candidate);
	}

 TArray<FVector> Loop;if(!GetEditableLoopCopy(LoopIndex,Loop)||!Loop.IsValidIndex(PointIndex)||(LoopIndex==INDEX_NONE&&Loop.Num()<=3))return false;
 if(LoopIndex!=INDEX_NONE&&Loop.Num()<=3)Loop.Reset();else Loop.RemoveAt(PointIndex);return SetEditedOutlineLoop(LoopIndex,Loop);
}

bool AEHB_FloorSlab::UpdateCornerWorldLocation(int32 PointIndex, const FVector& WorldLocation)
{
	return UpdateCornerWorldLocation(INDEX_NONE, PointIndex, WorldLocation);
}

bool AEHB_FloorSlab::UpdateCornerWorldLocation(int32 LoopIndex, int32 PointIndex, const FVector& WorldLocation)
{
	if (IsCutOperationLoopIndex(LoopIndex))
	{
		const FEHBCutOperation* Original = GetEditableCutOperationByLoopIndex(LoopIndex);
		if(!Original)return false;FEHBCutOperation Candidate=*Original;auto* Operation=&Candidate;
		TArray<FVector> Loop;
		if (!Operation
			|| !ResolveCutOperationToLocalPolygon(*Operation, Loop)
			|| !Loop.IsValidIndex(PointIndex))
		{
			return false;
		}

		if (!EnsureCutOperationEditablePolygon(*Operation)
			|| !Operation->Source.ExplicitPolygon.Points.IsValidIndex(PointIndex))
		{
			return false;
		}

		FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
		LocalLocation.Z = Operation->Source.ExplicitPolygon.Points[PointIndex].LocalPosition.Z;
		Operation->Source.ExplicitPolygon.Points[PointIndex].LocalPosition = LocalLocation;

		return UpdateCutOperation(Candidate);
	}

 TArray<FVector> Loop;if(!GetEditableLoopCopy(LoopIndex,Loop)||!Loop.IsValidIndex(PointIndex)||WorldLocation.ContainsNaN())return false;
 FVector Point=GetActorTransform().InverseTransformPosition(WorldLocation);Point.Z=Loop[PointIndex].Z;Loop[PointIndex]=Point;return SetEditedOutlineLoop(LoopIndex,Loop);
}

bool AEHB_FloorSlab::UpdateCornerDistanceToAdjacentPoint(
	int32 LoopIndex,
	int32 PointIndex,
	bool bPreviousPoint,
	float NewDistance)
{
	TArray<FVector> Loop;
	if (!GetEditableLoopCopy(LoopIndex, Loop) || Loop.Num() < 3 || !Loop.IsValidIndex(PointIndex))
	{
		return false;
	}

	const int32 AnchorIndex = bPreviousPoint
		? (PointIndex - 1 + Loop.Num()) % Loop.Num()
		: (PointIndex + 1) % Loop.Num();
	const FVector CurrentPoint = Loop[PointIndex];
	const FVector AnchorPoint = Loop[AnchorIndex];
	const FVector Direction = (CurrentPoint - AnchorPoint).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	const float ClampedDistance = FMath::Clamp(NewDistance, 1.0f, 100000.0f);
	if (FMath::IsNearlyEqual(FVector::Dist2D(CurrentPoint, AnchorPoint), ClampedDistance, 0.01f))
	{
		return false;
	}

	FVector NewLocalPoint = AnchorPoint + Direction * ClampedDistance;
	NewLocalPoint.Z = CurrentPoint.Z;
	return UpdateCornerWorldLocation(
		LoopIndex,
		PointIndex,
		GetActorTransform().TransformPosition(NewLocalPoint));
}

bool AEHB_FloorSlab::OffsetEdgeWorldLocation(int32 FirstPointIndex, int32 SecondPointIndex, const FVector& WorldDelta)
{
	return OffsetEdgeWorldLocation(INDEX_NONE, FirstPointIndex, SecondPointIndex, WorldDelta);
}

bool AEHB_FloorSlab::OffsetEdgeWorldLocation(int32 LoopIndex, int32 FirstPointIndex, int32 SecondPointIndex, const FVector& WorldDelta)
{
	if (IsCutOperationLoopIndex(LoopIndex))
	{
		const FEHBCutOperation* Original = GetEditableCutOperationByLoopIndex(LoopIndex);
		if(!Original)return false;FEHBCutOperation Candidate=*Original;auto* Operation=&Candidate;
		TArray<FVector> Loop;
		if (!Operation
			|| !ResolveCutOperationToLocalPolygon(*Operation, Loop)
			|| !Loop.IsValidIndex(FirstPointIndex)
			|| !Loop.IsValidIndex(SecondPointIndex))
		{
			return false;
		}

		if (!EnsureCutOperationEditablePolygon(*Operation)
			|| !Operation->Source.ExplicitPolygon.Points.IsValidIndex(FirstPointIndex)
			|| !Operation->Source.ExplicitPolygon.Points.IsValidIndex(SecondPointIndex))
		{
			return false;
		}

		const FVector LocalDelta = GetActorTransform().InverseTransformVectorNoScale(WorldDelta);
		const FVector PlanarDelta(LocalDelta.X, LocalDelta.Y, 0.0f);
		Operation->Source.ExplicitPolygon.Points[FirstPointIndex].LocalPosition += PlanarDelta;
		Operation->Source.ExplicitPolygon.Points[SecondPointIndex].LocalPosition += PlanarDelta;

		return UpdateCutOperation(Candidate);
	}

 TArray<FVector> Loop;if(!GetEditableLoopCopy(LoopIndex,Loop)||!Loop.IsValidIndex(FirstPointIndex)||!Loop.IsValidIndex(SecondPointIndex)||WorldDelta.ContainsNaN())return false;
 const FVector Delta=GetActorTransform().InverseTransformVectorNoScale(WorldDelta);Loop[FirstPointIndex]+=FVector(Delta.X,Delta.Y,0);Loop[SecondPointIndex]+=FVector(Delta.X,Delta.Y,0);return SetEditedOutlineLoop(LoopIndex,Loop);
}

bool AEHB_FloorSlab::SnapOuterCornerHandleWorldLocation(int32 PointIndex, FVector& InOutWorldLocation, float MaxDistance) const
{
	if (!bEnableAdjacentSlabSnap || !LocalTopPolygon.IsValidIndex(PointIndex))
	{
		return false;
	}

	float ThisMinZ = 0.0f;
	float ThisMaxZ = 0.0f;
	GetApproxWorldVerticalRange(ThisMinZ, ThisMaxZ);

	const float SafeMaxDistance = FMath::Max(1.0f, MaxDistance);
	constexpr float VerticalTolerance = 2.0f;
	bool bHasBestPoint = false;
	float BestDistance = TNumericLimits<float>::Max();
	FVector BestPoint = InOutWorldLocation;

	TArray<FEHBSurfaceSideSnapEdge> TargetEdges;
	GatherSurfaceSideSnapEdgesForSlab(this, TargetEdges);
	const float SnapDistance = FMath::Max(
		SafeMaxDistance,
		FMath::Clamp(FMath::Max(1.0f, Thickness) + 20.0f, 35.0f, 160.0f));

	for (const FEHBSurfaceSideSnapEdge& TargetEdge : TargetEdges)
	{
		if (!TargetEdge.IsValid()
			|| !DoVerticalRangesOverlap(ThisMinZ, ThisMaxZ, TargetEdge.MinWorldZ, TargetEdge.MaxWorldZ, VerticalTolerance))
		{
			continue;
		}

		const FVector TargetPoints[] = { TargetEdge.WorldStart, TargetEdge.WorldEnd };
		for (const FVector& TargetPoint : TargetPoints)
		{
			const float Distance = FVector::Dist2D(InOutWorldLocation, TargetPoint);
			if (Distance <= SnapDistance && Distance < BestDistance)
			{
				bHasBestPoint = true;
				BestDistance = Distance;
				BestPoint = TargetPoint;
			}
		}
	}

	if (!bHasBestPoint || FVector::DistSquared2D(InOutWorldLocation, BestPoint) <= 0.01f)
	{
		return false;
	}

	const float OriginalZ = InOutWorldLocation.Z;
	InOutWorldLocation = FVector(BestPoint.X, BestPoint.Y, OriginalZ);
	return true;
}

bool AEHB_FloorSlab::SnapCornerToAdjacentAxesWorldLocation(
	int32 LoopIndex,
	int32 PointIndex,
	FVector& InOutWorldLocation,
	float MaxDistance) const
{
	TArray<FVector> Loop;
	if (!GetEditableLoopCopy(LoopIndex, Loop) || Loop.Num() < 3 || !Loop.IsValidIndex(PointIndex))
	{
		return false;
	}

	const int32 PreviousIndex = (PointIndex - 1 + Loop.Num()) % Loop.Num();
	const int32 NextIndex = (PointIndex + 1) % Loop.Num();
	const FTransform CoordinateTransform = OwningBuilding
		? OwningBuilding->GetActorTransform()
		: FTransform::Identity;
	const FTransform SlabTransform = GetActorTransform();

	FVector Candidate = CoordinateTransform.InverseTransformPosition(InOutWorldLocation);
	const FVector Previous = CoordinateTransform.InverseTransformPosition(
		SlabTransform.TransformPosition(Loop[PreviousIndex]));
	const FVector Next = CoordinateTransform.InverseTransformPosition(
		SlabTransform.TransformPosition(Loop[NextIndex]));
	const float SafeMaxDistance = FMath::Max(1.0f, MaxDistance);

	auto SnapCoordinate = [SafeMaxDistance](double Value, double PreviousValue, double NextValue, double& OutValue) -> bool
	{
		const double PreviousDistance = FMath::Abs(Value - PreviousValue);
		const double NextDistance = FMath::Abs(Value - NextValue);
		const double BestDistance = FMath::Min(PreviousDistance, NextDistance);
		if (BestDistance > SafeMaxDistance)
		{
			return false;
		}

		OutValue = PreviousDistance <= NextDistance ? PreviousValue : NextValue;
		return true;
	};

	bool bSnapped = false;
	bSnapped |= SnapCoordinate(Candidate.X, Previous.X, Next.X, Candidate.X);
	bSnapped |= SnapCoordinate(Candidate.Y, Previous.Y, Next.Y, Candidate.Y);
	if (!bSnapped)
	{
		return false;
	}

	const float OriginalWorldZ = InOutWorldLocation.Z;
	InOutWorldLocation = CoordinateTransform.TransformPosition(Candidate);
	InOutWorldLocation.Z = OriginalWorldZ;
	return true;
}

bool AEHB_FloorSlab::SnapOuterEdgeHandleWorldDelta(
	int32 FirstPointIndex,
	int32 SecondPointIndex,
	FVector& InOutWorldDelta,
	float MaxAngleDegrees,
	float MaxDistance) const
{
	if (!bEnableAdjacentSlabSnap
		|| LocalTopPolygon.Num() < 3
		|| !LocalTopPolygon.IsValidIndex(FirstPointIndex)
		|| !LocalTopPolygon.IsValidIndex(SecondPointIndex)
		|| SecondPointIndex != (FirstPointIndex + 1) % LocalTopPolygon.Num())
	{
		return false;
	}

	const FTransform SlabTransform = GetActorTransform();
	TArray<FVector> CandidatePolygon;
	CandidatePolygon.Reserve(LocalTopPolygon.Num());
	for (int32 PointIndex = 0; PointIndex < LocalTopPolygon.Num(); ++PointIndex)
	{
		FVector WorldPoint = SlabTransform.TransformPosition(LocalTopPolygon[PointIndex]);
		if (PointIndex == FirstPointIndex || PointIndex == SecondPointIndex)
		{
			WorldPoint += InOutWorldDelta;
		}
		CandidatePolygon.Add(WorldPoint);
	}

	const TArray<FVector2d> CandidateLoop2D = To2DLoop(CandidatePolygon);
	if (CandidateLoop2D.Num() < 3)
	{
		return false;
	}

	const double SignedArea = CalculateTwiceArea(CandidateLoop2D);
	if (FMath::Abs(SignedArea) <= UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	FVector CandidateStart = CandidatePolygon[FirstPointIndex];
	FVector CandidateEnd = CandidatePolygon[SecondPointIndex];
	FVector CandidateDirection(CandidateEnd.X - CandidateStart.X, CandidateEnd.Y - CandidateStart.Y, 0.0f);
	if (!CandidateDirection.Normalize())
	{
		return false;
	}

	const bool bCounterClockwise = SignedArea > 0.0;
	const FVector CandidateOutwardNormal = bCounterClockwise
		? FVector(CandidateDirection.Y, -CandidateDirection.X, 0.0f).GetSafeNormal()
		: FVector(-CandidateDirection.Y, CandidateDirection.X, 0.0f).GetSafeNormal();
	if (CandidateOutwardNormal.IsNearlyZero())
	{
		return false;
	}

	float ThisMinZ = 0.0f;
	float ThisMaxZ = 0.0f;
	GetApproxWorldVerticalRange(ThisMinZ, ThisMaxZ);

	const FVector CandidateMidpoint = (CandidateStart + CandidateEnd) * 0.5f;
	const float SafeAngleDegrees = FMath::Clamp(MaxAngleDegrees, 0.0f, 89.0f);
	const float MinEdgeAlignment = FMath::Cos(FMath::DegreesToRadians(SafeAngleDegrees));
	const float SafeMaxDistance = FMath::Max(1.0f, MaxDistance);
	constexpr float MinProjectionOverlap = 5.0f;
	constexpr float VerticalTolerance = 2.0f;
	bool bHasBestDelta = false;
	float BestScore = TNumericLimits<float>::Max();
	FVector BestDelta = InOutWorldDelta;

	TArray<FEHBSurfaceSideSnapEdge> TargetEdges;
	GatherSurfaceSideSnapEdgesForSlab(this, TargetEdges);
	const float NearSnapDistance = FMath::Max(
		SafeMaxDistance,
		FMath::Clamp(FMath::Max(1.0f, Thickness) + 10.0f, 25.0f, 120.0f));
	const float OverlapSnapDistance = FMath::Max(NearSnapDistance, 250.0f);
	const float CornerSnapDistance = FMath::Max(
		SafeMaxDistance,
		FMath::Clamp(FMath::Max(1.0f, Thickness) + 25.0f, 40.0f, 160.0f));

	for (const FEHBSurfaceSideSnapEdge& TargetEdge : TargetEdges)
	{
		if (!TargetEdge.IsValid()
			|| !DoVerticalRangesOverlap(ThisMinZ, ThisMaxZ, TargetEdge.MinWorldZ, TargetEdge.MaxWorldZ, VerticalTolerance))
		{
			continue;
		}

		const float DirectionDot = DotXY(CandidateDirection, TargetEdge.WorldDirection);
		if (FMath::Abs(DirectionDot) < MinEdgeAlignment)
		{
			continue;
		}

		if (DotXY(CandidateOutwardNormal, TargetEdge.WorldOutwardNormal) > -0.5f)
		{
			continue;
		}

		const float CandidateStartProjection = DotXY(CandidateStart, TargetEdge.WorldDirection);
		const float CandidateEndProjection = DotXY(CandidateEnd, TargetEdge.WorldDirection);
		const float TargetStartProjection = DotXY(TargetEdge.WorldStart, TargetEdge.WorldDirection);
		const float TargetEndProjection = DotXY(TargetEdge.WorldEnd, TargetEdge.WorldDirection);
		const float ProjectionOverlap = GetIntervalOverlap(
			CandidateStartProjection,
			CandidateEndProjection,
			TargetStartProjection,
			TargetEndProjection);

		const float SignedDistance = DotXY(CandidateMidpoint - TargetEdge.WorldMidpoint, TargetEdge.WorldOutwardNormal);
		const float AbsDistance = FMath::Abs(SignedDistance);
		const bool bPolygonsOverlap = DoesTargetEdgeOwnerOverlapCandidatePolygon(TargetEdge, CandidatePolygon);
		if (!bPolygonsOverlap && AbsDistance > NearSnapDistance)
		{
			continue;
		}
		if (bPolygonsOverlap && AbsDistance > OverlapSnapDistance)
		{
			continue;
		}

		FVector CandidateAdjustment = -SignedDistance * TargetEdge.WorldOutwardNormal;
		CandidateAdjustment.Z = 0.0f;

		bool bAlignsCorner = false;
		float BestCornerDistance = TNumericLimits<float>::Max();
		FVector BestCornerAdjustment = CandidateAdjustment;
		const bool bSameDirection = DirectionDot >= 0.0f;
		const TPair<FVector, FVector> CornerPairs[] = {
			TPair<FVector, FVector>(CandidateStart + CandidateAdjustment, bSameDirection ? TargetEdge.WorldStart : TargetEdge.WorldEnd),
			TPair<FVector, FVector>(CandidateEnd + CandidateAdjustment, bSameDirection ? TargetEdge.WorldEnd : TargetEdge.WorldStart)
		};
		for (const TPair<FVector, FVector>& CornerPair : CornerPairs)
		{
			const float CornerDistance = FVector::Dist2D(CornerPair.Key, CornerPair.Value);
			if (CornerDistance <= CornerSnapDistance && CornerDistance < BestCornerDistance)
			{
				bAlignsCorner = true;
				BestCornerDistance = CornerDistance;
				BestCornerAdjustment = CandidateAdjustment + (CornerPair.Value - CornerPair.Key);
				BestCornerAdjustment.Z = 0.0f;
			}
		}

		if (!bAlignsCorner && ProjectionOverlap < MinProjectionOverlap)
		{
			continue;
		}

		if (bAlignsCorner)
		{
			CandidateAdjustment = BestCornerAdjustment;
		}

		if (CandidateAdjustment.X * CandidateAdjustment.X + CandidateAdjustment.Y * CandidateAdjustment.Y <= 0.01f)
		{
			continue;
		}

		const float Score = AbsDistance
			- FMath::Max(0.0f, FMath::Min(ProjectionOverlap, 500.0f)) * 0.01f
			+ (bPolygonsOverlap ? 0.0f : 25.0f)
			+ (bAlignsCorner ? BestCornerDistance * 0.1f - 50.0f : 0.0f);
		if (!bHasBestDelta || Score < BestScore)
		{
			bHasBestDelta = true;
			BestScore = Score;
			BestDelta = InOutWorldDelta + CandidateAdjustment;
		}
	}

	if (!bHasBestDelta || FVector::DistSquared2D(InOutWorldDelta, BestDelta) <= 0.01f)
	{
		return false;
	}

	BestDelta.Z = InOutWorldDelta.Z;
	InOutWorldDelta = BestDelta;
	return true;
}

bool AEHB_FloorSlab::UpdateCutterWorldTransform(int32 CutterIndex,const FTransform& WorldTransform)
{
 if(!PreviewCutters.IsValidIndex(CutterIndex)||WorldTransform.ContainsNaN())return false;
 auto Candidate=PreviewCutters;auto& Transform=Candidate[CutterIndex].LocalTransform;Transform=WorldTransform.GetRelativeTransform(GetActorTransform());
 FVector Scale=Transform.GetScale3D();Scale.X=FMath::Max(0.05f,FMath::Abs(Scale.X));Scale.Y=FMath::Max(0.05f,FMath::Abs(Scale.Y));Scale.Z=FMath::Max(0.05f,FMath::Abs(Scale.Z));Transform.SetScale3D(Scale);
 return SetCutState(CutOperations,Candidate);
}

bool AEHB_FloorSlab::RemovePreviewCutter(int32 CutterIndex)
{
 if(!PreviewCutters.IsValidIndex(CutterIndex))return false;auto Candidate=PreviewCutters;Candidate.RemoveAt(CutterIndex);return SetCutState(CutOperations,Candidate);
}

bool AEHB_FloorSlab::IsCutOperationLoopIndex(int32 LoopIndex)
{
	return LoopIndex < INDEX_NONE;
}

int32 AEHB_FloorSlab::MakeCutOperationLoopIndex(int32 CutOperationIndex)
{
	return CutOperationIndex >= 0 ? INDEX_NONE - 1 - CutOperationIndex : INDEX_NONE;
}

int32 AEHB_FloorSlab::GetCutOperationIndexFromLoopIndex(int32 LoopIndex)
{
	return IsCutOperationLoopIndex(LoopIndex) ? INDEX_NONE - 1 - LoopIndex : INDEX_NONE;
}

int32 AEHB_FloorSlab::GetEditableCutOperationCount() const
{
	return CutOperations.Num();
}

bool AEHB_FloorSlab::GetEditableCutOperationLoop(int32 CutOperationIndex, TArray<FVector>& OutLocalLoop) const
{
	OutLocalLoop.Reset();
	if (!CutOperations.IsValidIndex(CutOperationIndex))
	{
		return false;
	}

	return ResolveCutOperationToLocalPolygon(CutOperations[CutOperationIndex], OutLocalLoop)
		&& OutLocalLoop.Num() >= 3;
}

const TArray<FVector>* AEHB_FloorSlab::GetEditableLoop(int32 LoopIndex) const
{
	if (LoopIndex == INDEX_NONE)
	{
		return &LocalTopPolygon;
	}
	if (LocalHoles.IsValidIndex(LoopIndex))
	{
		return &LocalHoles[LoopIndex].LocalPolygon;
	}
	return nullptr;
}

bool AEHB_FloorSlab::GetEditableLoopCopy(int32 LoopIndex, TArray<FVector>& OutLocalLoop) const
{
	OutLocalLoop.Reset();
	if (LoopIndex == INDEX_NONE)
	{
		OutLocalLoop = LocalTopPolygon;
		return OutLocalLoop.Num() >= 3;
	}
	if (LocalHoles.IsValidIndex(LoopIndex))
	{
		OutLocalLoop = LocalHoles[LoopIndex].LocalPolygon;
		return OutLocalLoop.Num() >= 3;
	}
	if (IsCutOperationLoopIndex(LoopIndex))
	{
		return GetEditableCutOperationLoop(GetCutOperationIndexFromLoopIndex(LoopIndex), OutLocalLoop);
	}
	return false;
}

TArray<FVector>* AEHB_FloorSlab::GetMutableEditableLoop(int32 LoopIndex)
{
	if (LoopIndex == INDEX_NONE)
	{
		return &LocalTopPolygon;
	}
	if (LocalHoles.IsValidIndex(LoopIndex))
	{
		return &LocalHoles[LoopIndex].LocalPolygon;
	}
	return nullptr;
}

FEHBCutOperation* AEHB_FloorSlab::GetEditableCutOperationByLoopIndex(int32 LoopIndex)
{
	const int32 CutOperationIndex = GetCutOperationIndexFromLoopIndex(LoopIndex);
	return CutOperations.IsValidIndex(CutOperationIndex) ? &CutOperations[CutOperationIndex] : nullptr;
}

const FEHBCutOperation* AEHB_FloorSlab::GetEditableCutOperationByLoopIndex(int32 LoopIndex) const
{
	const int32 CutOperationIndex = GetCutOperationIndexFromLoopIndex(LoopIndex);
	return CutOperations.IsValidIndex(CutOperationIndex) ? &CutOperations[CutOperationIndex] : nullptr;
}

bool AEHB_FloorSlab::EnsureCutOperationEditablePolygon(FEHBCutOperation& Operation)
{
	const bool bAlreadyEditable =
		Operation.Source.SourceType == EEHBCutSourceType::ExplicitPolygon
		&& Operation.Source.PrimitiveShape == EEHBCutPrimitiveShape::Polygon
		&& Operation.Source.LocalTransform.Equals(FTransform::Identity, KINDA_SMALL_NUMBER)
		&& Operation.Source.ExplicitPolygon.Points.Num() >= 3;
	if (bAlreadyEditable)
	{
		Operation.EnsureGuids();
		return true;
	}

	TArray<FVector> ResolvedPolygon;
	if (!ResolveCutOperationToLocalPolygon(Operation, ResolvedPolygon) || ResolvedPolygon.Num() < 3)
	{
		return false;
	}

	Operation.Source.SourceType = EEHBCutSourceType::ExplicitPolygon;
	Operation.Source.PrimitiveShape = EEHBCutPrimitiveShape::Polygon;
	Operation.Source.LocalTransform = FTransform::Identity;
	Operation.Source.Height = FMath::Max(1.0f, Operation.Source.Height);
	Operation.Source.ExplicitPolygon.Points.Reset(ResolvedPolygon.Num());
	for (const FVector& LocalPoint : ResolvedPolygon)
	{
		FEHBCutPolygonPoint& Point = Operation.Source.ExplicitPolygon.Points.AddDefaulted_GetRef();
		Point.LocalPosition = LocalPoint;
		Point.EnsureGuid();
	}
	Operation.EnsureGuids();
	return true;
}

TArray<FVector> AEHB_FloorSlab::BuildCutterLocalPolygon(const FEHBFloorSlabCutterData& CutterData) const
{
	const float HalfSize = FMath::Max(1.0f, CutterData.Size) * 0.5f;
	TArray<FVector> LocalPoints;
	if (CutterData.Shape == EEHBFloorSlabCutterShape::Circle)
	{
		const int32 SideCount = FMath::Clamp(CutterData.CircleSideCount, 8, 96);
		LocalPoints.Reserve(SideCount);
		for (int32 Index = 0; Index < SideCount; ++Index)
		{
			const float Angle = 2.0f * PI * static_cast<float>(Index) / static_cast<float>(SideCount);
			LocalPoints.Add(FVector(FMath::Cos(Angle) * HalfSize, FMath::Sin(Angle) * HalfSize, 0.0f));
		}
	}
	else
	{
		LocalPoints = {
			FVector(-HalfSize, -HalfSize, 0.0f),
			FVector(-HalfSize, HalfSize, 0.0f),
			FVector(HalfSize, HalfSize, 0.0f),
			FVector(HalfSize, -HalfSize, 0.0f)
		};
	}

	TArray<FVector> Result;
	Result.Reserve(LocalPoints.Num());
	for (const FVector& Point : LocalPoints)
	{
		FVector TransformedPoint = CutterData.LocalTransform.TransformPosition(Point);
		TransformedPoint.Z = GetTopZ();
		Result.Add(TransformedPoint);
	}
	return Result;
}

TArray<TArray<FVector>> AEHB_FloorSlab::BuildPreviewHolePolygons() const
{
	FEHBPolygonClipResult ClipResult;
	if (BuildCutGeometry(ClipResult, true))
	{
		TArray<TArray<FVector>> Result;
		for (const FEHBPolygonRegion& Region : ClipResult.Regions)
		{
			for (const FEHBCutPolygon& HoleLoop : Region.HoleLoops)
			{
				TArray<FVector> HolePolygon = HoleLoop.ToLocalPositions();
				if (HolePolygon.Num() >= 3)
				{
					Result.Add(MoveTemp(HolePolygon));
				}
			}
		}
		if (!Result.IsEmpty())
		{
			return Result;
		}
	}

	TArray<TArray<FVector>> CutPolygons;
	GatherHorizontalCutPolygons(CutPolygons, true);
	return CutPolygons;
}

bool AEHB_FloorSlab::BuildDisplayRegions(const FEHBPolygonClipResult& ClipResult,bool bIncludeVisualExpansion,
 TArray<FEHBPlanarSurfaceRegion>& OutRegions,const FEHBSlabDisplayPartition* OverrideDisplay) const
{
 OutRegions.Reset();const auto& Partition=OverrideDisplay?*OverrideDisplay:DisplayPartition;
 if(bIncludeVisualExpansion&&Partition.IsActive())
 {
  for(const auto& R:Partition.Regions){auto& Region=OutRegions.AddDefaulted_GetRef();for(const auto& P:R.Boundary)Region.BoundaryLoop.Add(FVector(P.X,P.Y,GetTopZ()));for(const auto& H:R.Holes){auto& Hole=Region.HoleLoops.AddDefaulted_GetRef().LocalLoop;for(const auto& P:H.Vertices)Hole.Add(FVector(P.X,P.Y,GetTopZ()));}}
  return !OutRegions.IsEmpty();
 }
 const float Expansion=bIncludeVisualExpansion?FMath::Max(0.f,VisualExpansion):0.f;
 for(const auto& R:ClipResult.Regions)
 {
  if(R.OuterLoop.Num()<3)return false;
  auto& Region=OutRegions.AddDefaulted_GetRef();Region.BoundaryLoop=R.OuterLoop;
  for(auto& P:Region.BoundaryLoop)P.Z=GetTopZ();
  for(const auto& H:R.HoleLoops){auto& Hole=Region.HoleLoops.AddDefaulted_GetRef().LocalLoop;Hole=H.ToLocalPositions();for(auto& P:Hole)P.Z=GetTopZ();}
  if(Expansion>UE_KINDA_SMALL_NUMBER){TArray<FVector> Expanded;if(!OffsetFloorSlabPolygon(Region.BoundaryLoop,Expansion,Expanded))return false;Region.BoundaryLoop=MoveTemp(Expanded);}
 }
 // Expanded pieces may meet. Union the filled regions to avoid duplicate caps
 // and internal side walls; preserve holes and nested islands in the tree.
 if(Expansion>UE_KINDA_SMALL_NUMBER&&OutRegions.Num()>1)
 {
  ClipperLib::Clipper Clipper;Clipper.PreserveCollinear(true);
  auto Add=[&](const TArray<FVector>& Loop,bool Positive){auto Path=ToFloorSlabClipperPath(Loop);if(ClipperLib::Orientation(Path)!=Positive)std::reverse(Path.begin(),Path.end());return Clipper.AddPath(Path,ClipperLib::ptSubject,true);};
  for(const auto& R:OutRegions){if(!Add(R.BoundaryLoop,true))return false;for(const auto& H:R.HoleLoops)if(!Add(H.LocalLoop,false))return false;}
  ClipperLib::PolyTree Tree;if(!Clipper.Execute(ClipperLib::ctUnion,Tree,ClipperLib::pftNonZero,ClipperLib::pftNonZero))return false;
  OutRegions.Reset();
  for(auto* Node=Tree.GetFirst();Node;Node=Node->GetNext())if(!Node->IsHole())
  {
   auto& R=OutRegions.AddDefaulted_GetRef();R.BoundaryLoop=FromFloorSlabClipperPath(Node->Contour,GetTopZ());
   for(auto* Hole:Node->Childs)if(Hole->IsHole())R.HoleLoops.AddDefaulted_GetRef().LocalLoop=FromFloorSlabClipperPath(Hole->Contour,GetTopZ());
  }
 }
 return !OutRegions.IsEmpty();
}

bool AEHB_FloorSlab::BuildEffectiveDisplayRegions(TArray<FEHBPlanarSurfaceRegion>& OutRegions,bool bIncludeVisualExpansion,bool bIncludePreviewCutters) const
{
 OutRegions.Reset();if(!CheckDisplayPartition(LocalTopPolygon,LocalHoles,DisplayPartition,CutOperations,PreviewCutters))return false;
 FEHBPolygonClipResult Clip;if(!BuildCutGeometry(Clip,bIncludePreviewCutters))return false;
 return BuildDisplayRegions(Clip,bIncludeVisualExpansion,OutRegions);
}

bool AEHB_FloorSlab::BuildEffectiveOuterPolygon(TArray<FVector>& OutLocalPolygon,bool bIncludeVisualExpansion,bool bIncludePreviewCutters) const
{
 OutLocalPolygon.Reset();TArray<FEHBPlanarSurfaceRegion> Regions;
 if(!BuildEffectiveDisplayRegions(Regions,bIncludeVisualExpansion,bIncludePreviewCutters)||Regions.Num()!=1)return false;
 OutLocalPolygon=MoveTemp(Regions[0].BoundaryLoop);return true;
}

bool AEHB_FloorSlab::BuildEffectiveOuterWorldPolygons(TArray<TArray<FVector>>& OutWorldPolygons,bool bIncludeVisualExpansion,bool bIncludePreviewCutters) const
{
 OutWorldPolygons.Reset();TArray<FEHBPlanarSurfaceRegion> Regions;if(!BuildEffectiveDisplayRegions(Regions,bIncludeVisualExpansion,bIncludePreviewCutters))return false;
 for(const auto& R:Regions){auto& Loop=OutWorldPolygons.AddDefaulted_GetRef();for(const auto& P:R.BoundaryLoop)Loop.Add(GetActorTransform().TransformPosition(P));}return !OutWorldPolygons.IsEmpty();
}

bool AEHB_FloorSlab::BuildEffectiveOuterWorldPolygon(TArray<FVector>& OutWorldPolygon, bool bIncludeVisualExpansion, bool bIncludePreviewCutters) const
{
	TArray<FVector> LocalPolygon;
	if (!BuildEffectiveOuterPolygon(LocalPolygon, bIncludeVisualExpansion, bIncludePreviewCutters))
	{
		OutWorldPolygon.Reset();
		return false;
	}

	OutWorldPolygon.Reset(LocalPolygon.Num());
	const FTransform ActorTransform = GetActorTransform();
	for (const FVector& LocalPoint : LocalPolygon)
	{
		OutWorldPolygon.Add(ActorTransform.TransformPosition(LocalPoint));
	}
	return true;
}

bool AEHB_FloorSlab::ValidateCutStateSources(const TArray<FEHBCutOperation>& Cuts,const TArray<FEHBFloorSlabCutterData>& Preview) const
{
 TSet<FGuid> Ids;
 for(const auto& Cut:Cuts)
 {
  if(!Cut.OperationGuid.IsValid()||Ids.Contains(Cut.OperationGuid))return false;Ids.Add(Cut.OperationGuid);
  if(!Cut.bEnabled)continue;
  if(Cut.SurfaceHost.Version!=0){TArray<FVector> Polygon;if(!ResolveCutOperationToLocalPolygon(Cut,Polygon))return false;continue;}
  const auto& Source=Cut.Source;
  if(Source.LocalTransform.ContainsNaN()||!FMath::IsFinite(Source.Size)||!FMath::IsFinite(Source.Height)||Source.Size<=0||Source.Height<=0)return false;
  // Validate the source shape independently of whether its depth currently
  // reaches this slab. Moving a valid source away must be able to restore it.
  auto Shape=Cut;auto Location=Shape.Source.LocalTransform.GetLocation();Location.Z=(GetTopZ()+GetBottomZ())*0.5;
  Shape.Source.LocalTransform.SetLocation(Location);
  TArray<FVector> Polygon;if(!ResolveCutOperationToLocalPolygon(Shape,Polygon))return false;
  if(Polygon.ContainsByPredicate([](const FVector& P){return P.ContainsNaN()||FMath::Abs(P.X)>1.e8||FMath::Abs(P.Y)>1.e8;})||FMath::Abs(CalculateTwiceArea(To2DLoop(Polygon)))<=UE_DOUBLE_SMALL_NUMBER)return false;
 }
 for(const auto& Cut:Preview)if(Cut.LocalTransform.ContainsNaN()||!FMath::IsFinite(Cut.Size)||!FMath::IsFinite(Cut.Height)||Cut.Size<=0||Cut.Height<=0)return false;
 return true;
}

bool AEHB_FloorSlab::ValidateCutOperations(const TArray<FEHBCutOperation>& CandidateCuts,bool bDiscardPreviewCutters) const
{
 TArray<FEHBFloorSlabCutterData> Empty;const auto& Preview=bDiscardPreviewCutters?Empty:PreviewCutters;
 FEHBSurfaceMeshBuildResult Mesh;TArray<FVector> Polygon;TArray<TArray<FVector>> Holes;
 return BuildPreparedSlabMesh(LocalTopPolygon,LocalHoles,true,true,Mesh,Polygon,Holes,nullptr,nullptr,&CandidateCuts,&Preview);
}

bool AEHB_FloorSlab::SetCutState(const TArray<FEHBCutOperation>& CandidateCuts,const TArray<FEHBFloorSlabCutterData>& CandidatePreview)
{
 FEHBSurfaceMeshBuildResult Mesh;TArray<FVector> Polygon;TArray<TArray<FVector>> Holes;TArray<FEHBPlanarSurfaceRegion> Regions;
 if(!BuildPreparedSlabMesh(LocalTopPolygon,LocalHoles,true,true,Mesh,Polygon,Holes,nullptr,&Regions,&CandidateCuts,&CandidatePreview))return false;
 SetFlags(RF_Transactional);Modify();for(auto* Component:GetComponents()){Component->SetFlags(RF_Transactional);Component->Modify();}
 CutOperations=CandidateCuts;PreviewCutters=CandidatePreview;ApplyPreparedSlabMesh(Mesh,Regions);
 MarkPackageDirty();NotifyElementGeometryChanged(true);return true;
}

bool AEHB_FloorSlab::AddCutOperation(FEHBCutOperation Operation)
{
 Operation.EnsureGuids();auto Candidate=CutOperations;Candidate.Add(MoveTemp(Operation));return SetCutState(Candidate,PreviewCutters);
}

bool AEHB_FloorSlab::RemoveCutOperation(const FGuid& OperationGuid)
{
 if(!OperationGuid.IsValid())return false;auto Candidate=CutOperations;
 if(Candidate.RemoveAll([&](const auto& V){return V.OperationGuid==OperationGuid;})!=1)return false;
 return SetCutState(Candidate,PreviewCutters);
}

bool AEHB_FloorSlab::UpdateCutOperation(const FEHBCutOperation& Operation)
{
 if(!Operation.OperationGuid.IsValid())return false;auto Candidate=CutOperations;
 auto* Existing=Candidate.FindByPredicate([&](const auto& V){return V.OperationGuid==Operation.OperationGuid;});if(!Existing)return false;
 *Existing=Operation;Existing->EnsureGuids();return SetCutState(Candidate,PreviewCutters);
}

FEHBCutOperation* AEHB_FloorSlab::FindCutOperation(const FGuid& OperationGuid)
{
	if (!OperationGuid.IsValid())
	{
		return nullptr;
	}
	return CutOperations.FindByPredicate(
		[OperationGuid](const FEHBCutOperation& Operation)
		{
			return Operation.OperationGuid == OperationGuid;
		});
}

const FEHBCutOperation* AEHB_FloorSlab::FindCutOperation(const FGuid& OperationGuid) const
{
	if (!OperationGuid.IsValid())
	{
		return nullptr;
	}
	return CutOperations.FindByPredicate(
		[OperationGuid](const FEHBCutOperation& Operation)
		{
			return Operation.OperationGuid == OperationGuid;
		});
}

bool AEHB_FloorSlab::RemoveCutPolygonPoint(const FGuid& OperationGuid,const FGuid& PointGuid)
{
 const auto* Existing=FindCutOperation(OperationGuid);if(!Existing||!PointGuid.IsValid())return false;auto Candidate=*Existing;
 if(Candidate.Source.ExplicitPolygon.Points.RemoveAll([&](const auto& P){return P.PointGuid==PointGuid;})!=1)return false;
 if(Candidate.Source.ExplicitPolygon.Points.Num()<3)return RemoveCutOperation(OperationGuid);
 return UpdateCutOperation(Candidate);
}

bool AEHB_FloorSlab::UpdateCutPolygonPoint(const FGuid& OperationGuid,const FGuid& PointGuid,const FVector& NewLocalPosition)
{
 const auto* Existing=FindCutOperation(OperationGuid);if(!Existing||!PointGuid.IsValid())return false;auto Candidate=*Existing;
 auto* Point=Candidate.Source.ExplicitPolygon.Points.FindByPredicate([&](const auto& P){return P.PointGuid==PointGuid;});if(!Point)return false;
 Point->LocalPosition=NewLocalPosition;return UpdateCutOperation(Candidate);
}

bool AEHB_FloorSlab::SnapFoundationBottomToGround(float TraceDistance)
{
	if (!bIsFoundation || !bKeepFoundationBottomOnGround)
	{
		return false;
	}

	TArray<FEHBPlanarSurfaceRegion> Regions;
	if (!BuildEffectiveDisplayRegions(Regions, true, true))
	{
		return false;
	}

	const FTransform ActorTransform = GetActorTransform();
	const float SampleSpacing = FMath::Max(10.0f, FoundationGroundTraceSpacing);
	bool bFoundGround = false;
	double RequiredDeltaZ = -TNumericLimits<double>::Max();
	const double MinimumSideHeight = FMath::Max(1.0f, Thickness);

	for (const auto& Region : Regions)
	{
	const auto& LocalPolygon = Region.BoundaryLoop;
	for (int32 PointIndex = 0; PointIndex < LocalPolygon.Num(); ++PointIndex)
	{
		FVector LocalStart = LocalPolygon[PointIndex];
		FVector LocalEnd = LocalPolygon[(PointIndex + 1) % LocalPolygon.Num()];
		LocalStart.Z = GetTopZ();
		LocalEnd.Z = GetTopZ();
		const float EdgeLength = FVector::Dist2D(LocalStart, LocalEnd);
		const int32 SegmentCount = FMath::Clamp(FMath::CeilToInt(EdgeLength / SampleSpacing), 1, 512);

		for (int32 SegmentIndex = 0; SegmentIndex <= SegmentCount; ++SegmentIndex)
		{
			// Do not trace the first point twice; it was the preceding edge's last sample.
			if (PointIndex > 0 && SegmentIndex == 0)
			{
				continue;
			}

			const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
			const FVector WorldSample = ActorTransform.TransformPosition(FMath::Lerp(LocalStart, LocalEnd, Alpha));
			float GroundZ = 0.0f;
			if (TraceGroundWorldZAt(WorldSample, TraceDistance, GroundZ))
			{
				bFoundGround = true;
				RequiredDeltaZ = FMath::Max(
					RequiredDeltaZ,
					static_cast<double>(GroundZ) + MinimumSideHeight - WorldSample.Z);
			}
		}
	}

	}
	if (!bFoundGround)
	{
		return false;
	}

	const double DeltaZ = RequiredDeltaZ;
	if (FMath::Abs(DeltaZ) <= 0.1f)
	{
		return false;
	}

	Modify();
	AddActorWorldOffset(FVector(0.0f, 0.0f, DeltaZ), false, nullptr, ETeleportType::TeleportPhysics);
	return true;
}

bool AEHB_FloorSlab::SnapSideToAdjacentFloorSlab(float MaxAngleDegrees)
{
	if (!bEnableAdjacentSlabSnap)
	{
		return false;
	}

	TArray<TArray<FVector>> MovedPolygons;
	if (!BuildEffectiveOuterWorldPolygons(MovedPolygons, true, true))
	{
		return false;
	}

	TArray<FEHBFloorSlabWorldEdge> MovedEdges;
	for (const auto& Polygon : MovedPolygons)
	{
		TArray<FEHBFloorSlabWorldEdge> Edges;
		if (!BuildFloorSlabWorldEdges(Polygon, Edges)) return false;
		MovedEdges.Append(Edges);
	}
	if (MovedEdges.IsEmpty()) return false;

	float MovedMinZ = 0.0f;
	float MovedMaxZ = 0.0f;
	GetApproxWorldVerticalRange(MovedMinZ, MovedMaxZ);

	const float SafeAngleDegrees = FMath::Clamp(MaxAngleDegrees, 0.0f, 89.0f);
	const float MinEdgeAlignment = FMath::Cos(FMath::DegreesToRadians(SafeAngleDegrees));
	const float MinProjectionOverlap = 5.0f;
	const float VerticalTolerance = 2.0f;
	bool bHasBestCandidate = false;
	FEHBFloorSlabSnapCandidate BestCandidate;
	const FVector ActorPivot = GetActorLocation();

	TArray<FEHBSurfaceSideSnapEdge> TargetEdges;
	GatherSurfaceSideSnapEdgesForSlab(this, TargetEdges);
	const float NearSnapDistance = FMath::Clamp(FMath::Max(1.0f, Thickness) + 10.0f, 25.0f, 120.0f);
	const float OverlapSnapDistance = FMath::Max(NearSnapDistance, 250.0f);
	const float CornerSnapDistance = FMath::Clamp(FMath::Max(1.0f, Thickness) + 25.0f, 40.0f, 160.0f);

	for (const FEHBFloorSlabWorldEdge& MovedEdge : MovedEdges)
	{
		for (const FEHBSurfaceSideSnapEdge& TargetEdge : TargetEdges)
		{
			if (!TargetEdge.IsValid()
				|| !DoVerticalRangesOverlap(MovedMinZ, MovedMaxZ, TargetEdge.MinWorldZ, TargetEdge.MaxWorldZ, VerticalTolerance))
			{
				continue;
			}

			const float DirectionDot = DotXY(MovedEdge.Direction, TargetEdge.WorldDirection);
			if (FMath::Abs(DirectionDot) < MinEdgeAlignment)
			{
				continue;
			}

			if (DotXY(MovedEdge.OutwardNormal, TargetEdge.WorldOutwardNormal) > -0.5f)
			{
				continue;
			}

			const FVector DesiredMovedDirection = DirectionDot >= 0.0f
				? TargetEdge.WorldDirection
				: -TargetEdge.WorldDirection;
			const float YawDeltaRadians = FMath::Atan2(
				CrossXY(MovedEdge.Direction, DesiredMovedDirection),
				DotXY(MovedEdge.Direction, DesiredMovedDirection));
			const FVector RotatedMovedStart = RotatePointAroundPivotXY(MovedEdge.Start, ActorPivot, YawDeltaRadians);
			const FVector RotatedMovedEnd = RotatePointAroundPivotXY(MovedEdge.End, ActorPivot, YawDeltaRadians);
			const FVector RotatedMovedMidpoint = (RotatedMovedStart + RotatedMovedEnd) * 0.5f;

			const float MovedStartProjection = DotXY(MovedEdge.Start, TargetEdge.WorldDirection);
			const float MovedEndProjection = DotXY(MovedEdge.End, TargetEdge.WorldDirection);
			const float TargetStartProjection = DotXY(TargetEdge.WorldStart, TargetEdge.WorldDirection);
			const float TargetEndProjection = DotXY(TargetEdge.WorldEnd, TargetEdge.WorldDirection);
			const float ProjectionOverlap = GetIntervalOverlap(
				MovedStartProjection,
				MovedEndProjection,
				TargetStartProjection,
				TargetEndProjection);

			const float SignedDistance = DotXY(MovedEdge.Midpoint - TargetEdge.WorldMidpoint, TargetEdge.WorldOutwardNormal);
			const float AbsDistance = FMath::Abs(SignedDistance);
			const bool bPolygonsOverlap = MovedPolygons.ContainsByPredicate([&](const TArray<FVector>& Polygon)
			{ return DoesTargetEdgeOwnerOverlapCandidatePolygon(TargetEdge, Polygon); });
			if (!bPolygonsOverlap && AbsDistance > NearSnapDistance)
			{
				continue;
			}
			if (bPolygonsOverlap && AbsDistance > OverlapSnapDistance)
			{
				continue;
			}

			FVector CandidateDelta = -SignedDistance * TargetEdge.WorldOutwardNormal;
			CandidateDelta.Z = 0.0f;
			const float RotatedSignedDistance = DotXY(RotatedMovedMidpoint - TargetEdge.WorldMidpoint, TargetEdge.WorldOutwardNormal);
			FVector RotatedLineDelta = -RotatedSignedDistance * TargetEdge.WorldOutwardNormal;
			RotatedLineDelta.Z = 0.0f;
			bool bAlignsCorner = false;
			float BestCornerDistance = TNumericLimits<float>::Max();
			FVector BestCornerDelta = CandidateDelta;
			const bool bSameDirection = DirectionDot >= 0.0f;
			const TPair<FVector, FVector> CornerPairs[] = {
				TPair<FVector, FVector>(RotatedMovedStart + RotatedLineDelta, bSameDirection ? TargetEdge.WorldStart : TargetEdge.WorldEnd),
				TPair<FVector, FVector>(RotatedMovedEnd + RotatedLineDelta, bSameDirection ? TargetEdge.WorldEnd : TargetEdge.WorldStart)
			};
			for (const TPair<FVector, FVector>& CornerPair : CornerPairs)
			{
				const float CornerDistance = FVector::Dist2D(CornerPair.Key, CornerPair.Value);
				if (CornerDistance <= CornerSnapDistance && CornerDistance < BestCornerDistance)
				{
					bAlignsCorner = true;
					BestCornerDistance = CornerDistance;
					BestCornerDelta = CornerPair.Value - (CornerPair.Key - RotatedLineDelta);
					BestCornerDelta.Z = 0.0f;
				}
			}

			if (!bAlignsCorner && ProjectionOverlap < MinProjectionOverlap)
			{
				continue;
			}

			float CandidateYawDeltaRadians = 0.0f;
			if (bAlignsCorner)
			{
				CandidateDelta = BestCornerDelta;
				CandidateYawDeltaRadians = YawDeltaRadians;
			}

			if (CandidateDelta.X * CandidateDelta.X + CandidateDelta.Y * CandidateDelta.Y <= 0.01f
				&& FMath::Abs(CandidateYawDeltaRadians) <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const float AbsYawDeltaDegrees = FMath::Abs(FMath::RadiansToDegrees(CandidateYawDeltaRadians));
			const float Score = AbsDistance
				+ AbsYawDeltaDegrees * 0.5f
				- FMath::Max(0.0f, FMath::Min(ProjectionOverlap, 500.0f)) * 0.01f
				+ (bPolygonsOverlap ? 0.0f : 25.0f)
				+ (bAlignsCorner ? BestCornerDistance * 0.1f - 50.0f : 0.0f);
			if (!bHasBestCandidate || Score < BestCandidate.Score)
			{
				bHasBestCandidate = true;
				BestCandidate.Score = Score;
				BestCandidate.YawDeltaRadians = CandidateYawDeltaRadians;
				BestCandidate.WorldDelta = CandidateDelta;
				BestCandidate.bAlignsCorner = bAlignsCorner;
			}
		}
	}

	if (!bHasBestCandidate)
	{
		return false;
	}

	Modify();
	if (FMath::Abs(BestCandidate.YawDeltaRadians) > UE_KINDA_SMALL_NUMBER)
	{
		const FQuat DeltaRotation(FVector::UpVector, BestCandidate.YawDeltaRadians);
		SetActorRotation(DeltaRotation * GetActorQuat(), ETeleportType::TeleportPhysics);
	}
	AddActorWorldOffset(BestCandidate.WorldDelta, false, nullptr, ETeleportType::TeleportPhysics);
	return true;
}

bool AEHB_FloorSlab::BuildCutGeometry(FEHBPolygonClipResult& OutClipResult, bool bIncludePreviewCutters,
	const TArray<FVector>* OverridePolygon, const TArray<FEHBFloorSlabHole>* OverrideHoles, const TArray<FEHBCutOperation>* OverrideCuts, const TArray<FEHBFloorSlabCutterData>* OverridePreview) const
{
	OutClipResult.Reset();
	const auto& Polygon = OverridePolygon ? *OverridePolygon : LocalTopPolygon;
	if (Polygon.Num() < 3)
	{
		return false;
	}

	TArray<TArray<FVector>> CutPolygons;
 TArray<FEHBCutOperation> Resolved;if(!ResolveSurfaceOpeningCuts(OverrideCuts?*OverrideCuts:CutOperations,Polygon,OverrideHoles?*OverrideHoles:LocalHoles,Resolved))return false;
	GatherHorizontalCutPolygons(CutPolygons, bIncludePreviewCutters, OverrideHoles, &Resolved, OverridePreview);
	auto InvalidPoint=[](const FVector& P){return P.ContainsNaN() || FMath::Abs(P.X)>1.e8 || FMath::Abs(P.Y)>1.e8;};
	if(Polygon.ContainsByPredicate(InvalidPoint))return false;
	for(const auto& Cut:CutPolygons)if(Cut.ContainsByPredicate(InvalidPoint))return false;
	return FEHBPolygonClipper::CutHorizontalByPolygon(Polygon, CutPolygons, OutClipResult, EHBFloorSlabClipperScale);
}

bool AEHB_FloorSlab::ResolveCutOperationToLocalPolygon(
	const FEHBCutOperation& Operation,
	TArray<FVector>& OutLocalPolygon) const
{
	OutLocalPolygon.Reset();
	if(Operation.SurfaceHost.Version!=0)
 {
  TArray<FEHBCutOperation> Resolved;if(!ResolveSurfaceOpeningCuts({Operation},LocalTopPolygon,LocalHoles,Resolved)||Resolved.Num()!=1)return false;
  return ResolveCutOperationToLocalPolygon(Resolved[0],OutLocalPolygon);
 }
	if (!Operation.bEnabled
		|| Operation.Stage != EEHBCutStage::Profile
		|| Operation.ProjectionMode != EEHBCutProjectionMode::HorizontalXY
		|| Operation.OperationType != EEHBCutOperationType::Subtract)
	{
		return false;
	}

	const FEHBCutSource& Source = Operation.Source;
	const float CutterCenterZ = Source.LocalTransform.GetLocation().Z;
	const float CutterHalfHeight = FMath::Max(1.0f, Source.Height)
		* FMath::Max(0.05f, FMath::Abs(Source.LocalTransform.GetScale3D().Z))
		* 0.5f;
	if (CutterCenterZ - CutterHalfHeight > GetTopZ() || CutterCenterZ + CutterHalfHeight < GetBottomZ())
	{
		return false;
	}

	if (Source.SourceType == EEHBCutSourceType::ExplicitPolygon
		|| Source.PrimitiveShape == EEHBCutPrimitiveShape::Polygon)
	{
		const TArray<FVector> SourcePoints = Source.ExplicitPolygon.ToLocalPositions();
		if (SourcePoints.Num() < 3)
		{
			return false;
		}

		OutLocalPolygon.Reserve(SourcePoints.Num());
		for (const FVector& Point : SourcePoints)
		{
			FVector TransformedPoint = Source.LocalTransform.TransformPosition(Point);
			TransformedPoint.Z = GetTopZ();
			OutLocalPolygon.Add(TransformedPoint);
		}
		return OutLocalPolygon.Num() >= 3;
	}

	if (Source.SourceType == EEHBCutSourceType::ExplicitPrism)
	{
		FEHBFloorSlabCutterData LegacyCutter;
		LegacyCutter.Shape = Source.PrimitiveShape == EEHBCutPrimitiveShape::Circle
			? EEHBFloorSlabCutterShape::Circle
			: EEHBFloorSlabCutterShape::Square;
		LegacyCutter.LocalTransform = Source.LocalTransform;
		LegacyCutter.Size = Source.Size;
		LegacyCutter.Height = Source.Height;
		LegacyCutter.CircleSideCount = Source.CircleSideCount;
		OutLocalPolygon = BuildCutterLocalPolygon(LegacyCutter);
		return OutLocalPolygon.Num() >= 3;
	}

	return false;
}

void AEHB_FloorSlab::GatherHorizontalCutPolygons(TArray<TArray<FVector>>& OutCutPolygons, bool bIncludePreviewCutters, const TArray<FEHBFloorSlabHole>* OverrideHoles, const TArray<FEHBCutOperation>* OverrideCuts, const TArray<FEHBFloorSlabCutterData>* OverridePreview) const
{
	OutCutPolygons.Reset();
 const auto& CandidateCuts=OverrideCuts?*OverrideCuts:CutOperations;
 const auto& CandidatePreview=OverridePreview?*OverridePreview:PreviewCutters;
	OutCutPolygons.Reserve(CandidateCuts.Num() + (OverrideHoles ? OverrideHoles->Num() : LocalHoles.Num()) + (bIncludePreviewCutters ? CandidatePreview.Num() : 0));

	TArray<const FEHBCutOperation*> SortedOperations;
	SortedOperations.Reserve(CandidateCuts.Num());
	for (const FEHBCutOperation& Operation : CandidateCuts)
	{
		if (Operation.bEnabled)
		{
			SortedOperations.Add(&Operation);
		}
	}
	SortedOperations.Sort([](const FEHBCutOperation& Left, const FEHBCutOperation& Right)
	{
		return Left.Priority < Right.Priority;
	});

	for (const FEHBCutOperation* Operation : SortedOperations)
	{
		TArray<FVector> CutPolygon;
		if (Operation && ResolveCutOperationToLocalPolygon(*Operation, CutPolygon) && CutPolygon.Num() >= 3)
		{
			OutCutPolygons.Add(MoveTemp(CutPolygon));
		}
	}

	// Deprecated compatibility input. Persistent edits should use CutOperations.
	for (const FEHBFloorSlabHole& Hole : (OverrideHoles ? *OverrideHoles : LocalHoles))
	{
		if (Hole.LocalPolygon.Num() >= 3)
		{
			OutCutPolygons.Add(Hole.LocalPolygon);
		}
	}

	if (!bIncludePreviewCutters)
	{
		return;
	}

	for (const FEHBFloorSlabCutterData& CutterData : CandidatePreview)
	{
		const float CutterCenterZ = CutterData.LocalTransform.GetLocation().Z;
		const float CutterHalfHeight = FMath::Max(1.0f, CutterData.Height)
			* FMath::Max(0.05f, FMath::Abs(CutterData.LocalTransform.GetScale3D().Z))
			* 0.5f;
		if (CutterCenterZ - CutterHalfHeight > GetTopZ() || CutterCenterZ + CutterHalfHeight < GetBottomZ())
		{
			continue;
		}

		TArray<FVector> CutterPolygon = BuildCutterLocalPolygon(CutterData);
		if (CutterPolygon.Num() >= 3)
		{
			OutCutPolygons.Add(MoveTemp(CutterPolygon));
		}
	}
}

FEHBCutOperation AEHB_FloorSlab::MakePrimitiveCutOperation(EEHBCutPrimitiveShape Shape) const
{
	FVector LocalCenter = FVector::ZeroVector;
	for (const FVector& Point : LocalTopPolygon)
	{
		LocalCenter += Point;
	}
	if (!LocalTopPolygon.IsEmpty())
	{
		LocalCenter /= static_cast<float>(LocalTopPolygon.Num());
	}
	LocalCenter.Z = (GetTopZ() + GetBottomZ()) * 0.5f;

	FEHBCutOperation Operation;
	Operation.OperationGuid = FGuid::NewGuid();
	Operation.bEnabled = true;
	Operation.OperationType = EEHBCutOperationType::Subtract;
	Operation.Stage = EEHBCutStage::Profile;
	Operation.ProjectionMode = EEHBCutProjectionMode::HorizontalXY;
	Operation.TransformPolicy = EEHBCutTransformPolicy::TargetLocal;
	Operation.Source.SourceType = EEHBCutSourceType::ExplicitPrism;
	Operation.Source.PrimitiveShape = Shape;
	Operation.Source.LocalTransform = FTransform(LocalCenter);
	Operation.Source.Size = 160.0f;
	Operation.Source.Height = FMath::Max(Thickness + 80.0f, 120.0f);
	Operation.Source.CircleSideCount = 32;
	Operation.EnsureGuids();
	return Operation;
}

float AEHB_FloorSlab::GetTopZ() const
{
	return 0.0f;
}

float AEHB_FloorSlab::GetBottomZ() const
{
	return -FMath::Max(1.0f, Thickness);
}

void AEHB_FloorSlab::GetApproxWorldVerticalRange(float& OutMinZ, float& OutMaxZ) const
{
	OutMinZ = TNumericLimits<float>::Max();
	OutMaxZ = -TNumericLimits<float>::Max();

	TArray<FEHBPlanarSurfaceRegion> Regions;
	if (BuildEffectiveDisplayRegions(Regions,true,true))
	{
		const FTransform ActorTransform = GetActorTransform();
		for(const auto& Region:Regions) for (FVector LocalPoint : Region.BoundaryLoop)
		{
			LocalPoint.Z = GetTopZ();
			const float TopWorldZ = ActorTransform.TransformPosition(LocalPoint).Z;
			OutMinZ = FMath::Min(OutMinZ, TopWorldZ);
			OutMaxZ = FMath::Max(OutMaxZ, TopWorldZ);

			LocalPoint.Z = (bIsFoundation && bKeepFoundationBottomOnGround)
				? ResolveGroundLocalZAt(LocalPoint)
				: GetBottomZ();
			const float BottomWorldZ = ActorTransform.TransformPosition(LocalPoint).Z;
			OutMinZ = FMath::Min(OutMinZ, BottomWorldZ);
			OutMaxZ = FMath::Max(OutMaxZ, BottomWorldZ);
		}
	}

	if (OutMinZ > OutMaxZ)
	{
		const float TopWorldZ = GetActorTransform().TransformPosition(FVector(0.0f, 0.0f, GetTopZ())).Z;
		const float BottomWorldZ = GetActorTransform().TransformPosition(FVector(0.0f, 0.0f, GetBottomZ())).Z;
		OutMinZ = FMath::Min(TopWorldZ, BottomWorldZ);
		OutMaxZ = FMath::Max(TopWorldZ, BottomWorldZ);
	}
}

void AEHB_FloorSlab::AppendTopOrBottomMesh(bool bTop, const TArray<FVector>& TopPolygon, const TArray<TArray<FVector>>& HolePolygons, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs) const
{
	TArray<FVector2d> OuterLoop = To2DLoop(TopPolygon);
	if (OuterLoop.Num() < 3 || FMath::Abs(CalculateTwiceArea(OuterLoop)) <= UE_DOUBLE_SMALL_NUMBER)
	{
		return;
	}

	TArray<TArray<FVector2d>> HoleLoops;
	HoleLoops.Reserve(HolePolygons.Num());
	for (const TArray<FVector>& Hole : HolePolygons)
	{
		TArray<FVector2d> HoleLoop = To2DLoop(Hole);
		if (HoleLoop.Num() >= 3 && FMath::Abs(CalculateTwiceArea(HoleLoop)) > UE_DOUBLE_SMALL_NUMBER)
		{
			HoleLoops.Add(MoveTemp(HoleLoop));
		}
	}

	FBox2d Bounds(ForceInit);
	for (const FVector2d& Point : OuterLoop)
	{
		Bounds += Point;
	}
	const double ArrangementTolerance = FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y) / 128.0;
	UE::Geometry::FArrangement2d Arrangement(FMath::Max(0.01, ArrangementTolerance));
	InsertClosedLoop(Arrangement, OuterLoop);
	for (const TArray<FVector2d>& HoleLoop : HoleLoops)
	{
		InsertClosedLoop(Arrangement, HoleLoop);
	}

	UE::Geometry::FConstrainedDelaunay2d Triangulator;
	Triangulator.FillRule = UE::Geometry::FConstrainedDelaunay2d::EFillRule::Odd;
	Triangulator.bOrientedEdges = false;
	Triangulator.bSplitBowties = true;
	Triangulator.Add(Arrangement.Graph);

	const bool bSucceeded = Triangulator.Triangulate(
		[&OuterLoop, &HoleLoops](const TArray<FVector2d>& CandidateVertices, const UE::Geometry::FIndex3i& Triangle)
		{
			const FVector2d Centroid =
				(CandidateVertices[Triangle.A] + CandidateVertices[Triangle.B] + CandidateVertices[Triangle.C]) / 3.0;
			if (!IsPointInsidePolygon(Centroid, OuterLoop))
			{
				return false;
			}
			for (const TArray<FVector2d>& HoleLoop : HoleLoops)
			{
				if (IsPointInsidePolygon(Centroid, HoleLoop))
				{
					return false;
				}
			}
			return true;
		});

	if (!bSucceeded)
	{
		return;
	}

	const int32 BaseIndex = Vertices.Num();
	const float Z = bTop ? GetTopZ() : GetBottomZ();
	const FVector SurfaceNormal = bTop ? FVector::UpVector : FVector::DownVector;
	for (const FVector2d& Vertex2D : Triangulator.Vertices)
	{
		Vertices.Add(FVector(Vertex2D.X, Vertex2D.Y, Z));
		Normals.Add(SurfaceNormal);
		UVs.Add(FVector2D(Vertex2D.X / EHBFloorSlabUVWorldSize, Vertex2D.Y / EHBFloorSlabUVWorldSize));
	}

	for (const UE::Geometry::FIndex3i& Triangle : Triangulator.Triangles)
	{
		int32 A = BaseIndex + Triangle.A;
		int32 B = BaseIndex + Triangle.B;
		int32 C = BaseIndex + Triangle.C;
		const FVector TriangleNormal = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]).GetSafeNormal();
		if (FVector::DotProduct(TriangleNormal, SurfaceNormal) > 0.0f)
		{
			Swap(B, C);
		}
		Triangles.Append({ A, B, C });
	}
}

void AEHB_FloorSlab::AppendSideLoop(const TArray<FVector>& Loop, bool bInnerSide, bool bLoopCounterClockwise, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs) const
{
	if (Loop.Num() != 2)
	{
		return;
	}

	FVector TopA = Loop[0];
	FVector TopB = Loop[1];
	TopA.Z = GetTopZ();
	TopB.Z = GetTopZ();
	FVector BottomA = TopA;
	FVector BottomB = TopB;
	BottomA.Z = GetBottomZ();
	BottomB.Z = GetBottomZ();

	const FVector Normal = GetLoopSideNormal(TopA, TopB, bInnerSide, bLoopCounterClockwise);
	AppendQuad(Vertices, Triangles, Normals, UVs, BottomA, BottomB, TopB, TopA, Normal, bInnerSide);
}

void AEHB_FloorSlab::AppendFoundationSideLoop(const TArray<FVector>& Loop, bool bInnerSide, bool bLoopCounterClockwise, TArray<FVector>& Vertices, TArray<int32>& Triangles, TArray<FVector>& Normals, TArray<FVector2D>& UVs) const
{
	if (Loop.Num() != 2)
	{
		return;
	}

	const FVector TopA(Loop[0].X, Loop[0].Y, GetTopZ());
	const FVector TopB(Loop[1].X, Loop[1].Y, GetTopZ());
	const float EdgeLength = FVector::Dist2D(TopA, TopB);
	const int32 SegmentCount = FMath::Clamp(
		FMath::CeilToInt(EdgeLength / FMath::Max(10.0f, FoundationGroundTraceSpacing)),
		1,
		512);

	TArray<FVector> TopSamples;
	TArray<FVector> BottomSamples;
	TopSamples.Reserve(SegmentCount + 1);
	BottomSamples.Reserve(SegmentCount + 1);

	for (int32 SegmentIndex = 0; SegmentIndex <= SegmentCount; ++SegmentIndex)
	{
		const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount);
		const FVector TopPoint = FMath::Lerp(TopA, TopB, Alpha);
		FVector BottomPoint = TopPoint;
		BottomPoint.Z = ResolveGroundLocalZAt(TopPoint);
		TopSamples.Add(TopPoint);
		BottomSamples.Add(BottomPoint);
	}

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const FVector Normal = GetLoopSideNormal(
			TopSamples[SegmentIndex],
			TopSamples[SegmentIndex + 1],
			bInnerSide,
			bLoopCounterClockwise);

		AppendQuad(
			Vertices,
			Triangles,
			Normals,
			UVs,
			BottomSamples[SegmentIndex],
			BottomSamples[SegmentIndex + 1],
			TopSamples[SegmentIndex + 1],
			TopSamples[SegmentIndex],
			Normal,
			bInnerSide);
	}
}

float AEHB_FloorSlab::ResolveGroundLocalZAt(const FVector& LocalTopPoint) const
{
	const FVector WorldPoint = GetActorTransform().TransformPosition(LocalTopPoint);
	float GroundZ = 0.0f;
	if (TraceGroundWorldZAt(WorldPoint, 100000.0f, GroundZ))
	{
		return GetActorTransform().InverseTransformPosition(FVector(WorldPoint.X, WorldPoint.Y, GroundZ)).Z;
	}

	return GetBottomZ();
}

bool AEHB_FloorSlab::TraceGroundWorldZAt(const FVector& WorldPoint, float TraceDistance, float& OutGroundZ) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float SafeTraceDistance = FMath::Max(100.0f, TraceDistance);
	const float TraceStartHeight = FMath::Clamp(FMath::Max(FMath::Max(100.0f, Thickness * 4.0f), SafeTraceDistance * 0.01f), 100.0f, 2000.0f);
	const FVector TraceStart(WorldPoint.X, WorldPoint.Y, WorldPoint.Z + TraceStartHeight);
	const FVector TraceEnd(WorldPoint.X, WorldPoint.Y, WorldPoint.Z - SafeTraceDistance);

	TArray<FHitResult> HitResults;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EHBFloorSlabGroundTrace), true);
	QueryParams.AddIgnoredActor(this);
	if (OwningBuilding)
	{
		QueryParams.AddIgnoredActor(OwningBuilding);
	}
	QueryParams.bReturnPhysicalMaterial = false;

	if (!World->LineTraceMultiByChannel(HitResults, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
	{
		return false;
	}

	HitResults.Sort([](const FHitResult& A, const FHitResult& B)
	{
		return A.Distance < B.Distance;
	});

	for (const FHitResult& HitResult : HitResults)
	{
		if (!HitResult.bBlockingHit)
		{
			continue;
		}

		AActor* HitActor = HitResult.GetActor();
		if (!HitActor || HitActor == this || HitActor == OwningBuilding)
		{
			continue;
		}
		if (Cast<AEHBElementActorBase>(HitActor))
		{
			continue;
		}

		OutGroundZ = HitResult.ImpactPoint.Z;
		return true;
	}

	return false;
}

void AEHB_FloorSlab::ApplyMesh(const TArray<FVector>& Vertices, const TArray<int32>& Triangles, const TArray<FVector>& Normals, const TArray<FVector2D>& UVs)
{
	FEHBSurfaceMeshBuildResult BuildResult;
	BuildResult.Vertices = Vertices;
	BuildResult.Triangles = Triangles;
	BuildResult.Normals = Normals;
	BuildResult.UV0 = UVs;
	ApplyMesh(BuildResult);
}

void AEHB_FloorSlab::ApplyMesh(const FEHBSurfaceMeshBuildResult& BuildResult)
{
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;
	VertexColors.Init(FLinearColor::White, BuildResult.Vertices.Num());
	Tangents.Init(FProcMeshTangent(), BuildResult.Vertices.Num());

	UMaterialInterface* Material = SurfaceMaterial.LoadSynchronous();
	if (!Material)
	{
		Material = ResolveConfiguredDefaultWhiteBoxMaterial();
	}

	if (UEHBArchitecturalSurfaceComponent* SurfaceComponent = Cast<UEHBArchitecturalSurfaceComponent>(MeshComponent))
	{
		SurfaceComponent->SubmitMeshBuildResult(
			TEXT("FloorSlabSurface"),
			BuildResult,
			Material,
			true);
		return;
	}

	FEHBScopedGeneratedMeshUpdate ScopedMeshUpdate(MeshComponent);
	MeshComponent->CreateMeshSection_LinearColor(0, BuildResult.Vertices, BuildResult.Triangles, BuildResult.Normals, BuildResult.UV0, VertexColors, Tangents, true);
	MeshComponent->SetMeshSectionName(0, FName(TEXT("FloorSlabSurface")));
	MeshComponent->ClearMeshSectionsFrom(1);
	MeshComponent->SetMaterialIfChanged(0, Material);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
}

bool AEHB_FloorSlab::AddPreviewCutterWithShape(EEHBFloorSlabCutterShape Shape)
{
	if (LocalTopPolygon.Num() < 3)
	{
		return false;
	}

	FVector LocalCenter = FVector::ZeroVector;
	for (const FVector& Point : LocalTopPolygon)
	{
		LocalCenter += Point;
	}
	LocalCenter /= LocalTopPolygon.Num();
	LocalCenter.Z = (GetTopZ() + GetBottomZ()) * 0.5f;

	FEHBFloorSlabCutterData CutterData;
	CutterData.Shape = Shape;
	CutterData.Size = 160.0f;
	CutterData.Height = FMath::Max(Thickness + 80.0f, 120.0f);
	CutterData.LocalTransform = FTransform(LocalCenter);
	auto Candidate=PreviewCutters;Candidate.Add(CutterData);return SetCutState(CutOperations,Candidate);
}

FGuid AEHB_FloorSlab::BuildOutlineSignature() const
{
	const bool Independent=OutlineSource==EEHBOutlineSource::RetainedRegion;
	if (!OwningBuilding || (Independent?RoomFillLoopGuid.IsValid():!RoomFillLoopGuid.IsValid()) || RoomFillFloorIndex<=0 || (CutOperations.Num()>0&&(!DisplayPartition.IsActive()||DisplayPartition.SourceVersion!=1)) || PreviewCutters.Num()>0 || bIsFoundation) return FGuid();
	FEHBOutlineSignature Signature(Independent?TEXT("EHB.RetainedSlab.v1"):TEXT("EHB.SlabOutline.v1"),GetRootComponent(),OwningBuilding ? OwningBuilding->BuildingGuid : FGuid(),FloorIndex);
	FGuid Anchor=RoomFillAnchorWallGuid;bool HasAnchor=bHasRoomFillAnchor;
	uint8 Side=static_cast<uint8>(RoomFillAnchorWallSide);float StoredThickness=Thickness,Expansion=VisualExpansion;
	FGuid Room=RoomFillLoopGuid;int32 RoomFloor=RoomFillFloorIndex;
	Signature.Data << Anchor << HasAnchor << Side << StoredThickness << Expansion << Room << RoomFloor;
	Signature.Polygon(LocalTopPolygon);
	int32 HoleCount=LocalHoles.Num();Signature.Data << HoleCount;
	for(const auto& Hole:LocalHoles)Signature.Polygon(Hole.LocalPolygon);
	if(DisplayPartition.IsActive())
	{
		FString Tag=TEXT("DisplayPartition.v1");int32 Priority=DisplayPartition.Priority,Count=DisplayPartition.Regions.Num();float SourceExpansion=DisplayPartition.SourceExpansion;
		Signature.Data << Tag << Priority << SourceExpansion << Count;Signature.Polygon(DisplayPartition.SourcePolygon);
		if(DisplayPartition.SourceVersion!=0)
		{
			int32 Version=DisplayPartition.SourceVersion,SourceHoleCount=DisplayPartition.SourceHoles.Num();float SourceThickness=DisplayPartition.SourceThickness;
			Signature.Data << Version << SourceThickness << SourceHoleCount;
			for(const auto& H:DisplayPartition.SourceHoles)Signature.Polygon(H.LocalPolygon);
			auto AddCuts=[&](const TArray<FEHBCutOperation>& Cuts)
			{
				int32 CutCount=Cuts.Num();Signature.Data << CutCount;
				for(auto Cut:Cuts)
				{
					uint8 Type=static_cast<uint8>(Cut.OperationType),Stage=static_cast<uint8>(Cut.Stage),Projection=static_cast<uint8>(Cut.ProjectionMode),Policy=static_cast<uint8>(Cut.TransformPolicy);
					FString OperationTag=Cut.OperationTag.ToString();auto& Source=Cut.Source;
					uint8 SourceType=static_cast<uint8>(Source.SourceType),Shape=static_cast<uint8>(Source.PrimitiveShape);
					FGuid ReferencedElement=IsValid(Source.SourceElement)?Source.SourceElement->ElementGuid:FGuid();
					Signature.Data << Cut.OperationGuid << Cut.bEnabled << Type << Stage << Projection << Policy << Cut.Priority << OperationTag;
					// Preserve version-zero signatures byte-for-byte for saved source receipts.
					if(Cut.SurfaceHost.Version!=0)Signature.Data << Cut.SurfaceHost.Version << Cut.SurfaceHost.BuildingGuid << Cut.SurfaceHost.ElementGuid << Cut.SurfaceHost.SurfaceGuid;
					Signature.Data << SourceType << Source.SourceElementGuid << ReferencedElement << Source.LocalTransform << Shape << Source.Size << Source.Height << Source.CircleSideCount;
					Signature.bValid &= !Source.LocalTransform.ContainsNaN()&&FMath::IsFinite(Source.Size)&&FMath::IsFinite(Source.Height);
					int32 Points=Source.ExplicitPolygon.Points.Num();Signature.Data << Points;
					for(auto P:Source.ExplicitPolygon.Points){Signature.Data << P.PointGuid;Signature.Polygon({P.LocalPosition});}
				}
			};
			AddCuts(DisplayPartition.SourceCuts);AddCuts(CutOperations);
		}
		auto AddLoop=[&](const TArray<FVector2D>& Loop){TArray<FVector> P;for(const auto& V:Loop)P.Add(FVector(V.X,V.Y,0));Signature.Polygon(P);};
		for(const auto& R:DisplayPartition.Regions){AddLoop(R.Boundary);int32 Holes=R.Holes.Num();Signature.Data << Holes;for(const auto& H:R.Holes)AddLoop(H.Vertices);}
	}
	return Signature.Finish();
}

void AEHB_FloorSlab::RecordOutlineSource(EEHBOutlineSource Source)
{
	Modify();
	OutlineSource=Source;
	RecordedOutlineSignature=Source==EEHBOutlineSource::ManualOrUnclassified ? FGuid() : BuildOutlineSignature();
}

bool AEHB_FloorSlab::IsRecordedOutlineUnchanged() const
{
	return OutlineSource!=EEHBOutlineSource::ManualOrUnclassified && RecordedOutlineSignature.IsValid()
		&& RecordedOutlineSignature==BuildOutlineSignature();
}
