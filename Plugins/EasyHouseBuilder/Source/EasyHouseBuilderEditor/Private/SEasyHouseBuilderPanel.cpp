// Copyright Epic Games, Inc. All Rights Reserved.

#include "SEasyHouseBuilderPanel.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "EHBRailingFeedback.h"
#include "EHBBuildingCopy.h"
#include "Toolsets/EHBBuildingToolset.h"
#include "Core/EHBWallTopology.h"
#include "JsonObjectConverter.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"


#include "AI/EHBAIWorkflowExecutor.h"
#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHBGableRoof.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Railing.h"
#include "Actors/EHB_Stair.h"
#include "Actors/EHB_Wall.h"
#include "EHB_Building.h"
#include "Async/Async.h"
#include "AssetRegistry/AssetData.h"
#include "AssetThumbnail.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Core/EHBBuildingActorBase.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "DesktopPlatformModule.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Interfaces/IPluginManager.h"
#include "IDesktopPlatform.h"
#include "HttpModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "LevelEditorViewport.h"
#include "EasyHouseEditorMode.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sampling/EHBPillarMeshData.h"
#include "Sampling/EHBRailingMeshData.h"
#include "Sampling/EHBWallMeshData.h"
#include "Settings/EHBAISettings.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "EasyHouseBuilderEditorStyle.h"
#include "Widgets/SEHBElementEditorPanel.h"
#include "Widgets/SEHBDoorWindowPanel.h"
#include "Widgets/SEHBMeshSamplingPanel.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
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

#define LOCTEXT_NAMESPACE "SEasyHouseBuilderPanel"

namespace EasyHouseBuilderPanel
{
	/** 工具按钮与右侧面板的一一对应关系。 */
	struct FToolPanelDefinition
	{
		EEasyHouseToolPanel Panel;
		FText Label;
		FName IconName;
	};

	/** 返回当前建筑编辑模式支持的所有工具面板定义。 */
	static TArray<FToolPanelDefinition> GetPanelDefinitions()
	{
		return {
			{ EEasyHouseToolPanel::MeshSampling, LOCTEXT("MeshSampling", "\u7f51\u683c\u4f53\u91c7\u6837"), "EasyHouseBuilder.MeshSampling" },
			{ EEasyHouseToolPanel::BuildingSelection, LOCTEXT("BuildingSelection", "\u9009\u62e9\u5efa\u7b51\u5bf9\u8c61"), "EasyHouseBuilder.BuildingSelection" },
			{ EEasyHouseToolPanel::FoundationAndFloor, LOCTEXT("FoundationAndFloor", "\u5730\u57fa/\u5c42\u677f"), "EasyHouseBuilder.FoundationAndFloor" },
			{ EEasyHouseToolPanel::Walls, LOCTEXT("Walls", "\u5899\u4f53"), "EasyHouseBuilder.Walls" },
			{ EEasyHouseToolPanel::DoorsAndWindows, LOCTEXT("DoorsAndWindows", "\u95e8\u7a97"), "EasyHouseBuilder.DoorsAndWindows" },
			{ EEasyHouseToolPanel::Railings, LOCTEXT("Railings", "\u6276\u624b"), "EasyHouseBuilder.Railings" },
			{ EEasyHouseToolPanel::Roof, LOCTEXT("Roof", "\u623f\u9876"), "EasyHouseBuilder.Roof" },
			{ EEasyHouseToolPanel::Floor, LOCTEXT("Floor", "\u5730\u677f"), "EasyHouseBuilder.Floor" },
			{ EEasyHouseToolPanel::Stairs, LOCTEXT("Stairs", "\u697c\u68af"), "EasyHouseBuilder.Stairs" }
		};
	}

	/** 创建建筑对象时使用的最大射线距离，单位为厘米。 */
	static AEHBBuildingActorBase* GetEditorSelectedBuilding()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (!SelectedActors)
		{
			return nullptr;
		}

		for (FSelectionIterator It(*SelectedActors); It; ++It)
		{
			if (AEHBBuildingActorBase* Building = Cast<AEHBBuildingActorBase>(*It))
			{
				return Building;
			}
		}

		return nullptr;
	}

	static constexpr double PlacementTraceDistance = 1000000.0;

	/** 当前视口没有命中任何碰撞时使用的备用生成距离，单位为厘米。 */
	static constexpr double FallbackSpawnDistance = 1000.0;

	static const TCHAR* DefaultAICreationSystemPrompt = TEXT(
		"Existing EHB_Building rule: reuse the current existing EHB_Building object. Do not create a new root building object for ordinary generation, continuation, regeneration, or modification. If no existing building is active or selected, ask the user to select or manually create one first. "
		"你是 EasyHouseBuilder 的 MCP 建筑设计代理。"
		"必须先调用 bpt_get_status，再调用 bpt_get_building_snapshot。"
		"大改动先使用 bpt_preview_building_patch 预览，再 bpt_validate_building 校验。"
		"建筑由柱子点位和柱子关系驱动，共用墙角必须复用柱子，墙被拆分时只能输出相邻短墙。"
		"房间地板和顶部层板应由 generate_room_surfaces 生成。"
		"屋顶生成功能暂时关闭：不要生成屋顶，不要调用屋顶工具，也不要虚构屋顶操作。"
		"默认按 UE 厘米单位进行真实住宅尺度设计：墙高/层高建议 300-330cm，主要房间短边不小于 300cm，走廊净宽不小于 100cm，楼梯宽度不小于 90cm；如需楼梯，必须先预留楼梯间、落脚平台和洞口，空间不足时应扩大房屋轮廓或减少房间数量。"
		"室内房间必须形成可达动线：卧室、厨房、卫生间等主要房间应通过门连接到走廊、门厅、客厅或楼梯间；不要生成无门的封闭房间。"
		"楼梯、楼板和洞口必须作为一个整体设计：楼梯底端落在下层可通行地面，上端落在上层楼板/平台高度，洞口必须覆盖楼梯通行长度和宽度，不能出现楼梯悬空、穿板或与层板断开的结果。");

	static const TCHAR* AICreationSystemPromptRelativePath = TEXT("Resources/Prompts/MCPArchitectSystemPrompt.txt");

	static FString GetDefaultAITestBuildingJson()
	{
		if (FPlatformTime::Seconds() >= 0.0)
		{
			return TEXT(R"JSON({
  "schema": "EHB_AI_Workflow_v2",
  "building": {
    "name": "AI_Whole_Workflow_Test",
    "wallHeight": 300,
    "wallThickness": 24,
    "pillarWidth": 36,
    "pillarDepth": 36,
    "floorIndex": 1,
    "pillarMergeTolerance": 5,
    "foundationExpansion": 60
  },
  "foundation": {
    "id": "Raised_Foundation",
    "type": "foundation",
    "thickness": 70,
    "z": 80,
    "visualExpansion": 60,
    "polygon": [
      [-420, -240, 0],
      [420, -240, 0],
      [420, 280, 0],
      [-420, 280, 0]
    ]
  },
  "floors": [
    {
      "floor": 1,
      "baseZ": 80,
      "wallHeight": 300,
      "wallThickness": 24,
      "ceilingSlabThickness": 18,
      "pillars": [
        { "id": "F1_P0", "location": [-420, -240, 80] },
        { "id": "F1_P1", "location": [420, -240, 80] },
        { "id": "F1_P2", "location": [420, 280, 80] },
        { "id": "F1_P3", "location": [-420, 280, 80] },
        { "id": "F1_P4", "location": [0, -240, 80] },
        { "id": "F1_P5", "location": [0, 280, 80] },
        { "id": "F1_P6", "location": [420, 20, 80] },
        { "id": "F1_P7", "location": [0, 20, 80] }
      ],
      "pillarRelations": [
        { "start": "F1_P0", "end": "F1_P4" },
        { "start": "F1_P4", "end": "F1_P1" },
        { "start": "F1_P1", "end": "F1_P6" },
        { "start": "F1_P6", "end": "F1_P2" },
        { "start": "F1_P2", "end": "F1_P5" },
        { "start": "F1_P5", "end": "F1_P3" },
        { "start": "F1_P3", "end": "F1_P0" },
        { "start": "F1_P4", "end": "F1_P7" },
        { "start": "F1_P7", "end": "F1_P5" },
        { "start": "F1_P7", "end": "F1_P6" }
      ]
    }
  ],
  "autoRoomSurfaces": true
})JSON");
		}

		return TEXT(R"JSON({
  "schema": "EHB_AI_Workflow_v2",
  "building": {
    "name": "AI_Test_Room_Workflow_House",
    "wallHeight": 300,
    "wallThickness": 20,
    "pillarWidth": 24,
    "pillarDepth": 24,
    "floorIndex": 1,
    "pillarMergeTolerance": 5
  },
  "floors": [
    { "floor": 1, "baseZ": 80, "wallHeight": 300, "wallThickness": 20, "ceilingSlabThickness": 16 }
  ],
  "pillars": [
    { "id": "P0", "floor": 1, "location": [0, 0, 80] },
    { "id": "P4", "floor": 1, "location": [320, 0, 80] },
    { "id": "P1", "floor": 1, "location": [800, 0, 80] },
    { "id": "P6", "floor": 1, "location": [800, 260, 80] },
    { "id": "P2", "floor": 1, "location": [800, 600, 80] },
    { "id": "P5", "floor": 1, "location": [320, 600, 80] },
    { "id": "P3", "floor": 1, "location": [0, 600, 80] },
    { "id": "P7", "floor": 1, "location": [320, 260, 80] }
  ],
  "rooms": [
    { "id": "R_Left", "name": "左侧房间", "floor": 1, "pillarLoop": ["P0", "P4", "P7", "P5", "P3"] },
    { "id": "R_FrontRight", "name": "右前房间", "floor": 1, "pillarLoop": ["P4", "P1", "P6", "P7"] },
    { "id": "R_BackRight", "name": "右后房间", "floor": 1, "pillarLoop": ["P7", "P6", "P2", "P5"] }
  ],
  "foundation": {
    "id": "Raised_Foundation",
    "type": "foundation",
    "thickness": 60,
    "z": 80,
    "outline": ["P0", "P1", "P2", "P3"]
  },
  "roomCeilingSlabs": {
    "enabled": true,
    "defaultThickness": 16
  }
})JSON");
	}

}

namespace
{
	class FEHBWallSurfaceDragDropOp : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FEHBWallSurfaceDragDropOp, FDecoratedDragDropOp)

		static TSharedRef<FEHBWallSurfaceDragDropOp> New(
			FEasyHouseEditorMode* InEditorMode,
			AEHBBuildingActorBase* InBuilding,
			UDataTable* InTable,
			FName InRowName,
			bool bInCoverBothSides,
			bool bInFlipSampleSides)
		{
			TSharedRef<FEHBWallSurfaceDragDropOp> Operation = MakeShared<FEHBWallSurfaceDragDropOp>();
			Operation->EditorMode = InEditorMode;

			if (!InEditorMode)
			{
				Operation->CurrentHoverText = LOCTEXT("DragWallSurfaceNoMode", "\u8bf7\u5148\u8fdb\u5165\u5efa\u7b51\u7f16\u8f91\u6a21\u5f0f");
			}
			else if (!InBuilding)
			{
				Operation->CurrentHoverText = LOCTEXT("DragWallSurfaceNoBuilding", "\u8bf7\u5148\u9009\u62e9\u5efa\u7b51\u5bf9\u8c61");
			}
			else
			{
				Operation->CurrentHoverText = LOCTEXT("DragWallSurfaceHover", "\u62d6\u5230\u5899\u4f53\u4e0a\u8986\u76d6\u5899\u9762");
				InEditorMode->BeginWallSurfacePlacement(
					InBuilding,
					InTable,
					InRowName,
					bInCoverBothSides,
					bInFlipSampleSides);
			}

			Operation->CurrentIconBrush = FAppStyle::GetBrush("ClassIcon.DataTable");
			Operation->SetupDefaults();
			Operation->Construct();
			return Operation;
		}

		virtual ~FEHBWallSurfaceDragDropOp() override
		{
			if (EditorMode)
			{
				EditorMode->CancelWallSurfacePlacement();
			}
		}

		virtual void OnDragged(const FDragDropEvent& DragDropEvent) override
		{
			FDecoratedDragDropOp::OnDragged(DragDropEvent);
			if (EditorMode)
			{
				EditorMode->UpdateWallSurfacePlacementFromPointerEvent(DragDropEvent);
			}
		}

		virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
		{
			FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);
			if (EditorMode)
			{
				EditorMode->FinishWallSurfacePlacementFromPointerEvent(MouseEvent);
				EditorMode = nullptr;
			}
		}

	private:
		FEasyHouseEditorMode* EditorMode = nullptr;
	};

	class SEHBWallSurfaceDragHandle : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEHBWallSurfaceDragHandle) {}
			SLATE_ARGUMENT(TWeakPtr<SEasyHouseBuilderPanel>, OwnerWidget)
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
			if (TSharedPtr<SEasyHouseBuilderPanel> Owner = OwnerWidget.Pin())
			{
				return FReply::Handled().BeginDragDrop(Owner->CreateWallSurfaceDragDropOperationForRow(RowName));
			}

			return FReply::Unhandled();
		}

	private:
		TWeakPtr<SEasyHouseBuilderPanel> OwnerWidget;
		FName RowName = NAME_None;
	};

	class FEHBPillarMeshDragDropOp : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FEHBPillarMeshDragDropOp, FDecoratedDragDropOp)

		static TSharedRef<FEHBPillarMeshDragDropOp> New(
			FEasyHouseEditorMode* InEditorMode,
			AEHBBuildingActorBase* InBuilding,
			UDataTable* InTable,
			FName InRowName)
		{
			TSharedRef<FEHBPillarMeshDragDropOp> Operation = MakeShared<FEHBPillarMeshDragDropOp>();
			Operation->EditorMode = InEditorMode;

			if (!InEditorMode)
			{
				Operation->CurrentHoverText = LOCTEXT("DragPillarMeshNoMode", "\u8bf7\u5148\u8fdb\u5165\u5efa\u7b51\u7f16\u8f91\u6a21\u5f0f");
			}
			else if (!InBuilding)
			{
				Operation->CurrentHoverText = LOCTEXT("DragPillarMeshNoBuilding", "\u8bf7\u5148\u9009\u62e9\u5efa\u7b51\u5bf9\u8c61");
			}
			else
			{
				Operation->CurrentHoverText = LOCTEXT("DragPillarMeshHover", "\u62d6\u5230\u67f1\u4f53\u4e0a\u5e94\u7528\u67f1\u4f53\u91c7\u6837");
				InEditorMode->BeginPillarMeshPlacement(InBuilding, InTable, InRowName);
			}

			Operation->CurrentIconBrush = FAppStyle::GetBrush("ClassIcon.DataTable");
			Operation->SetupDefaults();
			Operation->Construct();
			return Operation;
		}

		virtual ~FEHBPillarMeshDragDropOp() override
		{
			if (EditorMode)
			{
				EditorMode->CancelPillarMeshPlacement();
			}
		}

		virtual void OnDragged(const FDragDropEvent& DragDropEvent) override
		{
			FDecoratedDragDropOp::OnDragged(DragDropEvent);
			if (EditorMode)
			{
				EditorMode->UpdatePillarMeshPlacementFromPointerEvent(DragDropEvent);
			}
		}

		virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
		{
			FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);
			if (EditorMode)
			{
				EditorMode->FinishPillarMeshPlacementFromPointerEvent(MouseEvent);
				EditorMode = nullptr;
			}
		}

	private:
		FEasyHouseEditorMode* EditorMode = nullptr;
	};

	class SEHBPillarMeshDragHandle : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEHBPillarMeshDragHandle) {}
			SLATE_ARGUMENT(TWeakPtr<SEasyHouseBuilderPanel>, OwnerWidget)
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
			if (TSharedPtr<SEasyHouseBuilderPanel> Owner = OwnerWidget.Pin())
			{
				return FReply::Handled().BeginDragDrop(Owner->CreatePillarMeshDragDropOperationForRow(RowName));
			}

			return FReply::Unhandled();
		}

	private:
		TWeakPtr<SEasyHouseBuilderPanel> OwnerWidget;
		FName RowName = NAME_None;
	};

	class FEHBRailingMeshDragDropOp : public FDecoratedDragDropOp
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FEHBRailingMeshDragDropOp, FDecoratedDragDropOp)

		static TSharedRef<FEHBRailingMeshDragDropOp> New(
			FEasyHouseEditorMode* InEditorMode,
			AEHBBuildingActorBase* InBuilding,
			UDataTable* InTable,
			FName InRowName,
			bool bApplyToSinglePost)
		{
			TSharedRef<FEHBRailingMeshDragDropOp> Operation = MakeShared<FEHBRailingMeshDragDropOp>();
			Operation->EditorMode = InEditorMode;

			if (!InEditorMode)
			{
				Operation->CurrentHoverText = LOCTEXT("DragRailingMeshNoMode", "\u8bf7\u5148\u8fdb\u5165\u5efa\u7b51\u7f16\u8f91\u6a21\u5f0f");
			}
			else if (!InBuilding)
			{
				Operation->CurrentHoverText = LOCTEXT("DragRailingMeshNoBuilding", "\u8bf7\u5148\u9009\u62e9\u5efa\u7b51\u5bf9\u8c61");
			}
			else
			{
				Operation->CurrentHoverText = bApplyToSinglePost
					? LOCTEXT("DragRailingMeshSinglePostHover", "拖到已有扶手柱子上替换单根柱子")
					: LOCTEXT("DragRailingMeshHover", "拖到已有扶手上应用完整扶手采样");
				InEditorMode->BeginRailingMeshPlacement(InBuilding, InTable, InRowName, bApplyToSinglePost);
			}

			Operation->CurrentIconBrush = FAppStyle::GetBrush("ClassIcon.DataTable");
			Operation->SetupDefaults();
			Operation->Construct();
			return Operation;
		}

		virtual ~FEHBRailingMeshDragDropOp() override
		{
			if (EditorMode)
			{
				EditorMode->CancelRailingMeshPlacement();
			}
		}

		virtual void OnDragged(const FDragDropEvent& DragDropEvent) override
		{
			FDecoratedDragDropOp::OnDragged(DragDropEvent);
			if (EditorMode)
			{
				EditorMode->UpdateRailingMeshPlacementFromPointerEvent(DragDropEvent);
			}
		}

		virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override
		{
			FDecoratedDragDropOp::OnDrop(bDropWasHandled, MouseEvent);
			if (EditorMode)
			{
				EditorMode->FinishRailingMeshPlacementFromPointerEvent(MouseEvent);
				EditorMode = nullptr;
			}
		}

	private:
		FEasyHouseEditorMode* EditorMode = nullptr;
	};

	class SEHBRailingMeshDragHandle : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SEHBRailingMeshDragHandle) {}
			SLATE_ARGUMENT(TWeakPtr<SEasyHouseBuilderPanel>, OwnerWidget)
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
			if (TSharedPtr<SEasyHouseBuilderPanel> Owner = OwnerWidget.Pin())
			{
				return FReply::Handled().BeginDragDrop(Owner->CreateRailingMeshDragDropOperationForRow(RowName));
			}

			return FReply::Unhandled();
		}

	private:
		TWeakPtr<SEasyHouseBuilderPanel> OwnerWidget;
		FName RowName = NAME_None;
	};
}

void SEasyHouseBuilderPanel::Construct(const FArguments& InArgs)
{
	// 先构建右侧所有面板内容，再把它们交给 WidgetSwitcher 管理。
	const TArray<EasyHouseBuilderPanel::FToolPanelDefinition> PanelDefinitions = EasyHouseBuilderPanel::GetPanelDefinitions();

	TSharedRef<SWidgetSwitcher> ContentSwitcher =
		SAssignNew(PanelSwitcher, SWidgetSwitcher)
		.WidgetIndex(static_cast<int32>(ActivePanel));

	for (const EasyHouseBuilderPanel::FToolPanelDefinition& Definition : PanelDefinitions)
	{
		ContentSwitcher->AddSlot()
		[
			BuildPanelContent(Definition.Panel, Definition.Label)
		];
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Recessed)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				BuildToolList()
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(1.0f, 0.0f, 0.0f, 0.0f)
			[
				ContentSwitcher
			]
		]
	];

	// “选择建筑对象”面板在构造时就会创建列表容器，这里立即刷新一次，打开模式即可看到当前关卡已有建筑。
	RefreshBuildingList();
	RefreshWallSurfaceRows();
	RefreshPillarMeshRows();
	RefreshRailingMeshRows();
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildElementEditorHost()
{
	TSharedRef<SEHBElementEditorPanel> NewEditorPanel = SNew(SEHBElementEditorPanel);
	ElementEditorPanel = NewEditorPanel;
	return SAssignNew(ElementEditorHost, SBox)
		[
			NewEditorPanel
		];
}

void SEasyHouseBuilderPanel::EnsureElementEditorPanel()
{
	if (!ElementEditorHost.IsValid() || ElementEditorPanel.IsValid())
	{
		return;
	}

	TSharedRef<SEHBElementEditorPanel> NewEditorPanel = SNew(SEHBElementEditorPanel);
	ElementEditorPanel = NewEditorPanel;
	ElementEditorHost->SetContent(NewEditorPanel);
}

