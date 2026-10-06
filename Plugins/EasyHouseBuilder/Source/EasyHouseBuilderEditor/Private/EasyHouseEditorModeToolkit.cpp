// Copyright Epic Games, Inc. All Rights Reserved.

#include "EasyHouseEditorModeToolkit.h"

#include "EditorModeManager.h"
#include "Framework/Docking/TabManager.h"
#include "EasyHouseEditorMode.h"
#include "SEasyHouseBuilderPanel.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SEHBElementEditorPanel.h"

#define LOCTEXT_NAMESPACE "FEasyHouseEditorModeToolkit"

namespace
{
	const FName EHBElementEditorTabId(TEXT("EasyHouseBuilder.ElementEditor"));
	constexpr float EHBElementEditorMinWidth = 380.0f;
	constexpr float EHBElementEditorMinHeight = 180.0f;

	TWeakPtr<SDockTab> GElementEditorTab;
	TSharedPtr<SEHBElementEditorPanel> GElementEditorPanel;
	bool bGElementEditorTabSpawnerRegistered = false;

	void HandleGlobalElementEditorTabClosed(TSharedRef<SDockTab> ClosedTab)
	{
		if (GElementEditorTab.Pin() == ClosedTab)
		{
			GElementEditorTab.Reset();
			GElementEditorPanel.Reset();
		}
	}

	TSharedRef<SDockTab> SpawnGlobalElementEditorTab(const FSpawnTabArgs& Args)
	{
		(void)Args;
		GElementEditorPanel = SNew(SEHBElementEditorPanel);

		TSharedRef<SDockTab> NewTab =
			SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Label(LOCTEXT("ElementEditorTabLabel", "\u5efa\u7b51\u5143\u7d20\u7f16\u8f91"))
			[
				SNew(SBox)
				.MinDesiredWidth(EHBElementEditorMinWidth)
				.MinDesiredHeight(EHBElementEditorMinHeight)
				[
					GElementEditorPanel.ToSharedRef()
				]
			];

		NewTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateStatic(&HandleGlobalElementEditorTabClosed));
		GElementEditorTab = NewTab;
		return NewTab;
	}
}

