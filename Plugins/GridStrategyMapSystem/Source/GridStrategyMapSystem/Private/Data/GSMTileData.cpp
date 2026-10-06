#include "GridStrategyMapSystem/Data/GSMTileData.h"

#include "GridStrategyMapSystem/Data/GSMMapData.h"
#include "GridStrategyMapSystem/Data/GSMPieceData.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"

void UGSMTileData::InitializeTile(
	UGSMMapData* InMapData,
	const FGuid& InMapGuid,
	const FGSMTileEntry& InTileEntry)
{
	MapData = InMapData;
	MapGuid = InMapGuid;
	TileEntry = InTileEntry;
}

UGSMPieceData* UGSMTileData::GetPieceByGuid(const FGuid& PieceGuid) const
{
	const TObjectPtr<UGSMPieceData>* Found = PiecesByGuid.Find(PieceGuid);
	return Found ? Found->Get() : nullptr;
}

TArray<UGSMPieceData*> UGSMTileData::GetPieces() const
{
	TArray<UGSMPieceData*> Result;
	Result.Reserve(PiecesByGuid.Num());
	for (const TPair<FGuid, TObjectPtr<UGSMPieceData>>& Pair : PiecesByGuid)
	{
		if (IsValid(Pair.Value))
		{
			Result.Add(Pair.Value);
		}
	}
	return Result;
}

bool UGSMTileData::AttachPiece(UGSMPieceData* PieceData)
{
	if (!IsValid(PieceData) || !PieceData->GetPieceGuid().IsValid() || PiecesByGuid.Contains(PieceData->GetPieceGuid()))
	{
		return false;
	}

	PiecesByGuid.Add(PieceData->GetPieceGuid(), PieceData);
	PieceData->SetOwningTile(this);
	PieceData->OnPieceDataChanged.AddUniqueDynamic(this, &UGSMTileData::HandlePieceDataChanged);
	OnPieceAdded.Broadcast(PieceData, PieceData->GetPlacement());
	ReceivePieceAdded(PieceData, PieceData->GetPlacement());
	NotifyTileDataUpdated();
	return true;
}

bool UGSMTileData::DetachPiece(UGSMPieceData* PieceData)
{
	if (!IsValid(PieceData) || PiecesByGuid.Remove(PieceData->GetPieceGuid()) == 0)
	{
		return false;
	}

	PieceData->OnPieceDataChanged.RemoveDynamic(this, &UGSMTileData::HandlePieceDataChanged);
	OnPieceRemoved.Broadcast(PieceData);
	ReceivePieceRemoved(PieceData);
	NotifyTileDataUpdated();
	return true;
}

void UGSMTileData::RegisterTile3D(AGSMTile3D* InTile3D)
{
	Tile3D = InTile3D;
	for (UGSMPieceData* PieceData : GetPieces())
	{
		OnPieceAdded.Broadcast(PieceData, PieceData->GetPlacement());
		ReceivePieceAdded(PieceData, PieceData->GetPlacement());
	}
}

void UGSMTileData::UnregisterTile3D(AGSMTile3D* InTile3D)
{
	if (Tile3D.Get() == InTile3D)
	{
		Tile3D.Reset();
	}
}

void UGSMTileData::HandlePieceDataChanged(UGSMPieceData* PieceData)
{
	if (!IsValid(PieceData) || !PiecesByGuid.Contains(PieceData->GetPieceGuid()))
	{
		return;
	}

	OnPieceUpdated.Broadcast(PieceData);
	ReceivePieceUpdated(PieceData);
	NotifyTileDataUpdated();
}

void UGSMTileData::NotifyTileDataUpdated()
{
	OnTileDataUpdated.Broadcast(this);
	ReceiveTileDataUpdated(this);
}

void UGSMTileData::InvalidateTileData()
{
	if (AGSMTile3D* ExistingTile3D = Tile3D.Get())
	{
		ExistingTile3D->BindTileData(nullptr);
	}

	for (const TPair<FGuid, TObjectPtr<UGSMPieceData>>& Pair : PiecesByGuid)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->OnPieceDataChanged.RemoveDynamic(this, &UGSMTileData::HandlePieceDataChanged);
		}
	}

	PiecesByGuid.Reset();
	Tile3D.Reset();
	Tile2D.Reset();
	MapData.Reset();
	MapGuid.Invalidate();
	TileEntry = FGSMTileEntry();
	OnPieceAdded.Clear();
	OnPieceUpdated.Clear();
	OnPieceRemoved.Clear();
	OnTileDataUpdated.Clear();
}
