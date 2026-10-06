#pragma once

#include "Modules/ModuleManager.h"

class FGSMSystemModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
