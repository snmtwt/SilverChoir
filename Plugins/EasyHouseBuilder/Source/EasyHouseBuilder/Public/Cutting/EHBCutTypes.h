// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EHBCutTypes.generated.h"

class AEHBElementActorBase;

UENUM(BlueprintType)
enum class EEHBCutStage : uint8
{
	Profile UMETA(DisplayName = "Profile"),
	SurfaceOpening UMETA(DisplayName = "Surface Opening"),
	SourceOverlap UMETA(DisplayName = "Source Overlap"),
	PostMesh UMETA(DisplayName = "Post Mesh")
};

UENUM(BlueprintType)
enum class EEHBCutProjectionMode : uint8
{
	HorizontalXY UMETA(DisplayName = "Horizontal XY"),
	VerticalXZ UMETA(DisplayName = "Vertical XZ"),
	TargetPlane UMETA(DisplayName = "Target Plane"),
	SourcePolygon UMETA(DisplayName = "Source Polygon"),
	SourceMesh UMETA(DisplayName = "Source Mesh")
};

UENUM(BlueprintType)
enum class EEHBCutSourceType : uint8
{
	Element UMETA(DisplayName = "Element"),
	ExplicitPolygon UMETA(DisplayName = "Explicit Polygon"),
	ExplicitPrism UMETA(DisplayName = "Explicit Prism"),
	SplineProfile UMETA(DisplayName = "Spline Profile"),
	MeshVolume UMETA(DisplayName = "Mesh Volume")
};

UENUM(BlueprintType)
enum class EEHBCutOperationType : uint8
{
	Subtract UMETA(DisplayName = "Subtract"),
	Split UMETA(DisplayName = "Split"),
	Intersect UMETA(DisplayName = "Intersect"),
	Union UMETA(DisplayName = "Union")
};

UENUM(BlueprintType)
enum class EEHBCutTransformPolicy : uint8
{
	StaticWorld UMETA(DisplayName = "Static World"),
	TargetLocal UMETA(DisplayName = "Target Local"),
	FollowTargetElement UMETA(DisplayName = "Follow Target Element"),
	SourceActorDriven UMETA(DisplayName = "Source Actor Driven")
};

UENUM(BlueprintType)
enum class EEHBCutPrimitiveShape : uint8
{
	Polygon UMETA(DisplayName = "Polygon"),
	Square UMETA(DisplayName = "Square"),
	Circle UMETA(DisplayName = "Circle")
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBCutPolygonPoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	FGuid PointGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	FVector LocalPosition = FVector::ZeroVector;

	void EnsureGuid()
	{
		if (!PointGuid.IsValid())
		{
			PointGuid = FGuid::NewGuid();
		}
	}
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBCutPolygon
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	TArray<FEHBCutPolygonPoint> Points;

	void EnsurePointGuids()
	{
		for (FEHBCutPolygonPoint& Point : Points)
		{
			Point.EnsureGuid();
		}
	}

	TArray<FVector> ToLocalPositions() const
	{
		TArray<FVector> Positions;
		Positions.Reserve(Points.Num());
		for (const FEHBCutPolygonPoint& Point : Points)
		{
			Positions.Add(Point.LocalPosition);
		}
		return Positions;
	}
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBCutSource
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	EEHBCutSourceType SourceType = EEHBCutSourceType::ExplicitPolygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	FGuid SourceElementGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	TObjectPtr<AEHBElementActorBase> SourceElement = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	FEHBCutPolygon ExplicitPolygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	FTransform LocalTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	EEHBCutPrimitiveShape PrimitiveShape = EEHBCutPrimitiveShape::Polygon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut", meta = (ClampMin = "1.0", Units = "cm"))
	float Size = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut", meta = (ClampMin = "1.0", Units = "cm"))
	float Height = 160.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut", meta = (ClampMin = "8", ClampMax = "128"))
	int32 CircleSideCount = 32;

	void EnsureGuids()
	{
		ExplicitPolygon.EnsurePointGuids();
	}
};

/** Version zero retains legacy element-axis coordinates. Version one qualifies a
 * logical surface and stores the polygon in that surface's XY chart, in cm. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBCutSurfaceHost
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface Host") int32 Version = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface Host") FGuid BuildingGuid;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface Host") FGuid ElementGuid;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Surface Host") FGuid SurfaceGuid;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBCutOperation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	FGuid OperationGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	EEHBCutOperationType OperationType = EEHBCutOperationType::Subtract;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	EEHBCutStage Stage = EEHBCutStage::Profile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	EEHBCutProjectionMode ProjectionMode = EEHBCutProjectionMode::HorizontalXY;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	EEHBCutTransformPolicy TransformPolicy = EEHBCutTransformPolicy::TargetLocal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	int32 Priority = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	FName OperationTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Cut")
	FEHBCutSource Source;

	/** Explicit opt-in. Old operations remain version zero and keep their axes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EHB Cut")
	FEHBCutSurfaceHost SurfaceHost;

	void EnsureGuids()
	{
		if (!OperationGuid.IsValid())
		{
			OperationGuid = FGuid::NewGuid();
		}
		Source.EnsureGuids();
	}
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBResolvedCutVolume
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Cut")
	FGuid OperationGuid;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Cut")
	EEHBCutStage Stage = EEHBCutStage::Profile;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Cut")
	EEHBCutProjectionMode ProjectionMode = EEHBCutProjectionMode::HorizontalXY;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Cut")
	EEHBCutOperationType OperationType = EEHBCutOperationType::Subtract;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Cut")
	TArray<FVector> TargetLocalPolygon;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Cut")
	float MinZ = -FLT_MAX;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Cut")
	float MaxZ = FLT_MAX;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBPolygonRegion
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Cut")
	TArray<FVector> OuterLoop;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Cut")
	TArray<FEHBCutPolygon> HoleLoops;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBPolygonClipResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Cut")
	TArray<FEHBPolygonRegion> Regions;

	void Reset()
	{
		Regions.Reset();
	}

	bool HasRegions() const
	{
		return !Regions.IsEmpty();
	}
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBMeshTriangleRef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	int32 VertexA = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	int32 VertexB = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	int32 VertexC = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	int32 SourceComponentIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	int32 SourceSectionIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	int32 SourceTriangleIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBMeshAggregateData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	FGuid SourceElementGuid;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	TArray<FVector> Vertices;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	TArray<FVector> Normals;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	TArray<FVector2D> UV0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	TArray<FEHBMeshTriangleRef> Triangles;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh Aggregate")
	FBox LocalBounds = FBox(ForceInit);

	void Reset()
	{
		SourceElementGuid.Invalidate();
		Vertices.Reset();
		Normals.Reset();
		UV0.Reset();
		Triangles.Reset();
		LocalBounds.Init();
	}
};
