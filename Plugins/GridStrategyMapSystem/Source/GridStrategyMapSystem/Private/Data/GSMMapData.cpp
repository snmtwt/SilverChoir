#include "GridStrategyMapSystem/Data/GSMMapData.h"

#include "GridStrategyMapSystem/Data/GSMMapDataAsset.h"
#include "GridStrategyMapSystem/Data/GSMMapSubsystem.h"
#include "GridStrategyMapSystem/Data/GSMPieceData.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "GridStrategyMapSystem/Display3D/GSMPiece3D.h"

DEFINE_LOG_CATEGORY_STATIC(LogGSMMapData, Log, All);

bool UGSMMapData::InitializeMap(const FGuid& InMapGuid, UGSMMapDataAsset* InMapDataAsset)
{
	if (!InMapGuid.IsValid() || !IsValid(InMapDataAsset))
	{
		return false;
	}

	MapGuid = InMapGuid;
	MapDataAsset = InMapDataAsset;
	TilesById.Reset();
	TilesByCoordinate.Reset();
	PiecesByGuid.Reset();

	for (const FGSMTileEntry& SourceEntry : MapDataAsset->TileEntries)
	{
		FGSMTileEntry Entry = SourceEntry;
		if (Entry.TileId.IsNone())
		{
			Entry.TileId = FName(*GSMLayout::MakeTileCoordinateLabel(
				Entry.GridCoordinate,
				MapDataAsset->HorizontalLabelType,
				MapDataAsset->VerticalLabelType));
		}

		if (TilesById.Contains(Entry.TileId) || TilesByCoordinate.Contains(Entry.GridCoordinate))
		{
			UE_LOG(LogGSMMapData, Error,
				TEXT("Create map %s failed: duplicate tile id or coordinate (%s, %d,%d)."),
				*MapGuid.ToString(), *Entry.TileId.ToString(), Entry.GridCoordinate.X, Entry.GridCoordinate.Y);
			TilesById.Reset();
			TilesByCoordinate.Reset();
			return false;
		}

		UClass* TileDataClass = MapDataAsset->ResolveTileDataClass(Entry).Get();
		UGSMTileData* TileData = NewObject<UGSMTileData>(this, TileDataClass);
		if (!TileData)
		{
			return false;
		}

		TileData->InitializeTile(this, MapGuid, Entry);
		TilesById.Add(Entry.TileId, TileData);
		TilesByCoordinate.Add(Entry.GridCoordinate, TileData);
	}

	return TilesById.Num() > 0;
}

UGSMTileData* UGSMMapData::GetTileById(FName TileId) const
{
	const TObjectPtr<UGSMTileData>* Found = TilesById.Find(TileId);
	return Found ? Found->Get() : nullptr;
}

UGSMTileData* UGSMMapData::GetTileByGridCoordinate(FIntPoint GridCoordinate) const
{
	const TObjectPtr<UGSMTileData>* Found = TilesByCoordinate.Find(GridCoordinate);
	return Found ? Found->Get() : nullptr;
}

TArray<UGSMTileData*> UGSMMapData::GetTiles() const
{
	TArray<UGSMTileData*> Result;
	Result.Reserve(TilesById.Num());
	for (const TPair<FName, TObjectPtr<UGSMTileData>>& Pair : TilesById)
	{
		if (IsValid(Pair.Value))
		{
			Result.Add(Pair.Value);
		}
	}
	Result.Sort([](const UGSMTileData& A, const UGSMTileData& B)
	{
		const FIntPoint AC = A.GetGridCoordinate();
		const FIntPoint BC = B.GetGridCoordinate();
		return AC.Y == BC.Y ? AC.X < BC.X : AC.Y < BC.Y;
	});
	return Result;
}

UGSMPieceData* UGSMMapData::GetPieceByGuid(const FGuid& PieceGuid) const
{
	const TObjectPtr<UGSMPieceData>* Found = PiecesByGuid.Find(PieceGuid);
	return Found ? Found->Get() : nullptr;
}

TArray<UGSMPieceData*> UGSMMapData::GetPieces() const
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

