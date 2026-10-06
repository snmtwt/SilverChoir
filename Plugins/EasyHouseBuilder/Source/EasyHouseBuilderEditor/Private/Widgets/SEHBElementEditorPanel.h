// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cutting/EHBCutTypes.h"
#include "Widgets/SCompoundWidget.h"

class AActor;
class AEHBBuildingActorBase;
class AEHBElementActorBase;
class AEHB_DoorWindow;
class AEHB_FloorSlab;
class AEHBGableRoof;
class AEHB_Pillar;
class AEHB_Railing;
class AEHB_Stair;
class AEHB_Wall;
class SVerticalBox;

/**
 * A singleton editor panel for the currently selected building object or element.
 * It is intentionally small for now: later passes can replace the type label with
 * type-specific property controls without changing selection routing.
 */
class SEHBElementEditorPanel : public SCompoundWidget
{
#if WITH_DEV_AUTOMATION_TESTS
 friend class FEHBFinishRegionControlsTest;
 friend class FEHBWallOpeningControlsTest;
 friend class FEHBOpeningDragTest;
#endif
public:
	SLATE_BEGIN_ARGS(SEHBElementEditorPanel) {}
	SLATE_END_ARGS()

	virtual ~SEHBElementEditorPanel() override;

	void Construct(const FArguments& InArgs);

	void SetSelectedActor(AActor* InSelectedActor);
	void SetSelectedFloorSlabCorner(AEHB_FloorSlab* InFloorSlab, int32 InLoopIndex, int32 InPointIndex);

private:
	enum class EEHBWallBasicField : uint8
	{
		Height,
		Thickness,
		CurveControlOffset,
		CurveSegmentLength
	};

	enum class EEHBPillarBasicField : uint8
	{
		Height,
		Width,
		Depth
	};

	enum class EEHBRailingBasicField : uint8
	{
		RailTopHeight,
		PostSpacing,
		PostWidth,
		PostHeight,
		RailThickness,
		MaxRailSegmentLength
	};

	enum class EEHBStairNumericField : uint8
	{
		TreadDepth,
		StairWidth,
		StairHeight,
		MinStepHeight,
		MaxStepHeight,
		PanelThickness,
		NosingLength,
		SideProtruding,
		SideThickness,
		SideBoardHeight,
		SideBoardTopOffset,
		SideGuardThickness,
		SideGuardHeight,
		SideGuardTopOffset,
		RailingStepsPerPost,
		RailingEdgeInset,
		RailingPostForwardOffset,
		RailingPostWidth,
		RailingPostHeight,
		RailingRailHeight,
		RailingRailThickness,
		RailingMaxRailSegmentLength
	};

	enum class EEHBStairToggleField : uint8
	{
		UseActualDimensions,
		GenerateTreads,
		FillRisers,
		FillBottomPart,
		GenerateSides,
		GenerateSideGuards,
		GenerateRailing,
		GenerateLeftRailing,
		GenerateRightRailing
	};

	enum class EEHBRoofAccessoryToggle : uint8
	{
		Ridge,
		Eaves,
		GableRakes,
		GableEndWalls
	};

