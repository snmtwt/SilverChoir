#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SMS_SceneManager.h"
#include "SMS_SceneLibrary.generated.h"
class USMS_SceneSubsystem;
class USMS_SceneBase;

UCLASS(meta=(DisplayName="场景函数库"))
class SCENEMANAGEMENTSYSTEM_API USMS_SceneLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category="场景", meta=(WorldContext="WorldContextObject", DisplayName="获取场景子系统")) static USMS_SceneSubsystem* GetSceneSubsystem(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="场景", meta=(WorldContext="WorldContextObject", DisplayName="获取场景管理器")) static USMS_SceneManager* GetSceneManager(const UObject* WorldContextObject);
	/** true 表示接受请求；等待状态与完成事件由管理器提供。 */
	UFUNCTION(BlueprintCallable, Category="场景", meta=(WorldContext="WorldContextObject", DisplayName="按标记切换场景")) static bool SwitchScene(const UObject* WorldContextObject, FGameplayTag TargetSceneTag);
	UFUNCTION(BlueprintPure, Category="场景", meta=(WorldContext="WorldContextObject", DisplayName="获取当前场景标记")) static FGameplayTag GetCurrentSceneTag(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="场景", meta=(WorldContext="WorldContextObject", DisplayName="获取当前场景对象")) static USMS_SceneBase* GetCurrentScene(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="场景", meta=(WorldContext="WorldContextObject", DisplayName="获取待进入场景标记")) static FGameplayTag GetPendingSceneTag(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="场景", meta=(WorldContext="WorldContextObject", DisplayName="获取场景切换状态")) static ESMS_SceneTransitionState GetTransitionState(const UObject* WorldContextObject);
	UFUNCTION(BlueprintCallable, Category="场景", meta=(WorldContext="WorldContextObject", DisplayName="通知指定场景离开完成")) static bool NotifyLeaveCompleted(const UObject* WorldContextObject, USMS_SceneBase* Scene);
	UFUNCTION(BlueprintCallable, Category="场景", meta=(WorldContext="WorldContextObject", DisplayName="通知指定场景进入完成")) static bool NotifyEnterCompleted(const UObject* WorldContextObject, USMS_SceneBase* Scene);
};
