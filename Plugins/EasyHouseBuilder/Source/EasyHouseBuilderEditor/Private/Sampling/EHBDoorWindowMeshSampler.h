// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Definitions/EHBBuildingTypes.h"

class AEHB_DoorWindow;
class UBlueprint;
class UStaticMesh;

/** 门窗采样时可由编辑器面板调整的参数。 */
struct FEHBDoorWindowMeshSamplingOptions
{
	/** 读取源静态网格体的 LOD 索引。 */
	int32 LODIndex = 0;

	/** 允许采样的最大源三角面数量。 */
	int32 MaxTriangleCount = 20000;

	/** 最小洞口宽度，单位厘米。 */
	float MinOpeningWidth = 10.0f;

	/** 最小洞口高度，单位厘米。 */
	float MinOpeningHeight = 10.0f;

	/** 最小门窗厚度，单位厘米。 */
	float MinOpeningThickness = 0.5f;

	/** 厚度与宽度的最大比例，用于排除不像薄门窗构件的网格。 */
	float MaxThicknessToWidthRatio = 0.5f;
};

/** 静态网格体是否适合作为门窗模板的检测结果。 */
struct FEHBDoorWindowMeshAnalysis
{
	bool bCanSample = false;
	FText Message;
	FVector BoundsSize = FVector::ZeroVector;
	FVector SourceBoundsMin = FVector::ZeroVector;
	FVector SourceBoundsMax = FVector::ZeroVector;
	float OpeningWidth = 0.0f;
	float OpeningHeight = 0.0f;
	float OpeningThickness = 0.0f;
	bool bWidthUsesSourceY = false;
	int32 SourceVertexCount = 0;
	int32 SourceTriangleCount = 0;
};

/** 创建门窗蓝图时使用的参数。 */
struct FEHBDoorWindowBlueprintCreationOptions
{
	TSubclassOf<AEHB_DoorWindow> ParentClass;
	EEHBDoorWindowElementKind Kind = EEHBDoorWindowElementKind::Window;
	float SillHeight = 90.0f;
	FString AssetFolder = TEXT("/Game/EHB_DoorWindows");
	FString AssetName = TEXT("BP_DoorWindow");
};

/** 门窗蓝图生成结果。 */
struct FEHBDoorWindowBlueprintCreationResult
{
	TWeakObjectPtr<UBlueprint> Blueprint;
	TWeakObjectPtr<UClass> CreatedClass;
	FString PackageName;
	FEHBDoorWindowMeshAnalysis Analysis;
};

/**
 * 门窗静态网格体采样器。
 * 负责判断网格体是否适合做门窗模板，并创建带默认参数的门窗蓝图资产。
 */
class FEHBDoorWindowMeshSampler
{
public:
	/** 只分析静态网格体是否满足门窗模板的基础采样要求，不创建资产。 */
	static bool AnalyzeMesh(UStaticMesh* StaticMesh, const FEHBDoorWindowMeshSamplingOptions& Options, FEHBDoorWindowMeshAnalysis& OutAnalysis);

	/** 分析静态网格体并生成一个 AEHB_DoorWindow 子蓝图。 */
	static bool CreateBlueprintFromStaticMesh(
		UStaticMesh* StaticMesh,
		const FEHBDoorWindowMeshSamplingOptions& SamplingOptions,
		const FEHBDoorWindowBlueprintCreationOptions& CreationOptions,
		FEHBDoorWindowBlueprintCreationResult& OutResult,
		FText& OutErrorMessage);
};
