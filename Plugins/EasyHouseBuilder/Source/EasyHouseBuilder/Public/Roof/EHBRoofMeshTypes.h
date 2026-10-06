#pragma once

#include "CoreMinimal.h"
#include "EHBRoofMeshTypes.generated.h"

UENUM(BlueprintType)
enum class EEHBRoofAxisMode : uint8
{
	RidgeAlongX UMETA(DisplayName = "Ridge Along X"),
	RidgeAlongY UMETA(DisplayName = "Ridge Along Y"),
};

USTRUCT(BlueprintType)
struct FEHBRoofProjectionBounds
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	bool bIsValid = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	FVector2D Min = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	FVector2D Max = FVector2D::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	FVector LocalCenter = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	FVector LocalSize = FVector::ZeroVector;

	void Reset()
	{
		bIsValid = false;
		Min = FVector2D::ZeroVector;
		Max = FVector2D::ZeroVector;
		LocalCenter = FVector::ZeroVector;
		LocalSize = FVector::ZeroVector;
	}
};

USTRUCT(BlueprintType)
struct FEHBRoofUnifiedMeshData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	TArray<FVector> Vertices;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	TArray<int32> Triangles;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	TArray<FVector> Normals;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	TArray<FVector2D> UV0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	TArray<int32> TriangleGroups;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Roof")
	FBox LocalBounds = FBox(ForceInit);

	void Reset()
	{
		Vertices.Reset();
		Triangles.Reset();
		Normals.Reset();
		UV0.Reset();
		TriangleGroups.Reset();
		LocalBounds.Init();
	}

	bool IsValid() const
	{
		return Vertices.Num() > 0
			&& Triangles.Num() >= 3
			&& Triangles.Num() % 3 == 0;
	}

	void RebuildBounds()
	{
		LocalBounds.Init();
		for (const FVector& Vertex : Vertices)
		{
			LocalBounds += Vertex;
		}
	}
};
