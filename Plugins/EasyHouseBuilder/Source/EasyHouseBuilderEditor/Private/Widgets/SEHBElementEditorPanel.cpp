// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SEHBElementEditorPanel.h"
#include "EHBFinishRegionDrag.h"
#include "EHBFinishRegionCommand.h"
#include "Toolsets/EHBBuildingToolset.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "Actors/EHBElementActorBase.h"
#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHBGableRoof.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Railing.h"
#include "Actors/EHB_Stair.h"
#include "Actors/EHB_Wall.h"
#include "Core/EHBBuildingActorBase.h"
#include "Core/EHBLogicalSurface.h"
#include "Diagnostics/EHBBuildingPerformanceAnalyzer.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "Engine/Selection.h"
#include "Misc/MessageDialog.h"
#include "Sampling/EHBPillarMeshData.h"
#include "Sampling/EHBRailingMeshData.h"
#include "Sampling/EHBWallMeshData.h"
#include "ScopedTransaction.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SEHBElementEditorPanel"

namespace
{
	constexpr int32 EHBFloorVisibilityRoofIndex = TNumericLimits<int32>::Min();

	bool IsRoofVisibilityIndex(TOptional<int32> FloorIndex)
	{
		return FloorIndex.IsSet() && FloorIndex.GetValue() == EHBFloorVisibilityRoofIndex;
	}

	bool IsRoofVisibilityElement(const AEHBElementActorBase* Element)
	{
		return Element
			&& (Element->ElementType == EEHBBuildingElementType::Roof
				|| Element->FloorRole == EEHBBuildingFloorElementRole::Roof);
	}

	FText GetElementTypeDisplayText(EEHBBuildingElementType ElementType)
	{
		switch (ElementType)
		{
		case EEHBBuildingElementType::MeshSampling:
			return LOCTEXT("MeshSamplingType", "\u7f51\u683c\u4f53\u91c7\u6837");
		case EEHBBuildingElementType::FoundationAndFloor:
			return LOCTEXT("FoundationAndFloorType", "\u5730\u57fa/\u5c42\u677f");
		case EEHBBuildingElementType::Wall:
			return LOCTEXT("WallType", "\u5899\u4f53");
		case EEHBBuildingElementType::DoorWindow:
			return LOCTEXT("DoorWindowType", "\u95e8\u7a97");
		case EEHBBuildingElementType::Railing:
			return LOCTEXT("RailingType", "\u6276\u624b");
		case EEHBBuildingElementType::Roof:
			return LOCTEXT("RoofType", "\u5c4b\u9876");
		case EEHBBuildingElementType::Floor:
			return LOCTEXT("FloorType", "\u5730\u677f");
		case EEHBBuildingElementType::Stair:
			return LOCTEXT("StairType", "\u697c\u68af");
		case EEHBBuildingElementType::Pillar:
			return LOCTEXT("PillarType", "\u67f1\u4f53");
		default:
			return LOCTEXT("UnknownElementType", "\u672a\u77e5\u5143\u7d20");
		}
	}

	FString GetPerformanceClassLabel(EEHBMeshSamplePerformanceClass PerformanceClass)
	{
		switch (PerformanceClass)
		{
		case EEHBMeshSamplePerformanceClass::Light:
			return TEXT("轻量");
		case EEHBMeshSamplePerformanceClass::Moderate:
			return TEXT("中等");
		case EEHBMeshSamplePerformanceClass::Heavy:
			return TEXT("较重");
		case EEHBMeshSamplePerformanceClass::Critical:
			return TEXT("严重");
		default:
			return TEXT("未知");
		}
	}

	FString FormatObjectPath(const FSoftObjectPath& ObjectPath)
	{
		return ObjectPath.IsValid() ? ObjectPath.ToString() : TEXT("无");
	}

	FString FormatSampleMetadata(const FEHBMeshSampleTemplateMetadata& Metadata)
	{
		const FString TemplateName = Metadata.TemplateName.IsNone() ? TEXT("未命名") : Metadata.TemplateName.ToString();
		return FString::Printf(
			TEXT("%s，性能等级 %s，估算开销 %d"),
			*TemplateName,
			*GetPerformanceClassLabel(Metadata.PerformanceClass),
			Metadata.EstimatedApplyCost);
	}

	FString FormatRowHandleName(const FDataTableRowHandle& RowHandle)
	{
		const FString TableName = RowHandle.DataTable ? RowHandle.DataTable->GetName() : TEXT("无");
		const FString RowName = RowHandle.RowName.IsNone() ? TEXT("无") : RowHandle.RowName.ToString();
		return FString::Printf(TEXT("%s:%s"), *TableName, *RowName);
	}

	FString DescribeWallSurfaceStyle(const TCHAR* Label, const FEHBWallSurfaceStyle& SurfaceStyle)
	{
		if (SurfaceStyle.SourceType != EEHBWallSurfaceSourceType::SampledMesh)
		{
			return FString::Printf(TEXT("%s 简单墙面"), Label);
		}

		const FDataTableRowHandle& RowHandle = SurfaceStyle.SampledWallRow;
		const FString SampleSide = SurfaceStyle.SampleSide == EEHBWallMeshSampleSide::Front ? TEXT("正面") : TEXT("背面");
		if (!RowHandle.DataTable || RowHandle.RowName.IsNone())
		{
			return FString::Printf(TEXT("%s 采样墙面，缺少数据行（%s）"), Label, *SampleSide);
		}
		if (RowHandle.DataTable->GetRowStruct() != FEHBWallMeshData::StaticStruct())
		{
			return FString::Printf(TEXT("%s 采样墙面，数据表类型无效（%s）"), Label, *FormatRowHandleName(RowHandle));
		}

		const FEHBWallMeshData* Row = RowHandle.DataTable->FindRow<FEHBWallMeshData>(
			RowHandle.RowName,
			TEXT("SEHBElementEditorPanel::DescribeWallSurfaceStyle"),
			false);
		if (!Row)
		{
			return FString::Printf(TEXT("%s 采样墙面，找不到数据行（%s）"), Label, *FormatRowHandleName(RowHandle));
		}

		return FString::Printf(
			TEXT("%s %s，采样面 %s，%s"),
			Label,
			*FormatRowHandleName(RowHandle),
			*SampleSide,
			*FormatSampleMetadata(Row->TemplateMetadata));
	}

	bool IsSampledWallSurfaceStyleForEditing(const FEHBWallSurfaceStyle& SurfaceStyle)
	{
		return SurfaceStyle.SourceType == EEHBWallSurfaceSourceType::SampledMesh
			&& SurfaceStyle.SampledWallRow.DataTable
			&& !SurfaceStyle.SampledWallRow.RowName.IsNone()
			&& SurfaceStyle.SampledWallRow.DataTable->GetRowStruct() == FEHBWallMeshData::StaticStruct();
	}

	bool AreWallSurfaceStylesSharedOffsetMatch(const FEHBWallSurfaceStyle& A, const FEHBWallSurfaceStyle& B)
	{
		return IsSampledWallSurfaceStyleForEditing(A)
			&& IsSampledWallSurfaceStyleForEditing(B)
			&& A.SampledWallRow.DataTable == B.SampledWallRow.DataTable
			&& A.SampledWallRow.RowName == B.SampledWallRow.RowName
			&& A.SampleSide == B.SampleSide
			&& A.bFlipSampleSide == B.bFlipSampleSide
			&& A.OverrideMaterial.ToSoftObjectPath() == B.OverrideMaterial.ToSoftObjectPath();
	}

	FString MakeWallSurfaceVisitKey(const AEHB_Wall& Wall, bool bLeftSide)
	{
		return FString::Printf(
			TEXT("%s|%s"),
			*Wall.ElementGuid.ToString(EGuidFormats::DigitsWithHyphens),
			bLeftSide ? TEXT("Left") : TEXT("Right"));
	}

	void ForEachSharedSampleOffsetSurface(
		AEHB_Wall& RootWall,
		bool bRootLeftSide,
		TFunctionRef<void(AEHB_Wall& Wall, bool bLeftSide)> Callback)
	{
		const FEHBWallSurfaceStyle RootStyle = bRootLeftSide ? RootWall.LeftSurfaceStyle : RootWall.RightSurfaceStyle;
		if (!IsSampledWallSurfaceStyleForEditing(RootStyle) || !RootWall.OwningBuilding)
		{
			return;
		}

		TSet<FString> VisitedSurfaces;
		TArray<TPair<TWeakObjectPtr<AEHB_Wall>, bool>> PendingSurfaces;
		PendingSurfaces.Emplace(&RootWall, bRootLeftSide);

		while (!PendingSurfaces.IsEmpty())
		{
			const TPair<TWeakObjectPtr<AEHB_Wall>, bool> Current = PendingSurfaces.Pop(EAllowShrinking::No);
			AEHB_Wall* Wall = Current.Key.Get();
			if (!Wall)
			{
				continue;
			}

			const bool bLeftSide = Current.Value;
			const FString VisitKey = MakeWallSurfaceVisitKey(*Wall, bLeftSide);
			if (VisitedSurfaces.Contains(VisitKey))
			{
				continue;
			}

			const FEHBWallSurfaceStyle& SurfaceStyle = bLeftSide ? Wall->LeftSurfaceStyle : Wall->RightSurfaceStyle;
			if (!AreWallSurfaceStylesSharedOffsetMatch(RootStyle, SurfaceStyle))
			{
				continue;
			}

			VisitedSurfaces.Add(VisitKey);
			Callback(*Wall, bLeftSide);

			const FGuid PillarGuids[] = { Wall->StartPillarGuid, Wall->EndPillarGuid };
			for (const FGuid& PillarGuid : PillarGuids)
			{
				const AEHB_Pillar* Pillar = PillarGuid.IsValid()
					? Cast<AEHB_Pillar>(Wall->OwningBuilding->FindElementActorByGuid(PillarGuid))
					: nullptr;
				if (!Pillar)
				{
					continue;
				}

				for (const FGuid& ConnectedWallGuid : Pillar->ConnectedWallGuids)
				{
					if (!ConnectedWallGuid.IsValid())
					{
						continue;
					}

					AEHB_Wall* ConnectedWall = Cast<AEHB_Wall>(Wall->OwningBuilding->FindElementActorByGuid(ConnectedWallGuid));
					if (!ConnectedWall)
					{
						continue;
					}

					if (AreWallSurfaceStylesSharedOffsetMatch(RootStyle, ConnectedWall->LeftSurfaceStyle))
					{
						PendingSurfaces.Emplace(ConnectedWall, true);
					}
					if (AreWallSurfaceStylesSharedOffsetMatch(RootStyle, ConnectedWall->RightSurfaceStyle))
					{
						PendingSurfaces.Emplace(ConnectedWall, false);
					}
				}
			}
		}
	}

	FEHBBuildingPerformanceBudget GetConfiguredPerformanceBudget()
	{
		if (const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>())
		{
			return Settings->DefaultPerformanceBudget;
		}
		return FEHBBuildingPerformanceBudget();
	}
}

void SEHBElementEditorPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.Padding(10.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				BuildFloorVisibilityControls()
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SScrollBox)

				+ SScrollBox::Slot()
				[
					SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ElementEditorTitle", "\u5efa\u7b51\u5143\u7d20\u7f16\u8f91"))
				.TextStyle(FAppStyle::Get(), "DetailsView.CategoryTextStyle")
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Visibility(this, &SEHBElementEditorPanel::GetEmptyVisibility)
				.Text(LOCTEXT("NoSelectedElement", "\u672a\u9009\u62e9\u5efa\u7b51\u5143\u7d20"))
				.ColorAndOpacity(FStyleColors::Foreground)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SVerticalBox)
				.Visibility(this, &SEHBElementEditorPanel::GetSelectionVisibility)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(this, &SEHBElementEditorPanel::GetSelectedTypeText)
					.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					.ColorAndOpacity(FStyleColors::AccentGreen)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(this, &SEHBElementEditorPanel::GetSelectedActorText)
					.ColorAndOpacity(FStyleColors::Foreground)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(this, &SEHBElementEditorPanel::GetSelectedClassText)
					.ColorAndOpacity(FStyleColors::Foreground)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Visibility(this, &SEHBElementEditorPanel::GetBuildingControlsVisibility)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FStyleColors::Recessed)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BuildingControlsTitle", "\u5efa\u7b51\u64cd\u4f5c"))
							.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("ClearBuildingElementsButton", "\u6e05\u7a7a\u5efa\u7b51\u6240\u6709\u5143\u7d20"))
							.ToolTipText(LOCTEXT("ClearBuildingElementsButtonTip", "\u5220\u9664\u5f53\u524d\u5efa\u7b51\u4e0b\u7684\u5899\u4f53\u3001\u67f1\u5b50\u3001\u95e8\u7a97\u3001\u5c42\u677f\u3001\u697c\u68af\u3001\u6276\u624b\u548c\u5c4b\u9876\u7b49\u6240\u6709\u5143\u7d20\u3002"))
							.IsEnabled(this, &SEHBElementEditorPanel::CanClearSelectedBuildingElements)
							.OnClicked(this, &SEHBElementEditorPanel::HandleClearBuildingElementsClicked)
							.HAlign(HAlign_Center)
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Visibility(this, &SEHBElementEditorPanel::GetRoofCuttingControlsVisibility)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FStyleColors::Recessed)
					[
						SNew(SExpandableArea)
						.InitiallyCollapsed(true)
						.HeaderContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("RoofAdvancedControlsTitle", "\u5c4b\u9876\u9ad8\u7ea7"))
							.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ColorAndOpacity(FStyleColors::Foreground)
						]
						.BodyContent()
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 8.0f, 0.0f, 0.0f)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SEHBElementEditorPanel::IsRoofAccessoryChecked, EEHBRoofAccessoryToggle::Ridge)
								.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandleRoofAccessoryChanged, EEHBRoofAccessoryToggle::Ridge)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("RoofGenerateRidgeLabel", "\u751f\u6210\u5c4b\u810a"))
									.ColorAndOpacity(FStyleColors::Foreground)
								]
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 8.0f, 0.0f, 0.0f)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SEHBElementEditorPanel::IsRoofAccessoryChecked, EEHBRoofAccessoryToggle::Eaves)
								.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandleRoofAccessoryChanged, EEHBRoofAccessoryToggle::Eaves)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("RoofGenerateEavesLabel", "\u751f\u6210\u6a90\u53e3"))
									.ColorAndOpacity(FStyleColors::Foreground)
								]
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 8.0f, 0.0f, 0.0f)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SEHBElementEditorPanel::IsRoofAccessoryChecked, EEHBRoofAccessoryToggle::GableRakes)
								.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandleRoofAccessoryChanged, EEHBRoofAccessoryToggle::GableRakes)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("RoofGenerateGableRakesLabel", "\u751f\u6210\u5c71\u5899\u659c\u8fb9"))
									.ColorAndOpacity(FStyleColors::Foreground)
								]
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 8.0f, 0.0f, 0.0f)
							[
								SNew(SCheckBox)
								.IsChecked(this, &SEHBElementEditorPanel::IsRoofAccessoryChecked, EEHBRoofAccessoryToggle::GableEndWalls)
								.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandleRoofAccessoryChanged, EEHBRoofAccessoryToggle::GableEndWalls)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("RoofGenerateGableEndWallsLabel", "\u751f\u6210\u5c71\u5899\u4fa7\u5899"))
									.ColorAndOpacity(FStyleColors::Foreground)
								]
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 8.0f, 0.0f, 0.0f)
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(SBox)
									.WidthOverride(110.0f)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("RoofGableEndWallBoundaryInsetLabel", "\u4fa7\u5899\u8fb9\u754c\u8ddd\u79bb"))
										.ColorAndOpacity(FStyleColors::Foreground)
									]
								]

								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.Padding(10.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SSpinBox<float>)
									.IsEnabled(this, &SEHBElementEditorPanel::IsRoofGableEndWallBoundaryInsetEnabled)
									.MinValue(0.0f)
									.MaxValue(10000.0f)
									.MinSliderValue(0.0f)
									.MaxSliderValue(300.0f)
									.Delta(1.0f)
									.Value(this, &SEHBElementEditorPanel::GetRoofGableEndWallBoundaryInset)
									.OnValueChanged(this, &SEHBElementEditorPanel::HandleRoofGableEndWallBoundaryInsetChanged)
								]
							]
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Visibility(this, &SEHBElementEditorPanel::GetRoofCuttingControlsVisibility)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FStyleColors::Recessed)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("RoofCuttingControlsTitle", "\u5c4b\u9876\u5207\u5272"))
							.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SEHBElementEditorPanel::IsRoofCutCollidingElementsChecked)
							.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandleRoofCutCollidingElementsChanged)
							.ToolTipText(LOCTEXT("RoofCutCollidingElementsTip", "\u5c4b\u9876\u79fb\u52a8\u7ed3\u675f\u65f6\uff0c\u5982\u679c\u4e0e\u5176\u4ed6\u5143\u7d20\u78b0\u649e\uff0c\u4f1a\u5efa\u7acb\u53cc\u5411\u81ea\u52a8\u5207\u5272\u3002"))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RoofCutCollidingElementsLabel", "\u5207\u5272\u78b0\u649e\u5143\u7d20"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SEHBElementEditorPanel::IsRoofRemoveDisconnectedCutPiecesChecked)
							.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandleRoofRemoveDisconnectedCutPiecesChanged)
							.ToolTipText(LOCTEXT("RoofRemoveDisconnectedCutPiecesTip", "\u5c4b\u9876\u88ab\u5207\u6210\u591a\u4e2a\u4e0d\u8fde\u7eed\u90e8\u5206\u65f6\uff0c\u53ea\u4fdd\u7559\u4e00\u6bb5\u3002"))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RoofRemoveDisconnectedCutPiecesLabel", "\u5220\u9664\u5206\u79bb\u5207\u5272\u6bb5"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SCheckBox)
							.IsEnabled(this, &SEHBElementEditorPanel::IsRoofKeepCutAwayDisconnectedPieceEnabled)
							.IsChecked(this, &SEHBElementEditorPanel::IsRoofKeepCutAwayDisconnectedPieceChecked)
							.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandleRoofKeepCutAwayDisconnectedPieceChanged)
							.ToolTipText(LOCTEXT("RoofKeepCutAwayDisconnectedPieceTip", "\u5f00\u542f\u540e\u4fdd\u7559\u88ab\u5207\u4e0b\u7684\u975e\u4e3b\u4f53\u7247\u6bb5\uff0c\u5173\u95ed\u65f6\u4fdd\u7559\u9762\u79ef\u6700\u5927\u7684\u4e3b\u4f53\u7247\u6bb5\u3002"))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RoofKeepCutAwayDisconnectedPieceLabel", "\u4fdd\u7559\u88ab\u5207\u4e0b\u90e8\u5206"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Visibility(this, &SEHBElementEditorPanel::GetWallControlsVisibility)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FStyleColors::Recessed)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("WallBasicControlsTitle", "\u5899\u4f53\u57fa\u7840\u53c2\u6570"))
							.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 10.0f, 0.0f, 0.0f)
						[
							SNew(SBorder)
							.Padding(8.0f)
							.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
							[
								SNew(SVerticalBox)

								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									SNew(STextBlock)
									.Text(LOCTEXT("WallLengthEditorTitle", "\u7f16\u8f91\u5899\u58c1\u957f\u5ea6"))
									.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
									.ColorAndOpacity(FStyleColors::Foreground)
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 8.0f, 0.0f, 0.0f)
								[
									SNew(SHorizontalBox)

									+ SHorizontalBox::Slot()
									.AutoWidth()
									.VAlign(VAlign_Center)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("WallTargetLengthLabel", "\u76ee\u6807\u957f\u5ea6 (cm)"))
										.ColorAndOpacity(FStyleColors::Foreground)
									]

									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.Padding(10.0f, 0.0f, 0.0f, 0.0f)
									[
										SNew(SSpinBox<float>)
										.MinValue(1.0f)
										.MaxValue(100000.0f)
										.MinSliderValue(1.0f)
										.MaxSliderValue(5000.0f)
										.Delta(1.0f)
										.Value(this, &SEHBElementEditorPanel::GetWallTargetLength)
										.OnValueChanged(this, &SEHBElementEditorPanel::HandleWallTargetLengthChanged)
										.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
										{
											HandleWallTargetLengthChanged(NewValue);
										})
									]
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								.Padding(0.0f, 8.0f, 0.0f, 0.0f)
								[
									SNew(SHorizontalBox)

									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.Padding(0.0f, 0.0f, 4.0f, 0.0f)
									[
										SNew(SButton)
										.IsEnabled(this, &SEHBElementEditorPanel::CanMoveWallLengthPillar)
										.Text(LOCTEXT("MoveLeftWallPillarButton", "\u79fb\u52a8\u5de6\u67f1\u5b50"))
										.HAlign(HAlign_Center)
										.OnClicked(this, &SEHBElementEditorPanel::HandleMoveWallLengthPillarClicked, true)
									]

									+ SHorizontalBox::Slot()
									.FillWidth(1.0f)
									.Padding(4.0f, 0.0f, 0.0f, 0.0f)
									[
										SNew(SButton)
										.IsEnabled(this, &SEHBElementEditorPanel::CanMoveWallLengthPillar)
										.Text(LOCTEXT("MoveRightWallPillarButton", "\u79fb\u52a8\u53f3\u67f1\u5b50"))
										.HAlign(HAlign_Center)
										.OnClicked(this, &SEHBElementEditorPanel::HandleMoveWallLengthPillarClicked, false)
									]
								]
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("WallHeightLabel", "\u5899\u9ad8"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(1.0f)
								.MaxValue(20000.0f)
								.MinSliderValue(1.0f)
								.MaxSliderValue(1000.0f)
								.Delta(1.0f)
								.Value(this, &SEHBElementEditorPanel::GetWallHeight)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									ApplyWallBasicField(EEHBWallBasicField::Height, NewValue);
								})
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
								{
									ApplyWallBasicField(EEHBWallBasicField::Height, NewValue);
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("WallThicknessLabel", "\u5899\u539a"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(1.0f)
								.MaxValue(5000.0f)
								.MinSliderValue(1.0f)
								.MaxSliderValue(200.0f)
								.Delta(1.0f)
								.Value(this, &SEHBElementEditorPanel::GetWallThickness)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									ApplyWallBasicField(EEHBWallBasicField::Thickness, NewValue);
								})
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
								{
									ApplyWallBasicField(EEHBWallBasicField::Thickness, NewValue);
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("WallCurveControlOffsetLabel", "\u66f2\u5899\u504f\u79fb"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(-5000.0f)
								.MaxValue(5000.0f)
								.MinSliderValue(-1000.0f)
								.MaxSliderValue(1000.0f)
								.Delta(1.0f)
								.Value(this, &SEHBElementEditorPanel::GetWallCurveControlOffset)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									ApplyWallBasicField(EEHBWallBasicField::CurveControlOffset, NewValue);
								})
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
								{
									ApplyWallBasicField(EEHBWallBasicField::CurveControlOffset, NewValue);
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("WallCurveSegmentLengthLabel", "\u5206\u6bb5\u957f\u5ea6"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(10.0f)
								.MaxValue(5000.0f)
								.MinSliderValue(10.0f)
								.MaxSliderValue(300.0f)
								.Delta(1.0f)
								.Value(this, &SEHBElementEditorPanel::GetWallCurveSegmentLength)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									ApplyWallBasicField(EEHBWallBasicField::CurveSegmentLength, NewValue);
								})
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
								{
									ApplyWallBasicField(EEHBWallBasicField::CurveSegmentLength, NewValue);
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("LeftWallSampleStartOffsetLabel", "左墙采样偏移"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(0.0f)
								.MaxValue(1.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(1.0f)
								.Delta(0.01f)
								.IsEnabled(this, &SEHBElementEditorPanel::IsLeftWallSampleStartOffsetEnabled)
								.Value(this, &SEHBElementEditorPanel::GetLeftWallSampleStartOffset)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									HandleWallSampleStartOffsetChanging(true, NewValue);
								})
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
								{
									ApplyWallSampleStartOffset(true, NewValue);
								})
								.OnEndSliderMovement_Lambda([this](float NewValue)
								{
									ApplyWallSampleStartOffset(true, NewValue);
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RightWallSampleStartOffsetLabel", "右墙采样偏移"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(0.0f)
								.MaxValue(1.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(1.0f)
								.Delta(0.01f)
								.IsEnabled(this, &SEHBElementEditorPanel::IsRightWallSampleStartOffsetEnabled)
								.Value(this, &SEHBElementEditorPanel::GetRightWallSampleStartOffset)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									HandleWallSampleStartOffsetChanging(false, NewValue);
								})
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
								{
									ApplyWallSampleStartOffset(false, NewValue);
								})
								.OnEndSliderMovement_Lambda([this](float NewValue)
								{
									ApplyWallSampleStartOffset(false, NewValue);
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SEHBElementEditorPanel::IsWallLinkedPillarEndCapsChecked)
							.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandleWallLinkedPillarEndCapsChanged)
							.ToolTipText(LOCTEXT("WallLinkedPillarEndCapsTip", "\u53d6\u6d88\u540e\uff0c\u5899\u4f53\u8fde\u63a5\u67f1\u5b50\u7684\u7aef\u9762\u4e0d\u751f\u6210\u5c01\u8fb9\u3002"))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("WallLinkedPillarEndCapsLabel", "\u751f\u6210\u8fde\u63a5\u5c01\u8fb9"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Visibility(this, &SEHBElementEditorPanel::GetWindowControlsVisibility)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FStyleColors::Recessed)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("WindowBasicControlsTitle", "\u7a97\u6237\u57fa\u7840\u53c2\u6570"))
							.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("WindowSillHeightLabel", "\u79bb\u5730\u9ad8\u5ea6"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(0.0f)
								.MaxValue(20000.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(300.0f)
								.Delta(1.0f)
								.Value(this, &SEHBElementEditorPanel::GetWindowSillHeight)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									ApplyWindowSillHeight(NewValue);
								})
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
								{
									ApplyWindowSillHeight(NewValue);
								})
							]
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Visibility(this, &SEHBElementEditorPanel::GetPillarControlsVisibility)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FStyleColors::Recessed)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("PillarBasicControlsTitle", "\u67f1\u4f53\u57fa\u7840\u53c2\u6570"))
							.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PillarHeightLabel", "\u9ad8\u5ea6"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(1.0f)
								.MaxValue(20000.0f)
								.MinSliderValue(1.0f)
								.MaxSliderValue(1000.0f)
								.Delta(1.0f)
								.Value(this, &SEHBElementEditorPanel::GetPillarHeight)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									ApplyPillarBasicField(EEHBPillarBasicField::Height, NewValue);
								})
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
								{
									ApplyPillarBasicField(EEHBPillarBasicField::Height, NewValue);
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PillarWidthLabel", "\u5bbd\u5ea6"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(1.0f)
								.MaxValue(5000.0f)
								.MinSliderValue(1.0f)
								.MaxSliderValue(300.0f)
								.Delta(1.0f)
								.Value(this, &SEHBElementEditorPanel::GetPillarWidth)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									ApplyPillarBasicField(EEHBPillarBasicField::Width, NewValue);
								})
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
								{
									ApplyPillarBasicField(EEHBPillarBasicField::Width, NewValue);
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PillarDepthLabel", "\u6df1\u5ea6"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(1.0f)
								.MaxValue(5000.0f)
								.MinSliderValue(1.0f)
								.MaxSliderValue(300.0f)
								.Delta(1.0f)
								.Value(this, &SEHBElementEditorPanel::GetPillarDepth)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									ApplyPillarBasicField(EEHBPillarBasicField::Depth, NewValue);
								})
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type)
								{
									ApplyPillarBasicField(EEHBPillarBasicField::Depth, NewValue);
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SEHBElementEditorPanel::IsPillarLinkedWallFacesChecked)
							.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandlePillarLinkedWallFacesChanged)
							.ToolTipText(LOCTEXT("PillarLinkedWallFacesTip", "\u53d6\u6d88\u540e\uff0c\u67f1\u5b50\u4e0e\u5899\u4f53\u8fde\u63a5\u7684\u5c01\u8fb9\u4fa7\u9762\u4e0d\u751f\u6210\u3002"))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PillarLinkedWallFacesLabel", "\u751f\u6210\u8fde\u63a5\u5c01\u8fb9"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					BuildStairControls()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					BuildRailingControls()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SBorder)
					.Visibility(this, &SEHBElementEditorPanel::GetDiagnosticsVisibility)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FStyleColors::Recessed)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ElementDiagnosticsTitle", "\u5143\u7d20\u8bca\u65ad"))
							.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SEHBElementEditorPanel::GetElementIdentityText)
							.AutoWrapText(true)
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SEHBElementEditorPanel::GetRelationSummaryText)
							.AutoWrapText(true)
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SEHBElementEditorPanel::GetGeneratedMeshStatsText)
							.AutoWrapText(true)
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(this, &SEHBElementEditorPanel::GetSampleSourceSummaryText)
							.AutoWrapText(true)
							.ColorAndOpacity(FStyleColors::Foreground)
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[BuildWallOpeningControls()]
					+ SVerticalBox::Slot().AutoHeight()[BuildIndependentRegionControls()]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)
				[
					SNew(SBorder)
					.Visibility(this, &SEHBElementEditorPanel::GetFloorSlabControlsVisibility)
					.Padding(8.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FStyleColors::Recessed)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FloorSlabControlsTitle", "\u5c42\u677f/\u5730\u57fa\u53c2\u6570"))
							.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SVerticalBox)
							.Visibility(this, &SEHBElementEditorPanel::GetFloorSlabCornerControlsVisibility)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("FloorSlabSelectedCornerTitle", "\u5f53\u524d\u89d2\u70b9"))
								.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("FloorSlabPreviousCornerDistanceLabel", "\u8ddd\u4e0a\u4e00\u4e2a\u70b9"))
									.ColorAndOpacity(FStyleColors::Foreground)
								]

								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.Padding(10.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SSpinBox<float>)
									.MinValue(1.0f)
									.MaxValue(100000.0f)
									.MinSliderValue(1.0f)
									.MaxSliderValue(5000.0f)
									.Delta(1.0f)
									.Value(this, &SEHBElementEditorPanel::GetFloorSlabPreviousCornerDistance)
									.OnValueCommitted(this, &SEHBElementEditorPanel::HandleFloorSlabCornerDistanceCommitted, true)
									.ToolTipText(LOCTEXT("FloorSlabPreviousCornerDistanceTip", "\u5355\u4f4d\u4e3a\u5398\u7c73\u3002\u63d0\u4ea4\u540e\u4f1a\u6cbf\u5f53\u524d\u65b9\u5411\u8c03\u6574\u89d2\u70b9\u4e0e\u4e0a\u4e00\u4e2a\u70b9\u7684\u8ddd\u79bb\u3002"))
								]
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(SHorizontalBox)

								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("FloorSlabNextCornerDistanceLabel", "\u8ddd\u4e0b\u4e00\u4e2a\u70b9"))
									.ColorAndOpacity(FStyleColors::Foreground)
								]

								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.Padding(10.0f, 0.0f, 0.0f, 0.0f)
								[
									SNew(SSpinBox<float>)
									.MinValue(1.0f)
									.MaxValue(100000.0f)
									.MinSliderValue(1.0f)
									.MaxSliderValue(5000.0f)
									.Delta(1.0f)
									.Value(this, &SEHBElementEditorPanel::GetFloorSlabNextCornerDistance)
									.OnValueCommitted(this, &SEHBElementEditorPanel::HandleFloorSlabCornerDistanceCommitted, false)
									.ToolTipText(LOCTEXT("FloorSlabNextCornerDistanceTip", "\u5355\u4f4d\u4e3a\u5398\u7c73\u3002\u63d0\u4ea4\u540e\u4f1a\u6cbf\u5f53\u524d\u65b9\u5411\u8c03\u6574\u89d2\u70b9\u4e0e\u4e0b\u4e00\u4e2a\u70b9\u7684\u8ddd\u79bb\u3002"))
								]
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("FloorSlabVisualExpansionLabel", "\u53ef\u89c6\u6269\u8fb9"))
								.ToolTipText(LOCTEXT("FloorSlabVisualExpansionTip", "\u53ea\u6269\u5927\u663e\u793a\u7f51\u683c\u7684\u5916\u8f6e\u5ed3\uff0c\u4e0d\u4f1a\u6539\u53d8\u771f\u5b9e\u63a7\u5236\u70b9\u6216\u4fdd\u5b58\u8f6e\u5ed3\u3002"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(10.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(0.0f)
								.MaxValue(2000.0f)
								.MinSliderValue(0.0f)
								.MaxSliderValue(300.0f)
								.Delta(1.0f)
								.Value(this, &SEHBElementEditorPanel::GetFloorSlabVisualExpansion)
								.OnValueChanged(this, &SEHBElementEditorPanel::HandleFloorSlabVisualExpansionChanged)
								.ToolTipText(LOCTEXT("FloorSlabVisualExpansionSpinTip", "\u5355\u4f4d\u4e3a\u5398\u7c73\u3002\u8bbe\u4e3a 0 \u65f6\u6309\u539f\u59cb\u8f6e\u5ed3\u663e\u793a\u3002"))
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(SCheckBox)
							.IsChecked(this, &SEHBElementEditorPanel::IsFloorSlabAdjacentSnapChecked)
							.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandleFloorSlabAdjacentSnapChanged)
							.ToolTipText(LOCTEXT("FloorSlabAdjacentSnapTip", "\u79fb\u52a8\u5c42\u677f/\u5730\u57fa\u540e\uff0c\u82e5\u4e0e\u5176\u5b83\u5c42\u677f/\u5730\u57fa\u4fa7\u8fb9\u63a5\u8fd1\u6216\u91cd\u53e0\uff0c\u81ea\u52a8\u8fdb\u884c\u4fa7\u8fb9\u5438\u9644\uff1b\u89d2\u70b9\u63a5\u8fd1\u65f6\u4f1a\u540c\u65f6\u65cb\u8f6c\u5bf9\u9f50\u3002"))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("FloorSlabAdjacentSnapLabel", "\u542f\u7528\u4fa7\u8fb9/\u89d2\u70b9\u5438\u9644"))
								.ColorAndOpacity(FStyleColors::Foreground)
							]
						]
					]
				]
			]
		]
		]
		]
	];

	RefreshFloorVisibilityControls();
}

SEHBElementEditorPanel::~SEHBElementEditorPanel()
{
	ResetFloorVisibilityFilter();
}

void SEHBElementEditorPanel::SetSelectedActor(AActor* InSelectedActor)
{
	AEHBBuildingActorBase* PreviousBuilding = FloorVisibilityBuilding.Get();
	AActor* PreviousActor = SelectedActor.Get();
	SelectedActor = InSelectedActor;
	if (PreviousActor != InSelectedActor)
	{
		PendingWallTargetLength.Reset();
		SelectedWallOpening.Invalidate();
		SelectedWallOpeningSource.Reset();
		WallOpeningFeedback = FText::GetEmpty();
	}
	if (SelectedFloorSlabCornerOwner.Get() != InSelectedActor)
	{
		SelectedFloorSlabCornerOwner.Reset();
		SelectedFloorSlabCornerLoopIndex = INDEX_NONE;
		SelectedFloorSlabCornerPointIndex = INDEX_NONE;
	}
	PendingLeftWallSampleStartOffset.Reset();
	PendingRightWallSampleStartOffset.Reset();

	AEHBBuildingActorBase* CurrentBuilding = GetSelectedBuildingMutable();
	const bool bBuildingChanged = PreviousBuilding != CurrentBuilding;
	if (PreviousBuilding && bBuildingChanged)
	{
		ResetFloorVisibilityFilter();
	}

	FloorVisibilityBuilding = CurrentBuilding;
	if (bBuildingChanged)
	{
		RefreshFloorVisibilityControls();
	}
	if (VisibleFloorLimit.IsSet())
	{
		ApplyFloorVisibilityFilter();
	}
}

