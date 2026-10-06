// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SEHBMeshSamplingPanel.h"

#include "Actors/EHB_DoorWindow.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "EasyHouseEditorMode.h"
#include "PropertyCustomizationHelpers.h"
#include "Sampling/EHBMeshSampleValidation.h"
#include "ScopedTransaction.h"
#include "Selection.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SEHBMeshSamplingPanel"

namespace EHBMeshSamplingPanel
{
	FText AxisToText(EEHBWallMeshSampleAxis Axis)
	{
		switch (Axis)
		{
		case EEHBWallMeshSampleAxis::X:
			return LOCTEXT("AxisX", "X");
		case EEHBWallMeshSampleAxis::Y:
			return LOCTEXT("AxisY", "Y");
		case EEHBWallMeshSampleAxis::Z:
			return LOCTEXT("AxisZ", "Z");
		default:
			return LOCTEXT("AxisUnknown", "未知");
		}
	}

	UDataTable* LoadConfiguredDataTable(const TSoftObjectPtr<UDataTable>& ConfigValue, UScriptStruct* ExpectedStruct)
	{
		UDataTable* Table = ConfigValue.IsNull() ? nullptr : ConfigValue.LoadSynchronous();
		return Table && Table->GetRowStruct() == ExpectedStruct ? Table : nullptr;
	}

	void SaveConfiguredDataTable(
		TSoftObjectPtr<UDataTable> UEHBBuildingToolsetSettings::* SettingMember,
		UDataTable* DataTable)
	{
		UEHBBuildingToolsetSettings* Settings = GetMutableDefault<UEHBBuildingToolsetSettings>();
		if (!Settings || !SettingMember)
		{
			return;
		}

		(Settings->*SettingMember) = DataTable;
		Settings->SaveConfig();
		Settings->TryUpdateDefaultConfigFile(TEXT(""), false);
	}

	void SaveConfiguredStaticMesh(
		TSoftObjectPtr<UStaticMesh> UEHBBuildingToolsetSettings::* SettingMember,
		UStaticMesh* StaticMesh)
	{
		UEHBBuildingToolsetSettings* Settings = GetMutableDefault<UEHBBuildingToolsetSettings>();
		if (!Settings || !SettingMember)
		{
			return;
		}

		(Settings->*SettingMember) = StaticMesh;
		Settings->SaveConfig();
		Settings->TryUpdateDefaultConfigFile(TEXT(""), false);
	}
}