int32 UGSMMapData::ClearAllPieceData()
{
	const TArray<UGSMPieceData*> ExistingPieces = GetPieces();
	int32 RemovedCount = 0;
	for (UGSMPieceData* PieceData : ExistingPieces)
	{
		if (IsValid(PieceData) && RemovePieceByGuid(PieceData->GetPieceGuid()))
		{
			++RemovedCount;
		}
	}
	PiecesByGuid.Reset();
	return RemovedCount;
}

void UGSMMapData::ClearAllData()
{
	if (UGSMMapSubsystem* Subsystem = GetTypedOuter<UGSMMapSubsystem>();
		Subsystem && Subsystem->GetMapDataByGuid(MapGuid) == this)
	{
		Subsystem->RemoveMapDataObject(this);
		return;
	}

	if (!MapGuid.IsValid() && !IsValid(MapDataAsset) && TilesById.IsEmpty()
		&& PiecesByGuid.IsEmpty() && !Map3D.IsValid())
	{
		return;
	}

	ClearAllPieceData();
	if (AGSMMap3D* ExistingMap3D = Map3D.Get())
	{
		ExistingMap3D->HandleMapDataCleared(this);
	}
	ClearTileDataObjects();

	OnMapDataCleared.Broadcast(this);
	Map3D.Reset();
	MapDataAsset = nullptr;
	MapGuid.Invalidate();
	OnMapDataCleared.Clear();
}

void UGSMMapData::ClearTileDataObjects()
{
	const TArray<UGSMTileData*> ExistingTiles = GetTiles();
	for (UGSMTileData* TileData : ExistingTiles)
	{
		if (IsValid(TileData))
		{
			TileData->InvalidateTileData();
		}
	}
	TilesById.Reset();
	TilesByCoordinate.Reset();
}

FGuid UGSMMapData::AddPieceToTileById(
	FName TargetTileId,
	FGuid RequestedPieceGuid,
	TSubclassOf<UGSMPieceData> PieceDataClass,
	TSubclassOf<AGSMPiece3D> Piece3DClass,
	const FGSMPiecePlacement& Placement,
	UGSMPieceData*& OutPieceData)
{
	return AddPieceToTile(GetTileById(TargetTileId), RequestedPieceGuid, PieceDataClass, Piece3DClass, Placement, OutPieceData);
}

FGuid UGSMMapData::AddPieceToTile(
	UGSMTileData* TargetTile,
	FGuid RequestedPieceGuid,
	TSubclassOf<UGSMPieceData> PieceDataClass,
	TSubclassOf<AGSMPiece3D> Piece3DClass,
	const FGSMPiecePlacement& Placement,
	UGSMPieceData*& OutPieceData)
{
	OutPieceData = nullptr;
	if (!IsValid(TargetTile) || TargetTile->GetMapData() != this)
	{
		return FGuid();
	}

	const FGuid PieceGuid = RequestedPieceGuid.IsValid() ? RequestedPieceGuid : FGuid::NewGuid();
	if (PiecesByGuid.Contains(PieceGuid))
	{
		UE_LOG(LogGSMMapData, Warning, TEXT("Add piece failed: GUID %s already exists on map %s."),
			*PieceGuid.ToString(), *MapGuid.ToString());
		return FGuid();
	}

	UClass* ResolvedDataClass = PieceDataClass
		? PieceDataClass.Get()
		: (MapDataAsset->DefaultPieceDataClass ? MapDataAsset->DefaultPieceDataClass.Get() : UGSMPieceData::StaticClass());
	if (!Piece3DClass && MapDataAsset->DefaultPiece3DClass)
	{
		Piece3DClass = MapDataAsset->DefaultPiece3DClass;
	}
	UGSMPieceData* PieceData = NewObject<UGSMPieceData>(this, ResolvedDataClass);
	if (!PieceData)
	{
		return FGuid();
	}

	PieceData->InitializePiece(PieceGuid, MapGuid, TargetTile, Piece3DClass, Placement);
	PiecesByGuid.Add(PieceGuid, PieceData);
	if (!TargetTile->AttachPiece(PieceData))
	{
		PiecesByGuid.Remove(PieceGuid);
		return FGuid();
	}

	OutPieceData = PieceData;
	return PieceGuid;
}

