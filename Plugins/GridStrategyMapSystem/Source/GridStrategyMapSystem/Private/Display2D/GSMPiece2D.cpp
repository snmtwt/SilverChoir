#include "GridStrategyMapSystem/Display2D/GSMPiece2D.h"

#include "GridStrategyMapSystem/Data/GSMPieceData.h"

void UGSMPiece2D::NativeDestruct()
{
	BindPieceData(nullptr);
	Super::NativeDestruct();
}

void UGSMPiece2D::BindPieceData(UGSMPieceData* InPieceData)
{
	if (PieceData == InPieceData)
	{
		return;
	}

	if (PieceData)
	{
		PieceData->UnregisterPiece2D(this);
	}
	PieceData = InPieceData;
	if (PieceData)
	{
		PieceData->RegisterPiece2D(this);
	}
	ReceivePieceDataBound(PieceData);
}

void UGSMPiece2D::OnBoundPieceDataUpdated_Implementation(UGSMPieceData* UpdatedPieceData)
{
}

void UGSMPiece2D::OnBoundPieceMovedBetweenTiles_Implementation(
	UGSMPieceData* MovedPieceData,
	UGSMTileData* PreviousTileData,
	UGSMTileData* CurrentTileData)
{
}
