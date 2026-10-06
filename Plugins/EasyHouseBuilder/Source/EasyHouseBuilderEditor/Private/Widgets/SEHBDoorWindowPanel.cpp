// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SEHBDoorWindowPanel.h"

#include "Actors/EHB_DoorWindow.h"
#include "AssetRegistry/AssetData.h"
#include "AssetThumbnail.h"
#include "Core/EHBBuildingActorBase.h"
#include "DataTableEditorUtils.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "InputCoreTypes.h"
#include "EasyHouseEditorMode.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Sampling/EHBDoorWindowMeshData.h"
#include "ScopedTransaction.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SEHBDoorWindowPanel"

namespace
{
	/** 门窗卡片的拖拽操作。它负责把 Slate 拖拽事件转交给建筑编辑模式，由编辑模式在视口中维护真实预览 Actor。 */
	class FEHBDoorWindowDragDropOp : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FEHBDoorWindowDragDropOp, FDecoratedDragDropOp)

		static TSharedRef<FEHBDoorWindowDragDropOp> New(FEasyHouseEditorMode* InEditorMode, AEHBBuildingActorBase* InBuilding, UDataTable* InTable, FName InRowName)
		{
			TSharedRef<FEHBDoorWindowDragDropOp> Operation = MakeShared<FEHBDoorWindowDragDropOp>();
			Operation->EditorMode = InEditorMode;
			Operation->Building = InBuilding;
			Operation->Table = InTable;
			Operation->RowName = InRowName;

			if (!InEditorMode)
			{
				Operation->CurrentHoverText = LOCTEXT("DragDoorWindowNoMode", "请先进入建筑编辑模式");
			}
			else if (!InBuilding)
			{
				Operation->CurrentHoverText = LOCTEXT("DragDoorWindowNoBuilding", "请先选择建筑对象");
			}
			else
			{
				Operation->CurrentHoverText = LOCTEXT("DragDoorWindowHover", "拖到墙体上创建门窗");
				InEditorMode->BeginDoorWindowPlacement(InBuilding, InTable, InRowName);
			}

			Operation->CurrentIconBrush = FAppStyle::GetBrush("ClassIcon.Blueprint");
			Operation->SetupDefaults();
			Operation->Construct();
			return Operation;
		}

		static TSharedRef<FEHBDoorWindowDragDropOp> NewDefault(FEasyHouseEditorMode* InEditorMode, AEHBBuildingActorBase* InBuilding, EEHBDoorWindowElementKind InKind)
		{
			TSharedRef<FEHBDoorWindowDragDropOp> Operation = MakeShared<FEHBDoorWindowDragDropOp>();
			Operation->EditorMode = InEditorMode;
			Operation->Building = InBuilding;

			if (!InEditorMode)
			{
				Operation->CurrentHoverText = LOCTEXT("DragDefaultDoorWindowNoMode", "请先进入建筑编辑模式");
			}
			else if (!InBuilding)
			{
				Operation->CurrentHoverText = LOCTEXT("DragDefaultDoorWindowNoBuilding", "请先选择建筑对象");
			}
			else
			{
				Operation->CurrentHoverText = InKind == EEHBDoorWindowElementKind::Door
					? LOCTEXT("DragDefaultDoorHover", "拖到墙体上创建默认门")
					: LOCTEXT("DragDefaultWindowHover", "拖到墙体上创建默认窗");
				InEditorMode->BeginDefaultDoorWindowPlacement(InBuilding, InKind);
			}

			Operation->CurrentIconBrush = FAppStyle::GetBrush("ClassIcon.Blueprint");
			Operation->SetupDefaults();
			Operation->Construct();
			return Operation;
		}

		virtual ~FEHBDoorWindowDragDropOp() override
		{
			if (EditorMode)
			{
				EditorMode->CancelDoorWindowPlacement();
			}
		}

		virtual void OnDragged(const FDragDropEvent& DragDropEvent) override
		{
			FDecoratedDragDropOp::OnDragged(DragDropEvent);
			if (EditorMode)
			{
				EditorMode->UpdateDoorWindowPlacementFromPointerEvent(DragDropEvent);
			}
		}

		virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
		{
			FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);
			if (EditorMode)
			{
				EditorMode->FinishDoorWindowPlacementFromPointerEvent(MouseEvent);
				EditorMode = nullptr;
			}
		}

	private:
		FEasyHouseEditorMode* EditorMode = nullptr;
		TWeakObjectPtr<AEHBBuildingActorBase> Building;
		TWeakObjectPtr<UDataTable> Table;
		FName RowName = NAME_None;
	};

	/** 包在门窗模板卡片外层的拖拽句柄。 */
	class SEHBDoorWindowDragHandle : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEHBDoorWindowDragHandle) {}
			SLATE_ARGUMENT(TWeakPtr<SEHBDoorWindowPanel>, OwnerWidget)
			SLATE_ARGUMENT(FName, RowName)
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OwnerWidget = InArgs._OwnerWidget;
			RowName = InArgs._RowName;
			ChildSlot
			[
				InArgs._Content.Widget
			];
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
			}

			return FReply::Unhandled();
		}

		virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (TSharedPtr<SEHBDoorWindowPanel> Owner = OwnerWidget.Pin())
			{
				return FReply::Handled().BeginDragDrop(Owner->CreateDoorWindowDragDropOperationForRow(RowName));
			}

			return FReply::Unhandled();
		}

	private:
		TWeakPtr<SEHBDoorWindowPanel> OwnerWidget;
		FName RowName = NAME_None;
	};

	class SEHBDefaultDoorWindowDragHandle : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEHBDefaultDoorWindowDragHandle) {}
			SLATE_ARGUMENT(TWeakPtr<SEHBDoorWindowPanel>, OwnerWidget)
			SLATE_ARGUMENT(EEHBDoorWindowElementKind, Kind)
			SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OwnerWidget = InArgs._OwnerWidget;
			Kind = InArgs._Kind;
			ChildSlot
			[
				InArgs._Content.Widget
			];
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
			}

			return FReply::Unhandled();
		}

		virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (TSharedPtr<SEHBDoorWindowPanel> Owner = OwnerWidget.Pin())
			{
				return FReply::Handled().BeginDragDrop(Owner->CreateDefaultDoorWindowDragDropOperation(Kind));
			}

			return FReply::Unhandled();
		}

	private:
		TWeakPtr<SEHBDoorWindowPanel> OwnerWidget;
		EEHBDoorWindowElementKind Kind = EEHBDoorWindowElementKind::Window;
	};

	FEasyHouseEditorMode* GetActiveBuildingEditorMode()
	{
		return static_cast<FEasyHouseEditorMode*>(
			GLevelEditorModeTools().GetActiveMode(FEasyHouseEditorMode::EM_EasyHouseEditorModeId));
	}
}

