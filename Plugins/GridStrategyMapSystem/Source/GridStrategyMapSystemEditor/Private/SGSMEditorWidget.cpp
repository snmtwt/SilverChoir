#include "GridStrategyMapSystemEditor/SGSMEditorWidget.h"

#include "AssetRegistry/AssetData.h"
#include "FileHelpers.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/StaticMesh.h"
#include "GridStrategyMapSystem/Data/GSMMapDataAsset.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Display3D/GSMPathArrow3D.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "InputCoreTypes.h"
#include "Misc/DefaultValueHelper.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SGSMEditorWidget"

namespace GSMEditorWidget
{
	static constexpr float PreviewOuterPadding = 24.0f;
	static constexpr float PreviewLabelLeftPadding = 44.0f;
	static constexpr float PreviewLabelTopPadding = 34.0f;
	static constexpr float PreviewCellGap = 4.0f;
	static constexpr float PreviewCellSize = 50.0f;

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

	static TSharedRef<SWidget> MakeSectionHeader(const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"));
	}

	static TSharedRef<SWidget> MakeLabeledRow(const FText& Label, TSharedRef<SWidget> ValueWidget)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 2.0f, 10.0f, 2.0f)
			[
				SNew(SBox)
				.WidthOverride(118.0f)
				[
					SNew(STextBlock)
					.Text(Label)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 2.0f)
			[
				ValueWidget
			];
	}

	static FString TrimmedString(const FString& Source)
	{
		FString Result = Source;
		Result.TrimStartAndEndInline();
		return Result;
	}
}

void SGSMPreviewWidget::Construct(const FArguments& InArgs)
{
	GetEditableTiles = InArgs._GetEditableTiles;
	GetHorizontalLabelType = InArgs._GetHorizontalLabelType;
	GetVerticalLabelType = InArgs._GetVerticalLabelType;
	GetSelectedTileIndex = InArgs._GetSelectedTileIndex;
	GetSelectedTileIndices = InArgs._GetSelectedTileIndices;
	OnTileSelected = InArgs._OnTileSelected;

	ChildSlot
	[
		SNullWidget::NullWidget
	];
}

int32 SGSMPreviewWidget::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled
) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();

	BuildPreviewGeometries(AllottedGeometry);

	TArray<FGSMTileEntry>* Tiles = GetEditableTiles.IsBound() ? GetEditableTiles.Execute() : nullptr;
	if (!Tiles || Tiles->IsEmpty())
	{
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 2,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(192.0f, 20.0f),
				FSlateLayoutTransform(FVector2f(LocalSize.X * 0.5f - 96.0f, LocalSize.Y * 0.5f - 10.0f))
			),
			LOCTEXT("EmptyPreviewHint", "请选择地图配置资产"),
			FAppStyle::GetFontStyle("NormalFont"),
			ESlateDrawEffect::None,
			FLinearColor(0.62f, 0.62f, 0.62f, 1.0f)
		);
		return LayerId + 2;
	}

	const int32 SelectedIndex = GetSelectedTileIndex.IsBound() ? GetSelectedTileIndex.Execute() : INDEX_NONE;
	const TArray<int32> SelectedIndices = GetSelectedTileIndices.IsBound() ? GetSelectedTileIndices.Execute() : TArray<int32>();
	TSet<int32> SelectedIndexSet;
	for (const int32 SelectedTileIndex : SelectedIndices)
	{
		SelectedIndexSet.Add(SelectedTileIndex);
	}

	const FLinearColor TileFillColor(0.14f, 0.14f, 0.14f, 1.0f);
	const FLinearColor BlockedTileFillColor(0.26f, 0.075f, 0.075f, 1.0f);
	const FLinearColor TileBorderColor(0.27f, 0.27f, 0.27f, 1.0f);
	const FLinearColor BlockedTileBorderColor(0.42f, 0.15f, 0.15f, 1.0f);
	const FLinearColor HeaderTextColor(0.78f, 0.78f, 0.78f, 1.0f);
	const FLinearColor TileTextColor(0.70f, 0.70f, 0.70f, 1.0f);
	const FLinearColor SelectedBorderColor(0.08f, 0.36f, 0.92f, 1.0f);
	const FLinearColor HoveredBorderColor(0.90f, 0.58f, 0.12f, 1.0f);
	const FLinearColor RoadLineColor(0.05f, 0.32f, 0.76f, 1.0f);
	const FSlateFontInfo TileFont = FAppStyle::GetFontStyle("NormalFont");

	TMap<int32, TArray<float>> ColumnCenters;
	TMap<int32, TArray<float>> RowCenters;
	TMap<FName, const FGSMTileEntry*> TileEntryById;
	TMap<FName, const FPreviewTileGeometry*> TileGeometryById;

	for (const FGSMTileEntry& TileEntry : *Tiles)
	{
		if (!TileEntry.TileId.IsNone())
		{
			TileEntryById.Add(TileEntry.TileId, &TileEntry);
		}
	}

	for (const FPreviewTileGeometry& TileGeometry : CachedTileGeometries)
	{
		if (!Tiles->IsValidIndex(TileGeometry.TileIndex))
		{
			continue;
		}

		const FGSMTileEntry& TileEntry = (*Tiles)[TileGeometry.TileIndex];
		ColumnCenters.FindOrAdd(TileEntry.GridCoordinate.X).Add(TileGeometry.Center.X);
		RowCenters.FindOrAdd(TileEntry.GridCoordinate.Y).Add(TileGeometry.Center.Y);
		if (!TileEntry.TileId.IsNone())
		{
			TileGeometryById.Add(TileEntry.TileId, &TileGeometry);
		}

		if (TileGeometry.Points.Num() >= 4)
		{
			const FVector2D TileTopLeft = TileGeometry.Points[0];
			const FVector2D TileSize = TileGeometry.Points[2] - TileGeometry.Points[0];
			const FLinearColor CurrentTileFillColor = TileEntry.Navigation.bCanWalkThrough
				? TileFillColor
				: BlockedTileFillColor;
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				LayerId + 1,
				AllottedGeometry.ToPaintGeometry(
					FVector2f(TileSize.X, TileSize.Y),
					FSlateLayoutTransform(FVector2f(TileTopLeft.X, TileTopLeft.Y))
				),
				FAppStyle::GetBrush("WhiteBrush"),
				ESlateDrawEffect::None,
				CurrentTileFillColor
			);
		}

		TArray<FVector2D> LinePoints = TileGeometry.Points;
		if (!LinePoints.IsEmpty())
		{
			const FVector2D FirstPoint = LinePoints[0];
			LinePoints.Add(FirstPoint);
		}

		const bool bIsSelected = SelectedIndexSet.IsEmpty()
			? TileGeometry.TileIndex == SelectedIndex
			: SelectedIndexSet.Contains(TileGeometry.TileIndex);
		const FLinearColor CurrentBorderColor = TileEntry.Navigation.bCanWalkThrough
			? TileBorderColor
			: BlockedTileBorderColor;
		const bool bIsHovered = TileGeometry.TileIndex == HoveredTileIndex;
		const FLinearColor DisplayBorderColor = bIsSelected
			? SelectedBorderColor
			: (bIsHovered ? HoveredBorderColor : CurrentBorderColor);

		FSlateDrawElement::MakeLines(
			OutDrawElements,
			LayerId + 2,
			AllottedGeometry.ToPaintGeometry(),
			LinePoints,
			ESlateDrawEffect::None,
			DisplayBorderColor,
			true,
			bIsSelected ? 2.5f : (bIsHovered ? 2.0f : 1.0f)
		);

		const EGSMCoordinateLabelType HorizontalLabelType = GetHorizontalLabelType.IsBound()
			? GetHorizontalLabelType.Execute()
			: EGSMCoordinateLabelType::Letters;
		const EGSMCoordinateLabelType VerticalLabelType = GetVerticalLabelType.IsBound()
			? GetVerticalLabelType.Execute()
			: EGSMCoordinateLabelType::Numbers;
		const FString TileLabel = GSMLayout::MakeTileCoordinateLabel(
			TileEntry.GridCoordinate,
			HorizontalLabelType,
			VerticalLabelType);
		const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(TileLabel, TileFont);
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 3,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(TextSize.X, TextSize.Y),
				FSlateLayoutTransform(FVector2f(TileGeometry.Center.X - TextSize.X * 0.5f, TileGeometry.Center.Y - TextSize.Y * 0.5f))
			),
			FText::FromString(TileLabel),
			TileFont,
			ESlateDrawEffect::None,
			TileTextColor
		);
	}

	for (const FGSMTileEntry& TileEntry : *Tiles)
	{
		const FPreviewTileGeometry* TileGeometry = TileGeometryById.FindRef(TileEntry.TileId);
		if (!TileGeometry || TileGeometry->Points.Num() < 4)
		{
			continue;
		}

		for (const FGSMRoadConnection& RoadConnection : TileEntry.Navigation.RoadConnections)
		{
			const FGSMTileEntry* TargetTileEntry = TileEntryById.FindRef(RoadConnection.TargetTileId);
			if (!TargetTileEntry)
			{
				continue;
			}

			const FIntPoint Delta = TargetTileEntry->GridCoordinate - TileEntry.GridCoordinate;
			if (FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) != 1)
			{
				continue;
			}

			const FVector2D TopLeft = TileGeometry->Points[0];
			const FVector2D BottomRight = TileGeometry->Points[2];
			const FVector2D Center = TileGeometry->Center;
			const float EdgeInset = 2.0f;
			TArray<FVector2D> RoadLinePoints;
			RoadLinePoints.SetNum(2);

			if (Delta.X > 0)
			{
				RoadLinePoints[0] = Center;
				RoadLinePoints[1] = FVector2D(BottomRight.X - EdgeInset, Center.Y);
			}
			else if (Delta.X < 0)
			{
				RoadLinePoints[0] = Center;
				RoadLinePoints[1] = FVector2D(TopLeft.X + EdgeInset, Center.Y);
			}
			else if (Delta.Y > 0)
			{
				RoadLinePoints[0] = Center;
				RoadLinePoints[1] = FVector2D(Center.X, BottomRight.Y - EdgeInset);
			}
			else
			{
				RoadLinePoints[0] = Center;
				RoadLinePoints[1] = FVector2D(Center.X, TopLeft.Y + EdgeInset);
			}

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				LayerId + 4,
				AllottedGeometry.ToPaintGeometry(),
				RoadLinePoints,
				ESlateDrawEffect::None,
				RoadLineColor,
				true,
				8.0f
			);
		}
	}

	TArray<int32> SortedColumns;
	ColumnCenters.GetKeys(SortedColumns);
	SortedColumns.Sort();

	for (int32 ColumnIndex = 0; ColumnIndex < SortedColumns.Num(); ++ColumnIndex)
	{
		const TArray<float>& Centers = ColumnCenters[SortedColumns[ColumnIndex]];
		float AverageX = 0.0f;
		for (const float CenterX : Centers)
		{
			AverageX += CenterX;
		}
		AverageX /= static_cast<float>(Centers.Num());

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 4,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(28.0f, 18.0f),
				FSlateLayoutTransform(FVector2f(AverageX - 14.0f, 13.0f))
			),
			FText::FromString(GSMLayout::MakeCoordinateLabel(
				FMath::Max(0, SortedColumns[ColumnIndex]),
				GetHorizontalLabelType.IsBound() ? GetHorizontalLabelType.Execute() : EGSMCoordinateLabelType::Letters)),
			FAppStyle::GetFontStyle("NormalFont"),
			ESlateDrawEffect::None,
			HeaderTextColor
		);
	}

	TArray<int32> SortedRows;
	RowCenters.GetKeys(SortedRows);
	SortedRows.Sort();

	for (int32 RowIndex = 0; RowIndex < SortedRows.Num(); ++RowIndex)
	{
		const TArray<float>& Centers = RowCenters[SortedRows[RowIndex]];
		float AverageY = 0.0f;
		for (const float CenterY : Centers)
		{
			AverageY += CenterY;
		}
		AverageY /= static_cast<float>(Centers.Num());

		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 4,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(28.0f, 18.0f),
				FSlateLayoutTransform(FVector2f(22.0f, AverageY - 9.0f))
			),
			FText::FromString(GSMLayout::MakeCoordinateLabel(
				FMath::Max(0, SortedRows[RowIndex]),
				GetVerticalLabelType.IsBound() ? GetVerticalLabelType.Execute() : EGSMCoordinateLabelType::Numbers)),
			FAppStyle::GetFontStyle("NormalFont"),
			ESlateDrawEffect::None,
			HeaderTextColor
		);
	}

	return LayerId + 4;
}

