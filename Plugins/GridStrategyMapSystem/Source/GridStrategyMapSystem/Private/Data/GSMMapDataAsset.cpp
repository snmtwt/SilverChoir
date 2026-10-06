#include "GridStrategyMapSystem/Data/GSMMapDataAsset.h"

#include "GridStrategyMapSystem/Data/GSMSettings.h"
#include "GridStrategyMapSystem/Data/GSMMapData.h"
#include "GridStrategyMapSystem/Data/GSMPieceData.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "GridStrategyMapSystem/Display3D/GSMPiece3D.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"

UGSMMapDataAsset::UGSMMapDataAsset()
{
	EditorGridColumns = 10;
	EditorGridRows = 8;
	MapDataClass = UGSMMapData::StaticClass();
	DefaultTileDataClass = UGSMTileData::StaticClass();
	DefaultPieceDataClass = UGSMPieceData::StaticClass();
	Map3DClass = AGSMMap3D::StaticClass();
	DefaultPiece3DClass = AGSMPiece3D::StaticClass();
}

TSubclassOf<UGSMTileData> UGSMMapDataAsset::ResolveTileDataClass(const FGSMTileEntry& TileEntry) const
{
	if (TileEntry.TileDataClass)
	{
		return TileEntry.TileDataClass;
	}
	if (DefaultTileDataClass)
	{
		return DefaultTileDataClass;
	}
	return UGSMTileData::StaticClass();
}

bool UGSMMapDataAsset::GetRegionDefinition(
	FName RegionId,
	FGSMRegionDefinition& OutRegionDefinition
) const
{
	for (const FGSMRegionDefinition& RegionDefinition : RegionDefinitions)
	{
		if (RegionDefinition.RegionId == RegionId)
		{
			OutRegionDefinition = RegionDefinition;
			return true;
		}
	}

	return false;
}

TSubclassOf<AGSMTile3D> UGSMMapDataAsset::ResolveTileActorClass(
	const FGSMTileEntry& TileEntry
) const
{
	if (TileEntry.TileActorClass)
	{
		return TileEntry.TileActorClass;
	}

	if (DefaultTileActorClass)
	{
		return DefaultTileActorClass;
	}

	return UGSMSettings::GetDefaultTileActorClass();
}