void SEasyHouseBuilderPanel::SetSelectedElementForEditor(AActor* SelectedActor)
{
	if (!SelectedActor && !ElementEditorPanel.IsValid())
	{
		return;
	}

	EnsureElementEditorPanel();
	if (ElementEditorPanel.IsValid())
	{
		ElementEditorPanel->SetSelectedActor(SelectedActor);
	}
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildToolList()
{
	// 左侧工具栏使用一列固定宽度按钮，和 UE 建模模式的工具列表保持接近的交互节奏。
	const TArray<EasyHouseBuilderPanel::FToolPanelDefinition> PanelDefinitions = EasyHouseBuilderPanel::GetPanelDefinitions();

	TSharedRef<SVerticalBox> ButtonList = SNew(SVerticalBox);
	for (const EasyHouseBuilderPanel::FToolPanelDefinition& Definition : PanelDefinitions)
	{
		ButtonList->AddSlot()
		.AutoHeight()
		.Padding(4.0f, 3.0f)
		[
			BuildPanelButton(Definition.Panel, Definition.Label, Definition.IconName)
		];
	}

	return SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SBox)
			.WidthOverride(96.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					ButtonList
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildPanelButton(EEasyHouseToolPanel Panel, const FText& Label, FName IconName)
{
	// 这里使用插件自带 SVG 图标，避免不同 UE 内置图标混用造成风格不统一。
	return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "PlacementBrowser.Tab")
		.IsEnabled(this, &SEasyHouseBuilderPanel::IsPanelEnabled, Panel)
		.IsChecked(this, &SEasyHouseBuilderPanel::IsPanelChecked, Panel)
		.OnCheckStateChanged(this, &SEasyHouseBuilderPanel::HandlePanelSelectionChanged, Panel)
		.ToolTipText(this, &SEasyHouseBuilderPanel::GetPanelButtonToolTipText, Panel, Label)
		[
			SNew(SBox)
			.WidthOverride(88.0f)
			.HeightOverride(58.0f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(0.0f, 7.0f, 0.0f, 2.0f)
				[
					SNew(SImage)
					.Image(FEasyHouseBuilderEditorStyle::Get().GetBrush(IconName))
					.ColorAndOpacity(FStyleColors::Foreground)
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(3.0f, 0.0f, 3.0f, 5.0f)
				[
					SNew(STextBlock)
					.Text(Label)
					.TextStyle(FAppStyle::Get(), "PlacementBrowser.Tab.Text")
					.Justification(ETextJustify::Center)
					.AutoWrapText(true)
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildPanelContent(EEasyHouseToolPanel Panel, const FText& Label)
{
	if (Panel == EEasyHouseToolPanel::MeshSampling)
	{
		return SNew(SEHBMeshSamplingPanel);
	}

	if (Panel == EEasyHouseToolPanel::BuildingSelection)
	{
		return BuildBuildingSelectionPanel();
	}

	if (Panel == EEasyHouseToolPanel::FoundationAndFloor)
	{
		return BuildFoundationAndFloorPanel();
	}

	if (Panel == EEasyHouseToolPanel::Walls)
	{
		return BuildWallsPanel();
	}

	if (Panel == EEasyHouseToolPanel::DoorsAndWindows)
	{
		return SNew(SEHBDoorWindowPanel);
	}

	if (Panel == EEasyHouseToolPanel::Railings)
	{
		return BuildRailingsPanel();
	}

	if (Panel == EEasyHouseToolPanel::Roof)
	{
		return BuildRoofPanel();
	}

	if (Panel == EEasyHouseToolPanel::Floor)
	{
		return BuildFloorPanel();
	}

	if (Panel == EEasyHouseToolPanel::Stairs)
	{
		return BuildStairsPanel();
	}

	// 其余面板先提供统一的参数区壳，后续具体工具逻辑会逐步替换这里的占位内容。
	return SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FStyleColors::Foreground)
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
				SNew(SBorder)
				.Padding(12.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("PanelPlaceholder", "{0} \u5de5\u5177\u53c2\u6570\u533a"), Label))
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildBuildingSelectionPanel()
{
	return SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BuildingSelectionTitle", "\u9009\u62e9\u5efa\u7b51\u5bf9\u8c61"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("BuildingSelectionTitleTip", "\u5728\u8fd9\u4e2a\u9762\u677f\u4e2d\u521b\u5efa\u6216\u9009\u62e9\u5f53\u524d\u7f16\u8f91\u7684\u5efa\u7b51\u5bf9\u8c61\u3002\u540e\u7eed\u6240\u6709\u5899\u4f53\u3001\u95e8\u7a97\u3001\u697c\u68af\u7b49\u64cd\u4f5c\u90fd\u4f1a\u4ee5\u5b83\u4e3a\u4e0a\u4e0b\u6587\u3002"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 8.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("CreateBuildingButton", "\u521b\u5efa\u5efa\u7b51\u5bf9\u8c61"))
				.ToolTipText(LOCTEXT("CreateBuildingButtonTip", "\u4ece\u5f53\u524d\u573a\u666f\u89c6\u53e3\u7684\u4e2d\u5fc3\u5411\u524d\u53d1\u5c04\u4e00\u6761 Visibility \u5c04\u7ebf\u3002\u5982\u679c\u547d\u4e2d\u573a\u666f\u78b0\u649e\uff0c\u5c31\u5728\u547d\u4e2d\u70b9\u751f\u6210\u5efa\u7b51\u5bf9\u8c61\uff1b\u5982\u679c\u672a\u547d\u4e2d\uff0c\u5219\u5728\u76f8\u673a\u524d\u65b9\u4e00\u6bb5\u8ddd\u79bb\u5904\u751f\u6210\u3002\u65b0\u5efa\u5bf9\u8c61\u4f1a\u81ea\u52a8\u6210\u4e3a\u5f53\u524d\u5efa\u7b51\u5bf9\u8c61\u3002"))
				.HAlign(HAlign_Center)
				.OnClicked(this, &SEasyHouseBuilderPanel::HandleCreateBuildingClicked)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[
				SNew(SButton).Text(LOCTEXT("CopyWholeBuildingButton","复制当前建筑"))
				.ToolTipText(LOCTEXT("CopyWholeBuildingButtonTip","在世界 X 正方向放置独立副本，间距为建筑宽度加 200 cm，可整体撤销。支持已准备连接的原生墙柱和完整房间地板/层板，允许没有实体柱身；门窗、扶手、屋顶及其他复杂构件尚未接入。"))
				.IsEnabled_Lambda([this](){return ActiveBuilding.IsValid()&&GEditor&&!GEditor->PlayWorld&&!GEditor->IsTransactionActive()&&EasyHouseBuilderPanel::GetEditorSelectedBuilding()==ActiveBuilding.Get();})
				.OnClicked(this,&SEasyHouseBuilderPanel::HandleCopyBuildingClicked)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
			[
				SNew(SButton)
				.Text_Lambda([this](){return ActiveBuilding.IsValid()&&ActiveBuilding->WallNodeAuthority.Version==2?LOCTEXT("WallNodeEditingEnabled","墙角联动已启用"):LOCTEXT("EnableWallNodeEditing","启用墙角联动");})
				.ToolTipText(LOCTEXT("EnableWallNodeEditingTip","启用墙角移动与房间地板、层板联动，并允许独立移除柱身。已有柱身保留，整个启用操作可撤销。当前支持空建筑及原生直墙、柱子、完整房间地板和层板；其他构件需要后续适配。"))
				.IsEnabled_Lambda([this](){return ActiveBuilding.IsValid()&&ActiveBuilding->WallNodeAuthority.Version!=2&&GEditor&&!GEditor->PlayWorld&&!GEditor->IsTransactionActive()&&GEditor->GetSelectedActors()->Num()==1&&GEditor->GetSelectedActors()->IsSelected(ActiveBuilding.Get());})
				.OnClicked(this,&SEasyHouseBuilderPanel::HandleEnableWallNodeEditingClicked)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SAssignNew(BuildingSelectionHintText, STextBlock)
				.Text(FText::GetEmpty())
				.ColorAndOpacity(FStyleColors::Warning)
				.AutoWrapText(true)
				.ToolTipText(LOCTEXT("BuildingSelectionHintTip", "\u5f53\u5176\u4ed6\u5de5\u5177\u9700\u8981\u5148\u9009\u62e9\u5efa\u7b51\u5bf9\u8c61\u65f6\uff0c\u8fd9\u91cc\u4f1a\u663e\u793a\u5f15\u5bfc\u63d0\u793a\u3002"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("BuildingListTitle", "\u5f53\u524d\u573a\u666f\u4e2d\u7684\u5efa\u7b51\u5bf9\u8c61"))
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("BuildingListTitleTip", "\u8fd9\u91cc\u4f1a\u663e\u793a\u5f53\u524d\u7f16\u8f91\u4e16\u754c\u4e2d\u6240\u6709\u7ee7\u627f AEHBBuildingActorBase \u7684\u5efa\u7b51 Actor\u3002\u70b9\u51fb\u6761\u76ee\u53ef\u5c06\u5176\u8bbe\u4e3a\u5f53\u524d\u5efa\u7b51\u5bf9\u8c61\u3002"))
			]

			+ SVerticalBox::Slot()
			.FillHeight(0.45f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(BuildingListBox, SVerticalBox)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.FillHeight(0.55f)
			[
				BuildElementEditorHost()
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildAICreationPanel()
{
	AIConversationScrollBox.Reset();
	AIMessageListBox.Reset();
	AIImageAttachmentListBox.Reset();
	ActiveAIResponseTextWidget.Reset();
	ActiveAIProgressPercent = 0;

	const UEHBAISettings* AISettings = GetDefault<UEHBAISettings>();
	const FText EmbeddedChatSummary = AISettings && AISettings->bEnableEmbeddedAIChat
		? FText::Format(
			LOCTEXT("EmbeddedChatEnabledWithMode", "已启用（{0}）"),
			AISettings->EmbeddedChatProvider == EEHBAIEmbeddedChatProvider::LocalCodex
				? LOCTEXT("EmbeddedChatModeLocalCodex", "本地 Codex")
				: LOCTEXT("EmbeddedChatModeHTTPAPI", "HTTP API"))
		: LOCTEXT("EmbeddedChatDisabled", "未启用");
	const FText AISettingsSummary = AISettings
		? FText::Format(
			LOCTEXT("AISettingsSummary", "MCP：{0}    内置对话：{1}    服务：{2}\n桥目录：{3}\n最近生成：{4}"),
			AISettings->bEnableBuildingAIMCP ? LOCTEXT("AIEnabled", "\u5df2\u542f\u7528") : LOCTEXT("AIDisabled", "\u672a\u542f\u7528"),
			EmbeddedChatSummary,
			FText::FromString(AISettings->MCPServerName),
			FText::FromString(AISettings->GetBridgeRootDir()),
			AISettings->LastGeneratedStatus.IsEmpty() ? LOCTEXT("AIMCPNotGenerated", "\u5c1a\u672a\u751f\u6210 MCP \u5ba2\u6237\u7aef\u914d\u7f6e") : FText::FromString(AISettings->LastGeneratedStatus))
		: LOCTEXT("AISettingsUnavailable", "\u672a\u627e\u5230 AI \u914d\u7f6e\u3002");
	return SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AICreationPanelTitle", "AI \u521b\u5efa"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("AICreationPanelTitleTip", "\u5efa\u7b51 AI \u5df2\u6539\u4e3a MCP \u63a5\u5165\uff1a\u8bf7\u5728 Codex\u3001Cursor \u6216 Claude Desktop \u7b49 MCP \u5ba2\u6237\u7aef\u4e2d\u8c03\u7528 bpt-unreal \u5de5\u5177\u8bfb\u53d6\u548c\u4fee\u6539\u5f53\u524d UE \u5efa\u7b51\u3002"))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Header)
				[
					SNew(STextBlock)
					.Text(AISettingsSummary)
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MCPPromptTitle", "AI 隐式规范"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
					.ColorAndOpacity(FStyleColors::Foreground)
					.ToolTipText(LOCTEXT("MCPPromptTitleTip", "完整建筑生成规范会通过 MCP initialize instructions 自动下发给 AI，不会作为普通聊天消息显示给用户。"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("CopyMCPStarterPromptButton", "复制启动语"))
					.HAlign(HAlign_Center)
					.ToolTipText(LOCTEXT("CopyMCPStarterPromptButtonTip", "复制一段适合粘贴到 Codex、Cursor 或 Claude Desktop 的开场请求，让 AI 先检查 UE 连接并读取当前建筑。"))
					.OnClicked(this, &SEasyHouseBuilderPanel::HandleCopyMCPStarterPromptClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CopyMCPPromptButton", "复制完整规范"))
					.HAlign(HAlign_Center)
					.ToolTipText(LOCTEXT("CopyMCPPromptButtonTip", "仅用于调试或兼容不支持 MCP instructions 的客户端；普通使用时不需要复制。"))
					.OnClicked(this, &SEasyHouseBuilderPanel::HandleCopyMCPPromptClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MCPPromptHiddenStatus", "已启用隐藏系统规范：AI 连接 bpt-unreal 时会自动收到建筑生成流程、关系图规则、圆弧墙方向、地基/层板规则和预览提交流程。该内容不会显示在用户对话中。"))
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(0.48f)
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SAssignNew(AIConversationScrollBox, SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(AIMessageListBox, SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							BuildAIMessageBubble(LOCTEXT("AIConversationWelcome", "这里可以直接和 UE 内置 AI 对话。推荐使用“本地 Codex”模式：不需要在插件里填写 API Key，会调用本机已登录的 Codex；原来的 HTTP API 模式也仍然可用。完整建筑规范会作为隐藏系统消息发送，不会显示在聊天区。"), false)
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SAssignNew(AIInputTextBox, SEditableTextBox)
					.HintText(LOCTEXT("AIInputHint", "在这里直接和 AI 对话，例如：帮我设计一个带圆弧门厅的一层建筑方案"))
					.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type CommitType)
					{
						if (CommitType == ETextCommit::OnEnter)
						{
							HandleSendAIMessageClicked();
						}
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("AttachAIImagesButton", "\u6dfb\u52a0\u56fe\u7247"))
					.HAlign(HAlign_Center)
					.ToolTipText(LOCTEXT("AttachAIImagesButtonTip", "\u6dfb\u52a0 PNG\u3001JPG\u3001WebP \u6216 GIF \u56fe\u7247\u9644\u4ef6\uff0c\u53d1\u9001\u65f6\u4f1a\u4e00\u8d77\u4ea4\u7ed9 AI\u3002"))
					.OnClicked(this, &SEasyHouseBuilderPanel::HandleAttachAIImagesClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("SendAIMessageButton", "发送"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SEasyHouseBuilderPanel::HandleSendAIMessageClicked)
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SAssignNew(AIImageAttachmentListBox, SVerticalBox)
				.Visibility_Lambda([this]()
				{
					return PendingAIImagePaths.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
				})
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 12.0f, 0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AIJsonTestTitle", "JSON 生成测试"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("AIJsonTestTitleTip", "把 AI 或手写的建筑 JSON 粘贴到这里，点击生成后会在当前建筑对象下创建柱、墙、地基和层板。单位为厘米。"))
			]

			+ SVerticalBox::Slot()
			.FillHeight(0.44f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SAssignNew(AIJsonTestTextBox, SMultiLineEditableTextBox)
					.Text(FText::FromString(EasyHouseBuilderPanel::GetDefaultAITestBuildingJson()))
					.AutoWrapText(false)
					.AllowContextMenu(true)
					.ToolTipText(LOCTEXT("AIJsonTestInputTip", "支持 EHB_AI_Workflow_v2。推荐使用 floors、pillars、rooms、foundation、roomCeilingSlabs；普通墙会从房间柱子环自动推导，房间顶板会按每个 room 自动生成。"))
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("GenerateBuildingFromJsonButton", "生成测试建筑"))
					.HAlign(HAlign_Center)
					.ToolTipText(LOCTEXT("GenerateBuildingFromJsonButtonTip", "解析上方 JSON，并在当前建筑对象中生成测试用白模建筑。请先在“选择建筑对象”页选择或创建一个建筑对象。"))
					.OnClicked(this, &SEasyHouseBuilderPanel::HandleGenerateBuildingFromJsonClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("CopyCurrentBuildingDataButton", "复制当前建筑数据"))
					.HAlign(HAlign_Center)
					.ToolTipText(LOCTEXT("CopyCurrentBuildingDataButtonTip", "将当前建筑对象下的柱子、墙体、地基、楼板、地板、门窗、屋顶、楼梯、扶手和闭合房间数据复制到剪贴板，可直接粘贴给 AI 作为续写上下文。"))
					.OnClicked(this, &SEasyHouseBuilderPanel::HandleCopyCurrentBuildingDataClicked)
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(10.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(AIJsonGenerationStatusText, STextBlock)
					.Text(FText::GetEmpty())
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildAIMessageBubble(const FText& Message, bool bIsUserMessage, TSharedPtr<SMultiLineEditableText>* OutMessageTextWidget) const
{
	const FLinearColor BubbleColor = bIsUserMessage
		? FLinearColor(0.05f, 0.23f, 0.46f, 1.0f)
		: FLinearColor(0.12f, 0.13f, 0.15f, 1.0f);
	const FLinearColor LabelColor = bIsUserMessage
		? FLinearColor(0.68f, 0.82f, 1.0f, 1.0f)
		: FLinearColor(0.62f, 0.88f, 0.72f, 1.0f);
	const FText SpeakerLabel = bIsUserMessage
		? LOCTEXT("AIChatUserLabel", "\u4f60")
		: LOCTEXT("AIChatAssistantLabel", "AI");

	TSharedPtr<SMultiLineEditableText> MessageTextWidget;
	TSharedRef<SWidget> Bubble =
		SNew(SBox)
		.MaxDesiredWidth(560.0f)
		[
			SNew(SBorder)
			.Padding(FMargin(12.0f, 8.0f))
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(BubbleColor)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(SpeakerLabel)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(LabelColor)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SAssignNew(MessageTextWidget, SMultiLineEditableText)
					.Text(Message)
					.IsReadOnly(true)
					.AutoWrapText(true)
					.AllowContextMenu(true)
				]
			]
		];

	if (OutMessageTextWidget)
	{
		*OutMessageTextWidget = MessageTextWidget;
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			bIsUserMessage ? SNullWidget::NullWidget : Bubble
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(bIsUserMessage ? FMargin(24.0f, 0.0f, 0.0f, 0.0f) : FMargin(0.0f, 0.0f, 24.0f, 0.0f))
		[
			bIsUserMessage ? Bubble : SNullWidget::NullWidget
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildFoundationAndFloorPanel()
{
	return SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FoundationAndFloorPanelTitle", "地基/层板"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FStyleColors::Foreground)
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
				SNew(SBorder)
				.Padding(12.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("FloorSlabCreateSettingsTitle", "创建设置"))
						.ColorAndOpacity(FStyleColors::Foreground)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 10.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FloorSlabSizeLabel", "默认边长"))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SSpinBox<float>)
							.MinValue(10.0f)
							.MaxValue(100000.0f)
							.MinSliderValue(100.0f)
							.MaxSliderValue(2000.0f)
							.Value(FloorSlabCreationSize)
							.OnValueChanged_Lambda([this](float NewValue)
							{
								FloorSlabCreationSize = FMath::Max(10.0f, NewValue);
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
							.Text(LOCTEXT("FloorSlabThicknessLabel", "厚度"))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SSpinBox<float>)
							.MinValue(1.0f)
							.MaxValue(100000.0f)
							.MinSliderValue(5.0f)
							.MaxSliderValue(200.0f)
							.Value(FloorSlabCreationThickness)
							.OnValueChanged_Lambda([this](float NewValue)
							{
								FloorSlabCreationThickness = FMath::Max(1.0f, NewValue);
							})
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 12.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("CreateFoundationSlabButton", "创建地基"))
							.HAlign(HAlign_Center)
							.OnClicked(this, &SEasyHouseBuilderPanel::HandleCreateFoundationSlabClicked)
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("CreateFloorSlabButton", "创建层板"))
							.HAlign(HAlign_Center)
							.OnClicked(this, &SEasyHouseBuilderPanel::HandleCreateFloorSlabClicked)
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FloorSlabPlacementHint", "点击创建按钮后，在视口中移动鼠标预览位置，松开左键生成。选中地基/层板后，切割工具会显示在视口下方。"))
				.AutoWrapText(true)
				.ColorAndOpacity(FStyleColors::Foreground)
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildFloorPanel()
{
	return SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FloorPanelTitle", "\u5730\u677f"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FStyleColors::Foreground)
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
				SNew(SBorder)
				.Padding(12.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("FloorCreateSettingsTitle", "\u521b\u5efa\u8bbe\u7f6e"))
						.ColorAndOpacity(FStyleColors::Foreground)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 10.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FloorFinishSizeLabel", "\u9ed8\u8ba4\u8fb9\u957f"))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SSpinBox<float>)
							.MinValue(10.0f)
							.MaxValue(100000.0f)
							.MinSliderValue(100.0f)
							.MaxSliderValue(2000.0f)
							.Value(FloorFinishCreationSize)
							.OnValueChanged_Lambda([this](float NewValue)
							{
								FloorFinishCreationSize = FMath::Max(10.0f, NewValue);
							})
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 12.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("CreateFloorFinishButton", "\u521b\u5efa\u5730\u677f"))
						.HAlign(HAlign_Center)
						.OnClicked(this, &SEasyHouseBuilderPanel::HandleCreateFloorFinishClicked)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FloorFinishPlacementHint", "\u70b9\u51fb\u521b\u5efa\u540e\uff0c\u5730\u677f\u9884\u89c8\u4f1a\u8ddf\u968f\u9f20\u6807\u5438\u9644\u5230\u7ed3\u6784\u9876\u9762\uff1b\u5de6\u952e\u751f\u6210\u3002\u9009\u4e2d\u5730\u677f\u540e\uff0c\u89c6\u53e3\u4e0b\u65b9\u4f1a\u51fa\u73b0\u201c\u586b\u5145\u623f\u95f4\u201d\u6309\u94ae\u3002"))
				.AutoWrapText(true)
				.ColorAndOpacity(FStyleColors::Foreground)
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildRailingsPanel()
{
	RailingMeshThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	LoadRailingMeshTableFromConfig();

	return SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RailingsPanelTitle", "\u6276\u624b"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					.ColorAndOpacity(FStyleColors::Foreground)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
				[
					SNew(SButton)
					.Text(LOCTEXT("PrepareWallRailing", "准备墙中扶手"))
					.ToolTipText(LOCTEXT("PrepareWallRailingTip", "首次记录当前建筑的墙柱连接，可撤销。已过期或冲突的记录不会自动覆盖。"))
					.IsEnabled_Lambda([this]() { return ActiveBuilding.IsValid() && EasyHouseBuilderPanel::GetEditorSelectedBuilding() == ActiveBuilding.Get() && GEditor && !GEditor->PlayWorld && !GEditor->IsTransactionActive(); })
					.OnClicked_Lambda([this]()
					{
						if (auto* Mode = GetActiveEditorMode()) Mode->CancelRailingCreation();
						FEHBTopologyMigrationResult Result;
						const FString Json = UEHBBuildingToolset::PrepareTopologyMigration(ActiveBuilding.Get(), true);
						const bool bParsed = FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Result);
						const FString Status = bParsed ? Result.Status.ToString() : TEXT("InvalidResponse");
						UE_LOG(LogTemp, Display, TEXT("EHB prepare wall railing: %s"), *Status);
						FNotificationInfo Notice(EHBRailingFeedback::Describe(Status));
						Notice.ExpireDuration = 6;
						FSlateNotificationManager::Get().AddNotification(Notice);
						return FReply::Handled();
					})
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).AutoWrapText(true)
					.Text(LOCTEXT("WallRailingHelp", "先选择当前建筑并准备连接记录，再启用扶手创建工具，从墙体中部侧面拖拽。准备完成不代表所有构件类型均受支持。"))
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
					SNew(SBorder)
					.Padding(12.0f)
					.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FStyleColors::Panel)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SimpleRailingSettingsTitle", "\u7b80\u5355\u6276\u624b\u8bbe\u7f6e"))
							.ColorAndOpacity(FStyleColors::Foreground)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 10.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RailingCreationHeightLabel", "\u6276\u624b\u9ad8\u5ea6"))
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(12.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(1.0f)
								.MaxValue(100000.0f)
								.MinSliderValue(50.0f)
								.MaxSliderValue(200.0f)
								.Value(RailingCreationHeight)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									RailingCreationHeight = FMath::Max(1.0f, NewValue);
									if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
									{
										EditorMode->SetRailingCreationDefaults(RailingCreationHeight, RailingCreationPostSpacing, RailingCreationThickness);
									}
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
								.Text(LOCTEXT("RailingCreationPostSpacingLabel", "\u67f1\u95f4\u8ddd"))
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(12.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(1.0f)
								.MaxValue(100000.0f)
								.MinSliderValue(40.0f)
								.MaxSliderValue(300.0f)
								.Value(RailingCreationPostSpacing)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									RailingCreationPostSpacing = FMath::Max(1.0f, NewValue);
									if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
									{
										EditorMode->SetRailingCreationDefaults(RailingCreationHeight, RailingCreationPostSpacing, RailingCreationThickness);
									}
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
								.Text(LOCTEXT("RailingCreationThicknessLabel", "\u6746\u4ef6\u539a\u5ea6"))
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(12.0f, 0.0f, 0.0f, 0.0f)
							[
								SNew(SSpinBox<float>)
								.MinValue(0.1f)
								.MaxValue(100000.0f)
								.MinSliderValue(2.0f)
								.MaxSliderValue(40.0f)
								.Value(RailingCreationThickness)
								.OnValueChanged_Lambda([this](float NewValue)
								{
									RailingCreationThickness = FMath::Max(0.1f, NewValue);
									if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
									{
										EditorMode->SetRailingCreationDefaults(RailingCreationHeight, RailingCreationPostSpacing, RailingCreationThickness);
									}
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 12.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("CreateRailingButton", "\u521b\u5efa\u6276\u624b"))
							.HAlign(HAlign_Center)
							.OnClicked(this, &SEasyHouseBuilderPanel::HandleCreateRailingClicked)
						]

						+ SVerticalBox::Slot().AutoHeight().Padding(0,8)[BuildCreationAssistControls()]
					+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SAssignNew(RailingCreationStatusText, STextBlock)
							.Text(FText::GetEmpty())
							.ColorAndOpacity(FStyleColors::AccentGreen)
							.AutoWrapText(true)
						]
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					BuildRailingMeshTablePicker()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 6.0f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RailingMeshTemplateListTitle", "\u6276\u624b\u91c7\u6837\u9879"))
						.ColorAndOpacity(FStyleColors::Foreground)
						.ToolTipText(LOCTEXT("RailingMeshTemplateListTitleTip", "\u8fd9\u91cc\u663e\u793a\u5f53\u524d\u6276\u624b\u91c7\u6837\u8868\u4e2d\u7684\u6240\u6709\u884c\u3002\u62d6\u62fd\u67d0\u4e00\u9879\u5230\u5df2\u6709\u6276\u624b\u4e0a\u65f6\uff0c\u547d\u4e2d\u7684\u6276\u624b\u4f1a\u663e\u793a\u7eff\u8272\u9884\u89c8\u6846\u3002"))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SCheckBox)
						.IsChecked_Lambda([this]()
						{
							return bApplyRailingMeshToSinglePost ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						})
						.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
						{
							bApplyRailingMeshToSinglePost = NewState == ECheckBoxState::Checked;
						})
						.ToolTipText(LOCTEXT("RailingMeshSinglePostApplyTip", "开启后，拖拽扶手采样项时只替换鼠标命中的单根扶手柱子；关闭后应用完整扶手采样，包括柱子和横杆。"))
						[
							SNew(STextBlock)
							.Text(LOCTEXT("RailingMeshSinglePostApplyLabel", "只覆盖单根柱子"))
						]
					]
				]

				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					BuildRailingMeshCardsArea()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SAssignNew(RailingMeshStatusText, STextBlock)
					.Text(FText::GetEmpty())
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildRailingMeshTablePicker()
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
				.Text(LOCTEXT("RailingMeshTableLabel", "\u6276\u624b\u91c7\u6837\u8868"))
				.ColorAndOpacity(FStyleColors::Foreground)
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
					.ObjectPath(this, &SEasyHouseBuilderPanel::GetRailingMeshTablePath)
					.OnObjectChanged(this, &SEasyHouseBuilderPanel::HandleRailingMeshTableChanged)
					.OnShouldFilterAsset(this, &SEasyHouseBuilderPanel::ShouldFilterRailingMeshTable)
					.AllowClear(true)
					.DisplayThumbnail(false)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("RefreshRailingMeshRowsButton", "\u5237\u65b0"))
					.ToolTipText(LOCTEXT("RefreshRailingMeshRowsButtonTip", "\u91cd\u65b0\u8bfb\u53d6\u5f53\u524d\u6276\u624b\u91c7\u6837\u8868\u4e2d\u7684\u6240\u6709\u884c\u3002"))
					.OnClicked(this, &SEasyHouseBuilderPanel::HandleRefreshRailingMeshRowsClicked)
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildRailingMeshCardsArea()
{
	return SNew(SBorder)
		.Padding(8.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(RailingMeshCardsBox, SWrapBox)
				.UseAllottedSize(true)
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildRailingMeshCard(FName RowName)
{
	const FEHBRailingMeshData* Row = RailingMeshTable.IsValid()
		? RailingMeshTable->FindRow<FEHBRailingMeshData>(RowName, TEXT("SEasyHouseBuilderPanel::BuildRailingMeshCard"), false)
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
				.Image(FAppStyle::GetBrush("ClassIcon.StaticMesh"))
				.ColorAndOpacity(FStyleColors::Foreground)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RailingMeshNoPreview", "\u65e0\u9884\u89c8"))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FStyleColors::Foreground)
			]
		];

	if (UStaticMesh* PreviewMesh = GetPreviewMeshFromRailingMeshRow(Row))
	{
		TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(FAssetData(PreviewMesh), 112, 112, RailingMeshThumbnailPool);
		RailingMeshThumbnails.Add(Thumbnail);
		PreviewWidget = Thumbnail->MakeThumbnailWidget();
	}

	const FText NameText = FText::FromName(RowName);
	const FText SizeText = Row
		? FText::Format(
			LOCTEXT("RailingMeshCardSize", "\u67f1\u9ad8 {0} / \u67f1\u5bbd {1} / \u95f4\u8ddd {2} cm"),
			FText::AsNumber(Row->RecommendedPostHeight),
			FText::AsNumber(Row->RecommendedPostWidth),
			FText::AsNumber(Row->RecommendedPostSpacing))
		: LOCTEXT("RailingMeshCardInvalidSize", "\u65e0\u6548\u6276\u624b\u6570\u636e");
	const FText RailText = Row
		? FText::Format(
			LOCTEXT("RailingMeshCardRailSize", "\u6a2a\u6746\u539a {0} / \u6700\u5927\u5206\u6bb5 {1} cm"),
			FText::AsNumber(Row->RecommendedRailThickness),
			FText::AsNumber(Row->RecommendedMaxRailSegmentLength))
		: FText::GetEmpty();
	const FText PostMeshText = Row && !Row->PostMesh.SourceMeshName.IsNone()
		? FText::FromName(Row->PostMesh.SourceMeshName)
		: LOCTEXT("RailingMeshCardDefaultPostSource", "\u9ed8\u8ba4\u7acb\u67f1");
	const FText RailMeshText = Row && !Row->RailMesh.SourceMeshName.IsNone()
		? FText::FromName(Row->RailMesh.SourceMeshName)
		: LOCTEXT("RailingMeshCardDefaultRailSource", "\u9ed8\u8ba4\u6a2a\u6746");
	const FText MeshText = FText::Format(
		LOCTEXT("RailingMeshCardSourceSummary", "\u7acb\u67f1\uff1a{0} / \u6a2a\u6746\uff1a{1}"),
		PostMeshText,
		RailMeshText);

	return SNew(SEHBRailingMeshDragHandle)
		.OwnerWidget(SharedThis(this))
		.RowName(RowName)
		[
			SNew(SBox)
			.WidthOverride(250.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.10f, 0.14f, 0.11f, 1.0f))
				.ToolTipText(Row
					? FText::Format(LOCTEXT("RailingMeshCardTip", "{0}\n{1}\n\u62d6\u5230\u5df2\u6709\u6276\u624b\u4e0a\u8fdb\u884c\u9884\u89c8\u3002"), NameText, MeshText)
					: LOCTEXT("RailingMeshInvalidRowTip", "\u8be5\u6570\u636e\u8868\u884c\u65e0\u6548\u6216\u65e0\u6cd5\u8bfb\u53d6\u3002"))
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
						.Text(RailText)
						.ColorAndOpacity(FStyleColors::Foreground)
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(MeshText)
						.ColorAndOpacity(FStyleColors::Foreground)
						.AutoWrapText(true)
					]
				]
			]
		];
}

EVisibility SEasyHouseBuilderPanel::GetGableRoofElementOptionsVisibility() const
{
	return ActiveRoofCreationType == EEasyHouseRoofCreationType::Gable
		|| ActiveRoofCreationType == EEasyHouseRoofCreationType::Hip
		|| ActiveRoofCreationType == EEasyHouseRoofCreationType::HalfHip
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SEasyHouseBuilderPanel::GetUnsupportedRoofElementOptionsVisibility() const
{
	return ActiveRoofCreationType == EEasyHouseRoofCreationType::Gable
		|| ActiveRoofCreationType == EEasyHouseRoofCreationType::Hip
		|| ActiveRoofCreationType == EEasyHouseRoofCreationType::HalfHip
		? EVisibility::Collapsed
		: EVisibility::Visible;
}

void SEasyHouseBuilderPanel::HandleRoofCreationTypeChanged(ECheckBoxState NewState, EEasyHouseRoofCreationType RoofType)
{
	if (NewState == ECheckBoxState::Checked)
	{
		ActiveRoofCreationType = RoofType;
	}
}

ECheckBoxState SEasyHouseBuilderPanel::IsRoofCreationTypeChecked(EEasyHouseRoofCreationType RoofType) const
{
	return ActiveRoofCreationType == RoofType ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool SEasyHouseBuilderPanel::IsSelectedRoofCreationTypeImplemented() const
{
	return ActiveRoofCreationType == EEasyHouseRoofCreationType::Gable
		|| ActiveRoofCreationType == EEasyHouseRoofCreationType::Hip
		|| ActiveRoofCreationType == EEasyHouseRoofCreationType::HalfHip;
}

FText SEasyHouseBuilderPanel::GetCreateRoofButtonText() const
{
	switch (ActiveRoofCreationType)
	{
	case EEasyHouseRoofCreationType::Gable:
		return LOCTEXT("CreateConfiguredGableRoofButton", "\u521b\u5efa\u5c71\u5f62\u5c4b\u9876");
	case EEasyHouseRoofCreationType::Hip:
		return LOCTEXT("CreateConfiguredHipRoofButton", "\u521b\u5efa\u56db\u5761\u5c4b\u9876");
	case EEasyHouseRoofCreationType::HalfHip:
		return LOCTEXT("CreateConfiguredHalfHipRoofButton", "\u521b\u5efa\u534a\u56db\u5761\u5c4b\u9876");
	case EEasyHouseRoofCreationType::TwoSideSampled:
		return LOCTEXT("CreateTwoSideSampledRoofButton", "\u521b\u5efa\u53cc\u4fa7\u8fb9\u91c7\u6837\u5c4b\u9876");
	case EEasyHouseRoofCreationType::FourSideSampled:
		return LOCTEXT("CreateFourSideSampledRoofButton", "\u521b\u5efa\u56db\u8fb9\u91c7\u6837\u5c4b\u9876");
	case EEasyHouseRoofCreationType::FixedSampled:
		return LOCTEXT("CreateFixedSampledRoofButton", "\u521b\u5efa\u56fa\u5b9a\u91c7\u6837\u5c4b\u9876");
	default:
		return LOCTEXT("CreateRoofButton", "\u521b\u5efa\u5c4b\u9876");
	}
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildRoofCreationTypeButton(EEasyHouseRoofCreationType RoofType, const FText& Label)
{
	return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "DetailsView.SectionButton")
		.IsChecked(this, &SEasyHouseBuilderPanel::IsRoofCreationTypeChecked, RoofType)
		.OnCheckStateChanged(this, &SEasyHouseBuilderPanel::HandleRoofCreationTypeChanged, RoofType)
		.ToolTipText(Label)
		[
			SNew(SBox)
			.MinDesiredWidth(92.0f)
			.Padding(10.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(Label)
				.Justification(ETextJustify::Center)
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildRoofCreationTypeSelector()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("RoofCreationTypeTitle", "\u5c4b\u9876\u7c7b\u578b"))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FStyleColors::Foreground)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SWrapBox)
			.UseAllottedSize(true)
			.InnerSlotPadding(FVector2D(6.0f, 6.0f))

			+ SWrapBox::Slot()
			[
				BuildRoofCreationTypeButton(EEasyHouseRoofCreationType::Gable, LOCTEXT("RoofTypeGable", "\u5c71\u5f62"))
			]

			+ SWrapBox::Slot()
			[
				BuildRoofCreationTypeButton(EEasyHouseRoofCreationType::Hip, LOCTEXT("RoofTypeHip", "\u56db\u5761"))
			]

			+ SWrapBox::Slot()
			[
				BuildRoofCreationTypeButton(EEasyHouseRoofCreationType::HalfHip, LOCTEXT("RoofTypeHalfHip", "\u534a\u56db\u5761"))
			]

			+ SWrapBox::Slot()
			[
				BuildRoofCreationTypeButton(EEasyHouseRoofCreationType::TwoSideSampled, LOCTEXT("RoofTypeTwoSideSampled", "\u53cc\u4fa7\u8fb9\u91c7\u6837"))
			]

			+ SWrapBox::Slot()
			[
				BuildRoofCreationTypeButton(EEasyHouseRoofCreationType::FourSideSampled, LOCTEXT("RoofTypeFourSideSampled", "\u56db\u8fb9\u91c7\u6837"))
			]

			+ SWrapBox::Slot()
			[
				BuildRoofCreationTypeButton(EEasyHouseRoofCreationType::FixedSampled, LOCTEXT("RoofTypeFixedSampled", "\u56fa\u5b9a\u91c7\u6837"))
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildGableRoofElementOptions()
{
	auto BuildRoofNumericBox = [this](
		float SEasyHouseBuilderPanel::* ValueMember,
		float MinValue,
		float MaxSliderValue)
	{
		return SNew(SSpinBox<float>)
			.MinValue(MinValue)
			.MaxValue(100000.0f)
			.MinSliderValue(MinValue)
			.MaxSliderValue(MaxSliderValue)
			.Value_Lambda([this, ValueMember]()
			{
				return this->*ValueMember;
			})
			.OnValueChanged_Lambda([this, ValueMember, MinValue](float NewValue)
			{
				this->*ValueMember = FMath::Max(MinValue, NewValue);
			});
	};

	auto BuildRoofRow = [&BuildRoofNumericBox](
		const FText& Label,
		float SEasyHouseBuilderPanel::* ValueMember,
		float MinValue,
		float MaxSliderValue)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
					.WidthOverride(92.0f)
					[
						SNew(STextBlock)
							.Text(Label)
					]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				BuildRoofNumericBox(ValueMember, MinValue, MaxSliderValue)
			];
	};

	auto BuildRoofCheckRow = [this](
		const FText& Label,
		bool SEasyHouseBuilderPanel::* ValueMember)
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([this, ValueMember]()
			{
				return this->*ValueMember ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, ValueMember](ECheckBoxState NewState)
			{
				this->*ValueMember = NewState == ECheckBoxState::Checked;
			})
			[
				SNew(STextBlock)
					.Text(Label)
			];
	};

	return SNew(SBorder)
		.Visibility(this, &SEasyHouseBuilderPanel::GetGableRoofElementOptionsVisibility)
		.Padding(12.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
					.Text(ActiveRoofCreationType == EEasyHouseRoofCreationType::HalfHip
						? LOCTEXT("HalfHipRoofBasicOptionsTitle", "\u534a\u56db\u5761\u5c4b\u9876\u53c2\u6570")
						: (ActiveRoofCreationType == EEasyHouseRoofCreationType::Hip
							? LOCTEXT("HipRoofBasicOptionsTitle", "\u56db\u5761\u5c4b\u9876\u53c2\u6570")
							: LOCTEXT("GableRoofBasicOptionsTitle", "\u5c71\u5f62\u5c4b\u9876\u53c2\u6570")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				BuildRoofRow(LOCTEXT("GableRoofLength", "长度"), &SEasyHouseBuilderPanel::RoofCreationLength, 1.0f, 1200.0f)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				BuildRoofRow(LOCTEXT("GableRoofWidth", "宽度"), &SEasyHouseBuilderPanel::RoofCreationWidth, 1.0f, 1000.0f)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				BuildRoofRow(LOCTEXT("GableRoofPitch", "坡度"), &SEasyHouseBuilderPanel::RoofCreationPitchDegrees, 1.0f, 60.0f)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				BuildRoofRow(LOCTEXT("GableRoofThickness", "厚度"), &SEasyHouseBuilderPanel::RoofCreationThickness, 0.1f, 80.0f)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				BuildRoofRow(LOCTEXT("GableRoofEaveOffset", "出檐"), &SEasyHouseBuilderPanel::RoofCreationEaveOffset, 0.0f, 300.0f)
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
							.Text(LOCTEXT("GableRoofAdvancedOptionsTitle", "\u9ad8\u7ea7"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					]
					.BodyContent()
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							BuildRoofCheckRow(LOCTEXT("GableRoofGenerateRidge", "\u751f\u6210\u5c4b\u810a"), &SEasyHouseBuilderPanel::bRoofCreationGenerateRidge)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							BuildRoofCheckRow(LOCTEXT("GableRoofGenerateEaves", "\u751f\u6210\u6a90\u53e3"), &SEasyHouseBuilderPanel::bRoofCreationGenerateEaves)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							BuildRoofCheckRow(
								ActiveRoofCreationType == EEasyHouseRoofCreationType::HalfHip
									? LOCTEXT("HalfHipRoofGenerateHipRidges", "\u751f\u6210\u659c\u810a/\u5207\u9762\u659c\u8fb9")
									: (ActiveRoofCreationType == EEasyHouseRoofCreationType::Hip
										? LOCTEXT("HipRoofGenerateHipRidges", "\u751f\u6210\u56db\u5761\u659c\u810a")
										: LOCTEXT("GableRoofGenerateRakes", "\u751f\u6210\u5c71\u5899\u659c\u8fb9")),
								&SEasyHouseBuilderPanel::bRoofCreationGenerateGableRakes)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							SNew(SBox)
								.Visibility_Lambda([this]()
								{
									return ActiveRoofCreationType == EEasyHouseRoofCreationType::Gable
										? EVisibility::Visible
										: EVisibility::Collapsed;
								})
								[
									BuildRoofCheckRow(LOCTEXT("GableRoofGenerateEndWalls", "\u751f\u6210\u5c71\u5899\u4fa7\u5899"), &SEasyHouseBuilderPanel::bRoofCreationGenerateGableEndWalls)
								]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							SNew(SBox)
								.Visibility_Lambda([this]()
								{
									return ActiveRoofCreationType == EEasyHouseRoofCreationType::Gable
										? EVisibility::Visible
										: EVisibility::Collapsed;
								})
								.IsEnabled_Lambda([this]()
								{
									return bRoofCreationGenerateGableEndWalls;
								})
								[
									BuildRoofRow(LOCTEXT("GableRoofEndWallBoundaryInset", "\u4fa7\u5899\u8fb9\u754c\u8ddd\u79bb"), &SEasyHouseBuilderPanel::RoofCreationGableEndWallBoundaryInset, 0.0f, 300.0f)
								]
						]
					]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildUnsupportedRoofElementOptions()
{
	return SNew(SBorder)
		.Visibility(this, &SEasyHouseBuilderPanel::GetUnsupportedRoofElementOptionsVisibility)
		.Padding(12.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("UnsupportedRoofOptionsTitle", "\u91c7\u6837\u5c4b\u9876\u53c2\u6570"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(FStyleColors::Foreground)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("UnsupportedRoofOptionsBody", "\u8fd9\u4e2a\u5c4b\u9876\u7c7b\u578b\u7684\u521b\u5efa\u6d41\u7a0b\u8fd8\u6ca1\u6709\u63a5\u5165\u3002\u5f53\u524d\u5148\u4fdd\u7559\u7c7b\u578b\u9009\u62e9\u548c\u52a8\u6001\u53c2\u6570\u533a\uff0c\u540e\u7eed\u63a5\u91c7\u6837\u6570\u636e\u65f6\u53ef\u4ee5\u76f4\u63a5\u5728\u8fd9\u91cc\u66ff\u6362\u914d\u7f6e\u63a7\u4ef6\u3002"))
				.AutoWrapText(true)
				.ColorAndOpacity(FStyleColors::Foreground)
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildRoofPanel()
{
	return SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background)
		[
			SNew(SScrollBox)

			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
						.Text(LOCTEXT("RoofPanelV2Title", "\u5c4b\u9876"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						.ColorAndOpacity(FStyleColors::Foreground)
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
					BuildRoofCreationTypeSelector()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					SNew(SSeparator)
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					BuildGableRoofElementOptions()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					BuildUnsupportedRoofElementOptions()
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
						.HAlign(HAlign_Center)
						.IsEnabled(this, &SEasyHouseBuilderPanel::IsSelectedRoofCreationTypeImplemented)
						.Text(this, &SEasyHouseBuilderPanel::GetCreateRoofButtonText)
						.OnClicked(this, &SEasyHouseBuilderPanel::HandleCreateRoofClicked)
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildStairsPanel()
{
	auto BuildDimensionRow = [](const FText& Label, float InitialValue, float MinValue, float MaxSliderValue, TFunction<void(float)> OnValueChanged)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SSpinBox<float>)
				.MinValue(MinValue)
				.MaxValue(100000.0f)
				.MinSliderValue(MinValue)
				.MaxSliderValue(MaxSliderValue)
				.Value(InitialValue)
				.OnValueChanged_Lambda([OnValueChanged](float NewValue)
				{
					OnValueChanged(NewValue);
				})
			];
	};

	auto BuildOptionRow = [](const FText& Label, TFunction<ECheckBoxState()> GetState, TFunction<void(ECheckBoxState)> OnStateChanged)
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([GetState]()
			{
				return GetState();
			})
			.OnCheckStateChanged_Lambda([OnStateChanged](ECheckBoxState NewState)
			{
				OnStateChanged(NewState);
			})
			[
				SNew(STextBlock)
					.Text(Label)
			];
	};

	return SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StairPanelTitle", "\u697c\u68af"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FStyleColors::Foreground)
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
				SNew(SBorder)
				.Padding(12.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
							.Text(LOCTEXT("StairCreateSettingsTitle", "\u521b\u5efa\u8bbe\u7f6e"))
							.ColorAndOpacity(FStyleColors::Foreground)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 10.0f, 0.0f, 0.0f)
					[
						BuildDimensionRow(
							LOCTEXT("StairHeightLabel", "\u697c\u68af\u603b\u9ad8"),
							StairCreationHeight,
							1.0f,
							600.0f,
							[this](float NewValue) { StairCreationHeight = FMath::Max(1.0f, NewValue); })
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildDimensionRow(
							LOCTEXT("StairWidthLabel", "\u697c\u68af\u5bbd\u5ea6"),
							StairCreationWidth,
							1.0f,
							400.0f,
							[this](float NewValue) { StairCreationWidth = FMath::Max(1.0f, NewValue); })
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BuildDimensionRow(
							LOCTEXT("StairTreadDepthLabel", "\u8e0f\u6b65\u6df1\u5ea6"),
							StairCreationTreadDepth,
							1.0f,
							100.0f,
							[this](float NewValue) { StairCreationTreadDepth = FMath::Max(1.0f, NewValue); })
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 12.0f, 0.0f, 0.0f)
					[
						BuildOptionRow(
							LOCTEXT("StairGenerateTreadsLabel", "\u751f\u6210\u8e0f\u677f"),
							[this]() { return bStairCreationGenerateTreads ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; },
							[this](ECheckBoxState State) { bStairCreationGenerateTreads = State == ECheckBoxState::Checked; })
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						BuildOptionRow(
							LOCTEXT("StairFillRisersLabel", "\u751f\u6210\u8e22\u677f"),
							[this]() { return bStairCreationFillRisers ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; },
							[this](ECheckBoxState State) { bStairCreationFillRisers = State == ECheckBoxState::Checked; })
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						BuildOptionRow(
							LOCTEXT("StairFillBottomLabel", "\u586b\u5145\u697c\u68af\u5e95\u90e8"),
							[this]() { return bStairCreationFillBottomPart ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; },
							[this](ECheckBoxState State) { bStairCreationFillBottomPart = State == ECheckBoxState::Checked; })
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						BuildOptionRow(
							LOCTEXT("StairGenerateSidesLabel", "\u751f\u6210\u952f\u9f7f\u4fa7\u677f"),
							[this]() { return bStairCreationGenerateSides ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; },
							[this](ECheckBoxState State) { bStairCreationGenerateSides = State == ECheckBoxState::Checked; })
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						BuildOptionRow(
							LOCTEXT("StairGenerateSideGuardsLabel", "\u751f\u6210\u6276\u624b\u4fa7\u6321\u677f"),
							[this]() { return bStairCreationGenerateSideGuards ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; },
							[this](ECheckBoxState State) { bStairCreationGenerateSideGuards = State == ECheckBoxState::Checked; })
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						BuildOptionRow(
							LOCTEXT("StairGenerateRailingLabel", "\u751f\u6210\u6276\u624b"),
							[this]() { return bStairCreationGenerateRailing ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; },
							[this](ECheckBoxState State) { bStairCreationGenerateRailing = State == ECheckBoxState::Checked; })
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() { return bStairCreationGenerateRailing ? EVisibility::Visible : EVisibility::Collapsed; })

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							BuildOptionRow(
								LOCTEXT("StairGenerateLeftRailingLabel", "\u5de6\u4fa7\u67f1\u5b50"),
								[this]() { return bStairCreationGenerateLeftRailing ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; },
								[this](ECheckBoxState State) { bStairCreationGenerateLeftRailing = State == ECheckBoxState::Checked; })
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						[
							BuildOptionRow(
								LOCTEXT("StairGenerateRightRailingLabel", "\u53f3\u4fa7\u67f1\u5b50"),
								[this]() { return bStairCreationGenerateRightRailing ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; },
								[this](ECheckBoxState State) { bStairCreationGenerateRightRailing = State == ECheckBoxState::Checked; })
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						.IsEnabled_Lambda([this]() { return bStairCreationGenerateRailing; })

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("StairRailingStepsPerPostLabel", "\u6276\u624b\u67f1\u5b50\u95f4\u9694"))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SSpinBox<int32>)
							.MinValue(1)
							.MaxValue(64)
							.MinSliderValue(1)
							.MaxSliderValue(12)
							.Value(StairCreationRailingStepsPerPost)
							.ToolTipText(LOCTEXT("StairRailingStepsPerPostTip", "\u6bcf\u9694\u51e0\u4e2a\u53f0\u9636\u653e\u7f6e\u4e00\u6839\u6276\u624b\u67f1\u5b50\u3002"))
							.OnValueChanged_Lambda([this](int32 NewValue)
							{
								StairCreationRailingStepsPerPost = FMath::Max(1, NewValue);
							})
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						.IsEnabled_Lambda([this]() { return bStairCreationGenerateRailing; })

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("StairRailingEdgeInsetLabel", "\u8ddd\u8e0f\u6b65\u8fb9\u754c"))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SSpinBox<float>)
							.MinValue(0.0f)
							.MaxValue(500.0f)
							.MinSliderValue(0.0f)
							.MaxSliderValue(60.0f)
							.Value(StairCreationRailingEdgeInset)
							.ToolTipText(LOCTEXT("StairRailingEdgeInsetTip", "\u6276\u624b\u67f1\u5b50\u4e2d\u5fc3\u8ddd\u53f0\u9636\u8fb9\u754c\u5411\u5185\u7684\u8ddd\u79bb\u3002"))
							.OnValueChanged_Lambda([this](float NewValue)
							{
								StairCreationRailingEdgeInset = FMath::Max(0.0f, NewValue);
							})
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						.IsEnabled_Lambda([this]() { return bStairCreationGenerateRailing; })

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("StairRailingPostForwardOffsetLabel", "\u67f1\u5b50\u524d\u540e\u504f\u79fb"))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SSpinBox<float>)
							.MinValue(-10000.0f)
							.MaxValue(10000.0f)
							.MinSliderValue(-150.0f)
							.MaxSliderValue(150.0f)
							.Value(StairCreationRailingPostForwardOffset)
							.ToolTipText(LOCTEXT("StairRailingPostForwardOffsetTip", "\u6276\u624b\u67f1\u5b50\u6cbf\u697c\u68af\u524d\u540e\u65b9\u5411\u76f8\u5bf9\u8e0f\u677f\u7684\u504f\u79fb\u8ddd\u79bb\u3002"))
							.OnValueChanged_Lambda([this](float NewValue)
							{
								StairCreationRailingPostForwardOffset = FMath::Clamp(NewValue, -10000.0f, 10000.0f);
							})
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 12.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
							.Text(LOCTEXT("CreateStairButton", "\u521b\u5efa\u697c\u68af"))
							.HAlign(HAlign_Center)
							.OnClicked(this, &SEasyHouseBuilderPanel::HandleCreateStairClicked)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
					.Text(LOCTEXT("StairPlacementHint", "\u70b9\u51fb\u521b\u5efa\u697c\u68af\u540e\uff0c\u697c\u68af\u4f1a\u8ddf\u968f\u89c6\u53e3\u9f20\u6807\u3002\u6309\u4e0b\u9f20\u6807\u5de6\u952e\u521b\u5efa\uff0c\u6309 Escape \u6216\u5355\u51fb\u9f20\u6807\u53f3\u952e\u53d6\u6d88\u3002"))
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildWallsPanel()
{
	WallSurfaceThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	PillarMeshThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	LoadWallSurfaceTableFromConfig();
	LoadPillarMeshTableFromConfig();

	return SNew(SBorder)
		.Padding(14.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Background)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WallsPanelTitle", "\u5899\u4f53"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
				.ColorAndOpacity(FStyleColors::Foreground)
				.ToolTipText(LOCTEXT("WallsPanelTitleTip", "\u5899\u4f53\u5de5\u5177\u7528\u4e8e\u5728\u5f53\u524d\u5efa\u7b51\u5bf9\u8c61\u4e2d\u521b\u5efa\u548c\u7f16\u8f91\u5899\u4f53\u3001\u67f1\u5b50\u4ee5\u53ca\u540e\u7eed\u7684\u5899\u9762\u6784\u4ef6\u3002"))
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
				SNew(SBorder)
				.Padding(12.0f)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FStyleColors::Panel)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SimpleWallSettingsTitle", "\u7b80\u5355\u5899\u9762\u8bbe\u7f6e"))
						.ColorAndOpacity(FStyleColors::Foreground)
						.ToolTipText(LOCTEXT("SimpleWallSettingsTitleTip", "\u8fd9\u91cc\u914d\u7f6e\u62d6\u62fd\u521b\u5efa\u5899\u9762\u65f6\u4f7f\u7528\u7684\u57fa\u7840\u5c3a\u5bf8\u3002\u5f53\u524d\u9636\u6bb5\u4f1a\u521b\u5efa\u6570\u636e\u7ea7\u7684\u8d77\u70b9\u67f1\u3001\u7ec8\u70b9\u67f1\u548c\u5de6\u53f3\u4e24\u4fa7\u7b80\u5355\u5899\u9762\u3002"))
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0,8)
					[
						SNew(SCheckBox)
						.IsEnabled_Lambda([this](){return ActiveBuilding.IsValid()&&ActiveBuilding->WallNodeAuthority.Version==2;})
						.IsChecked_Lambda([this](){return bWallCreationPhysicalColumns||!ActiveBuilding.IsValid()||ActiveBuilding->WallNodeAuthority.Version!=2?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State){bWallCreationPhysicalColumns=State==ECheckBoxState::Checked;if(auto* Mode=GetActiveEditorMode())Mode->SetWallCreationPhysicalColumns(bWallCreationPhysicalColumns);})
						.ToolTipText(LOCTEXT("WallCreationPhysicalColumnsTip","先在建筑面板启用墙角联动。关闭后，拖动或矩形创建的新墙角不生成实体柱身；已有柱子保留。点击墙面可插入无柱控制点，空处单击不创建构件。切换选项会取消当前拖动。"))
						[SNew(STextBlock).Text(LOCTEXT("WallCreationPhysicalColumns","在新墙角生成柱身"))]
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0,8)
					[
						SNew(SCheckBox)
						.IsEnabled_Lambda([this](){return !ActiveBuilding.IsValid()||ActiveBuilding->WallNodeAuthority.Version!=2;})
						.IsChecked_Lambda([this](){const auto* Mode=GetActiveEditorMode();return (ActiveBuilding.IsValid()&&ActiveBuilding->WallNodeAuthority.Version==2)||(Mode&&Mode->IsRoomFloorWallMoveEnabled())?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
						.OnCheckStateChanged_Lambda([this](ECheckBoxState State){if(auto* Mode=GetActiveEditorMode())Mode->SetRoomFloorWallMoveEnabled(State==ECheckBoxState::Checked);})
						.ToolTipText(LOCTEXT("RoomWallMoveCheckTip","启用墙角联动的建筑始终联动房间地板和层板。旧建筑可单独试用此选项，取消勾选会取消当前拖动。目前支持未改形的完整房间铺面，并保持层板标高。"))
						[SNew(STextBlock).Text_Lambda([this](){return ActiveBuilding.IsValid()&&ActiveBuilding->WallNodeAuthority.Version==2?LOCTEXT("RoomWallMoveAlwaysEnabled","房间地板/层板随墙体联动（已启用）"):LOCTEXT("RoomWallMoveCheck","移动墙体时联动房间地板/层板（试用）");})]
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SButton).Text(LOCTEXT("PrepareRoomWallMove","准备墙体联动"))
						.Visibility_Lambda([this](){return ActiveBuilding.IsValid()&&ActiveBuilding->WallNodeAuthority.Version==2?EVisibility::Collapsed:EVisibility::Visible;})
						.ToolTipText(LOCTEXT("PrepareRoomWallMoveTip","先选中当前建筑，点击记录墙柱连接；随后选墙，用移动手柄拖动。准备可撤销，冲突或过期记录不会覆盖。"))
						.IsEnabled_Lambda([this](){return ActiveBuilding.IsValid()&&EasyHouseBuilderPanel::GetEditorSelectedBuilding()==ActiveBuilding.Get()&&GEditor&&!GEditor->PlayWorld&&!GEditor->IsTransactionActive();})
						.OnClicked_Lambda([this](){
							FEHBTopologyMigrationResult Result;
							const bool Parsed=FJsonObjectConverter::JsonObjectStringToUStruct(UEHBBuildingToolset::PrepareTopologyMigration(ActiveBuilding.Get(),true),&Result);
							FNotificationInfo Notice(Parsed&&Result.bSucceeded?LOCTEXT("RoomWallMovePrepared","墙柱连接已准备好。勾选房间联动后，选中一面墙并水平拖动移动手柄；Esc 取消。"):EHBRailingFeedback::Describe(Parsed?Result.Status.ToString():TEXT("InvalidResponse")));
							Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);return FReply::Handled();
						})
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 10.0f, 0.0f, 0.0f)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("WallCreationHeightLabel", "\u5899\u9ad8"))
							.ToolTipText(LOCTEXT("WallCreationHeightTip", "\u62d6\u62fd\u521b\u5efa\u5899\u9762\u65f6\u4f7f\u7528\u7684\u5899\u4f53\u9ad8\u5ea6\uff0c\u5355\u4f4d\u4e3a\u5398\u7c73\u3002"))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SSpinBox<float>)
							.MinValue(1.0f)
							.MaxValue(100000.0f)
							.MinSliderValue(50.0f)
							.MaxSliderValue(1000.0f)
							.Value(WallCreationHeight)
							.ToolTipText(LOCTEXT("WallCreationHeightInputTip", "\u8bbe\u7f6e\u4e0b\u4e00\u6b21\u62d6\u62fd\u521b\u5efa\u5899\u9762\u65f6\u5199\u5165\u5899\u4f53\u548c\u67f1\u5b50\u6570\u636e\u7684\u9ad8\u5ea6\u3002"))
							.OnValueChanged_Lambda([this](float NewValue)
							{
								WallCreationHeight = FMath::Max(1.0f, NewValue);
								if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
								{
									EditorMode->SetWallCreationDefaults(WallCreationHeight, WallCreationThickness);
		EditorMode->SetWallCreationPhysicalColumns(bWallCreationPhysicalColumns);
								}
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
							.Text(LOCTEXT("WallCreationThicknessLabel", "\u5899\u539a\u5ea6"))
							.ToolTipText(LOCTEXT("WallCreationThicknessTip", "\u62d6\u62fd\u521b\u5efa\u5899\u9762\u65f6\u4f7f\u7528\u7684\u5899\u4f53\u539a\u5ea6\uff0c\u5355\u4f4d\u4e3a\u5398\u7c73\u3002\u8d77\u70b9\u548c\u7ec8\u70b9\u67f1\u4f1a\u4f7f\u7528\u76f8\u540c\u7684\u6a2a\u5411\u5c3a\u5bf8\u3002"))
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(12.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SSpinBox<float>)
							.MinValue(1.0f)
							.MaxValue(100000.0f)
							.MinSliderValue(5.0f)
							.MaxSliderValue(200.0f)
							.Value(WallCreationThickness)
							.ToolTipText(LOCTEXT("WallCreationThicknessInputTip", "\u8bbe\u7f6e\u4e0b\u4e00\u6b21\u62d6\u62fd\u521b\u5efa\u5899\u9762\u65f6\u5199\u5165\u5899\u4f53\u548c\u67f1\u5b50\u6570\u636e\u7684\u539a\u5ea6\u3002"))
							.OnValueChanged_Lambda([this](float NewValue)
							{
								WallCreationThickness = FMath::Max(1.0f, NewValue);
								if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
								{
									EditorMode->SetWallCreationDefaults(WallCreationHeight, WallCreationThickness);
		EditorMode->SetWallCreationPhysicalColumns(bWallCreationPhysicalColumns);
								}
							})
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 12.0f, 0.0f, 0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("CreateWallButton", "\u521b\u5efa\u5899\u9762"))
						.ToolTipText(LOCTEXT("CreateWallButtonTip", "\u8fdb\u5165\u89c6\u53e3\u62d6\u62fd\u521b\u5efa\u72b6\u6001\u3002\u5728\u573a\u666f\u4e2d\u6309\u4f4f\u5de6\u952e\u62d6\u62fd\u786e\u5b9a\u8d77\u70b9\u548c\u7ec8\u70b9\uff0c\u91ca\u653e\u540e\u5728\u5f53\u524d\u5efa\u7b51\u5bf9\u8c61\u4e2d\u521b\u5efa\u8d77\u70b9\u67f1\u3001\u7ec8\u70b9\u67f1\u4ee5\u53ca\u5de6\u53f3\u4e24\u4fa7\u7b80\u5355\u5899\u9762\u3002"))
						.HAlign(HAlign_Center)
						.OnClicked(this, &SEasyHouseBuilderPanel::HandleCreateWallClicked)
					]

					+ SVerticalBox::Slot().AutoHeight().Padding(0,8)[BuildCreationAssistControls(true)]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SAssignNew(WallCreationStatusText, STextBlock)
						.Text(FText::GetEmpty())
						.ColorAndOpacity(FStyleColors::AccentGreen)
						.AutoWrapText(true)
						.ToolTipText(LOCTEXT("WallCreationStatusTip", "\u663e\u793a\u5899\u9762\u521b\u5efa\u5de5\u5177\u7684\u5f53\u524d\u72b6\u6001\u3002"))
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 14.0f, 0.0f, 8.0f)
			[
				SNew(SSeparator)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					BuildWallSubPanelButton(EEasyHouseWallSubPanel::WallSurface, LOCTEXT("WallSurfaceSubPanel", "\u5899\u9762"))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					BuildWallSubPanelButton(EEasyHouseWallSubPanel::Pillar, LOCTEXT("PillarSubPanel", "\u67f1\u4f53"))
				]
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(WallSubPanelSwitcher, SWidgetSwitcher)
				.WidgetIndex(static_cast<int32>(ActiveWallSubPanel))

				+ SWidgetSwitcher::Slot()
				[
					BuildWallSurfacePage()
				]

				+ SWidgetSwitcher::Slot()
				[
					BuildWallPillarPage()
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildWallSubPanelButton(EEasyHouseWallSubPanel Panel, const FText& Label)
{
	return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "DetailsView.SectionButton")
		.IsChecked(this, &SEasyHouseBuilderPanel::IsWallSubPanelChecked, Panel)
		.OnCheckStateChanged(this, &SEasyHouseBuilderPanel::HandleWallSubPanelSelectionChanged, Panel)
		.ToolTipText(Label)
		[
			SNew(SBox)
			.MinDesiredWidth(82.0f)
			.Padding(10.0f, 4.0f)
			[
				SNew(STextBlock)
				.Text(Label)
				.Justification(ETextJustify::Center)
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildWallSurfacePage()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildWallSurfaceTablePicker()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			BuildWallSurfaceCoverOptions()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("WallSurfaceTemplateListTitle", "\u5899\u9762\u91c7\u6837\u9879"))
			.ColorAndOpacity(FStyleColors::Foreground)
			.ToolTipText(LOCTEXT("WallSurfaceTemplateListTitleTip", "\u8fd9\u91cc\u663e\u793a\u5f53\u524d\u5899\u9762\u91c7\u6837\u8868\u4e2d\u7684\u6240\u6709\u884c\u3002\u62d6\u62fd\u67d0\u4e00\u9879\u5230\u5df2\u6709\u5899\u4f53\u4e0a\u65f6\uff0c\u547d\u4e2d\u7684\u5899\u4f53\u4f1a\u663e\u793a\u7eff\u8272\u9884\u89c8\u6807\u8bc6\u3002"))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			BuildWallSurfaceCardsArea()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SAssignNew(WallSurfaceStatusText, STextBlock)
			.Text(FText::GetEmpty())
			.AutoWrapText(true)
			.ColorAndOpacity(FStyleColors::Foreground)
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildWallPillarPage()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildPillarMeshTablePicker()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 10.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PillarMeshTemplateListTitle", "\u67f1\u4f53\u91c7\u6837\u9879"))
			.ColorAndOpacity(FStyleColors::Foreground)
			.ToolTipText(LOCTEXT("PillarMeshTemplateListTitleTip", "\u8fd9\u91cc\u663e\u793a\u5f53\u524d\u67f1\u4f53\u91c7\u6837\u8868\u4e2d\u7684\u6240\u6709\u884c\u3002\u62d6\u62fd\u67d0\u4e00\u9879\u5230\u5df2\u6709\u67f1\u4f53\u4e0a\u65f6\uff0c\u547d\u4e2d\u7684\u67f1\u4f53\u4f1a\u663e\u793a\u7eff\u8272\u9884\u89c8\u6846\u3002"))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			BuildPillarMeshCardsArea()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SAssignNew(PillarMeshStatusText, STextBlock)
			.Text(FText::GetEmpty())
			.AutoWrapText(true)
			.ColorAndOpacity(FStyleColors::Foreground)
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildWallSurfaceTablePicker()
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
				.Text(LOCTEXT("WallSurfaceTableLabel", "\u5899\u4f53\u91c7\u6837\u8868"))
				.ColorAndOpacity(FStyleColors::Foreground)
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
					.ObjectPath(this, &SEasyHouseBuilderPanel::GetWallSurfaceTablePath)
					.OnObjectChanged(this, &SEasyHouseBuilderPanel::HandleWallSurfaceTableChanged)
					.OnShouldFilterAsset(this, &SEasyHouseBuilderPanel::ShouldFilterWallSurfaceTable)
					.AllowClear(true)
					.DisplayThumbnail(false)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("RefreshWallSurfaceRowsButton", "\u5237\u65b0"))
					.ToolTipText(LOCTEXT("RefreshWallSurfaceRowsButtonTip", "\u91cd\u65b0\u8bfb\u53d6\u5f53\u524d\u5899\u9762\u91c7\u6837\u8868\u4e2d\u7684\u6240\u6709\u884c\u3002"))
					.OnClicked(this, &SEasyHouseBuilderPanel::HandleRefreshWallSurfaceRowsClicked)
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildWallSurfaceCoverOptions()
{
	return SNew(SBorder)
		.Padding(12.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WallSurfaceCoverModeTitle", "\u8986\u76d6\u6a21\u5f0f"))
				.ColorAndOpacity(FStyleColors::Foreground)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 14.0f, 0.0f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return bWallSurfaceCoverBothSides ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bWallSurfaceCoverBothSides = NewState == ECheckBoxState::Checked;
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("WallSurfaceCoverBothSides", "\u8986\u76d6\u53cc\u9762"))
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return bWallSurfaceFlipSampleSides ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bWallSurfaceFlipSampleSides = NewState == ECheckBoxState::Checked;
					})
					[
						SNew(STextBlock)
						.Text(LOCTEXT("WallSurfaceFlipSampleSides", "\u7ffb\u8f6c\u91c7\u6837\u6b63\u53cd\u9762"))
					]
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildWallSurfaceCardsArea()
{
	return SNew(SBorder)
		.Padding(8.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(WallSurfaceCardsBox, SWrapBox)
				.UseAllottedSize(true)
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildWallSurfaceCard(FName RowName)
{
	const FEHBWallMeshData* Row = WallSurfaceTable.IsValid()
		? WallSurfaceTable->FindRow<FEHBWallMeshData>(RowName, TEXT("SEasyHouseBuilderPanel::BuildWallSurfaceCard"), false)
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
				.Image(FAppStyle::GetBrush("ClassIcon.StaticMesh"))
				.ColorAndOpacity(FStyleColors::Foreground)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("WallSurfaceNoPreview", "\u65e0\u9884\u89c8"))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FStyleColors::Foreground)
			]
		];

	if (UStaticMesh* PreviewMesh = GetPreviewMeshFromWallSurfaceRow(Row))
	{
		TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(FAssetData(PreviewMesh), 112, 112, WallSurfaceThumbnailPool);
		WallSurfaceThumbnails.Add(Thumbnail);
		PreviewWidget = Thumbnail->MakeThumbnailWidget();
	}

	const FText NameText = FText::FromName(RowName);
	const FText SizeText = Row
		? FText::Format(
			LOCTEXT("WallSurfaceCardSize", "\u5bbd {0} / \u9ad8 {1} / \u539a {2} cm"),
			FText::AsNumber(Row->WallWidth),
			FText::AsNumber(Row->WallHeight),
			FText::AsNumber(Row->WallThickness))
		: LOCTEXT("WallSurfaceCardInvalidSize", "\u65e0\u6548\u5899\u9762\u6570\u636e");
	const FText MeshText = Row && !Row->SourceMeshName.IsNone()
		? FText::FromName(Row->SourceMeshName)
		: LOCTEXT("WallSurfaceCardNoSourceName", "\u672a\u8bb0\u5f55\u6e90\u7f51\u683c");

	return SNew(SEHBWallSurfaceDragHandle)
		.OwnerWidget(SharedThis(this))
		.RowName(RowName)
		[
			SNew(SBox)
			.WidthOverride(250.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.10f, 0.14f, 0.11f, 1.0f))
				.ToolTipText(Row
					? FText::Format(LOCTEXT("WallSurfaceCardTip", "{0}\n\u6e90\u7f51\u683c\u4f53\uff1a{1}\n\u62d6\u5230\u5df2\u6709\u5899\u4f53\u4e0a\u8fdb\u884c\u9884\u89c8\u3002"), NameText, MeshText)
					: LOCTEXT("WallSurfaceInvalidRowTip", "\u8be5\u6570\u636e\u8868\u884c\u65e0\u6548\u6216\u65e0\u6cd5\u8bfb\u53d6\u3002"))
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
						.Text(MeshText)
						.ColorAndOpacity(FStyleColors::Foreground)
						.AutoWrapText(true)
					]
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildPillarMeshTablePicker()
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
				.Text(LOCTEXT("PillarMeshTableLabel", "\u67f1\u4f53\u91c7\u6837\u8868"))
				.ColorAndOpacity(FStyleColors::Foreground)
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
					.ObjectPath(this, &SEasyHouseBuilderPanel::GetPillarMeshTablePath)
					.OnObjectChanged(this, &SEasyHouseBuilderPanel::HandlePillarMeshTableChanged)
					.OnShouldFilterAsset(this, &SEasyHouseBuilderPanel::ShouldFilterPillarMeshTable)
					.AllowClear(true)
					.DisplayThumbnail(false)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("RefreshPillarMeshRowsButton", "\u5237\u65b0"))
					.ToolTipText(LOCTEXT("RefreshPillarMeshRowsButtonTip", "\u91cd\u65b0\u8bfb\u53d6\u5f53\u524d\u67f1\u4f53\u91c7\u6837\u8868\u4e2d\u7684\u6240\u6709\u884c\u3002"))
					.OnClicked(this, &SEasyHouseBuilderPanel::HandleRefreshPillarMeshRowsClicked)
				]
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildPillarMeshCardsArea()
{
	return SNew(SBorder)
		.Padding(8.0f)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Panel)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(PillarMeshCardsBox, SWrapBox)
				.UseAllottedSize(true)
			]
		];
}

TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildPillarMeshCard(FName RowName)
{
	const FEHBPillarMeshData* Row = PillarMeshTable.IsValid()
		? PillarMeshTable->FindRow<FEHBPillarMeshData>(RowName, TEXT("SEasyHouseBuilderPanel::BuildPillarMeshCard"), false)
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
				.Image(FAppStyle::GetBrush("ClassIcon.StaticMesh"))
				.ColorAndOpacity(FStyleColors::Foreground)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PillarMeshNoPreview", "\u65e0\u9884\u89c8"))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FStyleColors::Foreground)
			]
		];

	if (UStaticMesh* PreviewMesh = GetPreviewMeshFromPillarMeshRow(Row))
	{
		TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(FAssetData(PreviewMesh), 112, 112, PillarMeshThumbnailPool);
		PillarMeshThumbnails.Add(Thumbnail);
		PreviewWidget = Thumbnail->MakeThumbnailWidget();
	}

	const FText NameText = FText::FromName(RowName);
	const FText SizeText = Row
		? FText::Format(
			LOCTEXT("PillarMeshCardSize", "\u5bbd {0} / \u6df1 {1} / \u9ad8 {2} cm"),
			FText::AsNumber(Row->Width),
			FText::AsNumber(Row->Depth),
			FText::AsNumber(Row->Height))
		: LOCTEXT("PillarMeshCardInvalidSize", "\u65e0\u6548\u67f1\u4f53\u6570\u636e");
	const FText MeshText = Row && !Row->SourceMeshName.IsNone()
		? FText::FromName(Row->SourceMeshName)
		: LOCTEXT("PillarMeshCardNoSourceName", "\u672a\u8bb0\u5f55\u6e90\u7f51\u683c");
	const FText StatsText = Row
		? FText::Format(
			LOCTEXT("PillarMeshCardStats", "{0} tris / {1} verts"),
			FText::AsNumber(Row->SourceTriangleCount),
			FText::AsNumber(Row->SourceVertexCount))
		: FText::GetEmpty();

	return SNew(SEHBPillarMeshDragHandle)
		.OwnerWidget(SharedThis(this))
		.RowName(RowName)
		[
			SNew(SBox)
			.WidthOverride(250.0f)
			[
				SNew(SBorder)
				.Padding(8.0f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.10f, 0.14f, 0.11f, 1.0f))
				.ToolTipText(Row
					? FText::Format(LOCTEXT("PillarMeshCardTip", "{0}\n\u6e90\u7f51\u683c\u4f53\uff1a{1}\n\u62d6\u5230\u5df2\u6709\u67f1\u4f53\u4e0a\u8fdb\u884c\u9884\u89c8\u3002"), NameText, MeshText)
					: LOCTEXT("PillarMeshInvalidRowTip", "\u8be5\u6570\u636e\u8868\u884c\u65e0\u6548\u6216\u65e0\u6cd5\u8bfb\u53d6\u3002"))
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
						.Text(MeshText)
						.ColorAndOpacity(FStyleColors::Foreground)
						.AutoWrapText(true)
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(StatsText)
						.ColorAndOpacity(FStyleColors::Foreground)
						.AutoWrapText(true)
					]
				]
			]
		];
}

