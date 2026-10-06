// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/EHB_DoorWindow.h"
#include "Engine/DataTable.h"
#include "Sampling/EHBMeshSampleTemplateMetadata.h"
#include "EHBDoorWindowMeshData.generated.h"

class UStaticMesh;

/**
 * 门窗网格体数据。
 * 在内容浏览器中新建数据表时，将“行结构”设置为这个结构体即可作为“门窗数据表”使用。
 * 一行代表一个门窗模板，内部保存生成出的门窗蓝图类、源网格体、洞口尺寸和放置时需要的基础参数。
 */
USTRUCT(BlueprintType, meta = (DisplayName = "门窗网格体数据", ToolTip = "数据表中的一行门窗模板数据。一行保存一个生成出的门窗蓝图类，以及源网格体、洞口宽高厚、离地高度和检测统计信息，便于后续统一管理和放置门窗。"))
struct EASYHOUSEBUILDER_API FEHBDoorWindowMeshData : public FTableRowBase
{
	GENERATED_BODY()

	FEHBDoorWindowMeshData()
	{
		TemplateMetadata.TemplateKind = EEHBMeshSampleTemplateKind::DoorWindow;
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Template")
	FEHBMeshSampleTemplateMetadata TemplateMetadata;

	/** 生成出的门窗蓝图类，后续门窗工具会优先使用这个类创建实例。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "基础", meta = (DisplayName = "门窗蓝图类", ToolTip = "门窗采样成功后生成的 AEHB_DoorWindow 子蓝图类。后续在墙体上放置门窗时，可以从数据表读取这个类并创建对应 Actor。"))
	TSoftClassPtr<AEHB_DoorWindow> DoorWindowClass;

	/** 该行对应的源静态网格体。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "基础", meta = (DisplayName = "源静态网格体", ToolTip = "生成这条门窗数据时读取的源静态网格体。保存为软引用，避免数据表加载时强制加载所有模型。"))
	TSoftObjectPtr<UStaticMesh> SourceStaticMesh;

	/** 源静态网格体名称，便于在数据表中快速识别。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "基础", meta = (DisplayName = "源网格体名称", ToolTip = "源静态网格体的对象名称，仅用于查看和排查数据来源。"))
	FName SourceMeshName = NAME_None;

	/** 当前构件是门还是窗。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "门窗", meta = (DisplayName = "构件类型", ToolTip = "表示这条模板用于门还是窗。门的底边离地高度固定为 0；窗会使用离地高度作为窗台高度。"))
	EEHBDoorWindowElementKind Kind = EEHBDoorWindowElementKind::Window;

	/** 洞口水平宽度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "洞口宽度", ToolTip = "墙体布尔挖洞时使用的洞口水平宽度，单位厘米。采样时取源网格体 XY 中较长的方向。", ClampMin = "1.0", Units = "cm"))
	float OpeningWidth = 120.0f;

	/** 洞口垂直高度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "洞口高度", ToolTip = "墙体布尔挖洞时使用的洞口垂直高度，单位厘米。采样时取源网格体 Z 方向尺寸。", ClampMin = "1.0", Units = "cm"))
	float OpeningHeight = 150.0f;

	/** 洞口或门窗构件厚度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "洞口厚度", ToolTip = "门窗构件在墙体厚度方向上的尺寸，单位厘米。采样时取源网格体 XY 中较短的方向。", ClampMin = "1.0", Units = "cm"))
	float OpeningThickness = 10.0f;

	/** 洞口底边相对墙体底部或当前楼层地面的高度，单位为厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "洞口", meta = (DisplayName = "离地高度", ToolTip = "洞口底边相对墙体底部或当前楼层地面的高度。门通常为 0；窗通常大于 0。", ClampMin = "0.0", Units = "cm"))
	float SillHeight = 90.0f;

	/** 源静态网格体被采样的 LOD 索引。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "源数据", meta = (DisplayName = "源 LOD 索引", ToolTip = "采样时读取的源静态网格体 LOD 索引。默认使用 LOD0。", ClampMin = "0"))
	int32 LODIndex = 0;

	/** 源网格体宽度是否来自源 Y 轴；否则来自源 X 轴。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "源数据", meta = (DisplayName = "宽度来自源Y轴", ToolTip = "为 true 时，说明源静态网格体 Y 方向比 X 方向更宽，生成出的门窗蓝图会自动旋转显示网格，使洞口宽度统一落在本地 X 方向。"))
	bool bWidthUsesSourceY = false;

	/** 源静态网格体本地包围盒最小值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "源数据", meta = (DisplayName = "源包围盒最小值", ToolTip = "源静态网格体在采样 LOD 中读取到的本地包围盒最小值。"))
	FVector SourceBoundsMin = FVector::ZeroVector;

	/** 源静态网格体本地包围盒最大值。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "源数据", meta = (DisplayName = "源包围盒最大值", ToolTip = "源静态网格体在采样 LOD 中读取到的本地包围盒最大值。"))
	FVector SourceBoundsMax = FVector::ZeroVector;

	/** 源网格体三角面数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "统计", meta = (DisplayName = "源三角面数", ToolTip = "源静态网格体采样 LOD 中读取到的总三角面数量。"))
	int32 SourceTriangleCount = 0;

	/** 源网格体顶点数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "统计", meta = (DisplayName = "源顶点数", ToolTip = "源静态网格体采样 LOD 中读取到的总顶点数量。"))
	int32 SourceVertexCount = 0;
};
