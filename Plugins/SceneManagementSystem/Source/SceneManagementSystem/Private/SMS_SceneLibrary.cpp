#include "SMS_SceneLibrary.h"
#include "SMS_SceneSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

USMS_SceneSubsystem* USMS_SceneLibrary::GetSceneSubsystem(const UObject* Context)
{
	UWorld* World=GEngine?GEngine->GetWorldFromContextObject(Context,EGetWorldErrorMode::ReturnNull):nullptr;
	return World?World->GetSubsystem<USMS_SceneSubsystem>():nullptr;
}
USMS_SceneManager* USMS_SceneLibrary::GetSceneManager(const UObject* Context)
{
	auto* Subsystem=GetSceneSubsystem(Context); return Subsystem?Subsystem->GetManager():nullptr;
}
bool USMS_SceneLibrary::SwitchScene(const UObject* Context,FGameplayTag Tag) { auto* Manager=GetSceneManager(Context); return Manager && Manager->SwitchScene(Tag); }
FGameplayTag USMS_SceneLibrary::GetCurrentSceneTag(const UObject* Context) { auto* Manager=GetSceneManager(Context); return Manager?Manager->GetCurrentSceneTag():FGameplayTag(); }
USMS_SceneBase* USMS_SceneLibrary::GetCurrentScene(const UObject* Context) { auto* Manager=GetSceneManager(Context); return Manager?Manager->GetCurrentScene():nullptr; }
FGameplayTag USMS_SceneLibrary::GetPendingSceneTag(const UObject* Context) { auto* Manager=GetSceneManager(Context); return Manager?Manager->GetPendingSceneTag():FGameplayTag(); }
ESMS_SceneTransitionState USMS_SceneLibrary::GetTransitionState(const UObject* Context) { auto* Manager=GetSceneManager(Context); return Manager?Manager->GetTransitionState():ESMS_SceneTransitionState::Idle; }
bool USMS_SceneLibrary::NotifyLeaveCompleted(const UObject* Context,USMS_SceneBase* Scene) { auto* Manager=GetSceneManager(Context); return Manager && Manager->NotifyLeaveCompleted(Scene); }
bool USMS_SceneLibrary::NotifyEnterCompleted(const UObject* Context,USMS_SceneBase* Scene) { auto* Manager=GetSceneManager(Context); return Manager && Manager->NotifyEnterCompleted(Scene); }
