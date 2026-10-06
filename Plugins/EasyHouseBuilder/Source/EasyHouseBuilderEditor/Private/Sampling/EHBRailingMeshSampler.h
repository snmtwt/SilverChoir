// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sampling/EHBRailingMeshData.h"

class UDataTable;
class UStaticMesh;

struct FEHBRailingMeshSamplingOptions
{
	int32 LODIndex = 0;
	int32 MaxTriangleCountPerMesh = 50000;
	bool bOverwriteExistingRows = true;
	EEHBRailingFillMode FillMode = EEHBRailingFillMode::PostsAndRails;
	float PostSpacing = 120.0f;
};

struct FEHBRailingMeshPartAnalysis
{
	bool bHasMesh = false;
	FVector BoundsSize = FVector::ZeroVector;
	int32 SourceTriangleCount = 0;
	int32 SourceVertexCount = 0;
};

struct FEHBRailingMeshAnalysis
{
	bool bCanSample = false;
	FText Message;
	FEHBRailingMeshPartAnalysis Post;
	FEHBRailingMeshPartAnalysis Rail;
	FEHBRailingMeshPartAnalysis Panel;
	FName PanelWidthAxis = NAME_None;
	float RecommendedPostSpacing = 120.0f;
	float RecommendedPostWidth = 8.0f;
	float RecommendedPostHeight = 100.0f;
	float RecommendedRailThickness = 8.0f;
	float RecommendedRailHeight = 100.0f;
	float RecommendedMaxRailSegmentLength = 120.0f;
	float RecommendedPanelWidth = 0.0f;
	float RecommendedPanelThickness = 0.0f;
	float RecommendedPanelBottomOffset = 10.0f;
	float RecommendedPanelTopOffset = 90.0f;
};

struct FEHBRailingMeshSamplingResult
{
	FEHBRailingMeshAnalysis Analysis;
	FName RowName = NAME_None;
};

class FEHBRailingMeshSampler
{
public:
	static bool AnalyzeMeshes(
		UStaticMesh* PostMesh,
		UStaticMesh* RailMesh,
		UStaticMesh* PanelMaterialSourceMesh,
		const FEHBRailingMeshSamplingOptions& Options,
		FEHBRailingMeshAnalysis& OutAnalysis);

	static bool SampleToDataTable(
		UStaticMesh* PostMesh,
		UStaticMesh* RailMesh,
		UStaticMesh* PanelMaterialSourceMesh,
		UDataTable* TargetDataTable,
		FName RowName,
		const FEHBRailingMeshSamplingOptions& Options,
		FEHBRailingMeshSamplingResult& OutResult,
		FText& OutError);
};
