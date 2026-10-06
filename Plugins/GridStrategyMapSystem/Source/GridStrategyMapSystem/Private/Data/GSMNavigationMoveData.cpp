#include "GridStrategyMapSystem/Data/GSMNavigationMoveData.h"

#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"

void UGSMNavigationMoveData::SetNavigationPath(
	const TArray<UGSMTileData*>& InPathTiles
)
{
	PathTileIds.Reset(InPathTiles.Num());
	PathTiles.Reset(InPathTiles.Num());
	PathLinkTypes.Reset(FMath::Max(0, InPathTiles.Num() - 1));

	for (UGSMTileData* PathTile : InPathTiles)
	{
		if (!IsValid(PathTile))
		{
			continue;
		}

		PathTileIds.Add(PathTile->GetTileId());
		PathTiles.Add(PathTile);
	}

	for (int32 LinkIndex = 0; LinkIndex < PathTiles.Num() - 1; ++LinkIndex)
	{
		PathLinkTypes.Add(EGSMNavigationLinkType::Walking);
	}

	if (HasNavigationPath())
	{
		OnNavigationPathUpdated();
	}
	else
	{
		OnNavigationPathCleared();
	}
}

void UGSMNavigationMoveData::SetNavigationPathFromResult(
	const FGSMPathResult& PathResult
)
{
	PathTileIds.Reset(PathResult.PathTileIds.Num());
	PathTiles.Reset(PathResult.PathTiles.Num());
	PathLinkTypes = PathResult.PathLinkTypes;

	for (FName PathTileId : PathResult.PathTileIds)
	{
		if (!PathTileId.IsNone())
		{
			PathTileIds.Add(PathTileId);
		}
	}

	for (const TObjectPtr<UGSMTileData>& PathTile : PathResult.PathTiles)
	{
		if (IsValid(PathTile))
		{
			PathTiles.Add(PathTile);
		}
	}

	if (HasNavigationPath())
	{
		OnNavigationPathUpdated();
	}
	else
	{
		OnNavigationPathCleared();
	}
}

void UGSMNavigationMoveData::ClearNavigationPathData()
{
	if (PathTileIds.IsEmpty() && PathTiles.IsEmpty() && PathLinkTypes.IsEmpty())
	{
		return;
	}

	PathTileIds.Reset();
	PathTiles.Reset();
	PathLinkTypes.Reset();
	OnNavigationPathCleared();
}

TArray<UGSMTileData*> UGSMNavigationMoveData::GetPathTiles() const
{
	TArray<UGSMTileData*> ValidPathTiles;
	ValidPathTiles.Reserve(PathTiles.Num());

	for (const TObjectPtr<UGSMTileData>& PathTile : PathTiles)
	{
		if (IsValid(PathTile))
		{
			ValidPathTiles.Add(PathTile.Get());
		}
	}

	return ValidPathTiles;
}