void SEHBDoorWindowPanel::Construct(const FArguments& InArgs)
{
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	LoadDoorWindowTableFromConfig();

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
				.Text(LOCTEXT("DoorWindowPanelTitle", "门窗"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("DoorWindowPanelTitleTip", "门窗工具用于从门窗采样表中选择门窗模板，并在后续步骤中放置到墙体上生成洞口。"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildTablePicker()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildDefaultDoorWindowButtons()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DoorWindowTemplateListTitle", "门窗模板"))
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("DoorWindowTemplateListTitleTip", "这里显示当前门窗采样表中的所有模板。卡片会显示预览图、名称、尺寸、离地高度，并提供删除和定位蓝图按钮。"))
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				BuildCardsArea()
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SAssignNew(StatusTextBlock, STextBlock)
				.Text(FText::GetEmpty())
				.AutoWrapText(true)
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("DoorWindowStatusTip", "显示最近一次刷新、删除或定位操作的结果。"))
			]
		]
	];

	RefreshDoorWindowRows();
}

TSharedRef<FDragDropOperation> SEHBDoorWindowPanel::CreateDoorWindowDragDropOperationForRow(FName RowName)
{
	FEasyHouseEditorMode* EditorMode = GetActiveBuildingEditorMode();
	AEHBBuildingActorBase* Building = EditorMode ? EditorMode->GetActiveBuilding() : nullptr;
	return FEHBDoorWindowDragDropOp::New(EditorMode, Building, DoorWindowTable.Get(), RowName);
}

