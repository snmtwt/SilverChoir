#include "GridStrategyMapSystemEditor/GSMEditorToolSubsystem.h"

#include "GridStrategyMapSystem/Data/GSMMapDataAsset.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"

namespace GSMEditorTool
{
	static const TArray<FIntPoint>& SquareNeighborOffsets()
	{
		static const TArray<FIntPoint> Offsets = {
			FIntPoint(1, 0),
			FIntPoint(-1, 0),
			FIntPoint(0, 1),
			FIntPoint(0, -1)
		};

		return Offsets;
	}
}

void FGSMValidationResult::Reset()
{
	bIsValid = true;
	Issues.Reset();
	Summary = FText::GetEmpty();
}

void FGSMValidationResult::AddIssue(
	EGSMValidationSeverity Severity,
	FName TileId,
	const FText& Message
)
{
	FGSMValidationIssue Issue;
	Issue.Severity = Severity;
	Issue.TileId = TileId;
	Issue.Message = Message;
	Issues.Add(Issue);

	if (Severity == EGSMValidationSeverity::Error)
	{
		bIsValid = false;
	}
}

bool UGSMEditorToolSubsystem::GenerateRectangularSquareMap(
	UGSMMapDataAsset* MapConfig,
	const FGSMRectSquareGenerateSettings& Settings
)
{
	if (!MapConfig || Settings.Columns <= 0 || Settings.Rows <= 0)
	{
		return false;
	}

	MarkConfigModified(MapConfig);
	MapConfig->EditorGridColumns = Settings.Columns;
	MapConfig->EditorGridRows = Settings.Rows;

	if (Settings.bClearExistingTiles)
	{
		MapConfig->TileEntries.Reset();
	}

	for (int32 Row = 0; Row < Settings.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Settings.Columns; ++Column)
		{
			const FIntPoint Coordinate(Column, Row);

			FGSMTileEntry TileEntry;
			TileEntry.TileId = FName(*GSMLayout::MakeTileCoordinateLabel(
				Coordinate,
				MapConfig->HorizontalLabelType,
				MapConfig->VerticalLabelType));
			TileEntry.RegionId = Settings.RegionId;
			TileEntry.DisplayName = FText::FromName(TileEntry.TileId);
			TileEntry.GridCoordinate = Coordinate;
			TileEntry.LocalTransform = FTransform(SquareGridToWorld(
				Coordinate,
				Settings.Columns,
				Settings.Rows,
				Settings.Origin
			));
			TileEntry.TileActorClass = Settings.TileActorClass;
			MapConfig->TileEntries.Add(TileEntry);
		}
	}

	if (Settings.bAutoFillWalkingNeighbors)
	{
		AutoFillWalkingNeighbors(MapConfig);
	}

	MapConfig->MarkPackageDirty();
	return true;
}

bool UGSMEditorToolSubsystem::AutoFillWalkingNeighbors(UGSMMapDataAsset* MapConfig)
{
	if (!MapConfig)
	{
		return false;
	}

	MarkConfigModified(MapConfig);

	TMap<FIntPoint, FName> TileIdByCoordinate;
	for (const FGSMTileEntry& TileEntry : MapConfig->TileEntries)
	{
		if (!TileEntry.TileId.IsNone())
		{
			TileIdByCoordinate.Add(TileEntry.GridCoordinate, TileEntry.TileId);
		}
	}

	for (FGSMTileEntry& TileEntry : MapConfig->TileEntries)
	{
		TileEntry.Navigation.WalkingNeighborTileIds.Reset();

		for (const FIntPoint& Offset : GSMEditorTool::SquareNeighborOffsets())
		{
			if (const FName* NeighborTileId = TileIdByCoordinate.Find(TileEntry.GridCoordinate + Offset))
			{
				TileEntry.Navigation.WalkingNeighborTileIds.Add(*NeighborTileId);
			}
		}
	}

	MapConfig->MarkPackageDirty();
	return true;
}

bool UGSMEditorToolSubsystem::AddBidirectionalRoadConnection(
	UGSMMapDataAsset* MapConfig,
	FName TileAId,
	FName TileBId,
	float CostOverride
)
{
	if (!MapConfig || TileAId.IsNone() || TileBId.IsNone() || TileAId == TileBId)
	{
		return false;
	}

	FGSMTileEntry* TileA = MapConfig->TileEntries.FindByPredicate([TileAId](const FGSMTileEntry& TileEntry)
	{
		return TileEntry.TileId == TileAId;
	});

	FGSMTileEntry* TileB = MapConfig->TileEntries.FindByPredicate([TileBId](const FGSMTileEntry& TileEntry)
	{
		return TileEntry.TileId == TileBId;
	});

	if (!TileA || !TileB)
	{
		return false;
	}

	MarkConfigModified(MapConfig);

	const auto AddConnection = [CostOverride](FGSMTileEntry& FromTile, FName ToTileId)
	{
		FGSMRoadConnection* ExistingConnection = FromTile.Navigation.RoadConnections.FindByPredicate([ToTileId](const FGSMRoadConnection& Connection)
		{
			return Connection.TargetTileId == ToTileId;
		});

		if (ExistingConnection)
		{
			ExistingConnection->CostOverride = CostOverride;
			ExistingConnection->bBidirectional = true;
			return;
		}

		FGSMRoadConnection NewConnection;
		NewConnection.TargetTileId = ToTileId;
		NewConnection.CostOverride = CostOverride;
		NewConnection.bBidirectional = true;
		FromTile.Navigation.RoadConnections.Add(NewConnection);
	};

	AddConnection(*TileA, TileBId);
	AddConnection(*TileB, TileAId);

	MapConfig->MarkPackageDirty();
	return true;
}

