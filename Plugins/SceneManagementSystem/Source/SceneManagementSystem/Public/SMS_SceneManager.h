#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "SMS_SceneManager.generated.h"

class USMS_SceneBase;

UENUM(BlueprintType)
enum class ESMS_SceneTransitionState : uint8
{
	Idle UMETA(DisplayName="空闲"),
	Leaving UMETA(DisplayName="等待离开完成"),
	Entering UMETA(DisplayName="等待进入完成")
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSMS_SceneChanged,FGameplayTag,PreviousSceneTag,FGameplayTag,CurrentSceneTag);

/** 处理类：持有场景配置、当前实例、待进入实例，以及切换状态机。 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="场景管理类", ShowWorldContextPin))
class SCENEMANAGEMENTSYSTEM_API USMS_SceneManager : public UObject
{
	GENERATED_BODY()
public:
	virtual UWorld* GetWorld() const override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="场景配置", meta=(DisplayName="场景类列表", AllowAbstract="false")) TArray<TSubclassOf<USMS_SceneBase>> SceneClasses;
	bool InitializeManager();
	void ShutdownManager();
	/** 返回请求是否被接受，不代表异步进入已完成；忙碌时拒绝新请求。 */
	UFUNCTION(BlueprintCallable, Category="场景", meta=(DisplayName="切换场景")) bool SwitchScene(FGameplayTag TargetSceneTag);
	/** 必须传入当次正在退出的实例；过期实例或重复通知返回 false。 */
	UFUNCTION(BlueprintCallable, Category="场景", meta=(DisplayName="通知离开场景完成")) bool NotifyLeaveCompleted(USMS_SceneBase* Scene);
	UFUNCTION(BlueprintCallable, Category="场景", meta=(DisplayName="通知进入场景完成")) bool NotifyEnterCompleted(USMS_SceneBase* Scene);
	UFUNCTION(BlueprintPure, Category="场景") USMS_SceneBase* GetCurrentScene() const { return CurrentScene; }
	UFUNCTION(BlueprintPure, Category="场景") FGameplayTag GetCurrentSceneTag() const;
	UFUNCTION(BlueprintPure, Category="场景") FGameplayTag GetPendingSceneTag() const;
	UFUNCTION(BlueprintPure, Category="场景") ESMS_SceneTransitionState GetTransitionState() const { return State; }
	UFUNCTION(BlueprintPure, Category="场景") bool IsReady() const { return bInitialized; }
	UFUNCTION(BlueprintPure, Category="场景") FText GetLastError() const { return LastError; }
	UPROPERTY(BlueprintAssignable, Category="场景", meta=(DisplayName="场景切换完成")) FSMS_SceneChanged OnSceneChanged;
private:
	UPROPERTY(Transient) TMap<FGameplayTag,TSubclassOf<USMS_SceneBase>> Registry;
	UPROPERTY(Transient) TObjectPtr<USMS_SceneBase> CurrentScene;
	UPROPERTY(Transient) TObjectPtr<USMS_SceneBase> PendingScene;
	UPROPERTY(Transient) FText LastError;
	ESMS_SceneTransitionState State=ESMS_SceneTransitionState::Idle;
	FGameplayTag PreviousTag;
	bool bInitialized=false;
	bool bDispatchingCallback=false;
	bool bCompletionSignalled=false;
	bool Reject(const FText& Error);
	void EnterPendingScene();
	void FinishEnter();
};
