#include "GridStrategyMapSystem/Display2D/GSMMapWidget2D.h"

#include "GridStrategyMapSystem/Data/GSMMapData.h"
#include "GridStrategyMapSystem/Data/GSMPieceData.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"

void UGSMMapWidget2D::NativeDestruct()
{
	UnbindMapEvents();
	MapData = nullptr;
	Super::NativeDestruct();
}

void UGSMMapWidget2D::SetMapData(UGSMMapData* InMapData)
{
	if (MapData == InMapData)
	{
		return;
	}
	UnbindMapEvents();
	MapData = InMapData;
	BindMapEvents();
	ReceiveMapDataLoaded(MapData);
	RefreshMap2D();
}

void UGSMMapWidget2D::BindMapEvents()
{
	if (!MapData)
	{
		return;
	}
	MapData->OnMapDataCleared.AddUniqueDynamic(this, &UGSMMapWidget2D::HandleMapDataCleared);
	for (UGSMTileData* TileData : MapData->GetTiles())
	{
		if (!TileData)
		{
			continue;
		}
		TileData->OnPieceAdded.AddUniqueDynamic(this, &UGSMMapWidget2D::HandlePieceAdded);
		TileData->OnPieceUpdated.AddUniqueDynamic(this, &UGSMMapWidget2D::HandlePieceUpdated);
		TileData->OnPieceRemoved.AddUniqueDynamic(this, &UGSMMapWidget2D::HandlePieceRemoved);
		BoundTiles.Add(TileData);
	}
}

void UGSMMapWidget2D::UnbindMapEvents()
{
	if (MapData)
	{
		MapData->OnMapDataCleared.RemoveDynamic(this, &UGSMMapWidget2D::HandleMapDataCleared);
	}
	for (const TWeakObjectPtr<UGSMTileData>& Tile : BoundTiles)
	{
		if (UGSMTileData* TileData = Tile.Get())
		{
			TileData->OnPieceAdded.RemoveDynamic(this, &UGSMMapWidget2D::HandlePieceAdded);
			TileData->OnPieceUpdated.RemoveDynamic(this, &UGSMMapWidget2D::HandlePieceUpdated);
			TileData->OnPieceRemoved.RemoveDynamic(this, &UGSMMapWidget2D::HandlePieceRemoved);
		}
	}
	BoundTiles.Reset();
}

void UGSMMapWidget2D::HandlePieceAdded(UGSMPieceData* PieceData, const FGSMPiecePlacement& Placement)
{
	ReceivePieceAdded2D(PieceData ? PieceData->GetTileData() : nullptr, PieceData, Placement);
}

void UGSMMapWidget2D::HandlePieceUpdated(UGSMPieceData* PieceData)
{
	ReceivePieceUpdated2D(PieceData ? PieceData->GetTileData() : nullptr, PieceData);
}

void UGSMMapWidget2D::HandlePieceRemoved(UGSMPieceData* PieceData)
{
	ReceivePieceRemoved2D(PieceData ? PieceData->GetTileData() : nullptr, PieceData);
}

void UGSMMapWidget2D::HandleMapDataCleared(UGSMMapData* ClearedMapData)
{
	if (MapData == ClearedMapData)
	{
		SetMapData(nullptr);
	}
}