	FText GetSelectedTypeText() const;
	FText GetSelectedActorText() const;
	FText GetSelectedClassText() const;
	FText GetElementIdentityText() const;
	FText GetRelationSummaryText() const;
	FText GetGeneratedMeshStatsText() const;
	FText GetSampleSourceSummaryText() const;
	EVisibility GetDiagnosticsVisibility() const;
	EVisibility GetFloorVisibilityControlsVisibility() const;
	EVisibility GetBuildingControlsVisibility() const;
	EVisibility GetRoofCuttingControlsVisibility() const;
	EVisibility GetWallControlsVisibility() const;
	EVisibility GetPillarControlsVisibility() const;
	EVisibility GetWindowControlsVisibility() const;
	EVisibility GetRailingControlsVisibility() const;
	EVisibility GetStairControlsVisibility() const;
	EVisibility GetFloorSlabControlsVisibility() const;
	TSharedRef<SWidget> BuildIndependentRegionControls();
	TSharedRef<SWidget> BuildWallOpeningControls();
	FReply ApplyWallOpeningRectangle(bool bRemoveSelected, bool bUpdateSelected = false);
	TSharedRef<SWidget> BuildWallOpeningMenu();
	bool SelectWallOpening(FGuid OperationGuid);
	FText GetWallOpeningSelectionText() const;
	FGuid SelectedWallOpening;
	TOptional<FEHBCutOperation> SelectedWallOpeningSource;
	FVector4f WallOpeningRectangle=FVector4f(40,90,100,120);
	FText WallOpeningFeedback;
	float GetIndependentRegionSize(bool bX) const;
	void HandleIndependentRegionSize(float Value,ETextCommit::Type Commit,bool bX);
	FReply HandleIndependentRegionBind();
	EVisibility GetFloorSlabCornerControlsVisibility() const;
	float GetWallHeight() const;
	float GetWallThickness() const;
	float GetWallCurveControlOffset() const;
	float GetWallCurveSegmentLength() const;
	float GetWallTargetLength() const;
	bool CanMoveWallLengthPillar() const;
	float GetLeftWallSampleStartOffset() const;
	float GetRightWallSampleStartOffset() const;
	bool IsLeftWallSampleStartOffsetEnabled() const;
	bool IsRightWallSampleStartOffsetEnabled() const;
	float GetWindowSillHeight() const;
	float GetPillarHeight() const;
	float GetPillarWidth() const;
	float GetPillarDepth() const;
	float GetRailingRailTopHeight() const;
	float GetRailingPostSpacing() const;
	float GetRailingPostWidth() const;
	float GetRailingPostHeight() const;
	float GetRailingRailThickness() const;
	float GetRailingMaxRailSegmentLength() const;
	float GetStairNumericField(EEHBStairNumericField Field) const;
	float GetFloorSlabVisualExpansion() const;
	float GetFloorSlabPreviousCornerDistance() const;
	float GetFloorSlabNextCornerDistance() const;
	ECheckBoxState IsStairToggleChecked(EEHBStairToggleField Field) const;
	void HandleStairToggleChanged(ECheckBoxState NewState, EEHBStairToggleField Field);
	ECheckBoxState IsWallLinkedPillarEndCapsChecked() const;
	void HandleWallLinkedPillarEndCapsChanged(ECheckBoxState NewState);
	ECheckBoxState IsPillarLinkedWallFacesChecked() const;
	void HandlePillarLinkedWallFacesChanged(ECheckBoxState NewState);
	ECheckBoxState IsFloorSlabAdjacentSnapChecked() const;
	ECheckBoxState IsRoofCutCollidingElementsChecked() const;
	void HandleRoofCutCollidingElementsChanged(ECheckBoxState NewState);
	ECheckBoxState IsRoofRemoveDisconnectedCutPiecesChecked() const;
	void HandleRoofRemoveDisconnectedCutPiecesChanged(ECheckBoxState NewState);
	ECheckBoxState IsRoofKeepCutAwayDisconnectedPieceChecked() const;
	void HandleRoofKeepCutAwayDisconnectedPieceChanged(ECheckBoxState NewState);
	bool IsRoofKeepCutAwayDisconnectedPieceEnabled() const;
	ECheckBoxState IsRoofAccessoryChecked(EEHBRoofAccessoryToggle Toggle) const;
	void HandleRoofAccessoryChanged(ECheckBoxState NewState, EEHBRoofAccessoryToggle Toggle);
	float GetRoofGableEndWallBoundaryInset() const;
	void HandleRoofGableEndWallBoundaryInsetChanged(float NewValue);
	bool IsRoofGableEndWallBoundaryInsetEnabled() const;
	void ApplyWallBasicField(EEHBWallBasicField Field, float NewValue);
	void HandleWallTargetLengthChanged(float NewValue);
	FReply HandleMoveWallLengthPillarClicked(bool bMoveStartPillar);
	void HandleWallSampleStartOffsetChanging(bool bLeftSide, float NewValue);
	void ApplyWallSampleStartOffset(bool bLeftSide, float NewValue);
	void ApplyWindowSillHeight(float NewValue);
	void ApplyPillarBasicField(EEHBPillarBasicField Field, float NewValue);
	void ApplyRailingBasicField(EEHBRailingBasicField Field, float NewValue);
	void ApplyStairNumericField(EEHBStairNumericField Field, float NewValue);
	void HandleFloorSlabVisualExpansionChanged(float NewValue);
	void HandleFloorSlabCornerDistanceCommitted(float NewValue, ETextCommit::Type CommitType, bool bPreviousPoint);
	void HandleFloorSlabAdjacentSnapChanged(ECheckBoxState NewState);
	EVisibility GetSelectionVisibility() const;
	EVisibility GetEmptyVisibility() const;
	TSharedRef<SWidget> BuildFloorVisibilityControls();
	TSharedRef<SWidget> BuildRailingControls();
	TSharedRef<SWidget> BuildStairControls();
	void RefreshFloorVisibilityControls();
	FReply HandleFloorVisibilityClicked(TOptional<int32> FloorIndex);
	ECheckBoxState IsFloorVisibilityChecked(TOptional<int32> FloorIndex) const;
	void ApplyFloorVisibilityFilter();
	void ResetFloorVisibilityFilter();
	void ApplyFloorVisibilityElementState(AEHBElementActorBase* Element, bool bHideElement);
	void RestoreFloorVisibilityCollisionState(AEHBElementActorBase* Element);
	void RestoreAllFloorVisibilityCollisionStates();
	bool CanClearSelectedBuildingElements() const;
	FReply HandleClearBuildingElementsClicked();
	TArray<int32> GetAvailableFloorIndices() const;
	TArray<AEHBGableRoof*> GetEditorSelectedRoofs() const;
	AEHBBuildingActorBase* GetSelectedBuildingObjectMutable() const;
	AEHBBuildingActorBase* GetSelectedBuildingMutable() const;
	AEHB_Wall* GetSelectedWall() const;
	AEHB_DoorWindow* GetSelectedDoorWindow() const;
	AEHB_Pillar* GetSelectedPillar() const;
	AEHB_Railing* GetSelectedRailing() const;
	AEHB_Stair* GetSelectedStair() const;
	AEHBGableRoof* GetSelectedRoof() const;
	AEHB_FloorSlab* GetSelectedFloorSlab() const;
	const AEHBElementActorBase* GetSelectedElementActor() const;
	const AEHBBuildingActorBase* GetSelectedBuilding() const;
	void RedrawEditorViewports() const;

	TWeakObjectPtr<AActor> SelectedActor;
	TWeakObjectPtr<AEHBBuildingActorBase> FloorVisibilityBuilding;
	TOptional<int32> VisibleFloorLimit;
	TMap<TWeakObjectPtr<AEHBElementActorBase>, bool> FloorVisibilityCollisionStates;
	TSharedPtr<SVerticalBox> FloorVisibilityButtonsBox;
	TOptional<float> PendingWallTargetLength;
	TOptional<float> PendingLeftWallSampleStartOffset;
	TOptional<float> PendingRightWallSampleStartOffset;
	TWeakObjectPtr<AEHB_FloorSlab> SelectedFloorSlabCornerOwner;
	int32 SelectedFloorSlabCornerLoopIndex = INDEX_NONE;
	int32 SelectedFloorSlabCornerPointIndex = INDEX_NONE;
};
