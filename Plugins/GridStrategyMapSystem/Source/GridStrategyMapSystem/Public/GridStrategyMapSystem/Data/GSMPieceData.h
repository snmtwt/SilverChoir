#pragma once

#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "UObject/Object.h"
#include "GSMPieceData.generated.h"

class AGSMPiece3D;
class UGSMPiece2D;
class UGSMTileData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGSMPieceDataChanged, UGSMPieceData*, PieceData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FGSMPieceTileChanged,
	UGSMPieceData*, PieceData,
	UGSMTileData*, PreviousTileData,
	UGSMTileData*, CurrentTileData
);
DECLARE_MULTICAST_DELEGATE_ThreeParams(
	FGSMPieceTileChangedNative,
	UGSMPieceData*,
	UGSMTileData*,
	UGSMTileData*
);

/** 独立于任何场景 Actor 的棋子运行时数据。 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "GSM棋子数据"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMPieceData : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "GSM|棋子")
	bool IsPieceDataValid() const { return PieceGuid.IsValid() && MapGuid.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "GSM|棋子")
	FGuid GetPieceGuid() const { return PieceGuid; }

	UFUNCTION(BlueprintPure, Category = "GSM|棋子")
	FGuid GetMapGuid() const { return MapGuid; }

	UFUNCTION(BlueprintPure, Category = "GSM|棋子")
	FName GetTileId() const { return TileId; }

	UFUNCTION(BlueprintPure, Category = "GSM|棋子")
	UGSMTileData* GetTileData() const { return TileData.Get(); }

	UFUNCTION(BlueprintPure, Category = "GSM|棋子")
	FGSMPiecePlacement GetPlacement() const { return Placement; }

	UFUNCTION(BlueprintPure, Category = "GSM|棋子")
	TSubclassOf<AGSMPiece3D> GetPiece3DClass() const { return Piece3DClass; }

	UFUNCTION(BlueprintCallable, Category = "GSM|棋子")
	void SetPlacement(const FGSMPiecePlacement& NewPlacement);

	UFUNCTION(BlueprintCallable, Category = "GSM|棋子")
	void SetPiece3DClass(TSubclassOf<AGSMPiece3D> NewPiece3DClass);

	UFUNCTION(BlueprintPure, Category = "GSM|棋子")
	AGSMPiece3D* GetPiece3D() const { return Piece3D.Get(); }

	UFUNCTION(BlueprintPure, Category = "GSM|棋子")
	TArray<UGSMPiece2D*> GetPiece2DInstances() const;

	UPROPERTY(BlueprintAssignable, Category = "GSM|棋子")
	FGSMPieceDataChanged OnPieceDataChanged;

	UPROPERTY(BlueprintAssignable, Category = "GSM|棋子")
	FGSMPieceTileChanged OnPieceTileChanged;

	/** C++ listener variant of OnPieceTileChanged. */
	FGSMPieceTileChangedNative OnPieceTileChangedNative;

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|棋子", meta = (DisplayName = "当棋子数据更新"))
	void ReceivePieceDataChanged();

	/** 棋子成功从一个瓦片移动到另一个瓦片后调用。子类和蓝图可在这里处理游戏侧状态。 */
	UFUNCTION(BlueprintNativeEvent, Category = "GSM|棋子", meta = (DisplayName = "当棋子移动到新瓦片"))
	void OnPieceMovedBetweenTiles(UGSMTileData* PreviousTileData, UGSMTileData* CurrentTileData);

protected:
	/** 子类批量修改自定义字段后，通过该入口只发送一次数据更新通知。 */
	UFUNCTION(BlueprintCallable, Category = "GSM|棋子", meta = (BlueprintProtected, DisplayName = "通知棋子数据已更新"))
	void NotifyPieceDataChanged();

	/** Native hook always runs after the Blueprint cross-tile event. */
	virtual void HandlePieceMovedBetweenTilesNative(
		UGSMTileData* PreviousTileData,
		UGSMTileData* CurrentTileData) {}

private:
	friend class UGSMMapData;
	friend class UGSMTileData;
	friend class AGSMPiece3D;
	friend class UGSMPiece2D;

	void InitializePiece(const FGuid& InPieceGuid, const FGuid& InMapGuid, UGSMTileData* InTileData,
		TSubclassOf<AGSMPiece3D> InPiece3DClass, const FGSMPiecePlacement& InPlacement);
	void SetOwningTile(UGSMTileData* InTileData);
	void SetPiece3D(AGSMPiece3D* InPiece3D);
	void RegisterPiece2D(UGSMPiece2D* InPiece2D);
	void UnregisterPiece2D(UGSMPiece2D* InPiece2D);
	void NotifyMovedBetweenTiles(UGSMTileData* PreviousTileData, UGSMTileData* CurrentTileData);
	void InvalidatePieceData();

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|棋子", meta = (AllowPrivateAccess = "true"))
	FGuid PieceGuid;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|棋子", meta = (AllowPrivateAccess = "true"))
	FGuid MapGuid;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|棋子", meta = (AllowPrivateAccess = "true"))
	FName TileId = NAME_None;

	UPROPERTY(Transient)
	TWeakObjectPtr<UGSMTileData> TileData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GSM|棋子", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AGSMPiece3D> Piece3DClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "GSM|棋子", meta = (AllowPrivateAccess = "true"))
	FGSMPiecePlacement Placement;

	UPROPERTY(Transient)
	TWeakObjectPtr<AGSMPiece3D> Piece3D;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UGSMPiece2D>> Piece2DInstances;
};