TSharedRef<SWidget> SEHBElementEditorPanel::BuildFloorVisibilityControls()
{
	return SNew(SBorder)
		.Visibility(this, &SEHBElementEditorPanel::GetFloorVisibilityControlsVisibility)
		.Padding(8.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FStyleColors::Recessed)
		[
			SNew(SBox)
			.WidthOverride(110.0f)
			[
				SAssignNew(FloorVisibilityButtonsBox, SVerticalBox)
			]
		];
}

TSharedRef<SWidget> SEHBElementEditorPanel::BuildRailingControls()
{
	auto BuildNumericRow = [this](
		const FText& Label,
		const TAttribute<float>& Value,
		float MinValue,
		float MaxValue,
		float MinSliderValue,
		float MaxSliderValue,
		float Delta,
		EEHBRailingBasicField Field)
	{
		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.ColorAndOpacity(FStyleColors::Foreground)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SSpinBox<float>)
				.MinValue(MinValue)
				.MaxValue(MaxValue)
				.MinSliderValue(MinSliderValue)
				.MaxSliderValue(MaxSliderValue)
				.Delta(Delta)
				.Value(Value)
				.OnValueChanged_Lambda([this, Field](float NewValue)
				{
					ApplyRailingBasicField(Field, NewValue);
				})
				.OnValueCommitted_Lambda([this, Field](float NewValue, ETextCommit::Type)
				{
					ApplyRailingBasicField(Field, NewValue);
				})
			];
	};

	return SNew(SBorder)
		.Visibility(this, &SEHBElementEditorPanel::GetRailingControlsVisibility)
		.Padding(8.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FStyleColors::Recessed)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RailingBasicControlsTitle", "\u6276\u624b\u57fa\u7840\u53c2\u6570"))
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.ColorAndOpacity(FStyleColors::Foreground)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildNumericRow(
					LOCTEXT("RailingRailTopHeightLabel", "\u6276\u624b\u9ad8\u5ea6\uff08\u6a2a\u6746\u9876\u9762\uff09"),
					TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SEHBElementEditorPanel::GetRailingRailTopHeight)),
					1.0f,
					20000.0f,
					1.0f,
					1000.0f,
					1.0f,
					EEHBRailingBasicField::RailTopHeight)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildNumericRow(
					LOCTEXT("RailingPostSpacingLabel", "\u67f1\u95f4\u8ddd"),
					TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SEHBElementEditorPanel::GetRailingPostSpacing)),
					1.0f,
					20000.0f,
					1.0f,
					500.0f,
					1.0f,
					EEHBRailingBasicField::PostSpacing)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildNumericRow(
					LOCTEXT("RailingPostWidthLabel", "\u7acb\u67f1\u5bbd\u5ea6"),
					TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SEHBElementEditorPanel::GetRailingPostWidth)),
					0.1f,
					5000.0f,
					0.1f,
					200.0f,
					1.0f,
					EEHBRailingBasicField::PostWidth)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildNumericRow(
					LOCTEXT("RailingPostHeightLabel", "\u7acb\u67f1\u9ad8\u5ea6"),
					TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SEHBElementEditorPanel::GetRailingPostHeight)),
					1.0f,
					20000.0f,
					1.0f,
					1000.0f,
					1.0f,
					EEHBRailingBasicField::PostHeight)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildNumericRow(
					LOCTEXT("RailingRailThicknessLabel", "\u6a2a\u6746\u539a\u5ea6"),
					TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SEHBElementEditorPanel::GetRailingRailThickness)),
					0.1f,
					5000.0f,
					0.1f,
					200.0f,
					1.0f,
					EEHBRailingBasicField::RailThickness)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildNumericRow(
					LOCTEXT("RailingMaxRailSegmentLengthLabel", "\u6a2a\u6746\u6700\u5927\u5206\u6bb5"),
					TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SEHBElementEditorPanel::GetRailingMaxRailSegmentLength)),
					1.0f,
					20000.0f,
					1.0f,
					1000.0f,
					1.0f,
					EEHBRailingBasicField::MaxRailSegmentLength)
			]
		];
}

TSharedRef<SWidget> SEHBElementEditorPanel::BuildStairControls()
{
	auto BuildNumericRow = [this](
		const FText& Label,
		EEHBStairNumericField Field,
		float MinValue,
		float MaxValue,
		float MinSliderValue,
		float MaxSliderValue,
		float Delta)
	{
		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(116.0f)
				[
					SNew(STextBlock)
					.Text(Label)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(10.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SSpinBox<float>)
				.MinValue(MinValue)
				.MaxValue(MaxValue)
				.MinSliderValue(MinSliderValue)
				.MaxSliderValue(MaxSliderValue)
				.Delta(Delta)
				.Value(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SEHBElementEditorPanel::GetStairNumericField, Field)))
				.OnValueChanged_Lambda([this, Field](float NewValue)
				{
					ApplyStairNumericField(Field, NewValue);
				})
				.OnValueCommitted_Lambda([this, Field](float NewValue, ETextCommit::Type)
				{
					ApplyStairNumericField(Field, NewValue);
				})
			];
	};

	auto BuildToggleRow = [this](const FText& Label, EEHBStairToggleField Field)
	{
		return SNew(SCheckBox)
			.IsChecked(this, &SEHBElementEditorPanel::IsStairToggleChecked, Field)
			.OnCheckStateChanged(this, &SEHBElementEditorPanel::HandleStairToggleChanged, Field)
			[
				SNew(STextBlock)
				.Text(Label)
				.ColorAndOpacity(FStyleColors::Foreground)
			];
	};

	return SNew(SBorder)
		.Visibility(this, &SEHBElementEditorPanel::GetStairControlsVisibility)
		.Padding(8.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FStyleColors::Recessed)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StairBasicControlsTitle", "楼梯基础参数"))
				.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
				.ColorAndOpacity(FStyleColors::Foreground)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildToggleRow(LOCTEXT("StairUseActualDimensionsLabel", "使用当前尺寸"), EEHBStairToggleField::UseActualDimensions)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildNumericRow(LOCTEXT("StairTreadDepthLabel", "踏板深度"), EEHBStairNumericField::TreadDepth, 1.0f, 20000.0f, 1.0f, 300.0f, 1.0f)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildNumericRow(LOCTEXT("StairWidthLabel", "楼梯宽度"), EEHBStairNumericField::StairWidth, 1.0f, 20000.0f, 1.0f, 1000.0f, 1.0f)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildNumericRow(LOCTEXT("StairHeightLabel", "楼梯高度"), EEHBStairNumericField::StairHeight, 1.0f, 20000.0f, 1.0f, 1000.0f, 1.0f)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StairGenerationControlsTitle", "踏步与侧板"))
					.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					.ColorAndOpacity(FStyleColors::Foreground)
				]
				.BodyContent()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildToggleRow(LOCTEXT("StairGenerateTreadsLabel", "生成踏板"), EEHBStairToggleField::GenerateTreads)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildToggleRow(LOCTEXT("StairFillRisersLabel", "填充立板"), EEHBStairToggleField::FillRisers)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildToggleRow(LOCTEXT("StairFillBottomPartLabel", "填充底部"), EEHBStairToggleField::FillBottomPart)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildToggleRow(LOCTEXT("StairGenerateSidesLabel", "生成锯齿侧板"), EEHBStairToggleField::GenerateSides)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildToggleRow(LOCTEXT("StairGenerateSideGuardsLabel", "生成扶手侧挡板"), EEHBStairToggleField::GenerateSideGuards)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairMinStepHeightLabel", "最小步高"), EEHBStairNumericField::MinStepHeight, 1.0f, 1000.0f, 1.0f, 50.0f, 1.0f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairMaxStepHeightLabel", "最大步高"), EEHBStairNumericField::MaxStepHeight, 1.0f, 1000.0f, 1.0f, 50.0f, 1.0f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairPanelThicknessLabel", "面板厚度"), EEHBStairNumericField::PanelThickness, 0.1f, 1000.0f, 0.1f, 50.0f, 0.5f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairNosingLengthLabel", "踏鼻长度"), EEHBStairNumericField::NosingLength, 0.0f, 1000.0f, 0.0f, 80.0f, 0.5f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairSideProtrudingLabel", "侧板前伸"), EEHBStairNumericField::SideProtruding, 0.0f, 1000.0f, 0.0f, 80.0f, 0.5f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairSideThicknessLabel", "侧板厚度"), EEHBStairNumericField::SideThickness, 0.1f, 1000.0f, 0.1f, 80.0f, 0.5f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairSideBoardHeightLabel", "侧板高度"), EEHBStairNumericField::SideBoardHeight, 1.0f, 2000.0f, 1.0f, 200.0f, 1.0f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairSideBoardTopOffsetLabel", "侧板顶偏移"), EEHBStairNumericField::SideBoardTopOffset, 0.0f, 1000.0f, 0.0f, 100.0f, 0.5f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairSideGuardThicknessLabel", "侧挡板厚度"), EEHBStairNumericField::SideGuardThickness, 0.1f, 1000.0f, 0.1f, 80.0f, 0.5f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairSideGuardHeightLabel", "侧挡板高度"), EEHBStairNumericField::SideGuardHeight, 1.0f, 2000.0f, 1.0f, 200.0f, 1.0f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairSideGuardTopOffsetLabel", "侧挡板顶偏移"), EEHBStairNumericField::SideGuardTopOffset, -1000.0f, 1000.0f, -100.0f, 100.0f, 0.5f)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(false)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StairEmbeddedRailingControlsTitle", "内嵌扶手"))
					.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					.ColorAndOpacity(FStyleColors::Foreground)
				]
				.BodyContent()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildToggleRow(LOCTEXT("StairGenerateRailingLabel", "生成扶手"), EEHBStairToggleField::GenerateRailing)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildToggleRow(LOCTEXT("StairGenerateLeftRailingLabel", "左侧扶手"), EEHBStairToggleField::GenerateLeftRailing)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildToggleRow(LOCTEXT("StairGenerateRightRailingLabel", "右侧扶手"), EEHBStairToggleField::GenerateRightRailing)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairRailingStepsPerPostLabel", "柱子步距"), EEHBStairNumericField::RailingStepsPerPost, 1.0f, 100.0f, 1.0f, 10.0f, 1.0f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairRailingEdgeInsetLabel", "柱子边距"), EEHBStairNumericField::RailingEdgeInset, 0.0f, 10000.0f, 0.0f, 300.0f, 1.0f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairRailingPostForwardOffsetLabel", "柱子前后偏移"), EEHBStairNumericField::RailingPostForwardOffset, -10000.0f, 10000.0f, -150.0f, 150.0f, 1.0f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairRailingPostWidthLabel", "柱子宽度"), EEHBStairNumericField::RailingPostWidth, 0.1f, 1000.0f, 0.1f, 100.0f, 0.5f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairRailingPostHeightLabel", "柱子高度"), EEHBStairNumericField::RailingPostHeight, 1.0f, 5000.0f, 1.0f, 300.0f, 1.0f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairRailingRailHeightLabel", "横杆高度"), EEHBStairNumericField::RailingRailHeight, 1.0f, 5000.0f, 1.0f, 300.0f, 1.0f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairRailingRailThicknessLabel", "横杆厚度"), EEHBStairNumericField::RailingRailThickness, 0.1f, 1000.0f, 0.1f, 100.0f, 0.5f)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildNumericRow(LOCTEXT("StairRailingMaxRailSegmentLengthLabel", "横杆分段"), EEHBStairNumericField::RailingMaxRailSegmentLength, 1.0f, 10000.0f, 1.0f, 500.0f, 1.0f)
					]
				]
			]
		];
}

void SEHBElementEditorPanel::RefreshFloorVisibilityControls()
{
	if (!FloorVisibilityButtonsBox.IsValid())
	{
		return;
	}

	FloorVisibilityButtonsBox->ClearChildren();
	FloorVisibilityButtonsBox->AddSlot()
	.AutoHeight()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("FloorVisibilityTitle", "楼层显示"))
		.Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
		.ColorAndOpacity(FStyleColors::Foreground)
	];

	const AEHBBuildingActorBase* Building = GetSelectedBuilding();
	if (!Building)
	{
		FloorVisibilityButtonsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FloorVisibilityNoBuilding", "未选择建筑"))
			.AutoWrapText(true)
			.ColorAndOpacity(FStyleColors::Foreground)
		];
		return;
	}

	auto AddFloorButton = [this](TOptional<int32> FloorIndex, const FText& Label)
	{
		FloorVisibilityButtonsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.IsChecked(this, &SEHBElementEditorPanel::IsFloorVisibilityChecked, FloorIndex)
			.OnCheckStateChanged_Lambda([this, FloorIndex](ECheckBoxState NewState)
			{
				if (NewState == ECheckBoxState::Checked)
				{
					HandleFloorVisibilityClicked(FloorIndex);
				}
			})
			[
				SNew(SBox)
				.WidthOverride(86.0f)
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(Label)
					.Justification(ETextJustify::Center)
				]
			]
		];
	};

	AddFloorButton(TOptional<int32>(), LOCTEXT("FloorVisibilityAll", "全部"));
	TArray<int32> FloorIndices = GetAvailableFloorIndices();
	for (const int32 FloorIndex : FloorIndices)
	{
		if (FloorIndex <= 0)
		{
			continue;
		}

		AddFloorButton(
			FloorIndex,
			FText::Format(LOCTEXT("FloorVisibilityFloorFormat", "{0}楼"), FText::AsNumber(FloorIndex)));
	}

	if (FloorIndices.IsEmpty())
	{
		FloorVisibilityButtonsBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("FloorVisibilityNoFloors", "未检测到楼层"))
			.AutoWrapText(true)
			.ColorAndOpacity(FStyleColors::Foreground)
		];
	}
}

FReply SEHBElementEditorPanel::HandleFloorVisibilityClicked(TOptional<int32> FloorIndex)
{
	VisibleFloorLimit = FloorIndex;
	ApplyFloorVisibilityFilter();
	RefreshFloorVisibilityControls();
	return FReply::Handled();
}

ECheckBoxState SEHBElementEditorPanel::IsFloorVisibilityChecked(TOptional<int32> FloorIndex) const
{
	if (!VisibleFloorLimit.IsSet() && !FloorIndex.IsSet())
	{
		return ECheckBoxState::Checked;
	}

	return VisibleFloorLimit.IsSet()
		&& FloorIndex.IsSet()
		&& VisibleFloorLimit.GetValue() == FloorIndex.GetValue()
			? ECheckBoxState::Checked
			: ECheckBoxState::Unchecked;
}

void SEHBElementEditorPanel::ApplyFloorVisibilityFilter()
{
#if WITH_EDITOR
	AEHBBuildingActorBase* Building = GetSelectedBuildingMutable();
	if (!Building)
	{
		return;
	}

	FloorVisibilityBuilding = Building;

	FEHBElementQuery Query;
	const TArray<AEHBElementActorBase*> Elements = Building->QueryElements(Query);
	for (AEHBElementActorBase* Element : Elements)
	{
		if (!Element || Element->IsActorBeingDestroyed())
		{
			continue;
		}

		bool bHideElement = false;
		if (VisibleFloorLimit.IsSet())
		{
			const bool bIsRoofElement = IsRoofVisibilityElement(Element);
			if (IsRoofVisibilityIndex(VisibleFloorLimit))
			{
				bHideElement = !bIsRoofElement;
			}
			else if (bIsRoofElement)
			{
				bHideElement = true;
			}
			else
			{
				const int32 LimitFloor = VisibleFloorLimit.GetValue();
				bHideElement = Element->FloorIndex > LimitFloor
					|| (Element->FloorIndex == LimitFloor
						&& Element->FloorRole == EEHBBuildingFloorElementRole::FloorCeiling);
			}
		}

		ApplyFloorVisibilityElementState(Element, bHideElement);
	}

	RedrawEditorViewports();
#endif
}

void SEHBElementEditorPanel::ResetFloorVisibilityFilter()
{
#if WITH_EDITOR
	AEHBBuildingActorBase* Building = FloorVisibilityBuilding.Get();
	if (!Building)
	{
		RestoreAllFloorVisibilityCollisionStates();
		VisibleFloorLimit.Reset();
		return;
	}

	FEHBElementQuery Query;
	const TArray<AEHBElementActorBase*> Elements = Building->QueryElements(Query);
	for (AEHBElementActorBase* Element : Elements)
	{
		if (Element && !Element->IsActorBeingDestroyed())
		{
			Element->SetIsTemporarilyHiddenInEditor(false);
		}
	}
	RestoreAllFloorVisibilityCollisionStates();

	VisibleFloorLimit.Reset();
	RedrawEditorViewports();
#endif
}

void SEHBElementEditorPanel::ApplyFloorVisibilityElementState(AEHBElementActorBase* Element, bool bHideElement)
{
#if WITH_EDITOR
	if (!Element || Element->IsActorBeingDestroyed())
	{
		return;
	}

	Element->SetIsTemporarilyHiddenInEditor(bHideElement);
	if (bHideElement)
	{
		const TWeakObjectPtr<AEHBElementActorBase> ElementKey(Element);
		if (!FloorVisibilityCollisionStates.Contains(ElementKey))
		{
			FloorVisibilityCollisionStates.Add(ElementKey, Element->GetActorEnableCollision());
		}
		Element->SetActorEnableCollision(false);
	}
	else
	{
		RestoreFloorVisibilityCollisionState(Element);
	}
#endif
}

void SEHBElementEditorPanel::RestoreFloorVisibilityCollisionState(AEHBElementActorBase* Element)
{
#if WITH_EDITOR
	if (!Element || Element->IsActorBeingDestroyed())
	{
		return;
	}

	const TWeakObjectPtr<AEHBElementActorBase> ElementKey(Element);
	if (const bool* bWasCollisionEnabled = FloorVisibilityCollisionStates.Find(ElementKey))
	{
		Element->SetActorEnableCollision(*bWasCollisionEnabled);
		FloorVisibilityCollisionStates.Remove(ElementKey);
	}
#endif
}

