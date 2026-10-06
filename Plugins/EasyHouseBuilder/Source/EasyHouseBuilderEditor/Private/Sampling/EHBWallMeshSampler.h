// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sampling/EHBWallMeshData.h"

class UDataTable;
class UStaticMesh;

/** 墙面采样时可由编辑器面板调整的高级参数。 */
struct FEHBWallMeshSamplingOptions
{
	/** 读取源静态网格体的哪个 LOD。 */
	int32 LODIndex = 0;

	/** 允许作为墙面模板的最大源三角面数。 */
	int32 MaxTriangleCount = 10000;

	/** 允许的最大厚宽比。值越小，越严格要求模型是薄墙。 */
	float MaxThicknessToWidthRatio = 0.25f;

	/** 三角面法线与厚度轴的最小对齐度，用于只保留墙体正反两个侧面并排除顶面、底面和端面。 */
	float MinSideFaceNormalAlignment = 0.55f;

	/** 墙面宽度最小值，单位厘米。 */
	float MinWallWidth = 10.0f;

	/** 墙面高度最小值，单位厘米。 */
	float MinWallHeight = 10.0f;

	/** 墙面厚度最小值，单位厘米。用于避免完全没有厚度的平面被拆成空的反面。 */
	float MinWallThickness = 0.1f;

	/** 允许的最大分离网格块数量。 */
	int32 MaxDisconnectedComponentCount = 64;

	/** 写入数据表时是否允许覆盖同名墙面模板行。 */
	bool bOverwriteExistingRows = true;
};

/** 对源静态网格体是否适合作为墙面的分析结果。 */
struct FEHBWallMeshAnalysis
{
	/** 是否通过墙面模板检测，并且具备执行采样的基本条件。 */
	bool bCanSample = false;

	/** 最近一次检测得到的中文状态消息，直接显示在采样面板中。 */
	FText Message;

	/** 统一墙体本地 X 方向的宽度，来自源网格体 XY 平面较长轴。 */
	float WallWidth = 0.0f;

	/** 统一墙体本地 Y 方向的厚度，来自源网格体 XY 平面较短轴。 */
	float WallThickness = 0.0f;

	/** 统一墙体本地 Z 方向的高度，当前固定来自源网格体 Z 轴。 */
	float WallHeight = 0.0f;

	/** 源静态网格体在指定 LOD 中读取到的三角面数量。 */
	int32 SourceTriangleCount = 0;

	/** 源静态网格体在指定 LOD 中读取到的顶点数量。 */
	int32 SourceVertexCount = 0;

	/** 通过法线朝向预估出的正面侧面三角面数量，不包含顶面、底面和端面。 */
	int32 EstimatedFrontSideTriangleCount = 0;

	/** 通过法线朝向预估出的反面侧面三角面数量，不包含顶面、底面和端面。 */
	int32 EstimatedBackSideTriangleCount = 0;

	/** 源网格体中互不相连的网格块数量，用于判断模型是否过碎。 */
	int32 DisconnectedComponentCount = 0;

	/** 源网格体中被识别为墙面宽度方向的轴。 */
	EEHBWallMeshSampleAxis WidthAxis = EEHBWallMeshSampleAxis::X;

	/** 源网格体中被识别为墙面厚度方向的轴。 */
	EEHBWallMeshSampleAxis ThicknessAxis = EEHBWallMeshSampleAxis::Y;
};

/** 墙面采样并写入数据表后的结果摘要。 */
struct FEHBWallMeshSamplingResult
{
	/** 本次采样使用的检测结果，方便 UI 在写表后继续显示尺寸和统计信息。 */
	FEHBWallMeshAnalysis Analysis;

	/** 写入或覆盖的数据表行名。 */
	FName RowName = NAME_None;

	/** 写入正面采样结构体的三角面数量。 */
	int32 FrontTriangleCount = 0;

	/** 写入反面采样结构体的三角面数量。 */
	int32 BackTriangleCount = 0;
};

/**
 * 墙面静态网格体采样器。
 * 这个类只处理几何识别、正反面拆分和数据表写入，不持有 Slate 状态。
 */
class FEHBWallMeshSampler
{
public:
	/** 只分析静态网格体是否满足墙面模板要求，不写入资产。 */
	static bool AnalyzeMesh(UStaticMesh* StaticMesh, const FEHBWallMeshSamplingOptions& Options, FEHBWallMeshAnalysis& OutAnalysis);

	/** 分析并采样静态网格体，把正面和反面数据作为同一行写入目标数据表。 */
	static bool SampleToDataTable(
		UStaticMesh* StaticMesh,
		UDataTable* TargetDataTable,
		FName BaseRowName,
		const FEHBWallMeshSamplingOptions& Options,
		FEHBWallMeshSamplingResult& OutResult,
		FText& OutError);
};
