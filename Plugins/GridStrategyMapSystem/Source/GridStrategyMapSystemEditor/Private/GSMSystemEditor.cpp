#include "GridStrategyMapSystemEditor/GSMSystemEditor.h"

#include "GridStrategyMapSystemEditor/SGSMEditorWidget.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FGSMSystemEditorModule"

static const FName GridStrategyMapEditorTabName(TEXT("GridStrategyMapEditor"));

void FGSMSystemEditorModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		GridStrategyMapEditorTabName,
		FOnSpawnTab::CreateRaw(this, &FGSMSystemEditorModule::SpawnMapEditorTab)
	)
	.SetDisplayName(LOCTEXT("GridStrategyMapEditorTabTitle", "网格策略地图编辑器"))
	.SetTooltipText(LOCTEXT("GridStrategyMapEditorTabTooltip", "打开网格策略地图配置编辑器"))
	.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FGSMSystemEditorModule::RegisterMenus));
}

void FGSMSystemEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(GridStrategyMapEditorTabName);
}

void FGSMSystemEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	if (UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools"))
	{
		FToolMenuSection& Section = ToolsMenu->FindOrAddSection("GridStrategyMapSystem");
		Section.Label = LOCTEXT("GridStrategyMapSystemSection", "网格策略地图系统");

		Section.AddMenuEntry(
			"OpenGridStrategyMapEditor",
			LOCTEXT("OpenGridStrategyMapEditor", "网格策略地图编辑器"),
			LOCTEXT("OpenGSMEditorTooltip", "打开地图瓦片预览和配置工具"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FGSMSystemEditorModule::OpenMapEditorTab))
		);
	}
}

void FGSMSystemEditorModule::OpenMapEditorTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(GridStrategyMapEditorTabName);
}

TSharedRef<SDockTab> FGSMSystemEditorModule::SpawnMapEditorTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("GridStrategyMapEditorDockTabLabel", "网格策略地图编辑器"))
		[
			SNew(SGSMEditorWidget)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGSMSystemEditorModule, GridStrategyMapSystemEditor)
