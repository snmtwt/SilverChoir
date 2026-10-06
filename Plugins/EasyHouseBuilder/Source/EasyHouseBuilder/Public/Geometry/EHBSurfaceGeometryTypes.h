// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EHBSurfaceGeometryTypes.generated.h"

class UMaterialInterface;

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceHoleLoop
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Geometry")
	TArray<FVector> LocalLoop;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBPlanarSurfaceRegion
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EHB Surface|Geometry")
 TArray<FVector> BoundaryLoop;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EHB Surface|Geometry")
 TArray<FEHBSurfaceHoleLoop> HoleLoops;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceOpeningLoop
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Geometry")
	TArray<FVector> LocalLoop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Geometry")
	FName OpeningName = NAME_None;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceMeshBuildResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Mesh")
	TArray<FVector> Vertices;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Mesh")
	TArray<int32> Triangles;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Mesh")
	TArray<FVector> Normals;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Mesh")
	TArray<FVector2D> UV0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Mesh")
	TArray<int32> TriangleMaterialIndices;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Mesh")
	TArray<TSoftObjectPtr<UMaterialInterface>> SourceMaterials;

	void Reset()
	{
		Vertices.Reset();
		Triangles.Reset();
		Normals.Reset();
		UV0.Reset();
		TriangleMaterialIndices.Reset();
		SourceMaterials.Reset();
	}

	bool IsValidMesh() const
	{
		return Vertices.Num() > 0
			&& Triangles.Num() >= 3
			&& Triangles.Num() % 3 == 0;
	}
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceBoundaryHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Boundary")
	FVector WorldPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Boundary")
	FVector WorldTangent = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Boundary")
	FVector WorldNormal = FVector::RightVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Boundary")
	int32 SegmentIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Boundary")
	float SegmentAlpha = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Boundary")
	float Distance = 0.0f;
};

struct EASYHOUSEBUILDER_API FEHBPlanarSurfaceMeshBuildInput
{
	TArray<FVector> BoundaryLoop;
	TArray<TArray<FVector>> HoleLoops;
	float PlaneZ = 0.0f;
	FVector PlaneNormal = FVector::UpVector;
	float UVWorldSize = 100.0f;
	bool bFlipWinding = false;
};

struct EASYHOUSEBUILDER_API FEHBSlabSurfaceMeshBuildInput
{
	TArray<FVector> TopBoundaryLoop;
	TArray<TArray<FVector>> TopHoleLoops;
	float TopZ = 0.0f;
	float DefaultBottomZ = -20.0f;
	float UVWorldSize = 100.0f;
	bool bBuildTop = true;
	bool bBuildBottom = true;
	bool bBuildSides = true;
	bool bDoubleSideInnerLoops = true;
	float SideSegmentLength = 100.0f;
	TFunction<float(const FVector& LocalTopPoint)> ResolveBottomZ;
};

struct EASYHOUSEBUILDER_API FEHBVerticalSurfaceMeshBuildInput
{
	TArray<FVector> LocalBasePolyline;
	float SurfaceHeight = 300.0f;
	float BottomOffset = 0.0f;
	bool bClosedLoop = false;
	float UVWorldSize = 100.0f;
};

class EASYHOUSEBUILDER_API FEHBSurfaceGeometryUtil
{
public:
	static TArray<FVector2d> To2DLoop(const TArray<FVector>& Loop, double PointTolerance = 0.01);
	static double CalculateSignedArea2D(const TArray<FVector2d>& Loop);
	static double CalculateSignedAreaXY(const TArray<FVector>& Loop);
	static bool IsPointInsidePolygon2D(const FVector2d& Point, const TArray<FVector2d>& Polygon);
	static float GetClosestAlphaOnSegmentXY(const FVector& SegmentStart, const FVector& SegmentEnd, const FVector& Point);
	static bool FindClosestBoundaryPointXY(
		const TArray<FVector>& LocalLoop,
		const FTransform& LocalToWorld,
		const FVector& WorldPoint,
		float LocalZ,
		bool bClosed,
		FEHBSurfaceBoundaryHit& OutHit);
};

class EASYHOUSEBUILDER_API FEHBPlanarSurfaceGeometryBuilder
{
public:
	static bool BuildPlanarSurface(
		const FEHBPlanarSurfaceMeshBuildInput& Input,
		FEHBSurfaceMeshBuildResult& OutResult);

	static bool BuildSlabSurface(
		const FEHBSlabSurfaceMeshBuildInput& Input,
		FEHBSurfaceMeshBuildResult& OutResult);
};

class EASYHOUSEBUILDER_API FEHBVerticalSurfaceGeometryBuilder
{
public:
	static bool BuildVerticalSurface(
		const FEHBVerticalSurfaceMeshBuildInput& Input,
		FEHBSurfaceMeshBuildResult& OutResult);

	static void AppendSideLoop(
		const TArray<FVector>& Loop,
		float TopZ,
		float DefaultBottomZ,
		bool bInnerSide,
		bool bLoopCounterClockwise,
		float UVWorldSize,
		FEHBSurfaceMeshBuildResult& InOutResult,
		const TFunction<float(const FVector& LocalTopPoint)>& ResolveBottomZ = nullptr,
		bool bDoubleSided = false,
		float SideSegmentLength = 100.0f);
};
