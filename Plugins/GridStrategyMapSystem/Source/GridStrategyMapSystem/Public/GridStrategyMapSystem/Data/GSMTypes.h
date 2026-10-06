#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "GSMTypes.generated.h"

class AGSMTile3D;
class AGSMPiece3D;
class UGSMTileData;

DECLARE_DYNAMIC_DELEGATE_TwoParams(
	FGSMNavigationFinished,
	const TArray<UGSMTileData*>&, NavigationPath,
	bool, bNavigationValid
);

/**
 * 网格策略地图寻路模式。
 */
UENUM(BlueprintType)
enum class EGSMNavigationMode : uint8
{
	WalkingOnly UMETA(DisplayName = "纯步行"),
	RoadPreferred UMETA(DisplayName = "公路优先"),
	RoadOnly UMETA(DisplayName = "仅公路"),
	Flying UMETA(DisplayName = "飞行器")
};

/**
 * 路径中相邻两个瓦片之间实际采用的连接类型。
 */
UENUM(BlueprintType)
enum class EGSMNavigationLinkType : uint8
{
	Walking UMETA(DisplayName = "步行"),
	Road UMETA(DisplayName = "公路"),
	Flying UMETA(DisplayName = "飞行")
};

/** 地图坐标轴标签的显示方式。 */
UENUM(BlueprintType)
enum class EGSMCoordinateLabelType : uint8
{
	Letters UMETA(DisplayName = "字母"),
	Numbers UMETA(DisplayName = "数字")
};

/**
 * 地图可见/可点击范围。
 *
 * 坐标使用地图 Actor 的本地 XY 平面，按 A -> B -> C -> D 顺序组成一个四边形。
 */
USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMQuadBounds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图范围", meta = (DisplayName = "角点A"))
	FVector2D CornerA = FVector2D(-500.0, -500.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图范围", meta = (DisplayName = "角点B"))
	FVector2D CornerB = FVector2D(500.0, -500.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图范围", meta = (DisplayName = "角点C"))
	FVector2D CornerC = FVector2D(500.0, 500.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图范围", meta = (DisplayName = "角点D"))
	FVector2D CornerD = FVector2D(-500.0, 500.0);

	bool ContainsPoint(const FVector2D& Point, float EdgeTolerance = 1.0f) const;
	FBox2D GetBoundsBox() const;
};

/**
 * 一条公路连接。
 *
 * 公路连接以 TileId 为目标，不要求两个瓦片在格子上相邻。
 */
USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMRoadConnection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|导航", meta = (DisplayName = "目标瓦片标识"))
	FName TargetTileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|导航", meta = (DisplayName = "代价覆盖", ClampMin = "-1.0"))
	float CostOverride = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|导航", meta = (DisplayName = "双向连接"))
	bool bBidirectional = true;
};

/**
 * 单个瓦片的导航配置。
 *
 * 该结构直接嵌入瓦片列表中，不作为独立资产使用。
 */
USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMTileNavigationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|导航", meta = (DisplayName = "允许步行通过"))
	bool bCanWalkThrough = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|导航", meta = (DisplayName = "步行进入代价", ClampMin = "0.01"))
	float WalkEnterCost = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|导航", meta = (DisplayName = "公路进入代价", ClampMin = "0.01"))
	float RoadEnterCost = 0.35f;

	/** 以下倍率用于游戏层的实际移动结算，与寻路代价分离。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|战略移动", meta = (DisplayName = "步行耗时倍率", ClampMin = "0.01"))
	float WalkTimeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|战略移动", meta = (DisplayName = "步行体力倍率", ClampMin = "0.0"))
	float WalkStaminaMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|战略移动", meta = (DisplayName = "公路耗时倍率", ClampMin = "0.01"))
	float RoadTimeMultiplier = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|战略移动", meta = (DisplayName = "车辆耗时倍率", ClampMin = "0.01"))
	float VehicleTimeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|战略移动", meta = (DisplayName = "车辆燃料倍率", ClampMin = "0.0"))
	float VehicleFuelMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|战略移动", meta = (DisplayName = "飞行耗时倍率", ClampMin = "0.01"))
	float FlyingTimeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|导航", meta = (DisplayName = "步行邻接瓦片标识"))
	TArray<FName> WalkingNeighborTileIds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|导航", meta = (DisplayName = "公路连接"))
	TArray<FGSMRoadConnection> RoadConnections;
};

/**
 * 寻路结果。
 *
 * PathTileIds 和 PathTiles 包含起点与终点；
 * PathLinkTypes 通常比 PathTileIds 少 1，用于描述每一步使用步行、公路还是飞行。
 */
USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMPathResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "地图路径", meta = (DisplayName = "是否成功"))
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "地图路径", meta = (DisplayName = "总代价"))
	float TotalCost = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "地图路径", meta = (DisplayName = "路径瓦片标识"))
	TArray<FName> PathTileIds;

	UPROPERTY(BlueprintReadOnly, Category = "地图路径", meta = (DisplayName = "路径瓦片对象"))
	TArray<TObjectPtr<UGSMTileData>> PathTiles;

	UPROPERTY(BlueprintReadOnly, Category = "地图路径", meta = (DisplayName = "路径世界坐标"))
	TArray<FVector> PathWorldLocations;

	UPROPERTY(BlueprintReadOnly, Category = "地图路径", meta = (DisplayName = "路径连接类型"))
	TArray<EGSMNavigationLinkType> PathLinkTypes;

	UPROPERTY(BlueprintReadOnly, Category = "地图路径", meta = (DisplayName = "失败原因"))
	FText FailureReason;

	void Reset();
};

