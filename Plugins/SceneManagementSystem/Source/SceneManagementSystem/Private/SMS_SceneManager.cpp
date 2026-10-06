#include "SMS_SceneManager.h"
#include "SMS_SceneBase.h"
#include "Engine/World.h"

UWorld* USMS_SceneManager::GetWorld() const
{
	return HasAnyFlags(RF_ClassDefaultObject) ? nullptr : GetTypedOuter<UWorld>();
}
bool USMS_SceneManager::Reject(const FText& Error) { LastError=Error; return false; }
bool USMS_SceneManager::InitializeManager()
{
	if (bInitialized) return Reject(NSLOCTEXT("SMS","AlreadyInitialized","场景管理器已初始化。"));
	TMap<FGameplayTag,TSubclassOf<USMS_SceneBase>> Validated;
	for (const auto& Class:SceneClasses)
	{
		if (!Class || Class->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists))
			return Reject(NSLOCTEXT("SMS","InvalidClass","场景类为空、抽象或已失效。"));
		const auto* Defaults=Class->GetDefaultObject<USMS_SceneBase>();
		if (!Defaults->SceneTag.IsValid() || Validated.Contains(Defaults->SceneTag))
			return Reject(FText::Format(NSLOCTEXT("SMS","InvalidTag","场景类 {0} 的 GameplayTag 为空或重复。"),FText::FromString(Class->GetName())));
		Validated.Add(Defaults->SceneTag,Class);
	}
	Registry=MoveTemp(Validated); LastError=FText::GetEmpty(); bInitialized=true;
	return true;
}
void USMS_SceneManager::ShutdownManager()
{
	bInitialized=false;
	if (CurrentScene) CurrentScene->Manager.Reset();
	if (PendingScene) PendingScene->Manager.Reset();
	CurrentScene=nullptr; PendingScene=nullptr; Registry.Reset(); PreviousTag=FGameplayTag();
	State=ESMS_SceneTransitionState::Idle; bCompletionSignalled=false; OnSceneChanged.Clear();
}
FGameplayTag USMS_SceneManager::GetCurrentSceneTag() const { return CurrentScene?CurrentScene->SceneTag:FGameplayTag(); }
FGameplayTag USMS_SceneManager::GetPendingSceneTag() const { return PendingScene?PendingScene->SceneTag:FGameplayTag(); }
bool USMS_SceneManager::SwitchScene(FGameplayTag TargetSceneTag)
{
	if (!bInitialized) return Reject(NSLOCTEXT("SMS","NotReady","场景管理器尚未就绪。"));
	if (State!=ESMS_SceneTransitionState::Idle || bDispatchingCallback)
		return Reject(NSLOCTEXT("SMS","Busy","场景正在切换或执行生命周期回调，请等待完成后重试。"));
	const auto* Class=Registry.Find(TargetSceneTag);
	if (!Class) return Reject(NSLOCTEXT("SMS","UnknownTag","未配置此 GameplayTag 对应的场景类。"));
	LastError=FText::GetEmpty();
	if (CurrentScene && GetCurrentSceneTag()==TargetSceneTag) return true;
	PendingScene=NewObject<USMS_SceneBase>(this,Class->Get());
	if (!PendingScene) return Reject(NSLOCTEXT("SMS","CreateFailed","无法创建目标场景对象。"));
	PendingScene->Manager=this;
	PreviousTag=GetCurrentSceneTag();
	if (!CurrentScene) { EnterPendingScene(); return true; }
	State=ESMS_SceneTransitionState::Leaving; bCompletionSignalled=false;
	bool Complete=false;
	{
		TGuardValue<bool> Guard(bDispatchingCallback,true);
		Complete=CurrentScene->LeaveScene(TargetSceneTag);
	}
	if (bInitialized && State==ESMS_SceneTransitionState::Leaving && (Complete || bCompletionSignalled)) EnterPendingScene();
	return true;
}
bool USMS_SceneManager::NotifyLeaveCompleted(USMS_SceneBase* Scene)
{
	if (!bInitialized || State!=ESMS_SceneTransitionState::Leaving || !Scene || Scene!=CurrentScene || bCompletionSignalled) return false;
	bCompletionSignalled=true;
	// A Blueprint may notify synchronously inside LeaveScene. Continue only after it returns.
	if (!bDispatchingCallback) EnterPendingScene();
	return true;
}
void USMS_SceneManager::EnterPendingScene()
{
	if (!bInitialized || !PendingScene) return;
	if (CurrentScene) CurrentScene->Manager.Reset();
	CurrentScene=PendingScene; PendingScene=nullptr;
	State=ESMS_SceneTransitionState::Entering; bCompletionSignalled=false;
	bool Complete=false;
	{
		TGuardValue<bool> Guard(bDispatchingCallback,true);
		Complete=CurrentScene->EnterScene(PreviousTag);
	}
	if (bInitialized && State==ESMS_SceneTransitionState::Entering && (Complete || bCompletionSignalled)) FinishEnter();
}
bool USMS_SceneManager::NotifyEnterCompleted(USMS_SceneBase* Scene)
{
	if (!bInitialized || State!=ESMS_SceneTransitionState::Entering || !Scene || Scene!=CurrentScene || bCompletionSignalled) return false;
	bCompletionSignalled=true;
	if (!bDispatchingCallback) FinishEnter();
	return true;
}
void USMS_SceneManager::FinishEnter()
{
	State=ESMS_SceneTransitionState::Idle; bCompletionSignalled=false;
	TGuardValue<bool> Guard(bDispatchingCallback,true);
	OnSceneChanged.Broadcast(PreviousTag,GetCurrentSceneTag());
}
