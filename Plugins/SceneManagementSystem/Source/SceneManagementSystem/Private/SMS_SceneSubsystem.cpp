#include "SMS_SceneSubsystem.h"
#include "SMS_SceneManager.h"
#include "SMS_SceneSettings.h"

bool USMS_SceneSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType==EWorldType::Game || WorldType==EWorldType::PIE;
}
void USMS_SceneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const auto& Configured=GetDefault<USMS_SceneSettings>()->ManagerClass;
	UClass* Class=Configured.IsNull()?USMS_SceneManager::StaticClass():Configured.LoadSynchronous();
	if (!Class || !Class->IsChildOf(USMS_SceneManager::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists))
	{
		InitializationError=NSLOCTEXT("SMS","InvalidManager","场景管理类无法加载或不是有效的管理类。请检查项目设置。");
		return;
	}
	Manager=NewObject<USMS_SceneManager>(this,Class);
	if (!Manager->InitializeManager()) InitializationError=Manager->GetLastError();
}
void USMS_SceneSubsystem::Deinitialize()
{
	if (Manager) Manager->ShutdownManager();
	Manager=nullptr;
	Super::Deinitialize();
}
bool USMS_SceneSubsystem::IsReady() const { return Manager && Manager->IsReady(); }
