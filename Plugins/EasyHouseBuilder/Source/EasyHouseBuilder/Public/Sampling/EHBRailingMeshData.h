// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Definitions/EHBBuildingTypes.h"
#include "Engine/DataTable.h"
#include "Sampling/EHBMeshSampleTemplateMetadata.h"
#include "EHBRailingMeshData.generated.h"

class UMaterialInterface;
class UStaticMesh;

USTRUCT(BlueprintType, meta = (DisplayName = "Railing Sample Mesh Part"))
struct EASYHOUSEBUILDER_API FEHBRailingSampleMeshPart
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing Sampling")
	TSoftObjectPtr<UStaticMesh> SourceStaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing Sampling")
	FName SourceMeshName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing Sampling", meta = (ClampMin = "0"))
	int32 LODIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing Sampling", meta = (Units = "cm"))
	FVector SourceBoundsMin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing Sampling", meta = (Units = "cm"))
	FVector SourceBoundsMax = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing Sampling", meta = (Units = "cm"))
	FVector BoundsSize = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing Sampling")
	int32 SourceTriangleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing Sampling")
	int32 SourceVertexCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing Sampling")
	TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Railing Mesh Data"))
struct EASYHOUSEBUILDER_API FEHBRailingMeshData : public FTableRowBase
{
	GENERATED_BODY()

	FEHBRailingMeshData()
	{
		TemplateMetadata.TemplateKind = EEHBMeshSampleTemplateKind::Railing;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	FEHBMeshSampleTemplateMetadata TemplateMetadata;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basic")
	FEHBRailingSampleMeshPart PostMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basic")
	FEHBRailingSampleMeshPart RailMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Basic")
	FEHBRailingSampleMeshPart PanelMaterialSourceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing")
	EEHBRailingFillMode FillMode = EEHBRailingFillMode::PostsAndRails;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts", meta = (ClampMin = "0.1", Units = "cm"))
	float RecommendedPostWidth = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts", meta = (ClampMin = "1.0", Units = "cm"))
	float RecommendedPostHeight = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts", meta = (ClampMin = "1.0", Units = "cm"))
	float RecommendedPostSpacing = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Rails", meta = (ClampMin = "1.0", Units = "cm"))
	float RecommendedRailHeight = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Rails", meta = (ClampMin = "0.1", Units = "cm"))
	float RecommendedRailThickness = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Rails", meta = (ClampMin = "1.0", Units = "cm"))
	float RecommendedMaxRailSegmentLength = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Panel", meta = (ClampMin = "0.0", Units = "cm"))
	float RecommendedPanelBottomOffset = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Panel", meta = (ClampMin = "1.0", Units = "cm"))
	float RecommendedPanelTopOffset = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Material")
	TSoftObjectPtr<UMaterialInterface> PostMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Material")
	TSoftObjectPtr<UMaterialInterface> RailMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Material")
	TSoftObjectPtr<UMaterialInterface> PanelMaterial;
};