FVector2D SGSMPreviewWidget::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	(void)LayoutScaleMultiplier;

	TArray<FGSMTileEntry>* Tiles = GetEditableTiles.IsBound() ? GetEditableTiles.Execute() : nullptr;
	if (!Tiles || Tiles->IsEmpty())
	{
		return FVector2D(520.0f, 360.0f);
	}

	int32 MinColumn = TNumericLimits<int32>::Max();
	int32 MaxColumn = TNumericLimits<int32>::Min();
	int32 MinRow = TNumericLimits<int32>::Max();
	int32 MaxRow = TNumericLimits<int32>::Min();

	for (const FGSMTileEntry& TileEntry : *Tiles)
	{
		MinColumn = FMath::Min(MinColumn, TileEntry.GridCoordinate.X);
		MaxColumn = FMath::Max(MaxColumn, TileEntry.GridCoordinate.X);
		MinRow = FMath::Min(MinRow, TileEntry.GridCoordinate.Y);
		MaxRow = FMath::Max(MaxRow, TileEntry.GridCoordinate.Y);
	}

	const int32 ColumnCount = FMath::Max(1, MaxColumn - MinColumn + 1);
	const int32 RowCount = FMath::Max(1, MaxRow - MinRow + 1);
	return FVector2D(
		GSMEditorWidget::PreviewLabelLeftPadding
			+ GSMEditorWidget::PreviewOuterPadding
			+ static_cast<float>(ColumnCount) * GSMEditorWidget::PreviewCellSize
			+ static_cast<float>(ColumnCount - 1) * GSMEditorWidget::PreviewCellGap,
		GSMEditorWidget::PreviewLabelTopPadding
			+ GSMEditorWidget::PreviewOuterPadding
			+ static_cast<float>(RowCount) * GSMEditorWidget::PreviewCellSize
			+ static_cast<float>(RowCount - 1) * GSMEditorWidget::PreviewCellGap
	);
}

