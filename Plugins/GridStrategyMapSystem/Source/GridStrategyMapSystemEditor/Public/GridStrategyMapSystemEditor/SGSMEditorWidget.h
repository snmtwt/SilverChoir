#pragma once

#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "Widgets/SCompoundWidget.h"

class SGSMConfigPanelWidget;
class SGSMPreviewWidget;
class UGSMMapDataAsset;
class AGSMTile3D;
class AGSMPathArrow3D;
class UStaticMesh;

DECLARE_DELEGATE_RetVal(TArray<FGSMTileEntry>*, FGSMGetEditableTiles);
DECLARE_DELEGATE_RetVal(EGSMCoordinateLabelType, FGSMGetCoordinateLabelType);
DECLARE_DELEGATE_RetVal(int32, FGSMGetSelectedTileIndex);
DECLARE_DELEGATE_RetVal(TArray<int32>, FGSMGetSelectedTileIndices);
DECLARE_DELEGATE_ThreeParams(FGSMOnPreviewTileSelected, int32, bool, bool);
DECLARE_DELEGATE(FGSMOnTilesChanged);

/**
 * 左侧地图瓦片预览控件。
 *
 * 这个控件只负责绘制和点击命中，不持有资产，也不保存数据。它通过委托读取当前瓦片工作副本，
 * 因此未来可以独立替换为四边形、等距、真实缩略图等其它预览实现。
 */
class SGSMPreviewWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGSMPreviewWidget) {}
		SLATE_EVENT(FGSMGetEditableTiles, GetEditableTiles)
		SLATE_EVENT(FGSMGetCoordinateLabelType, GetHorizontalLabelType)
		SLATE_EVENT(FGSMGetCoordinateLabelType, GetVerticalLabelType)
		SLATE_EVENT(FGSMGetSelectedTileIndex, GetSelectedTileIndex)
		SLATE_EVENT(FGSMGetSelectedTileIndices, GetSelectedTileIndices)
		SLATE_EVENT(FGSMOnPreviewTileSelected, OnTileSelected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled
	) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

private:
	struct FPreviewTileGeometry
	{
		int32 TileIndex = INDEX_NONE;
		FVector2D Center = FVector2D::ZeroVector;
		TArray<FVector2D> Points;
	};

	void BuildPreviewGeometries(const FGeometry& AllottedGeometry) const;
	int32 FindTileIndexAtLocalPosition(const FVector2D& LocalPosition) const;
	static bool IsPointInsidePolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon);

private:
	FGSMGetEditableTiles GetEditableTiles;
	FGSMGetCoordinateLabelType GetHorizontalLabelType;
	FGSMGetCoordinateLabelType GetVerticalLabelType;
	FGSMGetSelectedTileIndex GetSelectedTileIndex;
	FGSMGetSelectedTileIndices GetSelectedTileIndices;
	FGSMOnPreviewTileSelected OnTileSelected;

	mutable TArray<FPreviewTileGeometry> CachedTileGeometries;
	mutable FVector2D CachedGeometrySize = FVector2D::ZeroVector;
	int32 HoveredTileIndex = INDEX_NONE;
};

/**
 * 右侧地图配置面板。
 *
 * 这个控件负责选择配置资产、维护可编辑工作副本、编辑选中瓦片以及保存回资产。左侧预览控件只通过
 * GetEditableTiles/SelectTileByIndex 与它交互，因此二者可以被其它编辑器容器重新组合。
 */
class SGSMConfigPanelWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGSMConfigPanelWidget) {}
		SLATE_EVENT(FGSMOnTilesChanged, OnTilesChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	TArray<FGSMTileEntry>* GetEditableTiles();
	EGSMCoordinateLabelType GetHorizontalLabelType() const { return WorkingHorizontalLabelType; }
	EGSMCoordinateLabelType GetVerticalLabelType() const { return WorkingVerticalLabelType; }
	int32 GetSelectedTileIndex() const { return SelectedTileIndex; }
	TArray<int32> GetSelectedTileIndices() const { return SelectedTileIndices; }
	void SelectTileByIndex(int32 TileIndex, bool bControlDown, bool bShiftDown);

private:
	UGSMMapDataAsset* GetEditingAsset() const;
	FGSMTileEntry* GetSelectedTile();
	const FGSMTileEntry* GetSelectedTile() const;

	FString GetSelectedAssetPath() const;
	void OnAssetSelected(const struct FAssetData& AssetData);
	FString GetWholeMapTerrainMeshPath() const;
	void OnWholeMapTerrainMeshSelected(const struct FAssetData& AssetData);
	void LoadAssetIntoWorkingCopy(UGSMMapDataAsset* InAsset);
	bool CommitWorkingCopyToAsset(bool bSavePackage);

	FText GetConfigSummaryText() const;
	FText GetSelectedTileTitleText() const;
	FText GetMultiSelectionText() const;
	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;

	void RefreshTileDetails();
	void RebuildTileDetails();
	void MarkWorkingCopyChanged();
	void BroadcastTilesChanged() const;
	bool HasMultiSelection() const;
	bool HasAnySelectedRoadConnection() const;
	ECheckBoxState GetSelectedTilesWalkableState() const;
	void SetSelectedTilesWalkable(bool bWalkable);
	void RebuildWorkingGridFromDimensions(bool bPreserveExistingTiles);
	void ApplyDefaultTileClassesToGrid();
	void RefreshTileIdsFromCoordinates();
	void DeriveGridDimensionsFromTiles();
	FGSMTileEntry MakeDefaultTileEntry(FIntPoint GridCoordinate) const;

	FReply SaveToAsset();
	FReply RebuildGrid();
	FReply AutoFillWalkingNeighbors();
	FReply ConnectSelectedTilesAsRoad();
	FReply DisconnectSelectedTilesAsRoad();
	FReply ValidateConfig();

	TSharedRef<SWidget> BuildPanel();
	TSharedRef<SWidget> BuildMapConfigArea();
	TSharedRef<SWidget> BuildTileDetailsPanel();
	TSharedRef<SWidget> MakeNameTextBox(
		const FText& Label,
		TFunction<FName()> Getter,
		TFunction<void(FName)> Setter
	);
	TSharedRef<SWidget> MakeStringTextBox(
		const FText& Label,
		TFunction<FString()> Getter,
		TFunction<void(const FString&)> Setter
	);
	TSharedRef<SWidget> MakeIntBox(
		const FText& Label,
		TFunction<int32()> Getter,
		TFunction<void(int32)> Setter
	);
	TSharedRef<SWidget> MakeFloatBox(
		const FText& Label,
		TFunction<float()> Getter,
		TFunction<void(float)> Setter
	);
	TSharedRef<SWidget> MakeCheckBox(
		const FText& Label,
		TFunction<bool()> Getter,
		TFunction<void(bool)> Setter
	);
	TSharedRef<SWidget> MakeGridDimensionControls();
	TSharedRef<SWidget> MakeCoordinateLabelTypePicker(const FText& Label, bool bHorizontal);
	TSharedRef<SWidget> MakeTerrainConfigPanel();
	TSharedRef<SWidget> MakeDefaultTileClassPicker();
	TSharedRef<SWidget> MakeDefaultTileDataClassPicker();
	TSharedRef<SWidget> MakePathArrowClassPicker();
	TSharedRef<SWidget> MakeTileClassPickers();
	TSharedRef<SWidget> MakeDefaultViewPanel();

	FString GetWalkingNeighborsText() const;
	void SetWalkingNeighborsText(const FString& Text);
	FString GetRoadConnectionsText() const;
	void SetRoadConnectionsText(const FString& Text);

	static TArray<FName> ParseNameList(const FString& Text);
	static FString JoinNameList(const TArray<FName>& Names);
	FString MakeTileCoordinateLabel(FIntPoint GridCoordinate) const;

private:
	FGSMOnTilesChanged OnTilesChanged;

	TWeakObjectPtr<UGSMMapDataAsset> EditingAsset;
	FString WorkingMapName;
	TArray<FGSMTileEntry> WorkingTileEntries;
	TArray<FGSMRegionDefinition> WorkingRegionDefinitions;
	TSubclassOf<AGSMTile3D> WorkingDefaultTileActorClass;
	TSubclassOf<UGSMTileData> WorkingDefaultTileDataClass;
	TSubclassOf<AGSMPathArrow3D> WorkingPathArrowActorClass;
	TSubclassOf<AGSMTile3D> LastAppliedDefaultTileActorClass;
	TSubclassOf<UGSMTileData> LastAppliedDefaultTileDataClass;
	TWeakObjectPtr<UStaticMesh> WorkingWholeMapTerrainMesh;
	bool bWorkingFitWholeMapTerrainMeshToTileGridBounds = true;
	bool bWorkingScaleWholeMapTerrainMeshZWithXY = true;
	FVector WorkingWholeMapTerrainMeshBaseScale = FVector::OneVector;
	float WorkingWholeMapTerrainMeshHeightOffset = 5.0f;
	bool bWorkingAlignWholeMapTerrainMeshBottomToGroovePlane = true;
	float WorkingWholeMapTerrainMeshYawDegrees = 180.0f;
	int32 WorkingGridColumns = 10;
	int32 WorkingGridRows = 8;
	EGSMCoordinateLabelType WorkingHorizontalLabelType = EGSMCoordinateLabelType::Letters;
	EGSMCoordinateLabelType WorkingVerticalLabelType = EGSMCoordinateLabelType::Numbers;
	FVector2D WorkingDefaultViewCenter = FVector2D::ZeroVector;
	float WorkingDefaultViewScale = 1.0f;
	int32 SelectedTileIndex = INDEX_NONE;
	int32 SelectionAnchorTileIndex = INDEX_NONE;
	TArray<int32> SelectedTileIndices;
	bool bHasUnsavedChanges = false;
	FText StatusText;
	bool bStatusIsError = false;

	TSharedPtr<SVerticalBox> TileDetailsBox;
};

/**
 * 工具菜单打开的默认组合界面。
 *
 * 它只把左侧预览和右侧配置面板拼在一起；真正的逻辑分别在两个子控件中，便于以后复用。
 */
class SGSMEditorWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGSMEditorWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void NotifyPreviewDataChanged() const;

private:
	TSharedPtr<SGSMPreviewWidget> PreviewWidget;
	TSharedPtr<SGSMConfigPanelWidget> ConfigPanelWidget;
};
