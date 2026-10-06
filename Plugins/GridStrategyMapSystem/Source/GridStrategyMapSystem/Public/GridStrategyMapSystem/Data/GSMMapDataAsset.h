#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "GSMMapDataAsset.generated.h"

class UStaticMesh;
class UMaterialInterface;
class AGSMMap3D;
class AGSMPathArrow3D;
class AGSMPiece3D;
class UGSMMapData;
class UGSMPieceData;
class UGSMTileData;

/**
 * 网格策略地图配置资产。
 *
 * 该资产只保存地图结构数据，不保存瓦片网格、材质或贴花。
 * 视觉表现由 AGSMTile3D 及其蓝图子类自己控制。
 */
UCLASS(BlueprintType, meta = (DisplayName = "网格策略地图配置"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMMapDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UGSMMapDataAsset();

	/** 地图的用户可读名称。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置", meta = (DisplayName = "地图名称"))
	FText MapName;

	/** 创建运行时地图时使用的数据对象类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|数据类", meta = (DisplayName = "地图数据类"))
	TSubclassOf<UGSMMapData> MapDataClass;

	/** 瓦片条目没有单独覆盖时使用的数据对象类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|数据类", meta = (DisplayName = "默认瓦片数据类"))
	TSubclassOf<UGSMTileData> DefaultTileDataClass;

	/** 添加棋子时未传入数据类时使用。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|数据类", meta = (DisplayName = "默认棋子数据类"))
	TSubclassOf<UGSMPieceData> DefaultPieceDataClass;

	/** 3D 地图展示类，仅用于需要由配置主动创建展示时。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|3D展示", meta = (DisplayName = "3D地图类"))
	TSubclassOf<AGSMMap3D> Map3DClass;

	/** 3D 地图导航时生成的路径箭头类。未设置时使用 3D 地图 Actor 自身配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|3D显示", meta = (DisplayName = "3D导航箭头类", ToolTip = "3D 地图导航时用于显示路径的箭头 Actor 类。留空时使用 3D 地图 Actor 上配置的路径箭头类。"))
	TSubclassOf<AGSMPathArrow3D> PathArrowActorClass;

	/** 棋子数据未指定 3D 类时使用的默认展示类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|3D展示", meta = (DisplayName = "默认3D棋子类"))
	TSubclassOf<AGSMPiece3D> DefaultPiece3DClass;

	/** 资产级默认瓦片对象类。未设置时使用网格策略地图插件设置中的默认类。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置", meta = (DisplayName = "默认瓦片对象类", ToolTip = "地图生成瓦片时使用的默认对象类。单个瓦片条目可覆盖该设置。"))
	TSubclassOf<AGSMTile3D> DefaultTileActorClass;

	/** 整体地图地形网格体。运行时会挂到地图对象的地形组件上，并按地图范围自动缩放。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|地形", meta = (DisplayName = "整体地图地形网格体", ToolTip = "运行时会按瓦片网格范围和配置比例自动适配。"))
	TObjectPtr<UStaticMesh> WholeMapTerrainMesh;

	/** 整体地形的可选材质覆盖。数组索引对应网格体材质槽；空条目保留网格体原材质。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|地形", meta = (DisplayName = "整体地形材质覆盖", ToolTip = "按材质槽覆盖整体地形材质，例如使用支持沙盘范围裁切的材质实例。空条目保留原材质，不修改源网格体。"))
	TArray<TObjectPtr<UMaterialInterface>> WholeMapTerrainMaterialOverrides;

	/** 是否使用 CustomDepth Stencil 限制地图网格贴花的接收对象。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|贴花遮罩", meta = (DisplayName = "启用CustomStencil贴花遮罩", ToolTip = "开启后，地图会将允许接收网格贴花的组件写入 CustomDepth Stencil，贴花材质可只在匹配 Stencil 的表面显示。"))
	bool bUseCustomStencilForMapDecals = true;

	/** 地图网格贴花使用的 CustomDepth Stencil 值。项目设置中 Custom Depth-Stencil Pass 需要启用 Stencil。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|贴花遮罩", meta = (DisplayName = "贴花接收Stencil值", ClampMin = "0", ClampMax = "255"))
	int32 MapDecalReceiverStencilValue = 71;

	/** 整体地图静态网格体是否接收地图网格贴花。默认开启，使瓦片上的规划区域贴花只投到地图 Actor 的整体地图静态网格体。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|贴花遮罩", meta = (DisplayName = "整体地图网格接收贴花Stencil"))
	bool bMarkWholeMapTerrainAsDecalReceiver = true;

	/** 瓦片静态网格体是否接收地图网格贴花。默认关闭，避免瓦片 Actor 自身网格接收规划区域贴花。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|贴花遮罩", meta = (DisplayName = "瓦片网格接收贴花Stencil"))
	bool bMarkTileMeshesAsDecalReceivers = false;

	/** 是否按瓦片网格总范围自动缩放整体地形。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|地形", meta = (DisplayName = "整体地形匹配瓦片范围", ToolTip = "开启后，整体地形网格会根据瓦片数量和固定瓦片尺寸自动缩放到地图总范围。"))
	bool bFitWholeMapTerrainMeshToTileGridBounds = true;

	/** 是否让整体地形高度随平面缩放。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|地形", meta = (DisplayName = "整体地形高度随平面缩放", ToolTip = "开启后 Z 轴会随 XY 适配比例一起缩放；关闭后使用基础缩放中的 Z 值。"))
	bool bScaleWholeMapTerrainMeshZWithXY = true;

	/** 整体地形基础缩放。自动匹配范围后仍会乘上该缩放，用于做美术资源微调。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|地形", meta = (DisplayName = "整体地形基础缩放", ToolTip = "整体地形自动适配后的额外缩放倍率。可用于校准导入模型比例。"))
	FVector WholeMapTerrainMeshBaseScale = FVector::OneVector;

	/** 整体地形高度偏移。默认让地形略高于凹槽底部，避免与沙盘槽底重叠闪烁。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|地形", meta = (DisplayName = "整体地形高度偏移", ToolTip = "整体地形相对棋盘凹槽底部或自动对齐平面的高度偏移。"))
	float WholeMapTerrainMeshHeightOffset = 5.0f;

	/** 是否把整体地形当前可见区域的最低点贴到凹槽平面。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|地形", meta = (DisplayName = "整体地形底部对齐凹槽平面", ToolTip = "开启后会根据凹槽范围内的可见地形最低点调整高度，使其贴近凹槽底部平面。"))
	bool bAlignWholeMapTerrainMeshBottomToGroovePlane = true;

	/** 整体地形水平旋转角度。默认 180 度，用于修正常见导入朝向相反的问题。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|地形", meta = (DisplayName = "整体地形水平角度", ToolTip = "整体地形绕 Z 轴的水平旋转角度。默认 180 度用于修正导入方向。"))
	float WholeMapTerrainMeshYawDegrees = 180.0f;

	/** 整体地形额外相对旋转。会叠加在水平角度之后，用于处理特殊模型。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|地形", meta = (DisplayName = "整体地形相对旋转", ToolTip = "整体地形额外的相对旋转，会叠加在水平角度之后。"))
	FRotator WholeMapTerrainMeshRelativeRotation = FRotator::ZeroRotator;

	/** 整体地形额外相对偏移。用于细调模型在沙盘内的位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "网格策略地图|地形", meta = (DisplayName = "整体地形相对偏移", ToolTip = "整体地形适配范围后的额外本地位置偏移。"))
	FVector WholeMapTerrainMeshRelativeOffset = FVector::ZeroVector;

	/** 编辑器网格宽度。用于配置工具生成 A/B/C... 列表头和对应瓦片工作副本。*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|编辑器预览", meta = (DisplayName = "网格宽度", ClampMin = "1"))
	int32 EditorGridColumns = 10;

	/** 编辑器网格高度。用于配置工具生成 1/2/3... 行表头和对应瓦片工作副本。*/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|编辑器预览", meta = (DisplayName = "网格高度", ClampMin = "1"))
	int32 EditorGridRows = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|坐标", meta = (DisplayName = "横向标签类型"))
	EGSMCoordinateLabelType HorizontalLabelType = EGSMCoordinateLabelType::Letters;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|坐标", meta = (DisplayName = "纵向标签类型"))
	EGSMCoordinateLabelType VerticalLabelType = EGSMCoordinateLabelType::Numbers;

	/** 地图加载后希望置于视口中心的格子坐标，XY 从 1 开始；(3,3) 对应默认标签下的 C3，(0,0) 表示整图中心。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|默认视图", meta = (DisplayName = "默认中心格坐标", ClampMin = "0", ToolTip = "XY 从 1 开始；例如 (3,3) 表示第三列第三行（默认标签下为 C3）。(0,0) 表示整张地图中心。靠近边缘时会优先防止地图留白。"))
	FVector2D DefaultViewCenter = FVector2D::ZeroVector;

	/** 归一化缩放：1 表示完整展示全部瓦片，大于 1 表示放大。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置|默认视图", meta = (DisplayName = "默认缩放", ClampMin = "1.0", ToolTip = "1 表示完整展示全部瓦片；数值越大，瓦片显示越大。"))
	float DefaultViewScale = 1.0f;

	/** 轻量区域定义。区域只作为分组标签和点击权限，不负责表现和导航覆盖。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置", meta = (DisplayName = "区域定义"))
	TArray<FGSMRegionDefinition> RegionDefinitions;

	/** 地图瓦片列表。每个条目会生成一个瓦片 Actor。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "地图配置", meta = (DisplayName = "瓦片列表"))
	TArray<FGSMTileEntry> TileEntries;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|配置", meta = (DisplayName = "获取区域定义"))
	bool GetRegionDefinition(
		UPARAM(DisplayName = "区域标识") FName RegionId,
		UPARAM(DisplayName = "区域定义") FGSMRegionDefinition& OutRegionDefinition
	) const;

	UFUNCTION(BlueprintPure, Category = "网格策略地图|配置", meta = (DisplayName = "获取瓦片数量"))
	int32 GetTileCount() const { return TileEntries.Num(); }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|配置", meta = (DisplayName = "获取四边形瓦片尺寸"))
	float GetSquareTileSize() const { return GSMLayout::FixedSquareTileSize; }

	/** 按“瓦片类 -> 默认类 -> 基础类”的优先级解析最终生成类。 */
	TSubclassOf<AGSMTile3D> ResolveTileActorClass(const FGSMTileEntry& TileEntry) const;
	TSubclassOf<UGSMTileData> ResolveTileDataClass(const FGSMTileEntry& TileEntry) const;
};
