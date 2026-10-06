#pragma once

#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "UObject/Object.h"
#include "GSMMapData.generated.h"

class AGSMMap3D;
class AGSMPiece3D;
class UGSMMapDataAsset;
class UGSMMapData;
class UGSMPieceData;
class UGSMTileData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGSMMapDataCleared, UGSMMapData*, MapData);

/** 一张地图的权威运行时数据。地图展示可随时创建或销毁，不影响此对象。 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "GSM地图数据"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMMapData : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "GSM|地图")
	bool IsMapDataValid() const { return MapGuid.IsValid() && MapDataAsset.Get() != nullptr; }

	UFUNCTION(BlueprintPure, Category = "GSM|地图")
	FGuid GetMapGuid() const { return MapGuid; }

	UFUNCTION(BlueprintPure, Category = "GSM|地图")
	UGSMMapDataAsset* GetMapDataAsset() const { return MapDataAsset; }

	UFUNCTION(BlueprintPure, Category = "GSM|地图|瓦片")
	UGSMTileData* GetTileById(FName TileId) const;

	UFUNCTION(BlueprintPure, Category = "GSM|地图|瓦片")
	UGSMTileData* GetTileByGridCoordinate(FIntPoint GridCoordinate) const;

	UFUNCTION(BlueprintPure, Category = "GSM|地图|瓦片")
	TArray<UGSMTileData*> GetTiles() const;

	UFUNCTION(BlueprintPure, Category = "GSM|地图|棋子")
	UGSMPieceData* GetPieceByGuid(const FGuid& PieceGuid) const;

	UFUNCTION(BlueprintPure, Category = "GSM|地图|棋子")
	TArray<UGSMPieceData*> GetPieces() const;

	/** 删除全部棋子数据，并同步移除所有 3D/2D 棋子展示。 */
	UFUNCTION(BlueprintCallable, Category = "GSM|地图|清理", meta = (DisplayName = "清空全部棋子数据"))
	int32 ClearAllPieceData();

	/** 深度清空地图，使仍被外部持有的地图、瓦片和棋子对象全部失效。 */
	UFUNCTION(BlueprintCallable, Category = "GSM|地图|清理", meta = (DisplayName = "深度清空地图数据"))
	void ClearAllData();

	UPROPERTY(BlueprintAssignable, Category = "GSM|地图|清理", meta = (DisplayName = "当地图数据被清空"))
	FGSMMapDataCleared OnMapDataCleared;

	UFUNCTION(BlueprintCallable, Category = "GSM|地图|棋子", meta = (DisplayName = "添加棋子到瓦片ID"))
	FGuid AddPieceToTileById(
		FName TargetTileId,
		FGuid RequestedPieceGuid,
		TSubclassOf<UGSMPieceData> PieceDataClass,
		TSubclassOf<AGSMPiece3D> Piece3DClass,
		const FGSMPiecePlacement& Placement,
		UGSMPieceData*& OutPieceData
	);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图|棋子", meta = (DisplayName = "添加棋子到瓦片对象"))
	FGuid AddPieceToTile(
		UGSMTileData* TargetTile,
		FGuid RequestedPieceGuid,
		TSubclassOf<UGSMPieceData> PieceDataClass,
		TSubclassOf<AGSMPiece3D> Piece3DClass,
		const FGSMPiecePlacement& Placement,
		UGSMPieceData*& OutPieceData
	);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图|棋子")
	bool RemovePieceByGuid(const FGuid& PieceGuid);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图|棋子")
	bool UpdatePiece(const FGuid& PieceGuid, TSubclassOf<AGSMPiece3D> Piece3DClass,
		const FGSMPiecePlacement& Placement, UGSMPieceData*& OutPieceData);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图|棋子", meta = (DisplayName = "按棋子ID移动到瓦片ID"))
	bool MovePieceByGuidToTileId(const FGuid& PieceGuid, FName TargetTileId,
		const FGSMPiecePlacement& Placement, UGSMPieceData*& OutPieceData);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图|棋子", meta = (DisplayName = "按棋子ID移动到瓦片对象"))
	bool MovePieceByGuidToTile(const FGuid& PieceGuid, UGSMTileData* TargetTile,
		const FGSMPiecePlacement& Placement, UGSMPieceData*& OutPieceData);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图|棋子", meta = (DisplayName = "按棋子数据移动到瓦片ID"))
	bool MovePieceDataToTileId(UGSMPieceData* PieceData, FName TargetTileId,
		const FGSMPiecePlacement& Placement);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图|棋子", meta = (DisplayName = "按棋子数据移动到瓦片对象"))
	bool MovePieceDataToTile(UGSMPieceData* PieceData, UGSMTileData* TargetTile,
		const FGSMPiecePlacement& Placement);

	UFUNCTION(BlueprintCallable, Category = "GSM|地图|导航")
	bool FindPathByTileIds(FName StartTileId, FName GoalTileId,
		EGSMNavigationMode NavigationMode, FGSMPathResult& OutPath) const;

	UFUNCTION(BlueprintCallable, Category = "GSM|地图|导航")
	bool FindPathByTiles(UGSMTileData* StartTile, UGSMTileData* GoalTile,
		EGSMNavigationMode NavigationMode, FGSMPathResult& OutPath) const;

	UFUNCTION(BlueprintPure, Category = "GSM|地图|展示")
	AGSMMap3D* GetMap3D() const { return Map3D.Get(); }

private:
	friend class UGSMMapSubsystem;
	friend class AGSMMap3D;

	bool InitializeMap(const FGuid& InMapGuid, UGSMMapDataAsset* InMapDataAsset);
	bool RegisterMap3D(AGSMMap3D* InMap3D);
	void UnregisterMap3D(AGSMMap3D* InMap3D);
	bool IsAdjacentCoordinate(const FIntPoint& A, const FIntPoint& B) const;
	void ClearTileDataObjects();

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|地图", meta = (AllowPrivateAccess = "true"))
	FGuid MapGuid;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|地图", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGSMMapDataAsset> MapDataAsset;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UGSMTileData>> TilesById;

	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<UGSMTileData>> TilesByCoordinate;

	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<UGSMPieceData>> PiecesByGuid;

	UPROPERTY(Transient)
	TWeakObjectPtr<AGSMMap3D> Map3D;
};
