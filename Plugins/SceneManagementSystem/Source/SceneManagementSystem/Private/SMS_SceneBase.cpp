#include "SMS_SceneBase.h"
#include "SMS_SceneManager.h"

UWorld* USMS_SceneBase::GetWorld() const
{
	return !HasAnyFlags(RF_ClassDefaultObject) && Manager.IsValid() ? Manager->GetWorld() : nullptr;
}
bool USMS_SceneBase::EnterScene_Implementation(FGameplayTag PreviousSceneTag) { return true; }
bool USMS_SceneBase::LeaveScene_Implementation(FGameplayTag NextSceneTag) { return true; }
bool USMS_SceneBase::NotifyEnterCompleted() { return Manager.IsValid() && Manager->NotifyEnterCompleted(this); }
bool USMS_SceneBase::NotifyLeaveCompleted() { return Manager.IsValid() && Manager->NotifyLeaveCompleted(this); }