void SEasyHouseBuilderPanel::RefreshBuildingList()
{
	if (!BuildingListBox.IsValid())
	{
		return;
	}

	CachedBuildingActors.Reset();
	BuildingListBox->ClearChildren();

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		BuildingListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoEditorWorld", "\u672a\u627e\u5230\u53ef\u7528\u7684\u7f16\u8f91\u5668\u4e16\u754c"))
			.ColorAndOpacity(FStyleColors::Foreground)
			.ToolTipText(LOCTEXT("NoEditorWorldTip", "\u5f53\u524d\u6ca1\u6709\u53ef\u7528\u7684\u7f16\u8f91\u5668\u4e16\u754c\uff0c\u56e0\u6b64\u65e0\u6cd5\u67e5\u8be2\u6216\u521b\u5efa\u5efa\u7b51\u5bf9\u8c61\u3002"))
		];
		return;
	}

	// 扫描当前编辑世界中的所有建筑对象。TActorIterator 默认只遍历活动关卡并跳过待销毁 Actor。
	for (TActorIterator<AEHBBuildingActorBase> It(World); It; ++It)
	{
		AEHBBuildingActorBase* Building = *It;
		if (!Building || Building->IsPendingKillPending())
		{
			continue;
		}

		CachedBuildingActors.Add(Building);
	}

	if (CachedBuildingActors.Num() == 0)
	{
		BuildingListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoBuildingActors", "\u5f53\u524d\u573a\u666f\u4e2d\u8fd8\u6ca1\u6709\u5efa\u7b51\u5bf9\u8c61"))
			.ColorAndOpacity(FStyleColors::Foreground)
			.ToolTipText(LOCTEXT("NoBuildingActorsTip", "\u53ef\u4ee5\u70b9\u51fb\u4e0a\u65b9\u201c\u521b\u5efa\u5efa\u7b51\u5bf9\u8c61\u201d\u6309\u94ae\uff0c\u5728\u5f53\u524d\u89c6\u53e3\u4e2d\u5fc3\u5c04\u7ebf\u547d\u4e2d\u5904\u751f\u6210\u4e00\u4e2a\u65b0\u7684\u5efa\u7b51\u5bf9\u8c61\u3002"))
		];
		return;
	}

	for (TWeakObjectPtr<AEHBBuildingActorBase> Building : CachedBuildingActors)
	{
		AEHBBuildingActorBase* BuildingPtr = Building.Get();
		if (!BuildingPtr)
		{
			continue;
		}

		BuildingListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SButton)
			.Text(GetBuildingDisplayText(BuildingPtr))
			.ToolTipText(LOCTEXT("BuildingListItemTip", "\u70b9\u51fb\u540e\u5c06\u8be5\u5efa\u7b51 Actor \u8bbe\u4e3a\u5f53\u524d\u5efa\u7b51\u5bf9\u8c61\uff0c\u5e76\u5728\u573a\u666f\u4e2d\u9009\u4e2d\u5b83\u3002\u540e\u7eed\u6240\u6709\u5efa\u7b51\u5143\u7d20\u90fd\u4f1a\u5728\u8fd9\u4e2a\u5efa\u7b51\u5bf9\u8c61\u4e0b\u521b\u5efa\u548c\u4fdd\u5b58\u3002"))
			.HAlign(HAlign_Left)
			.OnClicked(this, &SEasyHouseBuilderPanel::HandleSelectBuildingClicked, Building)
		];
	}
}

FReply SEasyHouseBuilderPanel::HandleCreateBuildingClicked()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return FReply::Handled();
	}

	FVector SpawnLocation = FVector::ZeroVector;
	if (!GetViewportCenterPlacementLocation(World, SpawnLocation))
	{
		return FReply::Handled();
	}

	const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
	UClass* BuildingClass = Settings && !Settings->BuildingActorClass.IsNull()
		? Settings->BuildingActorClass.LoadSynchronous()
		: nullptr;

	if (!BuildingClass || !BuildingClass->IsChildOf(AEHBBuildingActorBase::StaticClass()))
	{
		BuildingClass = AEHB_Building::StaticClass();
	}

	const FScopedTransaction Transaction(LOCTEXT("CreateBuildingTransaction", "\u521b\u5efa\u5efa\u7b51\u5bf9\u8c61"));
	World->Modify();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.OverrideLevel = World->GetCurrentLevel();
	SpawnParameters.ObjectFlags = RF_Transactional;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AEHBBuildingActorBase* NewBuilding = World->SpawnActor<AEHBBuildingActorBase>(
		BuildingClass,
		FTransform(FRotator::ZeroRotator, SpawnLocation),
		SpawnParameters);

	if (NewBuilding)
	{
		NewBuilding->Modify();
		NewBuilding->EnsureBuildingGuid();

#if WITH_EDITOR
		NewBuilding->SetActorLabel(FString::Printf(TEXT("\u5efa\u7b51\u5bf9\u8c61_%03d"), CachedBuildingActors.Num() + 1));
#endif

		SetActiveBuilding(NewBuilding);
		RefreshBuildingList();
	}

	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCopyBuildingClicked()
{
	auto* Source=ActiveBuilding.Get();
	if(!Source||!GEditor||EasyHouseBuilderPanel::GetEditorSelectedBuilding()!=Source)return FReply::Handled();
	const auto Result=EHBBuildingCopy::Execute(Source,EHBBuildingCopy::SuggestedWorldOffset(Source));
	FText Message;
	if(Result.bSucceeded)
	{
		if (auto* Mode=GetActiveEditorMode()) Mode->AdoptCopiedBuilding(Result.Building);
		SetActiveBuilding(Result.Building);RefreshBuildingList();
		Message=LOCTEXT("CopyWholeBuildingDone","已创建独立建筑副本，并选中新建筑；可用撤销恢复。");
	}
	else if(Result.Status==TEXT("RequiresTypedNodeOwnership"))
		Message=LOCTEXT("CopyWholeBuildingNeedsNodes","此建筑尚未准备墙柱节点连接，暂不能整栋复制。");
	else
		Message=FText::Format(LOCTEXT("CopyWholeBuildingRejected","无法整栋复制：当前构件或关联尚未适配。原建筑已保留。原因：{0}"),FText::FromName(Result.FailureReason.IsNone()?Result.Status:Result.FailureReason));
	FNotificationInfo Notice(Message);Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleEnableWallNodeEditingClicked()
{
	const auto Result=UEHBBuildingToolset::EnableWallNodeEditing(ActiveBuilding.Get(),false);
	const FText Message=Result.bSucceeded?LOCTEXT("WallNodeEditingDone","墙角联动已启用，已有柱身保留。可用撤销恢复启用前的状态。")
		:FText::Format(LOCTEXT("WallNodeEditingRejected","未能启用墙角联动：{0}。请查看当前建筑的构件和关联。"),FText::FromString(Result.Message));
	FNotificationInfo Notice(Message);Notice.ExpireDuration=6;FSlateNotificationManager::Get().AddNotification(Notice);
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleSelectBuildingClicked(TWeakObjectPtr<AEHBBuildingActorBase> Building)
{
	SetActiveBuilding(Building.Get());
	RefreshBuildingList();
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCreateWallClicked()
{
	if (!ActiveBuilding.IsValid())
	{
		SwitchToPanel(EEasyHouseToolPanel::BuildingSelection);
		if (BuildingSelectionHintText.IsValid())
		{
			BuildingSelectionHintText->SetText(LOCTEXT("NeedBuildingForWallCreation", "\u9700\u8981\u5148\u9009\u62e9\u6216\u521b\u5efa\u4e00\u4e2a\u5efa\u7b51\u5bf9\u8c61\uff0c\u7136\u540e\u624d\u80fd\u5728\u5b83\u91cc\u9762\u521b\u5efa\u5899\u4f53\u3002"));
		}
		return FReply::Handled();
	}

	if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
	{
		EditorMode->SetWallCreationPhysicalColumns(bWallCreationPhysicalColumns);
		EditorMode->BeginWallCreation(ActiveBuilding.Get(), WallCreationHeight, WallCreationThickness);
		if (WallCreationStatusText.IsValid())
		{
			WallCreationStatusText->SetText(LOCTEXT("WallCreationToolStarted", "\u5899\u9762\u521b\u5efa\u5de5\u5177\u5df2\u5f00\u542f\uff1a\u5728\u89c6\u53e3\u4e2d\u6309\u4f4f\u5de6\u952e\u62d6\u62fd\uff0c\u91ca\u653e\u540e\u751f\u6210\u8d77\u70b9\u67f1\u3001\u7ec8\u70b9\u67f1\u548c\u5de6\u53f3\u4e24\u4fa7\u7b80\u5355\u5899\u9762\u3002"));
		}
	}

	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCreateRailingClicked()
{
	if (!ActiveBuilding.IsValid())
	{
		SwitchToPanel(EEasyHouseToolPanel::BuildingSelection);
		if (BuildingSelectionHintText.IsValid())
		{
			BuildingSelectionHintText->SetText(LOCTEXT("NeedBuildingForRailingCreation", "\u9700\u8981\u5148\u9009\u62e9\u6216\u521b\u5efa\u4e00\u4e2a\u5efa\u7b51\u5bf9\u8c61\uff0c\u7136\u540e\u624d\u80fd\u5728\u5b83\u91cc\u9762\u521b\u5efa\u6276\u624b\u3002"));
		}
		return FReply::Handled();
	}

	if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
	{
		EditorMode->BeginRailingCreation(
			ActiveBuilding.Get(),
			RailingCreationHeight,
			RailingCreationPostSpacing,
			RailingCreationThickness);
		if (RailingCreationStatusText.IsValid())
		{
			RailingCreationStatusText->SetText(LOCTEXT("RailingCreationToolStarted", "\u6276\u624b\u521b\u5efa\u5de5\u5177\u5df2\u5f00\u542f\uff1a\u5728\u89c6\u53e3\u4e2d\u6309\u4f4f\u5de6\u952e\u62d6\u62fd\uff0c\u91ca\u653e\u540e\u751f\u6210\u4e00\u6bb5\u76f4\u7ebf\u6276\u624b\u3002"));
		}
	}

	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCreateFoundationSlabClicked()
{
	if (!ActiveBuilding.IsValid())
	{
		SwitchToPanel(EEasyHouseToolPanel::BuildingSelection);
		if (BuildingSelectionHintText.IsValid())
		{
			BuildingSelectionHintText->SetText(LOCTEXT("NeedBuildingForFoundationSlab", "需要先选择或创建一个建筑对象，然后才能创建地基/层板。"));
		}
		return FReply::Handled();
	}

	if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
	{
		EditorMode->BeginFloorSlabPlacement(ActiveBuilding.Get(), true, FloorSlabCreationSize, FloorSlabCreationThickness);
	}
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCreateFloorSlabClicked()
{
	if (!ActiveBuilding.IsValid())
	{
		SwitchToPanel(EEasyHouseToolPanel::BuildingSelection);
		if (BuildingSelectionHintText.IsValid())
		{
			BuildingSelectionHintText->SetText(LOCTEXT("NeedBuildingForFloorSlab", "需要先选择或创建一个建筑对象，然后才能创建地基/层板。"));
		}
		return FReply::Handled();
	}

	if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
	{
		EditorMode->BeginFloorSlabPlacement(ActiveBuilding.Get(), false, FloorSlabCreationSize, FloorSlabCreationThickness);
	}
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCreateFloorFinishClicked()
{
	if (!ActiveBuilding.IsValid())
	{
		SwitchToPanel(EEasyHouseToolPanel::BuildingSelection);
		if (BuildingSelectionHintText.IsValid())
		{
			BuildingSelectionHintText->SetText(LOCTEXT("NeedBuildingForFloorFinish", "\u9700\u8981\u5148\u9009\u62e9\u6216\u521b\u5efa\u4e00\u4e2a\u5efa\u7b51\u5bf9\u8c61\uff0c\u7136\u540e\u624d\u80fd\u521b\u5efa\u5730\u677f\u3002"));
		}
		return FReply::Handled();
	}

	if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
	{
		EditorMode->BeginFloorPlacement(ActiveBuilding.Get(), FloorFinishCreationSize);
	}
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCreateRoofClicked()
{
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[EHB RoofCreate] button clicked type=%d implemented=%d activeBuilding=%s"),
		static_cast<int32>(ActiveRoofCreationType),
		IsSelectedRoofCreationTypeImplemented() ? 1 : 0,
		ActiveBuilding.IsValid() ? *ActiveBuilding->GetName() : TEXT("None"));

	if (!IsSelectedRoofCreationTypeImplemented())
	{
		return FReply::Handled();
	}

	if (!ActiveBuilding.IsValid())
	{
		SwitchToPanel(EEasyHouseToolPanel::BuildingSelection);
		if (BuildingSelectionHintText.IsValid())
		{
			BuildingSelectionHintText->SetText(LOCTEXT(
				"NeedBuildingForRoof",
				"\u9700\u8981\u5148\u9009\u62e9\u6216\u521b\u5efa\u4e00\u4e2a\u5efa\u7b51\u5bf9\u8c61\uff0c\u7136\u540e\u624d\u80fd\u521b\u5efa\u5c4b\u9876\u3002"));
		}
		return FReply::Handled();
	}

	if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[EHB RoofCreate] begin placement type=%d length=%.2f width=%.2f pitch=%.2f thickness=%.2f eave=%.2f"),
			static_cast<int32>(ActiveRoofCreationType),
			RoofCreationLength,
			RoofCreationWidth,
			RoofCreationPitchDegrees,
			RoofCreationThickness,
			RoofCreationEaveOffset);
		EditorMode->BeginRoofPlacement(
			ActiveBuilding.Get(),
			RoofCreationLength,
			RoofCreationWidth,
			RoofCreationPitchDegrees,
			RoofCreationThickness,
			RoofCreationEaveOffset,
			bRoofCreationGenerateRidge,
			bRoofCreationGenerateEaves,
			bRoofCreationGenerateGableRakes,
			bRoofCreationGenerateGableEndWalls,
			RoofCreationGableEndWallBoundaryInset,
			ActiveRoofCreationType == EEasyHouseRoofCreationType::Hip
				|| ActiveRoofCreationType == EEasyHouseRoofCreationType::HalfHip,
			ActiveRoofCreationType == EEasyHouseRoofCreationType::HalfHip);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[EHB RoofCreate] editor mode is not active; roof placement was not started"));
	}
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCreateStairClicked()
{
	if (!ActiveBuilding.IsValid())
	{
		SwitchToPanel(EEasyHouseToolPanel::BuildingSelection);
		if (BuildingSelectionHintText.IsValid())
		{
			BuildingSelectionHintText->SetText(LOCTEXT(
				"NeedBuildingForStair",
				"\u9700\u8981\u5148\u9009\u62e9\u6216\u521b\u5efa\u4e00\u4e2a\u5efa\u7b51\u5bf9\u8c61\uff0c\u7136\u540e\u624d\u80fd\u521b\u5efa\u697c\u68af\u3002"));
		}
		return FReply::Handled();
	}

	if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
	{
		EditorMode->BeginStairPlacement(
			ActiveBuilding.Get(),
			StairCreationHeight,
			StairCreationWidth,
			StairCreationTreadDepth,
			bStairCreationGenerateTreads,
			bStairCreationFillRisers,
			bStairCreationFillBottomPart,
			bStairCreationGenerateSides,
			bStairCreationGenerateSideGuards,
			bStairCreationGenerateRailing,
			bStairCreationGenerateLeftRailing,
			bStairCreationGenerateRightRailing,
			StairCreationRailingStepsPerPost,
			StairCreationRailingEdgeInset,
			StairCreationRailingPostForwardOffset);
	}
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleGenerateBuildingFromJsonClicked()
{
	auto SetJsonStatus = [this](const FText& StatusText)
	{
		if (AIJsonGenerationStatusText.IsValid())
		{
			AIJsonGenerationStatusText->SetText(StatusText);
		}
	};

	if (!AIJsonTestTextBox.IsValid())
	{
		return FReply::Handled();
	}

	AEHBBuildingActorBase* Building = EasyHouseBuilderPanel::GetEditorSelectedBuilding();
	if (!Building)
	{
		SetJsonStatus(LOCTEXT("AIJsonNoSelectedBuilding", "Select an EHB_Building actor in the editor before running AI generation."));
		return FReply::Handled();
	}

	SetActiveBuilding(Building);
	UWorld* World = Building ? Building->GetWorld() : nullptr;
	if (!Building || !World)
	{
		SetJsonStatus(LOCTEXT("AIJsonInvalidBuilding", "当前建筑对象无效，无法生成。"));
		return FReply::Handled();
	}

	const FString JsonText = AIJsonTestTextBox->GetText().ToString().TrimStartAndEnd();
	if (JsonText.IsEmpty())
	{
		SetJsonStatus(LOCTEXT("AIJsonEmptyInput", "JSON 为空。"));
		return FReply::Handled();
	}

	FEHBAIWorkflowDefaults WorkflowDefaults;
	WorkflowDefaults.WallHeight = WallCreationHeight;
	WorkflowDefaults.WallThickness = WallCreationThickness;
	WorkflowDefaults.FloorSlabThickness = FloorSlabCreationThickness;

	const FEHBAIWorkflowExecutionResult WorkflowResult = FEHBAIWorkflowExecutor::ExecuteJson(Building, JsonText, WorkflowDefaults);
	SetJsonStatus(WorkflowResult.StatusText);
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCopyCurrentBuildingDataClicked()
{
	auto SetJsonStatus = [this](const FText& StatusText)
	{
		if (AIJsonGenerationStatusText.IsValid())
		{
			AIJsonGenerationStatusText->SetText(StatusText);
		}
	};

	if (!ActiveBuilding.IsValid())
	{
		SetJsonStatus(LOCTEXT("CopyCurrentBuildingDataNoActiveBuilding", "请先选择或创建一个建筑对象，再复制当前建筑数据。"));
		return FReply::Handled();
	}

	const FString ContextText = BuildDetailedCurrentBuildingAIContext();
	FPlatformApplicationMisc::ClipboardCopy(*ContextText);
	SetJsonStatus(FText::Format(
		LOCTEXT("CopyCurrentBuildingDataSucceeded", "已复制当前建筑数据，约 {0} 个字符。可以粘贴给 AI 作为后续生成上下文。"),
		FText::AsNumber(ContextText.Len())));
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleAttachAIImagesClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		AppendAIConversationMessage(LOCTEXT("AIImagePickerUnavailable", "\u5f53\u524d\u7f16\u8f91\u5668\u65e0\u6cd5\u6253\u5f00\u7cfb\u7edf\u6587\u4ef6\u9009\u62e9\u5668\u3002"), false);
		return FReply::Handled();
	}

	TArray<FString> SelectedFiles;
	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("\u9009\u62e9 AI \u56fe\u7247\u9644\u4ef6"),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("\u56fe\u7247\u6587\u4ef6|*.png;*.jpg;*.jpeg;*.webp;*.gif"),
		EFileDialogFlags::Multiple,
		SelectedFiles);

	if (!bOpened || SelectedFiles.Num() == 0)
	{
		return FReply::Handled();
	}

	for (const FString& SelectedFile : SelectedFiles)
	{
		const FString ImagePath = FPaths::ConvertRelativePathToFull(SelectedFile);
		if (GetAIImageMimeType(ImagePath).IsEmpty())
		{
			AppendAIConversationMessage(
				FText::Format(LOCTEXT("AIImageUnsupportedType", "\u5df2\u5ffd\u7565\u4e0d\u652f\u6301\u7684\u56fe\u7247\u7c7b\u578b\uff1a{0}"), FText::FromString(ImagePath)),
				false);
			continue;
		}

		PendingAIImagePaths.AddUnique(ImagePath);
	}

	RefreshAIImageAttachmentList();
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleClearAIImagesClicked()
{
	PendingAIImagePaths.Reset();
	RefreshAIImageAttachmentList();
	return FReply::Handled();
}

void SEasyHouseBuilderPanel::RefreshAIImageAttachmentList()
{
	if (!AIImageAttachmentListBox.IsValid())
	{
		return;
	}

	AIImageAttachmentListBox->ClearChildren();
	if (PendingAIImagePaths.Num() == 0)
	{
		return;
	}

	AIImageAttachmentListBox->AddSlot()
	.AutoHeight()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("AIImageAttachmentCount", "\u5df2\u6dfb\u52a0 {0} \u5f20\u56fe\u7247"), FText::AsNumber(PendingAIImagePaths.Num())))
			.ColorAndOpacity(FStyleColors::Foreground)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("ClearAIImagesButton", "\u6e05\u7a7a\u56fe\u7247"))
			.HAlign(HAlign_Center)
			.OnClicked(this, &SEasyHouseBuilderPanel::HandleClearAIImagesClicked)
		]
	];

	for (const FString& ImagePath : PendingAIImagePaths)
	{
		AIImageAttachmentListBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 3.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("- %s"), *FPaths::GetCleanFilename(ImagePath))))
			.ToolTipText(FText::FromString(ImagePath))
			.ColorAndOpacity(FStyleColors::Foreground)
		];
	}
}

