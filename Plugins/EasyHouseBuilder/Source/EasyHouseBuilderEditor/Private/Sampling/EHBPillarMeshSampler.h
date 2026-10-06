// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sampling/EHBPillarMeshData.h"

class UDataTable;
class UStaticMesh;

/** 柱体采样时可由编辑器面板调整的高级参数。 */
struct FEHBPillarMeshSamplingOptions
{
	/** 读取源静态网格体的哪个 LOD。 */
	int32 LODIndex = 0;

	/** 允许作为柱体模板的最大源三角面数。 */
	int32 MaxTriangleCount = 50000;

	/** 写入数据表时是否允许覆盖同名柱体模板行。 */
	bool bOverwriteExistingRows = true;
};

/** 对源静态网格体是否可作为柱体模板采样的分析结果。 */
struct FEHBPillarMeshAnalysis
{
	/** 是否通过基础检测，并且具备执行采样的基本条件。 */
	bool bCanSample = false;

	/** 最近一次检测得到的中文状态消息，直接显示在采样面板中。 */
	FText Message;

	/** 源静态网格体包围盒尺寸，单位厘米。 */
	FVector BoundsSize = FVector::ZeroVector;

	/** 源静态网格体在指定 LOD 中读取到的三角面数量。 */
	int32 SourceTriangleCount = 0;

	/** 源静态网格体在指定 LOD 中读取到的顶点数量。 */
	int32 SourceVertexCount = 0;

	/** 源网格体中互不相连的网格块数量。 */
	int32 DisconnectedComponentCount = 0;
};

/** 柱体采样并写入数据表后的结果摘要。 */
struct FEHBPillarMeshSamplingResult
{
	/** 本次采样使用的检测结果，方便 UI 在写表后继续显示尺寸和统计信息。 */
	FEHBPillarMeshAnalysis Analysis;

	/** 写入或覆盖的数据表行名。 */
	FName RowName = NAME_None;

	/** 写入柱体数据表的三角面数量。 */
	int32 TriangleCount = 0;
};

/**
 * 柱体静态网格体采样器。
 * 这个类只处理完整网格读取、基础检测和数据表写入，不持有 Slate 状态。
 */
class FEHBPillarMeshSampler
{
public:
	/** 只分析静态网格体是否满足柱体模板的基础采样要求，不写入资产。 */
	static bool AnalyzeMesh(UStaticMesh* StaticMesh, const FEHBPillarMeshSamplingOptions& Options, FEHBPillarMeshAnalysis& OutAnalysis);

	/** 分析并采样静态网格体，把完整柱体网格数据作为同一行写入目标数据表。 */
	static bool SampleToDataTable(
		UStaticMesh* StaticMesh,
		UDataTable* TargetDataTable,
		FName RowName,
		const FEHBPillarMeshSamplingOptions& Options,
		FEHBPillarMeshSamplingResult& OutResult,
		FText& OutError);
};