bool UGSMMapData::RemovePieceByGuid(const FGuid& PieceGuid)
{
	UGSMPieceData* PieceData = GetPieceByGuid(PieceGuid);
	if (!IsValid(PieceData))
	{
		return false;
	}

	if (UGSMTileData* TileData = PieceData->GetTileData())
	{
		TileData->DetachPiece(PieceData);
	}
	const bool bRemoved = PiecesByGuid.Remove(PieceGuid) > 0;
	if (bRemoved)
	{
		PieceData->InvalidatePieceData();
	}
	return bRemoved;
}

bool UGSMMapData::UpdatePiece(
	const FGuid& PieceGuid,
	TSubclassOf<AGSMPiece3D> Piece3DClass,
	const FGSMPiecePlacement& Placement,
	UGSMPieceData*& OutPieceData)
{
	OutPieceData = GetPieceByGuid(PieceGuid);
	if (!IsValid(OutPieceData))
	{
		return false;
	}

	OutPieceData->SetPiece3DClass(Piece3DClass);
	OutPieceData->SetPlacement(Placement);
	return true;
}

bool UGSMMapData::MovePieceByGuidToTileId(
	const FGuid& PieceGuid,
	FName TargetTileId,
	const FGSMPiecePlacement& Placement,
	UGSMPieceData*& OutPieceData)
{
	return MovePieceByGuidToTile(PieceGuid, GetTileById(TargetTileId), Placement, OutPieceData);
}

bool UGSMMapData::MovePieceByGuidToTile(
	const FGuid& PieceGuid,
	UGSMTileData* TargetTile,
	const FGSMPiecePlacement& Placement,
	UGSMPieceData*& OutPieceData)
{
	OutPieceData = GetPieceByGuid(PieceGuid);
	return MovePieceDataToTile(OutPieceData, TargetTile, Placement);
}

bool UGSMMapData::MovePieceDataToTileId(
	UGSMPieceData* PieceData,
	FName TargetTileId,
	const FGSMPiecePlacement& Placement)
{
	return MovePieceDataToTile(PieceData, GetTileById(TargetTileId), Placement);
}

bool UGSMMapData::MovePieceDataToTile(
	UGSMPieceData* PieceData,
	UGSMTileData* TargetTile,
	const FGSMPiecePlacement& Placement)
{
	if (!IsValid(PieceData) || !IsValid(TargetTile) || TargetTile->GetMapData() != this
		|| GetPieceByGuid(PieceData->GetPieceGuid()) != PieceData)
	{
		return false;
	}

	UGSMTileData* SourceTile = PieceData->GetTileData();
	if (SourceTile == TargetTile)
	{
		PieceData->SetPlacement(Placement);
		return true;
	}

	if (SourceTile)
	{
		SourceTile->DetachPiece(PieceData);
	}
	PieceData->SetPlacement(Placement);
	if (!TargetTile->AttachPiece(PieceData))
	{
		if (SourceTile)
		{
			SourceTile->AttachPiece(PieceData);
		}
		return false;
	}

	PieceData->NotifyMovedBetweenTiles(SourceTile, TargetTile);

	return true;
}

bool UGSMMapData::FindPathByTileIds(
	FName StartTileId,
	FName GoalTileId,
	EGSMNavigationMode NavigationMode,
	FGSMPathResult& OutPath) const
{
	return FindPathByTiles(GetTileById(StartTileId), GetTileById(GoalTileId), NavigationMode, OutPath);
}

