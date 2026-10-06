#pragma once

#include "Modules/ModuleManager.h"

class FGSMSystemEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
	void OpenMapEditorTab();
	TSharedRef<class SDockTab> SpawnMapEditorTab(const class FSpawnTabArgs& SpawnTabArgs);
};
