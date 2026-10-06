#include "GridStrategyMapSystem/Data/GSMPieceData.h"

#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Display2D/GSMPiece2D.h"
#include "GridStrategyMapSystem/Display3D/GSMPiece3D.h"

namespace
{
bool ArePlacementsEquivalent(const FGSMPiecePlacement& A, const FGSMPiecePlacement& B)
{
	return A.RelativeTileXY.Equals(B.RelativeTileXY)
		&& FMath::IsNearlyEqual(A.RelativeTileZ, B.RelativeTileZ)
		&& FMath::IsNearlyEqual(A.RelativeTileYaw, B.RelativeTileYaw)
		&& FMath::IsNearlyEqual(A.DefaultScale, B.DefaultScale);
}
}

void UGSMPieceData::InitializePiece(
	const FGuid& InPieceGuid,
	const FGuid& InMapGuid,
	UGSMTileData* InTileData,
	TSubclassOf<AGSMPiece3D> InPiece3DClass,
	const FGSMPiecePlacement& InPlacement)
{
	PieceGuid = InPieceGuid;
	MapGuid = InMapGuid;
	Piece3DClass = InPiece3DClass;
	Placement = InPlacement;
	SetOwningTile(InTileData);
}

void UGSMPieceData::SetPlacement(const FGSMPiecePlacement& NewPlacement)
{
	FGSMPiecePlacement SanitizedPlacement = NewPlacement;
	SanitizedPlacement.DefaultScale = FMath::Max(SanitizedPlacement.DefaultScale, KINDA_SMALL_NUMBER);
	if (ArePlacementsEquivalent(Placement, SanitizedPlacement))
	{
		return;
	}

	Placement = SanitizedPlacement;
	NotifyPieceDataChanged();
}

void UGSMPieceData::SetPiece3DClass(TSubclassOf<AGSMPiece3D> NewPiece3DClass)
{
	if (Piece3DClass == NewPiece3DClass)
	{
		return;
	}

	Piece3DClass = NewPiece3DClass;
	NotifyPieceDataChanged();
}

TArray<UGSMPiece2D*> UGSMPieceData::GetPiece2DInstances() const
{
	TArray<UGSMPiece2D*> Result;
	Result.Reserve(Piece2DInstances.Num());
	for (const TWeakObjectPtr<UGSMPiece2D>& Piece2D : Piece2DInstances)
	{
		if (Piece2D.IsValid())
		{
			Result.Add(Piece2D.Get());
		}
	}
	return Result;
}

void UGSMPieceData::SetOwningTile(UGSMTileData* InTileData)
{
	TileData = InTileData;
	TileId = InTileData ? InTileData->GetTileId() : NAME_None;
}

void UGSMPieceData::SetPiece3D(AGSMPiece3D* InPiece3D)
{
	Piece3D = InPiece3D;
}

void UGSMPieceData::RegisterPiece2D(UGSMPiece2D* InPiece2D)
{
	if (!IsValid(InPiece2D))
	{
		return;
	}

	Piece2DInstances.RemoveAll([](const TWeakObjectPtr<UGSMPiece2D>& Item)
	{
		return !Item.IsValid();
	});
	Piece2DInstances.AddUnique(InPiece2D);
}

void UGSMPieceData::UnregisterPiece2D(UGSMPiece2D* InPiece2D)
{
	Piece2DInstances.RemoveAll([InPiece2D](const TWeakObjectPtr<UGSMPiece2D>& Item)
	{
		return !Item.IsValid() || Item.Get() == InPiece2D;
	});
}

void UGSMPieceData::NotifyPieceDataChanged()
{
	OnPieceDataChanged.Broadcast(this);
	ReceivePieceDataChanged();

	if (AGSMPiece3D* BoundPiece3D = Piece3D.Get())
	{
		BoundPiece3D->OnBoundPieceDataUpdated(this);
	}
	for (UGSMPiece2D* BoundPiece2D : GetPiece2DInstances())
	{
		BoundPiece2D->OnBoundPieceDataUpdated(this);
	}
}

void UGSMPieceData::NotifyMovedBetweenTiles(
	UGSMTileData* PreviousTileData,
	UGSMTileData* CurrentTileData)
{
	if (!IsValid(PreviousTileData) || !IsValid(CurrentTileData) || PreviousTileData == CurrentTileData)
	{
		return;
	}

	OnPieceTileChanged.Broadcast(this, PreviousTileData, CurrentTileData);
	OnPieceTileChangedNative.Broadcast(this, PreviousTileData, CurrentTileData);
	OnPieceMovedBetweenTiles(PreviousTileData, CurrentTileData);
	if (IsValid(this))
	{
		HandlePieceMovedBetweenTilesNative(PreviousTileData, CurrentTileData);
	}

	if (AGSMPiece3D* BoundPiece3D = Piece3D.Get())
	{
		BoundPiece3D->OnBoundPieceMovedBetweenTiles(this, PreviousTileData, CurrentTileData);
	}
	for (UGSMPiece2D* BoundPiece2D : GetPiece2DInstances())
	{
		BoundPiece2D->OnBoundPieceMovedBetweenTiles(this, PreviousTileData, CurrentTileData);
	}
}

void UGSMPieceData::OnPieceMovedBetweenTiles_Implementation(
	UGSMTileData* PreviousTileData,
	UGSMTileData* CurrentTileData)
{
}

void UGSMPieceData::InvalidatePieceData()
{
	if (AGSMPiece3D* ExistingPiece3D = Piece3D.Get())
	{
		ExistingPiece3D->BindPieceData(nullptr);
	}
	for (UGSMPiece2D* ExistingPiece2D : GetPiece2DInstances())
	{
		ExistingPiece2D->BindPieceData(nullptr);
	}

	Piece3D.Reset();
	Piece2DInstances.Reset();
	TileData.Reset();
	PieceGuid.Invalidate();
	MapGuid.Invalidate();
	TileId = NAME_None;
	Piece3DClass = nullptr;
	Placement = FGSMPiecePlacement();
	OnPieceDataChanged.Clear();
	OnPieceTileChanged.Clear();
	OnPieceTileChangedNative.Clear();
}
