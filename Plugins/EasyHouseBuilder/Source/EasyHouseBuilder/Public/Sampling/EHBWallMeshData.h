// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Sampling/EHBMeshSampleTemplateMetadata.h"
#include "EHBWallMeshData.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * 墙面采样结果所属的面。
 * 一个静态网格体墙面会沿厚度中心线拆成两份数据：正面和反面。
 */
UENUM(BlueprintType, meta = (DisplayName = "墙面采样面"))
enum class EEHBWallMeshSampleSide : uint8
{
	Front UMETA(DisplayName = "正面", ToolTip = "厚度轴正方向一侧的墙面采样结果。"),
	Back UMETA(DisplayName = "反面", ToolTip = "厚度轴负方向一侧的墙面采样结果。")
};

/**
 * 源静态网格体中的轴向标记。
 * 采样后会统一转换到墙体本地坐标：X=宽度，Y=厚度，Z=高度。
 */
UENUM(BlueprintType, meta = (DisplayName = "墙面源轴向"))
enum class EEHBWallMeshSampleAxis : uint8
{
	X UMETA(DisplayName = "X", ToolTip = "源静态网格体本地 X 轴。"),
	Y UMETA(DisplayName = "Y", ToolTip = "源静态网格体本地 Y 轴。"),
	Z UMETA(DisplayName = "Z", ToolTip = "源静态网格体本地 Z 轴。")
};

