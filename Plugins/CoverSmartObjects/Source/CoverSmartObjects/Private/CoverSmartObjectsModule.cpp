#include "Modules/ModuleManager.h"
#include "CSOFindCoverAsync.h"

class FCoverSmartObjectsModule : public IModuleInterface
{
    virtual void ShutdownModule() override { UCSOFindCoverAsync::ShutdownQueue(); }
};
IMPLEMENT_MODULE(FCoverSmartObjectsModule, CoverSmartObjects)