FString SEasyHouseBuilderPanel::BuildAIUserVisibleMessage(const FString& UserMessage, const TArray<FString>& ImagePaths) const
{
	FString VisibleMessage = UserMessage.TrimStartAndEnd();
	if (VisibleMessage.IsEmpty() && ImagePaths.Num() > 0)
	{
		VisibleMessage = TEXT("\u8bf7\u7ed3\u5408\u8fd9\u4e9b\u56fe\u7247\u7ed9\u51fa\u5efa\u7b51\u5efa\u8bae\u6216\u4fee\u6539\u65b9\u6848\u3002");
	}

	if (ImagePaths.Num() > 0)
	{
		VisibleMessage += TEXT("\n\n[\u56fe\u7247\u9644\u4ef6]");
		for (const FString& ImagePath : ImagePaths)
		{
			VisibleMessage += FString::Printf(TEXT("\n- %s"), *FPaths::GetCleanFilename(ImagePath));
		}
	}

	return VisibleMessage;
}

FString SEasyHouseBuilderPanel::BuildLocalCodexImageAttachmentPromptBlock(const TArray<FString>& ImagePaths) const
{
	if (ImagePaths.Num() == 0)
	{
		return FString();
	}

	FString PromptBlock = TEXT("\n\n\u56fe\u7247\u9644\u4ef6\uff08\u672c\u5730\u7edd\u5bf9\u8def\u5f84\uff09\uff1a");
	for (const FString& ImagePath : ImagePaths)
	{
		PromptBlock += FString::Printf(TEXT("\n- %s"), *FPaths::ConvertRelativePathToFull(ImagePath));
	}
	PromptBlock += TEXT("\n\u8bf7\u6839\u636e\u8fd9\u4e9b\u56fe\u7247\u7684\u53ef\u89c1\u5185\u5bb9\u7406\u89e3\u7528\u6237\u610f\u56fe\uff1b\u5982\u9700\u8981\u7ec6\u8282\uff0c\u8bf7\u76f4\u63a5\u8bfb\u53d6\u4e0a\u8ff0\u672c\u5730\u6587\u4ef6\u3002");
	return PromptBlock;
}

FString SEasyHouseBuilderPanel::GetAIImageMimeType(const FString& ImagePath) const
{
	const FString Extension = FPaths::GetExtension(ImagePath, true).ToLower();
	if (Extension == TEXT(".png"))
	{
		return TEXT("image/png");
	}
	if (Extension == TEXT(".jpg") || Extension == TEXT(".jpeg"))
	{
		return TEXT("image/jpeg");
	}
	if (Extension == TEXT(".webp"))
	{
		return TEXT("image/webp");
	}
	if (Extension == TEXT(".gif"))
	{
		return TEXT("image/gif");
	}
	return FString();
}

bool SEasyHouseBuilderPanel::BuildAIImageDataUrl(const FString& ImagePath, FString& OutDataUrl, FText& OutError) const
{
	OutDataUrl.Reset();

	const FString MimeType = GetAIImageMimeType(ImagePath);
	if (MimeType.IsEmpty())
	{
		OutError = FText::Format(LOCTEXT("AIImageUnsupportedForSend", "\u4e0d\u652f\u6301\u7684\u56fe\u7247\u7c7b\u578b\uff1a{0}"), FText::FromString(ImagePath));
		return false;
	}

	if (!FPaths::FileExists(ImagePath))
	{
		OutError = FText::Format(LOCTEXT("AIImageFileMissing", "\u56fe\u7247\u6587\u4ef6\u4e0d\u5b58\u5728\uff1a{0}"), FText::FromString(ImagePath));
		return false;
	}

	const int64 FileSize = IFileManager::Get().FileSize(*ImagePath);
	constexpr int64 MaxImageBytes = 20ll * 1024ll * 1024ll;
	if (FileSize <= 0 || FileSize > MaxImageBytes)
	{
		OutError = FText::Format(LOCTEXT("AIImageFileTooLarge", "\u56fe\u7247\u5927\u5c0f\u5fc5\u987b\u5728 1B \u5230 20MB \u4e4b\u95f4\uff1a{0}"), FText::FromString(ImagePath));
		return false;
	}

	TArray<uint8> ImageBytes;
	if (!FFileHelper::LoadFileToArray(ImageBytes, *ImagePath))
	{
		OutError = FText::Format(LOCTEXT("AIImageReadFailed", "\u65e0\u6cd5\u8bfb\u53d6\u56fe\u7247\u6587\u4ef6\uff1a{0}"), FText::FromString(ImagePath));
		return false;
	}

	OutDataUrl = FString::Printf(TEXT("data:%s;base64,%s"), *MimeType, *FBase64::Encode(ImageBytes));
	return true;
}


FReply SEasyHouseBuilderPanel::HandleSendAIMessageClicked()
{
	if (!AIInputTextBox.IsValid())
	{
		return FReply::Handled();
	}

	const FString UserMessage = AIInputTextBox->GetText().ToString().TrimStartAndEnd();
	TArray<FString> ImagePaths = PendingAIImagePaths;
	if (UserMessage.IsEmpty() && ImagePaths.Num() == 0)
	{
		return FReply::Handled();
	}

	const FString VisibleUserMessage = BuildAIUserVisibleMessage(UserMessage, ImagePaths);
	AppendAIConversationMessage(FText::FromString(VisibleUserMessage), true);
	AIInputTextBox->SetText(FText::GetEmpty());
	PendingAIImagePaths.Reset();
	RefreshAIImageAttachmentList();
	SendAIChatRequest(VisibleUserMessage, ImagePaths);
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCopyMCPPromptClicked()
{
	const FString PromptText = LoadAICreationSystemPrompt();
	FPlatformApplicationMisc::ClipboardCopy(*PromptText);
	AppendAIConversationMessage(
		FText::Format(LOCTEXT("CopyMCPPromptSucceeded", "已复制 MCP 建筑代理完整使用规范，约 {0} 个字符。"), FText::AsNumber(PromptText.Len())),
		false);
	return FReply::Handled();
}

FReply SEasyHouseBuilderPanel::HandleCopyMCPStarterPromptClicked()
{
	const UEHBAISettings* AISettings = GetDefault<UEHBAISettings>();
	const FString ServerName = AISettings && !AISettings->MCPServerName.TrimStartAndEnd().IsEmpty()
		? AISettings->MCPServerName.TrimStartAndEnd()
		: FString(TEXT("bpt-unreal"));
	const FString StarterPrompt = FString::Printf(
		TEXT("请使用 %s MCP 工具控制当前 UE 建筑工具集。先调用 bpt_get_status 检查 UE 桥服务，然后调用 bpt_get_building_snapshot 读取当前建筑。")
		TEXT("请遵守服务器 instructions 中的建筑生成规范：大改动先 bpt_preview_building_patch 预览，再 bpt_validate_building 校验；")
		TEXT("确认视觉结果后再 bpt_commit_preview。不要生成重叠长墙，房间共用墙角必须复用柱子，房间地板和顶部层板通过 generate_room_surfaces 生成。")
		TEXT("屋顶生成功能暂时关闭：不要生成屋顶，不要调用屋顶工具，也不要虚构屋顶操作。"),
		*ServerName);
	FPlatformApplicationMisc::ClipboardCopy(*StarterPrompt);
	AppendAIConversationMessage(LOCTEXT("CopyMCPStarterPromptSucceeded", "已复制 MCP 启动语。把它粘贴到 Codex、Cursor 或 Claude Desktop 的对话框即可开始。"), false);
	return FReply::Handled();
}

void SEasyHouseBuilderPanel::SendAIChatRequest(const FString& UserMessage, const TArray<FString>& ImagePaths)
{
	const UEHBAISettings* AISettings = GetDefault<UEHBAISettings>();
	if (!AISettings)
	{
		AppendAIConversationMessage(LOCTEXT("AISettingsMissingReply", "没有找到建筑 AI 配置。请在 Project Settings / Plugins / 建筑 AI 配置中检查设置。"), false);
		return;
	}

	if (!AISettings->bEnableEmbeddedAIChat)
	{
		AppendAIConversationMessage(LOCTEXT("AIEmbeddedChatDisabledReply", "UE 内置 AI 对话尚未启用。请打开 Project Settings / Plugins / 建筑 AI 配置，勾选“启用 UE 内置 AI 对话”。推荐使用“本地 Codex”模式，不需要在插件中填写 API Key。"), false);
		return;
	}

	if (bAIRequestInFlight)
	{
		AppendAIConversationMessage(LOCTEXT("AIRequestInFlightReply", "上一条 AI 请求还没有返回，请稍等一下。"), false);
		return;
	}

	bAIRequestInFlight = true;
	EmbeddedAIConversationHistory.Add(TPair<bool, FString>(true, UserMessage));
	AppendAIConversationMessage(LOCTEXT("AIRequestStartedReply", "正在连接 AI..."), false);

	if (AISettings->EmbeddedChatProvider == EEHBAIEmbeddedChatProvider::LocalCodex)
	{
		SendLocalCodexChatRequest(AISettings, UserMessage, ImagePaths);
	}
	else
	{
		SendHTTPAIChatRequest(AISettings, ImagePaths);
	}
}

void SEasyHouseBuilderPanel::SendHTTPAIChatRequest(const UEHBAISettings* AISettings, const TArray<FString>& ImagePaths)
{
	auto FailRequest = [this](const FText& ErrorMessage)
	{
		bAIRequestInFlight = false;
		if (EmbeddedAIConversationHistory.Num() > 0 && EmbeddedAIConversationHistory.Last().Key)
		{
			EmbeddedAIConversationHistory.Pop();
		}
		AppendAIConversationMessage(ErrorMessage, false);
	};

	if (!AISettings)
	{
		FailRequest(LOCTEXT("AIHTTPSettingsMissingReply", "没有找到建筑 AI 配置。请在 Project Settings / Plugins / 建筑 AI 配置中检查设置。"));
		return;
	}

	if (AISettings->EmbeddedChatEndpoint.TrimStartAndEnd().IsEmpty())
	{
		FailRequest(LOCTEXT("AIEmbeddedEndpointMissingReply", "UE 内置对话的 AI API 地址为空。请先在建筑 AI 配置中填写地址，或切换到“本地 Codex”模式。"));
		return;
	}

	if (AISettings->EmbeddedChatAPIKey.TrimStartAndEnd().IsEmpty())
	{
		FailRequest(LOCTEXT("AIEmbeddedAPIKeyMissingReply", "HTTP API 模式的 API Key 为空。请先在建筑 AI 配置中填写 Key，或切换到“本地 Codex”模式。"));
		return;
	}

	if (AISettings->EmbeddedChatModel.TrimStartAndEnd().IsEmpty())
	{
		FailRequest(LOCTEXT("AIEmbeddedModelMissingReply", "HTTP API 模式的模型名称为空。请先在建筑 AI 配置中填写模型。"));
		return;
	}

	for (const FString& ImagePath : ImagePaths)
	{
		FString DataUrl;
		FText Error;
		if (!BuildAIImageDataUrl(ImagePath, DataUrl, Error))
		{
			FailRequest(Error);
			return;
		}
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(AISettings->EmbeddedChatEndpoint.TrimStartAndEnd());
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	const FString TrimmedAPIKey = AISettings->EmbeddedChatAPIKey.TrimStartAndEnd();
	const FString AuthorizationHeader = AISettings->EmbeddedChatAuthMode == EEHBAIEmbeddedChatAuthMode::RawAuthorizationValue
		? TrimmedAPIKey
		: FString::Printf(TEXT("Bearer %s"), *TrimmedAPIKey);
	Request->SetHeader(TEXT("Authorization"), AuthorizationHeader);
	Request->SetTimeout(static_cast<float>(FMath::Max(5, AISettings->EmbeddedChatTimeoutSeconds)));
	Request->SetContentAsString(BuildEmbeddedAIRequestBody(AISettings, ImagePaths));
	Request->OnProcessRequestComplete().BindSP(SharedThis(this), &SEasyHouseBuilderPanel::HandleAIChatResponse);

	if (!Request->ProcessRequest())
	{
		FailRequest(LOCTEXT("AIRequestStartFailedReply", "AI 请求没有成功发出。请检查 API 地址和网络连接。"));
	}
}

void SEasyHouseBuilderPanel::SendLocalCodexChatRequest(const UEHBAISettings* AISettings, const FString& UserMessage, const TArray<FString>& ImagePaths)
{
	auto FailRequest = [this](const FText& ErrorMessage)
	{
		bAIRequestInFlight = false;
		if (EmbeddedAIConversationHistory.Num() > 0 && EmbeddedAIConversationHistory.Last().Key)
		{
			EmbeddedAIConversationHistory.Pop();
		}
		AppendAIConversationMessage(ErrorMessage, false);
	};

	if (!AISettings)
	{
		FailRequest(LOCTEXT("AILocalCodexSettingsMissingReply", "没有找到建筑 AI 配置。请在 Project Settings / Plugins / 建筑 AI 配置中检查设置。"));
		return;
	}

	const FString CodexCommand = AISettings->LocalCodexExecutable.TrimStartAndEnd().IsEmpty()
		? FString(TEXT("codex"))
		: AISettings->LocalCodexExecutable.TrimStartAndEnd();
	const bool bCodexCommandLooksLikePath =
		CodexCommand.Contains(TEXT(":"))
		|| CodexCommand.Contains(TEXT("/"))
		|| CodexCommand.Contains(TEXT("\\"));
	if (bCodexCommandLooksLikePath && !FPaths::FileExists(CodexCommand))
	{
		FailRequest(FText::Format(
			LOCTEXT("AILocalCodexExecutableMissingReply", "本地 Codex 可执行文件不存在：{0}。请在建筑 AI 配置中修正“Codex 可执行文件”，或留空使用 PATH 中的 codex。"),
			FText::FromString(CodexCommand)));
		return;
	}

	const FString NodeCommand = AISettings->NodeExecutable.TrimStartAndEnd().IsEmpty()
		? FString(TEXT("node"))
		: AISettings->NodeExecutable.TrimStartAndEnd();
	const FString BridgeScriptPath = GetLocalCodexChatBridgeScriptPath();
	if (!FPaths::FileExists(BridgeScriptPath))
	{
		FailRequest(FText::Format(
			LOCTEXT("AILocalCodexBridgeScriptMissingReply", "本地 Codex 桥接脚本不存在：{0}。请确认插件 MCP 目录完整。"),
			FText::FromString(BridgeScriptPath)));
		return;
	}

	const FString MCPServerScriptPath = AISettings->GetMCPServerScriptPath();
	if (!FPaths::FileExists(MCPServerScriptPath))
	{
		FailRequest(FText::Format(
			LOCTEXT("AILocalCodexMCPServerScriptMissingReply", "bpt-unreal MCP Server 脚本不存在：{0}。请确认插件 MCP 目录完整。"),
			FText::FromString(MCPServerScriptPath)));
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("codexCommand"), CodexCommand);
	RootObject->SetStringField(TEXT("cwd"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	RootObject->SetStringField(TEXT("prompt"), BuildLocalCodexChatPrompt(UserMessage, ImagePaths));
	RootObject->SetStringField(TEXT("developerInstructions"), BuildLocalCodexDeveloperInstructions());
	RootObject->SetNumberField(TEXT("timeoutMs"), FMath::Max(300, AISettings->EmbeddedChatTimeoutSeconds) * 1000);
	RootObject->SetStringField(TEXT("approvalPolicy"), TEXT("never"));
	RootObject->SetStringField(TEXT("sandbox"), TEXT("danger-full-access"));

	const FString ModelName = AISettings->EmbeddedChatModel.TrimStartAndEnd();
	if (!ModelName.IsEmpty() && ModelName != TEXT("gpt-5.5"))
	{
		RootObject->SetStringField(TEXT("model"), ModelName);
	}

	if (!LocalCodexThreadId.IsEmpty())
	{
		RootObject->SetStringField(TEXT("threadId"), LocalCodexThreadId);
	}

	if (ImagePaths.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ImagePathValues;
		ImagePathValues.Reserve(ImagePaths.Num());
		for (const FString& ImagePath : ImagePaths)
		{
			ImagePathValues.Add(MakeShared<FJsonValueString>(FPaths::ConvertRelativePathToFull(ImagePath)));
		}
		RootObject->SetArrayField(TEXT("imagePaths"), ImagePathValues);
	}

	TSharedRef<FJsonObject> MCPServerObject = MakeShared<FJsonObject>();
	MCPServerObject->SetStringField(
		TEXT("name"),
		AISettings->MCPServerName.TrimStartAndEnd().IsEmpty() ? FString(TEXT("bpt-unreal")) : AISettings->MCPServerName.TrimStartAndEnd());
	MCPServerObject->SetStringField(TEXT("command"), NodeCommand);
	MCPServerObject->SetStringField(TEXT("scriptPath"), MCPServerScriptPath);
	MCPServerObject->SetStringField(TEXT("projectRoot"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	RootObject->SetObjectField(TEXT("mcpServer"), MCPServerObject);

	const FString RequestDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EHB_MCP"), TEXT("CodexChat")));
	if (!IFileManager::Get().MakeDirectory(*RequestDir, true))
	{
		FailRequest(FText::Format(
			LOCTEXT("AILocalCodexRequestDirFailedReply", "无法创建本地 Codex 请求目录：{0}。"),
			FText::FromString(RequestDir)));
		return;
	}

	const FString RequestPath = FPaths::Combine(
		RequestDir,
		FString::Printf(TEXT("request-%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	const FString ProgressPath = FPaths::ChangeExtension(RequestPath, TEXT("progress.jsonl"));
	RootObject->SetStringField(TEXT("progressPath"), ProgressPath);

	FString RequestJson;
	TSharedRef<TJsonWriter<>> RequestWriter = TJsonWriterFactory<>::Create(&RequestJson);
	FJsonSerializer::Serialize(RootObject, RequestWriter);

	if (!FFileHelper::SaveStringToFile(RequestJson, *RequestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		FailRequest(FText::Format(
			LOCTEXT("AILocalCodexRequestWriteFailedReply", "无法写入本地 Codex 请求文件：{0}。"),
			FText::FromString(RequestPath)));
		return;
	}

	LocalCodexProgressPath = ProgressPath;
	LocalCodexProgressLineCount = 0;
	RegisterActiveTimer(
		0.25f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SEasyHouseBuilderPanel::HandleLocalCodexProgressTimer));

	const FString Params = FString::Printf(TEXT("\"%s\" --request \"%s\""), *BridgeScriptPath, *RequestPath);
	const FString WorkingDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	TWeakPtr<SEasyHouseBuilderPanel> WeakPanel = SharedThis(this);

	Async(EAsyncExecution::ThreadPool, [WeakPanel, NodeCommand, Params, WorkingDirectory]()
	{
		int32 ReturnCode = -1;
		FString Output;
		FString ErrorOutput;
		const bool bStarted = FPlatformProcess::ExecProcess(
			*NodeCommand,
			*Params,
			&ReturnCode,
			&Output,
			&ErrorOutput,
			*WorkingDirectory);

		AsyncTask(ENamedThreads::GameThread, [WeakPanel, Output, ErrorOutput, ReturnCode, bStarted]()
		{
			if (TSharedPtr<SEasyHouseBuilderPanel> Panel = WeakPanel.Pin())
			{
				Panel->HandleLocalCodexChatBridgeResult(Output, ErrorOutput, ReturnCode, bStarted);
			}
		});
	});
}

void SEasyHouseBuilderPanel::HandleLocalCodexChatBridgeResult(const FString& Output, const FString& ErrorOutput, int32 ReturnCode, bool bStarted)
{
	bAIRequestInFlight = false;
	PollLocalCodexProgress();
	LocalCodexProgressPath.Empty();
	LocalCodexProgressLineCount = 0;

	if (!bStarted)
	{
		AppendAIConversationMessage(LOCTEXT("AILocalCodexStartFailedReply", "本地 Codex 请求没有成功启动。请确认 Node 和 Codex 可执行文件路径正确。"), false);
		return;
	}

	TArray<FString> OutputLines;
	Output.ParseIntoArrayLines(OutputLines, true);

	FString JsonLine;
	for (int32 LineIndex = OutputLines.Num() - 1; LineIndex >= 0; --LineIndex)
	{
		const FString TrimmedLine = OutputLines[LineIndex].TrimStartAndEnd();
		if (TrimmedLine.StartsWith(TEXT("{")))
		{
			JsonLine = TrimmedLine;
			break;
		}
	}

	if (JsonLine.IsEmpty())
	{
		const FString ErrorSnippet = !ErrorOutput.TrimStartAndEnd().IsEmpty()
			? ErrorOutput.TrimStartAndEnd().Left(1000)
			: Output.TrimStartAndEnd().Left(1000);
		AppendAIConversationMessage(
			FText::Format(LOCTEXT("AILocalCodexNoJsonReply", "本地 Codex 没有返回可解析结果（进程返回码 {0}）：{1}"), FText::AsNumber(ReturnCode), FText::FromString(ErrorSnippet)),
			false);
		return;
	}

	TSharedPtr<FJsonObject> ResultObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
	if (!FJsonSerializer::Deserialize(Reader, ResultObject) || !ResultObject.IsValid())
	{
		AppendAIConversationMessage(
			FText::Format(LOCTEXT("AILocalCodexInvalidJsonReply", "本地 Codex 返回了无效 JSON：{0}"), FText::FromString(JsonLine.Left(1000))),
			false);
		return;
	}

	bool bOk = false;
	ResultObject->TryGetBoolField(TEXT("ok"), bOk);
	if (!bOk)
	{
		FString ErrorMessage;
		ResultObject->TryGetStringField(TEXT("error"), ErrorMessage);
		FString BridgeStderr;
		ResultObject->TryGetStringField(TEXT("stderr"), BridgeStderr);
		if (ErrorMessage.IsEmpty())
		{
			ErrorMessage = TEXT("本地 Codex 返回失败，但没有提供错误信息。");
		}
		if (!BridgeStderr.TrimStartAndEnd().IsEmpty())
		{
			ErrorMessage += FString::Printf(TEXT("\n%s"), *BridgeStderr.TrimStartAndEnd().Left(1000));
		}
		AppendAIConversationMessage(
			FText::Format(LOCTEXT("AILocalCodexFailedReply", "本地 Codex 请求失败：{0}"), FText::FromString(ErrorMessage)),
			false);
		return;
	}

	FString NewThreadId;
	if (ResultObject->TryGetStringField(TEXT("threadId"), NewThreadId) && !NewThreadId.TrimStartAndEnd().IsEmpty())
	{
		LocalCodexThreadId = NewThreadId.TrimStartAndEnd();
	}

	FString ReplyText;
	ResultObject->TryGetStringField(TEXT("content"), ReplyText);
	if (ReplyText.TrimStartAndEnd().IsEmpty())
	{
		ReplyText = TEXT("本地 Codex 返回成功，但没有文本内容。");
	}

	EmbeddedAIConversationHistory.Add(TPair<bool, FString>(false, ReplyText));
	AppendAIConversationMessage(FText::FromString(ReplyText), false);
}

EActiveTimerReturnType SEasyHouseBuilderPanel::HandleLocalCodexProgressTimer(double InCurrentTime, float InDeltaTime)
{
	PollLocalCodexProgress();
	return bAIRequestInFlight && !LocalCodexProgressPath.IsEmpty()
		? EActiveTimerReturnType::Continue
		: EActiveTimerReturnType::Stop;
}

void SEasyHouseBuilderPanel::PollLocalCodexProgress()
{
	if (LocalCodexProgressPath.IsEmpty() || !FPaths::FileExists(LocalCodexProgressPath))
	{
		return;
	}

	FString ProgressText;
	if (!FFileHelper::LoadFileToString(ProgressText, *LocalCodexProgressPath))
	{
		return;
	}

	TArray<FString> Lines;
	ProgressText.ParseIntoArrayLines(Lines, true);
	for (int32 LineIndex = LocalCodexProgressLineCount; LineIndex < Lines.Num(); ++LineIndex)
	{
		HandleLocalCodexProgressLine(Lines[LineIndex]);
	}
	LocalCodexProgressLineCount = Lines.Num();
}

void SEasyHouseBuilderPanel::HandleLocalCodexProgressLine(const FString& JsonLine)
{
	const FString TrimmedLine = JsonLine.TrimStartAndEnd();
	if (TrimmedLine.IsEmpty())
	{
		return;
	}

	TSharedPtr<FJsonObject> ProgressObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TrimmedLine);
	if (!FJsonSerializer::Deserialize(Reader, ProgressObject) || !ProgressObject.IsValid())
	{
		return;
	}

	FString Message;
	if (!ProgressObject->TryGetStringField(TEXT("message"), Message) || Message.TrimStartAndEnd().IsEmpty())
	{
		return;
	}

	FString Type;
	ProgressObject->TryGetStringField(TEXT("type"), Type);
	const FString Prefix = Type == TEXT("tool")
		? FString(TEXT("MCP："))
		: Type == TEXT("error")
			? FString(TEXT("错误："))
			: FString(TEXT("Codex："));
	UpdateAIResponseMessage(
		FText::FromString(Prefix + Message.TrimStartAndEnd()),
		EstimateAIResponseProgressPercent(Type, Message));
}

void SEasyHouseBuilderPanel::HandleAIChatResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	bAIRequestInFlight = false;

	if (!bWasSuccessful || !Response.IsValid())
	{
		AppendAIConversationMessage(LOCTEXT("AIRequestNetworkFailedReply", "AI 请求失败：没有收到有效响应。请检查网络、代理或 API 地址。"), false);
		return;
	}

	const int32 ResponseCode = Response->GetResponseCode();
	const FString ResponseContent = Response->GetContentAsString();
	if (ResponseCode < 200 || ResponseCode >= 300)
	{
		FText ErrorText = ExtractAIResponseText(ResponseContent);
		if (ErrorText.IsEmpty())
		{
			ErrorText = FText::FromString(ResponseContent.Left(1000));
		}
		AppendAIConversationMessage(
			FText::Format(LOCTEXT("AIRequestHTTPFailedReply", "AI 请求返回错误 HTTP {0}：{1}"), FText::AsNumber(ResponseCode), ErrorText),
			false);
		return;
	}

	const FText AIReply = ExtractAIResponseText(ResponseContent);
	const FString ReplyText = AIReply.IsEmpty()
		? FString::Printf(TEXT("AI 返回成功，但没有解析到文本内容。原始响应片段：%s"), *ResponseContent.Left(1000))
		: AIReply.ToString();
	EmbeddedAIConversationHistory.Add(TPair<bool, FString>(false, ReplyText));
	AppendAIConversationMessage(FText::FromString(ReplyText), false);
}

FString SEasyHouseBuilderPanel::BuildEmbeddedAIHiddenPrompt() const
{
	return FString::Printf(
		TEXT("%s\n\n")
		TEXT("Existing EHB_Building rule:\n")
		TEXT("- Reuse the current existing EHB_Building object. Do not create a new root building object for ordinary generation, continuation, regeneration, or modification.\n")
		TEXT("- Any JSON or BuildingPatch you draft must target the existing/current building only. Do not include an operation whose purpose is to create or replace the root building.\n")
		TEXT("- If the current building context says there is no active/existing building, stop and ask the user to select or manually create an EHB_Building in the editor first.\n\n")
		TEXT("UE 内置对话模式补充规则：\n")
		TEXT("- 你正在 UE 编辑器内部聊天窗口中回复用户，本窗口目前不能直接调用 MCP tool。\n")
		TEXT("- 不要声称已经调用 bpt_get_status、bpt_get_building_snapshot 或已经修改场景，除非用户把外部 MCP 工具返回结果粘贴给你。\n")
		TEXT("- 可以基于当前建筑上下文帮助用户设计方案、解释规则、生成可粘贴到 JSON 测试区的 EHB_AI_Workflow_v2，或生成适合外部 MCP 客户端执行的 BuildingPatch。\n")
		TEXT("- 如果用户希望你直接操作 UE 场景，请提示使用外部 MCP 客户端，或输出 JSON/BuildingPatch 让用户在本页测试。\n\n")
		TEXT("当前建筑上下文（隐藏）：\n%s"),
		*LoadAICreationSystemPrompt(),
		*BuildCurrentBuildingAIContext());
}

FString SEasyHouseBuilderPanel::BuildLocalCodexDeveloperInstructions() const
{
	return FString::Printf(
		TEXT("%s\n\n")
		TEXT("Existing EHB_Building rule:\n")
		TEXT("- For any generation or edit, call the status/active-building tools first and reuse the returned existing EHB_Building for every subsequent call.\n")
		TEXT("- Do not call a create-building tool, patch op, or fallback to spawn a new root building unless the user explicitly asks to make a separate new building object.\n")
		TEXT("- If no existing building is selected or resolvable, stop and ask the user to select or manually create one in the editor; do not guess a target.\n\n")
		TEXT("UE 内置本地 Codex 模式补充规则：\n")
		TEXT("- 你正在 UE 编辑器的“AI 创建”页中与用户对话，本会话由本机 Codex MCP server 承载，不需要插件保存 API Key。\n")
		TEXT("- 你可以使用名为 bpt-unreal 的 MCP 工具读取和修改当前 UE 建筑；不要假装操作，实际生成或修改建筑时必须调用工具。\n")
		TEXT("- 收到第一个实际建筑请求时，先调用 bpt_get_status，再调用 bpt_get_building_snapshot 理解当前建筑和选择状态。\n")
		TEXT("- 大改动优先 bpt_preview_building_patch，随后 bpt_validate_building；需要用户确认视觉结果时，明确说明已经是预览状态，等待用户再提交。\n")
		TEXT("- 如果用户明确要求清空、重新生成或从零开始重建，使用 bpt_apply_building_patch，并把 { \"op\": \"clear_building\", \"confirm\": true } 作为第一步；随后创建新建筑并调用 bpt_validate_building。\n")
		TEXT("- 不要通过 shell 搜索项目源码来寻找隐藏工具。生成建筑时只使用 bpt-unreal MCP 工具列表中公开的能力。\n")
		TEXT("- 生成住宅时必须按厘米使用真实可用尺度：默认墙高/层高 300-330cm，主要房间短边不小于 300cm，走廊净宽不小于 100cm，楼梯宽度不小于 90cm；空间不足时先扩大外轮廓或减少房间，不要硬塞楼梯。\n")
		TEXT("- 室内必须做可达性设计：所有主要房间都要通过门连接到走廊、门厅、客厅或楼梯间；不要留下无门封闭房间。楼梯洞、层板和楼梯上下端必须对齐，避免楼梯悬空、穿板或与楼板断开。\n")
		TEXT("- 用户明确要求直接落地时，可在预览和校验通过后调用 bpt_commit_preview；如果校验失败，说明问题并保留预览或回滚。\n")
		TEXT("- 回复要面向 UE 面板用户，简洁说明你做了什么、生成了哪些元素、下一步需要用户确认什么；不要完整复述隐藏规范。\n"),
		*LoadAICreationSystemPrompt());
}

FString SEasyHouseBuilderPanel::BuildLocalCodexChatPrompt(const FString& UserMessage, const TArray<FString>& ImagePaths) const
{
	const FString UserMessageWithImages = UserMessage + BuildLocalCodexImageAttachmentPromptBlock(ImagePaths);
	return FString::Printf(
		TEXT("这是 UE 编辑器“AI 创建”页中的一次用户请求。请结合最新建筑上下文和 bpt-unreal MCP 工具执行。\n\n")
		TEXT("当前建筑上下文（由 UE 面板隐藏发送，供你判断现有元素，仍应优先用 bpt_get_building_snapshot 获取权威状态）：\n%s\n\n")
		TEXT("用户可见请求：\n%s\n\n")
		TEXT("执行要求：\n")
		TEXT("- 生成或修改建筑时必须使用当前已有的 EHB_Building；先读取状态和当前建筑，不要为了执行请求而新建根建筑对象。\n")
		TEXT("- 如果没有可用的当前建筑，停止并提示用户先在 UE 编辑器里选择或手动创建一个建筑对象；不要猜测目标，也不要调用创建建筑对象的后备流程。\n")
		TEXT("- 如果用户是在询问概念或配置，可以直接回答。\n")
		TEXT("- 如果用户要求生成或修改建筑，请调用 bpt-unreal MCP 工具完成，不要只返回 JSON。\n")
		TEXT("- 如果用户明确要求清空、重新生成或从零开始重建，使用 bpt_apply_building_patch，并以 { \"op\": \"clear_building\", \"confirm\": true } 开始；不要为了寻找删除接口而搜索源码。\n")
		TEXT("- 生成建筑时保持元素关系完整：柱子点位先行，共用墙角复用柱子，墙只连接相邻柱子，闭合房间通过 generate_room_surfaces 生成房间地板和顶部层板。\n")
		TEXT("- 屋顶生成功能暂时关闭：不要生成屋顶，不要调用或虚构屋顶工具/操作。\n")
		TEXT("- 生成住宅时先检查空间尺度：主要房间短边至少 300cm，走廊净宽至少 100cm，墙高/层高通常 300-330cm；楼梯需要先预留楼梯间、洞口和上下落脚平台，房屋太小时应扩大轮廓或减少房间。\n")
		TEXT("- 室内设计必须有门和动线：房间分隔完成后，为卧室、厨房、卫生间、楼梯间等主要空间添加门；楼梯上端、下端、楼板和洞口必须连成一个可通行整体。\n")
		TEXT("- 回复中说明是否已预览、是否已校验、是否等待用户提交确认。\n"),
		*BuildCurrentBuildingAIContext(),
		*UserMessageWithImages);
}

FString SEasyHouseBuilderPanel::GetLocalCodexChatBridgeScriptPath() const
{
	FString PluginBaseDir;
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("EasyHouseBuilder")))
	{
		PluginBaseDir = Plugin->GetBaseDir();
	}
	else
	{
		PluginBaseDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("EasyHouseBuilder"));
	}

	return FPaths::ConvertRelativePathToFull(FPaths::Combine(PluginBaseDir, TEXT("MCP"), TEXT("bpt-codex-chat-bridge.mjs")));
}

FString SEasyHouseBuilderPanel::BuildEmbeddedAIRequestBody(const UEHBAISettings* AISettings, const TArray<FString>& ImagePaths) const
{
	const FString ModelName = AISettings ? AISettings->EmbeddedChatModel.TrimStartAndEnd() : FString(TEXT("gpt-5.5"));
	const FString HiddenPrompt = BuildEmbeddedAIHiddenPrompt();
	const int32 MaxOutputTokens = AISettings ? FMath::Max(128, AISettings->EmbeddedChatMaxOutputTokens) : 4096;
	TArray<FString> ImageDataUrls;
	ImageDataUrls.Reserve(ImagePaths.Num());
	for (const FString& ImagePath : ImagePaths)
	{
		FString DataUrl;
		FText Error;
		if (BuildAIImageDataUrl(ImagePath, DataUrl, Error))
		{
			ImageDataUrls.Add(DataUrl);
		}
	}

	if (AISettings && AISettings->EmbeddedChatRequestFormat == EEHBAIEmbeddedChatRequestFormat::ChatCompletions)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("model"), ModelName);
		Root->SetNumberField(TEXT("max_tokens"), MaxOutputTokens);

		auto MakeMessage = [&ImageDataUrls](const FString& Role, const FString& Text, bool bIncludeImages)
		{
			TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
			MessageObject->SetStringField(TEXT("role"), Role);
			if (bIncludeImages && ImageDataUrls.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> ContentArray;

				TSharedRef<FJsonObject> TextObject = MakeShared<FJsonObject>();
				TextObject->SetStringField(TEXT("type"), TEXT("text"));
				TextObject->SetStringField(TEXT("text"), Text);
				ContentArray.Add(MakeShared<FJsonValueObject>(TextObject));

				for (const FString& DataUrl : ImageDataUrls)
				{
					TSharedRef<FJsonObject> ImageUrlObject = MakeShared<FJsonObject>();
					ImageUrlObject->SetStringField(TEXT("url"), DataUrl);

					TSharedRef<FJsonObject> ImageObject = MakeShared<FJsonObject>();
					ImageObject->SetStringField(TEXT("type"), TEXT("image_url"));
					ImageObject->SetObjectField(TEXT("image_url"), ImageUrlObject);
					ContentArray.Add(MakeShared<FJsonValueObject>(ImageObject));
				}

				MessageObject->SetArrayField(TEXT("content"), ContentArray);
			}
			else
			{
				MessageObject->SetStringField(TEXT("content"), Text);
			}
			return MessageObject;
		};

		TArray<TSharedPtr<FJsonValue>> Messages;
		Messages.Add(MakeShared<FJsonValueObject>(MakeMessage(TEXT("system"), HiddenPrompt, false)));
		for (int32 MessageIndex = 0; MessageIndex < EmbeddedAIConversationHistory.Num(); ++MessageIndex)
		{
			const TPair<bool, FString>& HistoryMessage = EmbeddedAIConversationHistory[MessageIndex];
			const bool bIncludeImages = HistoryMessage.Key && MessageIndex == EmbeddedAIConversationHistory.Num() - 1;
			Messages.Add(MakeShared<FJsonValueObject>(MakeMessage(HistoryMessage.Key ? TEXT("user") : TEXT("assistant"), HistoryMessage.Value, bIncludeImages)));
		}
		Root->SetArrayField(TEXT("messages"), Messages);

		FString RequestBody;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
		FJsonSerializer::Serialize(Root, Writer);
		return RequestBody;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), ModelName);
	Root->SetBoolField(TEXT("stream"), false);
	Root->SetNumberField(TEXT("max_output_tokens"), MaxOutputTokens);
	Root->SetStringField(TEXT("instructions"), HiddenPrompt);

	auto MakeInputMessage = [&ImageDataUrls](const FString& Role, const FString& Text, bool bIncludeImages)
	{
		TSharedRef<FJsonObject> TextObject = MakeShared<FJsonObject>();
		TextObject->SetStringField(TEXT("type"), TEXT("input_text"));
		TextObject->SetStringField(TEXT("text"), Text);

		TArray<TSharedPtr<FJsonValue>> ContentArray;
		ContentArray.Add(MakeShared<FJsonValueObject>(TextObject));
		if (bIncludeImages && ImageDataUrls.Num() > 0)
		{
			for (const FString& DataUrl : ImageDataUrls)
			{
				TSharedRef<FJsonObject> ImageObject = MakeShared<FJsonObject>();
				ImageObject->SetStringField(TEXT("type"), TEXT("input_image"));
				ImageObject->SetStringField(TEXT("image_url"), DataUrl);
				ContentArray.Add(MakeShared<FJsonValueObject>(ImageObject));
			}
		}

		TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
		MessageObject->SetStringField(TEXT("role"), Role);
		MessageObject->SetArrayField(TEXT("content"), ContentArray);
		return MessageObject;
	};

	TArray<TSharedPtr<FJsonValue>> InputArray;
	for (int32 MessageIndex = 0; MessageIndex < EmbeddedAIConversationHistory.Num(); ++MessageIndex)
	{
		const TPair<bool, FString>& HistoryMessage = EmbeddedAIConversationHistory[MessageIndex];
		const bool bIncludeImages = HistoryMessage.Key && MessageIndex == EmbeddedAIConversationHistory.Num() - 1;
		InputArray.Add(MakeShared<FJsonValueObject>(MakeInputMessage(HistoryMessage.Key ? TEXT("user") : TEXT("assistant"), HistoryMessage.Value, bIncludeImages)));
	}
	Root->SetArrayField(TEXT("input"), InputArray);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(Root, Writer);
	return RequestBody;
}

FString SEasyHouseBuilderPanel::LoadAICreationSystemPrompt() const
{
	FString PromptPath;
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("EasyHouseBuilder")))
	{
		PromptPath = FPaths::Combine(Plugin->GetBaseDir(), EasyHouseBuilderPanel::AICreationSystemPromptRelativePath);
	}
	else
	{
		PromptPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("EasyHouseBuilder"), EasyHouseBuilderPanel::AICreationSystemPromptRelativePath);
	}

	FString Prompt;
	if (FFileHelper::LoadFileToString(Prompt, *PromptPath))
	{
		Prompt = Prompt.TrimStartAndEnd();
		if (!Prompt.IsEmpty())
		{
			return Prompt;
		}
	}

	Prompt = EasyHouseBuilderPanel::DefaultAICreationSystemPrompt;
	return Prompt;
}