void SEHBElementEditorPanel::RestoreAllFloorVisibilityCollisionStates()
{
#if WITH_EDITOR
	for (const TPair<TWeakObjectPtr<AEHBElementActorBase>, bool>& Pair : FloorVisibilityCollisionStates)
	{
		AEHBElementActorBase* Element = Pair.Key.Get();
		if (Element && !Element->IsActorBeingDestroyed())
		{
			Element->SetActorEnableCollision(Pair.Value);
		}
	}
	FloorVisibilityCollisionStates.Reset();
#endif
}

bool SEHBElementEditorPanel::CanClearSelectedBuildingElements() const
{
	const AEHBBuildingActorBase* Building = Cast<AEHBBuildingActorBase>(SelectedActor.Get());
	if (!Building)
	{
		return false;
	}

	FEHBElementQuery Query;
	return Building->QueryElements(Query).Num() > 0;
}

FReply SEHBElementEditorPanel::HandleClearBuildingElementsClicked()
{
#if WITH_EDITOR
	AEHBBuildingActorBase* Building = GetSelectedBuildingObjectMutable();
	if (!Building)
	{
		return FReply::Handled();
	}

	FEHBElementQuery Query;
	const int32 ElementCount = Building->QueryElements(Query).Num();
	if (ElementCount <= 0)
	{
		return FReply::Handled();
	}

	const FText ConfirmText = FText::Format(
		LOCTEXT("ClearBuildingElementsConfirm", "\u786e\u8ba4\u5220\u9664\u5efa\u7b51\u201c{0}\u201d\u4e0b\u7684 {1} \u4e2a\u5143\u7d20\uff1f"),
		FText::FromString(Building->GetActorLabel()),
		FText::AsNumber(ElementCount));
	if (FMessageDialog::Open(EAppMsgType::YesNo, ConfirmText) != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("ClearBuildingElementsTransaction", "Clear Building Elements"));
	RestoreAllFloorVisibilityCollisionStates();
	VisibleFloorLimit.Reset();
	FloorVisibilityBuilding.Reset();

	Building->Modify();
	const int32 RemovedCount = Building->ClearAllElements();
	if (RemovedCount > 0)
	{
		SelectedActor = Building;
		if (GEditor)
		{
			GEditor->SelectNone(false, true, false);
			GEditor->SelectActor(Building, true, true, true);
		}
	}

	RefreshFloorVisibilityControls();
	RedrawEditorViewports();
#endif
	return FReply::Handled();
}

TArray<int32> SEHBElementEditorPanel::GetAvailableFloorIndices() const
{
	TArray<int32> FloorIndices;
	const AEHBBuildingActorBase* Building = GetSelectedBuilding();
	if (!Building)
	{
		return FloorIndices;
	}

	FEHBElementQuery Query;
	const TArray<AEHBElementActorBase*> Elements = Building->QueryElements(Query);
	for (const AEHBElementActorBase* Element : Elements)
	{
		if (!Element || IsRoofVisibilityElement(Element) || Element->FloorIndex <= 0)
		{
			continue;
		}

		FloorIndices.AddUnique(Element->FloorIndex);
	}

	FloorIndices.Sort([](int32 A, int32 B)
	{
		return A > B;
	});
	return FloorIndices;
}

FText SEHBElementEditorPanel::GetSelectedTypeText() const
{
	const AActor* Actor = SelectedActor.Get();
	if (!Actor)
	{
		return LOCTEXT("NoType", "\u672a\u9009\u62e9");
	}

	if (Actor->IsA<AEHBBuildingActorBase>())
	{
		return LOCTEXT("BuildingObjectType", "\u5efa\u7b51\u5bf9\u8c61");
	}
	if (Actor->IsA<AEHB_Wall>())
	{
		return LOCTEXT("SelectedWallType", "\u5899\u4f53");
	}
	if (Actor->IsA<AEHB_Pillar>())
	{
		return LOCTEXT("SelectedPillarType", "\u67f1\u4f53");
	}
	if (Actor->IsA<AEHB_DoorWindow>())
	{
		return LOCTEXT("SelectedDoorWindowType", "\u95e8\u7a97");
	}
	if (Actor->IsA<AEHB_FloorSlab>())
	{
		return LOCTEXT("SelectedFloorSlabType", "\u5730\u57fa/\u5c42\u677f");
	}
	if (Actor->IsA<AEHB_Floor>())
	{
		return LOCTEXT("SelectedFloorType", "\u5730\u677f");
	}
	if (Actor->IsA<AEHBGableRoof>())
	{
		return LOCTEXT("SelectedRoofType", "\u5c4b\u9876");
	}
	if (Actor->IsA<AEHB_Stair>())
	{
		return LOCTEXT("SelectedStairType", "\u697c\u68af");
	}

	if (const AEHBElementActorBase* ElementActor = Cast<AEHBElementActorBase>(Actor))
	{
		return GetElementTypeDisplayText(ElementActor->ElementType);
	}

	return LOCTEXT("NonBuildingActorType", "\u975e\u5efa\u7b51\u5143\u7d20");
}

FText SEHBElementEditorPanel::GetSelectedActorText() const
{
	const AActor* Actor = SelectedActor.Get();
	if (!Actor)
	{
		return FText::GetEmpty();
	}

	return FText::Format(
		LOCTEXT("SelectedActorFormat", "\u5bf9\u8c61\uff1a{0}"),
		FText::FromString(Actor->GetActorLabel()));
}

FText SEHBElementEditorPanel::GetSelectedClassText() const
{
	const AActor* Actor = SelectedActor.Get();
	if (!Actor || !Actor->GetClass())
	{
		return FText::GetEmpty();
	}

	return FText::Format(
		LOCTEXT("SelectedClassFormat", "\u7c7b\uff1a{0}"),
		FText::FromString(Actor->GetClass()->GetName()));
}

FText SEHBElementEditorPanel::GetElementIdentityText() const
{
	const AActor* Actor = SelectedActor.Get();
	if (!Actor)
	{
		return FText::GetEmpty();
	}

	if (const AEHBBuildingActorBase* Building = Cast<AEHBBuildingActorBase>(Actor))
	{
		FEHBElementQuery Query;
		const int32 ElementCount = Building->QueryElements(Query).Num();
		return FText::FromString(FString::Printf(
			TEXT("身份：建筑，元素 %d"),
			ElementCount));
	}

	const AEHBElementActorBase* Element = GetSelectedElementActor();
	if (!Element)
	{
		return LOCTEXT("DiagnosticsIdentityNonElement", "身份：非 EHB Actor");
	}

	const FString GuidText = Element->ElementGuid.IsValid()
		? Element->ElementGuid.ToString(EGuidFormats::Digits).Left(8)
		: TEXT("无效");
	const FString ElementName = Element->ElementName.IsNone() ? TEXT("无") : Element->ElementName.ToString();
	const UEnum* FloorRoleEnum = StaticEnum<EEHBBuildingFloorElementRole>();
	const FString FloorRoleName = FloorRoleEnum
		? FloorRoleEnum->GetNameStringByValue(static_cast<int64>(Element->FloorRole))
		: TEXT("未知");

	return FText::FromString(FString::Printf(
		TEXT("身份：Guid %s，楼层 %d，角色 %s，名称 %s，能力 0x%02X"),
		*GuidText,
		Element->FloorIndex,
		*FloorRoleName,
		*ElementName,
		Element->ElementCapabilities));
}

FText SEHBElementEditorPanel::GetRelationSummaryText() const
{
	const AEHBBuildingActorBase* Building = GetSelectedBuilding();
	if (!Building)
	{
		return LOCTEXT("DiagnosticsNoBuilding", "关系：没有所属建筑");
	}

	const AEHBElementActorBase* Element = GetSelectedElementActor();
	FEHBRelationQuery Query;
	Query.bIncludeStale = true;
	Query.Direction = EEHBRelationQueryDirection::Both;
	if (Element && Element->ElementGuid.IsValid())
	{
		Query.ElementGuid = Element->ElementGuid;
	}

	const TArray<FEHBElementRelation> Relations = Building->QueryElementRelations(Query);
	int32 IncomingCount = 0;
	int32 OutgoingCount = 0;
	int32 StaleCount = 0;
	int32 DisabledCount = 0;
	for (const FEHBElementRelation& Relation : Relations)
	{
		if (Element)
		{
			if (Relation.Source.RefersToElement(Element->ElementGuid))
			{
				++OutgoingCount;
			}
			if (Relation.Target.RefersToElement(Element->ElementGuid))
			{
				++IncomingCount;
			}
		}
		if (Building->IsElementRelationStale(Relation.RelationGuid))
		{
			++StaleCount;
		}
		if (!Relation.bEnabled)
		{
			++DisabledCount;
		}
	}

	if (Element)
	{
		return FText::FromString(FString::Printf(
			TEXT("关系：总数 %d，传入 %d，传出 %d，失效 %d，禁用 %d"),
			Relations.Num(),
			IncomingCount,
			OutgoingCount,
			StaleCount,
			DisabledCount));
	}

	return FText::FromString(FString::Printf(
		TEXT("关系：图内总数 %d，失效 %d，禁用 %d"),
		Relations.Num(),
		StaleCount,
		DisabledCount));
}

FText SEHBElementEditorPanel::GetGeneratedMeshStatsText() const
{
	AActor* Actor = SelectedActor.Get();
	if (!Actor)
	{
		return FText::GetEmpty();
	}

	if (AEHBBuildingActorBase* Building = Cast<AEHBBuildingActorBase>(Actor))
	{
		const FEHBBuildingPerformanceReport Report = UEHBBuildingPerformanceAnalyzer::AnalyzeBuilding(
			Building,
			GetConfiguredPerformanceBudget());
		return FText::FromString(UEHBBuildingPerformanceAnalyzer::FormatPerformanceReportSummary(Report));
	}

	const FEHBElementPerformanceEntry Entry = UEHBBuildingPerformanceAnalyzer::AnalyzeActor(Actor);
	if (Entry.GeneratedMeshComponentCount == 0
		&& Entry.StaticMeshComponentCount == 0
		&& Entry.InstancedMeshComponentCount == 0)
	{
		return LOCTEXT("DiagnosticsNoMeshComponents", "网格：没有生成网格或静态网格组件");
	}

	const double BufferKB = static_cast<double>(
		Entry.GeneratedMeshStats.EstimatedVertexBufferBytes + Entry.GeneratedMeshStats.EstimatedIndexBufferBytes) / 1024.0;
	return FText::FromString(FString::Printf(
		TEXT("网格：生成组件 %d，可见分段 %d/%d，顶点 %d，三角面 %d，碰撞三角面 %d，静态组件 %d，实例 %d，采样估算开销 %d，缓冲区 %.1f KB，跳过重复更新 %lld/%lld"),
		Entry.GeneratedMeshComponentCount,
		Entry.GeneratedMeshStats.VisibleSectionCount,
		Entry.GeneratedMeshStats.SectionCount,
		Entry.GeneratedMeshStats.VertexCount,
		Entry.GeneratedMeshStats.TriangleCount,
		Entry.GeneratedMeshStats.CollisionTriangleCount,
		Entry.StaticMeshComponentCount,
		Entry.InstancedMeshInstanceCount,
		Entry.SampleEstimatedCost,
		BufferKB,
		static_cast<long long>(Entry.GeneratedMeshStats.SkippedIdenticalSectionUpdateCount),
		static_cast<long long>(Entry.GeneratedMeshStats.SubmittedSectionUpdateCount)));
}

FText SEHBElementEditorPanel::GetSampleSourceSummaryText() const
{
	const AActor* Actor = SelectedActor.Get();
	if (!Actor)
	{
		return FText::GetEmpty();
	}

	if (const AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor))
	{
		return FText::FromString(FString::Printf(
			TEXT("采样：%s；%s"),
			*DescribeWallSurfaceStyle(TEXT("左墙"), Wall->LeftSurfaceStyle),
			*DescribeWallSurfaceStyle(TEXT("右墙"), Wall->RightSurfaceStyle)));
	}

	if (const AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor))
	{
		const FDataTableRowHandle& RowHandle = Pillar->SampledPillarRow;
		if (Pillar->ShapeType == EEHBPillarShapeType::StaticMesh
			&& RowHandle.DataTable
			&& !RowHandle.RowName.IsNone()
			&& RowHandle.DataTable->GetRowStruct() == FEHBPillarMeshData::StaticStruct())
		{
			if (const FEHBPillarMeshData* Row = RowHandle.DataTable->FindRow<FEHBPillarMeshData>(
				RowHandle.RowName,
				TEXT("SEHBElementEditorPanel::GetSampleSourceSummaryText.Pillar"),
				false))
			{
				return FText::FromString(FString::Printf(
					TEXT("采样：柱体 %s，%s"),
					*FormatRowHandleName(RowHandle),
					*FormatSampleMetadata(Row->TemplateMetadata)));
			}
		}

		const FString SourceMesh = FormatObjectPath(Pillar->SourceStaticMesh.ToSoftObjectPath());
		return FText::FromString(FString::Printf(TEXT("采样：柱体程序化/静态网格 %s"), *SourceMesh));
	}

	if (const AEHBGableRoof* GableRoof = Cast<AEHBGableRoof>(Actor))
	{
		return FText::FromString(FString::Printf(
			TEXT("山形屋顶：%.1f x %.1f，坡度 %.1f，厚度 %.1f"),
			GableRoof->Length,
			GableRoof->Width,
			GableRoof->PitchDegrees,
			GableRoof->Thickness));
	}


	if (const AEHB_DoorWindow* DoorWindow = Cast<AEHB_DoorWindow>(Actor))
	{
		const UEnum* KindEnum = StaticEnum<EEHBDoorWindowElementKind>();
		const FString KindName = KindEnum
			? KindEnum->GetNameStringByValue(static_cast<int64>(DoorWindow->Kind))
			: TEXT("未知");
		return FText::FromString(FString::Printf(
			TEXT("采样：门窗 %s，网格 %s，洞口 %.1f x %.1f x %.1f，窗台 %.1f"),
			*KindName,
			*FormatObjectPath(DoorWindow->SourceStaticMesh.ToSoftObjectPath()),
			DoorWindow->OpeningWidth,
			DoorWindow->OpeningHeight,
			DoorWindow->OpeningThickness,
			DoorWindow->SillHeight));
	}

	if (const AEHB_Railing* Railing = Cast<AEHB_Railing>(Actor))
	{
		const FDataTableRowHandle& RowHandle = Railing->SampledRailingRow;
		if (RowHandle.DataTable
			&& !RowHandle.RowName.IsNone()
			&& RowHandle.DataTable->GetRowStruct() == FEHBRailingMeshData::StaticStruct())
		{
			if (const FEHBRailingMeshData* Row = RowHandle.DataTable->FindRow<FEHBRailingMeshData>(
				RowHandle.RowName,
				TEXT("SEHBElementEditorPanel::GetSampleSourceSummaryText.Railing"),
				false))
			{
				return FText::FromString(FString::Printf(
					TEXT("采样：扶手 %s，长度 %.1f，立柱 %d，门 %d，%s"),
					*FormatRowHandleName(RowHandle),
					Railing->GetRailingLength(),
					Railing->GeneratedPosts.Num(),
					Railing->GateConnections.Num(),
					*FormatSampleMetadata(Row->TemplateMetadata)));
			}
		}

		return FText::FromString(FString::Printf(
			TEXT("采样：扶手长度 %.1f，立柱 %d，门 %d，立柱网格 %s，横杆网格 %s"),
			Railing->GetRailingLength(),
			Railing->GeneratedPosts.Num(),
			Railing->GateConnections.Num(),
			*FormatObjectPath(Railing->PostMesh.ToSoftObjectPath()),
			*FormatObjectPath(Railing->RailMesh.ToSoftObjectPath())));
	}

	if (Actor->IsA<AEHBBuildingActorBase>())
	{
		return LOCTEXT("DiagnosticsBuildingSample", "采样：请选择一个元素以查看模板来源");
	}

	return LOCTEXT("DiagnosticsNoSample", "采样：没有 EHB 采样来源");
}