/**
 * 墙面采样缓存的单个顶点。
 * Position 使用统一墙体本地坐标，便于后续生成墙体时按宽度、高度和厚度方向重建。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "墙面采样顶点", ToolTip = "墙面采样结果中的单个顶点，包含统一墙体本地坐标、法线、切线和第一套 UV。"))
struct EASYHOUSEBUILDER_API FEHBWallMeshSampleVertex
{
	GENERATED_BODY()

	/** 顶点在墙体本地坐标中的位置。X 为墙面宽度方向，Y 为厚度方向，Z 为高度方向。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面采样", meta = (DisplayName = "位置", ToolTip = "顶点在墙体本地坐标中的位置。X 表示墙面宽度方向，Y 表示墙体厚度方向，Z 表示墙面高度方向。"))
	FVector Position = FVector::ZeroVector;

	/** 顶点法线，已经转换到墙体本地坐标。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面采样", meta = (DisplayName = "法线", ToolTip = "顶点法线，已经从源静态网格体坐标转换到统一墙体本地坐标。"))
	FVector Normal = FVector::UpVector;

	/** 顶点切线，已经转换到墙体本地坐标。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面采样", meta = (DisplayName = "切线", ToolTip = "顶点切线，已经从源静态网格体坐标转换到统一墙体本地坐标。"))
	FVector Tangent = FVector::ForwardVector;

	/** 源静态网格体的第一套 UV。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面采样", meta = (DisplayName = "第一套 UV", ToolTip = "源静态网格体顶点实例上的第一套 UV，用于后续保持采样墙面的贴图坐标。"))
	FVector2D UV0 = FVector2D::ZeroVector;
};

/**
 * 墙面单侧采样数据。
 * 一个墙面模板行会同时保存正面和反面，后续生成墙体时可以按需要同时使用两面或只读取其中一面。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "墙面单侧采样数据", ToolTip = "保存墙面正面或反面其中一侧的顶点、三角形索引和材质索引。"))
struct EASYHOUSEBUILDER_API FEHBWallMeshSampleSurface
{
	GENERATED_BODY()

	/** 这份数据表示墙面的正面还是反面。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面采样", meta = (DisplayName = "采样面", ToolTip = "表示这份单侧采样数据属于墙面厚度中心线的哪一侧。正面为厚度轴正方向，反面为厚度轴负方向。"))
	EEHBWallMeshSampleSide SampleSide = EEHBWallMeshSampleSide::Front;

	/** 当前采样面保存的三角面数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面采样|统计", meta = (DisplayName = "采样三角面数", ToolTip = "当前单侧采样数据实际保存的三角面数量。"))
	int32 SampleTriangleCount = 0;

	/** 当前采样面保存的顶点数据。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面采样|几何", meta = (DisplayName = "顶点", ToolTip = "当前单侧采样数据保存的顶点数组。坐标已经转换到统一墙体本地坐标。"))
	TArray<FEHBWallMeshSampleVertex> Vertices;

	/** 当前采样面保存的三角形索引，每 3 个整数组成一个三角面。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面采样|几何", meta = (DisplayName = "三角形索引", ToolTip = "当前单侧采样数据保存的三角形索引数组。每 3 个连续整数组成一个三角面。"))
	TArray<int32> Triangles;

	/** 每个三角面对应的源静态网格体材质槽索引。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "墙面采样|几何", meta = (DisplayName = "三角形材质索引", ToolTip = "当前单侧采样数据中每个三角面对应的源材质槽索引。数组长度应等于采样三角面数量。"))
	TArray<int32> TriangleMaterialIndices;
};

/**
 * 墙体静态网格体数据。
 * 在内容浏览器中新建数据表时，将“行结构”设置为这个结构体即可作为“墙面数据表”使用。
 * 一行代表一个完整墙面模板，内部同时包含正面和反面两份单侧采样数据。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "墙面网格体数据", ToolTip = "数据表中的一行墙面模板数据。一行同时保存源网格信息、墙体尺寸、正面采样和反面采样。"))
struct EASYHOUSEBUILDER_API FEHBWallMeshData : public FTableRowBase
{
	GENERATED_BODY()

	/** 初始化正反面字段的默认侧别，避免空行在编辑器中显示成两个正面。 */
	FEHBWallMeshData()
	{
		FrontSurface.SampleSide = EEHBWallMeshSampleSide::Front;
		BackSurface.SampleSide = EEHBWallMeshSampleSide::Back;
		TemplateMetadata.TemplateKind = EEHBMeshSampleTemplateKind::WallSurface;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	FEHBMeshSampleTemplateMetadata TemplateMetadata;

	/** 该行对应的源静态网格体。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "基础", meta = (DisplayName = "源静态网格体", ToolTip = "生成这条采样数据时读取的源静态网格体。保存为软引用，避免数据表加载时强制加载所有模型。"))
	TSoftObjectPtr<UStaticMesh> SourceStaticMesh;

	/** 源静态网格体名称，便于在数据表中快速识别。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "基础", meta = (DisplayName = "源网格体名称", ToolTip = "源静态网格体的对象名称，仅用于查看和排查数据来源。"))
	FName SourceMeshName = NAME_None;

	/** 源静态网格体被采样的 LOD 索引。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "基础", meta = (DisplayName = "源 LOD 索引", ToolTip = "采样时读取的源静态网格体 LOD 索引。默认使用 LOD0。", ClampMin = "0"))
	int32 LODIndex = 0;

	/** 墙面宽度，来自源静态网格体 XY 平面中的较长轴。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "尺寸", meta = (DisplayName = "墙面宽度", ToolTip = "墙面在统一本地 X 方向上的宽度。采样时取源静态网格体 XY 两个方向中较长的尺寸。", Units = "cm"))
	float WallWidth = 0.0f;

	/** 墙面厚度，来自源静态网格体 XY 平面中的较短轴。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "尺寸", meta = (DisplayName = "墙面厚度", ToolTip = "墙面在统一本地 Y 方向上的厚度。采样时取源静态网格体 XY 两个方向中较短的尺寸，并以厚度中心为 Y=0。", Units = "cm"))
	float WallThickness = 0.0f;

	/** 墙面高度，来自源静态网格体 Z 轴尺寸。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "尺寸", meta = (DisplayName = "墙面高度", ToolTip = "墙面在统一本地 Z 方向上的高度。采样时固定使用源静态网格体 Z 轴作为高度。", Units = "cm"))
	float WallHeight = 0.0f;

	/** 源网格体中被识别为宽度方向的轴。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "源数据", meta = (DisplayName = "源宽度轴", ToolTip = "源静态网格体中被识别为墙面宽度方向的轴。"))
	EEHBWallMeshSampleAxis SourceWidthAxis = EEHBWallMeshSampleAxis::X;

	/** 源网格体中被识别为厚度方向的轴。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "源数据", meta = (DisplayName = "源厚度轴", ToolTip = "源静态网格体中被识别为墙面厚度方向的轴。"))
	EEHBWallMeshSampleAxis SourceThicknessAxis = EEHBWallMeshSampleAxis::Y;

	/** 源网格体中被识别为高度方向的轴。当前固定为 Z。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "源数据", meta = (DisplayName = "源高度轴", ToolTip = "源静态网格体中被识别为墙面高度方向的轴。当前采样器固定使用 Z 轴作为高度。"))
	EEHBWallMeshSampleAxis SourceHeightAxis = EEHBWallMeshSampleAxis::Z;

	/** 源静态网格体本地包围盒最小值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "源数据", meta = (DisplayName = "源包围盒最小值", ToolTip = "源静态网格体在采样 LOD 中读取到的本地包围盒最小值。"))
	FVector SourceBoundsMin = FVector::ZeroVector;

	/** 源静态网格体本地包围盒最大值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "源数据", meta = (DisplayName = "源包围盒最大值", ToolTip = "源静态网格体在采样 LOD 中读取到的本地包围盒最大值。"))
	FVector SourceBoundsMax = FVector::ZeroVector;

	/** 采样后统一墙体本地包围盒最小值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "采样结果", meta = (DisplayName = "墙体本地包围盒最小值", ToolTip = "采样结果在统一墙体本地坐标中的包围盒最小值。通常为 X=0、Y=-厚度/2、Z=0。"))
	FVector WallLocalBoundsMin = FVector::ZeroVector;

	/** 采样后统一墙体本地包围盒最大值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "采样结果", meta = (DisplayName = "墙体本地包围盒最大值", ToolTip = "采样结果在统一墙体本地坐标中的包围盒最大值。通常为 X=宽度、Y=厚度/2、Z=高度。"))
	FVector WallLocalBoundsMax = FVector::ZeroVector;

	/** 源网格体三角面数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "统计", meta = (DisplayName = "源三角面数", ToolTip = "源静态网格体采样 LOD 中读取到的总三角面数量。"))
	int32 SourceTriangleCount = 0;

	/** 源网格体中互不相连的网格块数量，用于判断模型复杂度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "统计", meta = (DisplayName = "分离网格块数量", ToolTip = "通过三角面顶点连接关系统计出的分离网格块数量。数量过高通常表示模型过碎，后续生成成本会升高。"))
	int32 DisconnectedComponentCount = 0;

	/** 如果轴映射改变了坐标系手性，采样器会翻转三角形绕序。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "采样结果", meta = (DisplayName = "已翻转三角形绕序", ToolTip = "当源宽度轴和厚度轴交换导致坐标系手性改变时，采样器会翻转三角形绕序，使法线和面朝向保持一致。"))
	bool bTriangleWindingFlipped = false;

	/** 源静态网格体的材质槽。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "采样结果", meta = (DisplayName = "材质", ToolTip = "源静态网格体的材质槽软引用。后续生成采样墙面时可按三角面材质索引还原材质。"))
	TArray<TSoftObjectPtr<UMaterialInterface>> Materials;

	/** 厚度轴正方向一侧的墙面采样数据。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "采样结果", meta = (DisplayName = "正面采样", ToolTip = "厚度轴正方向一侧的墙面采样数据。后续可以单独使用，也可以和反面采样组合生成双面墙。"))
	FEHBWallMeshSampleSurface FrontSurface;

	/** 厚度轴负方向一侧的墙面采样数据。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "采样结果", meta = (DisplayName = "反面采样", ToolTip = "厚度轴负方向一侧的墙面采样数据。后续可以单独使用，也可以和正面采样组合生成双面墙。"))
	FEHBWallMeshSampleSurface BackSurface;
};