/**
 * 区域定义。
 *
 * 区域现在只作为轻量逻辑分组，不再承载网格、材质、贴花或导航覆盖。
 */
USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMRegionDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图区域", meta = (DisplayName = "区域标识"))
	FName RegionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图区域", meta = (DisplayName = "显示名称"))
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图区域", meta = (DisplayName = "允许点击"))
	bool bCanBeClicked = true;
};

/**
 * 地图瓦片条目。
 *
 * 每个条目会在地图加载时生成一个瓦片 Actor。
 * 网格、材质和贴花由瓦片 Actor/瓦片蓝图自己控制；这里只保存地图拓扑、布局和导航数据。
 */
USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMTileEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片", meta = (DisplayName = "瓦片标识"))
	FName TileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片", meta = (DisplayName = "区域标识"))
	FName RegionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片", meta = (DisplayName = "显示名称"))
	FText DisplayName;

	/** 网格坐标遵循用户视角下的编辑器控件规则：(0,0) 是左上角，X 向屏幕右侧增加，Y 向屏幕下方增加。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片", meta = (DisplayName = "格子坐标"))
	FIntPoint GridCoordinate = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片", meta = (DisplayName = "本地变换"))
	FTransform LocalTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片", meta = (DisplayName = "瓦片对象类"))
	TSubclassOf<AGSMTile3D> TileActorClass;

	/** 单个瓦片可覆盖资产上的默认数据对象类，方便项目在蓝图中扩展玩法字段。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片", meta = (DisplayName = "瓦片数据类"))
	TSubclassOf<UGSMTileData> TileDataClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片", meta = (DisplayName = "允许点击"))
	bool bCanBeClicked = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图瓦片|导航", meta = (DisplayName = "导航配置"))
	FGSMTileNavigationSettings Navigation;
};

/** 棋子在瓦片展示对象上的位置参数。数据层保存该值，3D/2D 展示层负责解释。 */
USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMPiecePlacement
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子", meta = (DisplayName = "相对瓦片XY"))
	FVector2D RelativeTileXY = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子", meta = (DisplayName = "相对瓦片Z"))
	float RelativeTileZ = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子", meta = (DisplayName = "相对瓦片Yaw"))
	float RelativeTileYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子", meta = (DisplayName = "默认缩放", ClampMin = "0.0001"))
	float DefaultScale = 1.0f;
};

USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMLegacyPieceRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子", meta = (DisplayName = "棋子ID"))
	FName PieceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子", meta = (DisplayName = "瓦片ID"))
	FName TileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子", meta = (DisplayName = "棋子类"))
	TSubclassOf<AGSMPiece3D> PieceClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子", meta = (DisplayName = "相对瓦片XY"))
	FVector2D RelativeTileXY = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子", meta = (DisplayName = "相对瓦片Yaw"))
	float RelativeTileYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子", meta = (DisplayName = "默认缩放", ClampMin = "0.0001"))
	float DefaultScale = 1.0f;
};

USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMPieceMoveOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子|移动", meta = (DisplayName = "棋子类"))
	TSubclassOf<AGSMPiece3D> PieceClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子|移动", meta = (DisplayName = "目标相对瓦片XY"))
	FVector2D TargetRelativeTileXY = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子|移动", meta = (DisplayName = "目标相对瓦片Yaw"))
	float TargetRelativeTileYaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子|移动", meta = (DisplayName = "保持已有相对位置"))
	bool bKeepExistingRelativeTileXY = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子|移动", meta = (DisplayName = "保持已有相对Yaw"))
	bool bKeepExistingRelativeTileYaw = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图棋子|移动", meta = (DisplayName = "默认缩放"))
	float DefaultScale = 0.0f;
};

USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEM_API FGSMRuntimeData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "地图数据", meta = (DisplayName = "地图ID"))
	FName MapId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图数据", meta = (DisplayName = "瓦片数据"))
	TArray<FGSMTileEntry> TileEntries;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "地图数据", meta = (DisplayName = "棋子数据"))
	TArray<FGSMLegacyPieceRecord> Pieces;
};

namespace GSMLayout
{
	inline constexpr float FixedSquareTileSize = 100.0f;

	/** 将 0 -> A, 25 -> Z, 26 -> AA，用于列坐标显示。 */
	GRIDSTRATEGYMAPSYSTEM_API FString MakeColumnLabel(int32 ColumnIndex);

	/** 按指定类型生成单轴坐标标签；数字类型从 1 开始。 */
	GRIDSTRATEGYMAPSYSTEM_API FString MakeCoordinateLabel(int32 CoordinateIndex, EGSMCoordinateLabelType LabelType);

	/** 按编辑器控件规则生成瓦片显示坐标，例如 A1、B1、A2。 */
	GRIDSTRATEGYMAPSYSTEM_API FString MakeTileCoordinateLabel(FIntPoint GridCoordinate);

	/** 按地图配置的横纵标签类型生成瓦片 ID。 */
	GRIDSTRATEGYMAPSYSTEM_API FString MakeTileCoordinateLabel(
		FIntPoint GridCoordinate,
		EGSMCoordinateLabelType HorizontalLabelType,
		EGSMCoordinateLabelType VerticalLabelType
	);

	/** 计算完整网格尺寸，优先使用编辑器配置尺寸，同时兼容已有瓦片坐标超出配置的旧资产。 */
	GRIDSTRATEGYMAPSYSTEM_API void GetGridSizeFromTileEntries(
		const TArray<FGSMTileEntry>& TileEntries,
		int32 MinimumColumns,
		int32 MinimumRows,
		int32& OutColumns,
		int32& OutRows
	);

	/** 计算未缩放的网格内容尺寸。 */
	GRIDSTRATEGYMAPSYSTEM_API FVector2D GetGridContentSize(int32 ColumnCount, int32 RowCount, float TileSize);

	/** 以用户视角左上角为 A1 的规则，计算瓦片中心在地图 Actor 本地 XY 平面中的位置。 */
	GRIDSTRATEGYMAPSYSTEM_API FVector2D MakeTopLeftGridTileCenter2D(
		FIntPoint GridCoordinate,
		int32 ColumnCount,
		int32 RowCount,
		float TileSize
	);

	/** 以用户视角左上角为 A1 的规则，计算瓦片中心在地图 Actor 本地空间中的位置。 */
	GRIDSTRATEGYMAPSYSTEM_API FVector MakeTopLeftGridTileLocalLocation(
		FIntPoint GridCoordinate,
		int32 ColumnCount,
		int32 RowCount,
		float TileSize,
		float LocalZ
	);
}