void FEasyHouseEditorModeToolkit::RegisterGlobalElementEditorTabSpawner()
{
	if (bGElementEditorTabSpawnerRegistered)
	{
		return;
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(EHBElementEditorTabId);
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(
			EHBElementEditorTabId,
			FOnSpawnTab::CreateStatic(&SpawnGlobalElementEditorTab))
		.SetDisplayName(LOCTEXT("ElementEditorTabTitle", "\u5efa\u7b51\u5143\u7d20\u7f16\u8f91"))
		.SetTooltipText(LOCTEXT("ElementEditorTabTooltip", "\u663e\u793a\u5f53\u524d\u9009\u4e2d\u7684\u5efa\u7b51\u5143\u7d20\u7c7b\u578b\u3002"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	bGElementEditorTabSpawnerRegistered = true;
}

void FEasyHouseEditorModeToolkit::UnregisterGlobalElementEditorTabSpawner()
{
	if (!bGElementEditorTabSpawnerRegistered)
	{
		return;
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(EHBElementEditorTabId);
	bGElementEditorTabSpawnerRegistered = false;
	GElementEditorTab.Reset();
	GElementEditorPanel.Reset();
}

void FEasyHouseEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	// 创建主工具面板。这里保持 Toolkit 只负责承载，具体按钮和业务逻辑交给 Slate 面板实现。
	ToolsetWidget = SNew(SEasyHouseBuilderPanel);

	RegisterElementEditorTabSpawner();

	FModeToolkit::Init(InitToolkitHost);
}

FName FEasyHouseEditorModeToolkit::GetToolkitFName() const
{
	// 使用稳定英文名称作为内部 ID，避免本地化文本影响编辑器恢复和调试。
	return FName("EasyHouseEditorMode");
}

FText FEasyHouseEditorModeToolkit::GetBaseToolkitName() const
{
	// 显示给用户的中文名称。
	return LOCTEXT("ToolkitName", "\u7a0b\u5e8f\u5316\u5efa\u7b51");
}

FEdMode* FEasyHouseEditorModeToolkit::GetEditorMode() const
{
	// 通过编辑器全局模式工具查找当前激活的建筑编辑模式。
	return GLevelEditorModeTools().GetActiveMode(FEasyHouseEditorMode::EM_EasyHouseEditorModeId);
}

TSharedPtr<SWidget> FEasyHouseEditorModeToolkit::GetInlineContent() const
{
	// FModeToolkit 会把这个 Widget 嵌入模式面板主区域。
	return ToolsetWidget;
}

void FEasyHouseEditorModeToolkit::SetSelectedElement(AActor* SelectedActor)
{
	if (!SelectedActor)
	{
		if (!ElementEditorPanel.IsValid() && GElementEditorPanel.IsValid())
		{
			ElementEditorPanel = GElementEditorPanel;
		}
		if (ElementEditorPanel.IsValid())
		{
			ElementEditorPanel->SetSelectedActor(nullptr);
		}
		return;
	}

	EnsureElementEditorTab();
	if (ElementEditorPanel.IsValid())
	{
		ElementEditorPanel->SetSelectedActor(SelectedActor);
	}
}

void FEasyHouseEditorModeToolkit::AdoptCopiedBuilding(AEHBBuildingActorBase* Building)
{
	if (ToolsetWidget.IsValid()) ToolsetWidget->AdoptCopiedBuilding(Building);
}

void FEasyHouseEditorModeToolkit::SetSelectedFloorSlabCorner(AEHB_FloorSlab* FloorSlab, int32 LoopIndex, int32 PointIndex)
{
	if (!ElementEditorPanel.IsValid() && GElementEditorPanel.IsValid())
	{
		ElementEditorPanel = GElementEditorPanel;
	}
	if (ElementEditorPanel.IsValid())
	{
		ElementEditorPanel->SetSelectedFloorSlabCorner(FloorSlab, LoopIndex, PointIndex);
	}
}

void FEasyHouseEditorModeToolkit::ShutdownElementEditor()
{
	if (!ElementEditorPanel.IsValid() && GElementEditorPanel.IsValid())
	{
		ElementEditorPanel = GElementEditorPanel;
	}
	if (ElementEditorPanel.IsValid())
	{
		ElementEditorPanel->SetSelectedActor(nullptr);
	}

	ElementEditorTab.Reset();
	ElementEditorPanel.Reset();
}

void FEasyHouseEditorModeToolkit::RegisterElementEditorTabSpawner()
{
	RegisterGlobalElementEditorTabSpawner();
	bElementEditorTabSpawnerRegistered = true;
}

void FEasyHouseEditorModeToolkit::UnregisterElementEditorTabSpawner()
{
	bElementEditorTabSpawnerRegistered = false;
}

void FEasyHouseEditorModeToolkit::EnsureElementEditorTab()
{
	RegisterElementEditorTabSpawner();

	if (ElementEditorTab.IsValid() && ElementEditorPanel.IsValid())
	{
		return;
	}

	if (GElementEditorTab.IsValid() && GElementEditorPanel.IsValid())
	{
		ElementEditorTab = GElementEditorTab;
		ElementEditorPanel = GElementEditorPanel;
		return;
	}

	TSharedPtr<SDockTab> InvokedTab = FGlobalTabmanager::Get()->TryInvokeTab(EHBElementEditorTabId, true);
	if (InvokedTab.IsValid())
	{
		ElementEditorTab = InvokedTab;
		ElementEditorPanel = GElementEditorPanel;
	}
}

TSharedRef<SDockTab> FEasyHouseEditorModeToolkit::SpawnElementEditorTab(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> NewTab = SpawnGlobalElementEditorTab(Args);
	ElementEditorTab = NewTab;
	ElementEditorPanel = GElementEditorPanel;
	return NewTab;
}

void FEasyHouseEditorModeToolkit::HandleElementEditorTabClosed(TSharedRef<SDockTab> ClosedTab)
{
	if (ElementEditorTab.Pin() == ClosedTab)
	{
		ElementEditorTab.Reset();
		ElementEditorPanel.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