void SEHBMeshSamplingPanel::Construct(const FArguments& InArgs)
{
	WallSamplingStatus = LOCTEXT("InitialWallSamplingStatus", "请选择墙面数据表和静态网格体。");
	PillarSamplingStatus = LOCTEXT("InitialPillarSamplingStatus", "请选择柱体数据表和静态网格体。");
	RailingSamplingStatus = LOCTEXT("InitialRailingSamplingStatus", "\u8bf7\u9009\u62e9\u6276\u624b\u91c7\u6837\u8868\uff0c\u5e76\u5728\u7acb\u67f1\u7f51\u683c\u4f53\u548c\u6a2a\u6746\u7f51\u683c\u4f53\u4e2d\u81f3\u5c11\u9009\u62e9\u4e00\u4e2a\u3002");
	RoofSamplingStatus = LOCTEXT("InitialRoofSamplingStatus", "\u8bf7\u9009\u62e9\u5c4b\u9876\u91c7\u6837\u8868\u548c\u5c4b\u9762\u74e6\u7247\u7f51\u683c\u4f53\u3002");
	DoorWindowSamplingStatus = LOCTEXT("InitialDoorWindowSamplingStatus", "请选择门窗静态网格体，并确认要生成的蓝图父类和资产名称。");

	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	DoorWindowBaseClass = ToolsetSettings && !ToolsetSettings->DoorWindowActorClass.IsNull()
		? ToolsetSettings->DoorWindowActorClass.LoadSynchronous()
		: AEHB_DoorWindow::StaticClass();
	if (ToolsetSettings)
	{
		WallDataTable = EHBMeshSamplingPanel::LoadConfiguredDataTable(
			ToolsetSettings->DefaultWallMeshDataTable,
			FEHBWallMeshData::StaticStruct());
		PillarDataTable = EHBMeshSamplingPanel::LoadConfiguredDataTable(
			ToolsetSettings->DefaultPillarMeshDataTable,
			FEHBPillarMeshData::StaticStruct());
		RailingDataTable = EHBMeshSamplingPanel::LoadConfiguredDataTable(
			ToolsetSettings->DefaultRailingMeshDataTable,
			FEHBRailingMeshData::StaticStruct());
		RoofDataTable = EHBMeshSamplingPanel::LoadConfiguredDataTable(
			ToolsetSettings->DefaultRoofMeshDataTable,
			FEHBRoofMeshData::StaticStruct());
		RoofSurfaceStaticMesh = ToolsetSettings->DefaultRoofSurfaceTileMesh.IsNull()
			? nullptr
			: ToolsetSettings->DefaultRoofSurfaceTileMesh.LoadSynchronous();
		RoofRidgeStaticMesh = ToolsetSettings->DefaultRoofRidgeTileMesh.IsNull()
			? nullptr
			: ToolsetSettings->DefaultRoofRidgeTileMesh.LoadSynchronous();
		RoofValleyStaticMesh = ToolsetSettings->DefaultRoofValleyTileMesh.IsNull()
			? nullptr
			: ToolsetSettings->DefaultRoofValleyTileMesh.LoadSynchronous();
		DoorWindowDataTable = EHBMeshSamplingPanel::LoadConfiguredDataTable(
			ToolsetSettings->DefaultDoorWindowMeshDataTable,
			FEHBDoorWindowMeshData::StaticStruct());
	}
	if (RoofSurfaceStaticMesh.IsValid())
	{
		RefreshRoofAnalysis();
	}

	TSharedRef<SWidgetSwitcher> Switcher =
		SAssignNew(PageSwitcher, SWidgetSwitcher)
		.WidgetIndex(static_cast<int32>(ActivePage));

	Switcher->AddSlot()
	[
		BuildWallSamplingPage()
	];

	Switcher->AddSlot()
	[
		BuildPillarSamplingPage()
	];

	Switcher->AddSlot()
	[
		BuildRailingSamplingPage()
	];

	Switcher->AddSlot()
	[
		BuildRoofSamplingPage()
	];

	Switcher->AddSlot()
	[
		BuildDoorWindowSamplingPage()
	];

	ChildSlot
	[
		SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background)
		[
			SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
				SNew(STextBlock)
				.Text(LOCTEXT("MeshSamplingPanelTitle", "网格体采样"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("MeshSamplingPanelTitleTip", "用于从已有静态网格体中提取建筑工具可复用的采样数据。墙面采样会保存正反两侧墙面数据；柱体采样会把完整源网格体保存为一个柱体模板行。"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 8.0f)
			[
				SNew(SWrapBox)
				.UseAllottedSize(true)
				.InnerSlotPadding(FVector2D(6.0f, 6.0f))

				+ SWrapBox::Slot()
				[
					BuildSamplingTabButton(EEHBMeshSamplingPage::Wall, LOCTEXT("WallSamplingTab", "墙面采样"))
				]

				+ SWrapBox::Slot()
				[
					BuildSamplingTabButton(EEHBMeshSamplingPage::Pillar, LOCTEXT("PillarSamplingTab", "柱体采样"))
				]

				+ SWrapBox::Slot()
				[
					BuildSamplingTabButton(EEHBMeshSamplingPage::Railing, LOCTEXT("RailingSamplingTab", "扶手采样"))
				]

				+ SWrapBox::Slot()
				[
					BuildSamplingTabButton(EEHBMeshSamplingPage::Roof, LOCTEXT("RoofSamplingTab", "\u5c4b\u9876\u91c7\u6837"))
				]

				+ SWrapBox::Slot()
				[
					BuildSamplingTabButton(EEHBMeshSamplingPage::DoorWindow, LOCTEXT("DoorWindowSamplingTab", "门窗采样"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				Switcher
			]
		]
	];
}

TSharedRef<SWidget> SEHBMeshSamplingPanel::BuildSamplingTabButton(EEHBMeshSamplingPage Page, const FText& Label)
{
	return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
		.IsChecked(this, &SEHBMeshSamplingPanel::IsSamplingPageChecked, Page)
		.OnCheckStateChanged(this, &SEHBMeshSamplingPanel::HandleSamplingPageChanged, Page)
		.ToolTipText(Label)
		[
			SNew(SBox)
			.MinDesiredWidth(92.0f)
			.HeightOverride(28.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FStyleColors::Foreground)
			]
		];
}

TSharedRef<SWidget> SEHBMeshSamplingPanel::BuildWallSamplingPage()
{
	return SNew(SScrollBox)

		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								BuildField(
					LOCTEXT("WallDataTableLabel", "墙面数据表"),
					LOCTEXT("WallDataTableTip", "选择用于保存墙面采样结果的数据表。请在内容浏览器中新建数据表，并将“行结构”设置为“墙面网格体数据 / FEHBWallMeshData”。采样成功后会写入一条记录，正面和反面数据会保存在该行内部。"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UDataTable::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetWallDataTablePath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleWallDataTableChanged)
					.OnShouldFilterAsset(this, &SEHBMeshSamplingPanel::ShouldFilterWallDataTable)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("WallStaticMeshLabel", "静态网格体"),
					LOCTEXT("WallStaticMeshTip", "选择要作为墙面模板采样的静态网格体。采样器会读取指定 LOD 的源网格数据，自动判断 Z 轴高度、XY 长轴宽度、XY 短轴厚度。"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UStaticMesh::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetWallStaticMeshPath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleWallStaticMeshChanged)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(this, &SEHBMeshSamplingPanel::GetWallMeshInfoText)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
					.ToolTipText(LOCTEXT("WallMeshInfoTip", "显示当前静态网格体的墙面检测信息。高度固定取 Z 轴，宽度取 XY 中较长轴，厚度取 XY 中较短轴。"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("WallRowNameLabel", "名称"),
					LOCTEXT("WallRowNameTip", "数据表行名。采样写入时会使用这个名称创建或覆盖一条墙面模板记录，正面和反面会分别保存在该行的“正面采样”和“反面采样”字段中。"),
					SNew(SEditableTextBox)
					.Text(this, &SEHBMeshSamplingPanel::GetWallRowNameText)
					.OnTextChanged(this, &SEHBMeshSamplingPanel::HandleWallRowNameChanged)
					.SelectAllTextWhenFocused(true))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AdvancedHeader", "高级"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(FStyleColors::Foreground)
				]
				.BodyContent()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("LODIndexLabel", "LOD 索引"),
							LOCTEXT("LODIndexTip", "采样时读取源静态网格体的 LOD 索引。默认使用 LOD0。"),
							SNew(SSpinBox<int32>)
							.MinValue(0)
							.MaxValue(16)
							.Value_Lambda([this]() { return WallSamplingOptions.LODIndex; })
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								WallSamplingOptions.LODIndex = FMath::Max(0, NewValue);
								RefreshWallAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("MaxTriangleCountLabel", "最大源三角面数"),
							LOCTEXT("MaxTriangleCountTip", "模型允许作为墙面模板的最大三角面数量。数值越低，越能避免把复杂雕花、整栋建筑或高面数模型误当成墙面。"),
							SNew(SSpinBox<int32>)
							.MinValue(1)
							.MaxValue(1000000)
							.Value_Lambda([this]() { return WallSamplingOptions.MaxTriangleCount; })
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								WallSamplingOptions.MaxTriangleCount = FMath::Max(1, NewValue);
								RefreshWallAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("ThicknessRatioLabel", "最大厚宽比"),
							LOCTEXT("ThicknessRatioTip", "厚度除以宽度的最大允许值。默认 0.25 表示厚度不能超过宽度的四分之一，用于判断模型是否整体像一片薄墙。"),
							SNew(SSpinBox<float>)
							.MinValue(0.01f)
							.MaxValue(1.0f)
							.Delta(0.01f)
							.Value_Lambda([this]() { return WallSamplingOptions.MaxThicknessToWidthRatio; })
							.OnValueChanged_Lambda([this](float NewValue)
							{
								WallSamplingOptions.MaxThicknessToWidthRatio = FMath::Clamp(NewValue, 0.01f, 1.0f);
								RefreshWallAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("SideFaceNormalAlignmentLabel", "侧面法线阈值"),
							LOCTEXT("SideFaceNormalAlignmentTip", "用于识别正反墙面的法线阈值。三角面的几何法线越接近厚度轴 Y，就越像正面或反面；顶面、底面和端面会因为主要朝向 Z 或 X 而被排除。数值越高过滤越严格。"),
							SNew(SSpinBox<float>)
							.MinValue(0.01f)
							.MaxValue(1.0f)
							.Delta(0.01f)
							.Value_Lambda([this]() { return WallSamplingOptions.MinSideFaceNormalAlignment; })
							.OnValueChanged_Lambda([this](float NewValue)
							{
								WallSamplingOptions.MinSideFaceNormalAlignment = FMath::Clamp(NewValue, 0.01f, 1.0f);
								RefreshWallAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("MinWallWidthLabel", "最小墙面宽度"),
							LOCTEXT("MinWallWidthTip", "墙面宽度的最小允许值，单位厘米。低于该值时会认为模型尺寸太小，不适合作为墙面模板。"),
							SNew(SSpinBox<float>)
							.MinValue(0.0f)
							.MaxValue(100000.0f)
							.Value_Lambda([this]() { return WallSamplingOptions.MinWallWidth; })
							.OnValueChanged_Lambda([this](float NewValue)
							{
								WallSamplingOptions.MinWallWidth = FMath::Max(0.0f, NewValue);
								RefreshWallAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("MinWallHeightLabel", "最小墙面高度"),
							LOCTEXT("MinWallHeightTip", "墙面高度的最小允许值，单位厘米。采样器固定将源网格体 Z 轴尺寸作为高度。"),
							SNew(SSpinBox<float>)
							.MinValue(0.0f)
							.MaxValue(100000.0f)
							.Value_Lambda([this]() { return WallSamplingOptions.MinWallHeight; })
							.OnValueChanged_Lambda([this](float NewValue)
							{
								WallSamplingOptions.MinWallHeight = FMath::Max(0.0f, NewValue);
								RefreshWallAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("MinWallThicknessLabel", "最小墙面厚度"),
							LOCTEXT("MinWallThicknessTip", "墙面厚度的最小允许值，单位厘米。需要有真实厚度，才能沿中心线拆出正面和反面。"),
							SNew(SSpinBox<float>)
							.MinValue(0.0f)
							.MaxValue(100000.0f)
							.Value_Lambda([this]() { return WallSamplingOptions.MinWallThickness; })
							.OnValueChanged_Lambda([this](float NewValue)
							{
								WallSamplingOptions.MinWallThickness = FMath::Max(0.0f, NewValue);
								RefreshWallAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("MaxComponentCountLabel", "最大分离网格块"),
							LOCTEXT("MaxComponentCountTip", "源网格体中允许的最大互不相连网格块数量。过高通常表示模型过碎，不适合作为基础墙面模板。"),
							SNew(SSpinBox<int32>)
							.MinValue(1)
							.MaxValue(100000)
							.Value_Lambda([this]() { return WallSamplingOptions.MaxDisconnectedComponentCount; })
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								WallSamplingOptions.MaxDisconnectedComponentCount = FMath::Max(1, NewValue);
								RefreshWallAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("OverwriteRowsLabel", "覆盖同名行"),
							LOCTEXT("OverwriteRowsTip", "开启后，采样会覆盖数据表中已有的同名墙面模板行；关闭后遇到同名行会停止并提示。"),
							SNew(SCheckBox)
							.IsChecked_Lambda([this]()
							{
								return WallSamplingOptions.bOverwriteExistingRows ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
							{
								WallSamplingOptions.bOverwriteExistingRows = NewState == ECheckBoxState::Checked;
							})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("OverwriteRowsCheckText", "允许覆盖已有墙面模板行"))
							])
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 10.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SampleWallButton", "采样"))
				.ToolTipText(LOCTEXT("SampleWallButtonTip", "开始墙面采样。采样器会检测源静态网格体是否像较薄的矩形墙面，通过后只采样厚度方向的正反两个侧面；顶面、底面和端面不会写入数据表。"))
				.HAlign(HAlign_Center)
				.IsEnabled(this, &SEHBMeshSamplingPanel::CanSampleWall)
				.OnClicked(this, &SEHBMeshSamplingPanel::HandleSampleWallClicked)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(this, &SEHBMeshSamplingPanel::GetWallSamplingStatusText)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
					.ToolTipText(LOCTEXT("WallSamplingStatusTip", "显示最近一次墙面检测或采样写表的结果。"))
				]
			]
		];
}

TSharedRef<SWidget> SEHBMeshSamplingPanel::BuildPillarSamplingPage()
{
	return SNew(SScrollBox)

		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("PillarDataTableLabel", "柱体数据表"),
					LOCTEXT("PillarDataTableTip", "选择用于保存柱体采样结果的数据表。请在内容浏览器中新建数据表，并将“行结构”设置为“柱体网格体数据 / FEHBPillarMeshData”。采样成功后会写入一条记录，记录中保存完整源网格体的顶点、三角形和材质索引。"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UDataTable::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetPillarDataTablePath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandlePillarDataTableChanged)
					.OnShouldFilterAsset(this, &SEHBMeshSamplingPanel::ShouldFilterPillarDataTable)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("PillarStaticMeshLabel", "静态网格体"),
					LOCTEXT("PillarStaticMeshTip", "选择要作为柱体模板采样的静态网格体。柱体采样会读取指定 LOD 的完整源网格数据，并保留所有可读取三角面。"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UStaticMesh::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetPillarStaticMeshPath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandlePillarStaticMeshChanged)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(this, &SEHBMeshSamplingPanel::GetPillarMeshInfoText)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
					.ToolTipText(LOCTEXT("PillarMeshInfoTip", "显示当前静态网格体的柱体检测信息，包括包围盒尺寸、源顶点数、源三角面数和分离网格块数量。"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("PillarRowNameLabel", "名称"),
					LOCTEXT("PillarRowNameTip", "数据表行名。采样写入时会使用这个名称创建或覆盖一条柱体模板记录。"),
					SNew(SEditableTextBox)
					.Text(this, &SEHBMeshSamplingPanel::GetPillarRowNameText)
					.OnTextChanged(this, &SEHBMeshSamplingPanel::HandlePillarRowNameChanged)
					.SelectAllTextWhenFocused(true))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("PillarAdvancedHeader", "高级"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(FStyleColors::Foreground)
				]
				.BodyContent()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("PillarLODIndexLabel", "LOD 索引"),
							LOCTEXT("PillarLODIndexTip", "采样时读取源静态网格体的 LOD 索引。默认使用 LOD0。"),
							SNew(SSpinBox<int32>)
							.MinValue(0)
							.MaxValue(16)
							.Value_Lambda([this]() { return PillarSamplingOptions.LODIndex; })
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								PillarSamplingOptions.LODIndex = FMath::Max(0, NewValue);
								RefreshPillarAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("PillarMaxTriangleCountLabel", "最大源三角面数"),
							LOCTEXT("PillarMaxTriangleCountTip", "模型允许作为柱体模板的最大三角面数量。数值越低，越能避免把过高面数模型误采样进数据表。"),
							SNew(SSpinBox<int32>)
							.MinValue(1)
							.MaxValue(1000000)
							.Value_Lambda([this]() { return PillarSamplingOptions.MaxTriangleCount; })
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								PillarSamplingOptions.MaxTriangleCount = FMath::Max(1, NewValue);
								RefreshPillarAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("PillarOverwriteRowsLabel", "覆盖同名行"),
							LOCTEXT("PillarOverwriteRowsTip", "开启后，采样会覆盖数据表中已有的同名柱体模板行；关闭后遇到同名行会停止并提示。"),
							SNew(SCheckBox)
							.IsChecked_Lambda([this]()
							{
								return PillarSamplingOptions.bOverwriteExistingRows ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
							{
								PillarSamplingOptions.bOverwriteExistingRows = NewState == ECheckBoxState::Checked;
							})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PillarOverwriteRowsCheckText", "允许覆盖已有柱体模板行"))
							])
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 10.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SamplePillarButton", "采样"))
				.ToolTipText(LOCTEXT("SamplePillarButtonTip", "开始柱体采样。采样器会读取源静态网格体的完整三角面数据，并写入柱体数据表中的一行。"))
				.HAlign(HAlign_Center)
				.IsEnabled(this, &SEHBMeshSamplingPanel::CanSamplePillar)
				.OnClicked(this, &SEHBMeshSamplingPanel::HandleSamplePillarClicked)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(this, &SEHBMeshSamplingPanel::GetPillarSamplingStatusText)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
					.ToolTipText(LOCTEXT("PillarSamplingStatusTip", "显示最近一次柱体检测或采样写表的结果。"))
				]
			]
		];
}

TSharedRef<SWidget> SEHBMeshSamplingPanel::BuildRailingSamplingPage()
{
	return SNew(SScrollBox)

		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("RailingDataTableLabel", "\u6276\u624b\u91c7\u6837\u8868"),
					LOCTEXT("RailingDataTableTip", "\u7528\u4e8e\u4fdd\u5b58\u6276\u624b\u91c7\u6837\u7ed3\u679c\u7684\u6570\u636e\u8868\u3002\u884c\u7ed3\u6784\u5fc5\u987b\u662f FEHBRailingMeshData\u3002"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UDataTable::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetRailingDataTablePath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleRailingDataTableChanged)
					.OnShouldFilterAsset(this, &SEHBMeshSamplingPanel::ShouldFilterRailingDataTable)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("RailingPostMeshLabel", "\u7acb\u67f1\u7f51\u683c\u4f53"),
					LOCTEXT("RailingPostMeshTip", "\u9009\u586b\u3002\u9009\u62e9\u8981\u4f5c\u4e3a\u6276\u624b\u7acb\u67f1\u6837\u5f0f\u7684\u9759\u6001\u7f51\u683c\u4f53\uff1b\u7acb\u67f1\u548c\u6a2a\u6746\u81f3\u5c11\u586b\u4e00\u4e2a\u3002"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UStaticMesh::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetRailingPostStaticMeshPath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleRailingPostStaticMeshChanged)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("RailingRailMeshLabel", "\u6a2a\u6746\u7f51\u683c\u4f53"),
					LOCTEXT("RailingRailMeshTip", "\u9009\u586b\u3002\u9009\u62e9\u8981\u4f5c\u4e3a\u6c34\u5e73\u6a2a\u6746\u6837\u5f0f\u7684\u9759\u6001\u7f51\u683c\u4f53\u3002\u5efa\u8bae\u4f7f\u7528 X \u8f74\u4f5c\u4e3a\u957f\u5ea6\u65b9\u5411\uff1b\u7acb\u67f1\u548c\u6a2a\u6746\u81f3\u5c11\u586b\u4e00\u4e2a\u3002"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UStaticMesh::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetRailingRailStaticMeshPath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleRailingRailStaticMeshChanged)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("RailingPanelMeshLabel", "\u56f4\u677f\u7f51\u683c\u4f53"),
					LOCTEXT("RailingPanelMeshTip", "\u53ef\u9009\u3002\u7528\u4e8e\u91c7\u6837\u56f4\u677f\u7f51\u683c\u3001\u6750\u8d28\u548c\u9ad8\u5ea6\u5efa\u8bae\u3002\u56f4\u677f\u9ed8\u8ba4\u4e3a\u7ad6\u76f4\u6a21\u578b\uff0cZ \u8f74\u662f\u9ad8\u5ea6\uff0cX/Y \u91cc\u66f4\u957f\u7684\u4e00\u8fb9\u4f1a\u4f5c\u4e3a\u6cbf\u6276\u624b\u65b9\u5411\u7684\u5bbd\u5ea6\u3002"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UStaticMesh::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetRailingPanelStaticMeshPath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleRailingPanelStaticMeshChanged)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(this, &SEHBMeshSamplingPanel::GetRailingMeshInfoText)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("RailingRowNameLabel", "\u91c7\u6837\u9879\u540d\u79f0"),
					LOCTEXT("RailingRowNameTip", "\u5199\u5165\u6570\u636e\u8868\u7684\u6276\u624b\u91c7\u6837\u884c\u540d\u79f0\u3002"),
					SNew(SEditableTextBox)
					.Text(this, &SEHBMeshSamplingPanel::GetRailingRowNameText)
					.OnTextChanged(this, &SEHBMeshSamplingPanel::HandleRailingRowNameChanged)
					.SelectAllTextWhenFocused(true))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RailingAdvancedHeader", "\u9ad8\u7ea7\u53c2\u6570"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(FStyleColors::Foreground)
				]
				.BodyContent()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("RailingLODIndexLabel", "LOD \u7d22\u5f15"),
							LOCTEXT("RailingLODIndexTip", "\u4ece\u6240\u9009\u6276\u624b\u7f51\u683c\u4f53\u8bfb\u53d6\u7684\u6e90 LOD \u7d22\u5f15\u3002"),
							SNew(SSpinBox<int32>)
							.MinValue(0)
							.MaxValue(16)
							.Value_Lambda([this]() { return RailingSamplingOptions.LODIndex; })
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								RailingSamplingOptions.LODIndex = FMath::Max(0, NewValue);
								RefreshRailingAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("RailingMaxTriangleCountLabel", "\u5355\u4e2a\u7f51\u683c\u6700\u5927\u4e09\u89d2\u9762\u6570"),
							LOCTEXT("RailingMaxTriangleCountTip", "\u6bcf\u4e2a\u6240\u9009\u6276\u624b\u7f51\u683c\u4f53\u5141\u8bb8\u7684\u6e90\u4e09\u89d2\u9762\u6570\u4e0a\u9650\u3002"),
							SNew(SSpinBox<int32>)
							.MinValue(1)
							.MaxValue(1000000)
							.Value_Lambda([this]() { return RailingSamplingOptions.MaxTriangleCountPerMesh; })
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								RailingSamplingOptions.MaxTriangleCountPerMesh = FMath::Max(1, NewValue);
								RefreshRailingAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("RailingPostSpacingLabel", "\u63a8\u8350\u7acb\u67f1\u95f4\u8ddd"),
							LOCTEXT("RailingPostSpacingTip", "\u5199\u5165\u6276\u624b\u91c7\u6837\u884c\u7684\u63a8\u8350\u7acb\u67f1\u95f4\u8ddd\u3002"),
							SNew(SSpinBox<float>)
							.MinValue(1.0f)
							.MaxValue(100000.0f)
							.Value_Lambda([this]() { return RailingSamplingOptions.PostSpacing; })
							.OnValueChanged_Lambda([this](float NewValue)
							{
								RailingSamplingOptions.PostSpacing = FMath::Max(1.0f, NewValue);
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("RailingUsePanelLabel", "\u751f\u6210\u56f4\u677f"),
							LOCTEXT("RailingUsePanelTip", "\u542f\u7528\u540e\uff0c\u91c7\u6837\u884c\u4f1a\u5efa\u8bae\u4f7f\u7528\u7acb\u67f1\u3001\u6a2a\u6746\u548c\u7a0b\u5e8f\u5316\u56f4\u677f\u3002"),
							SNew(SCheckBox)
							.IsChecked_Lambda([this]()
							{
								return RailingSamplingOptions.FillMode == EEHBRailingFillMode::PostsRailsAndPanel
									? ECheckBoxState::Checked
									: ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
							{
								RailingSamplingOptions.FillMode = NewState == ECheckBoxState::Checked
									? EEHBRailingFillMode::PostsRailsAndPanel
									: EEHBRailingFillMode::PostsAndRails;
								RefreshRailingAnalysis();
							})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RailingUsePanelCheckText", "\u4f7f\u7528\u56f4\u677f\u586b\u5145"))
							])
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("RailingOverwriteRowsLabel", "\u8986\u76d6\u540c\u540d\u884c"),
							LOCTEXT("RailingOverwriteRowsTip", "\u5141\u8bb8\u91c7\u6837\u65f6\u8986\u76d6\u540c\u540d\u6276\u624b\u6837\u5f0f\u884c\u3002"),
							SNew(SCheckBox)
							.IsChecked_Lambda([this]()
							{
								return RailingSamplingOptions.bOverwriteExistingRows ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
							{
								RailingSamplingOptions.bOverwriteExistingRows = NewState == ECheckBoxState::Checked;
							})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RailingOverwriteRowsCheckText", "\u5141\u8bb8\u8986\u76d6"))
							])
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 10.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SampleRailingButton", "\u751f\u6210\u6276\u624b\u91c7\u6837"))
				.ToolTipText(LOCTEXT("SampleRailingButtonTip", "\u5c06\u6240\u9009\u7acb\u67f1\u3001\u6a2a\u6746\u548c\u53ef\u9009\u56f4\u677f\u6765\u6e90\u7f51\u683c\u4f53\u91c7\u6837\u5230\u4e00\u6761\u6276\u624b\u6837\u5f0f\u884c\u4e2d\u3002\u7acb\u67f1\u548c\u6a2a\u6746\u53ef\u9009\u586b\uff0c\u4f46\u81f3\u5c11\u9700\u8981\u4e00\u4e2a\u3002"))
				.HAlign(HAlign_Center)
				.IsEnabled(this, &SEHBMeshSamplingPanel::CanSampleRailing)
				.OnClicked(this, &SEHBMeshSamplingPanel::HandleSampleRailingClicked)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(this, &SEHBMeshSamplingPanel::GetRailingSamplingStatusText)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]
		];
}

TSharedRef<SWidget> SEHBMeshSamplingPanel::BuildRoofSamplingPage()
{
	return SNew(SScrollBox)

		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("RoofDataTableLabel", "\u5c4b\u9876\u91c7\u6837\u8868"),
					LOCTEXT("RoofDataTableTip", "\u9009\u62e9\u7528\u4e8e\u4fdd\u5b58\u5c4b\u9876\u91c7\u6837\u7ed3\u679c\u7684\u6570\u636e\u8868\uff0c\u884c\u7ed3\u6784\u5fc5\u987b\u662f FEHBRoofMeshData\u3002"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UDataTable::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetRoofDataTablePath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleRoofDataTableChanged)
					.OnShouldFilterAsset(this, &SEHBMeshSamplingPanel::ShouldFilterRoofDataTable)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("RoofSurfaceMeshLabel", "\u5c4b\u9762\u74e6\u7247\u7f51\u683c\u4f53"),
					LOCTEXT("RoofSurfaceMeshTip", "\u9009\u62e9\u4f5c\u4e3a\u5c4b\u9876\u5761\u9762\u74e6\u7247\u6a21\u677f\u7684\u9759\u6001\u7f51\u683c\u4f53\u3002"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UStaticMesh::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetRoofSurfaceStaticMeshPath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleRoofSurfaceStaticMeshChanged)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("RoofRidgeMeshLabel", "\u5c4b\u810a\u74e6\u7247\u7f51\u683c\u4f53"),
					LOCTEXT("RoofRidgeMeshTip", "\u53ef\u9009\u3002\u9009\u62e9\u4f5c\u4e3a\u5c4b\u810a\u88c5\u9970\u6761\u7684\u9759\u6001\u7f51\u683c\u4f53\u3002"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UStaticMesh::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetRoofRidgeStaticMeshPath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleRoofRidgeStaticMeshChanged)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("RoofValleyMeshLabel", "\u8c37\u7ebf\u74e6\u7247\u7f51\u683c\u4f53"),
					LOCTEXT("RoofValleyMeshTip", "\u53ef\u9009\u3002\u9009\u62e9\u4f5c\u4e3a\u5c4b\u9876\u8c37\u7ebf\u88c5\u9970\u6761\u7684\u9759\u6001\u7f51\u683c\u4f53\u3002"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UStaticMesh::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetRoofValleyStaticMeshPath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleRoofValleyStaticMeshChanged)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(this, &SEHBMeshSamplingPanel::GetRoofMeshInfoText)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("RoofRowNameLabel", "\u540d\u79f0"),
					LOCTEXT("RoofRowNameTip", "\u6570\u636e\u8868\u884c\u540d\u3002\u91c7\u6837\u5199\u5165\u65f6\u4f1a\u521b\u5efa\u6216\u8986\u76d6\u8fd9\u4e00\u884c\u3002"),
					SNew(SEditableTextBox)
					.Text(this, &SEHBMeshSamplingPanel::GetRoofRowNameText)
					.OnTextChanged(this, &SEHBMeshSamplingPanel::HandleRoofRowNameChanged)
					.SelectAllTextWhenFocused(true))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RoofSamplingAdvancedTitle", "\u9ad8\u7ea7"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]
				.BodyContent()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("RoofLODLabel", "LOD"),
							LOCTEXT("RoofLODTip", "\u8bfb\u53d6\u6e90\u7f51\u683c\u4f53\u7684 LOD \u7d22\u5f15\u3002"),
							SNew(SSpinBox<int32>)
							.MinValue(0)
							.MaxValue(8)
							.Value_Lambda([this]()
							{
								return RoofSamplingOptions.LODIndex;
							})
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								RoofSamplingOptions.LODIndex = FMath::Max(0, NewValue);
								RefreshRoofAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("RoofMaxTrianglesLabel", "\u5355\u4e2a\u7f51\u683c\u4e09\u89d2\u9762\u4e0a\u9650"),
							LOCTEXT("RoofMaxTrianglesTip", "\u8d85\u8fc7\u8fd9\u4e2a\u6570\u91cf\u7684\u5355\u4e2a\u5c4b\u9876\u7f51\u683c\u4f53\u4e0d\u4f1a\u88ab\u5199\u5165\u91c7\u6837\u8868\u3002"),
							SNew(SSpinBox<int32>)
							.MinValue(1)
							.MaxValue(1000000)
							.Value_Lambda([this]()
							{
								return RoofSamplingOptions.MaxTriangleCountPerMesh;
							})
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								RoofSamplingOptions.MaxTriangleCountPerMesh = FMath::Max(1, NewValue);
								RefreshRoofAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("RoofRecommendedLengthLabel", "\u63a8\u8350\u957f\u5ea6"),
							LOCTEXT("RoofRecommendedLengthTip", "\u5199\u5165\u91c7\u6837\u884c\u7684\u9ed8\u8ba4\u5c4b\u9876\u957f\u5ea6\u3002"),
							SNew(SSpinBox<float>)
							.MinValue(1.0f)
							.MaxValue(100000.0f)
							.Value_Lambda([this]()
							{
								return RoofSamplingOptions.RecommendedLength;
							})
							.OnValueChanged_Lambda([this](float NewValue)
							{
								RoofSamplingOptions.RecommendedLength = FMath::Max(1.0f, NewValue);
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("RoofRecommendedWidthLabel", "\u63a8\u8350\u5bbd\u5ea6"),
							LOCTEXT("RoofRecommendedWidthTip", "\u5199\u5165\u91c7\u6837\u884c\u7684\u9ed8\u8ba4\u5c4b\u9876\u5bbd\u5ea6\u3002"),
							SNew(SSpinBox<float>)
							.MinValue(1.0f)
							.MaxValue(100000.0f)
							.Value_Lambda([this]()
							{
								return RoofSamplingOptions.RecommendedWidth;
							})
							.OnValueChanged_Lambda([this](float NewValue)
							{
								RoofSamplingOptions.RecommendedWidth = FMath::Max(1.0f, NewValue);
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("RoofRecommendedPitchLabel", "\u63a8\u8350\u5761\u5ea6"),
							LOCTEXT("RoofRecommendedPitchTip", "\u5199\u5165\u91c7\u6837\u884c\u7684\u9ed8\u8ba4\u5c4b\u9876\u5761\u5ea6\u3002"),
							SNew(SSpinBox<float>)
							.MinValue(1.0f)
							.MaxValue(89.0f)
							.Value_Lambda([this]()
							{
								return RoofSamplingOptions.RecommendedPitchDegrees;
							})
							.OnValueChanged_Lambda([this](float NewValue)
							{
								RoofSamplingOptions.RecommendedPitchDegrees = FMath::Clamp(NewValue, 1.0f, 89.0f);
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]()
						{
							return RoofSamplingOptions.bOverwriteExistingRows ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
						{
							RoofSamplingOptions.bOverwriteExistingRows = NewState == ECheckBoxState::Checked;
						})
						[
							SNew(STextBlock)
							.Text(LOCTEXT("RoofOverwriteExistingRows", "\u8986\u76d6\u540c\u540d\u884c"))
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 10.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SampleRoofButton", "\u751f\u6210\u5c4b\u9876\u91c7\u6837"))
				.HAlign(HAlign_Center)
				.IsEnabled(this, &SEHBMeshSamplingPanel::CanSampleRoof)
				.OnClicked(this, &SEHBMeshSamplingPanel::HandleSampleRoofClicked)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(this, &SEHBMeshSamplingPanel::GetRoofSamplingStatusText)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]
		];
}

TSharedRef<SWidget> SEHBMeshSamplingPanel::BuildDoorWindowSamplingPage()
{
	return SNew(SScrollBox)

		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("DoorWindowDataTableLabel", "门窗数据表"),
					LOCTEXT("DoorWindowDataTableTip", "选择用于保存门窗采样结果的数据表。请在内容浏览器中新建数据表，并将“行结构”设置为“门窗网格体数据 / FEHBDoorWindowMeshData”。生成门窗蓝图成功后，会把蓝图类、源网格体和洞口尺寸写入一条记录。"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UDataTable::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetDoorWindowDataTablePath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleDoorWindowDataTableChanged)
					.OnShouldFilterAsset(this, &SEHBMeshSamplingPanel::ShouldFilterDoorWindowDataTable)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("DoorWindowStaticMeshLabel", "静态网格体"),
					LOCTEXT("DoorWindowStaticMeshTip", "选择要作为门窗模板采样的静态网格体。采样器会使用 Z 轴作为洞口高度，XY 中较长方向作为洞口宽度，较短方向作为门窗厚度。"),
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UStaticMesh::StaticClass())
					.ObjectPath(this, &SEHBMeshSamplingPanel::GetDoorWindowStaticMeshPath)
					.OnObjectChanged(this, &SEHBMeshSamplingPanel::HandleDoorWindowStaticMeshChanged)
					.AllowClear(true)
					.DisplayThumbnail(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(this, &SEHBMeshSamplingPanel::GetDoorWindowMeshInfoText)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
					.ToolTipText(LOCTEXT("DoorWindowMeshInfoTip", "显示当前静态网格体的门窗检测信息。门窗模板需要整体接近一个较薄的竖向矩形构件，后续会用这些尺寸生成墙体布尔挖洞数据。"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("DoorWindowRowNameLabel", "名称"),
					LOCTEXT("DoorWindowRowNameTip", "门窗数据表行名。生成门窗蓝图成功后，会使用这个名称创建或覆盖数据表中的一行，便于后续门窗工具从表中查找和管理模板。"),
					SNew(SEditableTextBox)
					.Text(this, &SEHBMeshSamplingPanel::GetDoorWindowRowNameText)
					.OnTextChanged(this, &SEHBMeshSamplingPanel::HandleDoorWindowRowNameChanged)
					.SelectAllTextWhenFocused(true))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("DoorWindowBaseClassLabel", "蓝图父类"),
					LOCTEXT("DoorWindowBaseClassTip", "选择要生成的门窗蓝图父类。默认使用项目设置里的门窗 Actor 类，也可以在这里临时选择 AEHB_DoorWindow 的其他子类。"),
					SNew(SClassPropertyEntryBox)
					.MetaClass(AEHB_DoorWindow::StaticClass())
					.SelectedClass(this, &SEHBMeshSamplingPanel::GetSelectedDoorWindowBaseClass)
					.OnSetClass(this, &SEHBMeshSamplingPanel::HandleDoorWindowBaseClassChanged)
					.AllowNone(false))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("DoorWindowKindLabel", "构件类型"),
					LOCTEXT("DoorWindowKindTip", "选择生成门还是窗。门的洞口底边离地高度固定为 0；窗会使用下方的离地高度作为窗台高度。"),
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
						.IsChecked(this, &SEHBMeshSamplingPanel::IsDoorWindowKindChecked, EEHBDoorWindowElementKind::Window)
						.OnCheckStateChanged(this, &SEHBMeshSamplingPanel::HandleDoorWindowKindChanged, EEHBDoorWindowElementKind::Window)
						[
							SNew(SBox)
							.MinDesiredWidth(72.0f)
							.HeightOverride(26.0f)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("DoorWindowKindWindow", "窗"))
								.Justification(ETextJustify::Center)
							]
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
						.IsChecked(this, &SEHBMeshSamplingPanel::IsDoorWindowKindChecked, EEHBDoorWindowElementKind::Door)
						.OnCheckStateChanged(this, &SEHBMeshSamplingPanel::HandleDoorWindowKindChanged, EEHBDoorWindowElementKind::Door)
						[
							SNew(SBox)
							.MinDesiredWidth(72.0f)
							.HeightOverride(26.0f)
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("DoorWindowKindDoor", "门"))
								.Justification(ETextJustify::Center)
							]
						]
					])
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("DoorWindowSillHeightLabel", "离地高度"),
					LOCTEXT("DoorWindowSillHeightTip", "洞口底边相对墙体底部或当前楼层地面的高度，单位厘米。门会自动使用 0，窗通常使用大于 0 的窗台高度。"),
					SNew(SSpinBox<float>)
					.MinValue(0.0f)
					.MaxValue(100000.0f)
					.Value_Lambda([this]()
					{
						return DoorWindowKind == EEHBDoorWindowElementKind::Door ? 0.0f : DoorWindowSillHeight;
					})
					.IsEnabled_Lambda([this]()
					{
						return DoorWindowKind == EEHBDoorWindowElementKind::Window;
					})
					.OnValueChanged_Lambda([this](float NewValue)
					{
						DoorWindowSillHeight = FMath::Max(0.0f, NewValue);
					}))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("DoorWindowAssetFolderLabel", "蓝图目录"),
					LOCTEXT("DoorWindowAssetFolderTip", "生成门窗蓝图的内容浏览器目录，必须是 /Game 开头的路径，例如 /Game/EHB_DoorWindows。"),
					SNew(SEditableTextBox)
					.Text(this, &SEHBMeshSamplingPanel::GetDoorWindowAssetFolderText)
					.OnTextChanged(this, &SEHBMeshSamplingPanel::HandleDoorWindowAssetFolderChanged)
					.SelectAllTextWhenFocused(true))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildField(
					LOCTEXT("DoorWindowAssetNameLabel", "蓝图名称"),
					LOCTEXT("DoorWindowAssetNameTip", "生成门窗蓝图的资产名称。若同名资产已经存在，当前工具会停止并提示换名，避免覆盖已有蓝图。"),
					SNew(SEditableTextBox)
					.Text(this, &SEHBMeshSamplingPanel::GetDoorWindowAssetNameText)
					.OnTextChanged(this, &SEHBMeshSamplingPanel::HandleDoorWindowAssetNameChanged)
					.SelectAllTextWhenFocused(true))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DoorWindowAdvancedHeader", "高级"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(FStyleColors::Foreground)
				]
				.BodyContent()
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("DoorWindowLODIndexLabel", "LOD 索引"),
							LOCTEXT("DoorWindowLODIndexTip", "检测门窗网格时读取的源静态网格体 LOD 索引。默认使用 LOD0。"),
							SNew(SSpinBox<int32>)
							.MinValue(0)
							.MaxValue(16)
							.Value_Lambda([this]()
							{
								return DoorWindowSamplingOptions.LODIndex;
							})
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								DoorWindowSamplingOptions.LODIndex = FMath::Max(0, NewValue);
								RefreshDoorWindowAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("DoorWindowMaxTriangleCountLabel", "最大源三角面数"),
							LOCTEXT("DoorWindowMaxTriangleCountTip", "允许门窗模板使用的最大三角面数量。过高的模型会增加后续显示、布尔挖洞和运行时保存的成本。"),
							SNew(SSpinBox<int32>)
							.MinValue(1)
							.MaxValue(1000000)
							.Value_Lambda([this]()
							{
								return DoorWindowSamplingOptions.MaxTriangleCount;
							})
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								DoorWindowSamplingOptions.MaxTriangleCount = FMath::Max(1, NewValue);
								RefreshDoorWindowAnalysis();
							}))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildField(
							LOCTEXT("DoorWindowThicknessRatioLabel", "最大厚宽比"),
							LOCTEXT("DoorWindowThicknessRatioTip", "厚度除以宽度的最大允许值。数值越低，越倾向于只接受薄的门窗构件。"),
							SNew(SSpinBox<float>)
							.MinValue(0.01f)
							.MaxValue(2.0f)
							.Delta(0.01f)
							.Value_Lambda([this]()
							{
								return DoorWindowSamplingOptions.MaxThicknessToWidthRatio;
							})
							.OnValueChanged_Lambda([this](float NewValue)
							{
								DoorWindowSamplingOptions.MaxThicknessToWidthRatio = FMath::Clamp(NewValue, 0.01f, 2.0f);
								RefreshDoorWindowAnalysis();
							}))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 10.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CreateDoorWindowBlueprintButton", "生成门窗蓝图"))
				.ToolTipText(LOCTEXT("CreateDoorWindowBlueprintButtonTip", "检测当前静态网格体是否适合做门窗模板，通过后创建一个门窗蓝图，并把源网格体、洞口尺寸、门窗类型和离地高度写入蓝图默认值。"))
				.HAlign(HAlign_Center)
				.IsEnabled(this, &SEHBMeshSamplingPanel::CanCreateDoorWindowBlueprint)
				.OnClicked(this, &SEHBMeshSamplingPanel::HandleCreateDoorWindowBlueprintClicked)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(this, &SEHBMeshSamplingPanel::GetDoorWindowSamplingStatusText)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
					.ToolTipText(LOCTEXT("DoorWindowSamplingStatusTip", "显示最近一次门窗检测或蓝图生成的结果。"))
				]
			]
		];
}