EVisibility SEHBElementEditorPanel::GetDiagnosticsVisibility() const
{
	const AActor* Actor = SelectedActor.Get();
	return (GetSelectedElementActor() || (Actor && Actor->IsA<AEHBBuildingActorBase>()))
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetFloorVisibilityControlsVisibility() const
{
	return GetSelectedBuilding() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetBuildingControlsVisibility() const
{
	return Cast<AEHBBuildingActorBase>(SelectedActor.Get()) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetRoofCuttingControlsVisibility() const
{
	return GetSelectedRoof() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetWallControlsVisibility() const
{
	return GetSelectedWall() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetPillarControlsVisibility() const
{
	return GetSelectedPillar() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetWindowControlsVisibility() const
{
	const AEHB_DoorWindow* DoorWindow = GetSelectedDoorWindow();
	return DoorWindow && DoorWindow->Kind == EEHBDoorWindowElementKind::Window
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetRailingControlsVisibility() const
{
	return GetSelectedRailing() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetStairControlsVisibility() const
{
	return GetSelectedStair() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetFloorSlabControlsVisibility() const
{
	return GetSelectedFloorSlab() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetFloorSlabCornerControlsVisibility() const
{
	const AEHB_FloorSlab* FloorSlab = SelectedFloorSlabCornerOwner.Get();
	TArray<FVector> Loop;
	return FloorSlab
		&& FloorSlab == GetSelectedFloorSlab()
		&& FloorSlab->GetEditableLoopCopy(SelectedFloorSlabCornerLoopIndex, Loop)
		&& Loop.Num() >= 3
		&& Loop.IsValidIndex(SelectedFloorSlabCornerPointIndex)
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

float SEHBElementEditorPanel::GetWallHeight() const
{
	const AEHB_Wall* Wall = GetSelectedWall();
	return Wall ? FMath::Max(1.0f, Wall->Height) : 1.0f;
}

float SEHBElementEditorPanel::GetWallThickness() const
{
	const AEHB_Wall* Wall = GetSelectedWall();
	return Wall ? FMath::Max(1.0f, Wall->Thickness) : 1.0f;
}

float SEHBElementEditorPanel::GetWallCurveControlOffset() const
{
	const AEHB_Wall* Wall = GetSelectedWall();
	return Wall ? Wall->CurveControlOffset : 0.0f;
}

float SEHBElementEditorPanel::GetWallCurveSegmentLength() const
{
	const AEHB_Wall* Wall = GetSelectedWall();
	return Wall ? FMath::Max(10.0f, Wall->CurveSegmentLength) : 50.0f;
}

float SEHBElementEditorPanel::GetWallTargetLength() const
{
	if (PendingWallTargetLength.IsSet())
	{
		return PendingWallTargetLength.GetValue();
	}

	const AEHB_Wall* Wall = GetSelectedWall();
	return Wall ? FMath::Max(1.0f, FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd)) : 1.0f;
}

bool SEHBElementEditorPanel::CanMoveWallLengthPillar() const
{
	const AEHB_Wall* Wall = GetSelectedWall();
	const AEHBBuildingActorBase* Building = Wall ? Wall->OwningBuilding.Get() : nullptr;
	return Building
		&& Cast<AEHB_Pillar>(Building->FindElementActorByGuid(Wall->StartPillarGuid))
		&& Cast<AEHB_Pillar>(Building->FindElementActorByGuid(Wall->EndPillarGuid));
}

float SEHBElementEditorPanel::GetLeftWallSampleStartOffset() const
{
	if (PendingLeftWallSampleStartOffset.IsSet())
	{
		return PendingLeftWallSampleStartOffset.GetValue();
	}

	const AEHB_Wall* Wall = GetSelectedWall();
	return Wall ? FMath::Clamp(Wall->LeftSurfaceStyle.SampleStartOffset, 0.0f, 1.0f) : 0.0f;
}

float SEHBElementEditorPanel::GetRightWallSampleStartOffset() const
{
	if (PendingRightWallSampleStartOffset.IsSet())
	{
		return PendingRightWallSampleStartOffset.GetValue();
	}

	const AEHB_Wall* Wall = GetSelectedWall();
	return Wall ? FMath::Clamp(Wall->RightSurfaceStyle.SampleStartOffset, 0.0f, 1.0f) : 0.0f;
}

bool SEHBElementEditorPanel::IsLeftWallSampleStartOffsetEnabled() const
{
	const AEHB_Wall* Wall = GetSelectedWall();
	return Wall && IsSampledWallSurfaceStyleForEditing(Wall->LeftSurfaceStyle);
}

bool SEHBElementEditorPanel::IsRightWallSampleStartOffsetEnabled() const
{
	const AEHB_Wall* Wall = GetSelectedWall();
	return Wall && IsSampledWallSurfaceStyleForEditing(Wall->RightSurfaceStyle);
}

float SEHBElementEditorPanel::GetWindowSillHeight() const
{
	const AEHB_DoorWindow* DoorWindow = GetSelectedDoorWindow();
	return DoorWindow && DoorWindow->Kind == EEHBDoorWindowElementKind::Window
		? FMath::Max(0.0f, DoorWindow->SillHeight)
		: 0.0f;
}

float SEHBElementEditorPanel::GetPillarHeight() const
{
	const AEHB_Pillar* Pillar = GetSelectedPillar();
	return Pillar ? FMath::Max(1.0f, Pillar->Height) : 1.0f;
}

float SEHBElementEditorPanel::GetPillarWidth() const
{
	const AEHB_Pillar* Pillar = GetSelectedPillar();
	return Pillar ? FMath::Max(1.0f, Pillar->Width) : 1.0f;
}

float SEHBElementEditorPanel::GetPillarDepth() const
{
	const AEHB_Pillar* Pillar = GetSelectedPillar();
	return Pillar ? FMath::Max(1.0f, Pillar->Depth) : 1.0f;
}

float SEHBElementEditorPanel::GetRailingRailTopHeight() const
{
	const AEHB_Railing* Railing = GetSelectedRailing();
	return Railing ? FMath::Max(1.0f, Railing->GetRailTopHeight()) : 1.0f;
}

float SEHBElementEditorPanel::GetRailingPostSpacing() const
{
	const AEHB_Railing* Railing = GetSelectedRailing();
	return Railing ? FMath::Max(1.0f, Railing->PostSpacing) : 1.0f;
}

float SEHBElementEditorPanel::GetRailingPostWidth() const
{
	const AEHB_Railing* Railing = GetSelectedRailing();
	return Railing ? FMath::Max(0.1f, Railing->PostWidth) : 0.1f;
}

float SEHBElementEditorPanel::GetRailingPostHeight() const
{
	const AEHB_Railing* Railing = GetSelectedRailing();
	return Railing ? FMath::Max(1.0f, Railing->PostHeight) : 1.0f;
}

float SEHBElementEditorPanel::GetRailingRailThickness() const
{
	const AEHB_Railing* Railing = GetSelectedRailing();
	return Railing ? FMath::Max(0.1f, Railing->RailThickness) : 0.1f;
}

float SEHBElementEditorPanel::GetRailingMaxRailSegmentLength() const
{
	const AEHB_Railing* Railing = GetSelectedRailing();
	return Railing ? FMath::Max(1.0f, Railing->MaxRailSegmentLength) : 1.0f;
}

float SEHBElementEditorPanel::GetStairNumericField(EEHBStairNumericField Field) const
{
	const AEHB_Stair* Stair = GetSelectedStair();
	if (!Stair)
	{
		return 0.0f;
	}

	const FEHBStairData& Data = Stair->StairData;
	switch (Field)
	{
	case EEHBStairNumericField::TreadDepth:
		return FMath::Max(1.0f, Data.TreadDepth);
	case EEHBStairNumericField::StairWidth:
		return FMath::Max(1.0f, Data.StairWidth);
	case EEHBStairNumericField::StairHeight:
		return FMath::Max(1.0f, Data.StairHeight);
	case EEHBStairNumericField::MinStepHeight:
		return FMath::Max(1.0f, Data.MinStepHeight);
	case EEHBStairNumericField::MaxStepHeight:
		return FMath::Max(1.0f, Data.MaxStepHeight);
	case EEHBStairNumericField::PanelThickness:
		return FMath::Max(0.1f, Data.PanelThickness);
	case EEHBStairNumericField::NosingLength:
		return FMath::Max(0.0f, Data.NosingLength);
	case EEHBStairNumericField::SideProtruding:
		return FMath::Max(0.0f, Data.SideProtruding);
	case EEHBStairNumericField::SideThickness:
		return FMath::Max(0.1f, Data.SideThickness);
	case EEHBStairNumericField::SideBoardHeight:
		return FMath::Max(1.0f, Data.SideBoardHeight);
	case EEHBStairNumericField::SideBoardTopOffset:
		return FMath::Max(0.0f, Data.SideBoardTopOffset);
	case EEHBStairNumericField::SideGuardThickness:
		return FMath::Max(0.1f, Data.SideGuardThickness);
	case EEHBStairNumericField::SideGuardHeight:
		return FMath::Max(1.0f, Data.SideGuardHeight);
	case EEHBStairNumericField::SideGuardTopOffset:
		return Data.SideGuardTopOffset;
	case EEHBStairNumericField::RailingStepsPerPost:
		return static_cast<float>(FMath::Max(1, Data.RailingStepsPerPost));
	case EEHBStairNumericField::RailingEdgeInset:
		return FMath::Max(0.0f, Data.RailingEdgeInset);
	case EEHBStairNumericField::RailingPostForwardOffset:
		return Data.RailingPostForwardOffset;
	case EEHBStairNumericField::RailingPostWidth:
		return FMath::Max(0.1f, Data.RailingPostWidth);
	case EEHBStairNumericField::RailingPostHeight:
		return FMath::Max(1.0f, Data.RailingPostHeight);
	case EEHBStairNumericField::RailingRailHeight:
		return FMath::Max(1.0f, Data.RailingRailHeight);
	case EEHBStairNumericField::RailingRailThickness:
		return FMath::Max(0.1f, Data.RailingRailThickness);
	case EEHBStairNumericField::RailingMaxRailSegmentLength:
		return FMath::Max(1.0f, Data.RailingMaxRailSegmentLength);
	default:
		return 0.0f;
	}
}

void SEHBElementEditorPanel::SetSelectedFloorSlabCorner(
	AEHB_FloorSlab* InFloorSlab,
	int32 InLoopIndex,
	int32 InPointIndex)
{
	TArray<FVector> Loop;
	if (!InFloorSlab
		|| InFloorSlab != GetSelectedFloorSlab()
		|| !InFloorSlab->GetEditableLoopCopy(InLoopIndex, Loop)
		|| !Loop.IsValidIndex(InPointIndex))
	{
		SelectedFloorSlabCornerOwner.Reset();
		SelectedFloorSlabCornerLoopIndex = INDEX_NONE;
		SelectedFloorSlabCornerPointIndex = INDEX_NONE;
		return;
	}

	SelectedFloorSlabCornerOwner = InFloorSlab;
	SelectedFloorSlabCornerLoopIndex = InLoopIndex;
	SelectedFloorSlabCornerPointIndex = InPointIndex;
}

float SEHBElementEditorPanel::GetFloorSlabVisualExpansion() const
{
	const AEHB_FloorSlab* FloorSlab = GetSelectedFloorSlab();
	return FloorSlab ? FMath::Max(0.0f, FloorSlab->VisualExpansion) : 0.0f;
}

float SEHBElementEditorPanel::GetFloorSlabPreviousCornerDistance() const
{
	const AEHB_FloorSlab* FloorSlab = SelectedFloorSlabCornerOwner.Get();
	TArray<FVector> Loop;
	if (!FloorSlab
		|| FloorSlab != GetSelectedFloorSlab()
		|| !FloorSlab->GetEditableLoopCopy(SelectedFloorSlabCornerLoopIndex, Loop)
		|| Loop.Num() < 3
		|| !Loop.IsValidIndex(SelectedFloorSlabCornerPointIndex))
	{
		return 0.0f;
	}

	const int32 PreviousIndex = (SelectedFloorSlabCornerPointIndex - 1 + Loop.Num()) % Loop.Num();
	return FVector::Dist2D(Loop[SelectedFloorSlabCornerPointIndex], Loop[PreviousIndex]);
}

float SEHBElementEditorPanel::GetFloorSlabNextCornerDistance() const
{
	const AEHB_FloorSlab* FloorSlab = SelectedFloorSlabCornerOwner.Get();
	TArray<FVector> Loop;
	if (!FloorSlab
		|| FloorSlab != GetSelectedFloorSlab()
		|| !FloorSlab->GetEditableLoopCopy(SelectedFloorSlabCornerLoopIndex, Loop)
		|| Loop.Num() < 3
		|| !Loop.IsValidIndex(SelectedFloorSlabCornerPointIndex))
	{
		return 0.0f;
	}

	const int32 NextIndex = (SelectedFloorSlabCornerPointIndex + 1) % Loop.Num();
	return FVector::Dist2D(Loop[SelectedFloorSlabCornerPointIndex], Loop[NextIndex]);
}

ECheckBoxState SEHBElementEditorPanel::IsStairToggleChecked(EEHBStairToggleField Field) const
{
	const AEHB_Stair* Stair = GetSelectedStair();
	if (!Stair)
	{
		return ECheckBoxState::Unchecked;
	}

	const FEHBStairData& Data = Stair->StairData;
	bool bChecked = false;
	switch (Field)
	{
	case EEHBStairToggleField::UseActualDimensions:
		bChecked = Data.bUseActualDimensions;
		break;
	case EEHBStairToggleField::GenerateTreads:
		bChecked = Data.bGenerateTreads;
		break;
	case EEHBStairToggleField::FillRisers:
		bChecked = Data.bFillRisers;
		break;
	case EEHBStairToggleField::FillBottomPart:
		bChecked = Data.bFillBottomPart;
		break;
	case EEHBStairToggleField::GenerateSides:
		bChecked = Data.bGenerateSides;
		break;
	case EEHBStairToggleField::GenerateSideGuards:
		bChecked = Data.bGenerateSideGuards;
		break;
	case EEHBStairToggleField::GenerateRailing:
		bChecked = Data.bGenerateRailing;
		break;
	case EEHBStairToggleField::GenerateLeftRailing:
		bChecked = Data.bGenerateLeftRailing;
		break;
	case EEHBStairToggleField::GenerateRightRailing:
		bChecked = Data.bGenerateRightRailing;
		break;
	default:
		break;
	}

	return bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SEHBElementEditorPanel::HandleStairToggleChanged(ECheckBoxState NewState, EEHBStairToggleField Field)
{
	AEHB_Stair* Stair = GetSelectedStair();
	if (!Stair)
	{
		return;
	}

	bool* TargetFlag = nullptr;
	FText TransactionText = LOCTEXT("ChangeStairToggle", "Change Stair Option");
	switch (Field)
	{
	case EEHBStairToggleField::UseActualDimensions:
		TargetFlag = &Stair->StairData.bUseActualDimensions;
		TransactionText = LOCTEXT("ChangeStairUseActualDimensions", "Change Stair Use Actual Dimensions");
		break;
	case EEHBStairToggleField::GenerateTreads:
		TargetFlag = &Stair->StairData.bGenerateTreads;
		TransactionText = LOCTEXT("ChangeStairGenerateTreads", "Change Stair Generate Treads");
		break;
	case EEHBStairToggleField::FillRisers:
		TargetFlag = &Stair->StairData.bFillRisers;
		TransactionText = LOCTEXT("ChangeStairFillRisers", "Change Stair Fill Risers");
		break;
	case EEHBStairToggleField::FillBottomPart:
		TargetFlag = &Stair->StairData.bFillBottomPart;
		TransactionText = LOCTEXT("ChangeStairFillBottomPart", "Change Stair Fill Bottom Part");
		break;
	case EEHBStairToggleField::GenerateSides:
		TargetFlag = &Stair->StairData.bGenerateSides;
		TransactionText = LOCTEXT("ChangeStairGenerateSides", "Change Stair Generate Sides");
		break;
	case EEHBStairToggleField::GenerateSideGuards:
		TargetFlag = &Stair->StairData.bGenerateSideGuards;
		TransactionText = LOCTEXT("ChangeStairGenerateSideGuards", "Change Stair Generate Side Guards");
		break;
	case EEHBStairToggleField::GenerateRailing:
		TargetFlag = &Stair->StairData.bGenerateRailing;
		TransactionText = LOCTEXT("ChangeStairGenerateRailing", "Change Stair Generate Railing");
		break;
	case EEHBStairToggleField::GenerateLeftRailing:
		TargetFlag = &Stair->StairData.bGenerateLeftRailing;
		TransactionText = LOCTEXT("ChangeStairGenerateLeftRailing", "Change Stair Generate Left Railing");
		break;
	case EEHBStairToggleField::GenerateRightRailing:
		TargetFlag = &Stair->StairData.bGenerateRightRailing;
		TransactionText = LOCTEXT("ChangeStairGenerateRightRailing", "Change Stair Generate Right Railing");
		break;
	default:
		break;
	}

	if (!TargetFlag)
	{
		return;
	}

	const bool bNewEnabled = NewState == ECheckBoxState::Checked;
	if (*TargetFlag == bNewEnabled)
	{
		return;
	}

	const FScopedTransaction Transaction(TransactionText);
	Stair->Modify();
	*TargetFlag = bNewEnabled;
	Stair->RebuildStairMesh();
	Stair->NotifyElementGeometryChanged(true);
	Stair->MarkPackageDirty();
	RedrawEditorViewports();
}

ECheckBoxState SEHBElementEditorPanel::IsWallLinkedPillarEndCapsChecked() const
{
	const AEHB_Wall* Wall = GetSelectedWall();
	return !Wall || Wall->bGenerateLinkedPillarEndCaps
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SEHBElementEditorPanel::HandleWallLinkedPillarEndCapsChanged(ECheckBoxState NewState)
{
	AEHB_Wall* Wall = GetSelectedWall();
	if (!Wall)
	{
		return;
	}

	const bool bNewEnabled = NewState == ECheckBoxState::Checked;
	if (Wall->bGenerateLinkedPillarEndCaps == bNewEnabled)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeWallLinkedPillarEndCaps", "Change Wall Linked Pillar End Caps"));
	Wall->Modify();
	Wall->bGenerateLinkedPillarEndCaps = bNewEnabled;
	Wall->RebuildWallMesh();
	Wall->NotifyElementGeometryChanged(true);
	Wall->MarkPackageDirty();
	RedrawEditorViewports();
}

ECheckBoxState SEHBElementEditorPanel::IsPillarLinkedWallFacesChecked() const
{
	const AEHB_Pillar* Pillar = GetSelectedPillar();
	return !Pillar || Pillar->bGenerateLinkedWallFaces
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SEHBElementEditorPanel::HandlePillarLinkedWallFacesChanged(ECheckBoxState NewState)
{
	AEHB_Pillar* Pillar = GetSelectedPillar();
	if (!Pillar)
	{
		return;
	}

	const bool bNewEnabled = NewState == ECheckBoxState::Checked;
	if (Pillar->bGenerateLinkedWallFaces == bNewEnabled)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangePillarLinkedWallFaces", "Change Pillar Linked Wall Faces"));
	Pillar->Modify();
	Pillar->bGenerateLinkedWallFaces = bNewEnabled;
	Pillar->RebuildPillarMesh();
	Pillar->NotifyElementGeometryChanged(true);
	Pillar->MarkPackageDirty();
	RedrawEditorViewports();
}

ECheckBoxState SEHBElementEditorPanel::IsFloorSlabAdjacentSnapChecked() const
{
	const AEHB_FloorSlab* FloorSlab = GetSelectedFloorSlab();
	return !FloorSlab || FloorSlab->bEnableAdjacentSlabSnap
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

ECheckBoxState SEHBElementEditorPanel::IsRoofCutCollidingElementsChecked() const
{
	const AEHBGableRoof* Roof = GetSelectedRoof();
	return Roof && Roof->bCutCollidingElements
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SEHBElementEditorPanel::HandleRoofCutCollidingElementsChanged(ECheckBoxState NewState)
{
	AEHBGableRoof* Roof = GetSelectedRoof();
	if (!Roof)
	{
		return;
	}

	const bool bNewEnabled = NewState == ECheckBoxState::Checked;
	if (Roof->bCutCollidingElements == bNewEnabled)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeRoofCutCollidingElements", "Change Roof Cut Colliding Elements"));
	Roof->Modify();
	Roof->bCutCollidingElements = bNewEnabled;
	Roof->RefreshAutoCollisionCutOperations();
	Roof->NotifyElementGeometryChanged(true);
	Roof->MarkPackageDirty();
	RedrawEditorViewports();
}

ECheckBoxState SEHBElementEditorPanel::IsRoofRemoveDisconnectedCutPiecesChecked() const
{
	const AEHBGableRoof* Roof = GetSelectedRoof();
	return !Roof || Roof->bRemoveDisconnectedCutPieces
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SEHBElementEditorPanel::HandleRoofRemoveDisconnectedCutPiecesChanged(ECheckBoxState NewState)
{
	AEHBGableRoof* Roof = GetSelectedRoof();
	if (!Roof)
	{
		return;
	}

	const bool bNewEnabled = NewState == ECheckBoxState::Checked;
	if (Roof->bRemoveDisconnectedCutPieces == bNewEnabled)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeRoofRemoveDisconnectedCutPieces", "Change Roof Remove Disconnected Cut Pieces"));
	Roof->Modify();
	Roof->bRemoveDisconnectedCutPieces = bNewEnabled;
	Roof->RebuildRoofMesh();
	Roof->NotifyElementGeometryChanged(true);
	Roof->MarkPackageDirty();
	RedrawEditorViewports();
}

ECheckBoxState SEHBElementEditorPanel::IsRoofKeepCutAwayDisconnectedPieceChecked() const
{
	const AEHBGableRoof* Roof = GetSelectedRoof();
	return Roof && Roof->bKeepCutAwayDisconnectedPieces
		? ECheckBoxState::Checked
		: ECheckBoxState::Unchecked;
}

void SEHBElementEditorPanel::HandleRoofKeepCutAwayDisconnectedPieceChanged(ECheckBoxState NewState)
{
	AEHBGableRoof* Roof = GetSelectedRoof();
	if (!Roof || !Roof->bRemoveDisconnectedCutPieces)
	{
		return;
	}

	const bool bNewEnabled = NewState == ECheckBoxState::Checked;
	if (Roof->bKeepCutAwayDisconnectedPieces == bNewEnabled)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeRoofKeepCutAwayDisconnectedPiece", "Change Roof Keep Cut-Away Piece"));
	Roof->Modify();
	Roof->bKeepCutAwayDisconnectedPieces = bNewEnabled;
	Roof->RebuildRoofMesh();
	Roof->NotifyElementGeometryChanged(true);
	Roof->MarkPackageDirty();
	RedrawEditorViewports();
}

bool SEHBElementEditorPanel::IsRoofKeepCutAwayDisconnectedPieceEnabled() const
{
	const AEHBGableRoof* Roof = GetSelectedRoof();
	return Roof && Roof->bRemoveDisconnectedCutPieces;
}

ECheckBoxState SEHBElementEditorPanel::IsRoofAccessoryChecked(EEHBRoofAccessoryToggle Toggle) const
{
	const AEHBGableRoof* Roof = GetSelectedRoof();
	if (!Roof)
	{
		return ECheckBoxState::Unchecked;
	}

	bool bChecked = false;
	switch (Toggle)
	{
	case EEHBRoofAccessoryToggle::Ridge:
		bChecked = Roof->bGenerateRidge;
		break;
	case EEHBRoofAccessoryToggle::Eaves:
		bChecked = Roof->bGenerateEaves;
		break;
	case EEHBRoofAccessoryToggle::GableRakes:
		bChecked = Roof->bGenerateGableRakes;
		break;
	case EEHBRoofAccessoryToggle::GableEndWalls:
		bChecked = Roof->bGenerateGableEndWalls;
		break;
	default:
		break;
	}
	return bChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SEHBElementEditorPanel::HandleRoofAccessoryChanged(ECheckBoxState NewState, EEHBRoofAccessoryToggle Toggle)
{
	AEHBGableRoof* Roof = GetSelectedRoof();
	if (!Roof)
	{
		return;
	}

	bool* TargetFlag = nullptr;
	FText TransactionText = LOCTEXT("ChangeRoofAccessory", "Change Roof Accessory");
	switch (Toggle)
	{
	case EEHBRoofAccessoryToggle::Ridge:
		TargetFlag = &Roof->bGenerateRidge;
		TransactionText = LOCTEXT("ChangeRoofGenerateRidge", "Change Roof Generate Ridge");
		break;
	case EEHBRoofAccessoryToggle::Eaves:
		TargetFlag = &Roof->bGenerateEaves;
		TransactionText = LOCTEXT("ChangeRoofGenerateEaves", "Change Roof Generate Eaves");
		break;
	case EEHBRoofAccessoryToggle::GableRakes:
		TargetFlag = &Roof->bGenerateGableRakes;
		TransactionText = LOCTEXT("ChangeRoofGenerateGableRakes", "Change Roof Generate Gable Rakes");
		break;
	case EEHBRoofAccessoryToggle::GableEndWalls:
		TargetFlag = &Roof->bGenerateGableEndWalls;
		TransactionText = LOCTEXT("ChangeRoofGenerateGableEndWalls", "Change Roof Generate Gable End Walls");
		break;
	default:
		break;
	}

	if (!TargetFlag)
	{
		return;
	}

	const bool bNewEnabled = NewState == ECheckBoxState::Checked;
	if (*TargetFlag == bNewEnabled)
	{
		return;
	}

	const FScopedTransaction Transaction(TransactionText);
	Roof->Modify();
	*TargetFlag = bNewEnabled;
	Roof->RebuildRoofMesh();
	Roof->NotifyElementGeometryChanged(true);
	Roof->MarkPackageDirty();
	RedrawEditorViewports();
}

float SEHBElementEditorPanel::GetRoofGableEndWallBoundaryInset() const
{
	const AEHBGableRoof* Roof = GetSelectedRoof();
	return Roof ? Roof->GableEndWallBoundaryInset : 0.0f;
}

void SEHBElementEditorPanel::HandleRoofGableEndWallBoundaryInsetChanged(float NewValue)
{
	AEHBGableRoof* Roof = GetSelectedRoof();
	if (!Roof)
	{
		return;
	}

	const float ClampedValue = FMath::Max(0.0f, NewValue);
	if (FMath::IsNearlyEqual(Roof->GableEndWallBoundaryInset, ClampedValue, 0.01f))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeRoofGableEndWallBoundaryInset", "Change Roof Gable End Wall Boundary Inset"));
	Roof->Modify();
	Roof->GableEndWallBoundaryInset = ClampedValue;
	Roof->RebuildRoofMesh();
	Roof->NotifyElementGeometryChanged(true);
	Roof->MarkPackageDirty();
	RedrawEditorViewports();
}

bool SEHBElementEditorPanel::IsRoofGableEndWallBoundaryInsetEnabled() const
{
	const AEHBGableRoof* Roof = GetSelectedRoof();
	return Roof && Roof->bGenerateGableEndWalls;
}

void SEHBElementEditorPanel::ApplyWallBasicField(EEHBWallBasicField Field, float NewValue)
{
	AEHB_Wall* Wall = GetSelectedWall();
	if (!Wall)
	{
		return;
	}

	float ClampedValue = NewValue;
	float* TargetValue = nullptr;
	FText TransactionText;
	bool bRefreshFromConnectedPillars = false;
	bool bRebuildConnectedPillars = true;

	switch (Field)
	{
	case EEHBWallBasicField::Height:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Wall->Height;
		TransactionText = LOCTEXT("ChangeWallHeight", "Change Wall Height");
		break;
	case EEHBWallBasicField::Thickness:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Wall->Thickness;
		TransactionText = LOCTEXT("ChangeWallThickness", "Change Wall Thickness");
		bRefreshFromConnectedPillars = true;
		break;
	case EEHBWallBasicField::CurveControlOffset:
		TargetValue = &Wall->CurveControlOffset;
		TransactionText = LOCTEXT("ChangeWallCurveControlOffset", "Change Wall Curve Control Offset");
		break;
	case EEHBWallBasicField::CurveSegmentLength:
		ClampedValue = FMath::Max(10.0f, NewValue);
		TargetValue = &Wall->CurveSegmentLength;
		TransactionText = LOCTEXT("ChangeWallCurveSegmentLength", "Change Wall Curve Segment Length");
		break;
	default:
		return;
	}

	if (!TargetValue || FMath::IsNearlyEqual(*TargetValue, ClampedValue, 0.01f))
	{
		return;
	}

	if (Field == EEHBWallBasicField::CurveControlOffset || Field == EEHBWallBasicField::CurveSegmentLength)
	{
		const FScopedTransaction Transaction(TransactionText);
		Wall->Modify();
		const float ControlOffset = Field == EEHBWallBasicField::CurveControlOffset ? ClampedValue : Wall->CurveControlOffset;
		const float SegmentLength = Field == EEHBWallBasicField::CurveSegmentLength ? ClampedValue : Wall->CurveSegmentLength;
		Wall->ApplyCurveSettings(ControlOffset, SegmentLength, true);
		RedrawEditorViewports();
		return;
	}

	const FScopedTransaction Transaction(TransactionText);
	Wall->Modify();
	*TargetValue = ClampedValue;

	if (bRefreshFromConnectedPillars)
	{
		Wall->RefreshFromConnectedPillars(true);
		bRebuildConnectedPillars = false;
	}
	else
	{
		Wall->RebuildWallMesh();
	}

	if (bRebuildConnectedPillars)
	{
		Wall->RebuildConnectedPillarMeshes();
	}

	Wall->NotifyElementGeometryChanged(true);
	Wall->MarkPackageDirty();
	RedrawEditorViewports();
}

void SEHBElementEditorPanel::HandleWallTargetLengthChanged(float NewValue)
{
	PendingWallTargetLength = FMath::Clamp(NewValue, 1.0f, 100000.0f);
}

FReply SEHBElementEditorPanel::HandleMoveWallLengthPillarClicked(bool bMoveStartPillar)
{
	AEHB_Wall* Wall = GetSelectedWall();
	AEHBBuildingActorBase* Building = Wall ? Wall->OwningBuilding.Get() : nullptr;
	if (!Wall || !Building)
	{
		return FReply::Handled();
	}

	AEHB_Pillar* StartPillar = Cast<AEHB_Pillar>(Building->FindElementActorByGuid(Wall->StartPillarGuid));
	AEHB_Pillar* EndPillar = Cast<AEHB_Pillar>(Building->FindElementActorByGuid(Wall->EndPillarGuid));
	if (!StartPillar || !EndPillar || StartPillar == EndPillar)
	{
		return FReply::Handled();
	}

	const FVector StartLocation = StartPillar->GetElementLocalTransform().GetLocation();
	const FVector EndLocation = EndPillar->GetElementLocalTransform().GetLocation();
	const FVector WallDirection = (EndLocation - StartLocation).GetSafeNormal2D();
	const float CurrentPillarDistance = FVector::Dist2D(StartLocation, EndLocation);
	if (WallDirection.IsNearlyZero() || CurrentPillarDistance <= UE_SMALL_NUMBER)
	{
		return FReply::Handled();
	}

	const float TargetWallLength = FMath::Clamp(GetWallTargetLength(), 1.0f, 100000.0f);
	const float CurrentWallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
	const float ConnectionInset = FMath::Max(0.0f, CurrentPillarDistance - CurrentWallLength);
	const float TargetPillarDistance = TargetWallLength + ConnectionInset;
	AEHB_Pillar* MovedPillar = bMoveStartPillar ? StartPillar : EndPillar;
	const FVector FixedLocation = bMoveStartPillar ? EndLocation : StartLocation;
	FVector TargetLocation = bMoveStartPillar
		? FixedLocation - WallDirection * TargetPillarDistance
		: FixedLocation + WallDirection * TargetPillarDistance;
	TargetLocation.Z = MovedPillar->GetElementLocalTransform().GetLocation().Z;

	const FScopedTransaction Transaction(
		bMoveStartPillar
			? LOCTEXT("MoveWallStartPillarForLength", "Move Left Wall Pillar For Length")
			: LOCTEXT("MoveWallEndPillarForLength", "Move Right Wall Pillar For Length"));
	Building->Modify();
	Wall->Modify();
	MovedPillar->Modify();

	FTransform MovedPillarTransform = MovedPillar->GetElementLocalTransform();
	MovedPillarTransform.SetLocation(TargetLocation);
	MovedPillar->SetActorRelativeTransform(MovedPillarTransform);
	MovedPillar->RebuildPillarMesh();
	Building->RefreshWallsConnectedToPillar(MovedPillar->ElementGuid, true);
	MovedPillar->NotifyElementGeometryChanged(true);
	MovedPillar->MarkPackageDirty();
	Wall->MarkPackageDirty();
	Building->MarkPackageDirty();

	PendingWallTargetLength.Reset();
	RedrawEditorViewports();
	return FReply::Handled();
}

void SEHBElementEditorPanel::HandleWallSampleStartOffsetChanging(bool bLeftSide, float NewValue)
{
	const float ClampedValue = FMath::Clamp(NewValue, 0.0f, 1.0f);
	if (bLeftSide)
	{
		PendingLeftWallSampleStartOffset = ClampedValue;
	}
	else
	{
		PendingRightWallSampleStartOffset = ClampedValue;
	}
}

void SEHBElementEditorPanel::ApplyWallSampleStartOffset(bool bLeftSide, float NewValue)
{
	AEHB_Wall* Wall = GetSelectedWall();
	if (!Wall)
	{
		return;
	}

	if (bLeftSide)
	{
		PendingLeftWallSampleStartOffset.Reset();
	}
	else
	{
		PendingRightWallSampleStartOffset.Reset();
	}

	FEHBWallSurfaceStyle& RootStyle = bLeftSide ? Wall->LeftSurfaceStyle : Wall->RightSurfaceStyle;
	if (!IsSampledWallSurfaceStyleForEditing(RootStyle))
	{
		return;
	}

	const float ClampedValue = FMath::Clamp(NewValue, 0.0f, 1.0f);
	bool bHasChange = false;
	ForEachSharedSampleOffsetSurface(*Wall, bLeftSide, [ClampedValue, &bHasChange](AEHB_Wall& ConnectedWall, bool bConnectedLeftSide)
	{
		const FEHBWallSurfaceStyle& SurfaceStyle = bConnectedLeftSide
			? ConnectedWall.LeftSurfaceStyle
			: ConnectedWall.RightSurfaceStyle;
		bHasChange |= !FMath::IsNearlyEqual(SurfaceStyle.SampleStartOffset, ClampedValue, 0.001f);
	});

	if (!bHasChange)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeWallSampleStartOffset", "Change Wall Sample Start Offset"));
	ForEachSharedSampleOffsetSurface(*Wall, bLeftSide, [ClampedValue](AEHB_Wall& ConnectedWall, bool bConnectedLeftSide)
	{
		ConnectedWall.Modify();
		FEHBWallSurfaceStyle& SurfaceStyle = bConnectedLeftSide
			? ConnectedWall.LeftSurfaceStyle
			: ConnectedWall.RightSurfaceStyle;
		SurfaceStyle.SampleStartOffset = ClampedValue;
		if (bConnectedLeftSide)
		{
			ConnectedWall.RebuildLeftWallMesh();
		}
		else
		{
			ConnectedWall.RebuildRightWallMesh();
		}

		ConnectedWall.RebuildConnectedPillarMeshes();
		ConnectedWall.NotifyElementGeometryChanged(true);
		ConnectedWall.MarkPackageDirty();
	});

	RedrawEditorViewports();
}

void SEHBElementEditorPanel::ApplyWindowSillHeight(float NewValue)
{
	AEHB_DoorWindow* DoorWindow = GetSelectedDoorWindow();
	if (!DoorWindow || DoorWindow->Kind != EEHBDoorWindowElementKind::Window)
	{
		return;
	}

	const float ClampedValue = FMath::Max(0.0f, NewValue);
	if (FMath::IsNearlyEqual(DoorWindow->SillHeight, ClampedValue, 0.01f))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeWindowSillHeight", "Change Window Sill Height"));
	if (DoorWindow->SetWindowSillHeight(ClampedValue))
	{
		RedrawEditorViewports();
	}
}

void SEHBElementEditorPanel::ApplyPillarBasicField(EEHBPillarBasicField Field, float NewValue)
{
	AEHB_Pillar* Pillar = GetSelectedPillar();
	if (!Pillar)
	{
		return;
	}

	const float ClampedValue = FMath::Max(1.0f, NewValue);
	float* TargetValue = nullptr;
	FText TransactionText;

	switch (Field)
	{
	case EEHBPillarBasicField::Height:
		TargetValue = &Pillar->Height;
		TransactionText = LOCTEXT("ChangePillarHeight", "Change Pillar Height");
		break;
	case EEHBPillarBasicField::Width:
		TargetValue = &Pillar->Width;
		TransactionText = LOCTEXT("ChangePillarWidth", "Change Pillar Width");
		break;
	case EEHBPillarBasicField::Depth:
		TargetValue = &Pillar->Depth;
		TransactionText = LOCTEXT("ChangePillarDepth", "Change Pillar Depth");
		break;
	default:
		return;
	}

	if (!TargetValue || FMath::IsNearlyEqual(*TargetValue, ClampedValue, 0.01f))
	{
		return;
	}

	const FScopedTransaction Transaction(TransactionText);
	Pillar->Modify();
	*TargetValue = ClampedValue;
	Pillar->RebuildPillarMesh();
	if (Pillar->OwningBuilding)
	{
		Pillar->OwningBuilding->RefreshWallsConnectedToPillar(Pillar->ElementGuid, true);
	}

	Pillar->NotifyElementGeometryChanged(true);
	Pillar->MarkPackageDirty();
	RedrawEditorViewports();
}

void SEHBElementEditorPanel::ApplyRailingBasicField(EEHBRailingBasicField Field, float NewValue)
{
	AEHB_Railing* Railing = GetSelectedRailing();
	if (!Railing)
	{
		return;
	}

	const float CurrentRailTopHeight = Railing->GetRailTopHeight();
	float ClampedValue = NewValue;
	FText TransactionText;
	bool bHasChange = false;

	switch (Field)
	{
	case EEHBRailingBasicField::RailTopHeight:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TransactionText = LOCTEXT("ChangeRailingRailTopHeight", "Change Railing Rail Top Height");
		bHasChange = !FMath::IsNearlyEqual(CurrentRailTopHeight, ClampedValue, 0.01f);
		break;
	case EEHBRailingBasicField::PostSpacing:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TransactionText = LOCTEXT("ChangeRailingPostSpacing", "Change Railing Post Spacing");
		bHasChange = Railing->PostSpacingMode != EEHBRailingPostSpacingMode::Distance
			|| !FMath::IsNearlyEqual(Railing->PostSpacing, ClampedValue, 0.01f);
		break;
	case EEHBRailingBasicField::PostWidth:
		ClampedValue = FMath::Max(0.1f, NewValue);
		TransactionText = LOCTEXT("ChangeRailingPostWidth", "Change Railing Post Width");
		bHasChange = !FMath::IsNearlyEqual(Railing->PostWidth, ClampedValue, 0.01f);
		break;
	case EEHBRailingBasicField::PostHeight:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TransactionText = LOCTEXT("ChangeRailingPostHeight", "Change Railing Post Height");
		bHasChange = !FMath::IsNearlyEqual(Railing->PostHeight, ClampedValue, 0.01f);
		break;
	case EEHBRailingBasicField::RailThickness:
		ClampedValue = FMath::Max(0.1f, NewValue);
		TransactionText = LOCTEXT("ChangeRailingRailThickness", "Change Railing Rail Thickness");
		bHasChange = !FMath::IsNearlyEqual(Railing->RailThickness, ClampedValue, 0.01f);
		break;
	case EEHBRailingBasicField::MaxRailSegmentLength:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TransactionText = LOCTEXT("ChangeRailingMaxRailSegmentLength", "Change Railing Max Rail Segment Length");
		bHasChange = !FMath::IsNearlyEqual(Railing->MaxRailSegmentLength, ClampedValue, 0.01f);
		break;
	default:
		return;
	}

	if (!bHasChange)
	{
		return;
	}

	const FScopedTransaction Transaction(TransactionText);
	Railing->Modify();
	switch (Field)
	{
	case EEHBRailingBasicField::RailTopHeight:
		Railing->SetRailTopHeight(ClampedValue);
		break;
	case EEHBRailingBasicField::PostSpacing:
		Railing->PostSpacingMode = EEHBRailingPostSpacingMode::Distance;
		Railing->PostSpacing = ClampedValue;
		break;
	case EEHBRailingBasicField::PostWidth:
		Railing->PostWidth = ClampedValue;
		break;
	case EEHBRailingBasicField::PostHeight:
		Railing->PostHeight = ClampedValue;
		break;
	case EEHBRailingBasicField::RailThickness:
		Railing->RailThickness = ClampedValue;
		Railing->SetRailTopHeight(CurrentRailTopHeight);
		break;
	case EEHBRailingBasicField::MaxRailSegmentLength:
		Railing->MaxRailSegmentLength = ClampedValue;
		break;
	default:
		return;
	}

	Railing->RebuildRailing();
	Railing->NotifyElementGeometryChanged(true);
	Railing->MarkPackageDirty();
	RedrawEditorViewports();
}

void SEHBElementEditorPanel::ApplyStairNumericField(EEHBStairNumericField Field, float NewValue)
{
	AEHB_Stair* Stair = GetSelectedStair();
	if (!Stair)
	{
		return;
	}

	float ClampedValue = NewValue;
	float* TargetValue = nullptr;
	int32* TargetIntValue = nullptr;
	FText TransactionText = LOCTEXT("ChangeStairNumericField", "Change Stair Numeric Field");

	switch (Field)
	{
	case EEHBStairNumericField::TreadDepth:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Stair->StairData.TreadDepth;
		TransactionText = LOCTEXT("ChangeStairTreadDepth", "Change Stair Tread Depth");
		break;
	case EEHBStairNumericField::StairWidth:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Stair->StairData.StairWidth;
		TransactionText = LOCTEXT("ChangeStairWidth", "Change Stair Width");
		break;
	case EEHBStairNumericField::StairHeight:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Stair->StairData.StairHeight;
		TransactionText = LOCTEXT("ChangeStairHeight", "Change Stair Height");
		break;
	case EEHBStairNumericField::MinStepHeight:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Stair->StairData.MinStepHeight;
		TransactionText = LOCTEXT("ChangeStairMinStepHeight", "Change Stair Min Step Height");
		break;
	case EEHBStairNumericField::MaxStepHeight:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Stair->StairData.MaxStepHeight;
		TransactionText = LOCTEXT("ChangeStairMaxStepHeight", "Change Stair Max Step Height");
		break;
	case EEHBStairNumericField::PanelThickness:
		ClampedValue = FMath::Max(0.1f, NewValue);
		TargetValue = &Stair->StairData.PanelThickness;
		TransactionText = LOCTEXT("ChangeStairPanelThickness", "Change Stair Panel Thickness");
		break;
	case EEHBStairNumericField::NosingLength:
		ClampedValue = FMath::Max(0.0f, NewValue);
		TargetValue = &Stair->StairData.NosingLength;
		TransactionText = LOCTEXT("ChangeStairNosingLength", "Change Stair Nosing Length");
		break;
	case EEHBStairNumericField::SideProtruding:
		ClampedValue = FMath::Max(0.0f, NewValue);
		TargetValue = &Stair->StairData.SideProtruding;
		TransactionText = LOCTEXT("ChangeStairSideProtruding", "Change Stair Side Protruding");
		break;
	case EEHBStairNumericField::SideThickness:
		ClampedValue = FMath::Max(0.1f, NewValue);
		TargetValue = &Stair->StairData.SideThickness;
		TransactionText = LOCTEXT("ChangeStairSideThickness", "Change Stair Side Thickness");
		break;
	case EEHBStairNumericField::SideBoardHeight:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Stair->StairData.SideBoardHeight;
		TransactionText = LOCTEXT("ChangeStairSideBoardHeight", "Change Stair Side Board Height");
		break;
	case EEHBStairNumericField::SideBoardTopOffset:
		ClampedValue = FMath::Max(0.0f, NewValue);
		TargetValue = &Stair->StairData.SideBoardTopOffset;
		TransactionText = LOCTEXT("ChangeStairSideBoardTopOffset", "Change Stair Side Board Top Offset");
		break;
	case EEHBStairNumericField::SideGuardThickness:
		ClampedValue = FMath::Max(0.1f, NewValue);
		TargetValue = &Stair->StairData.SideGuardThickness;
		TransactionText = LOCTEXT("ChangeStairSideGuardThickness", "Change Stair Side Guard Thickness");
		break;
	case EEHBStairNumericField::SideGuardHeight:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Stair->StairData.SideGuardHeight;
		TransactionText = LOCTEXT("ChangeStairSideGuardHeight", "Change Stair Side Guard Height");
		break;
	case EEHBStairNumericField::SideGuardTopOffset:
		TargetValue = &Stair->StairData.SideGuardTopOffset;
		TransactionText = LOCTEXT("ChangeStairSideGuardTopOffset", "Change Stair Side Guard Top Offset");
		break;
	case EEHBStairNumericField::RailingStepsPerPost:
		ClampedValue = static_cast<float>(FMath::Max(1, FMath::RoundToInt(NewValue)));
		TargetIntValue = &Stair->StairData.RailingStepsPerPost;
		TransactionText = LOCTEXT("ChangeStairRailingStepsPerPost", "Change Stair Railing Steps Per Post");
		break;
	case EEHBStairNumericField::RailingEdgeInset:
		ClampedValue = FMath::Max(0.0f, NewValue);
		TargetValue = &Stair->StairData.RailingEdgeInset;
		TransactionText = LOCTEXT("ChangeStairRailingEdgeInset", "Change Stair Railing Edge Inset");
		break;
	case EEHBStairNumericField::RailingPostForwardOffset:
		ClampedValue = FMath::Clamp(NewValue, -10000.0f, 10000.0f);
		TargetValue = &Stair->StairData.RailingPostForwardOffset;
		TransactionText = LOCTEXT("ChangeStairRailingPostForwardOffset", "Change Stair Railing Post Forward Offset");
		break;
	case EEHBStairNumericField::RailingPostWidth:
		ClampedValue = FMath::Max(0.1f, NewValue);
		TargetValue = &Stair->StairData.RailingPostWidth;
		TransactionText = LOCTEXT("ChangeStairRailingPostWidth", "Change Stair Railing Post Width");
		break;
	case EEHBStairNumericField::RailingPostHeight:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Stair->StairData.RailingPostHeight;
		TransactionText = LOCTEXT("ChangeStairRailingPostHeight", "Change Stair Railing Post Height");
		break;
	case EEHBStairNumericField::RailingRailHeight:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Stair->StairData.RailingRailHeight;
		TransactionText = LOCTEXT("ChangeStairRailingRailHeight", "Change Stair Railing Rail Height");
		break;
	case EEHBStairNumericField::RailingRailThickness:
		ClampedValue = FMath::Max(0.1f, NewValue);
		TargetValue = &Stair->StairData.RailingRailThickness;
		TransactionText = LOCTEXT("ChangeStairRailingRailThickness", "Change Stair Railing Rail Thickness");
		break;
	case EEHBStairNumericField::RailingMaxRailSegmentLength:
		ClampedValue = FMath::Max(1.0f, NewValue);
		TargetValue = &Stair->StairData.RailingMaxRailSegmentLength;
		TransactionText = LOCTEXT("ChangeStairRailingMaxRailSegmentLength", "Change Stair Railing Max Rail Segment Length");
		break;
	default:
		return;
	}

	bool bHasChange = false;
	if (TargetValue)
	{
		bHasChange = !FMath::IsNearlyEqual(*TargetValue, ClampedValue, 0.01f);
	}
	else if (TargetIntValue)
	{
		bHasChange = *TargetIntValue != FMath::RoundToInt(ClampedValue);
	}

	if (!bHasChange)
	{
		return;
	}

	const FScopedTransaction Transaction(TransactionText);
	Stair->Modify();
	if (TargetValue)
	{
		*TargetValue = ClampedValue;
	}
	else if (TargetIntValue)
	{
		*TargetIntValue = FMath::RoundToInt(ClampedValue);
	}

	Stair->RebuildStairMesh();
	Stair->NotifyElementGeometryChanged(true);
	Stair->MarkPackageDirty();
	RedrawEditorViewports();
}

void SEHBElementEditorPanel::HandleFloorSlabVisualExpansionChanged(float NewValue)
{
	AEHB_FloorSlab* FloorSlab = GetSelectedFloorSlab();
	if (!FloorSlab)
	{
		return;
	}

	const float ClampedValue = FMath::Max(0.0f, NewValue);
	if (FMath::IsNearlyEqual(FloorSlab->VisualExpansion, ClampedValue, 0.01f))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeFloorSlabVisualExpansion", "Change Floor Slab Visual Expansion"));
	FloorSlab->Modify();
	FloorSlab->VisualExpansion = ClampedValue;
	FloorSlab->RebuildSlabMesh();
	FloorSlab->MarkPackageDirty();
	RedrawEditorViewports();
}

void SEHBElementEditorPanel::HandleFloorSlabCornerDistanceCommitted(
	float NewValue,
	ETextCommit::Type CommitType,
	bool bPreviousPoint)
{
	if(CommitType==ETextCommit::OnCleared||!FMath::IsFinite(NewValue))return;
	AEHB_FloorSlab* FloorSlab = SelectedFloorSlabCornerOwner.Get();
	TArray<FVector> Loop;
	if (!FloorSlab
		|| FloorSlab != GetSelectedFloorSlab()
		|| !FloorSlab->GetEditableLoopCopy(SelectedFloorSlabCornerLoopIndex, Loop)
		|| Loop.Num() < 3
		|| !Loop.IsValidIndex(SelectedFloorSlabCornerPointIndex))
	{
		return;
	}

	const float ClampedDistance = FMath::Clamp(NewValue, 1.0f, 100000.0f);
	const int32 AnchorIndex = bPreviousPoint
		? (SelectedFloorSlabCornerPointIndex - 1 + Loop.Num()) % Loop.Num()
		: (SelectedFloorSlabCornerPointIndex + 1) % Loop.Num();
	const float CurrentDistance = FVector::Dist2D(
		Loop[SelectedFloorSlabCornerPointIndex],
		Loop[AnchorIndex]);
	if (FMath::IsNearlyEqual(CurrentDistance, ClampedDistance, 0.01f))
	{
		return;
	}


 if(SelectedFloorSlabCornerLoopIndex!=INDEX_NONE&&EHBSlabOpeningEdit::Supports(FloorSlab))
 {
  FEHBFinishRegionDrag Draft;Draft.Begin(FloorSlab,SelectedFloorSlabCornerPointIndex,INDEX_NONE,true,SelectedFloorSlabCornerLoopIndex);
  FEHBToolsetOperationResult Result=Draft.Feedback;
  if(Draft.bReady)
  {
   const FVector Direction=(Loop[SelectedFloorSlabCornerPointIndex]-Loop[AnchorIndex]).GetSafeNormal2D();
   if(Direction.IsNearlyZero()){Result.bSucceeded=false;Result.Message=TEXT("InvalidOpeningHandle");}
   else {Draft.Polygon[SelectedFloorSlabCornerPointIndex]=Loop[AnchorIndex]+Direction*ClampedDistance;Draft.Polygon[SelectedFloorSlabCornerPointIndex].Z=Loop[SelectedFloorSlabCornerPointIndex].Z;Result=Draft.Execute(false);}
  }
  FNotificationInfo Notice(EHBFinishRegionCommand::DescribeResult(Result));Notice.ExpireDuration=5;FSlateNotificationManager::Get().AddNotification(Notice);RedrawEditorViewports();return;
 }
	if(EHBFinishRegionCommand::IsIndependent(FloorSlab))
	{
		if(SelectedFloorSlabCornerLoopIndex!=INDEX_NONE)return;
		Loop[SelectedFloorSlabCornerPointIndex]=Loop[AnchorIndex]+(Loop[SelectedFloorSlabCornerPointIndex]-Loop[AnchorIndex]).GetSafeNormal2D()*ClampedDistance;
		auto* B=FloorSlab->OwningBuilding.Get();const auto Result=UEHBBuildingToolset::EditFinishRegion(B,FloorSlab->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(FloorSlab->ElementGuid),Loop,{},false);
		FNotificationInfo Notice(EHBFinishRegionCommand::DescribeResult(Result));Notice.ExpireDuration=5;FSlateNotificationManager::Get().AddNotification(Notice);return;
	}
	const FScopedTransaction Transaction(bPreviousPoint
		? LOCTEXT("ChangeFloorSlabPreviousCornerDistance", "Change Floor Slab Previous Corner Distance")
		: LOCTEXT("ChangeFloorSlabNextCornerDistance", "Change Floor Slab Next Corner Distance"));
	FloorSlab->Modify();
	if (!FloorSlab->UpdateCornerDistanceToAdjacentPoint(
		SelectedFloorSlabCornerLoopIndex,
		SelectedFloorSlabCornerPointIndex,
		bPreviousPoint,
		ClampedDistance))
	{
		return;
	}

	if (FloorSlab->bIsFoundation && FloorSlab->SnapFoundationBottomToGround())
	{
		FloorSlab->RebuildSlabMesh();
	}
	FloorSlab->NotifyElementGeometryChanged(true);
	FloorSlab->MarkPackageDirty();
	RedrawEditorViewports();
}

void SEHBElementEditorPanel::HandleFloorSlabAdjacentSnapChanged(ECheckBoxState NewState)
{
	AEHB_FloorSlab* FloorSlab = GetSelectedFloorSlab();
	if (!FloorSlab)
	{
		return;
	}

	const bool bNewEnabled = NewState == ECheckBoxState::Checked;
	if (FloorSlab->bEnableAdjacentSlabSnap == bNewEnabled)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeFloorSlabAdjacentSnap", "Change Floor Slab Adjacent Snap"));
	FloorSlab->Modify();
	FloorSlab->bEnableAdjacentSlabSnap = bNewEnabled;
	FloorSlab->MarkPackageDirty();
	RedrawEditorViewports();
}

EVisibility SEHBElementEditorPanel::GetSelectionVisibility() const
{
	return SelectedActor.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SEHBElementEditorPanel::GetEmptyVisibility() const
{
	return SelectedActor.IsValid() ? EVisibility::Collapsed : EVisibility::Visible;
}

AEHB_Wall* SEHBElementEditorPanel::GetSelectedWall() const
{
	return Cast<AEHB_Wall>(SelectedActor.Get());
}

AEHB_DoorWindow* SEHBElementEditorPanel::GetSelectedDoorWindow() const
{
	return Cast<AEHB_DoorWindow>(SelectedActor.Get());
}

AEHB_Pillar* SEHBElementEditorPanel::GetSelectedPillar() const
{
	return Cast<AEHB_Pillar>(SelectedActor.Get());
}

AEHB_Railing* SEHBElementEditorPanel::GetSelectedRailing() const
{
	return Cast<AEHB_Railing>(SelectedActor.Get());
}

AEHB_Stair* SEHBElementEditorPanel::GetSelectedStair() const
{
	return Cast<AEHB_Stair>(SelectedActor.Get());
}

AEHBGableRoof* SEHBElementEditorPanel::GetSelectedRoof() const
{
	if (AEHBGableRoof* Roof = Cast<AEHBGableRoof>(SelectedActor.Get()))
	{
		return Roof;
	}

	const TArray<AEHBGableRoof*> SelectedRoofs = GetEditorSelectedRoofs();
	return SelectedRoofs.Num() == 1 ? SelectedRoofs[0] : nullptr;
}

AEHB_FloorSlab* SEHBElementEditorPanel::GetSelectedFloorSlab() const
{
	return Cast<AEHB_FloorSlab>(SelectedActor.Get());
}

TArray<AEHBGableRoof*> SEHBElementEditorPanel::GetEditorSelectedRoofs() const
{
	TArray<AEHBGableRoof*> Roofs;
#if WITH_EDITOR
	if (GEditor && GEditor->GetSelectedActors())
	{
		TArray<AActor*> SelectedActors;
		GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);
		for (AActor* Actor : SelectedActors)
		{
			AEHBGableRoof* Roof = Cast<AEHBGableRoof>(Actor);
			if (Roof && !Roof->IsActorBeingDestroyed())
			{
				Roofs.AddUnique(Roof);
			}
		}
	}
#endif

	AEHBGableRoof* SelectedRoof = Cast<AEHBGableRoof>(SelectedActor.Get());
	if (SelectedRoof && !SelectedRoof->IsActorBeingDestroyed())
	{
		Roofs.AddUnique(SelectedRoof);
	}
	return Roofs;
}

const AEHBElementActorBase* SEHBElementEditorPanel::GetSelectedElementActor() const
{
	return Cast<AEHBElementActorBase>(SelectedActor.Get());
}

const AEHBBuildingActorBase* SEHBElementEditorPanel::GetSelectedBuilding() const
{
	const AActor* Actor = SelectedActor.Get();
	if (!Actor)
	{
		return nullptr;
	}

	if (const AEHBBuildingActorBase* Building = Cast<AEHBBuildingActorBase>(Actor))
	{
		return Building;
	}

	if (const AEHBElementActorBase* ElementActor = Cast<AEHBElementActorBase>(Actor))
	{
		return ElementActor->OwningBuilding;
	}

	return nullptr;
}

AEHBBuildingActorBase* SEHBElementEditorPanel::GetSelectedBuildingObjectMutable() const
{
	return Cast<AEHBBuildingActorBase>(SelectedActor.Get());
}

AEHBBuildingActorBase* SEHBElementEditorPanel::GetSelectedBuildingMutable() const
{
	AActor* Actor = SelectedActor.Get();
	if (!Actor)
	{
		return nullptr;
	}

	if (AEHBBuildingActorBase* Building = Cast<AEHBBuildingActorBase>(Actor))
	{
		return Building;
	}

	if (AEHBElementActorBase* ElementActor = Cast<AEHBElementActorBase>(Actor))
	{
		return ElementActor->OwningBuilding;
	}

	return nullptr;
}

void SEHBElementEditorPanel::RedrawEditorViewports() const
{
	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

namespace
{
// Only round-trip rectangles whose chart and point ordering are understood by
// this control. Never flatten arbitrary polygons or actor-driven door/window cuts.
bool ReadEditableWallRectangle(const AEHB_Wall* Wall, const FEHBCutOperation& Cut, FVector4f& Out)
{
 if(!Wall || !Wall->OwningBuilding || !Cut.OperationGuid.IsValid() || !Cut.bEnabled
  || Cut.SurfaceHost.Version!=1 || Cut.SurfaceHost.BuildingGuid!=Wall->OwningBuilding->BuildingGuid
  || Cut.SurfaceHost.ElementGuid!=Wall->ElementGuid || Cut.SurfaceHost.SurfaceGuid!=Wall->FindLogicalSurfaceIdentity(TEXT("Wall.Left"))
  || Cut.OperationType!=EEHBCutOperationType::Subtract || Cut.Stage!=EEHBCutStage::SurfaceOpening
  || Cut.ProjectionMode!=EEHBCutProjectionMode::TargetPlane || Cut.TransformPolicy!=EEHBCutTransformPolicy::FollowTargetElement
  || Cut.Source.SourceType!=EEHBCutSourceType::ExplicitPolygon || Cut.Source.SourceElement || Cut.Source.SourceElementGuid.IsValid()
  || !Cut.Source.LocalTransform.Equals(FTransform::Identity) || Cut.Source.ExplicitPolygon.Points.Num()!=4) return false;
 const auto P=Cut.Source.ExplicitPolygon.ToLocalPositions();
 const double Width=P[1].X-P[0].X, Height=P[3].Y-P[0].Y;
 if(Width<=0 || Height<=0 || !FMath::IsFinite(Width) || !FMath::IsFinite(Height)) return false;
 const FVector Expected[]={FVector(P[0].X,P[0].Y,0),FVector(P[0].X+Width,P[0].Y,0),FVector(P[0].X+Width,P[0].Y+Height,0),FVector(P[0].X,P[0].Y+Height,0)};
 for(int32 I=0;I<4;++I) if(P[I].ContainsNaN() || !P[I].Equals(Expected[I],0.001)) return false;
 Out=FVector4f(P[0].X,P[0].Y,Width,Height);
 return true;
}
}

bool SEHBElementEditorPanel::SelectWallOpening(FGuid OperationGuid)
{
 const auto* W=GetSelectedWall();
 const auto* Cut=W?W->CutOperations.FindByPredicate([&](const auto& C){return C.OperationGuid==OperationGuid;}):nullptr;
 FVector4f Rectangle;
 if(!Cut || !ReadEditableWallRectangle(W,*Cut,Rectangle)) return false;
 SelectedWallOpening=OperationGuid;
 SelectedWallOpeningSource=*Cut;
 WallOpeningRectangle=Rectangle;
 WallOpeningFeedback=FText::GetEmpty();
 return true;
}

FText SEHBElementEditorPanel::GetWallOpeningSelectionText() const
{
 const auto* W=GetSelectedWall();
 const int32 Index=W?W->CutOperations.IndexOfByPredicate([this](const auto& C){return C.OperationGuid==SelectedWallOpening;}):INDEX_NONE;
 if(Index==INDEX_NONE) return LOCTEXT("SelectExistingWallOpening","选择已有矩形开口");
 return FText::Format(LOCTEXT("SelectedWallOpeningNumber","开口 {0}"),FText::AsNumber(Index+1));
}

TSharedRef<SWidget> SEHBElementEditorPanel::BuildWallOpeningMenu()
{
 FMenuBuilder Menu(true,nullptr);
 int32 Count=0;
 if(const auto* W=GetSelectedWall()) for(int32 I=0;I<W->CutOperations.Num();++I)
 {
  const auto& Cut=W->CutOperations[I]; FVector4f R;
  if(!ReadEditableWallRectangle(W,Cut,R)) continue;
  ++Count;
  Menu.AddMenuEntry(FText::Format(LOCTEXT("WallOpeningMenuEntry","开口 {0} · 起点 {1} · 宽 {2} × 高 {3} cm"),FText::AsNumber(I+1),FText::AsNumber(R.X),FText::AsNumber(R.Z),FText::AsNumber(R.W)),
   FText::GetEmpty(),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([this,Id=Cut.OperationGuid](){SelectWallOpening(Id);})));
 }
 if(!Count) Menu.AddMenuEntry(LOCTEXT("NoEditableWallRectangles","没有可编辑的矩形开口"),FText::GetEmpty(),FSlateIcon(),FUIAction());
 return Menu.MakeWidget();
}

TSharedRef<SWidget> SEHBElementEditorPanel::BuildWallOpeningControls()
{
 auto Box=SNew(SVerticalBox).Visibility_Lambda([this](){return GetSelectedWall()?EVisibility::Visible:EVisibility::Collapsed;});
 Box->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).Text(LOCTEXT("BoundOpeningTitle","矩形开口（厘米）"))];
 Box->AddSlot().AutoHeight().Padding(0,4)[SNew(SComboButton).OnGetMenuContent(this,&SEHBElementEditorPanel::BuildWallOpeningMenu)
  .ButtonContent()[SNew(STextBlock).Text(this,&SEHBElementEditorPanel::GetWallOpeningSelectionText)]];
 const FText Labels[]={LOCTEXT("BoundOpeningOffset","距左侧墙面起点"),LOCTEXT("BoundOpeningSill","离墙底高度"),LOCTEXT("BoundOpeningWidth","开口宽度"),LOCTEXT("BoundOpeningHeight","开口高度")};
 for(int32 I=0;I<4;++I)Box->AddSlot().AutoHeight().Padding(0,2)
 [SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(Labels[I])]
 +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<float>).MinValue(I<2?0.f:1.f).MaxValue(100000.f).Delta(1.f)
 .Value_Lambda([this,I](){return WallOpeningRectangle[I];}).OnValueChanged_Lambda([this,I](float V){WallOpeningRectangle[I]=V;})]];
 Box->AddSlot().AutoHeight().Padding(0,4)[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("BoundOpeningHint","离墙底高度填0可制作门洞。选择已有开口后修改数值，点击应用修改；添加会创建另一个开口。列表仅包含普通直墙左侧的矩形表面开口。"))];
 Box->AddSlot().AutoHeight()[SNew(SHorizontalBox)
 +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("AddBoundOpening","添加矩形开口")).OnClicked(this,&SEHBElementEditorPanel::ApplyWallOpeningRectangle,false,false)]
 +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("UpdateBoundOpening","应用修改")).IsEnabled_Lambda([this](){return SelectedWallOpeningSource.IsSet();}).OnClicked(this,&SEHBElementEditorPanel::ApplyWallOpeningRectangle,false,true)]
 +SHorizontalBox::Slot().FillWidth(1)[SNew(SButton).Text(LOCTEXT("RemoveBoundOpening","删除所选开口")).IsEnabled_Lambda([this](){return SelectedWallOpeningSource.IsSet();}).OnClicked(this,&SEHBElementEditorPanel::ApplyWallOpeningRectangle,true,false)]];
 Box->AddSlot().AutoHeight()[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return WallOpeningFeedback;})];
 return Box;
}

FReply SEHBElementEditorPanel::ApplyWallOpeningRectangle(bool bRemoveSelected, bool bUpdateSelected)
{
 auto* W=GetSelectedWall();if(!W||!W->OwningBuilding)return FReply::Handled();
 auto Cuts=W->CutOperations;
 const int32 SelectedIndex=Cuts.IndexOfByPredicate([this](const auto& C){return C.OperationGuid==SelectedWallOpening;});
 if(bRemoveSelected || bUpdateSelected)
 {
  if(!SelectedWallOpeningSource.IsSet() || SelectedIndex==INDEX_NONE
   || !FEHBCutOperation::StaticStruct()->CompareScriptStruct(&Cuts[SelectedIndex],&SelectedWallOpeningSource.GetValue(),0))
  {WallOpeningFeedback=LOCTEXT("StaleWallOpening","开口已变化或不存在，请重新选择后再编辑。");return FReply::Handled();}
 }
 FGuid AppliedGuid;
 if(bRemoveSelected) Cuts.RemoveAt(SelectedIndex);
 else
 {
  const auto R=WallOpeningRectangle;
  for(int32 I=0;I<4;++I) if(!FMath::IsFinite(R[I]) || R[I]<(I<2?0.f:1.f) || R[I]>100000.f)
  {WallOpeningFeedback=LOCTEXT("InvalidWallRectangle","请输入有效的厘米数值；宽度和高度至少为1。");return FReply::Handled();}
  TArray<FEHBLogicalSurfaceDefinition> Hosts;FName Status;
  if(!W->QueryLogicalBaseSurfaces(Hosts,Status)||Hosts.Num()!=2){WallOpeningFeedback=LOCTEXT("UnsupportedBoundOpeningWall","请选择普通直墙创建开口。");return FReply::Handled();}
  const auto* Host=Hosts.FindByPredicate([&](const auto& H){return H.SurfaceGuid==W->FindLogicalSurfaceIdentity(TEXT("Wall.Left"));});if(!Host)return FReply::Handled();
  FEHBCutOperation Cut=bUpdateSelected?Cuts[SelectedIndex]:FEHBCutOperation();
  if(!bUpdateSelected)
  {
   Cut.SurfaceHost.Version=1;Cut.SurfaceHost.BuildingGuid=Host->BuildingGuid;Cut.SurfaceHost.ElementGuid=Host->ElementGuid;Cut.SurfaceHost.SurfaceGuid=Host->SurfaceGuid;
   Cut.Stage=EEHBCutStage::SurfaceOpening;Cut.ProjectionMode=EEHBCutProjectionMode::TargetPlane;Cut.TransformPolicy=EEHBCutTransformPolicy::FollowTargetElement;Cut.Source.Height=W->Thickness;
   Cut.Source.ExplicitPolygon.Points.SetNum(4);
  }
  const FVector Points[]={FVector(R.X,R.Y,0),FVector(R.X+R.Z,R.Y,0),FVector(R.X+R.Z,R.Y+R.W,0),FVector(R.X,R.Y+R.W,0)};
  for(int32 I=0;I<4;++I) Cut.Source.ExplicitPolygon.Points[I].LocalPosition=Points[I];
  Cut.EnsureGuids();AppliedGuid=Cut.OperationGuid;
  if(bUpdateSelected) Cuts[SelectedIndex]=MoveTemp(Cut); else Cuts.Add(MoveTemp(Cut));
 }
 const auto Result=UEHBBuildingToolset::SetWallOpenings(W,Cuts,W->OwningBuilding->RelationshipGraphRevision,W->OwningBuilding->GetElementGeometryRevision(W->ElementGuid));
 if(Result.bSucceeded)
 {
  if(bRemoveSelected){SelectedWallOpening.Invalidate();SelectedWallOpeningSource.Reset();}
  else SelectWallOpening(AppliedGuid);
  RedrawEditorViewports();
 }
 WallOpeningFeedback=Result.bSucceeded?LOCTEXT("BoundOpeningApplied","开口已更新，可使用撤销恢复。"):
 LOCTEXT("BoundOpeningFailed","无法应用：请检查开口是否位于墙面内。旧门窗、曲墙和采样墙暂不支持此入口。");
 return FReply::Handled();
}

TSharedRef<SWidget> SEHBElementEditorPanel::BuildIndependentRegionControls()
{
 return SNew(SBorder).Padding(8).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
 .Visibility_Lambda([this]{return EHBFinishRegionCommand::IsIndependent(Cast<AEHBElementActorBase>(SelectedActor.Get()))?EVisibility::Visible:EVisibility::Collapsed;})
 [SNew(SVerticalBox)
  +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("IndependentRegionTitle","独立地面区域"))]
  +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("IndependentRegionHelp","尺寸单位：厘米。沿构件自身 X / Y 方向、以轮廓中心调整，保留高度与材质。"))]
  +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("IndependentX","X 宽度"))]
   +SHorizontalBox::Slot().FillWidth(1).Padding(8,0)[SNew(SSpinBox<float>).MinValue(1).MaxValue(100000).Value(this,&SEHBElementEditorPanel::GetIndependentRegionSize,true).OnValueCommitted(this,&SEHBElementEditorPanel::HandleIndependentRegionSize,true)]]
  +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("IndependentY","Y 深度"))]
   +SHorizontalBox::Slot().FillWidth(1).Padding(8,0)[SNew(SSpinBox<float>).MinValue(1).MaxValue(100000).Value(this,&SEHBElementEditorPanel::GetIndependentRegionSize,false).OnValueCommitted(this,&SEHBElementEditorPanel::HandleIndependentRegionSize,false)]]
  +SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(LOCTEXT("IndependentRebind","重新填满覆盖的房间"))
   .ToolTipText(LOCTEXT("IndependentRebindTip","当前区域只覆盖一个房间时，按该房间重建轮廓并恢复随墙调整；覆盖多个房间时保留原区域。"))
   .OnClicked(this,&SEHBElementEditorPanel::HandleIndependentRegionBind)]];
}
float SEHBElementEditorPanel::GetIndependentRegionSize(bool bX) const
{
 const auto P=EHBFinishRegionCommand::GetPolygon(Cast<AEHBElementActorBase>(SelectedActor.Get()));if(P.IsEmpty())return 0;
 FBox Box(P);return bX?Box.GetSize().X:Box.GetSize().Y;
}
void SEHBElementEditorPanel::HandleIndependentRegionSize(float Value,ETextCommit::Type Commit,bool bX)
{
 if(Commit==ETextCommit::OnCleared||!FMath::IsFinite(Value)||Value<1)return;
 auto* E=Cast<AEHBElementActorBase>(SelectedActor.Get());if(!EHBFinishRegionCommand::IsIndependent(E))return;
 auto P=EHBFinishRegionCommand::GetPolygon(E);FBox Box(P);const int32 Axis=bX?0:1;const double Size=Box.GetSize()[Axis];if(Size<=0||FMath::IsNearlyEqual(Size,double(Value),0.001))return;
 for(auto& V:P)V[Axis]=Box.GetCenter()[Axis]+(V[Axis]-Box.GetCenter()[Axis])*Value/Size;
 auto* B=E->OwningBuilding.Get();const auto Result=UEHBBuildingToolset::EditFinishRegion(B,E->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(E->ElementGuid),P,{},false);
 FNotificationInfo Notice(EHBFinishRegionCommand::DescribeResult(Result));Notice.ExpireDuration=5;FSlateNotificationManager::Get().AddNotification(Notice);
}
FReply SEHBElementEditorPanel::HandleIndependentRegionBind()
{
 auto* E=Cast<AEHBElementActorBase>(SelectedActor.Get());if(!EHBFinishRegionCommand::IsIndependent(E))return FReply::Handled();auto* B=E->OwningBuilding.Get();FEHBSurfaceRoomCoverage Coverage;FName Status;
 if(!B->QuerySurfaceRoomCoverage(E->ElementGuid,Coverage,Status)||Coverage.Rooms.Num()!=1)
 {FNotificationInfo Notice(LOCTEXT("IndependentRoomAmbiguous","当前区域需只覆盖一个完整房间。请先闭合墙体，或缩小区域到要填充的房间内。"));Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);return FReply::Handled();}
 const auto Result=UEHBBuildingToolset::EditFinishRegion(B,E->ElementGuid,B->RelationshipGraphRevision,B->GetElementGeometryRevision(E->ElementGuid),{},Coverage.Rooms[0].RoomGuid,false);
 FNotificationInfo Notice(EHBFinishRegionCommand::DescribeResult(Result));Notice.ExpireDuration=5;FSlateNotificationManager::Get().AddNotification(Notice);return FReply::Handled();
}
#undef LOCTEXT_NAMESPACE