FString SEasyHouseBuilderPanel::BuildCurrentBuildingAIContext() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("type"), TEXT("current_building_context"));
	Root->SetStringField(
		TEXT("instruction"),
		TEXT("这是当前建筑已有物体数据。生成 JSON 时应参考这些数据，避免重复创建相同位置的柱子、墙、地基和层板；如果用户要求扩建或修改，请在现有数据基础上延续。"));

	AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	if (!Building)
	{
		Root->SetBoolField(TEXT("hasActiveBuilding"), false);
		FString ContextText;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ContextText);
		FJsonSerializer::Serialize(Root, Writer);
		return ContextText;
	}

	auto MakeVectorValue = [](const FVector& Vector)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
		Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
		Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
		return MakeShared<FJsonValueArray>(Values);
	};

	auto MakePolygonValues = [&MakeVectorValue](const TArray<FVector>& Polygon, int32 MaxPoints)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		const int32 PointCount = FMath::Min(Polygon.Num(), MaxPoints);
		Values.Reserve(PointCount);
		for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			Values.Add(MakeVectorValue(Polygon[PointIndex]));
		}
		return Values;
	};

	Root->SetBoolField(TEXT("hasActiveBuilding"), true);
	Root->SetStringField(TEXT("buildingGuid"), Building->BuildingGuid.ToString(EGuidFormats::DigitsWithHyphens));
#if WITH_EDITOR
	Root->SetStringField(TEXT("buildingName"), Building->GetActorLabel());
#else
	Root->SetStringField(TEXT("buildingName"), Building->GetName());
#endif

	TArray<AActor*> AttachedActors;
	Building->GetAttachedActors(AttachedActors);

	TArray<TSharedPtr<FJsonValue>> PillarValues;
	TArray<TSharedPtr<FJsonValue>> WallValues;
	TArray<TSharedPtr<FJsonValue>> SlabValues;
	TArray<TSharedPtr<FJsonValue>> FloorValues;
	constexpr int32 MaxActorsPerType = 128;
	constexpr int32 MaxPolygonPoints = 32;

	for (AActor* Actor : AttachedActors)
	{
		if (!Actor)
		{
			continue;
		}

		if (const AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor))
		{
			if (PillarValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("guid"), Pillar->ElementGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetStringField(TEXT("name"), Pillar->ElementName.ToString());
			Object->SetNumberField(TEXT("floor"), Pillar->FloorIndex);
			Object->SetNumberField(TEXT("height"), Pillar->Height);
			Object->SetNumberField(TEXT("width"), Pillar->Width);
			Object->SetNumberField(TEXT("depth"), Pillar->Depth);
			Object->SetArrayField(TEXT("location"), MakeVectorValue(Pillar->GetElementLocalTransform().GetLocation())->AsArray());
			PillarValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor))
		{
			if (WallValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("guid"), Wall->ElementGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetStringField(TEXT("name"), Wall->ElementName.ToString());
			Object->SetNumberField(TEXT("floor"), Wall->FloorIndex);
			Object->SetNumberField(TEXT("height"), Wall->Height);
			Object->SetNumberField(TEXT("thickness"), Wall->Thickness);
			Object->SetStringField(TEXT("startPillarGuid"), Wall->StartPillarGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetStringField(TEXT("endPillarGuid"), Wall->EndPillarGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetArrayField(TEXT("localStart"), MakeVectorValue(Wall->LocalStart)->AsArray());
			Object->SetArrayField(TEXT("localEnd"), MakeVectorValue(Wall->LocalEnd)->AsArray());
			Object->SetNumberField(TEXT("curveControlOffset"), Wall->CurveControlOffset);
			Object->SetNumberField(TEXT("curveSegmentLength"), Wall->CurveSegmentLength);
			WallValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHB_FloorSlab* Slab = Cast<AEHB_FloorSlab>(Actor))
		{
			if (SlabValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("guid"), Slab->ElementGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetStringField(TEXT("name"), Slab->ElementName.ToString());
			Object->SetBoolField(TEXT("isFoundation"), Slab->bIsFoundation);
			Object->SetNumberField(TEXT("floor"), Slab->FloorIndex);
			Object->SetNumberField(TEXT("thickness"), Slab->Thickness);
			Object->SetNumberField(TEXT("visualExpansion"), Slab->VisualExpansion);
			Object->SetNumberField(TEXT("topZ"), Slab->GetElementLocalTransform().GetLocation().Z);
			Object->SetArrayField(TEXT("polygon"), MakePolygonValues(Slab->LocalTopPolygon, MaxPolygonPoints));
			SlabValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHB_Floor* Floor = Cast<AEHB_Floor>(Actor))
		{
			if (FloorValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("guid"), Floor->ElementGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetStringField(TEXT("name"), Floor->ElementName.ToString());
			Object->SetNumberField(TEXT("floor"), Floor->FloorIndex);
			Object->SetArrayField(TEXT("polygon"), MakePolygonValues(Floor->LocalFloorPolygon, MaxPolygonPoints));
			FloorValues.Add(MakeShared<FJsonValueObject>(Object));
		}
	}

	Root->SetArrayField(TEXT("pillars"), MoveTemp(PillarValues));
	Root->SetArrayField(TEXT("walls"), MoveTemp(WallValues));
	Root->SetArrayField(TEXT("slabs"), MoveTemp(SlabValues));
	Root->SetArrayField(TEXT("floors"), MoveTemp(FloorValues));

	FString ContextText;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ContextText);
	FJsonSerializer::Serialize(Root, Writer);
	return ContextText;
}

FString SEasyHouseBuilderPanel::BuildDetailedCurrentBuildingAIContext() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("type"), TEXT("current_building_context"));
	Root->SetStringField(TEXT("schema"), TEXT("EHB_CurrentBuildingContext_v1"));
	Root->SetStringField(TEXT("purpose"), TEXT("copy_current_building_data_for_ai_continuation"));
	Root->SetStringField(
		TEXT("instruction"),
		TEXT("这是当前建筑对象的已有元素快照，不是要直接执行的生成 JSON。AI 应读取这些 pillars/walls/slabs/floors/doorWindows/roofs/stairs/railings/closedLoops/relations 数据，在现有建筑基础上续写或扩建，并继续返回 EHB_AI_Workflow_v2。不要重复创建同一位置的已有柱、墙、地基、楼板或房间；需要连接已有建筑时，应让新增点位与现有墙/柱端点坐标精确对齐。"));
	Root->SetStringField(
		TEXT("coordinateSystem"),
		TEXT("所有 local 坐标均为建筑对象本地坐标，单位为厘米，Z 轴向上；floor 0 为地基，floor 1 起为普通楼层。"));

	AEHBBuildingActorBase* Building = ActiveBuilding.Get();
	if (!Building)
	{
		Root->SetBoolField(TEXT("hasActiveBuilding"), false);
		FString ContextText;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&ContextText);
		FJsonSerializer::Serialize(Root, Writer);
		return ContextText;
	}

	Building->RebuildElementAndRelationshipIndexes();
	Building->RebuildClosedLoops();

	constexpr int32 MaxPolygonPoints = 512;
	constexpr int32 MaxActorsPerType = 4096;

	auto MakeVectorValues = [](const FVector& Vector)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
		Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
		Values.Add(MakeShared<FJsonValueNumber>(Vector.Z));
		return Values;
	};

	auto MakeVector2DValues = [](const FVector2D& Vector)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueNumber>(Vector.X));
		Values.Add(MakeShared<FJsonValueNumber>(Vector.Y));
		return Values;
	};

	auto MakeRotatorValues = [](const FRotator& Rotator)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Add(MakeShared<FJsonValueNumber>(Rotator.Roll));
		Values.Add(MakeShared<FJsonValueNumber>(Rotator.Pitch));
		Values.Add(MakeShared<FJsonValueNumber>(Rotator.Yaw));
		return Values;
	};

	auto MakePolygonValues = [&MakeVectorValues](const TArray<FVector>& Polygon)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		const int32 PointCount = FMath::Min(Polygon.Num(), MaxPolygonPoints);
		Values.Reserve(PointCount);
		for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			Values.Add(MakeShared<FJsonValueArray>(MakeVectorValues(Polygon[PointIndex])));
		}
		return Values;
	};

	auto MakeGuidValues = [](const TArray<FGuid>& Guids)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(Guids.Num());
		for (const FGuid& Guid : Guids)
		{
			Values.Add(MakeShared<FJsonValueString>(Guid.ToString(EGuidFormats::DigitsWithHyphens)));
		}
		return Values;
	};

	auto MakeNameValues = [](const TArray<FName>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(Names.Num());
		for (const FName& Name : Names)
		{
			Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
		}
		return Values;
	};

	auto MakeEnumString = [](const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetNameStringByValue(Value) : FString::FromInt(static_cast<int32>(Value));
	};

	auto MakeTransformObject = [&MakeVectorValues, &MakeRotatorValues](const FTransform& Transform)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetArrayField(TEXT("location"), MakeVectorValues(Transform.GetLocation()));
		Object->SetArrayField(TEXT("rotationRollPitchYaw"), MakeRotatorValues(Transform.Rotator()));
		Object->SetArrayField(TEXT("scale"), MakeVectorValues(Transform.GetScale3D()));
		return Object;
	};

	auto MakeBoundsObject = [&MakeVectorValues](const FBox& Bounds)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetBoolField(TEXT("isValid"), Bounds.IsValid != 0);
		if (Bounds.IsValid)
		{
			Object->SetArrayField(TEXT("min"), MakeVectorValues(Bounds.Min));
			Object->SetArrayField(TEXT("max"), MakeVectorValues(Bounds.Max));
		}
		return Object;
	};

	auto MakeBaseElementObject = [
		&MakeBoundsObject,
		&MakeEnumString,
		&MakeNameValues,
		&MakeTransformObject](const AEHBElementActorBase* Element)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("guid"), Element->ElementGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("name"), Element->ElementName.ToString());
		Object->SetStringField(TEXT("actorName"), Element->GetName());
#if WITH_EDITOR
		Object->SetStringField(TEXT("actorLabel"), Element->GetActorLabel());
#endif
		Object->SetStringField(TEXT("class"), Element->GetClass()->GetName());
		Object->SetStringField(TEXT("elementType"), MakeEnumString(StaticEnum<EEHBBuildingElementType>(), static_cast<int64>(Element->ElementType)));
		Object->SetNumberField(TEXT("floor"), Element->FloorIndex);
		Object->SetStringField(TEXT("floorRole"), MakeEnumString(StaticEnum<EEHBBuildingFloorElementRole>(), static_cast<int64>(Element->FloorRole)));
		Object->SetObjectField(TEXT("localTransform"), MakeTransformObject(Element->GetElementLocalTransform()));
		Object->SetObjectField(TEXT("buildingLocalBounds"), MakeBoundsObject(Element->GetBuildingLocalBounds()));
		Object->SetArrayField(TEXT("semanticTags"), MakeNameValues(Element->SemanticTags));
		return Object;
	};

	auto MakeRelationEndpointObject = [&MakeEnumString](const FEHBElementRelationEndpoint& Endpoint)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("kind"), MakeEnumString(StaticEnum<EEHBRelationEndpointKind>(), static_cast<int64>(Endpoint.Kind)));
		Object->SetStringField(TEXT("elementGuid"), Endpoint.ElementGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Object->SetStringField(TEXT("surfaceKind"), MakeEnumString(StaticEnum<EEHBElementSurfaceKind>(), static_cast<int64>(Endpoint.SurfaceKind)));
		Object->SetStringField(TEXT("surfaceName"), Endpoint.SurfaceName.ToString());
		Object->SetNumberField(TEXT("subIndex"), Endpoint.SubIndex);
		if (!Endpoint.ExternalActor.IsNull())
		{
			Object->SetStringField(TEXT("externalActor"), Endpoint.ExternalActor.ToSoftObjectPath().ToString());
		}
		return Object;
	};

	auto MakeSlabHoleValues = [&MakePolygonValues](const TArray<FEHBFloorSlabHole>& Holes)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(Holes.Num());
		for (const FEHBFloorSlabHole& Hole : Holes)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetArrayField(TEXT("polygon"), MakePolygonValues(Hole.LocalPolygon));
			Values.Add(MakeShared<FJsonValueObject>(Object));
		}
		return Values;
	};

	auto MakeFloorFinishHoleValues = [&MakePolygonValues](const TArray<FEHBFloorFinishHole>& Holes)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(Holes.Num());
		for (const FEHBFloorFinishHole& Hole : Holes)
		{
			TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetArrayField(TEXT("polygon"), MakePolygonValues(Hole.LocalPolygon));
			Values.Add(MakeShared<FJsonValueObject>(Object));
		}
		return Values;
	};

	Root->SetBoolField(TEXT("hasActiveBuilding"), true);
	Root->SetStringField(TEXT("buildingGuid"), Building->BuildingGuid.ToString(EGuidFormats::DigitsWithHyphens));
#if WITH_EDITOR
	Root->SetStringField(TEXT("buildingName"), Building->GetActorLabel());
#else
	Root->SetStringField(TEXT("buildingName"), Building->GetName());