TSharedRef<FDragDropOperation> SEHBDoorWindowPanel::CreateDefaultDoorWindowDragDropOperation(EEHBDoorWindowElementKind Kind)
{
	FEasyHouseEditorMode* EditorMode = GetActiveBuildingEditorMode();
	AEHBBuildingActorBase* Building = EditorMode ? EditorMode->GetActiveBuilding() : nullptr;
	return FEHBDoorWindowDragDropOp::NewDefault(EditorMode, Building, Kind);
}

TSharedRef<SWidget> SEHBDoorWindowPanel::BuildTablePicker()
{
	return SNew(SBorder)
		.Padding(12.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DoorWindowTableLabel", "门窗采样表"))
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("DoorWindowTableTip", "选择用于管理门窗模板的采样数据表。选择后会自动保存到项目配置文件，下次打开工具时会直接读取。"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UDataTable::StaticClass())
					.ObjectPath(this, &SEHBDoorWindowPanel::GetDoorWindowTablePath)
					.OnObjectChanged(this, &SEHBDoorWindowPanel::HandleDoorWindowTableChanged)
					.OnShouldFilterAsset(this, &SEHBDoorWindowPanel::ShouldFilterDoorWindowTable)
					.AllowClear(true)
					.DisplayThumbnail(false)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("RefreshDoorWindowRowsButton", "刷新"))
					.ToolTipText(LOCTEXT("RefreshDoorWindowRowsButtonTip", "重新读取当前门窗采样表中的所有行。当你在数据表编辑器中修改、添加或删除行后，可以点击这里刷新列表。"))
					.OnClicked(this, &SEHBDoorWindowPanel::HandleRefreshDoorWindowRowsClicked)
				]
			]
		];
}

TSharedRef<SWidget> SEHBDoorWindowPanel::BuildDefaultDoorWindowButtons()
{
	auto MakeDefaultButton = [this](
		EEHBDoorWindowElementKind Kind,
		const FText& Label,
		const FText& Hint,
		const FLinearColor& AccentColor) -> TSharedRef<SWidget>
	{
		return SNew(SEHBDefaultDoorWindowDragHandle)
			.OwnerWidget(SharedThis(this))
			.Kind(Kind)
			[
				SNew(SBox)
				.WidthOverride(150.0f)
				.HeightOverride(48.0f)
				[
					SNew(SBorder)
					.Padding(10.0f, 6.0f)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(AccentColor)
					.ToolTipText(Hint)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(Label)
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.ColorAndOpacity(FStyleColors::Foreground)
							.Justification(ETextJustify::Center)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 3.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("DefaultDoorWindowDragHint", "拖拽到墙体"))
							.ColorAndOpacity(FStyleColors::Foreground)
							.Justification(ETextJustify::Center)
						]
					]
				]
			];
	};

	return SNew(SBorder)
		.Padding(12.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DefaultDoorWindowTitle", "默认门窗"))
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("DefaultDoorWindowTitleTip", "从项目设置中的默认门 Actor 类和默认窗 Actor 类创建门窗；按住按钮拖到墙体上即可放置。"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					MakeDefaultButton(
						EEHBDoorWindowElementKind::Door,
						LOCTEXT("CreateDefaultDoorButton", "默认门"),
						LOCTEXT("CreateDefaultDoorButtonTip", "从项目设置中的默认门 Actor 类创建门，并拖拽放置到墙体上。"),
						FLinearColor(0.16f, 0.12f, 0.08f, 1.0f))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeDefaultButton(
						EEHBDoorWindowElementKind::Window,
						LOCTEXT("CreateDefaultWindowButton", "默认窗"),
						LOCTEXT("CreateDefaultWindowButtonTip", "从项目设置中的默认窗 Actor 类创建窗，并拖拽放置到墙体上。"),
						FLinearColor(0.08f, 0.13f, 0.16f, 1.0f))
				]
			]
		];
}

TSharedRef<SWidget> SEHBDoorWindowPanel::BuildCardsArea()
{
	return SNew(SBorder)
		.Padding(8.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(DoorWindowCardsBox, SWrapBox)
				.UseAllottedSize(true)
			]
		];
}