FReply SGSMPreviewWidget::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	BuildPreviewGeometries(MyGeometry);

	const FVector2D LocalMousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const int32 TileIndex = FindTileIndexAtLocalPosition(LocalMousePosition);
	if (TileIndex != INDEX_NONE)
	{
		if (OnTileSelected.IsBound())
		{
			OnTileSelected.Execute(TileIndex, MouseEvent.IsControlDown(), MouseEvent.IsShiftDown());
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SGSMPreviewWidget::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	BuildPreviewGeometries(MyGeometry);
	const int32 NewHoveredTileIndex = FindTileIndexAtLocalPosition(
		MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	if (HoveredTileIndex != NewHoveredTileIndex)
	{
		HoveredTileIndex = NewHoveredTileIndex;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	return FReply::Unhandled();
}

void SGSMPreviewWidget::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	if (HoveredTileIndex != INDEX_NONE)
	{
		HoveredTileIndex = INDEX_NONE;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SGSMPreviewWidget::BuildPreviewGeometries(const FGeometry& AllottedGeometry) const
{
	CachedTileGeometries.Reset();
	CachedGeometrySize = AllottedGeometry.GetLocalSize();

	TArray<FGSMTileEntry>* Tiles = GetEditableTiles.IsBound() ? GetEditableTiles.Execute() : nullptr;
	if (!Tiles || Tiles->IsEmpty())
	{
		return;
	}

	int32 MinColumn = TNumericLimits<int32>::Max();
	int32 MaxColumn = TNumericLimits<int32>::Min();
	int32 MinRow = TNumericLimits<int32>::Max();
	int32 MaxRow = TNumericLimits<int32>::Min();

	for (const FGSMTileEntry& TileEntry : *Tiles)
	{
		MinColumn = FMath::Min(MinColumn, TileEntry.GridCoordinate.X);
		MaxColumn = FMath::Max(MaxColumn, TileEntry.GridCoordinate.X);
		MinRow = FMath::Min(MinRow, TileEntry.GridCoordinate.Y);
		MaxRow = FMath::Max(MaxRow, TileEntry.GridCoordinate.Y);
	}

	const float CellSize = GSMEditorWidget::PreviewCellSize;
	const float CellPitch = CellSize + GSMEditorWidget::PreviewCellGap;
	const FVector2D GridOrigin(
		GSMEditorWidget::PreviewLabelLeftPadding,
		GSMEditorWidget::PreviewLabelTopPadding
	);

	for (int32 TileIndex = 0; TileIndex < Tiles->Num(); ++TileIndex)
	{
		const FGSMTileEntry& TileEntry = (*Tiles)[TileIndex];
		const int32 Column = TileEntry.GridCoordinate.X - MinColumn;
		const int32 Row = TileEntry.GridCoordinate.Y - MinRow;
		const FVector2D TileTopLeft = GridOrigin + FVector2D(
			static_cast<float>(Column) * CellPitch,
			static_cast<float>(Row) * CellPitch
		);

		FPreviewTileGeometry TileGeometry;
		TileGeometry.TileIndex = TileIndex;
		TileGeometry.Center = TileTopLeft + FVector2D(CellSize * 0.5f, CellSize * 0.5f);
		TileGeometry.Points.Add(TileTopLeft);
		TileGeometry.Points.Add(TileTopLeft + FVector2D(CellSize, 0.0f));
		TileGeometry.Points.Add(TileTopLeft + FVector2D(CellSize, CellSize));
		TileGeometry.Points.Add(TileTopLeft + FVector2D(0.0f, CellSize));
		CachedTileGeometries.Add(MoveTemp(TileGeometry));
	}
}

int32 SGSMPreviewWidget::FindTileIndexAtLocalPosition(const FVector2D& LocalPosition) const
{
	for (int32 GeometryIndex = CachedTileGeometries.Num() - 1; GeometryIndex >= 0; --GeometryIndex)
	{
		const FPreviewTileGeometry& TileGeometry = CachedTileGeometries[GeometryIndex];
		if (IsPointInsidePolygon(LocalPosition, TileGeometry.Points))
		{
			return TileGeometry.TileIndex;
		}
	}

	return INDEX_NONE;
}

bool SGSMPreviewWidget::IsPointInsidePolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
{
	if (Polygon.Num() < 3)
	{
		return false;
	}

	bool bInside = false;
	for (int32 Index = 0, PreviousIndex = Polygon.Num() - 1; Index < Polygon.Num(); PreviousIndex = Index++)
	{
		const FVector2D& Current = Polygon[Index];
		const FVector2D& Previous = Polygon[PreviousIndex];
		const bool bIntersects = ((Current.Y > Point.Y) != (Previous.Y > Point.Y))
			&& (Point.X < (Previous.X - Current.X) * (Point.Y - Current.Y) / (Previous.Y - Current.Y + SMALL_NUMBER) + Current.X);
		if (bIntersects)
		{
			bInside = !bInside;
		}
	}

	return bInside;
}

void SGSMConfigPanelWidget::Construct(const FArguments& InArgs)
{
	OnTilesChanged = InArgs._OnTilesChanged;
	StatusText = LOCTEXT("InitialStatus", "请选择地图配置资产。");

	ChildSlot
	[
		BuildPanel()
	];
}

TArray<FGSMTileEntry>* SGSMConfigPanelWidget::GetEditableTiles()
{
	return &WorkingTileEntries;
}

void SGSMConfigPanelWidget::SelectTileByIndex(int32 TileIndex, bool bControlDown, bool bShiftDown)
{
	if (!WorkingTileEntries.IsValidIndex(TileIndex))
	{
		SelectedTileIndex = INDEX_NONE;
		SelectionAnchorTileIndex = INDEX_NONE;
		SelectedTileIndices.Reset();
	}
	else if (bShiftDown && WorkingTileEntries.IsValidIndex(SelectionAnchorTileIndex))
	{
		const FIntPoint AnchorCoordinate = WorkingTileEntries[SelectionAnchorTileIndex].GridCoordinate;
		const FIntPoint TargetCoordinate = WorkingTileEntries[TileIndex].GridCoordinate;
		const int32 MinColumn = FMath::Min(AnchorCoordinate.X, TargetCoordinate.X);
		const int32 MaxColumn = FMath::Max(AnchorCoordinate.X, TargetCoordinate.X);
		const int32 MinRow = FMath::Min(AnchorCoordinate.Y, TargetCoordinate.Y);
		const int32 MaxRow = FMath::Max(AnchorCoordinate.Y, TargetCoordinate.Y);

		if (!bControlDown)
		{
			SelectedTileIndices.Reset();
		}

		for (int32 CandidateIndex = 0; CandidateIndex < WorkingTileEntries.Num(); ++CandidateIndex)
		{
			const FIntPoint Coordinate = WorkingTileEntries[CandidateIndex].GridCoordinate;
			if (Coordinate.X >= MinColumn && Coordinate.X <= MaxColumn
				&& Coordinate.Y >= MinRow && Coordinate.Y <= MaxRow)
			{
				SelectedTileIndices.AddUnique(CandidateIndex);
			}
		}
		SelectedTileIndex = TileIndex;
	}
	else if (bControlDown)
	{
		SelectedTileIndex = TileIndex;
		SelectionAnchorTileIndex = TileIndex;
		if (SelectedTileIndices.Contains(TileIndex))
		{
			SelectedTileIndices.Remove(TileIndex);
			SelectedTileIndex = SelectedTileIndices.IsEmpty() ? INDEX_NONE : SelectedTileIndices.Last();
			SelectionAnchorTileIndex = SelectedTileIndex;
		}
		else
		{
			SelectedTileIndices.Add(TileIndex);
		}
	}
	else
	{
		SelectedTileIndex = TileIndex;
		SelectionAnchorTileIndex = TileIndex;
		SelectedTileIndices.Reset();
		SelectedTileIndices.Add(TileIndex);
	}

	RefreshTileDetails();
	BroadcastTilesChanged();
}

UGSMMapDataAsset* SGSMConfigPanelWidget::GetEditingAsset() const
{
	return EditingAsset.Get();
}

FGSMTileEntry* SGSMConfigPanelWidget::GetSelectedTile()
{
	return WorkingTileEntries.IsValidIndex(SelectedTileIndex) ? &WorkingTileEntries[SelectedTileIndex] : nullptr;
}

const FGSMTileEntry* SGSMConfigPanelWidget::GetSelectedTile() const
{
	return WorkingTileEntries.IsValidIndex(SelectedTileIndex) ? &WorkingTileEntries[SelectedTileIndex] : nullptr;
}

FString SGSMConfigPanelWidget::GetSelectedAssetPath() const
{
	const UGSMMapDataAsset* Asset = GetEditingAsset();
	return Asset ? Asset->GetPathName() : FString();
}

void SGSMConfigPanelWidget::OnAssetSelected(const FAssetData& AssetData)
{
	LoadAssetIntoWorkingCopy(Cast<UGSMMapDataAsset>(AssetData.GetAsset()));
}

FString SGSMConfigPanelWidget::GetWholeMapTerrainMeshPath() const
{
	UStaticMesh* Mesh = WorkingWholeMapTerrainMesh.Get();
	return Mesh ? Mesh->GetPathName() : FString();
}

void SGSMConfigPanelWidget::OnWholeMapTerrainMeshSelected(const FAssetData& AssetData)
{
	WorkingWholeMapTerrainMesh = Cast<UStaticMesh>(AssetData.GetAsset());
	MarkWorkingCopyChanged();
}

void SGSMConfigPanelWidget::LoadAssetIntoWorkingCopy(UGSMMapDataAsset* InAsset)
{
	EditingAsset = InAsset;
	WorkingMapName.Reset();
	WorkingTileEntries.Reset();
	WorkingRegionDefinitions.Reset();
	WorkingDefaultTileActorClass = nullptr;
	WorkingDefaultTileDataClass = nullptr;
	WorkingPathArrowActorClass = nullptr;
	LastAppliedDefaultTileActorClass = nullptr;
	LastAppliedDefaultTileDataClass = nullptr;
	WorkingWholeMapTerrainMesh.Reset();
	bWorkingFitWholeMapTerrainMeshToTileGridBounds = true;
	bWorkingScaleWholeMapTerrainMeshZWithXY = true;
	WorkingWholeMapTerrainMeshBaseScale = FVector::OneVector;
	WorkingWholeMapTerrainMeshHeightOffset = 5.0f;
	bWorkingAlignWholeMapTerrainMeshBottomToGroovePlane = true;
	WorkingWholeMapTerrainMeshYawDegrees = 180.0f;
	WorkingGridColumns = 10;
	WorkingGridRows = 8;
	WorkingHorizontalLabelType = EGSMCoordinateLabelType::Letters;
	WorkingVerticalLabelType = EGSMCoordinateLabelType::Numbers;
	WorkingDefaultViewCenter = FVector2D::ZeroVector;
	WorkingDefaultViewScale = 1.0f;
	SelectedTileIndex = INDEX_NONE;
	SelectionAnchorTileIndex = INDEX_NONE;
	SelectedTileIndices.Reset();
	bHasUnsavedChanges = false;
	bStatusIsError = false;

	if (!InAsset)
	{
		StatusText = LOCTEXT("NoAssetStatus", "请选择地图配置资产。");
		RefreshTileDetails();
		BroadcastTilesChanged();
		return;
	}

	WorkingMapName = InAsset->MapName.ToString();
	WorkingTileEntries = InAsset->TileEntries;
	WorkingRegionDefinitions = InAsset->RegionDefinitions;
	WorkingDefaultTileActorClass = InAsset->DefaultTileActorClass;
	WorkingDefaultTileDataClass = InAsset->DefaultTileDataClass;
	WorkingPathArrowActorClass = InAsset->PathArrowActorClass;
	LastAppliedDefaultTileActorClass = InAsset->DefaultTileActorClass;
	LastAppliedDefaultTileDataClass = InAsset->DefaultTileDataClass;
	WorkingWholeMapTerrainMesh = InAsset->WholeMapTerrainMesh.Get();
	bWorkingFitWholeMapTerrainMeshToTileGridBounds = InAsset->bFitWholeMapTerrainMeshToTileGridBounds;
	bWorkingScaleWholeMapTerrainMeshZWithXY = InAsset->bScaleWholeMapTerrainMeshZWithXY;
	WorkingWholeMapTerrainMeshBaseScale = InAsset->WholeMapTerrainMeshBaseScale;
	WorkingWholeMapTerrainMeshHeightOffset = InAsset->WholeMapTerrainMeshHeightOffset;
	bWorkingAlignWholeMapTerrainMeshBottomToGroovePlane = InAsset->bAlignWholeMapTerrainMeshBottomToGroovePlane;
	WorkingWholeMapTerrainMeshYawDegrees = InAsset->WholeMapTerrainMeshYawDegrees;
	WorkingGridColumns = FMath::Max(1, InAsset->EditorGridColumns);
	WorkingGridRows = FMath::Max(1, InAsset->EditorGridRows);
	WorkingHorizontalLabelType = InAsset->HorizontalLabelType;
	WorkingVerticalLabelType = InAsset->VerticalLabelType;
	WorkingDefaultViewCenter = InAsset->DefaultViewCenter;
	WorkingDefaultViewScale = FMath::Max(1.0f, InAsset->DefaultViewScale);

	const TArray<FGSMTileEntry> OriginalTileEntries = WorkingTileEntries;
	const int32 OriginalTileCount = OriginalTileEntries.Num();
	RebuildWorkingGridFromDimensions(true);
	bHasUnsavedChanges = OriginalTileCount != WorkingTileEntries.Num();
	if (!bHasUnsavedChanges)
	{
		for (int32 TileIndex = 0; TileIndex < WorkingTileEntries.Num(); ++TileIndex)
		{
			const FGSMTileEntry& OriginalTileEntry = OriginalTileEntries[TileIndex];
			const FGSMTileEntry& WorkingTileEntry = WorkingTileEntries[TileIndex];
			if (OriginalTileEntry.GridCoordinate != WorkingTileEntry.GridCoordinate ||
				OriginalTileEntry.TileId != WorkingTileEntry.TileId ||
				!OriginalTileEntry.LocalTransform.Equals(WorkingTileEntry.LocalTransform, KINDA_SMALL_NUMBER))
			{
				bHasUnsavedChanges = true;
				break;
			}
		}
	}

	SelectedTileIndex = WorkingTileEntries.IsEmpty() ? INDEX_NONE : 0;
	SelectionAnchorTileIndex = SelectedTileIndex;
	SelectedTileIndices.Reset();
	if (SelectedTileIndex != INDEX_NONE)
	{
		SelectedTileIndices.Add(SelectedTileIndex);
	}
	StatusText = bHasUnsavedChanges
		? FText::Format(LOCTEXT("AssetLoadedGeneratedGridStatus", "已载入 {0}，并按网格宽高生成了未保存的瓦片工作副本。"), FText::FromString(InAsset->GetName()))
		: FText::Format(LOCTEXT("AssetLoadedStatus", "已载入 {0}。"), FText::FromString(InAsset->GetName()));

	RefreshTileDetails();
	BroadcastTilesChanged();
}

FText SGSMConfigPanelWidget::GetConfigSummaryText() const
{
	const UGSMMapDataAsset* Asset = GetEditingAsset();
	if (!Asset)
	{
		return LOCTEXT("NoAssetSummary", "未选择配置资产");
	}

	return FText::Format(
		LOCTEXT("ConfigSummaryFormat", "{0} | {1}列 x {2}行 | 瓦片 {3}{4}"),
		FText::FromString(WorkingMapName.IsEmpty() ? Asset->GetName() : WorkingMapName),
		FText::AsNumber(WorkingGridColumns),
		FText::AsNumber(WorkingGridRows),
		FText::AsNumber(WorkingTileEntries.Num()),
		bHasUnsavedChanges ? LOCTEXT("UnsavedSuffix", " | 未保存") : FText::GetEmpty()
	);
}

FText SGSMConfigPanelWidget::GetSelectedTileTitleText() const
{
	if (HasMultiSelection())
	{
		return FText::Format(
			LOCTEXT("MultiSelectionTitle", "已选择 {0} 个瓦片"),
			FText::AsNumber(SelectedTileIndices.Num())
		);
	}

	const FGSMTileEntry* Tile = GetSelectedTile();
	if (!Tile)
	{
		return LOCTEXT("NoSelectedTileTitle", "瓦片配置");
	}

	return FText::Format(
		LOCTEXT("SelectedTileTitleFormat", "瓦片配置：{0}"),
		FText::FromName(Tile->TileId)
	);
}

FText SGSMConfigPanelWidget::GetMultiSelectionText() const
{
	TArray<FString> TileLabels;
	for (const int32 TileIndex : SelectedTileIndices)
	{
		if (WorkingTileEntries.IsValidIndex(TileIndex))
		{
			TileLabels.Add(MakeTileCoordinateLabel(WorkingTileEntries[TileIndex].GridCoordinate));
		}
	}

	return FText::Format(
		LOCTEXT("MultiSelectionText", "已选瓦片：{0}"),
		FText::FromString(FString::Join(TileLabels, TEXT(", ")))
	);
}

FText SGSMConfigPanelWidget::GetStatusText() const
{
	return StatusText;
}

FSlateColor SGSMConfigPanelWidget::GetStatusColor() const
{
	return bStatusIsError
		? FSlateColor(FLinearColor(0.82f, 0.12f, 0.08f, 1.0f))
		: FSlateColor(FLinearColor(0.16f, 0.16f, 0.16f, 1.0f));
}

void SGSMConfigPanelWidget::RefreshTileDetails()
{
	if (TileDetailsBox.IsValid())
	{
		RebuildTileDetails();
	}
}

void SGSMConfigPanelWidget::RebuildTileDetails()
{
	if (!TileDetailsBox.IsValid())
	{
		return;
	}

	TileDetailsBox->ClearChildren();
	TileDetailsBox->AddSlot()
	.AutoHeight()
	[
		BuildTileDetailsPanel()
	];
}

void SGSMConfigPanelWidget::MarkWorkingCopyChanged()
{
	bHasUnsavedChanges = true;
	bStatusIsError = false;
	StatusText = LOCTEXT("UnsavedStatus", "当前配置有未保存修改。");
	BroadcastTilesChanged();
}

void SGSMConfigPanelWidget::BroadcastTilesChanged() const
{
	if (OnTilesChanged.IsBound())
	{
		OnTilesChanged.Execute();
	}
}

bool SGSMConfigPanelWidget::HasMultiSelection() const
{
	return SelectedTileIndices.Num() > 1;
}

ECheckBoxState SGSMConfigPanelWidget::GetSelectedTilesWalkableState() const
{
	bool bFoundWalkable = false;
	bool bFoundBlocked = false;
	for (const int32 TileIndex : SelectedTileIndices)
	{
		if (!WorkingTileEntries.IsValidIndex(TileIndex))
		{
			continue;
		}

		if (WorkingTileEntries[TileIndex].Navigation.bCanWalkThrough)
		{
			bFoundWalkable = true;
		}
		else
		{
			bFoundBlocked = true;
		}
	}

	if (bFoundWalkable && bFoundBlocked)
	{
		return ECheckBoxState::Undetermined;
	}
	return bFoundWalkable ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SGSMConfigPanelWidget::SetSelectedTilesWalkable(bool bWalkable)
{
	TSet<FName> DisabledTileIds;
	bool bChanged = false;
	for (const int32 TileIndex : SelectedTileIndices)
	{
		if (!WorkingTileEntries.IsValidIndex(TileIndex))
		{
			continue;
		}

		FGSMTileEntry& TileEntry = WorkingTileEntries[TileIndex];
		bChanged |= TileEntry.Navigation.bCanWalkThrough != bWalkable;
		TileEntry.Navigation.bCanWalkThrough = bWalkable;
		if (!bWalkable)
		{
			DisabledTileIds.Add(TileEntry.TileId);
			TileEntry.Navigation.WalkingNeighborTileIds.Reset();
		}
	}

	if (!bWalkable && !DisabledTileIds.IsEmpty())
	{
		for (FGSMTileEntry& TileEntry : WorkingTileEntries)
		{
			TileEntry.Navigation.WalkingNeighborTileIds.RemoveAll(
				[&DisabledTileIds](FName TileId) { return DisabledTileIds.Contains(TileId); });
		}
	}

	if (bChanged)
	{
		MarkWorkingCopyChanged();
		bStatusIsError = false;
		StatusText = bWalkable
			? LOCTEXT("MultiSelectionWalkableEnabled", "已将选中的瓦片统一设为可步行通过。")
			: LOCTEXT("MultiSelectionWalkableDisabled", "已将选中的瓦片统一设为不可步行通过，并清理相关步行邻接。");
	}
	RefreshTileDetails();
}

bool SGSMConfigPanelWidget::HasAnySelectedRoadConnection() const
{
	if (SelectedTileIndices.Num() < 2)
	{
		return false;
	}

	for (int32 SelectionIndex = 0; SelectionIndex < SelectedTileIndices.Num() - 1; ++SelectionIndex)
	{
		const int32 FromIndex = SelectedTileIndices[SelectionIndex];
		const int32 ToIndex = SelectedTileIndices[SelectionIndex + 1];
		if (!WorkingTileEntries.IsValidIndex(FromIndex) || !WorkingTileEntries.IsValidIndex(ToIndex))
		{
			continue;
		}

		const FGSMTileEntry& FromTile = WorkingTileEntries[FromIndex];
		const FGSMTileEntry& ToTile = WorkingTileEntries[ToIndex];
		const bool bFromConnectsToTarget = FromTile.Navigation.RoadConnections.ContainsByPredicate([TargetTileId = ToTile.TileId](const FGSMRoadConnection& RoadConnection)
		{
			return RoadConnection.TargetTileId == TargetTileId;
		});
		const bool bToConnectsToFrom = ToTile.Navigation.RoadConnections.ContainsByPredicate([TargetTileId = FromTile.TileId](const FGSMRoadConnection& RoadConnection)
		{
			return RoadConnection.TargetTileId == TargetTileId;
		});

		if (bFromConnectsToTarget || bToConnectsToFrom)
		{
			return true;
		}
	}

	return false;
}

void SGSMConfigPanelWidget::RebuildWorkingGridFromDimensions(bool bPreserveExistingTiles)
{
	WorkingGridColumns = FMath::Max(1, WorkingGridColumns);
	WorkingGridRows = FMath::Max(1, WorkingGridRows);

	TMap<FIntPoint, FGSMTileEntry> ExistingTilesByCoordinate;
	if (bPreserveExistingTiles)
	{
		for (const FGSMTileEntry& TileEntry : WorkingTileEntries)
		{
			ExistingTilesByCoordinate.Add(TileEntry.GridCoordinate, TileEntry);
		}
	}

	const bool bHadPreviousSelection = WorkingTileEntries.IsValidIndex(SelectedTileIndex);
	const FIntPoint PreviousSelectedCoordinate = bHadPreviousSelection
		? WorkingTileEntries[SelectedTileIndex].GridCoordinate
		: FIntPoint::ZeroValue;

	WorkingTileEntries.Reset(WorkingGridColumns * WorkingGridRows);
	int32 NewSelectedIndex = INDEX_NONE;

	for (int32 Row = 0; Row < WorkingGridRows; ++Row)
	{
		for (int32 Column = 0; Column < WorkingGridColumns; ++Column)
		{
			const FIntPoint Coordinate(Column, Row);

			FGSMTileEntry TileEntry;
			if (FGSMTileEntry* ExistingTile = ExistingTilesByCoordinate.Find(Coordinate))
			{
				TileEntry = *ExistingTile;
				TileEntry.GridCoordinate = Coordinate;
			}
			else
			{
				TileEntry = MakeDefaultTileEntry(Coordinate);
			}

			FTransform NormalizedLocalTransform = TileEntry.LocalTransform;
			NormalizedLocalTransform.SetLocation(GSMLayout::MakeTopLeftGridTileLocalLocation(
				Coordinate,
				WorkingGridColumns,
				WorkingGridRows,
				GSMLayout::FixedSquareTileSize,
				NormalizedLocalTransform.GetLocation().Z
			));
			TileEntry.LocalTransform = NormalizedLocalTransform;

			const int32 AddedIndex = WorkingTileEntries.Add(TileEntry);
			if (bHadPreviousSelection && Coordinate == PreviousSelectedCoordinate)
			{
				NewSelectedIndex = AddedIndex;
			}
		}
	}

	SelectedTileIndex = NewSelectedIndex != INDEX_NONE
		? NewSelectedIndex
		: (WorkingTileEntries.IsEmpty() ? INDEX_NONE : 0);
	SelectionAnchorTileIndex = SelectedTileIndex;
	SelectedTileIndices.Reset();
	if (SelectedTileIndex != INDEX_NONE)
	{
		SelectedTileIndices.Add(SelectedTileIndex);
	}

	RefreshTileIdsFromCoordinates();
}

void SGSMConfigPanelWidget::RefreshTileIdsFromCoordinates()
{
	TMap<FName, FName> RemappedTileIds;
	for (FGSMTileEntry& TileEntry : WorkingTileEntries)
	{
		const FName OldTileId = TileEntry.TileId;
		const FName NewTileId(*MakeTileCoordinateLabel(TileEntry.GridCoordinate));
		if (!OldTileId.IsNone() && OldTileId != NewTileId)
		{
			RemappedTileIds.Add(OldTileId, NewTileId);
		}

		if (TileEntry.DisplayName.IsEmpty() || TileEntry.DisplayName.EqualTo(FText::FromName(OldTileId)))
		{
			TileEntry.DisplayName = FText::FromName(NewTileId);
		}
		TileEntry.TileId = NewTileId;
	}

	for (FGSMTileEntry& TileEntry : WorkingTileEntries)
	{
		for (FName& NeighborTileId : TileEntry.Navigation.WalkingNeighborTileIds)
		{
			if (const FName* NewTileId = RemappedTileIds.Find(NeighborTileId))
			{
				NeighborTileId = *NewTileId;
			}
		}
		for (FGSMRoadConnection& RoadConnection : TileEntry.Navigation.RoadConnections)
		{
			if (const FName* NewTileId = RemappedTileIds.Find(RoadConnection.TargetTileId))
			{
				RoadConnection.TargetTileId = *NewTileId;
			}
		}
	}
}

void SGSMConfigPanelWidget::ApplyDefaultTileClassesToGrid()
{
	for (FGSMTileEntry& TileEntry : WorkingTileEntries)
	{
		if (!TileEntry.TileDataClass || TileEntry.TileDataClass == LastAppliedDefaultTileDataClass)
		{
			TileEntry.TileDataClass = WorkingDefaultTileDataClass;
		}
		if (!TileEntry.TileActorClass || TileEntry.TileActorClass == LastAppliedDefaultTileActorClass)
		{
			TileEntry.TileActorClass = WorkingDefaultTileActorClass;
		}
	}

	LastAppliedDefaultTileDataClass = WorkingDefaultTileDataClass;
	LastAppliedDefaultTileActorClass = WorkingDefaultTileActorClass;
}

void SGSMConfigPanelWidget::DeriveGridDimensionsFromTiles()
{
	if (WorkingTileEntries.IsEmpty())
	{
		return;
	}

	int32 MaxColumn = 0;
	int32 MaxRow = 0;
	for (const FGSMTileEntry& TileEntry : WorkingTileEntries)
	{
		MaxColumn = FMath::Max(MaxColumn, TileEntry.GridCoordinate.X);
		MaxRow = FMath::Max(MaxRow, TileEntry.GridCoordinate.Y);
	}

	WorkingGridColumns = FMath::Max(WorkingGridColumns, MaxColumn + 1);
	WorkingGridRows = FMath::Max(WorkingGridRows, MaxRow + 1);
}

FGSMTileEntry SGSMConfigPanelWidget::MakeDefaultTileEntry(FIntPoint GridCoordinate) const
{
	FGSMTileEntry TileEntry;
	const FString CoordinateLabel = MakeTileCoordinateLabel(GridCoordinate);
	TileEntry.TileId = FName(*CoordinateLabel);
	TileEntry.DisplayName = FText::FromString(CoordinateLabel);
	TileEntry.GridCoordinate = GridCoordinate;
	TileEntry.LocalTransform = FTransform(GSMLayout::MakeTopLeftGridTileLocalLocation(
		GridCoordinate,
		WorkingGridColumns,
		WorkingGridRows,
		GSMLayout::FixedSquareTileSize,
		0.0f
	));
	return TileEntry;
}

bool SGSMConfigPanelWidget::CommitWorkingCopyToAsset(bool bSavePackage)
{
	UGSMMapDataAsset* Asset = GetEditingAsset();
	if (!Asset)
	{
		bStatusIsError = true;
		StatusText = LOCTEXT("SaveNoAssetStatus", "保存失败：未选择地图配置资产。");
		return false;
	}

	Asset->Modify();
	Asset->MapName = FText::FromString(WorkingMapName);
	Asset->DefaultTileActorClass = WorkingDefaultTileActorClass;
	Asset->DefaultTileDataClass = WorkingDefaultTileDataClass;
	Asset->PathArrowActorClass = WorkingPathArrowActorClass;
	Asset->WholeMapTerrainMesh = WorkingWholeMapTerrainMesh.Get();
	Asset->bFitWholeMapTerrainMeshToTileGridBounds = bWorkingFitWholeMapTerrainMeshToTileGridBounds;
	Asset->bScaleWholeMapTerrainMeshZWithXY = bWorkingScaleWholeMapTerrainMeshZWithXY;
	Asset->WholeMapTerrainMeshBaseScale = WorkingWholeMapTerrainMeshBaseScale;
	Asset->WholeMapTerrainMeshHeightOffset = WorkingWholeMapTerrainMeshHeightOffset;
	Asset->bAlignWholeMapTerrainMeshBottomToGroovePlane = bWorkingAlignWholeMapTerrainMeshBottomToGroovePlane;
	Asset->WholeMapTerrainMeshYawDegrees = WorkingWholeMapTerrainMeshYawDegrees;
	Asset->EditorGridColumns = WorkingGridColumns;
	Asset->EditorGridRows = WorkingGridRows;
	Asset->HorizontalLabelType = WorkingHorizontalLabelType;
	Asset->VerticalLabelType = WorkingVerticalLabelType;
	Asset->DefaultViewCenter = FVector2D(
		FMath::Max(0.0f, FMath::RoundToFloat(WorkingDefaultViewCenter.X)),
		FMath::Max(0.0f, FMath::RoundToFloat(WorkingDefaultViewCenter.Y)));
	Asset->DefaultViewScale = FMath::Max(1.0f, WorkingDefaultViewScale);
	Asset->RegionDefinitions = WorkingRegionDefinitions;
	Asset->TileEntries = WorkingTileEntries;
	Asset->MarkPackageDirty();

	if (!bSavePackage)
	{
		bHasUnsavedChanges = true;
		bStatusIsError = false;
		return true;
	}

	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(Asset->GetOutermost());
	const FEditorFileUtils::EPromptReturnCode SaveResult = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false);
	const bool bSaved = SaveResult == FEditorFileUtils::PR_Success;

	bHasUnsavedChanges = !bSaved;
	bStatusIsError = !bSaved;
	StatusText = bSaved
		? LOCTEXT("SaveSuccessStatus", "已保存到配置资产。")
		: LOCTEXT("SaveFailedStatus", "资产已标记为脏，但保存到磁盘失败或被取消。");

	BroadcastTilesChanged();
	return bSaved;
}

FReply SGSMConfigPanelWidget::SaveToAsset()
{
	CommitWorkingCopyToAsset(true);
	return FReply::Handled();
}

FReply SGSMConfigPanelWidget::RebuildGrid()
{
	if (!GetEditingAsset())
	{
		bStatusIsError = true;
		StatusText = LOCTEXT("RebuildNoAssetStatus", "重建失败：未选择地图配置资产。");
		return FReply::Handled();
	}

	RebuildWorkingGridFromDimensions(true);
	ApplyDefaultTileClassesToGrid();
	MarkWorkingCopyChanged();
	RefreshTileDetails();
	bStatusIsError = false;
	StatusText = FText::Format(
		LOCTEXT("RebuildGridSuccessStatus", "已按 {0} 列 x {1} 行重建瓦片网格。"),
		FText::AsNumber(WorkingGridColumns),
		FText::AsNumber(WorkingGridRows));
	return FReply::Handled();
}

FReply SGSMConfigPanelWidget::AutoFillWalkingNeighbors()
{
	if (WorkingTileEntries.IsEmpty())
	{
		bStatusIsError = true;
		StatusText = LOCTEXT("AutoFillNoTilesStatus", "没有可填充邻接的瓦片。");
		return FReply::Handled();
	}

	TMap<FIntPoint, const FGSMTileEntry*> TileByCoordinate;
	for (const FGSMTileEntry& TileEntry : WorkingTileEntries)
	{
		if (!TileEntry.TileId.IsNone())
		{
			TileByCoordinate.Add(TileEntry.GridCoordinate, &TileEntry);
		}
	}

	for (FGSMTileEntry& TileEntry : WorkingTileEntries)
	{
		TileEntry.Navigation.WalkingNeighborTileIds.Reset();
		if (!TileEntry.Navigation.bCanWalkThrough)
		{
			continue;
		}

		for (const FIntPoint& Offset : GSMEditorWidget::SquareNeighborOffsets())
		{
			if (const FGSMTileEntry* const* NeighborTile = TileByCoordinate.Find(TileEntry.GridCoordinate + Offset))
			{
				if (*NeighborTile && (*NeighborTile)->Navigation.bCanWalkThrough)
				{
					TileEntry.Navigation.WalkingNeighborTileIds.AddUnique((*NeighborTile)->TileId);
				}
			}
		}
	}

	MarkWorkingCopyChanged();
	StatusText = LOCTEXT("AutoFillSuccessStatus", "已按四边形网格填充步行邻接。");
	return FReply::Handled();
}

FReply SGSMConfigPanelWidget::ConnectSelectedTilesAsRoad()
{
	if (SelectedTileIndices.Num() < 2)
	{
		bStatusIsError = true;
		StatusText = LOCTEXT("ConnectRoadNotEnoughTiles", "至少需要选择两个瓦片才能连接公路。");
		return FReply::Handled();
	}

	int32 ConnectedPairCount = 0;
	int32 SkippedPairCount = 0;

	const auto AddRoadConnection = [](FGSMTileEntry& FromTile, FName ToTileId)
	{
		if (ToTileId.IsNone())
		{
			return;
		}

		if (FGSMRoadConnection* ExistingConnection = FromTile.Navigation.RoadConnections.FindByPredicate([ToTileId](const FGSMRoadConnection& RoadConnection)
		{
			return RoadConnection.TargetTileId == ToTileId;
		}))
		{
			ExistingConnection->CostOverride = -1.0f;
			ExistingConnection->bBidirectional = true;
			return;
		}

		FGSMRoadConnection NewConnection;
		NewConnection.TargetTileId = ToTileId;
		NewConnection.CostOverride = -1.0f;
		NewConnection.bBidirectional = true;
		FromTile.Navigation.RoadConnections.Add(NewConnection);
	};

	for (int32 SelectionIndex = 0; SelectionIndex < SelectedTileIndices.Num() - 1; ++SelectionIndex)
	{
		const int32 FromIndex = SelectedTileIndices[SelectionIndex];
		const int32 ToIndex = SelectedTileIndices[SelectionIndex + 1];
		if (!WorkingTileEntries.IsValidIndex(FromIndex) || !WorkingTileEntries.IsValidIndex(ToIndex))
		{
			++SkippedPairCount;
			continue;
		}

		FGSMTileEntry& FromTile = WorkingTileEntries[FromIndex];
		FGSMTileEntry& ToTile = WorkingTileEntries[ToIndex];
		const FIntPoint Delta = ToTile.GridCoordinate - FromTile.GridCoordinate;
		if (FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) != 1)
		{
			++SkippedPairCount;
			continue;
		}

		AddRoadConnection(FromTile, ToTile.TileId);
		AddRoadConnection(ToTile, FromTile.TileId);
		++ConnectedPairCount;
	}

	if (ConnectedPairCount > 0)
	{
		MarkWorkingCopyChanged();
		bStatusIsError = false;
		StatusText = FText::Format(
			LOCTEXT("ConnectRoadSuccess", "已连接 {0} 段公路，跳过 {1} 段非相邻选择。"),
			FText::AsNumber(ConnectedPairCount),
			FText::AsNumber(SkippedPairCount)
		);
	}
	else
	{
		bStatusIsError = true;
		StatusText = LOCTEXT("ConnectRoadNoAdjacentTiles", "没有可连接的相邻瓦片，请按相邻顺序多选。");
		BroadcastTilesChanged();
	}

	RefreshTileDetails();
	return FReply::Handled();
}

FReply SGSMConfigPanelWidget::DisconnectSelectedTilesAsRoad()
{
	if (SelectedTileIndices.Num() < 2)
	{
		bStatusIsError = true;
		StatusText = LOCTEXT("DisconnectRoadNotEnoughTiles", "至少需要选择两个瓦片才能断开公路。");
		return FReply::Handled();
	}

	int32 DisconnectedPairCount = 0;

	const auto RemoveRoadConnection = [](FGSMTileEntry& FromTile, FName ToTileId)
	{
		const int32 RemovedCount = FromTile.Navigation.RoadConnections.RemoveAll([ToTileId](const FGSMRoadConnection& RoadConnection)
		{
			return RoadConnection.TargetTileId == ToTileId;
		});
		return RemovedCount;
	};

	for (int32 SelectionIndex = 0; SelectionIndex < SelectedTileIndices.Num() - 1; ++SelectionIndex)
	{
		const int32 FromIndex = SelectedTileIndices[SelectionIndex];
		const int32 ToIndex = SelectedTileIndices[SelectionIndex + 1];
		if (!WorkingTileEntries.IsValidIndex(FromIndex) || !WorkingTileEntries.IsValidIndex(ToIndex))
		{
			continue;
		}

		FGSMTileEntry& FromTile = WorkingTileEntries[FromIndex];
		FGSMTileEntry& ToTile = WorkingTileEntries[ToIndex];
		const int32 RemovedForward = RemoveRoadConnection(FromTile, ToTile.TileId);
		const int32 RemovedBackward = RemoveRoadConnection(ToTile, FromTile.TileId);
		if (RemovedForward > 0 || RemovedBackward > 0)
		{
			++DisconnectedPairCount;
		}
	}

	if (DisconnectedPairCount > 0)
	{
		MarkWorkingCopyChanged();
		bStatusIsError = false;
		StatusText = FText::Format(
			LOCTEXT("DisconnectRoadSuccess", "已断开 {0} 段公路链接。"),
			FText::AsNumber(DisconnectedPairCount)
		);
	}
	else
	{
		bStatusIsError = true;
		StatusText = LOCTEXT("DisconnectRoadNoConnection", "当前多选顺序中没有可断开的公路链接。");
		BroadcastTilesChanged();
	}

	RefreshTileDetails();
	return FReply::Handled();
}

FReply SGSMConfigPanelWidget::ValidateConfig()
{
	int32 ErrorCount = 0;
	int32 WarningCount = 0;

	TSet<FName> TileIds;
	TSet<FIntPoint> GridCoordinates;
	TSet<FName> RegionIds;

	for (const FGSMRegionDefinition& RegionDefinition : WorkingRegionDefinitions)
	{
		if (!RegionDefinition.RegionId.IsNone())
		{
			RegionIds.Add(RegionDefinition.RegionId);
		}
	}

	for (const FGSMTileEntry& TileEntry : WorkingTileEntries)
	{
		if (TileEntry.TileId.IsNone())
		{
			++ErrorCount;
		}
		else if (TileIds.Contains(TileEntry.TileId))
		{
			++ErrorCount;
		}
		else
		{
			TileIds.Add(TileEntry.TileId);
		}

		if (GridCoordinates.Contains(TileEntry.GridCoordinate))
		{
			++WarningCount;
		}
		else
		{
			GridCoordinates.Add(TileEntry.GridCoordinate);
		}

		if (!TileEntry.RegionId.IsNone() && !RegionIds.Contains(TileEntry.RegionId))
		{
			++WarningCount;
		}
	}

	for (const FGSMTileEntry& TileEntry : WorkingTileEntries)
	{
		for (FName NeighborTileId : TileEntry.Navigation.WalkingNeighborTileIds)
		{
			if (!TileIds.Contains(NeighborTileId))
			{
				++ErrorCount;
			}
		}

		for (const FGSMRoadConnection& RoadConnection : TileEntry.Navigation.RoadConnections)
		{
			if (!TileIds.Contains(RoadConnection.TargetTileId))
			{
				++ErrorCount;
			}
		}
	}

	bStatusIsError = ErrorCount > 0;
	StatusText = FText::Format(
		LOCTEXT("ValidateStatusFormat", "校验完成：错误 {0}，警告 {1}。"),
		FText::AsNumber(ErrorCount),
		FText::AsNumber(WarningCount)
	);

	return FReply::Handled();
}

TSharedRef<SWidget> SGSMConfigPanelWidget::BuildPanel()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(10.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UGSMMapDataAsset::StaticClass())
					.ObjectPath(this, &SGSMConfigPanelWidget::GetSelectedAssetPath)
					.OnObjectChanged(this, &SGSMConfigPanelWidget::OnAssetSelected)
					.DisplayUseSelected(true)
					.DisplayBrowse(true)
					.AllowClear(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(SExpandableArea)
					.InitiallyCollapsed(false)
					.HeaderContent()
					[
						GSMEditorWidget::MakeSectionHeader(LOCTEXT("MapConfigSectionHeader", "地图配置"))
					]
					.BodyContent()
					[
						BuildMapConfigArea()
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 8.0f)
				[
					SNew(SSeparator)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text(this, &SGSMConfigPanelWidget::GetSelectedTileTitleText)
					.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(TileDetailsBox, SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildTileDetailsPanel()
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 8.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("AutoFillWalkingNeighborsButton", "自动填充步行邻接"))
						.HAlign(HAlign_Center)
						.OnClicked(this, &SGSMConfigPanelWidget::AutoFillWalkingNeighbors)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ValidateConfigButton", "校验配置"))
						.HAlign(HAlign_Center)
						.OnClicked(this, &SGSMConfigPanelWidget::ValidateConfig)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 8.0f)
				[
					SNew(STextBlock)
					.Text(this, &SGSMConfigPanelWidget::GetStatusText)
					.ColorAndOpacity(this, &SGSMConfigPanelWidget::GetStatusColor)
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.Text(LOCTEXT("SaveToAssetButton", "保存到配置资产"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SGSMConfigPanelWidget::SaveToAsset)
				]
			]
		];
}

TSharedRef<SWidget> SGSMConfigPanelWidget::BuildMapConfigArea()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 4.0f)
		[
			MakeStringTextBox(
				LOCTEXT("MapNameLabel", "地图名称"),
				[this]() { return WorkingMapName; },
				[this](const FString& NewValue)
				{
					WorkingMapName = NewValue;
					MarkWorkingCopyChanged();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeGridDimensionControls()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeCoordinateLabelTypePicker(LOCTEXT("HorizontalLabelTypeLabel", "横向标签类型"), true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeCoordinateLabelTypePicker(LOCTEXT("VerticalLabelTypeLabel", "纵向标签类型"), false)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeDefaultTileDataClassPicker()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeDefaultTileClassPicker()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakePathArrowClassPicker()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeTerrainConfigPanel()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeDefaultViewPanel()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("RebuildGridButton", "重建网格"))
			.HAlign(HAlign_Center)
			.OnClicked(this, &SGSMConfigPanelWidget::RebuildGrid)
		];
}
TSharedRef<SWidget> SGSMConfigPanelWidget::BuildTileDetailsPanel()
{
	if (!GetEditingAsset())
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("TileDetailsNoAsset", "选择一个地图配置资产后，可以在这里编辑瓦片参数。"))
			.AutoWrapText(true);
	}

	if (HasMultiSelection())
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(this, &SGSMConfigPanelWidget::GetMultiSelectionText)
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				GSMEditorWidget::MakeLabeledRow(
					LOCTEXT("MultiSelectionCanWalkThroughLabel", "可步行通过"),
					SNew(SCheckBox)
					.IsChecked(this, &SGSMConfigPanelWidget::GetSelectedTilesWalkableState)
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						SetSelectedTilesWalkable(NewState == ECheckBoxState::Checked);
					})
				)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ConnectRoadButton", "连接公路"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SGSMConfigPanelWidget::ConnectSelectedTilesAsRoad)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.Visibility_Lambda([this]()
					{
						return HasAnySelectedRoadConnection() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.Text(LOCTEXT("DisconnectRoadButton", "断开公路链接"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SGSMConfigPanelWidget::DisconnectSelectedTilesAsRoad)
				]
			];
	}

	if (!GetSelectedTile())
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("TileDetailsNoSelection", "点击左侧预览中的瓦片来编辑它的参数。"))
			.AutoWrapText(true);
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			GSMEditorWidget::MakeLabeledRow(
				LOCTEXT("TileIdLabel", "瓦片ID"),
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					return FText::FromName(GetSelectedTile() ? GetSelectedTile()->TileId : NAME_None);
				}))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeStringTextBox(
				LOCTEXT("DisplayNameLabel", "显示名称"),
				[this]() { return GetSelectedTile() ? GetSelectedTile()->DisplayName.ToString() : FString(); },
				[this](const FString& NewValue)
				{
					if (FGSMTileEntry* Tile = GetSelectedTile())
					{
						Tile->DisplayName = FText::FromString(NewValue);
						MarkWorkingCopyChanged();
					}
				}
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeTileClassPickers()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 4.0f)
		[
			GSMEditorWidget::MakeSectionHeader(LOCTEXT("TileNavigationSection", "导航"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeCheckBox(
				LOCTEXT("CanWalkThroughLabel", "可步行通过"),
				[this]() { return GetSelectedTile() ? GetSelectedTile()->Navigation.bCanWalkThrough : false; },
				[this](bool bNewValue)
				{
					if (FGSMTileEntry* Tile = GetSelectedTile())
					{
						Tile->Navigation.bCanWalkThrough = bNewValue;
						if (!bNewValue)
						{
							const FName DisabledTileId = Tile->TileId;
							Tile->Navigation.WalkingNeighborTileIds.Reset();
							for (FGSMTileEntry& OtherTile : WorkingTileEntries)
							{
								OtherTile.Navigation.WalkingNeighborTileIds.Remove(DisabledTileId);
							}
							RefreshTileDetails();
						}
						MarkWorkingCopyChanged();
					}
				}
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeFloatBox(
				LOCTEXT("WalkCostLabel", "步行Cost"),
				[this]() { return GetSelectedTile() ? GetSelectedTile()->Navigation.WalkEnterCost : 1.0f; },
				[this](float NewValue)
				{
					if (FGSMTileEntry* Tile = GetSelectedTile())
					{
						Tile->Navigation.WalkEnterCost = FMath::Max(0.01f, NewValue);
						MarkWorkingCopyChanged();
					}
				}
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeFloatBox(
				LOCTEXT("RoadCostLabel", "公路Cost"),
				[this]() { return GetSelectedTile() ? GetSelectedTile()->Navigation.RoadEnterCost : 0.35f; },
				[this](float NewValue)
				{
					if (FGSMTileEntry* Tile = GetSelectedTile())
					{
						Tile->Navigation.RoadEnterCost = FMath::Max(0.01f, NewValue);
						MarkWorkingCopyChanged();
					}
				}
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeStringTextBox(
				LOCTEXT("WalkingNeighborsLabel", "步行邻接"),
				[this]() { return GetWalkingNeighborsText(); },
				[this](const FString& NewValue) { SetWalkingNeighborsText(NewValue); }
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeStringTextBox(
				LOCTEXT("RoadConnectionsLabel", "公路连接"),
				[this]() { return GetRoadConnectionsText(); },
				[this](const FString& NewValue) { SetRoadConnectionsText(NewValue); }
			)
		];
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeNameTextBox(
	const FText& Label,
	TFunction<FName()> Getter,
	TFunction<void(FName)> Setter
)
{
	return GSMEditorWidget::MakeLabeledRow(
		Label,
		SNew(SEditableTextBox)
		.Text_Lambda([Getter]() { return FText::FromName(Getter()); })
		.OnTextCommitted_Lambda([Setter](const FText& NewText, ETextCommit::Type)
		{
			const FString NewValue = GSMEditorWidget::TrimmedString(NewText.ToString());
			Setter(NewValue.IsEmpty() ? NAME_None : FName(*NewValue));
		})
	);
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeStringTextBox(
	const FText& Label,
	TFunction<FString()> Getter,
	TFunction<void(const FString&)> Setter
)
{
	return GSMEditorWidget::MakeLabeledRow(
		Label,
		SNew(SEditableTextBox)
		.Text_Lambda([Getter]() { return FText::FromString(Getter()); })
		.OnTextCommitted_Lambda([Setter](const FText& NewText, ETextCommit::Type)
		{
			Setter(GSMEditorWidget::TrimmedString(NewText.ToString()));
		})
	);
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeIntBox(
	const FText& Label,
	TFunction<int32()> Getter,
	TFunction<void(int32)> Setter
)
{
	return GSMEditorWidget::MakeLabeledRow(
		Label,
		SNew(SNumericEntryBox<int32>)
		.Value_Lambda([Getter]() { return TOptional<int32>(Getter()); })
		.OnValueCommitted_Lambda([Setter](int32 NewValue, ETextCommit::Type)
		{
			Setter(NewValue);
		})
	);
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeFloatBox(
	const FText& Label,
	TFunction<float()> Getter,
	TFunction<void(float)> Setter
)
{
	return GSMEditorWidget::MakeLabeledRow(
		Label,
		SNew(SNumericEntryBox<float>)
		.Value_Lambda([Getter]() { return TOptional<float>(Getter()); })
		.OnValueCommitted_Lambda([Setter](float NewValue, ETextCommit::Type)
		{
			Setter(NewValue);
		})
	);
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeCheckBox(
	const FText& Label,
	TFunction<bool()> Getter,
	TFunction<void(bool)> Setter
)
{
	return GSMEditorWidget::MakeLabeledRow(
		Label,
		SNew(SCheckBox)
		.IsChecked_Lambda([Getter]() { return Getter() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([Setter](ECheckBoxState NewState)
		{
			Setter(NewState == ECheckBoxState::Checked);
		})
	);
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeGridDimensionControls()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			GSMEditorWidget::MakeLabeledRow(
				LOCTEXT("GridCellsPerRowLabel", "单行网格数量"),
				SNew(SNumericEntryBox<int32>)
				.Value_Lambda([this]() { return TOptional<int32>(WorkingGridColumns); })
				.MinValue(1)
				.MinSliderValue(1)
				.OnValueCommitted_Lambda([this](int32 NewValue, ETextCommit::Type)
				{
					const int32 ClampedValue = FMath::Max(1, NewValue);
					if (WorkingGridColumns != ClampedValue)
					{
						WorkingGridColumns = ClampedValue;
						MarkWorkingCopyChanged();
					}
				})
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			GSMEditorWidget::MakeLabeledRow(
				LOCTEXT("GridCellsPerColumnLabel", "单列网格数量"),
				SNew(SNumericEntryBox<int32>)
				.Value_Lambda([this]() { return TOptional<int32>(WorkingGridRows); })
				.MinValue(1)
				.MinSliderValue(1)
				.OnValueCommitted_Lambda([this](int32 NewValue, ETextCommit::Type)
				{
					const int32 ClampedValue = FMath::Max(1, NewValue);
					if (WorkingGridRows != ClampedValue)
					{
						WorkingGridRows = ClampedValue;
						MarkWorkingCopyChanged();
					}
				})
			)
		];
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeCoordinateLabelTypePicker(const FText& Label, bool bHorizontal)
{
	const auto GetCurrentType = [this, bHorizontal]()
	{
		return bHorizontal ? WorkingHorizontalLabelType : WorkingVerticalLabelType;
	};
	const auto SetType = [this, bHorizontal](EGSMCoordinateLabelType NewType)
	{
		EGSMCoordinateLabelType& TargetType = bHorizontal ? WorkingHorizontalLabelType : WorkingVerticalLabelType;
		if (TargetType == NewType)
		{
			return;
		}
		TargetType = NewType;
		RefreshTileIdsFromCoordinates();
		MarkWorkingCopyChanged();
		RefreshTileDetails();
	};

	return GSMEditorWidget::MakeLabeledRow(
		Label,
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "RadioButton")
			.IsChecked_Lambda([GetCurrentType]()
			{
				return GetCurrentType() == EGSMCoordinateLabelType::Letters
					? ECheckBoxState::Checked
					: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([SetType](ECheckBoxState State)
			{
				if (State == ECheckBoxState::Checked)
				{
					SetType(EGSMCoordinateLabelType::Letters);
				}
			})
			[
				SNew(STextBlock).Text(LOCTEXT("CoordinateLabelLetters", "字母"))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "RadioButton")
			.IsChecked_Lambda([GetCurrentType]()
			{
				return GetCurrentType() == EGSMCoordinateLabelType::Numbers
					? ECheckBoxState::Checked
					: ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([SetType](ECheckBoxState State)
			{
				if (State == ECheckBoxState::Checked)
				{
					SetType(EGSMCoordinateLabelType::Numbers);
				}
			})
			[
				SNew(STextBlock).Text(LOCTEXT("CoordinateLabelNumbers", "数字"))
			]
		]
	);
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeTerrainConfigPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			GSMEditorWidget::MakeLabeledRow(
				LOCTEXT("WholeMapTerrainMeshLabel", "地图网格体"),
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UStaticMesh::StaticClass())
				.ObjectPath(this, &SGSMConfigPanelWidget::GetWholeMapTerrainMeshPath)
				.OnObjectChanged(this, &SGSMConfigPanelWidget::OnWholeMapTerrainMeshSelected)
				.DisplayUseSelected(true)
				.DisplayBrowse(true)
				.AllowClear(true)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeCheckBox(
				LOCTEXT("FitWholeMapTerrainLabel", "地形匹配网格范围"),
				[this]() { return bWorkingFitWholeMapTerrainMeshToTileGridBounds; },
				[this](bool Value)
				{
					bWorkingFitWholeMapTerrainMeshToTileGridBounds = Value;
					MarkWorkingCopyChanged();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeCheckBox(
				LOCTEXT("ScaleWholeMapTerrainZLabel", "地形高度随平面缩放"),
				[this]() { return bWorkingScaleWholeMapTerrainMeshZWithXY; },
				[this](bool Value)
				{
					bWorkingScaleWholeMapTerrainMeshZWithXY = Value;
					MarkWorkingCopyChanged();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeFloatBox(
				LOCTEXT("WholeMapTerrainHeightOffsetLabel", "地形高度偏移"),
				[this]() { return WorkingWholeMapTerrainMeshHeightOffset; },
				[this](float Value)
				{
					WorkingWholeMapTerrainMeshHeightOffset = Value;
					MarkWorkingCopyChanged();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeCheckBox(
				LOCTEXT("AlignWholeMapTerrainBottomLabel", "地形底部对齐凹槽"),
				[this]() { return bWorkingAlignWholeMapTerrainMeshBottomToGroovePlane; },
				[this](bool Value)
				{
					bWorkingAlignWholeMapTerrainMeshBottomToGroovePlane = Value;
					MarkWorkingCopyChanged();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeFloatBox(
				LOCTEXT("WholeMapTerrainYawDegreesLabel", "地形水平角度"),
				[this]() { return WorkingWholeMapTerrainMeshYawDegrees; },
				[this](float Value)
				{
					WorkingWholeMapTerrainMeshYawDegrees = Value;
					MarkWorkingCopyChanged();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeFloatBox(
				LOCTEXT("WholeMapTerrainBaseScaleXLabel", "模型缩放 X"),
				[this]() { return static_cast<float>(WorkingWholeMapTerrainMeshBaseScale.X); },
				[this](float Value)
				{
					WorkingWholeMapTerrainMeshBaseScale.X = FMath::Max(0.0001f, Value);
					MarkWorkingCopyChanged();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeFloatBox(
				LOCTEXT("WholeMapTerrainBaseScaleYLabel", "模型缩放 Y"),
				[this]() { return static_cast<float>(WorkingWholeMapTerrainMeshBaseScale.Y); },
				[this](float Value)
				{
					WorkingWholeMapTerrainMeshBaseScale.Y = FMath::Max(0.0001f, Value);
					MarkWorkingCopyChanged();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeFloatBox(
				LOCTEXT("WholeMapTerrainBaseScaleZLabel", "模型缩放 Z"),
				[this]() { return static_cast<float>(WorkingWholeMapTerrainMeshBaseScale.Z); },
				[this](float Value)
				{
					WorkingWholeMapTerrainMeshBaseScale.Z = FMath::Max(0.0001f, Value);
					MarkWorkingCopyChanged();
				})
		];
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeDefaultViewPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeFloatBox(
				LOCTEXT("DefaultViewCenterXLabel", "默认中心格 X（从1开始）"),
				[this]() { return static_cast<float>(WorkingDefaultViewCenter.X); },
				[this](float Value)
				{
					WorkingDefaultViewCenter.X = FMath::Max(0.0f, FMath::RoundToFloat(Value));
					MarkWorkingCopyChanged();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			MakeFloatBox(
				LOCTEXT("DefaultViewCenterYLabel", "默认中心格 Y（从1开始）"),
				[this]() { return static_cast<float>(WorkingDefaultViewCenter.Y); },
				[this](float Value)
				{
					WorkingDefaultViewCenter.Y = FMath::Max(0.0f, FMath::RoundToFloat(Value));
					MarkWorkingCopyChanged();
				})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeFloatBox(
				LOCTEXT("DefaultViewScaleLabel", "默认缩放"),
				[this]() { return WorkingDefaultViewScale; },
				[this](float Value)
				{
					WorkingDefaultViewScale = FMath::Max(1.0f, Value);
					MarkWorkingCopyChanged();
				})
		];
}
TSharedRef<SWidget> SGSMConfigPanelWidget::MakeDefaultTileClassPicker()
{
	return GSMEditorWidget::MakeLabeledRow(
		LOCTEXT("DefaultTileActorClassLabel", "默认3D瓦片Actor类"),
		SNew(SClassPropertyEntryBox)
		.MetaClass(AGSMTile3D::StaticClass())
		.ToolTipText(LOCTEXT("DefaultTileActorClassTooltip", "地图中未单独覆盖类型的瓦片使用此3D Actor类；留空时使用项目设置。"))
		.SelectedClass_Lambda([this]() -> const UClass*
		{
			return WorkingDefaultTileActorClass ? WorkingDefaultTileActorClass.Get() : nullptr;
		})
		.OnSetClass_Lambda([this](const UClass* NewClass)
		{
			WorkingDefaultTileActorClass = const_cast<UClass*>(NewClass);
			MarkWorkingCopyChanged();
		})
		.AllowNone(true)
	);
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeDefaultTileDataClassPicker()
{
	return GSMEditorWidget::MakeLabeledRow(
		LOCTEXT("DefaultTileDataClassLabel", "默认瓦片数据类"),
		SNew(SClassPropertyEntryBox)
		.MetaClass(UGSMTileData::StaticClass())
		.ToolTipText(LOCTEXT("DefaultTileDataClassTooltip", "地图中未单独覆盖类型的瓦片使用此数据类；留空时使用UGSMTileData。"))
		.SelectedClass_Lambda([this]() -> const UClass*
		{
			return WorkingDefaultTileDataClass ? WorkingDefaultTileDataClass.Get() : nullptr;
		})
		.OnSetClass_Lambda([this](const UClass* NewClass)
		{
			WorkingDefaultTileDataClass = const_cast<UClass*>(NewClass);
			MarkWorkingCopyChanged();
		})
		.AllowNone(true)
	);
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakePathArrowClassPicker()
{
	return GSMEditorWidget::MakeLabeledRow(
		LOCTEXT("PathArrowActorClassLabel", "3D导航箭头类"),
		SNew(SClassPropertyEntryBox)
		.MetaClass(AGSMPathArrow3D::StaticClass())
		.ToolTipText(LOCTEXT("PathArrowActorClassTooltip", "3D 地图导航时生成的路径箭头 Actor 类；留空时使用 3D 地图 Actor 自身配置。"))
		.SelectedClass_Lambda([this]() -> const UClass*
		{
			return WorkingPathArrowActorClass ? WorkingPathArrowActorClass.Get() : nullptr;
		})
		.OnSetClass_Lambda([this](const UClass* NewClass)
		{
			WorkingPathArrowActorClass = const_cast<UClass*>(NewClass);
			MarkWorkingCopyChanged();
		})
		.AllowNone(true)
	);
}

TSharedRef<SWidget> SGSMConfigPanelWidget::MakeTileClassPickers()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			GSMEditorWidget::MakeLabeledRow(
				LOCTEXT("TileDataClassLabel", "瓦片数据类"),
				SNew(SClassPropertyEntryBox)
				.MetaClass(UGSMTileData::StaticClass())
				.ToolTipText(LOCTEXT("TileDataClassTooltip", "留空时使用地图配置中的默认瓦片数据类。"))
				.SelectedClass_Lambda([this]() -> const UClass*
				{
					const FGSMTileEntry* Tile = GetSelectedTile();
					return Tile && Tile->TileDataClass ? Tile->TileDataClass.Get() : nullptr;
				})
				.OnSetClass_Lambda([this](const UClass* NewClass)
				{
					if (FGSMTileEntry* Tile = GetSelectedTile())
					{
						Tile->TileDataClass = const_cast<UClass*>(NewClass);
						MarkWorkingCopyChanged();
					}
				})
				.AllowNone(true)
			)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			GSMEditorWidget::MakeLabeledRow(
				LOCTEXT("TileActorClassLabel", "3D瓦片Actor类"),
				SNew(SClassPropertyEntryBox)
				.MetaClass(AGSMTile3D::StaticClass())
				.ToolTipText(LOCTEXT("TileActorClassTooltip", "留空时使用地图配置中的默认3D瓦片Actor类。"))
				.SelectedClass_Lambda([this]() -> const UClass*
				{
					const FGSMTileEntry* Tile = GetSelectedTile();
					return Tile && Tile->TileActorClass ? Tile->TileActorClass.Get() : nullptr;
				})
				.OnSetClass_Lambda([this](const UClass* NewClass)
				{
					if (FGSMTileEntry* Tile = GetSelectedTile())
					{
						Tile->TileActorClass = const_cast<UClass*>(NewClass);
						MarkWorkingCopyChanged();
					}
				})
				.AllowNone(true)
			)
		];
}

FString SGSMConfigPanelWidget::GetWalkingNeighborsText() const
{
	const FGSMTileEntry* Tile = GetSelectedTile();
	return Tile ? JoinNameList(Tile->Navigation.WalkingNeighborTileIds) : FString();
}

void SGSMConfigPanelWidget::SetWalkingNeighborsText(const FString& Text)
{
	if (FGSMTileEntry* Tile = GetSelectedTile())
	{
		TArray<FName> ParsedNeighborIds = ParseNameList(Text);
		if (!Tile->Navigation.bCanWalkThrough)
		{
			ParsedNeighborIds.Reset();
		}
		else
		{
			const FIntPoint SourceCoordinate = Tile->GridCoordinate;
			ParsedNeighborIds.RemoveAll([this](FName NeighborTileId)
			{
				const FGSMTileEntry* NeighborTile = WorkingTileEntries.FindByPredicate([NeighborTileId](const FGSMTileEntry& TileEntry)
				{
					return TileEntry.TileId == NeighborTileId;
				});
				return !NeighborTile || !NeighborTile->Navigation.bCanWalkThrough;
			});
			ParsedNeighborIds.RemoveAll([this, SourceCoordinate](FName NeighborTileId)
			{
				const FGSMTileEntry* NeighborTile = WorkingTileEntries.FindByPredicate([NeighborTileId](const FGSMTileEntry& TileEntry)
				{
					return TileEntry.TileId == NeighborTileId;
				});
				if (!NeighborTile)
				{
					return true;
				}

				const FIntPoint Delta = NeighborTile->GridCoordinate - SourceCoordinate;
				return !GSMEditorWidget::SquareNeighborOffsets().Contains(Delta);
			});
		}

		Tile->Navigation.WalkingNeighborTileIds = ParsedNeighborIds;
		MarkWorkingCopyChanged();
	}
}

FString SGSMConfigPanelWidget::GetRoadConnectionsText() const
{
	const FGSMTileEntry* Tile = GetSelectedTile();
	if (!Tile)
	{
		return FString();
	}

	TArray<FString> Parts;
	for (const FGSMRoadConnection& RoadConnection : Tile->Navigation.RoadConnections)
	{
		if (RoadConnection.TargetTileId.IsNone())
		{
			continue;
		}

		if (RoadConnection.CostOverride >= 0.0f)
		{
			Parts.Add(FString::Printf(TEXT("%s:%g"), *RoadConnection.TargetTileId.ToString(), RoadConnection.CostOverride));
		}
		else
		{
			Parts.Add(RoadConnection.TargetTileId.ToString());
		}
	}

	return FString::Join(Parts, TEXT(", "));
}

void SGSMConfigPanelWidget::SetRoadConnectionsText(const FString& Text)
{
	FGSMTileEntry* Tile = GetSelectedTile();
	if (!Tile)
	{
		return;
	}

	Tile->Navigation.RoadConnections.Reset();

	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(","), true);
	for (FString Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (Part.IsEmpty())
		{
			continue;
		}

		FString TargetNameString;
		FString CostString;
		float CostOverride = -1.0f;
		if (Part.Split(TEXT(":"), &TargetNameString, &CostString))
		{
			TargetNameString.TrimStartAndEndInline();
			CostString.TrimStartAndEndInline();
			FDefaultValueHelper::ParseFloat(CostString, CostOverride);
		}
		else
		{
			TargetNameString = Part;
		}

		TargetNameString.TrimStartAndEndInline();
		if (TargetNameString.IsEmpty())
		{
			continue;
		}

		FGSMRoadConnection RoadConnection;
		RoadConnection.TargetTileId = FName(*TargetNameString);
		RoadConnection.CostOverride = CostOverride;
		RoadConnection.bBidirectional = true;
		Tile->Navigation.RoadConnections.Add(RoadConnection);
	}

	MarkWorkingCopyChanged();
}

TArray<FName> SGSMConfigPanelWidget::ParseNameList(const FString& Text)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(","), true);

	TArray<FName> Names;
	for (FString Part : Parts)
	{
		Part.TrimStartAndEndInline();
		if (!Part.IsEmpty())
		{
			Names.AddUnique(FName(*Part));
		}
	}

	return Names;
}

FString SGSMConfigPanelWidget::JoinNameList(const TArray<FName>& Names)
{
	TArray<FString> Parts;
	for (const FName Name : Names)
	{
		if (!Name.IsNone())
		{
			Parts.Add(Name.ToString());
		}
	}

	return FString::Join(Parts, TEXT(", "));
}

FString SGSMConfigPanelWidget::MakeTileCoordinateLabel(FIntPoint GridCoordinate) const
{
	return GSMLayout::MakeTileCoordinateLabel(
		GridCoordinate,
		WorkingHorizontalLabelType,
		WorkingVerticalLabelType);
}

void SGSMEditorWidget::Construct(const FArguments& InArgs)
{
	SAssignNew(ConfigPanelWidget, SGSMConfigPanelWidget)
		.OnTilesChanged(FGSMOnTilesChanged::CreateSP(this, &SGSMEditorWidget::NotifyPreviewDataChanged));

	TSharedPtr<SScrollBar> HorizontalPreviewScrollBar;
	TSharedPtr<SScrollBar> VerticalPreviewScrollBar;
	SAssignNew(HorizontalPreviewScrollBar, SScrollBar)
		.Orientation(Orient_Horizontal);
	SAssignNew(VerticalPreviewScrollBar, SScrollBar)
		.Orientation(Orient_Vertical);

	const auto MakeLegendItem = [](const FLinearColor& Color, const FText& Label) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(10.0f)
				.HeightOverride(10.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(Color)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	};

	ChildSlot
	[
		SNew(SSplitter)
		+ SSplitter::Slot()
		.Value(0.64f)
		[
			SNew(SBox)
			.MinDesiredWidth(520.0f)
			.MinDesiredHeight(360.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 6.0f, 8.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("TileGridTitle", "瓦片导航网格"))
						.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpacer)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 12.0f, 0.0f)
					[
						MakeLegendItem(FLinearColor(0.26f, 0.075f, 0.075f, 1.0f), LOCTEXT("BlockedLegend", "阻塞"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 12.0f, 0.0f)
					[
						MakeLegendItem(FLinearColor(0.05f, 0.32f, 0.76f, 1.0f), LOCTEXT("RoadLegend", "道路"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						MakeLegendItem(FLinearColor(0.08f, 0.36f, 0.92f, 1.0f), LOCTEXT("SelectedLegend", "选中"))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.0f, 0.0f, 8.0f, 6.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TileGridHint", "Ctrl 单击可逐个增减选择；Shift 单击可选择锚点与目标之间的矩形区域。"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SScrollBox)
						.Orientation(Orient_Horizontal)
						.ExternalScrollbar(HorizontalPreviewScrollBar)
						.ScrollBarAlwaysVisible(false)
						+ SScrollBox::Slot()
						[
							SNew(SScrollBox)
							.Orientation(Orient_Vertical)
							.ExternalScrollbar(VerticalPreviewScrollBar)
							.ScrollBarAlwaysVisible(false)
							+ SScrollBox::Slot()
							[
								SAssignNew(PreviewWidget, SGSMPreviewWidget)
								.GetEditableTiles(FGSMGetEditableTiles::CreateSP(ConfigPanelWidget.ToSharedRef(), &SGSMConfigPanelWidget::GetEditableTiles))
								.GetHorizontalLabelType(FGSMGetCoordinateLabelType::CreateSP(ConfigPanelWidget.ToSharedRef(), &SGSMConfigPanelWidget::GetHorizontalLabelType))
								.GetVerticalLabelType(FGSMGetCoordinateLabelType::CreateSP(ConfigPanelWidget.ToSharedRef(), &SGSMConfigPanelWidget::GetVerticalLabelType))
								.GetSelectedTileIndex(FGSMGetSelectedTileIndex::CreateSP(ConfigPanelWidget.ToSharedRef(), &SGSMConfigPanelWidget::GetSelectedTileIndex))
								.GetSelectedTileIndices(FGSMGetSelectedTileIndices::CreateSP(ConfigPanelWidget.ToSharedRef(), &SGSMConfigPanelWidget::GetSelectedTileIndices))
								.OnTileSelected(FGSMOnPreviewTileSelected::CreateSP(ConfigPanelWidget.ToSharedRef(), &SGSMConfigPanelWidget::SelectTileByIndex))
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						VerticalPreviewScrollBar.ToSharedRef()
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						HorizontalPreviewScrollBar.ToSharedRef()
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(12.0f)
						.HeightOverride(12.0f)
					]
				]
			]
		]
		+ SSplitter::Slot()
		.Value(0.36f)
		[
			ConfigPanelWidget.ToSharedRef()
		]
	];
}

void SGSMEditorWidget::NotifyPreviewDataChanged() const
{
	if (PreviewWidget.IsValid())
	{
		PreviewWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

#undef LOCTEXT_NAMESPACE