bool UGSMMapData::FindPathByTiles(
	UGSMTileData* StartTile,
	UGSMTileData* GoalTile,
	EGSMNavigationMode NavigationMode,
	FGSMPathResult& OutPath) const
{
	OutPath.Reset();
	if (!IsValid(StartTile) || !IsValid(GoalTile) || StartTile->GetMapData() != this || GoalTile->GetMapData() != this)
	{
		OutPath.FailureReason = FText::FromString(TEXT("Start or goal tile data is invalid."));
		return false;
	}

	const auto CanEnter = [](const UGSMTileData* Tile, EGSMNavigationLinkType LinkType)
	{
		if (!IsValid(Tile))
		{
			return false;
		}
		if (LinkType == EGSMNavigationLinkType::Flying || LinkType == EGSMNavigationLinkType::Road)
		{
			return true;
		}
		return Tile->GetNavigationSettings().bCanWalkThrough;
	};

	const auto CanEnterByMode = [&CanEnter](const UGSMTileData* Tile, EGSMNavigationMode Mode)
	{
		switch (Mode)
		{
		case EGSMNavigationMode::Flying: return CanEnter(Tile, EGSMNavigationLinkType::Flying);
		case EGSMNavigationMode::WalkingOnly: return CanEnter(Tile, EGSMNavigationLinkType::Walking);
		case EGSMNavigationMode::RoadOnly: return CanEnter(Tile, EGSMNavigationLinkType::Road);
		default: return CanEnter(Tile, EGSMNavigationLinkType::Walking) || CanEnter(Tile, EGSMNavigationLinkType::Road);
		}
	};

	if (!CanEnterByMode(StartTile, NavigationMode) || !CanEnterByMode(GoalTile, NavigationMode))
	{
		OutPath.FailureReason = FText::FromString(TEXT("Start or goal tile cannot be entered by this navigation mode."));
		return false;
	}
	if (NavigationMode != EGSMNavigationMode::Flying
		&& !GoalTile->GetNavigationSettings().bCanWalkThrough)
	{
		OutPath.FailureReason = FText::FromString(TEXT("Non-flying navigation requires a walkable goal tile."));
		return false;
	}

	const FName StartId = StartTile->GetTileId();
	const FName GoalId = GoalTile->GetTileId();
	struct FEdge { FName Target; float Cost; EGSMNavigationLinkType LinkType; };
	const float MinimumCost = 0.01f;

	const auto GatherEdges = [this, NavigationMode, &CanEnter, MinimumCost](UGSMTileData* Current, TArray<FEdge>& OutEdges)
	{
		const FGSMTileNavigationSettings Settings = Current->GetNavigationSettings();
		if (NavigationMode == EGSMNavigationMode::Flying)
		{
			for (UGSMTileData* Candidate : GetTiles())
			{
				if (Candidate != Current && IsAdjacentCoordinate(Current->GetGridCoordinate(), Candidate->GetGridCoordinate()))
				{
					OutEdges.Add({Candidate->GetTileId(), MinimumCost, EGSMNavigationLinkType::Flying});
				}
			}
			return;
		}

		if (NavigationMode != EGSMNavigationMode::RoadOnly && Settings.bCanWalkThrough)
		{
			if (Settings.WalkingNeighborTileIds.Num() > 0)
			{
				for (FName NeighborId : Settings.WalkingNeighborTileIds)
				{
					if (UGSMTileData* Neighbor = GetTileById(NeighborId); CanEnter(Neighbor, EGSMNavigationLinkType::Walking))
					{
						OutEdges.Add({NeighborId, FMath::Max(Neighbor->GetNavigationSettings().WalkEnterCost, MinimumCost), EGSMNavigationLinkType::Walking});
					}
				}
			}
			else
			{
				for (UGSMTileData* Neighbor : GetTiles())
				{
					if (Neighbor != Current && IsAdjacentCoordinate(Current->GetGridCoordinate(), Neighbor->GetGridCoordinate())
						&& CanEnter(Neighbor, EGSMNavigationLinkType::Walking))
					{
						OutEdges.Add({Neighbor->GetTileId(), FMath::Max(Neighbor->GetNavigationSettings().WalkEnterCost, MinimumCost), EGSMNavigationLinkType::Walking});
					}
				}
			}
		}

		if (NavigationMode != EGSMNavigationMode::WalkingOnly)
		{
			for (const FGSMRoadConnection& Road : Settings.RoadConnections)
			{
				if (UGSMTileData* Target = GetTileById(Road.TargetTileId); CanEnter(Target, EGSMNavigationLinkType::Road))
				{
					const float Cost = Road.CostOverride >= 0.0f ? Road.CostOverride : Target->GetNavigationSettings().RoadEnterCost;
					OutEdges.Add({Road.TargetTileId, FMath::Max(Cost, MinimumCost), EGSMNavigationLinkType::Road});
				}
			}
			for (UGSMTileData* Other : GetTiles())
			{
				for (const FGSMRoadConnection& Road : Other->GetNavigationSettings().RoadConnections)
				{
					if (Road.bBidirectional && Road.TargetTileId == Current->GetTileId() && CanEnter(Other, EGSMNavigationLinkType::Road))
					{
						const float Cost = Road.CostOverride >= 0.0f ? Road.CostOverride : Other->GetNavigationSettings().RoadEnterCost;
						OutEdges.Add({Other->GetTileId(), FMath::Max(Cost, MinimumCost), EGSMNavigationLinkType::Road});
					}
				}
			}
		}
	};

	TMap<FName, float> BestCosts;
	TMap<FName, FName> CameFrom;
	TMap<FName, EGSMNavigationLinkType> CameVia;
	TSet<FName> OpenSet;
	TSet<FName> ClosedSet;
	BestCosts.Add(StartId, 0.0f);
	OpenSet.Add(StartId);

	while (!OpenSet.IsEmpty())
	{
		FName CurrentId = NAME_None;
		float CurrentCost = TNumericLimits<float>::Max();
		for (FName CandidateId : OpenSet)
		{
			if (const float* CandidateCost = BestCosts.Find(CandidateId); CandidateCost && *CandidateCost < CurrentCost)
			{
				CurrentId = CandidateId;
				CurrentCost = *CandidateCost;
			}
		}
		if (CurrentId.IsNone() || CurrentId == GoalId)
		{
			break;
		}
		OpenSet.Remove(CurrentId);
		ClosedSet.Add(CurrentId);

		TArray<FEdge> Edges;
		GatherEdges(GetTileById(CurrentId), Edges);
		for (const FEdge& Edge : Edges)
		{
			if (ClosedSet.Contains(Edge.Target))
			{
				continue;
			}
			const float NewCost = CurrentCost + Edge.Cost;
			const float ExistingCost = BestCosts.Contains(Edge.Target) ? BestCosts[Edge.Target] : TNumericLimits<float>::Max();
			if (NewCost < ExistingCost)
			{
				BestCosts.Add(Edge.Target, NewCost);
				CameFrom.Add(Edge.Target, CurrentId);
				CameVia.Add(Edge.Target, Edge.LinkType);
				OpenSet.Add(Edge.Target);
			}
		}
	}

	if (!BestCosts.Contains(GoalId))
	{
		OutPath.FailureReason = FText::FromString(TEXT("No path was found."));
		return false;
	}

	TArray<FName> ReverseIds{GoalId};
	TArray<EGSMNavigationLinkType> ReverseLinks;
	FName Cursor = GoalId;
	while (Cursor != StartId)
	{
		const FName* Parent = CameFrom.Find(Cursor);
		const EGSMNavigationLinkType* Link = CameVia.Find(Cursor);
		if (!Parent || !Link)
		{
			OutPath.FailureReason = FText::FromString(TEXT("Path reconstruction failed."));
			return false;
		}
		ReverseLinks.Add(*Link);
		Cursor = *Parent;
		ReverseIds.Add(Cursor);
	}

	for (int32 Index = ReverseIds.Num() - 1; Index >= 0; --Index)
	{
		UGSMTileData* Tile = GetTileById(ReverseIds[Index]);
		OutPath.PathTileIds.Add(ReverseIds[Index]);
		OutPath.PathTiles.Add(Tile);
		OutPath.PathWorldLocations.Add(Tile ? Tile->GetLocalTransform().GetLocation() : FVector::ZeroVector);
	}
	for (int32 Index = ReverseLinks.Num() - 1; Index >= 0; --Index)
	{
		OutPath.PathLinkTypes.Add(ReverseLinks[Index]);
	}
	OutPath.bSuccess = true;
	OutPath.TotalCost = BestCosts[GoalId];
	return true;
}

bool UGSMMapData::RegisterMap3D(AGSMMap3D* InMap3D)
{
	if (!IsValid(InMap3D))
	{
		return false;
	}
	if (Map3D.IsValid() && Map3D.Get() != InMap3D)
	{
		UE_LOG(LogGSMMapData, Warning, TEXT("Map %s already has a 3D view (%s)."),
			*MapGuid.ToString(), *GetNameSafe(Map3D.Get()));
		return false;
	}
	Map3D = InMap3D;
	return true;
}

void UGSMMapData::UnregisterMap3D(AGSMMap3D* InMap3D)
{
	if (Map3D.Get() == InMap3D)
	{
		Map3D.Reset();
	}
}

bool UGSMMapData::IsAdjacentCoordinate(const FIntPoint& A, const FIntPoint& B) const
{
	const FIntPoint Delta = B - A;
	return FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) == 1;
}