TSharedRef<SWidget> SEHBDoorWindowPanel::BuildDoorWindowCard(FName RowName)
{
	const FEHBDoorWindowMeshData* Row = DoorWindowTable.IsValid()
		? DoorWindowTable->FindRow<FEHBDoorWindowMeshData>(RowName, TEXT("SEHBDoorWindowPanel::BuildDoorWindowCard"), false)
		: nullptr;

	TSharedRef<SWidget> PreviewWidget = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Recessed)
		.Padding(8.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("ClassIcon.Blueprint"))
				.ColorAndOpacity(FStyleColors::Foreground)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DoorWindowNoPreview", "无预览"))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FStyleColors::Foreground)
			]
		];

	if (UStaticMesh* PreviewMesh = GetPreviewMeshFromRow(Row))
	{
		TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(FAssetData(PreviewMesh), 112, 112, ThumbnailPool);
		DoorWindowThumbnails.Add(Thumbnail);
		PreviewWidget = Thumbnail->MakeThumbnailWidget();
	}

	const FText KindText = Row && Row->Kind == EEHBDoorWindowElementKind::Door
		? LOCTEXT("DoorWindowKindDoor", "门")
		: LOCTEXT("DoorWindowKindWindow", "窗");
	const FText NameText = FText::FromName(RowName);
	const FText SizeText = Row
		? FText::Format(
			LOCTEXT("DoorWindowCardSize", "{0}  宽 {1} / 高 {2} / 厚 {3} cm"),
			KindText,
			FText::AsNumber(Row->OpeningWidth),
			FText::AsNumber(Row->OpeningHeight),
			FText::AsNumber(Row->OpeningThickness))
		: LOCTEXT("DoorWindowCardInvalidSize", "无效门窗数据");
	const FText HeightText = Row
		? FText::Format(LOCTEXT("DoorWindowCardSillHeight", "离地高度 {0} cm"), FText::AsNumber(Row->SillHeight))
		: FText::GetEmpty();
	const FLinearColor CardBackgroundColor = Row && Row->Kind == EEHBDoorWindowElementKind::Door
		? FLinearColor(0.16f, 0.12f, 0.08f, 1.0f)
		: FLinearColor(0.08f, 0.13f, 0.16f, 1.0f);

	return SNew(SEHBDoorWindowDragHandle)
		.OwnerWidget(SharedThis(this))
		.RowName(RowName)
		[
			SNew(SBox)
			.WidthOverride(250.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(CardBackgroundColor)
				.ToolTipText(Row
					? FText::Format(
						LOCTEXT("DoorWindowCardTip", "{0}\n源网格体：{1}\n蓝图类：{2}\n拖拽此卡片到墙体上可创建门窗实例。"),
						NameText,
						Row->SourceStaticMesh.IsNull() ? LOCTEXT("DoorWindowNoSourceMeshTip", "未设置") : FText::FromString(Row->SourceStaticMesh.ToSoftObjectPath().ToString()),
						Row->DoorWindowClass.IsNull() ? LOCTEXT("DoorWindowNoClassTip", "未设置") : FText::FromString(Row->DoorWindowClass.ToSoftObjectPath().ToString()))
					: LOCTEXT("DoorWindowInvalidRowTip", "该数据表行无效或无法读取。"))
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.WidthOverride(112.0f)
						.HeightOverride(112.0f)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							PreviewWidget
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(NameText)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						.ColorAndOpacity(FStyleColors::Foreground)
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(SizeText)
						.ColorAndOpacity(FStyleColors::Foreground)
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(HeightText)
						.ColorAndOpacity(FStyleColors::Foreground)
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("LocateDoorWindowBlueprintButton", "定位蓝图"))
							.ToolTipText(LOCTEXT("LocateDoorWindowBlueprintButtonTip", "在内容浏览器中定位这条门窗数据对应的蓝图资产。"))
							.HAlign(HAlign_Center)
							.IsEnabled(Row != nullptr && !Row->DoorWindowClass.IsNull())
							.OnClicked(this, &SEHBDoorWindowPanel::HandleLocateDoorWindowBlueprint, RowName)
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("DeleteDoorWindowRowButton", "删除"))
							.ToolTipText(LOCTEXT("DeleteDoorWindowRowButtonTip", "从当前门窗采样表中删除这一行。该操作只删除数据表行，不会删除已经生成的蓝图资产。"))
							.HAlign(HAlign_Center)
							.OnClicked(this, &SEHBDoorWindowPanel::HandleDeleteDoorWindowRow, RowName)
						]
					]
				]
			]
		];
}

