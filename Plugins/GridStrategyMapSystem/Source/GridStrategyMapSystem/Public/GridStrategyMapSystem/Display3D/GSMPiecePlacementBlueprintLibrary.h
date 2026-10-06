#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GSMPiecePlacementBlueprintLibrary.generated.h"

class AGSMMap3D;
class AGSMTile3D;

/** 瓦片本地空间中的四个角。正 X / 正 Y 的含义跟随瓦片 Actor 的本地坐标轴。 */
UENUM(BlueprintType)
enum class EGSMTileQueueCorner : uint8
{
	NegativeXNegativeY UMETA(DisplayName = "左下角（-X，-Y）"),
	NegativeXPositiveY UMETA(DisplayName = "左上角（-X，+Y）"),
	PositiveXPositiveY UMETA(DisplayName = "右上角（+X，+Y）"),
	PositiveXNegativeY UMETA(DisplayName = "右下角（+X，-Y）")
};

/** 边缘排队换行时的推进方向。自动朝中心适合绝大多数沿边排队。 */
UENUM(BlueprintType)
enum class EGSMTileQueueRowDirection : uint8
{
	TowardCenter UMETA(DisplayName = "自动朝瓦片中心"),
	PositiveX UMETA(DisplayName = "本地 +X"),
	NegativeX UMETA(DisplayName = "本地 -X"),
	PositiveY UMETA(DisplayName = "本地 +Y"),
	NegativeY UMETA(DisplayName = "本地 -Y")
};

/**
 * 3D 地图棋子排队工具蓝图函数库。
 *
 * 所有位置都使用瓦片本地坐标完成布局，再转换成世界坐标；因此会跟随瓦片旋转、Actor 缩放和地图当前缩放。
 * 排队函数只负责平面布局，返回位置的 Z 使用瓦片自身高度；需要贴合地形时，再调用地形 Z 计算函数。
 */
UCLASS(meta = (DisplayName = "3D地图棋子排队工具库"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMPiecePlacementBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 以瓦片中心为中心紧凑排队。最后一行不足最大列数时，该行会单独居中（旧版排队逻辑）。
	 * 输出数量不会超过 最大列数 × 最大行数。
	 */
	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|棋子排队", meta = (DisplayName = "计算瓦片居中排队世界位置"))
	static TArray<FVector> CalculateCenteredQueueWorldLocations(
		UPARAM(DisplayName = "3D瓦片") const AGSMTile3D* Tile,
		UPARAM(DisplayName = "棋子数量") int32 PieceCount,
		UPARAM(DisplayName = "一行最大棋子数") int32 MaxColumns = 5,
		UPARAM(DisplayName = "最大棋子行数") int32 MaxRows = 2,
		UPARAM(DisplayName = "棋子间隔") float Spacing = 25.0f,
		UPARAM(DisplayName = "排队角度") float ArrangementAngleDegrees = 0.0f
	);

	/**
	 * 从瓦片同一条边上的一个角到另一个角建立首行，并沿指定方向换行。
	 * 每行槽位会在起点和终点之间自动等距分布；最后一行从起点开始占用剩余槽位。
	 * 行距也会根据最大行数和推进方向在瓦片范围内自动计算。
	 */
	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|棋子排队", meta = (DisplayName = "计算瓦片边缘排队世界位置"))
	static TArray<FVector> CalculateEdgeQueueWorldLocations(
		UPARAM(DisplayName = "3D瓦片") const AGSMTile3D* Tile,
		UPARAM(DisplayName = "棋子数量") int32 PieceCount,
		UPARAM(DisplayName = "横向起点角") EGSMTileQueueCorner StartCorner = EGSMTileQueueCorner::NegativeXPositiveY,
		UPARAM(DisplayName = "横向终点角") EGSMTileQueueCorner EndCorner = EGSMTileQueueCorner::PositiveXPositiveY,
		UPARAM(DisplayName = "换行方向") EGSMTileQueueRowDirection RowDirection = EGSMTileQueueRowDirection::TowardCenter,
		UPARAM(DisplayName = "一行最大棋子数") int32 MaxPiecesPerRow = 5,
		UPARAM(DisplayName = "最大棋子行数") int32 MaxRows = 2,
		UPARAM(DisplayName = "边缘内缩") float EdgeInset = 10.0f
	);

	/** 使用世界位置中的 XY，在地图当前缩放后的地形网格体上插值计算世界 Z；输入位置的 Z 不参与采样。 */
	UFUNCTION(BlueprintPure, Category = "GSM|3D地图|地形", meta = (DisplayName = "通过世界位置计算地图地形Z"))
	static bool CalculateMapTerrainWorldZAtXY(
		UPARAM(DisplayName = "3D地图") const AGSMMap3D* Map3D,
		UPARAM(DisplayName = "世界位置") FVector WorldLocation,
		UPARAM(DisplayName = "高度偏移") float HeightOffset,
		UPARAM(DisplayName = "世界Z") float& OutWorldZ
	);
};
