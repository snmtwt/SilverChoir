#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GSMPiece2D.generated.h"

class UGSMPieceData;
class UGSMTileData;

/** 可由任意 2D 地图视图创建的棋子控件基类；同一数据对象允许同时绑定多个 2D 棋子。 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "GSM 2D棋子"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMPiece2D : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintCallable, Category = "GSM|2D展示|棋子")
	void BindPieceData(UGSMPieceData* InPieceData);

	UFUNCTION(BlueprintPure, Category = "GSM|2D展示|棋子")
	UGSMPieceData* GetPieceData() const { return PieceData; }

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|2D展示|棋子", meta = (DisplayName = "当2D棋子绑定数据"))
	void ReceivePieceDataBound(UGSMPieceData* InPieceData);

	UFUNCTION(BlueprintNativeEvent, Category = "GSM|2D展示|棋子", meta = (DisplayName = "当2D棋子数据更新"))
	void OnBoundPieceDataUpdated(UGSMPieceData* UpdatedPieceData);

	UFUNCTION(BlueprintNativeEvent, Category = "GSM|2D展示|棋子", meta = (DisplayName = "当2D棋子移动到新瓦片"))
	void OnBoundPieceMovedBetweenTiles(
		UGSMPieceData* MovedPieceData,
		UGSMTileData* PreviousTileData,
		UGSMTileData* CurrentTileData);

protected:
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|2D展示|棋子")
	TObjectPtr<UGSMPieceData> PieceData;
};