#endif
	Root->SetObjectField(TEXT("buildingWorldTransform"), MakeTransformObject(Building->GetActorTransform()));
	Root->SetNumberField(TEXT("relationshipGraphRevision"), Building->RelationshipGraphRevision);
	Root->SetNumberField(TEXT("relationshipSchemaVersion"), Building->RelationshipSchemaVersion);

	TArray<AActor*> AttachedActors;
	Building->GetAttachedActors(AttachedActors);

	TArray<TSharedPtr<FJsonValue>> PillarValues;
	TArray<TSharedPtr<FJsonValue>> WallValues;
	TArray<TSharedPtr<FJsonValue>> SlabValues;
	TArray<TSharedPtr<FJsonValue>> FloorValues;
	TArray<TSharedPtr<FJsonValue>> DoorWindowValues;
	TArray<TSharedPtr<FJsonValue>> RoofValues;
	TArray<TSharedPtr<FJsonValue>> StairValues;
	TArray<TSharedPtr<FJsonValue>> RailingValues;
	TArray<TSharedPtr<FJsonValue>> OtherElementValues;

	for (AActor* Actor : AttachedActors)
	{
		if (!Actor)
		{
			continue;
		}

		if (const AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor))
		{
			if (PillarValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeBaseElementObject(Pillar);
			Object->SetStringField(TEXT("shapeType"), MakeEnumString(StaticEnum<EEHBPillarShapeType>(), static_cast<int64>(Pillar->ShapeType)));
			Object->SetNumberField(TEXT("height"), Pillar->Height);
			Object->SetNumberField(TEXT("width"), Pillar->Width);
			Object->SetNumberField(TEXT("depth"), Pillar->Depth);
			Object->SetNumberField(TEXT("radius"), Pillar->Radius);
			Object->SetNumberField(TEXT("cylinderSideCount"), Pillar->CylinderSideCount);
			Object->SetArrayField(TEXT("location"), MakeVectorValues(Pillar->GetElementLocalTransform().GetLocation()));
			Object->SetArrayField(TEXT("connectedWallGuids"), MakeGuidValues(Pillar->ConnectedWallGuids));
			Object->SetArrayField(TEXT("connectedPillarGuids"), MakeGuidValues(Pillar->ConnectedPillarGuids));
			Object->SetArrayField(TEXT("footprint"), MakePolygonValues(Pillar->PolygonPillarFootprint));
			if (!Pillar->OverrideMaterial.IsNull())
			{
				Object->SetStringField(TEXT("overrideMaterial"), Pillar->OverrideMaterial.ToSoftObjectPath().ToString());
			}
			PillarValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor))
		{
			if (WallValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeBaseElementObject(Wall);
			Object->SetNumberField(TEXT("height"), Wall->Height);
			Object->SetNumberField(TEXT("thickness"), Wall->Thickness);
			Object->SetStringField(TEXT("startPillarGuid"), Wall->StartPillarGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetStringField(TEXT("endPillarGuid"), Wall->EndPillarGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetArrayField(TEXT("localStart"), MakeVectorValues(Wall->LocalStart));
			Object->SetArrayField(TEXT("localEnd"), MakeVectorValues(Wall->LocalEnd));
			Object->SetNumberField(TEXT("curveControlOffset"), Wall->CurveControlOffset);
			Object->SetNumberField(TEXT("curveSegmentLength"), Wall->CurveSegmentLength);
			Object->SetNumberField(TEXT("doorWindowConnectionCount"), Wall->DoorWindowConnections.Num());

			TArray<TSharedPtr<FJsonValue>> DoorWindowConnectionValues;
			for (const FEHBWallDoorWindowConnection& Connection : Wall->DoorWindowConnections)
			{
				TSharedRef<FJsonObject> ConnectionObject = MakeShared<FJsonObject>();
				ConnectionObject->SetStringField(TEXT("doorWindowGuid"), Connection.DoorWindowGuid.ToString(EGuidFormats::DigitsWithHyphens));
				ConnectionObject->SetStringField(TEXT("kind"), MakeEnumString(StaticEnum<EEHBDoorWindowElementKind>(), static_cast<int64>(Connection.Kind)));
				ConnectionObject->SetNumberField(TEXT("distanceFromStart"), Connection.DistanceFromStart);
				ConnectionObject->SetNumberField(TEXT("bottomHeight"), Connection.BottomHeight);
				ConnectionObject->SetNumberField(TEXT("openingWidth"), Connection.OpeningWidth);
				ConnectionObject->SetNumberField(TEXT("openingHeight"), Connection.OpeningHeight);
				ConnectionObject->SetNumberField(TEXT("openingThickness"), Connection.OpeningThickness);
				ConnectionObject->SetObjectField(TEXT("doorWindowLocalToWall"), MakeTransformObject(Connection.DoorWindowLocalToWall));
				ConnectionObject->SetArrayField(TEXT("localOutlinePoints"), MakePolygonValues(Connection.LocalOutlinePoints));
				DoorWindowConnectionValues.Add(MakeShared<FJsonValueObject>(ConnectionObject));
			}
			Object->SetArrayField(TEXT("doorWindowConnections"), MoveTemp(DoorWindowConnectionValues));
			WallValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHB_FloorSlab* Slab = Cast<AEHB_FloorSlab>(Actor))
		{
			if (SlabValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeBaseElementObject(Slab);
			Object->SetBoolField(TEXT("isFoundation"), Slab->bIsFoundation);
			Object->SetNumberField(TEXT("thickness"), Slab->Thickness);
			Object->SetNumberField(TEXT("offset"), Slab->Offset);
			Object->SetNumberField(TEXT("visualExpansion"), Slab->VisualExpansion);
			Object->SetBoolField(TEXT("keepFoundationBottomOnGround"), Slab->bKeepFoundationBottomOnGround);
			Object->SetNumberField(TEXT("topZ"), Slab->GetElementLocalTransform().GetLocation().Z);
			Object->SetArrayField(TEXT("polygon"), MakePolygonValues(Slab->LocalTopPolygon));
			Object->SetBoolField(TEXT("hasAIFoundationSource"), Slab->bHasAIFoundationSource);
			Object->SetNumberField(TEXT("aiFoundationExpansion"), Slab->AIFoundationExpansion);
			Object->SetArrayField(TEXT("aiDesignTopPolygon"), MakePolygonValues(Slab->AIDesignTopPolygon));
			Object->SetArrayField(TEXT("holes"), MakeSlabHoleValues(Slab->LocalHoles));
			Object->SetBoolField(TEXT("hasRoomFillAnchor"), Slab->bHasRoomFillAnchor);
			Object->SetStringField(TEXT("roomFillAnchorWallGuid"), Slab->RoomFillAnchorWallGuid.ToString(EGuidFormats::DigitsWithHyphens));
			if (!Slab->SurfaceMaterial.IsNull())
			{
				Object->SetStringField(TEXT("surfaceMaterial"), Slab->SurfaceMaterial.ToSoftObjectPath().ToString());
			}
			SlabValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHB_Floor* Floor = Cast<AEHB_Floor>(Actor))
		{
			if (FloorValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeBaseElementObject(Floor);
			Object->SetArrayField(TEXT("polygon"), MakePolygonValues(Floor->LocalFloorPolygon));
			Object->SetNumberField(TEXT("visualOffset"), Floor->VisualOffset);
			Object->SetStringField(TEXT("roomLoopGuid"), Floor->RoomLoopGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetNumberField(TEXT("roomFloorIndex"), Floor->RoomFloorIndex);

			TArray<TSharedPtr<FJsonValue>> RegionValues;
			for (const FEHBFloorFinishRegion& Region : Floor->FloorRegions)
			{
				TSharedRef<FJsonObject> RegionObject = MakeShared<FJsonObject>();
				RegionObject->SetArrayField(TEXT("outerPolygon"), MakePolygonValues(Region.OuterPolygon));
				RegionObject->SetArrayField(TEXT("holes"), MakeFloorFinishHoleValues(Region.Holes));
				RegionValues.Add(MakeShared<FJsonValueObject>(RegionObject));
			}
			Object->SetArrayField(TEXT("regions"), MoveTemp(RegionValues));
			Object->SetArrayField(TEXT("surfaceFinishRelationGuids"), MakeGuidValues(Floor->SurfaceFinishRelationGuids));
			FloorValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHB_DoorWindow* DoorWindow = Cast<AEHB_DoorWindow>(Actor))
		{
			if (DoorWindowValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeBaseElementObject(DoorWindow);
			Object->SetStringField(TEXT("kind"), MakeEnumString(StaticEnum<EEHBDoorWindowElementKind>(), static_cast<int64>(DoorWindow->Kind)));
			Object->SetNumberField(TEXT("sillHeight"), DoorWindow->SillHeight);
			Object->SetNumberField(TEXT("openingWidth"), DoorWindow->OpeningWidth);
			Object->SetNumberField(TEXT("openingHeight"), DoorWindow->OpeningHeight);
			Object->SetNumberField(TEXT("openingThickness"), DoorWindow->OpeningThickness);
			Object->SetStringField(TEXT("owningWallGuid"), DoorWindow->OwningWallGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetNumberField(TEXT("distanceFromWallStart"), DoorWindow->DistanceFromWallStart);
			Object->SetArrayField(TEXT("sourceBoundsMin"), MakeVectorValues(DoorWindow->SourceBoundsMin));
			Object->SetArrayField(TEXT("sourceBoundsMax"), MakeVectorValues(DoorWindow->SourceBoundsMax));
			Object->SetArrayField(TEXT("openingOutlineLocalPoints"), MakePolygonValues(DoorWindow->GetOpeningOutlineLocalPoints()));
			DoorWindowValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHBGableRoof* Roof = Cast<AEHBGableRoof>(Actor))
		{
			if (RoofValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeBaseElementObject(Roof);
			Object->SetStringField(TEXT("roofType"), TEXT("Gable"));
			Object->SetStringField(TEXT("axisMode"), MakeEnumString(StaticEnum<EEHBRoofAxisMode>(), static_cast<int64>(Roof->AxisMode)));
			Object->SetNumberField(TEXT("length"), Roof->Length);
			Object->SetNumberField(TEXT("width"), Roof->Width);
			Object->SetNumberField(TEXT("pitchDegrees"), Roof->PitchDegrees);
			Object->SetNumberField(TEXT("thickness"), Roof->Thickness);
			Object->SetNumberField(TEXT("eaveOffset"), Roof->EaveOffset);
			Object->SetNumberField(TEXT("ridgeOffsetRatio"), Roof->RidgeOffsetRatio);
			Object->SetBoolField(TEXT("generateRidge"), Roof->bGenerateRidge);
			Object->SetBoolField(TEXT("generateEaves"), Roof->bGenerateEaves);
			Object->SetBoolField(TEXT("generateGableRakes"), Roof->bGenerateGableRakes);
			Object->SetBoolField(TEXT("generateGableEndWalls"), Roof->bGenerateGableEndWalls);
			Object->SetNumberField(TEXT("gableEndWallBoundaryInset"), Roof->GableEndWallBoundaryInset);
			Object->SetBoolField(TEXT("cutCollidingElements"), Roof->bCutCollidingElements);
			Object->SetBoolField(TEXT("removeDisconnectedCutPieces"), Roof->bRemoveDisconnectedCutPieces);
			Object->SetBoolField(TEXT("keepCutAwayDisconnectedPieces"), Roof->bKeepCutAwayDisconnectedPieces);
			RoofValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHB_Stair* Stair = Cast<AEHB_Stair>(Actor))
		{
			if (StairValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			const FEHBStairData& StairData = Stair->StairData;
			TSharedRef<FJsonObject> Object = MakeBaseElementObject(Stair);
			Object->SetBoolField(TEXT("useActualDimensions"), StairData.bUseActualDimensions);
			Object->SetNumberField(TEXT("treadDepth"), StairData.TreadDepth);
			Object->SetNumberField(TEXT("stairWidth"), StairData.StairWidth);
			Object->SetNumberField(TEXT("stairHeight"), StairData.StairHeight);
			Object->SetNumberField(TEXT("generatedStepCount"), Stair->GeneratedStepCount);
			Object->SetNumberField(TEXT("generatedStepHeight"), Stair->GeneratedStepHeight);
			Object->SetBoolField(TEXT("generateTreads"), StairData.bGenerateTreads);
			Object->SetBoolField(TEXT("fillRisers"), StairData.bFillRisers);
			Object->SetBoolField(TEXT("fillBottomPart"), StairData.bFillBottomPart);
			Object->SetBoolField(TEXT("generateSides"), StairData.bGenerateSides);
			Object->SetBoolField(TEXT("generateSideGuards"), StairData.bGenerateSideGuards);
			Object->SetArrayField(TEXT("bottomStepLocation"), MakeVector2DValues(StairData.BottomStepLocation));
			Object->SetNumberField(TEXT("bottomStepYawOffset"), StairData.BottomStepYawOffset);

			TArray<TSharedPtr<FJsonValue>> ControlOffsetValues;
			for (const FVector2D& Offset : StairData.IntermediateControlOffsets)
			{
				ControlOffsetValues.Add(MakeShared<FJsonValueArray>(MakeVector2DValues(Offset)));
			}
			Object->SetArrayField(TEXT("intermediateControlOffsets"), MoveTemp(ControlOffsetValues));
			StairValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHB_Railing* Railing = Cast<AEHB_Railing>(Actor))
		{
			if (RailingValues.Num() >= MaxActorsPerType)
			{
				continue;
			}

			TSharedRef<FJsonObject> Object = MakeBaseElementObject(Railing);
			Object->SetStringField(TEXT("pathMode"), MakeEnumString(StaticEnum<EEHBRailingPathMode>(), static_cast<int64>(Railing->PathMode)));
			Object->SetStringField(TEXT("railingSide"), MakeEnumString(StaticEnum<EEHBRailingSide>(), static_cast<int64>(Railing->RailingSide)));
			Object->SetStringField(TEXT("hostedStairGuid"), Railing->HostedStairGuid.ToString(EGuidFormats::DigitsWithHyphens));
			Object->SetArrayField(TEXT("linearStart"), MakeVectorValues(Railing->LinearStart));
			Object->SetArrayField(TEXT("linearEnd"), MakeVectorValues(Railing->LinearEnd));
			Object->SetStringField(TEXT("postSpacingMode"), MakeEnumString(StaticEnum<EEHBRailingPostSpacingMode>(), static_cast<int64>(Railing->PostSpacingMode)));
			Object->SetNumberField(TEXT("postSpacing"), Railing->PostSpacing);
			Object->SetNumberField(TEXT("stepsPerPost"), Railing->StepsPerPost);
			Object->SetNumberField(TEXT("postWidth"), Railing->PostWidth);
			Object->SetNumberField(TEXT("postHeight"), Railing->PostHeight);
			Object->SetNumberField(TEXT("railHeight"), Railing->RailHeight);
			Object->SetNumberField(TEXT("railThickness"), Railing->RailThickness);
			Object->SetStringField(TEXT("fillMode"), MakeEnumString(StaticEnum<EEHBRailingFillMode>(), static_cast<int64>(Railing->FillMode)));
			Object->SetNumberField(TEXT("generatedPostCount"), Railing->GeneratedPosts.Num());
			Object->SetArrayField(TEXT("railingRelationGuids"), MakeGuidValues(Railing->RailingRelationGuids));
			RailingValues.Add(MakeShared<FJsonValueObject>(Object));
			continue;
		}

		if (const AEHBElementActorBase* Element = Cast<AEHBElementActorBase>(Actor))
		{
			if (OtherElementValues.Num() < MaxActorsPerType)
			{
				OtherElementValues.Add(MakeShared<FJsonValueObject>(MakeBaseElementObject(Element)));
			}
		}
	}

	TSharedRef<FJsonObject> SummaryObject = MakeShared<FJsonObject>();
	SummaryObject->SetNumberField(TEXT("attachedActorCount"), AttachedActors.Num());
	SummaryObject->SetNumberField(TEXT("pillarCount"), PillarValues.Num());
	SummaryObject->SetNumberField(TEXT("wallCount"), WallValues.Num());
	SummaryObject->SetNumberField(TEXT("slabCount"), SlabValues.Num());
	SummaryObject->SetNumberField(TEXT("floorFinishCount"), FloorValues.Num());
	SummaryObject->SetNumberField(TEXT("doorWindowCount"), DoorWindowValues.Num());
	SummaryObject->SetNumberField(TEXT("roofCount"), RoofValues.Num());
	SummaryObject->SetNumberField(TEXT("stairCount"), StairValues.Num());
	SummaryObject->SetNumberField(TEXT("railingCount"), RailingValues.Num());
	SummaryObject->SetNumberField(TEXT("closedLoopCount"), Building->ClosedLoops.Num());
	SummaryObject->SetNumberField(TEXT("relationCount"), Building->ElementRelations.Num());
	Root->SetObjectField(TEXT("summary"), SummaryObject);

	Root->SetArrayField(TEXT("pillars"), MoveTemp(PillarValues));
	Root->SetArrayField(TEXT("walls"), MoveTemp(WallValues));
	Root->SetArrayField(TEXT("slabs"), MoveTemp(SlabValues));
	Root->SetArrayField(TEXT("floors"), MoveTemp(FloorValues));
	Root->SetArrayField(TEXT("doorWindows"), MoveTemp(DoorWindowValues));
	Root->SetArrayField(TEXT("roofs"), MoveTemp(RoofValues));
	Root->SetArrayField(TEXT("stairs"), MoveTemp(StairValues));
	Root->SetArrayField(TEXT("railings"), MoveTemp(RailingValues));
	Root->SetArrayField(TEXT("otherElements"), MoveTemp(OtherElementValues));

	TArray<TSharedPtr<FJsonValue>> FloorIndexValues;
	for (const TPair<int32, FEHBBuildingFloorElementList>& FloorPair : Building->FloorElementsByIndex)
	{
		TSharedRef<FJsonObject> FloorIndexObject = MakeShared<FJsonObject>();
		FloorIndexObject->SetNumberField(TEXT("floor"), FloorPair.Key);
		TArray<TSharedPtr<FJsonValue>> EntryValues;
		for (const FEHBBuildingFloorElementEntry& Entry : FloorPair.Value.Elements)
		{
			TSharedRef<FJsonObject> EntryObject = MakeShared<FJsonObject>();
			EntryObject->SetStringField(TEXT("elementGuid"), Entry.ElementGuid.ToString(EGuidFormats::DigitsWithHyphens));
			EntryObject->SetStringField(TEXT("elementType"), MakeEnumString(StaticEnum<EEHBBuildingElementType>(), static_cast<int64>(Entry.ElementType)));
			EntryObject->SetNumberField(TEXT("floor"), Entry.FloorIndex);
			EntryObject->SetStringField(TEXT("floorRole"), MakeEnumString(StaticEnum<EEHBBuildingFloorElementRole>(), static_cast<int64>(Entry.FloorRole)));
			EntryValues.Add(MakeShared<FJsonValueObject>(EntryObject));
		}
		FloorIndexObject->SetArrayField(TEXT("elements"), MoveTemp(EntryValues));
		FloorIndexValues.Add(MakeShared<FJsonValueObject>(FloorIndexObject));
	}
	Root->SetArrayField(TEXT("floorElementIndex"), MoveTemp(FloorIndexValues));

	TArray<TSharedPtr<FJsonValue>> WallConnectionValues;
	for (const TPair<FGuid, FEHBBuildingWallConnection>& WallConnectionPair : Building->WallConnectionsByWallGuid)
	{
		TSharedRef<FJsonObject> ConnectionObject = MakeShared<FJsonObject>();
		ConnectionObject->SetStringField(TEXT("wallGuid"), WallConnectionPair.Value.WallGuid.ToString(EGuidFormats::DigitsWithHyphens));
		ConnectionObject->SetStringField(TEXT("startPillarGuid"), WallConnectionPair.Value.StartPillarGuid.ToString(EGuidFormats::DigitsWithHyphens));
		ConnectionObject->SetStringField(TEXT("endPillarGuid"), WallConnectionPair.Value.EndPillarGuid.ToString(EGuidFormats::DigitsWithHyphens));
		WallConnectionValues.Add(MakeShared<FJsonValueObject>(ConnectionObject));
	}
	Root->SetArrayField(TEXT("wallConnections"), MoveTemp(WallConnectionValues));

	TArray<TSharedPtr<FJsonValue>> ClosedLoopValues;
	for (const FEHBBuildingClosedLoop& Loop : Building->ClosedLoops)
	{
		TSharedRef<FJsonObject> LoopObject = MakeShared<FJsonObject>();
		LoopObject->SetStringField(TEXT("loopGuid"), Loop.LoopGuid.ToString(EGuidFormats::DigitsWithHyphens));
		LoopObject->SetNumberField(TEXT("floor"), Loop.FloorIndex);
		LoopObject->SetArrayField(TEXT("pillarGuids"), MakeGuidValues(Loop.PillarGuids));
		LoopObject->SetArrayField(TEXT("wallGuids"), MakeGuidValues(Loop.WallGuids));
		LoopObject->SetBoolField(TEXT("clockwise"), Loop.bClockwise);
		LoopObject->SetNumberField(TEXT("area"), Loop.Area);
		ClosedLoopValues.Add(MakeShared<FJsonValueObject>(LoopObject));
	}
	Root->SetArrayField(TEXT("closedLoops"), MoveTemp(ClosedLoopValues));

	TArray<TSharedPtr<FJsonValue>> RelationValues;
	for (const FEHBElementRelation& Relation : Building->ElementRelations)
	{
		TSharedRef<FJsonObject> RelationObject = MakeShared<FJsonObject>();
		RelationObject->SetStringField(TEXT("relationGuid"), Relation.RelationGuid.ToString(EGuidFormats::DigitsWithHyphens));
		RelationObject->SetStringField(TEXT("type"), MakeEnumString(StaticEnum<EEHBElementRelationType>(), static_cast<int64>(Relation.Type)));
		RelationObject->SetObjectField(TEXT("source"), MakeRelationEndpointObject(Relation.Source));
		RelationObject->SetObjectField(TEXT("target"), MakeRelationEndpointObject(Relation.Target));
		RelationObject->SetStringField(TEXT("origin"), MakeEnumString(StaticEnum<EEHBRelationOrigin>(), static_cast<int64>(Relation.Origin)));
		RelationObject->SetBoolField(TEXT("geometryDependent"), Relation.bGeometryDependent);
		RelationObject->SetBoolField(TEXT("affectsFloorAssignment"), Relation.bAffectsFloorAssignment);
		RelationObject->SetBoolField(TEXT("enabled"), Relation.bEnabled);
		RelationObject->SetArrayField(TEXT("contactPoint"), MakeVectorValues(Relation.ContactPoint));
		RelationObject->SetArrayField(TEXT("contactNormal"), MakeVectorValues(Relation.ContactNormal));
		RelationObject->SetNumberField(TEXT("contactArea"), Relation.ContactArea);
		RelationValues.Add(MakeShared<FJsonValueObject>(RelationObject));
	}
	Root->SetArrayField(TEXT("relations"), MoveTemp(RelationValues));

	FString ContextText;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&ContextText);
	FJsonSerializer::Serialize(Root, Writer);
	return ContextText;
}

FText SEasyHouseBuilderPanel::ExtractAIResponseText(const FString& ResponseContent) const
{
	if (ResponseContent.Contains(TEXT("data:")))
	{
		TArray<FString> Lines;
		ResponseContent.ParseIntoArrayLines(Lines);

		TArray<FString> DeltaTextParts;
		TArray<FString> CompletedTextParts;
		for (const FString& Line : Lines)
		{
			FString DataLine = Line.TrimStartAndEnd();
			if (!DataLine.RemoveFromStart(TEXT("data:")))
			{
				continue;
			}

			DataLine = DataLine.TrimStartAndEnd();
			if (DataLine.IsEmpty() || DataLine == TEXT("[DONE]"))
			{
				continue;
			}

			TSharedPtr<FJsonObject> StreamRoot;
			TSharedRef<TJsonReader<>> StreamReader = TJsonReaderFactory<>::Create(DataLine);
			if (FJsonSerializer::Deserialize(StreamReader, StreamRoot) && StreamRoot.IsValid())
			{
				FString Type;
				StreamRoot->TryGetStringField(TEXT("type"), Type);

				FString Delta;
				if (StreamRoot->TryGetStringField(TEXT("delta"), Delta) && !Delta.IsEmpty())
				{
					DeltaTextParts.Add(Delta);
					continue;
				}

				FString Text;
				if (Type.Contains(TEXT("delta")) && StreamRoot->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
				{
					DeltaTextParts.Add(Text);
					continue;
				}
			}

			const FText ParsedDataText = ExtractAIResponseText(DataLine);
			if (!ParsedDataText.IsEmpty())
			{
				CompletedTextParts.Add(ParsedDataText.ToString());
			}
		}

		if (DeltaTextParts.Num() > 0)
		{
			return FText::FromString(FString::Join(DeltaTextParts, TEXT("")));
		}

		if (CompletedTextParts.Num() > 0)
		{
			return FText::FromString(FString::Join(CompletedTextParts, TEXT("\n")));
		}
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return FText::GetEmpty();
	}

	const TSharedPtr<FJsonObject>* ErrorObject = nullptr;
	if (Root->TryGetObjectField(TEXT("error"), ErrorObject) && ErrorObject && ErrorObject->IsValid())
	{
		FString ErrorMessage;
		if ((*ErrorObject)->TryGetStringField(TEXT("message"), ErrorMessage))
		{
			return FText::FromString(ErrorMessage);
		}
	}

	FString OutputText;
	if (Root->TryGetStringField(TEXT("output_text"), OutputText) && !OutputText.IsEmpty())
	{
		return FText::FromString(OutputText);
	}

	TArray<FString> TextParts;
	auto AddTextField = [&TextParts](const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return;
		}

		FString Text;
		if (Object->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
		{
			TextParts.AddUnique(Text);
		}

		FString Delta;
		if (Object->TryGetStringField(TEXT("delta"), Delta) && !Delta.IsEmpty())
		{
			TextParts.AddUnique(Delta);
		}

		FString NestedOutputText;
		if (Object->TryGetStringField(TEXT("output_text"), NestedOutputText) && !NestedOutputText.IsEmpty())
		{
			TextParts.AddUnique(NestedOutputText);
		}

		FString Content;
		if (Object->TryGetStringField(TEXT("content"), Content) && !Content.IsEmpty())
		{
			TextParts.AddUnique(Content);
		}
	};

	AddTextField(Root);

	const TArray<TSharedPtr<FJsonValue>>* OutputArray = nullptr;
	if (Root->TryGetArrayField(TEXT("output"), OutputArray))
	{
		for (const TSharedPtr<FJsonValue>& OutputValue : *OutputArray)
		{
			const TSharedPtr<FJsonObject> OutputObject = OutputValue.IsValid() ? OutputValue->AsObject() : nullptr;
			if (!OutputObject.IsValid())
			{
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
			AddTextField(OutputObject);
			if (!OutputObject->TryGetArrayField(TEXT("content"), ContentArray))
			{
				continue;
			}

			for (const TSharedPtr<FJsonValue>& ContentValue : *ContentArray)
			{
				const TSharedPtr<FJsonObject> ContentObject = ContentValue.IsValid() ? ContentValue->AsObject() : nullptr;
				if (!ContentObject.IsValid())
				{
					continue;
				}

				FString Text;
				if (ContentObject->TryGetStringField(TEXT("text"), Text) && !Text.IsEmpty())
				{
					TextParts.AddUnique(Text);
				}
				AddTextField(ContentObject);
			}
		}
	}

	const TSharedPtr<FJsonObject>* ResponseObject = nullptr;
	if (Root->TryGetObjectField(TEXT("response"), ResponseObject) && ResponseObject && ResponseObject->IsValid())
	{
		AddTextField(*ResponseObject);

		const TArray<TSharedPtr<FJsonValue>>* NestedOutputArray = nullptr;
		if ((*ResponseObject)->TryGetArrayField(TEXT("output"), NestedOutputArray))
		{
			for (const TSharedPtr<FJsonValue>& OutputValue : *NestedOutputArray)
			{
				const TSharedPtr<FJsonObject> OutputObject = OutputValue.IsValid() ? OutputValue->AsObject() : nullptr;
				if (!OutputObject.IsValid())
				{
					continue;
				}

				AddTextField(OutputObject);

				const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
				if (!OutputObject->TryGetArrayField(TEXT("content"), ContentArray))
				{
					continue;
				}

				for (const TSharedPtr<FJsonValue>& ContentValue : *ContentArray)
				{
					const TSharedPtr<FJsonObject> ContentObject = ContentValue.IsValid() ? ContentValue->AsObject() : nullptr;
					AddTextField(ContentObject);
				}
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ChoicesArray = nullptr;
	if (Root->TryGetArrayField(TEXT("choices"), ChoicesArray))
	{
		for (const TSharedPtr<FJsonValue>& ChoiceValue : *ChoicesArray)
		{
			const TSharedPtr<FJsonObject> ChoiceObject = ChoiceValue.IsValid() ? ChoiceValue->AsObject() : nullptr;
			if (!ChoiceObject.IsValid())
			{
				continue;
			}

			AddTextField(ChoiceObject);

			const TSharedPtr<FJsonObject>* MessageObject = nullptr;
			if (ChoiceObject->TryGetObjectField(TEXT("message"), MessageObject) && MessageObject && MessageObject->IsValid())
			{
				AddTextField(*MessageObject);
			}

			const TSharedPtr<FJsonObject>* DeltaObject = nullptr;
			if (ChoiceObject->TryGetObjectField(TEXT("delta"), DeltaObject) && DeltaObject && DeltaObject->IsValid())
			{
				AddTextField(*DeltaObject);
			}
		}
	}

	return TextParts.Num() > 0
		? FText::FromString(FString::Join(TextParts, TEXT("\n")))
		: FText::GetEmpty();
}

void SEasyHouseBuilderPanel::AppendAIConversationMessage(const FText& Message, bool bIsUserMessage)
{
	if (!bIsUserMessage && bAIRequestInFlight && !ActiveAIResponseTextWidget.IsValid())
	{
		StartAIResponseMessage(Message);
		return;
	}

	if (!bIsUserMessage && !bAIRequestInFlight && ActiveAIResponseTextWidget.IsValid())
	{
		FinishAIResponseMessage(Message);
		return;
	}

	if (!AIMessageListBox.IsValid())
	{
		return;
	}

	AIMessageListBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		BuildAIMessageBubble(Message, bIsUserMessage)
	];

	if (AIConversationScrollBox.IsValid())
	{
		AIConversationScrollBox->ScrollToEnd();
	}
}

void SEasyHouseBuilderPanel::StartAIResponseMessage(const FText& Message)
{
	ActiveAIResponseTextWidget.Reset();
	ActiveAIProgressPercent = 5;

	if (!AIMessageListBox.IsValid())
	{
		return;
	}

	TSharedPtr<SMultiLineEditableText> MessageTextWidget;
	AIMessageListBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 0.0f, 0.0f, 8.0f)
	[
		BuildAIMessageBubble(FormatAIResponseMessage(Message, ActiveAIProgressPercent, false), false, &MessageTextWidget)
	];

	ActiveAIResponseTextWidget = MessageTextWidget;

	if (AIConversationScrollBox.IsValid())
	{
		AIConversationScrollBox->ScrollToEnd();
	}
}

void SEasyHouseBuilderPanel::UpdateAIResponseMessage(const FText& Message, int32 Percent)
{
	if (!ActiveAIResponseTextWidget.IsValid())
	{
		StartAIResponseMessage(Message);
	}

	if (!ActiveAIResponseTextWidget.IsValid())
	{
		return;
	}

	const int32 ClampedPercent = FMath::Clamp(Percent, 1, AIResponseProgressMaxInFlightPercent);
	ActiveAIProgressPercent = FMath::Max(ActiveAIProgressPercent, ClampedPercent);

	ActiveAIResponseTextWidget->SetText(FormatAIResponseMessage(Message, ActiveAIProgressPercent, false));

	if (AIConversationScrollBox.IsValid())
	{
		AIConversationScrollBox->ScrollToEnd();
	}
}

void SEasyHouseBuilderPanel::FinishAIResponseMessage(const FText& Message)
{
	if (!ActiveAIResponseTextWidget.IsValid())
	{
		StartAIResponseMessage(Message);
	}

	if (ActiveAIResponseTextWidget.IsValid())
	{
		ActiveAIProgressPercent = 100;
		ActiveAIResponseTextWidget->SetText(FormatAIResponseMessage(Message, ActiveAIProgressPercent, true));
		ActiveAIResponseTextWidget.Reset();
	}

	if (AIConversationScrollBox.IsValid())
	{
		AIConversationScrollBox->ScrollToEnd();
	}
}

int32 SEasyHouseBuilderPanel::EstimateAIResponseProgressPercent(const FString& Type, const FString& Message) const
{
	const FString TrimmedMessage = Message.TrimStartAndEnd();
	int32 Percent = ActiveAIProgressPercent > 0 ? ActiveAIProgressPercent + 8 : 8;

	if (Type == TEXT("error"))
	{
		Percent = AIResponseProgressMaxInFlightPercent;
	}
	else if (Type == TEXT("tool"))
	{
		const bool bToolCompleted = TrimmedMessage.Contains(TEXT("完成"));
		Percent = FMath::Max(Percent, bToolCompleted ? 78 : 60);
	}
	else if (TrimmedMessage.Contains(TEXT("正在启动")) || TrimmedMessage.Contains(TEXT("正在继续")))
	{
		Percent = FMath::Max(Percent, 10);
	}
	else if (TrimmedMessage.Contains(TEXT("会话已启动")))
	{
		Percent = FMath::Max(Percent, 20);
	}
	else if (TrimmedMessage.Contains(TEXT("开始处理")))
	{
		Percent = FMath::Max(Percent, 30);
	}
	else if (TrimmedMessage.Contains(TEXT("规划")) || TrimmedMessage.Contains(TEXT("思考")))
	{
		Percent = FMath::Max(Percent, 45);
	}
	else if (TrimmedMessage.Contains(TEXT("最终回复")))
	{
		Percent = FMath::Max(Percent, 90);
	}
	else if (TrimmedMessage.Contains(TEXT("响应完成")) || TrimmedMessage.Contains(TEXT("完成本轮处理")))
	{
		Percent = FMath::Max(Percent, AIResponseProgressMaxInFlightPercent);
	}

	return FMath::Clamp(Percent, 1, AIResponseProgressMaxInFlightPercent);
}

FText SEasyHouseBuilderPanel::FormatAIResponseMessage(const FText& Message, int32 Percent, bool bCompleted) const
{
	const FString Body = Message.ToString().TrimStartAndEnd();
	const bool bLooksLikeFailure =
		Body.Contains(TEXT("失败"))
		|| Body.Contains(TEXT("错误"))
		|| Body.Contains(TEXT("failed"), ESearchCase::IgnoreCase)
		|| Body.Contains(TEXT("error"), ESearchCase::IgnoreCase);
	const int32 DisplayPercent = bCompleted
		? 100
		: FMath::Clamp(Percent, 1, AIResponseProgressMaxInFlightPercent);
	const FString Prefix = bCompleted
		? (bLooksLikeFailure ? FString(TEXT("处理失败")) : FString(TEXT("处理完成（100%）")))
		: FString::Printf(TEXT("正在处理（%d%%）"), DisplayPercent);
	return Body.IsEmpty()
		? FText::FromString(Prefix)
		: FText::FromString(FString::Printf(TEXT("%s\n%s"), *Prefix, *Body));
}

TSharedRef<FDragDropOperation> SEasyHouseBuilderPanel::CreateWallSurfaceDragDropOperationForRow(FName RowName)
{
	FEasyHouseEditorMode* EditorMode = GetActiveEditorMode();
	AEHBBuildingActorBase* Building = EditorMode ? EditorMode->GetActiveBuilding() : nullptr;
	return FEHBWallSurfaceDragDropOp::New(
		EditorMode,
		Building,
		WallSurfaceTable.Get(),
		RowName,
		bWallSurfaceCoverBothSides,
		bWallSurfaceFlipSampleSides);
}

TSharedRef<FDragDropOperation> SEasyHouseBuilderPanel::CreatePillarMeshDragDropOperationForRow(FName RowName)
{
	FEasyHouseEditorMode* EditorMode = GetActiveEditorMode();
	AEHBBuildingActorBase* Building = EditorMode ? EditorMode->GetActiveBuilding() : nullptr;
	return FEHBPillarMeshDragDropOp::New(EditorMode, Building, PillarMeshTable.Get(), RowName);
}

TSharedRef<FDragDropOperation> SEasyHouseBuilderPanel::CreateRailingMeshDragDropOperationForRow(FName RowName)
{
	FEasyHouseEditorMode* EditorMode = GetActiveEditorMode();
	AEHBBuildingActorBase* Building = EditorMode ? EditorMode->GetActiveBuilding() : nullptr;
	return FEHBRailingMeshDragDropOp::New(
		EditorMode,
		Building,
		RailingMeshTable.Get(),
		RowName,
		bApplyRailingMeshToSinglePost);
}

void SEasyHouseBuilderPanel::HandleWallSubPanelSelectionChanged(ECheckBoxState NewState, EEasyHouseWallSubPanel Panel)
{
	if (NewState == ECheckBoxState::Checked)
	{
		ActiveWallSubPanel = Panel;
		if (WallSubPanelSwitcher.IsValid())
		{
			WallSubPanelSwitcher->SetActiveWidgetIndex(static_cast<int32>(ActiveWallSubPanel));
		}
	}
}

ECheckBoxState SEasyHouseBuilderPanel::IsWallSubPanelChecked(EEasyHouseWallSubPanel Panel) const
{
	return ActiveWallSubPanel == Panel ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FString SEasyHouseBuilderPanel::GetWallSurfaceTablePath() const
{
	return WallSurfaceTable.IsValid() ? WallSurfaceTable->GetPathName() : FString();
}

void SEasyHouseBuilderPanel::HandleWallSurfaceTableChanged(const FAssetData& AssetData)
{
	UDataTable* Table = Cast<UDataTable>(AssetData.GetAsset());
	WallSurfaceTable = Table && Table->GetRowStruct() == FEHBWallMeshData::StaticStruct() ? Table : nullptr;
	SaveWallSurfaceTableToConfig();
	RefreshWallSurfaceRows();
}

bool SEasyHouseBuilderPanel::ShouldFilterWallSurfaceTable(const FAssetData& AssetData) const
{
	UDataTable* Table = Cast<UDataTable>(AssetData.GetAsset());
	return !Table || Table->GetRowStruct() != FEHBWallMeshData::StaticStruct();
}

FReply SEasyHouseBuilderPanel::HandleRefreshWallSurfaceRowsClicked()
{
	RefreshWallSurfaceRows();
	return FReply::Handled();
}

void SEasyHouseBuilderPanel::RefreshWallSurfaceRows()
{
	WallSurfaceRowNames.Reset();
	WallSurfaceThumbnails.Reset();
	if (!WallSurfaceCardsBox.IsValid())
	{
		return;
	}

	WallSurfaceCardsBox->ClearChildren();
	if (WallSurfaceTable.IsValid() && WallSurfaceTable->GetRowStruct() == FEHBWallMeshData::StaticStruct())
	{
		for (const FName& RowName : WallSurfaceTable->GetRowNames())
		{
			if (WallSurfaceTable->FindRow<FEHBWallMeshData>(RowName, TEXT("SEasyHouseBuilderPanel::RefreshWallSurfaceRows"), false))
			{
				WallSurfaceRowNames.Add(RowName);
				WallSurfaceCardsBox->AddSlot()
				.Padding(0.0f, 0.0f, 8.0f, 8.0f)
				[
					BuildWallSurfaceCard(RowName)
				];
			}
		}
	}

	SetWallSurfaceStatusText(FText::Format(
		LOCTEXT("WallSurfaceRowsRefreshed", "\u5df2\u8bfb\u53d6 {0} \u4e2a\u5899\u9762\u91c7\u6837\u9879\u3002"),
		FText::AsNumber(WallSurfaceRowNames.Num())));
}

void SEasyHouseBuilderPanel::SaveWallSurfaceTableToConfig() const
{
	UEHBBuildingToolsetSettings* Settings = GetMutableDefault<UEHBBuildingToolsetSettings>();
	if (Settings)
	{
		Settings->DefaultWallMeshDataTable = WallSurfaceTable.Get();
		Settings->SaveConfig();
		Settings->TryUpdateDefaultConfigFile(TEXT(""), false);
	}
}

void SEasyHouseBuilderPanel::LoadWallSurfaceTableFromConfig()
{
	const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
	UDataTable* Table = Settings && !Settings->DefaultWallMeshDataTable.IsNull()
		? Settings->DefaultWallMeshDataTable.LoadSynchronous()
		: nullptr;
	WallSurfaceTable = Table && Table->GetRowStruct() == FEHBWallMeshData::StaticStruct() ? Table : nullptr;
}

void SEasyHouseBuilderPanel::ReloadWallSurfaceTable()
{
	if (WallSurfaceTable.IsValid())
	{
		const FString TablePath = WallSurfaceTable->GetPathName();
		WallSurfaceTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *TablePath));
	}
}

UStaticMesh* SEasyHouseBuilderPanel::GetPreviewMeshFromWallSurfaceRow(const FEHBWallMeshData* Row) const
{
	return Row ? Row->SourceStaticMesh.LoadSynchronous() : nullptr;
}

void SEasyHouseBuilderPanel::SetWallSurfaceStatusText(const FText& NewStatus) const
{
	if (WallSurfaceStatusText.IsValid())
	{
		WallSurfaceStatusText->SetText(NewStatus);
	}
}

FString SEasyHouseBuilderPanel::GetPillarMeshTablePath() const
{
	return PillarMeshTable.IsValid() ? PillarMeshTable->GetPathName() : FString();
}

void SEasyHouseBuilderPanel::HandlePillarMeshTableChanged(const FAssetData& AssetData)
{
	UDataTable* Table = Cast<UDataTable>(AssetData.GetAsset());
	PillarMeshTable = Table && Table->GetRowStruct() == FEHBPillarMeshData::StaticStruct() ? Table : nullptr;
	SavePillarMeshTableToConfig();
	RefreshPillarMeshRows();
}

bool SEasyHouseBuilderPanel::ShouldFilterPillarMeshTable(const FAssetData& AssetData) const
{
	UDataTable* Table = Cast<UDataTable>(AssetData.GetAsset());
	return !Table || Table->GetRowStruct() != FEHBPillarMeshData::StaticStruct();
}

FReply SEasyHouseBuilderPanel::HandleRefreshPillarMeshRowsClicked()
{
	RefreshPillarMeshRows();
	return FReply::Handled();
}

void SEasyHouseBuilderPanel::RefreshPillarMeshRows()
{
	PillarMeshRowNames.Reset();
	PillarMeshThumbnails.Reset();
	if (!PillarMeshCardsBox.IsValid())
	{
		return;
	}

	PillarMeshCardsBox->ClearChildren();
	if (PillarMeshTable.IsValid() && PillarMeshTable->GetRowStruct() == FEHBPillarMeshData::StaticStruct())
	{
		for (const FName& RowName : PillarMeshTable->GetRowNames())
		{
			if (PillarMeshTable->FindRow<FEHBPillarMeshData>(RowName, TEXT("SEasyHouseBuilderPanel::RefreshPillarMeshRows"), false))
			{
				PillarMeshRowNames.Add(RowName);
				PillarMeshCardsBox->AddSlot()
				.Padding(0.0f, 0.0f, 8.0f, 8.0f)
				[
					BuildPillarMeshCard(RowName)
				];
			}
		}
	}

	SetPillarMeshStatusText(FText::Format(
		LOCTEXT("PillarMeshRowsRefreshed", "\u5df2\u8bfb\u53d6 {0} \u4e2a\u67f1\u4f53\u91c7\u6837\u9879\u3002"),
		FText::AsNumber(PillarMeshRowNames.Num())));
}

void SEasyHouseBuilderPanel::SavePillarMeshTableToConfig() const
{
	UEHBBuildingToolsetSettings* Settings = GetMutableDefault<UEHBBuildingToolsetSettings>();
	if (Settings)
	{
		Settings->DefaultPillarMeshDataTable = PillarMeshTable.Get();
		Settings->SaveConfig();
		Settings->TryUpdateDefaultConfigFile(TEXT(""), false);
	}
}

void SEasyHouseBuilderPanel::LoadPillarMeshTableFromConfig()
{
	const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
	UDataTable* Table = Settings && !Settings->DefaultPillarMeshDataTable.IsNull()
		? Settings->DefaultPillarMeshDataTable.LoadSynchronous()
		: nullptr;
	PillarMeshTable = Table && Table->GetRowStruct() == FEHBPillarMeshData::StaticStruct() ? Table : nullptr;
}

void SEasyHouseBuilderPanel::ReloadPillarMeshTable()
{
	if (PillarMeshTable.IsValid())
	{
		const FString TablePath = PillarMeshTable->GetPathName();
		PillarMeshTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *TablePath));
	}
}

UStaticMesh* SEasyHouseBuilderPanel::GetPreviewMeshFromPillarMeshRow(const FEHBPillarMeshData* Row) const
{
	return Row ? Row->SourceStaticMesh.LoadSynchronous() : nullptr;
}

void SEasyHouseBuilderPanel::SetPillarMeshStatusText(const FText& NewStatus) const
{
	if (PillarMeshStatusText.IsValid())
	{
		PillarMeshStatusText->SetText(NewStatus);
	}
}

FString SEasyHouseBuilderPanel::GetRailingMeshTablePath() const
{
	return RailingMeshTable.IsValid() ? RailingMeshTable->GetPathName() : FString();
}

void SEasyHouseBuilderPanel::HandleRailingMeshTableChanged(const FAssetData& AssetData)
{
	UDataTable* Table = Cast<UDataTable>(AssetData.GetAsset());
	RailingMeshTable = Table && Table->GetRowStruct() == FEHBRailingMeshData::StaticStruct() ? Table : nullptr;
	SaveRailingMeshTableToConfig();
	RefreshRailingMeshRows();
}

bool SEasyHouseBuilderPanel::ShouldFilterRailingMeshTable(const FAssetData& AssetData) const
{
	UDataTable* Table = Cast<UDataTable>(AssetData.GetAsset());
	return !Table || Table->GetRowStruct() != FEHBRailingMeshData::StaticStruct();
}

FReply SEasyHouseBuilderPanel::HandleRefreshRailingMeshRowsClicked()
{
	RefreshRailingMeshRows();
	return FReply::Handled();
}

void SEasyHouseBuilderPanel::RefreshRailingMeshRows()
{
	RailingMeshRowNames.Reset();
	RailingMeshThumbnails.Reset();

	if (!RailingMeshCardsBox.IsValid())
	{
		return;
	}

	RailingMeshCardsBox->ClearChildren();
	if (!RailingMeshTable.IsValid())
	{
		RailingMeshCardsBox->AddSlot()
		.Padding(0.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SBox)
			.WidthOverride(340.0f)
			[
				SNew(SBorder)
				.Padding(10.0f)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoRailingMeshTableSelected", "\u8bf7\u9009\u62e9\u6276\u624b\u91c7\u6837\u8868\u3002"))
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]
		];
		SetRailingMeshStatusText(LOCTEXT("NoRailingMeshTableStatus", "\u672a\u9009\u62e9\u6276\u624b\u91c7\u6837\u8868\u3002"));
		return;
	}

	if (RailingMeshTable->GetRowStruct() != FEHBRailingMeshData::StaticStruct())
	{
		RailingMeshCardsBox->AddSlot()
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
					.Text(LOCTEXT("InvalidRailingMeshTableStruct", "\u5f53\u524d\u6570\u636e\u8868\u884c\u7ed3\u6784\u4e0d\u662f\u201c\u6276\u624b\u7f51\u683c\u4f53\u6570\u636e / FEHBRailingMeshData\u201d\u3002"))
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Error)
				]
			]
		];
		SetRailingMeshStatusText(LOCTEXT("InvalidRailingMeshTableStatus", "\u6276\u624b\u91c7\u6837\u8868\u884c\u7ed3\u6784\u4e0d\u6b63\u786e\u3002"));
		return;
	}

	for (const FName& RowName : RailingMeshTable->GetRowNames())
	{
		if (RailingMeshTable->FindRow<FEHBRailingMeshData>(RowName, TEXT("SEasyHouseBuilderPanel::RefreshRailingMeshRows"), false))
		{
			RailingMeshRowNames.Add(RowName);
		}
	}

	if (RailingMeshRowNames.Num() == 0)
	{
		RailingMeshCardsBox->AddSlot()
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
					.Text(LOCTEXT("NoRailingMeshRows", "\u5f53\u524d\u6276\u624b\u91c7\u6837\u8868\u4e2d\u8fd8\u6ca1\u6709\u53ef\u7528\u7684\u6276\u624b\u91c7\u6837\u9879\u3002"))
					.AutoWrapText(true)
					.ColorAndOpacity(FStyleColors::Foreground)
				]
			]
		];
		SetRailingMeshStatusText(LOCTEXT("NoRailingMeshRowsStatus", "\u6276\u624b\u91c7\u6837\u8868\u4e2d\u6ca1\u6709\u53ef\u7528\u9879\u3002"));
		return;
	}

	for (const FName& RowName : RailingMeshRowNames)
	{
		RailingMeshCardsBox->AddSlot()
		.Padding(0.0f, 0.0f, 8.0f, 8.0f)
		[
			BuildRailingMeshCard(RowName)
		];
	}

	SetRailingMeshStatusText(FText::Format(LOCTEXT("RailingMeshRowsRefreshed", "\u5df2\u8bfb\u53d6 {0} \u4e2a\u6276\u624b\u91c7\u6837\u9879\u3002"), FText::AsNumber(RailingMeshRowNames.Num())));
}

