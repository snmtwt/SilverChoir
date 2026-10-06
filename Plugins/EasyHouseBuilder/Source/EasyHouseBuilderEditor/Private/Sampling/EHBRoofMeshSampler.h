// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sampling/EHBRoofMeshData.h"

class UDataTable;
class UStaticMesh;

struct FEHBRoofMeshSamplingOptions
{
	int32 LODIndex = 0;
	int32 MaxTriangleCountPerMesh = 50000;
	float RecommendedLength = 600.0f;
	float RecommendedWidth = 500.0f;
	float RecommendedPitchDegrees = 25.0f;
	float RecommendedThickness = 20.0f;
	float RecommendedEaveOffset = 30.0f;
	bool bOverwriteExistingRows = true;
};

struct FEHBRoofMeshPartAnalysis
{
	bool bHasMesh = false;
	FVector BoundsSize = FVector::ZeroVector;
	int32 SourceTriangleCount = 0;
	int32 SourceVertexCount = 0;
};

struct FEHBRoofMeshAnalysis
{
	bool bCanSample = false;
	FText Message;
	FEHBRoofMeshPartAnalysis SurfaceTile;
	FEHBRoofMeshPartAnalysis RidgeTile;
	FEHBRoofMeshPartAnalysis ValleyTile;
	int32 TotalTriangleCount = 0;
	int32 TotalVertexCount = 0;
};

struct FEHBRoofMeshSamplingResult
{
	FEHBRoofMeshAnalysis Analysis;
	FName RowName = NAME_None;
};

class FEHBRoofMeshSampler
{
public:
	static bool AnalyzeMeshes(
		UStaticMesh* SurfaceTileMesh,
		UStaticMesh* RidgeTileMesh,
		UStaticMesh* ValleyTileMesh,
		const FEHBRoofMeshSamplingOptions& Options,
		FEHBRoofMeshAnalysis& OutAnalysis);

	static bool SampleToDataTable(
		UStaticMesh* SurfaceTileMesh,
		UStaticMesh* RidgeTileMesh,
		UStaticMesh* ValleyTileMesh,
		UDataTable* TargetDataTable,
		FName RowName,
		const FEHBRoofMeshSamplingOptions& Options,
		FEHBRoofMeshSamplingResult& OutResult,
		FText& OutError);
};
