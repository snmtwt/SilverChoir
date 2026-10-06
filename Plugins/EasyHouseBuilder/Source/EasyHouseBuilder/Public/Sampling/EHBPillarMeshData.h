// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Sampling/EHBMeshSampleTemplateMetadata.h"
#include "EHBPillarMeshData.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * 柱体网格体采样顶点。
 * 柱体采样会保留源静态网格体的完整三角面，因此这里保存的是源模型本地坐标中的顶点实例数据。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "柱体采样顶点", ToolTip = "柱体网格体采样结果中的单个顶点，包含源模型本地坐标、法线、切线和第一套 UV。"))
struct EASYHOUSEBUILDER_API FEHBPillarMeshSampleVertex
{
	GENERATED_BODY()

	/** 顶点在源静态网格体本地坐标中的位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "柱体采样", meta = (DisplayName = "位置", ToolTip = "顶点在源静态网格体本地坐标中的位置。后续生成柱体时可以直接按这个坐标重建网格，或再按柱体尺寸进行缩放。"))
	FVector Position = FVector::ZeroVector;

	/** 顶点法线。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "柱体采样", meta = (DisplayName = "法线", ToolTip = "源静态网格体顶点实例上的法线，用于后续重建柱体表面的光照方向。"))
	FVector Normal = FVector::UpVector;

	/** 顶点切线。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "柱体采样", meta = (DisplayName = "切线", ToolTip = "源静态网格体顶点实例上的切线，用于后续重建法线贴图方向和切线空间。"))
	FVector Tangent = FVector::ForwardVector;

	/** 源静态网格体的第一套 UV。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "柱体采样", meta = (DisplayName = "第一套 UV", ToolTip = "源静态网格体顶点实例上的第一套 UV，用于后续保持柱体模板的贴图坐标。"))
	FVector2D UV0 = FVector2D::ZeroVector;
};

/**
 * 柱体静态网格体数据。
 * 在内容浏览器中新建数据表时，将“行结构”设置为这个结构体即可作为“柱体数据表”使用。
 * 一行代表一个完整柱体模板，内部保存整份源网格体的三角面数据。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "柱体网格体数据", ToolTip = "数据表中的一行柱体模板数据。一行保存一个完整静态网格体的顶点、三角形、材质索引、尺寸和源资产信息。"))
struct EASYHOUSEBUILDER_API FEHBPillarMeshData : public FTableRowBase
{
	GENERATED_BODY()

	FEHBPillarMeshData()
	{
		TemplateMetadata.TemplateKind = EEHBMeshSampleTemplateKind::Pillar;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	FEHBMeshSampleTemplateMetadata TemplateMetadata;

	/** 该行对应的源静态网格体。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "基础", meta = (DisplayName = "源静态网格体", ToolTip = "生成这条柱体采样数据时读取的源静态网格体。保存为软引用，避免数据表加载时强制加载所有模型。"))
	TSoftObjectPtr<UStaticMesh> SourceStaticMesh;

	/** 源静态网格体名称，便于在数据表中快速识别。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "基础", meta = (DisplayName = "源网格体名称", ToolTip = "源静态网格体的对象名称，仅用于查看和排查数据来源。"))
	FName SourceMeshName = NAME_None;

	/** 源静态网格体被采样的 LOD 索引。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "基础", meta = (DisplayName = "源 LOD 索引", ToolTip = "采样时读取的源静态网格体 LOD 索引。默认使用 LOD0。", ClampMin = "0"))
	int32 LODIndex = 0;

	/** 源静态网格体本地包围盒最小值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "尺寸", meta = (DisplayName = "源包围盒最小值", ToolTip = "源静态网格体在采样 LOD 中读取到的本地包围盒最小值。"))
	FVector SourceBoundsMin = FVector::ZeroVector;

	/** 源静态网格体本地包围盒最大值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "尺寸", meta = (DisplayName = "源包围盒最大值", ToolTip = "源静态网格体在采样 LOD 中读取到的本地包围盒最大值。"))
	FVector SourceBoundsMax = FVector::ZeroVector;

	/** 柱体本地 X 方向尺寸，单位厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "尺寸", meta = (DisplayName = "宽度", ToolTip = "柱体源网格体在本地 X 方向上的包围盒尺寸，单位为厘米。", Units = "cm"))
	float Width = 0.0f;

	/** 柱体本地 Y 方向尺寸，单位厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "尺寸", meta = (DisplayName = "深度", ToolTip = "柱体源网格体在本地 Y 方向上的包围盒尺寸，单位为厘米。", Units = "cm"))
	float Depth = 0.0f;

	/** 柱体本地 Z 方向尺寸，单位厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "尺寸", meta = (DisplayName = "高度", ToolTip = "柱体源网格体在本地 Z 方向上的包围盒尺寸，单位为厘米。", Units = "cm"))
	float Height = 0.0f;

	/** 源网格体三角面数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "统计", meta = (DisplayName = "源三角面数", ToolTip = "源静态网格体采样 LOD 中读取到的总三角面数量。"))
	int32 SourceTriangleCount = 0;

	/** 源网格体顶点数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "统计", meta = (DisplayName = "源顶点数", ToolTip = "源静态网格体采样 LOD 中读取到的总顶点数量。"))
	int32 SourceVertexCount = 0;

	/** 源网格体中互不相连的网格块数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "统计", meta = (DisplayName = "分离网格块数量", ToolTip = "通过三角面顶点连接关系统计出的分离网格块数量。数量过高通常表示模型由很多碎件组成。"))
	int32 DisconnectedComponentCount = 0;

	/** 源静态网格体的材质槽。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "采样结果", meta = (DisplayName = "材质", ToolTip = "源静态网格体的材质槽软引用。后续生成采样柱体时可按三角面材质索引还原材质。"))
	TArray<TSoftObjectPtr<UMaterialInterface>> Materials;

	/** 完整柱体模板顶点数据。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "采样结果", meta = (DisplayName = "顶点", ToolTip = "柱体模板保存的完整顶点数组。当前采样器会按三角面展开顶点，以保留顶点实例法线、切线和 UV。"))
	TArray<FEHBPillarMeshSampleVertex> Vertices;

	/** 完整柱体模板三角形索引，每 3 个整数组成一个三角面。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "采样结果", meta = (DisplayName = "三角形索引", ToolTip = "柱体模板保存的三角形索引数组。每 3 个连续整数组成一个三角面。"))
	TArray<int32> Triangles;

	/** 每个三角面对应的源静态网格体材质槽索引。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "采样结果", meta = (DisplayName = "三角形材质索引", ToolTip = "柱体模板中每个三角面对应的源材质槽索引。数组长度应等于源三角面数量。"))
	TArray<int32> TriangleMaterialIndices;
};