TSharedRef<SWidget> SEHBMeshSamplingPanel::BuildReservedSamplingPage(const FText& Label) const
{
	return SNew(SBorder)
		.Padding(12.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("ReservedSamplingPage", "{0} 页面已预留"), Label))
			.ColorAndOpacity(FStyleColors::Foreground)
		];
}

TSharedRef<SWidget> SEHBMeshSamplingPanel::BuildField(const FText& Label, const FText& ToolTip, const TSharedRef<SWidget>& ValueWidget) const
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(Label)
			.ToolTipText(ToolTip)
			.ColorAndOpacity(FStyleColors::Foreground)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			ValueWidget
		];
}

void SEHBMeshSamplingPanel::HandleSamplingPageChanged(ECheckBoxState NewState, EEHBMeshSamplingPage Page)
{
	if (NewState != ECheckBoxState::Checked)
	{
		return;
	}

	ActivePage = Page;
	if (PageSwitcher.IsValid())
	{
		PageSwitcher->SetActiveWidgetIndex(static_cast<int32>(ActivePage));
	}
}

ECheckBoxState SEHBMeshSamplingPanel::IsSamplingPageChecked(EEHBMeshSamplingPage Page) const
{
	return ActivePage == Page ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FEasyHouseEditorMode* SEHBMeshSamplingPanel::GetActiveEditorMode() const
{
	return static_cast<FEasyHouseEditorMode*>(
		GLevelEditorModeTools().GetActiveMode(FEasyHouseEditorMode::EM_EasyHouseEditorModeId));
}


FString SEHBMeshSamplingPanel::GetWallDataTablePath() const
{
	return WallDataTable.IsValid() ? WallDataTable->GetPathName() : FString();
}

FString SEHBMeshSamplingPanel::GetWallStaticMeshPath() const
{
	return WallStaticMesh.IsValid() ? WallStaticMesh->GetPathName() : FString();
}

void SEHBMeshSamplingPanel::HandleWallDataTableChanged(const FAssetData& AssetData)
{
	WallDataTable = Cast<UDataTable>(AssetData.GetAsset());
	EHBMeshSamplingPanel::SaveConfiguredDataTable(
		&UEHBBuildingToolsetSettings::DefaultWallMeshDataTable,
		WallDataTable.Get());
}

void SEHBMeshSamplingPanel::HandleWallStaticMeshChanged(const FAssetData& AssetData)
{
	WallStaticMesh = Cast<UStaticMesh>(AssetData.GetAsset());
	if (WallStaticMesh.IsValid() && (WallBaseRowName.IsNone() || WallBaseRowName == TEXT("WallMesh")))
	{
		WallBaseRowName = WallStaticMesh->GetFName();
	}
	RefreshWallAnalysis();
}

bool SEHBMeshSamplingPanel::ShouldFilterWallDataTable(const FAssetData& AssetData) const
{
	static const FName RowStructureTagName(TEXT("RowStructure"));
	FString RowStructure;
	if (!AssetData.GetTagValue<FString>(RowStructureTagName, RowStructure))
	{
		return true;
	}

	UScriptStruct* ExpectedStruct = FEHBWallMeshData::StaticStruct();
	if (RowStructure == ExpectedStruct->GetPathName())
	{
		return false;
	}

	UScriptStruct* RowStruct = UClass::TryFindTypeSlow<UScriptStruct>(RowStructure);
	return !(RowStruct && RowStruct->IsChildOf(ExpectedStruct));
}

FText SEHBMeshSamplingPanel::GetWallRowNameText() const
{
	return WallBaseRowName.IsNone() ? FText::GetEmpty() : FText::FromName(WallBaseRowName);
}

void SEHBMeshSamplingPanel::HandleWallRowNameChanged(const FText& NewText)
{
	const FString TrimmedName = NewText.ToString().TrimStartAndEnd();
	WallBaseRowName = TrimmedName.IsEmpty() ? NAME_None : FName(*TrimmedName);
}

void SEHBMeshSamplingPanel::RefreshWallAnalysis()
{
	CachedWallAnalysis = FEHBWallMeshAnalysis();
	if (!WallStaticMesh.IsValid())
	{
		WallSamplingStatus = LOCTEXT("NoWallStaticMeshForAnalysis", "请选择静态网格体。");
		return;
	}

	FEHBWallMeshSampler::AnalyzeMesh(WallStaticMesh.Get(), WallSamplingOptions, CachedWallAnalysis);
	WallSamplingStatus = CachedWallAnalysis.Message;
}

FText SEHBMeshSamplingPanel::GetWallMeshInfoText() const
{
	if (!WallStaticMesh.IsValid())
	{
		return LOCTEXT("NoWallMeshInfo", "未选择静态网格体。");
	}

	if (CachedWallAnalysis.SourceTriangleCount <= 0)
	{
		return LOCTEXT("WallMeshNotAnalyzed", "已选择静态网格体，等待检测。");
	}

	return FText::Format(
		LOCTEXT("WallMeshInfo", "高度：{0} cm\n宽度：{1} cm（源 {2} 轴）\n厚度：{3} cm（源 {4} 轴）\n源顶点：{5} 个\n源三角面：{6} 个\n正面侧面三角面：{7} 个\n反面侧面三角面：{8} 个\n分离网格块：{9} 个"),
		FText::AsNumber(CachedWallAnalysis.WallHeight),
		FText::AsNumber(CachedWallAnalysis.WallWidth),
		EHBMeshSamplingPanel::AxisToText(CachedWallAnalysis.WidthAxis),
		FText::AsNumber(CachedWallAnalysis.WallThickness),
		EHBMeshSamplingPanel::AxisToText(CachedWallAnalysis.ThicknessAxis),
		FText::AsNumber(CachedWallAnalysis.SourceVertexCount),
		FText::AsNumber(CachedWallAnalysis.SourceTriangleCount),
		FText::AsNumber(CachedWallAnalysis.EstimatedFrontSideTriangleCount),
		FText::AsNumber(CachedWallAnalysis.EstimatedBackSideTriangleCount),
		FText::AsNumber(CachedWallAnalysis.DisconnectedComponentCount));
}

FText SEHBMeshSamplingPanel::GetWallSamplingStatusText() const
{
	return WallSamplingStatus;
}

bool SEHBMeshSamplingPanel::CanSampleWall() const
{
	return WallDataTable.IsValid() && WallStaticMesh.IsValid() && !WallBaseRowName.IsNone() && CachedWallAnalysis.bCanSample;
}

FReply SEHBMeshSamplingPanel::HandleSampleWallClicked()
{
	RefreshWallAnalysis();

	FEHBWallMeshSamplingResult Result;
	FText ErrorMessage;
	if (!FEHBWallMeshSampler::SampleToDataTable(
		WallStaticMesh.Get(),
		WallDataTable.Get(),
		WallBaseRowName,
		WallSamplingOptions,
		Result,
		ErrorMessage))
	{
		WallSamplingStatus = ErrorMessage;
		return FReply::Handled();
	}

	WallSamplingStatus = FText::Format(
		LOCTEXT("WallSamplingSucceeded", "采样成功：已写入行 {0}。\n正面三角面：{1} 个，反面三角面：{2} 个。\n墙面尺寸：宽 {3} cm，厚 {4} cm，高 {5} cm。"),
		FText::FromName(Result.RowName),
		FText::AsNumber(Result.FrontTriangleCount),
		FText::AsNumber(Result.BackTriangleCount),
		FText::AsNumber(Result.Analysis.WallWidth),
		FText::AsNumber(Result.Analysis.WallThickness),
		FText::AsNumber(Result.Analysis.WallHeight));

	return FReply::Handled();
}

FString SEHBMeshSamplingPanel::GetPillarDataTablePath() const
{
	return PillarDataTable.IsValid() ? PillarDataTable->GetPathName() : FString();
}

FString SEHBMeshSamplingPanel::GetPillarStaticMeshPath() const
{
	return PillarStaticMesh.IsValid() ? PillarStaticMesh->GetPathName() : FString();
}

void SEHBMeshSamplingPanel::HandlePillarDataTableChanged(const FAssetData& AssetData)
{
	PillarDataTable = Cast<UDataTable>(AssetData.GetAsset());
	EHBMeshSamplingPanel::SaveConfiguredDataTable(
		&UEHBBuildingToolsetSettings::DefaultPillarMeshDataTable,
		PillarDataTable.Get());
}

void SEHBMeshSamplingPanel::HandlePillarStaticMeshChanged(const FAssetData& AssetData)
{
	PillarStaticMesh = Cast<UStaticMesh>(AssetData.GetAsset());
	if (PillarStaticMesh.IsValid() && (PillarRowName.IsNone() || PillarRowName == TEXT("PillarMesh")))
	{
		PillarRowName = PillarStaticMesh->GetFName();
	}
	RefreshPillarAnalysis();
}

bool SEHBMeshSamplingPanel::ShouldFilterPillarDataTable(const FAssetData& AssetData) const
{
	static const FName RowStructureTagName(TEXT("RowStructure"));
	FString RowStructure;
	if (!AssetData.GetTagValue<FString>(RowStructureTagName, RowStructure))
	{
		return true;
	}

	UScriptStruct* ExpectedStruct = FEHBPillarMeshData::StaticStruct();
	if (RowStructure == ExpectedStruct->GetPathName())
	{
		return false;
	}

	UScriptStruct* RowStruct = UClass::TryFindTypeSlow<UScriptStruct>(RowStructure);
	return !(RowStruct && RowStruct->IsChildOf(ExpectedStruct));
}

FText SEHBMeshSamplingPanel::GetPillarRowNameText() const
{
	return PillarRowName.IsNone() ? FText::GetEmpty() : FText::FromName(PillarRowName);
}

void SEHBMeshSamplingPanel::HandlePillarRowNameChanged(const FText& NewText)
{
	const FString TrimmedName = NewText.ToString().TrimStartAndEnd();
	PillarRowName = TrimmedName.IsEmpty() ? NAME_None : FName(*TrimmedName);
}

void SEHBMeshSamplingPanel::RefreshPillarAnalysis()
{
	CachedPillarAnalysis = FEHBPillarMeshAnalysis();
	if (!PillarStaticMesh.IsValid())
	{
		PillarSamplingStatus = LOCTEXT("NoPillarStaticMeshForAnalysis", "请选择静态网格体。");
		return;
	}

	FEHBPillarMeshSampler::AnalyzeMesh(PillarStaticMesh.Get(), PillarSamplingOptions, CachedPillarAnalysis);
	PillarSamplingStatus = CachedPillarAnalysis.Message;
}

FText SEHBMeshSamplingPanel::GetPillarMeshInfoText() const
{
	if (!PillarStaticMesh.IsValid())
	{
		return LOCTEXT("NoPillarMeshInfo", "未选择静态网格体。");
	}

	if (CachedPillarAnalysis.SourceTriangleCount <= 0)
	{
		return LOCTEXT("PillarMeshNotAnalyzed", "已选择静态网格体，等待检测。");
	}

	return FText::Format(
		LOCTEXT("PillarMeshInfo", "宽度：{0} cm\n深度：{1} cm\n高度：{2} cm\n源顶点：{3} 个\n源三角面：{4} 个\n分离网格块：{5} 个"),
		FText::AsNumber(CachedPillarAnalysis.BoundsSize.X),
		FText::AsNumber(CachedPillarAnalysis.BoundsSize.Y),
		FText::AsNumber(CachedPillarAnalysis.BoundsSize.Z),
		FText::AsNumber(CachedPillarAnalysis.SourceVertexCount),
		FText::AsNumber(CachedPillarAnalysis.SourceTriangleCount),
		FText::AsNumber(CachedPillarAnalysis.DisconnectedComponentCount));
}

FText SEHBMeshSamplingPanel::GetPillarSamplingStatusText() const
{
	return PillarSamplingStatus;
}

bool SEHBMeshSamplingPanel::CanSamplePillar() const
{
	return PillarDataTable.IsValid() && PillarStaticMesh.IsValid() && !PillarRowName.IsNone() && CachedPillarAnalysis.bCanSample;
}

FReply SEHBMeshSamplingPanel::HandleSamplePillarClicked()
{
	RefreshPillarAnalysis();

	FEHBPillarMeshSamplingResult Result;
	FText ErrorMessage;
	if (!FEHBPillarMeshSampler::SampleToDataTable(
		PillarStaticMesh.Get(),
		PillarDataTable.Get(),
		PillarRowName,
		PillarSamplingOptions,
		Result,
		ErrorMessage))
	{
		PillarSamplingStatus = ErrorMessage;
		return FReply::Handled();
	}

	PillarSamplingStatus = FText::Format(
		LOCTEXT("PillarSamplingSucceeded", "采样成功：已写入行 {0}。\n柱体三角面：{1} 个。\n柱体尺寸：宽 {2} cm，深 {3} cm，高 {4} cm。"),
		FText::FromName(Result.RowName),
		FText::AsNumber(Result.TriangleCount),
		FText::AsNumber(Result.Analysis.BoundsSize.X),
		FText::AsNumber(Result.Analysis.BoundsSize.Y),
		FText::AsNumber(Result.Analysis.BoundsSize.Z));

	return FReply::Handled();
}

FString SEHBMeshSamplingPanel::GetRailingDataTablePath() const
{
	return RailingDataTable.IsValid() ? RailingDataTable->GetPathName() : FString();
}

FString SEHBMeshSamplingPanel::GetRailingPostStaticMeshPath() const
{
	return RailingPostStaticMesh.IsValid() ? RailingPostStaticMesh->GetPathName() : FString();
}

FString SEHBMeshSamplingPanel::GetRailingRailStaticMeshPath() const
{
	return RailingRailStaticMesh.IsValid() ? RailingRailStaticMesh->GetPathName() : FString();
}

FString SEHBMeshSamplingPanel::GetRailingPanelStaticMeshPath() const
{
	return RailingPanelStaticMesh.IsValid() ? RailingPanelStaticMesh->GetPathName() : FString();
}

void SEHBMeshSamplingPanel::HandleRailingDataTableChanged(const FAssetData& AssetData)
{
	RailingDataTable = Cast<UDataTable>(AssetData.GetAsset());
	EHBMeshSamplingPanel::SaveConfiguredDataTable(
		&UEHBBuildingToolsetSettings::DefaultRailingMeshDataTable,
		RailingDataTable.Get());
}

void SEHBMeshSamplingPanel::HandleRailingPostStaticMeshChanged(const FAssetData& AssetData)
{
	RailingPostStaticMesh = Cast<UStaticMesh>(AssetData.GetAsset());
	if (RailingPostStaticMesh.IsValid() && (RailingRowName.IsNone() || RailingRowName == TEXT("RailingMesh")))
	{
		RailingRowName = RailingPostStaticMesh->GetFName();
	}
	RefreshRailingAnalysis();
}

void SEHBMeshSamplingPanel::HandleRailingRailStaticMeshChanged(const FAssetData& AssetData)
{
	RailingRailStaticMesh = Cast<UStaticMesh>(AssetData.GetAsset());
	if (RailingRailStaticMesh.IsValid() && (RailingRowName.IsNone() || RailingRowName == TEXT("RailingMesh")))
	{
		RailingRowName = RailingRailStaticMesh->GetFName();
	}
	RefreshRailingAnalysis();
}

void SEHBMeshSamplingPanel::HandleRailingPanelStaticMeshChanged(const FAssetData& AssetData)
{
	RailingPanelStaticMesh = Cast<UStaticMesh>(AssetData.GetAsset());
	RefreshRailingAnalysis();
}

bool SEHBMeshSamplingPanel::ShouldFilterRailingDataTable(const FAssetData& AssetData) const
{
	static const FName RowStructureTagName(TEXT("RowStructure"));
	FString RowStructure;
	if (!AssetData.GetTagValue<FString>(RowStructureTagName, RowStructure))
	{
		return true;
	}

	UScriptStruct* ExpectedStruct = FEHBRailingMeshData::StaticStruct();
	if (RowStructure == ExpectedStruct->GetPathName())
	{
		return false;
	}

	UScriptStruct* RowStruct = UClass::TryFindTypeSlow<UScriptStruct>(RowStructure);
	return !(RowStruct && RowStruct->IsChildOf(ExpectedStruct));
}

FText SEHBMeshSamplingPanel::GetRailingRowNameText() const
{
	return RailingRowName.IsNone() ? FText::GetEmpty() : FText::FromName(RailingRowName);
}

void SEHBMeshSamplingPanel::HandleRailingRowNameChanged(const FText& NewText)
{
	const FString TrimmedName = NewText.ToString().TrimStartAndEnd();
	RailingRowName = TrimmedName.IsEmpty() ? NAME_None : FName(*TrimmedName);
}

void SEHBMeshSamplingPanel::RefreshRailingAnalysis()
{
	CachedRailingAnalysis = FEHBRailingMeshAnalysis();
	if (!RailingPostStaticMesh.IsValid() && !RailingRailStaticMesh.IsValid())
	{
		RailingSamplingStatus = LOCTEXT("NoRailingMeshesForAnalysis", "\u8bf7\u9009\u62e9\u7acb\u67f1\u7f51\u683c\u4f53\u6216\u6a2a\u6746\u7f51\u683c\u4f53\uff0c\u81f3\u5c11\u9700\u8981\u4e00\u4e2a\u3002");
		return;
	}

	FEHBRailingMeshSampler::AnalyzeMeshes(
		RailingPostStaticMesh.Get(),
		RailingRailStaticMesh.Get(),
		RailingPanelStaticMesh.Get(),
		RailingSamplingOptions,
		CachedRailingAnalysis);
	RailingSamplingStatus = CachedRailingAnalysis.Message;
}

FText SEHBMeshSamplingPanel::GetRailingMeshInfoText() const
{
	if (!RailingPostStaticMesh.IsValid() && !RailingRailStaticMesh.IsValid())
	{
		return LOCTEXT("NoRailingMeshInfo", "\u5c1a\u672a\u9009\u62e9\u6276\u624b\u7f51\u683c\u4f53\u3002");
	}

	if (!CachedRailingAnalysis.bCanSample)
	{
		return LOCTEXT("RailingMeshNotAnalyzed", "\u5df2\u9009\u62e9\u6276\u624b\u7f51\u683c\u4f53\uff0c\u6b63\u7b49\u5f85\u68c0\u6d4b\u7ed3\u679c\u3002");
	}

	const FText PostInfoText = CachedRailingAnalysis.Post.bHasMesh
		? FText::Format(
			LOCTEXT("RailingPostMeshInfo", "{0} x {1} x {2} cm\uff0c{3} \u4e2a\u4e09\u89d2\u9762"),
			FText::AsNumber(CachedRailingAnalysis.Post.BoundsSize.X),
			FText::AsNumber(CachedRailingAnalysis.Post.BoundsSize.Y),
			FText::AsNumber(CachedRailingAnalysis.Post.BoundsSize.Z),
			FText::AsNumber(CachedRailingAnalysis.Post.SourceTriangleCount))
		: LOCTEXT("RailingPostMeshInfoDefault", "\u672a\u9009\u62e9\uff0c\u66ff\u6362\u65f6\u4f7f\u7528\u9ed8\u8ba4\u7acb\u67f1");
	const FText RailInfoText = CachedRailingAnalysis.Rail.bHasMesh
		? FText::Format(
			LOCTEXT("RailingRailMeshInfo", "{0} x {1} x {2} cm\uff0c{3} \u4e2a\u4e09\u89d2\u9762"),
			FText::AsNumber(CachedRailingAnalysis.Rail.BoundsSize.X),
			FText::AsNumber(CachedRailingAnalysis.Rail.BoundsSize.Y),
			FText::AsNumber(CachedRailingAnalysis.Rail.BoundsSize.Z),
			FText::AsNumber(CachedRailingAnalysis.Rail.SourceTriangleCount))
		: LOCTEXT("RailingRailMeshInfoDefault", "\u672a\u9009\u62e9\uff0c\u66ff\u6362\u65f6\u4f7f\u7528\u9ed8\u8ba4\u6a2a\u6746");

	return FText::Format(
		LOCTEXT("RailingMeshInfo", "\u7acb\u67f1\uff1a{0}\n\u6a2a\u6746\uff1a{1}\n\u63a8\u8350\uff1a\u7acb\u67f1\u9ad8 {2} cm\uff0c\u7acb\u67f1\u5bbd {3} cm\uff0c\u7acb\u67f1\u95f4\u8ddd {4} cm\uff0c\u6a2a\u6746\u539a {5} cm\uff0c\u6a2a\u6746\u6700\u5927\u5206\u6bb5 {6} cm\n\u56f4\u677f\uff1a{7}"),
		PostInfoText,
		RailInfoText,
		FText::AsNumber(CachedRailingAnalysis.RecommendedPostHeight),
		FText::AsNumber(CachedRailingAnalysis.RecommendedPostWidth),
		FText::AsNumber(CachedRailingAnalysis.RecommendedPostSpacing),
		FText::AsNumber(CachedRailingAnalysis.RecommendedRailThickness),
		FText::AsNumber(CachedRailingAnalysis.RecommendedMaxRailSegmentLength),
		CachedRailingAnalysis.Panel.bHasMesh
			? FText::Format(
				LOCTEXT("RailingPanelInfoPresent", "\u5df2\u9009\u62e9\uff0c\u5bbd\u5ea6\u8f74 {0}\uff0c\u5bbd {1} cm"),
				FText::FromName(CachedRailingAnalysis.PanelWidthAxis),
				FText::AsNumber(CachedRailingAnalysis.RecommendedPanelWidth))
			: LOCTEXT("RailingPanelInfoNone", "\u65e0"));
}

FText SEHBMeshSamplingPanel::GetRailingSamplingStatusText() const
{
	return RailingSamplingStatus;
}

bool SEHBMeshSamplingPanel::CanSampleRailing() const
{
	return RailingDataTable.IsValid()
		&& (RailingPostStaticMesh.IsValid() || RailingRailStaticMesh.IsValid())
		&& !RailingRowName.IsNone()
		&& CachedRailingAnalysis.bCanSample;
}

FReply SEHBMeshSamplingPanel::HandleSampleRailingClicked()
{
	RefreshRailingAnalysis();

	FEHBRailingMeshSamplingResult Result;
	FText ErrorMessage;
	if (!FEHBRailingMeshSampler::SampleToDataTable(
		RailingPostStaticMesh.Get(),
		RailingRailStaticMesh.Get(),
		RailingPanelStaticMesh.Get(),
		RailingDataTable.Get(),
		RailingRowName,
		RailingSamplingOptions,
		Result,
		ErrorMessage))
	{
		RailingSamplingStatus = ErrorMessage;
		return FReply::Handled();
	}

	RailingSamplingStatus = FText::Format(
		LOCTEXT("RailingSamplingSucceeded", "\u91c7\u6837\u6210\u529f\uff1a\u5df2\u5199\u5165\u884c {0}\u3002\n\u63a8\u8350\u7acb\u67f1\uff1a\u5bbd {1} cm\uff0c\u9ad8 {2} cm\uff0c\u95f4\u8ddd {3} cm\u3002\n\u63a8\u8350\u6a2a\u6746\uff1a\u539a {4} cm\uff0c\u6700\u5927\u5206\u6bb5 {5} cm\u3002"),
		FText::FromName(Result.RowName),
		FText::AsNumber(Result.Analysis.RecommendedPostWidth),
		FText::AsNumber(Result.Analysis.RecommendedPostHeight),
		FText::AsNumber(Result.Analysis.RecommendedPostSpacing),
		FText::AsNumber(Result.Analysis.RecommendedRailThickness),
		FText::AsNumber(Result.Analysis.RecommendedMaxRailSegmentLength));

	return FReply::Handled();
}

FString SEHBMeshSamplingPanel::GetRoofDataTablePath() const
{
	return RoofDataTable.IsValid() ? RoofDataTable->GetPathName() : FString();
}

FString SEHBMeshSamplingPanel::GetRoofSurfaceStaticMeshPath() const
{
	return RoofSurfaceStaticMesh.IsValid() ? RoofSurfaceStaticMesh->GetPathName() : FString();
}

FString SEHBMeshSamplingPanel::GetRoofRidgeStaticMeshPath() const
{
	return RoofRidgeStaticMesh.IsValid() ? RoofRidgeStaticMesh->GetPathName() : FString();
}

FString SEHBMeshSamplingPanel::GetRoofValleyStaticMeshPath() const
{
	return RoofValleyStaticMesh.IsValid() ? RoofValleyStaticMesh->GetPathName() : FString();
}

void SEHBMeshSamplingPanel::HandleRoofDataTableChanged(const FAssetData& AssetData)
{
	RoofDataTable = Cast<UDataTable>(AssetData.GetAsset());
	EHBMeshSamplingPanel::SaveConfiguredDataTable(
		&UEHBBuildingToolsetSettings::DefaultRoofMeshDataTable,
		RoofDataTable.Get());
}

void SEHBMeshSamplingPanel::HandleRoofSurfaceStaticMeshChanged(const FAssetData& AssetData)
{
	RoofSurfaceStaticMesh = Cast<UStaticMesh>(AssetData.GetAsset());
	if (RoofSurfaceStaticMesh.IsValid() && (RoofRowName.IsNone() || RoofRowName == TEXT("RoofMesh")))
	{
		RoofRowName = RoofSurfaceStaticMesh->GetFName();
	}
	EHBMeshSamplingPanel::SaveConfiguredStaticMesh(
		&UEHBBuildingToolsetSettings::DefaultRoofSurfaceTileMesh,
		RoofSurfaceStaticMesh.Get());
	RefreshRoofAnalysis();
}

void SEHBMeshSamplingPanel::HandleRoofRidgeStaticMeshChanged(const FAssetData& AssetData)
{
	RoofRidgeStaticMesh = Cast<UStaticMesh>(AssetData.GetAsset());
	EHBMeshSamplingPanel::SaveConfiguredStaticMesh(
		&UEHBBuildingToolsetSettings::DefaultRoofRidgeTileMesh,
		RoofRidgeStaticMesh.Get());
	RefreshRoofAnalysis();
}

void SEHBMeshSamplingPanel::HandleRoofValleyStaticMeshChanged(const FAssetData& AssetData)
{
	RoofValleyStaticMesh = Cast<UStaticMesh>(AssetData.GetAsset());
	EHBMeshSamplingPanel::SaveConfiguredStaticMesh(
		&UEHBBuildingToolsetSettings::DefaultRoofValleyTileMesh,
		RoofValleyStaticMesh.Get());
	RefreshRoofAnalysis();
}

bool SEHBMeshSamplingPanel::ShouldFilterRoofDataTable(const FAssetData& AssetData) const
{
	static const FName RowStructureTagName(TEXT("RowStructure"));
	FString RowStructure;
	if (!AssetData.GetTagValue<FString>(RowStructureTagName, RowStructure))
	{
		return true;
	}

	UScriptStruct* ExpectedStruct = FEHBRoofMeshData::StaticStruct();
	if (RowStructure == ExpectedStruct->GetPathName())
	{
		return false;
	}

	UScriptStruct* RowStruct = UClass::TryFindTypeSlow<UScriptStruct>(RowStructure);
	return !(RowStruct && RowStruct->IsChildOf(ExpectedStruct));
}

FText SEHBMeshSamplingPanel::GetRoofRowNameText() const
{
	return RoofRowName.IsNone() ? FText::GetEmpty() : FText::FromName(RoofRowName);
}

void SEHBMeshSamplingPanel::HandleRoofRowNameChanged(const FText& NewText)
{
	const FString TrimmedName = NewText.ToString().TrimStartAndEnd();
	RoofRowName = TrimmedName.IsEmpty() ? NAME_None : FName(*TrimmedName);
}

void SEHBMeshSamplingPanel::RefreshRoofAnalysis()
{
	CachedRoofAnalysis = FEHBRoofMeshAnalysis();
	if (!RoofSurfaceStaticMesh.IsValid())
	{
		RoofSamplingStatus = LOCTEXT("NoRoofSurfaceMeshForAnalysis", "\u8bf7\u9009\u62e9\u5c4b\u9762\u74e6\u7247\u7f51\u683c\u4f53\u3002");
		return;
	}

	FEHBRoofMeshSampler::AnalyzeMeshes(
		RoofSurfaceStaticMesh.Get(),
		RoofRidgeStaticMesh.Get(),
		RoofValleyStaticMesh.Get(),
		RoofSamplingOptions,
		CachedRoofAnalysis);
	RoofSamplingStatus = CachedRoofAnalysis.Message;
}

FText SEHBMeshSamplingPanel::GetRoofMeshInfoText() const
{
	if (!RoofSurfaceStaticMesh.IsValid())
	{
		return LOCTEXT("NoRoofMeshInfo", "\u5c1a\u672a\u9009\u62e9\u5c4b\u9762\u74e6\u7247\u7f51\u683c\u4f53\u3002");
	}

	if (!CachedRoofAnalysis.bCanSample)
	{
		return LOCTEXT("RoofMeshNotAnalyzed", "\u5df2\u9009\u62e9\u5c4b\u9876\u7f51\u683c\u4f53\uff0c\u6b63\u7b49\u5f85\u68c0\u6d4b\u7ed3\u679c\u3002");
	}

	const auto DescribePart = [](const FEHBRoofMeshPartAnalysis& Part, const FText& MissingText)
	{
		return Part.bHasMesh
			? FText::Format(
				LOCTEXT("RoofPartMeshInfo", "{0} x {1} x {2} cm\uff0c{3} \u4e2a\u4e09\u89d2\u9762"),
				FText::AsNumber(Part.BoundsSize.X),
				FText::AsNumber(Part.BoundsSize.Y),
				FText::AsNumber(Part.BoundsSize.Z),
				FText::AsNumber(Part.SourceTriangleCount))
			: MissingText;
	};

	return FText::Format(
		LOCTEXT("RoofMeshInfo", "\u5c4b\u9762\uff1a{0}\n\u5c4b\u810a\uff1a{1}\n\u8c37\u7ebf\uff1a{2}\n\u603b\u4e09\u89d2\u9762\uff1a{3}\uff0c\u603b\u9876\u70b9\uff1a{4}"),
		DescribePart(CachedRoofAnalysis.SurfaceTile, LOCTEXT("RoofSurfaceMissing", "\u672a\u9009\u62e9")),
		DescribePart(CachedRoofAnalysis.RidgeTile, LOCTEXT("RoofRidgeMissing", "\u672a\u9009\u62e9")),
		DescribePart(CachedRoofAnalysis.ValleyTile, LOCTEXT("RoofValleyMissing", "\u672a\u9009\u62e9")),
		FText::AsNumber(CachedRoofAnalysis.TotalTriangleCount),
		FText::AsNumber(CachedRoofAnalysis.TotalVertexCount));
}

FText SEHBMeshSamplingPanel::GetRoofSamplingStatusText() const
{
	return RoofSamplingStatus;
}

bool SEHBMeshSamplingPanel::CanSampleRoof() const
{
	return RoofDataTable.IsValid()
		&& RoofSurfaceStaticMesh.IsValid()
		&& !RoofRowName.IsNone()
		&& CachedRoofAnalysis.bCanSample;
}

FReply SEHBMeshSamplingPanel::HandleSampleRoofClicked()
{
	RefreshRoofAnalysis();

	FEHBRoofMeshSamplingResult Result;
	FText ErrorMessage;
	if (!FEHBRoofMeshSampler::SampleToDataTable(
		RoofSurfaceStaticMesh.Get(),
		RoofRidgeStaticMesh.Get(),
		RoofValleyStaticMesh.Get(),
		RoofDataTable.Get(),
		RoofRowName,
		RoofSamplingOptions,
		Result,
		ErrorMessage))
	{
		RoofSamplingStatus = ErrorMessage;
		return FReply::Handled();
	}

	RoofSamplingStatus = FText::Format(
		LOCTEXT("RoofSamplingSucceeded", "\u91c7\u6837\u6210\u529f\uff1a\u5df2\u5199\u5165\u884c {0}\u3002\n\u603b\u4e09\u89d2\u9762\uff1a{1}\uff0c\u603b\u9876\u70b9\uff1a{2}\u3002"),
		FText::FromName(Result.RowName),
		FText::AsNumber(Result.Analysis.TotalTriangleCount),
		FText::AsNumber(Result.Analysis.TotalVertexCount));

	return FReply::Handled();
}

FString SEHBMeshSamplingPanel::GetDoorWindowDataTablePath() const
{
	return DoorWindowDataTable.IsValid() ? DoorWindowDataTable->GetPathName() : FString();
}

void SEHBMeshSamplingPanel::HandleDoorWindowDataTableChanged(const FAssetData& AssetData)
{
	DoorWindowDataTable = Cast<UDataTable>(AssetData.GetAsset());
	EHBMeshSamplingPanel::SaveConfiguredDataTable(
		&UEHBBuildingToolsetSettings::DefaultDoorWindowMeshDataTable,
		DoorWindowDataTable.Get());
}

bool SEHBMeshSamplingPanel::ShouldFilterDoorWindowDataTable(const FAssetData& AssetData) const
{
	static const FName RowStructureTagName(TEXT("RowStructure"));
	FString RowStructure;
	if (!AssetData.GetTagValue<FString>(RowStructureTagName, RowStructure))
	{
		return true;
	}

	UScriptStruct* ExpectedStruct = FEHBDoorWindowMeshData::StaticStruct();
	if (RowStructure == ExpectedStruct->GetPathName())
	{
		return false;
	}

	UScriptStruct* RowStruct = UClass::TryFindTypeSlow<UScriptStruct>(RowStructure);
	return !(RowStruct && RowStruct->IsChildOf(ExpectedStruct));
}

FText SEHBMeshSamplingPanel::GetDoorWindowRowNameText() const
{
	return DoorWindowRowName.IsNone() ? FText::GetEmpty() : FText::FromName(DoorWindowRowName);
}

void SEHBMeshSamplingPanel::HandleDoorWindowRowNameChanged(const FText& NewText)
{
	const FString TrimmedName = NewText.ToString().TrimStartAndEnd();
	DoorWindowRowName = TrimmedName.IsEmpty() ? NAME_None : FName(*TrimmedName);
}

FString SEHBMeshSamplingPanel::GetDoorWindowStaticMeshPath() const
{
	return DoorWindowStaticMesh.IsValid() ? DoorWindowStaticMesh->GetPathName() : FString();
}

void SEHBMeshSamplingPanel::HandleDoorWindowStaticMeshChanged(const FAssetData& AssetData)
{
	DoorWindowStaticMesh = Cast<UStaticMesh>(AssetData.GetAsset());
	if (DoorWindowStaticMesh.IsValid())
	{
		DoorWindowAssetName = FString::Printf(TEXT("BP_%s"), *DoorWindowStaticMesh->GetName());
		if (DoorWindowRowName.IsNone() || DoorWindowRowName == TEXT("DoorWindowMesh"))
		{
			DoorWindowRowName = DoorWindowStaticMesh->GetFName();
		}
	}

	RefreshDoorWindowAnalysis();
}

const UClass* SEHBMeshSamplingPanel::GetSelectedDoorWindowBaseClass() const
{
	return DoorWindowBaseClass.IsValid() ? DoorWindowBaseClass.Get() : AEHB_DoorWindow::StaticClass();
}

void SEHBMeshSamplingPanel::HandleDoorWindowBaseClassChanged(const UClass* NewClass)
{
	if (NewClass && NewClass->IsChildOf(AEHB_DoorWindow::StaticClass()))
	{
		DoorWindowBaseClass = const_cast<UClass*>(NewClass);
		return;
	}

	DoorWindowBaseClass = AEHB_DoorWindow::StaticClass();
}

FText SEHBMeshSamplingPanel::GetDoorWindowAssetNameText() const
{
	return FText::FromString(DoorWindowAssetName);
}

void SEHBMeshSamplingPanel::HandleDoorWindowAssetNameChanged(const FText& NewText)
{
	DoorWindowAssetName = NewText.ToString().TrimStartAndEnd();
}

FText SEHBMeshSamplingPanel::GetDoorWindowAssetFolderText() const
{
	return FText::FromString(DoorWindowAssetFolder);
}

void SEHBMeshSamplingPanel::HandleDoorWindowAssetFolderChanged(const FText& NewText)
{
	DoorWindowAssetFolder = NewText.ToString().TrimStartAndEnd();
}

void SEHBMeshSamplingPanel::HandleDoorWindowKindChanged(ECheckBoxState NewState, EEHBDoorWindowElementKind NewKind)
{
	if (NewState != ECheckBoxState::Checked)
	{
		return;
	}

	DoorWindowKind = NewKind;
	if (DoorWindowKind == EEHBDoorWindowElementKind::Door)
	{
		DoorWindowSillHeight = 0.0f;
	}
}

ECheckBoxState SEHBMeshSamplingPanel::IsDoorWindowKindChecked(EEHBDoorWindowElementKind TestKind) const
{
	return DoorWindowKind == TestKind ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SEHBMeshSamplingPanel::RefreshDoorWindowAnalysis()
{
	CachedDoorWindowAnalysis = FEHBDoorWindowMeshAnalysis();
	if (!DoorWindowStaticMesh.IsValid())
	{
		DoorWindowSamplingStatus = LOCTEXT("NoDoorWindowStaticMeshForAnalysis", "请选择门窗静态网格体。");
		return;
	}

	FEHBDoorWindowMeshSampler::AnalyzeMesh(DoorWindowStaticMesh.Get(), DoorWindowSamplingOptions, CachedDoorWindowAnalysis);
	DoorWindowSamplingStatus = CachedDoorWindowAnalysis.Message;
}

FText SEHBMeshSamplingPanel::GetDoorWindowMeshInfoText() const
{
	if (!DoorWindowStaticMesh.IsValid())
	{
		return LOCTEXT("NoDoorWindowMeshInfo", "未选择门窗静态网格体。");
	}

	if (CachedDoorWindowAnalysis.SourceTriangleCount <= 0)
	{
		return LOCTEXT("DoorWindowMeshNotAnalyzed", "已选择门窗静态网格体，等待检测。");
	}

	return FText::Format(
		LOCTEXT("DoorWindowMeshInfo", "洞口宽度：{0} cm\n洞口厚度：{1} cm\n洞口高度：{2} cm\n宽度来源：{3}\n源顶点：{4} 个\n源三角面：{5} 个"),
		FText::AsNumber(CachedDoorWindowAnalysis.OpeningWidth),
		FText::AsNumber(CachedDoorWindowAnalysis.OpeningThickness),
		FText::AsNumber(CachedDoorWindowAnalysis.OpeningHeight),
		CachedDoorWindowAnalysis.bWidthUsesSourceY ? LOCTEXT("DoorWindowWidthSourceY", "源网格体 Y 轴") : LOCTEXT("DoorWindowWidthSourceX", "源网格体 X 轴"),
		FText::AsNumber(CachedDoorWindowAnalysis.SourceVertexCount),
		FText::AsNumber(CachedDoorWindowAnalysis.SourceTriangleCount));
}

FText SEHBMeshSamplingPanel::GetDoorWindowSamplingStatusText() const
{
	return DoorWindowSamplingStatus;
}

bool SEHBMeshSamplingPanel::CanCreateDoorWindowBlueprint() const
{
	return DoorWindowDataTable.IsValid()
		&& DoorWindowStaticMesh.IsValid()
		&& DoorWindowBaseClass.IsValid()
		&& !DoorWindowRowName.IsNone()
		&& !DoorWindowAssetName.TrimStartAndEnd().IsEmpty()
		&& CachedDoorWindowAnalysis.bCanSample;
}

FReply SEHBMeshSamplingPanel::HandleCreateDoorWindowBlueprintClicked()
{
	RefreshDoorWindowAnalysis();

	FEHBDoorWindowBlueprintCreationOptions CreationOptions;
	CreationOptions.ParentClass = const_cast<UClass*>(GetSelectedDoorWindowBaseClass());
	CreationOptions.Kind = DoorWindowKind;
	CreationOptions.SillHeight = DoorWindowKind == EEHBDoorWindowElementKind::Door ? 0.0f : DoorWindowSillHeight;
	CreationOptions.AssetFolder = DoorWindowAssetFolder;
	CreationOptions.AssetName = DoorWindowAssetName;

	FEHBDoorWindowBlueprintCreationResult Result;
	FText ErrorMessage;
	if (!FEHBDoorWindowMeshSampler::CreateBlueprintFromStaticMesh(
		DoorWindowStaticMesh.Get(),
		DoorWindowSamplingOptions,
		CreationOptions,
		Result,
		ErrorMessage))
	{
		DoorWindowSamplingStatus = ErrorMessage;
		return FReply::Handled();
	}

	if (!WriteDoorWindowDataTableRow(Result, CreationOptions, ErrorMessage))
	{
		DoorWindowSamplingStatus = ErrorMessage;
		return FReply::Handled();
	}

	DoorWindowSamplingStatus = FText::Format(
		LOCTEXT("DoorWindowBlueprintCreated", "生成成功：已创建门窗蓝图 {0}，并写入门窗数据表行 {1}。\n洞口宽 {2} cm，厚 {3} cm，高 {4} cm，离地高度 {5} cm。"),
		FText::FromString(Result.PackageName),
		FText::FromName(DoorWindowRowName),
		FText::AsNumber(Result.Analysis.OpeningWidth),
		FText::AsNumber(Result.Analysis.OpeningThickness),
		FText::AsNumber(Result.Analysis.OpeningHeight),
		FText::AsNumber(CreationOptions.SillHeight));

	return FReply::Handled();
}

bool SEHBMeshSamplingPanel::WriteDoorWindowDataTableRow(const FEHBDoorWindowBlueprintCreationResult& Result, const FEHBDoorWindowBlueprintCreationOptions& CreationOptions, FText& OutErrorMessage)
{
	if (!DoorWindowDataTable.IsValid())
	{
		OutErrorMessage = LOCTEXT("NoDoorWindowDataTable", "门窗采样失败：请选择门窗数据表。");
		return false;
	}

	if (DoorWindowDataTable->GetRowStruct() != FEHBDoorWindowMeshData::StaticStruct())
	{
		OutErrorMessage = LOCTEXT("WrongDoorWindowDataTableRowStruct", "门窗采样失败：门窗数据表的行结构必须是“门窗网格体数据 / FEHBDoorWindowMeshData”。");
		return false;
	}

	if (DoorWindowRowName.IsNone())
	{
		OutErrorMessage = LOCTEXT("NoDoorWindowRowName", "门窗采样失败：请填写名称。名称会直接作为门窗数据表行名。");
		return false;
	}

	UClass* CreatedClass = Result.CreatedClass.Get();
	if (!CreatedClass || !CreatedClass->IsChildOf(AEHB_DoorWindow::StaticClass()))
	{
		OutErrorMessage = LOCTEXT("NoDoorWindowCreatedClass", "门窗采样失败：蓝图已经创建，但没有得到有效的 AEHB_DoorWindow 子类，无法写入数据表。");
		return false;
	}

	FEHBDoorWindowMeshData SampleRow;
	SampleRow.DoorWindowClass = CreatedClass;
	SampleRow.SourceStaticMesh = DoorWindowStaticMesh.Get();
	SampleRow.SourceMeshName = DoorWindowStaticMesh.IsValid() ? DoorWindowStaticMesh->GetFName() : NAME_None;
	SampleRow.Kind = CreationOptions.Kind;
	SampleRow.OpeningWidth = Result.Analysis.OpeningWidth;
	SampleRow.OpeningHeight = Result.Analysis.OpeningHeight;
	SampleRow.OpeningThickness = Result.Analysis.OpeningThickness;
	SampleRow.SillHeight = CreationOptions.SillHeight;
	SampleRow.LODIndex = DoorWindowSamplingOptions.LODIndex;
	SampleRow.bWidthUsesSourceY = Result.Analysis.bWidthUsesSourceY;
	SampleRow.SourceBoundsMin = Result.Analysis.SourceBoundsMin;
	SampleRow.SourceBoundsMax = Result.Analysis.SourceBoundsMax;
	SampleRow.SourceTriangleCount = Result.Analysis.SourceTriangleCount;
	SampleRow.SourceVertexCount = Result.Analysis.SourceVertexCount;
	FEHBMeshSampleValidation::InitializeMetadata(
		SampleRow.TemplateMetadata,
		EEHBMeshSampleTemplateKind::DoorWindow,
		DoorWindowRowName,
		DoorWindowStaticMesh.Get(),
		Result.Analysis.SourceTriangleCount);

	const FScopedTransaction Transaction(LOCTEXT("WriteDoorWindowMeshDataTransaction", "写入门窗网格体数据"));
	DoorWindowDataTable->Modify();
	DoorWindowDataTable->AddRow(DoorWindowRowName, SampleRow);
	DoorWindowDataTable->MarkPackageDirty();
	return true;
}

#undef LOCTEXT_NAMESPACE