FString SEHBDoorWindowPanel::GetDoorWindowTablePath() const
{
	return DoorWindowTable.IsValid() ? DoorWindowTable->GetPathName() : FString();
}

void SEHBDoorWindowPanel::HandleDoorWindowTableChanged(const FAssetData& AssetData)
{
	UDataTable* NewTable = Cast<UDataTable>(AssetData.GetAsset());
	DoorWindowTable = NewTable && NewTable->GetRowStruct() == FEHBDoorWindowMeshData::StaticStruct()
		? NewTable
		: nullptr;

	SaveDoorWindowTableToConfig();
	RefreshDoorWindowRows();
}

bool SEHBDoorWindowPanel::ShouldFilterDoorWindowTable(const FAssetData& AssetData) const
{
	const UDataTable* DataTable = Cast<UDataTable>(AssetData.GetAsset());
	return !DataTable || DataTable->GetRowStruct() != FEHBDoorWindowMeshData::StaticStruct();
}

FReply SEHBDoorWindowPanel::HandleRefreshDoorWindowRowsClicked()
{
	ReloadDoorWindowTable();
	RefreshDoorWindowRows();
	return FReply::Handled();
}

void SEHBDoorWindowPanel::RefreshDoorWindowRows()
{
	DoorWindowRowNames.Reset();
	DoorWindowThumbnails.Reset();

	if (!DoorWindowCardsBox.IsValid())
	{
		return;
	}

	DoorWindowCardsBox->ClearChildren();
	if (!DoorWindowTable.IsValid())
	{
		DoorWindowCardsBox->AddSlot()
		.Padding(0.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SBox)
			.WidthOverride(320.0f)
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoDoorWindowTableSelected", "请选择门窗采样表。"))
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]
		];
		SetStatusText(LOCTEXT("NoDoorWindowTableStatus", "未选择门窗采样表。"));
		return;
	}

	if (DoorWindowTable->GetRowStruct() != FEHBDoorWindowMeshData::StaticStruct())
	{
		DoorWindowCardsBox->AddSlot()
		.Padding(0.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SBox)
			.WidthOverride(360.0f)
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("InvalidDoorWindowTableStruct", "当前数据表行结构不是“门窗网格体数据 / FEHBDoorWindowMeshData”。"))
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Error)
				]
			]
		];
		SetStatusText(LOCTEXT("InvalidDoorWindowTableStatus", "门窗采样表行结构不正确。"));
		return;
	}

	for (const FName& RowName : DoorWindowTable->GetRowNames())
	{
		const FEHBDoorWindowMeshData* Row = DoorWindowTable->FindRow<FEHBDoorWindowMeshData>(RowName, TEXT("SEHBDoorWindowPanel::RefreshDoorWindowRows"), false);
		if (Row)
		{
			DoorWindowRowNames.Add(RowName);
		}
	}

	if (DoorWindowRowNames.Num() == 0)
	{
		DoorWindowCardsBox->AddSlot()
		.Padding(0.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SBox)
			.WidthOverride(360.0f)
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoDoorWindowRows", "当前门窗采样表中还没有可用模板。请先到“网格体采样 / 门窗采样”中生成门窗蓝图并写入表格。"))
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]
		];
		SetStatusText(LOCTEXT("NoDoorWindowRowsStatus", "门窗采样表中没有可用模板。"));
		return;
	}

	for (const FName& RowName : DoorWindowRowNames)
	{
		DoorWindowCardsBox->AddSlot()
		.Padding(0.0f, 0.0f, 8.0f, 8.0f)
		[
			BuildDoorWindowCard(RowName)
		];
	}

	SetStatusText(FText::Format(LOCTEXT("DoorWindowRowsRefreshed", "已读取 {0} 个门窗模板。"), FText::AsNumber(DoorWindowRowNames.Num())));
}

void SEHBDoorWindowPanel::SaveDoorWindowTableToConfig() const
{
	UEHBBuildingToolsetSettings* Settings = GetMutableDefault<UEHBBuildingToolsetSettings>();
	if (!Settings)
	{
		return;
	}

	Settings->DefaultDoorWindowMeshDataTable = DoorWindowTable.Get();
	Settings->SaveConfig();
}