bool UGSMEditorToolSubsystem::ValidateMapConfig(
	UGSMMapDataAsset* MapConfig,
	FGSMValidationResult& OutResult
) const
{
	OutResult.Reset();

	if (!MapConfig)
	{
		OutResult.AddIssue(EGSMValidationSeverity::Error, NAME_None, FText::FromString(TEXT("地图配置为空。")));
		OutResult.Summary = FText::FromString(TEXT("地图配置无效：配置为空。"));
		return false;
	}

	TSet<FName> RegionIds;
	for (const FGSMRegionDefinition& RegionDefinition : MapConfig->RegionDefinitions)
	{
		if (!RegionDefinition.RegionId.IsNone())
		{
			RegionIds.Add(RegionDefinition.RegionId);
		}
	}

	TSet<FName> TileIds;
	TSet<FIntPoint> GridCoordinates;
	TMap<FName, const FGSMTileEntry*> TileById;

	for (const FGSMTileEntry& TileEntry : MapConfig->TileEntries)
	{
		if (TileEntry.TileId.IsNone())
		{
			OutResult.AddIssue(EGSMValidationSeverity::Error, NAME_None, FText::FromString(TEXT("存在未填写 TileId 的瓦片。")));
			continue;
		}

		if (TileIds.Contains(TileEntry.TileId))
		{
			OutResult.AddIssue(EGSMValidationSeverity::Error, TileEntry.TileId, FText::FromString(TEXT("TileId 重复。")));
		}
		else
		{
			TileIds.Add(TileEntry.TileId);
			TileById.Add(TileEntry.TileId, &TileEntry);
		}

		if (GridCoordinates.Contains(TileEntry.GridCoordinate))
		{
			OutResult.AddIssue(EGSMValidationSeverity::Warning, TileEntry.TileId, FText::FromString(TEXT("格子坐标重复。")));
		}
		else
		{
			GridCoordinates.Add(TileEntry.GridCoordinate);
		}

		if (!TileEntry.RegionId.IsNone() && !RegionIds.Contains(TileEntry.RegionId))
		{
			OutResult.AddIssue(EGSMValidationSeverity::Warning, TileEntry.TileId, FText::FromString(TEXT("瓦片引用了未定义的区域 ID。")));
		}

		if (!MapConfig->ResolveTileActorClass(TileEntry))
		{
			OutResult.AddIssue(EGSMValidationSeverity::Error, TileEntry.TileId, FText::FromString(TEXT("没有可用于生成的瓦片 Actor 类。")));
		}
	}

	for (const FGSMTileEntry& TileEntry : MapConfig->TileEntries)
	{
		for (FName NeighborTileId : TileEntry.Navigation.WalkingNeighborTileIds)
		{
			if (!TileById.Contains(NeighborTileId))
			{
				OutResult.AddIssue(EGSMValidationSeverity::Error, TileEntry.TileId, FText::FromString(TEXT("步行邻接引用了不存在的瓦片。")));
			}
		}

		for (const FGSMRoadConnection& RoadConnection : TileEntry.Navigation.RoadConnections)
		{
			if (!TileById.Contains(RoadConnection.TargetTileId))
			{
				OutResult.AddIssue(EGSMValidationSeverity::Error, TileEntry.TileId, FText::FromString(TEXT("公路连接引用了不存在的瓦片。")));
			}
		}
	}

	OutResult.Summary = FText::Format(
		FText::FromString(TEXT("校验完成：{0} 个问题，配置{1}。")),
		FText::AsNumber(OutResult.Issues.Num()),
		OutResult.bIsValid ? FText::FromString(TEXT("有效")) : FText::FromString(TEXT("无效"))
	);

	return OutResult.bIsValid;
}

FVector UGSMEditorToolSubsystem::SquareGridToWorld(FIntPoint GridCoordinate, int32 ColumnCount, int32 RowCount, const FVector& Origin)
{
	return Origin + GSMLayout::MakeTopLeftGridTileLocalLocation(
		GridCoordinate,
		ColumnCount,
		RowCount,
		GSMLayout::FixedSquareTileSize,
		0.0f
	);
}

void UGSMEditorToolSubsystem::MarkConfigModified(UGSMMapDataAsset* MapConfig)
{
	if (MapConfig)
	{
		MapConfig->Modify();
	}
}