void SEasyHouseBuilderPanel::SaveRailingMeshTableToConfig() const
{
	UEHBBuildingToolsetSettings* Settings = GetMutableDefault<UEHBBuildingToolsetSettings>();
	if (!Settings)
	{
		return;
	}

	Settings->DefaultRailingMeshDataTable = RailingMeshTable.Get();
	Settings->SaveConfig();
	Settings->TryUpdateDefaultConfigFile(TEXT(""), false);
}

void SEasyHouseBuilderPanel::LoadRailingMeshTableFromConfig()
{
	const UEHBBuildingToolsetSettings* Settings = GetDefault<UEHBBuildingToolsetSettings>();
	UDataTable* ConfigTable = Settings && !Settings->DefaultRailingMeshDataTable.IsNull()
		? Settings->DefaultRailingMeshDataTable.LoadSynchronous()
		: nullptr;

	RailingMeshTable = ConfigTable && ConfigTable->GetRowStruct() == FEHBRailingMeshData::StaticStruct()
		? ConfigTable
		: nullptr;
}

void SEasyHouseBuilderPanel::ReloadRailingMeshTable()
{
	if (!RailingMeshTable.IsValid())
	{
		return;
	}

	const FString TablePath = RailingMeshTable->GetPathName();
	if (!TablePath.IsEmpty())
	{
		if (UDataTable* ReloadedTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *TablePath)))
		{
			RailingMeshTable = ReloadedTable;
		}
	}
}

UStaticMesh* SEasyHouseBuilderPanel::GetPreviewMeshFromRailingMeshRow(const FEHBRailingMeshData* Row) const
{
	if (!Row)
	{
		return nullptr;
	}
	if (UStaticMesh* PostMesh = Row->PostMesh.SourceStaticMesh.LoadSynchronous())
	{
		return PostMesh;
	}
	return Row->RailMesh.SourceStaticMesh.LoadSynchronous();
}

void SEasyHouseBuilderPanel::SetRailingMeshStatusText(const FText& NewStatus) const
{
	if (RailingMeshStatusText.IsValid())
	{
		RailingMeshStatusText->SetText(NewStatus);
	}
}

UWorld* SEasyHouseBuilderPanel::GetEditorWorld() const
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

bool SEasyHouseBuilderPanel::GetViewportCenterPlacementLocation(UWorld* World, FVector& OutLocation) const
{
	if (!World || !GCurrentLevelEditingViewportClient)
	{
		return false;
	}

	const FVector TraceStart = GCurrentLevelEditingViewportClient->GetViewLocation();
	const FVector TraceDirection = GCurrentLevelEditingViewportClient->GetViewRotation().Vector();
	const FVector TraceEnd = TraceStart + TraceDirection * EasyHouseBuilderPanel::PlacementTraceDistance;

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EasyHouseBuilder_CreateBuildingTrace), true);
	QueryParams.bReturnPhysicalMaterial = false;

	if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, QueryParams) && HitResult.bBlockingHit)
	{
		OutLocation = HitResult.ImpactPoint;
		return true;
	}

	OutLocation = TraceStart + TraceDirection * EasyHouseBuilderPanel::FallbackSpawnDistance;
	return true;
}

void SEasyHouseBuilderPanel::AdoptCopiedBuilding(AEHBBuildingActorBase* Building)
{
	ActiveBuilding = Building;
	if (BuildingSelectionHintText.IsValid()) BuildingSelectionHintText->SetText(FText::GetEmpty());
	RefreshBuildingList();
}

void SEasyHouseBuilderPanel::SetActiveBuilding(AEHBBuildingActorBase* Building)
{
	ActiveBuilding = Building;

	if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
	{
		EditorMode->SetActiveBuilding(Building);
	}

	if (GEditor && Building)
	{
		GEditor->SelectNone(false, true, false);
		GEditor->SelectActor(Building, true, true, true);
	}

	if (BuildingSelectionHintText.IsValid() && Building)
	{
		BuildingSelectionHintText->SetText(FText::GetEmpty());
	}
}

FEasyHouseEditorMode* SEasyHouseBuilderPanel::GetActiveEditorMode() const
{
	return static_cast<FEasyHouseEditorMode*>(
		GLevelEditorModeTools().GetActiveMode(FEasyHouseEditorMode::EM_EasyHouseEditorModeId));
}

FText SEasyHouseBuilderPanel::GetBuildingDisplayText(const AEHBBuildingActorBase* Building) const
{
	if (!Building)
	{
		return LOCTEXT("InvalidBuilding", "\u65e0\u6548\u5efa\u7b51\u5bf9\u8c61");
	}

	const bool bIsActive = ActiveBuilding.Get() == Building;
	const FString ActorLabel = Building->GetActorLabel();
	const FVector Location = Building->GetActorLocation();
	const FString Prefix = bIsActive ? TEXT("[\u5f53\u524d] ") : TEXT("");

	return FText::FromString(FString::Printf(
		TEXT("%s%s  (%.0f, %.0f, %.0f)"),
		*Prefix,
		*ActorLabel,
		Location.X,
		Location.Y,
		Location.Z));
}

void SEasyHouseBuilderPanel::SwitchToPanel(EEasyHouseToolPanel Panel)
{
	ActivePanel = Panel;

	if (FEasyHouseEditorMode* EditorMode = GetActiveEditorMode())
	{
		if (AEHBBuildingActorBase* ModeBuilding = EditorMode->GetActiveBuilding())
		{
			ActiveBuilding = ModeBuilding;
		}
		EditorMode->SetWallsPanelActive(Panel == EEasyHouseToolPanel::Walls);
		EditorMode->SetWallCreationDefaults(WallCreationHeight, WallCreationThickness);
		EditorMode->SetWallCreationPhysicalColumns(bWallCreationPhysicalColumns);
		EditorMode->SetRailingCreationDefaults(RailingCreationHeight, RailingCreationPostSpacing, RailingCreationThickness);
	}

	if (PanelSwitcher.IsValid())
	{
		PanelSwitcher->SetActiveWidgetIndex(static_cast<int32>(ActivePanel));
	}

	if (Panel == EEasyHouseToolPanel::BuildingSelection)
	{
		RefreshBuildingList();
	}
	else if (Panel == EEasyHouseToolPanel::Railings)
	{
		RefreshRailingMeshRows();
	}
}

void SEasyHouseBuilderPanel::HandlePanelSelectionChanged(ECheckBoxState NewState, EEasyHouseToolPanel Panel)
{
	if (NewState != ECheckBoxState::Checked)
	{
		return;
	}

	if (!IsPanelEnabled(Panel))
	{
		return;
	}

	SwitchToPanel(Panel);
}

ECheckBoxState SEasyHouseBuilderPanel::IsPanelChecked(EEasyHouseToolPanel Panel) const
{
	return ActivePanel == Panel ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

bool SEasyHouseBuilderPanel::IsPanelEnabled(EEasyHouseToolPanel Panel) const
{
	if (Panel == EEasyHouseToolPanel::MeshSampling
		|| Panel == EEasyHouseToolPanel::BuildingSelection
		|| ActiveBuilding.IsValid())
	{
		return true;
	}

	const FEasyHouseEditorMode* EditorMode = GetActiveEditorMode();
	return EditorMode && EditorMode->GetActiveBuilding() != nullptr;
}

FText SEasyHouseBuilderPanel::GetPanelButtonToolTipText(EEasyHouseToolPanel Panel, FText Label) const
{
	if (IsPanelEnabled(Panel))
	{
		return Label;
	}

	return LOCTEXT("PanelDisabledNeedsBuilding", "\u8bf7\u5148\u5728\u201c\u9009\u62e9\u5efa\u7b51\u5bf9\u8c61\u201d\u9875\u5361\u4e2d\u9009\u62e9\u6216\u521b\u5efa\u4e00\u4e2a\u5efa\u7b51\u5bf9\u8c61\u3002");
}


TSharedRef<SWidget> SEasyHouseBuilderPanel::BuildCreationAssistControls(bool bWall)
{
 auto Read=[this](){auto* Mode=GetActiveEditorMode();return Mode?Mode->CreationAssist:EHBDragAngleSnap::FOptions{};};
 auto Box=SNew(SVerticalBox);
 auto Toggle=[this,Read,Box](FText Text,bool EHBDragAngleSnap::FOptions::*Field)
 {
  Box->AddSlot().AutoHeight().Padding(0,2)
  [SNew(SCheckBox).IsChecked_Lambda([Read,Field](){return Read().*Field?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
   .OnCheckStateChanged_Lambda([this,Field](ECheckBoxState State){if(auto* Mode=GetActiveEditorMode())Mode->CreationAssist.*Field=State==ECheckBoxState::Checked;})
   [SNew(STextBlock).Text(Text)]];
 };
 Toggle(LOCTEXT("DrawAngleAssist","八方向角度吸附"),&EHBDragAngleSnap::FOptions::bAngleSnap);
 Toggle(LOCTEXT("DrawFixedLength","固定端点间水平距离（厘米）"),&EHBDragAngleSnap::FOptions::bFixedLength);
 Box->AddSlot().AutoHeight()[SNew(SNumericEntryBox<double>).MinValue(1).MaxValue(1000000)
  .Value_Lambda([Read](){return TOptional<double>(Read().LengthCm);})
  .IsEnabled_Lambda([Read](){return Read().bFixedLength;})
  .OnValueChanged_Lambda([this](double Value){if(FMath::IsFinite(Value))if(auto* Mode=GetActiveEditorMode())Mode->CreationAssist.LengthCm=FMath::Clamp(Value,1.0,1000000.0);})];
 Toggle(LOCTEXT("DrawFixedDirection","锁定世界方向（度）"),&EHBDragAngleSnap::FOptions::bFixedDirection);
 Box->AddSlot().AutoHeight()[SNew(SNumericEntryBox<double>).MinValue(-360).MaxValue(360)
  .Value_Lambda([Read](){return TOptional<double>(Read().DirectionDegrees);})
  .IsEnabled_Lambda([Read](){return Read().bFixedDirection;})
  .OnValueChanged_Lambda([this](double Value){if(FMath::IsFinite(Value))if(auto* Mode=GetActiveEditorMode())Mode->CreationAssist.DirectionDegrees=FMath::UnwindDegrees(Value);})];

 if(bWall)
 {
  Toggle(LOCTEXT("DrawFixedRectangle","固定矩形宽深（厘米，沿建筑轴）"),&EHBDragAngleSnap::FOptions::bFixedRectangle);
  auto Dimension=[this,Read,Box](FText Label,double EHBDragAngleSnap::FOptions::*Field)
  {
   Box->AddSlot().AutoHeight()[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[SNew(STextBlock).Text(Label)]
    +SHorizontalBox::Slot().FillWidth(1)[SNew(SNumericEntryBox<double>).MinValue(11).MaxValue(1000000)
     .Value_Lambda([Read,Field](){return TOptional<double>(Read().*Field);})
     .IsEnabled_Lambda([Read](){return Read().bFixedRectangle;})
     .OnValueChanged_Lambda([this,Field](double Value){if(FMath::IsFinite(Value))if(auto* Mode=GetActiveEditorMode())Mode->CreationAssist.*Field=FMath::Clamp(Value,11.0,1000000.0);})]];
  };
  Dimension(LOCTEXT("DrawRectangleWidth","宽 X"),&EHBDragAngleSnap::FOptions::RectangleWidthCm);
  Dimension(LOCTEXT("DrawRectangleDepth","深 Y"),&EHBDragAngleSnap::FOptions::RectangleDepthCm);
 }
 Box->AddSlot().AutoHeight().Padding(0,4)[SNew(STextBlock).AutoWrapText(true)
  .Text(LOCTEXT("DrawAssistScope","墙和扶手共用。Ctrl 临时绕过定长、方向、矩形尺寸与八方向辅助；0°沿世界X，90°沿世界Y。已有端点、表面与楼梯连接优先；矩形绘墙不使用定长/方向。"))];
 return Box;
}

#undef LOCTEXT_NAMESPACE