void SEHBDoorWindowPanel::LoadDoorWindowTableFromConfig()
{
	const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
	UDataTable* ConfigTable = Settings && !Settings->DefaultDoorWindowMeshDataTable.IsNull()
		? Settings->DefaultDoorWindowMeshDataTable.LoadSynchronous()
		: nullptr;

	DoorWindowTable = ConfigTable && ConfigTable->GetRowStruct() == FEHBDoorWindowMeshData::StaticStruct()
		? ConfigTable
		: nullptr;
}

void SEHBDoorWindowPanel::ReloadDoorWindowTable()
{
	if (!DoorWindowTable.IsValid())
	{
		return;
	}

	const FString TablePath = DoorWindowTable->GetPathName();
	if (!TablePath.IsEmpty())
	{
		if (UDataTable* ReloadedTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *TablePath)))
		{
			DoorWindowTable = ReloadedTable;
		}
	}
}

FReply SEHBDoorWindowPanel::HandleDeleteDoorWindowRow(FName RowName)
{
	if (!DoorWindowTable.IsValid() || RowName.IsNone())
	{
		return FReply::Handled();
	}

	const FText ConfirmText = FText::Format(
		LOCTEXT("DeleteDoorWindowRowConfirm", "确认从门窗采样表中删除“{0}”？\n这只会删除数据表行，不会删除已经生成的蓝图资产。"),
		FText::FromName(RowName));
	if (FMessageDialog::Open(EAppMsgType::YesNo, ConfirmText) != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteDoorWindowRowTransaction", "删除门窗模板行"));
	UDataTable* TableToEdit = DoorWindowTable.Get();
	TableToEdit->Modify();
	if (FDataTableEditorUtils::RemoveRow(TableToEdit, RowName))
	{
		TableToEdit->MarkPackageDirty();
		SetStatusText(FText::Format(LOCTEXT("DoorWindowRowDeleted", "已删除门窗模板行 {0}。"), FText::FromName(RowName)));
		RefreshDoorWindowRows();
	}

	return FReply::Handled();
}

FReply SEHBDoorWindowPanel::HandleLocateDoorWindowBlueprint(FName RowName) const
{
	const FEHBDoorWindowMeshData* Row = DoorWindowTable.IsValid()
		? DoorWindowTable->FindRow<FEHBDoorWindowMeshData>(RowName, TEXT("SEHBDoorWindowPanel::HandleLocateDoorWindowBlueprint"), false)
		: nullptr;
	if (!Row || Row->DoorWindowClass.IsNull() || !GEditor)
	{
		return FReply::Handled();
	}

	UClass* DoorWindowClass = Row->DoorWindowClass.LoadSynchronous();
	UObject* ObjectToSync = nullptr;
	if (DoorWindowClass)
	{
		ObjectToSync = DoorWindowClass->ClassGeneratedBy ? DoorWindowClass->ClassGeneratedBy : DoorWindowClass;
	}

	if (ObjectToSync)
	{
		TArray<UObject*> ObjectsToSync;
		ObjectsToSync.Add(ObjectToSync);
		GEditor->SyncBrowserToObjects(ObjectsToSync);
	}

	return FReply::Handled();
}

UStaticMesh* SEHBDoorWindowPanel::GetPreviewMeshFromRow(const FEHBDoorWindowMeshData* Row) const
{
	if (!Row)
	{
		return nullptr;
	}

	if (UStaticMesh* SourceMesh = Row->SourceStaticMesh.LoadSynchronous())
	{
		return SourceMesh;
	}

	UClass* DoorWindowClass = Row->DoorWindowClass.LoadSynchronous();
	const AEHB_DoorWindow* DefaultDoorWindow = DoorWindowClass ? Cast<AEHB_DoorWindow>(DoorWindowClass->GetDefaultObject()) : nullptr;
	return DefaultDoorWindow ? DefaultDoorWindow->SourceStaticMesh.LoadSynchronous() : nullptr;
}

void SEHBDoorWindowPanel::SetStatusText(const FText& NewStatus) const
{
	if (StatusTextBlock.IsValid())
	{
		StatusTextBlock->SetText(NewStatus);
	}
}

#undef LOCTEXT_NAMESPACE
