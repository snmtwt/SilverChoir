// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Sampling/EHBMeshSampleTemplateMetadata.h"
#include "EHBRoofMeshData.generated.h"

class UMaterialInterface;
class UStaticMesh;

USTRUCT(BlueprintType, meta = (DisplayName = "Roof Surface Patch"))
struct EASYHOUSEBUILDER_API FEHBRoofSurfacePatch
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling")
	TSoftObjectPtr<UStaticMesh> SourceStaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling")
	FName SourceMeshName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling", meta = (ClampMin = "0"))
	int32 LODIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling", meta = (Units = "cm"))
	FVector SourceBoundsMin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling", meta = (Units = "cm"))
	FVector SourceBoundsMax = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling", meta = (Units = "cm"))
	FVector BoundsSize = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling")
	int32 SourceTriangleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling")
	int32 SourceVertexCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling")
	TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Roof Mesh Data"))
struct EASYHOUSEBUILDER_API FEHBRoofMeshData : public FTableRowBase
{
	GENERATED_BODY()

	FEHBRoofMeshData()
	{
		TemplateMetadata.TemplateKind = EEHBMeshSampleTemplateKind::Roof;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	FEHBMeshSampleTemplateMetadata TemplateMetadata;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling")
	FEHBRoofSurfacePatch SurfaceTileMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling")
	FEHBRoofSurfacePatch RidgeTileMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling")
	FEHBRoofSurfacePatch ValleyTileMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Recommended", meta = (ClampMin = "1.0", Units = "cm"))
	float RecommendedLength = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Recommended", meta = (ClampMin = "1.0", Units = "cm"))
	float RecommendedWidth = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Recommended", meta = (ClampMin = "1.0", ClampMax = "89.0", Units = "deg"))
	float RecommendedPitchDegrees = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Recommended", meta = (ClampMin = "0.1", Units = "cm"))
	float RecommendedThickness = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Recommended", meta = (ClampMin = "0.0", Units = "cm"))
	float RecommendedEaveOffset = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Modes")
	bool bSupportsTwoSideSampled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Modes")
	bool bSupportsFourSideSampled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Modes")
	bool bSupportsFixedSampled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Material")
	TSoftObjectPtr<UMaterialInterface> SurfaceMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Material")
	TSoftObjectPtr<UMaterialInterface> RidgeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Material")
	TSoftObjectPtr<UMaterialInterface> ValleyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Stats")
	int32 SourceTriangleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roof Sampling|Stats")
	int32 SourceVertexCount = 0;
};
