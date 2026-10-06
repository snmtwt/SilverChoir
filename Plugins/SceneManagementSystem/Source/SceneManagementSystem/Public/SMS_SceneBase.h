#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "SMS_SceneBase.generated.h"

class USMS_SceneManager;

/** 地图内的功能场景，不负责加载 UWorld。子蓝图在类默认值中指定唯一 Tag。 */
UCLASS(Abstract, BlueprintType, Blueprintable, meta=(DisplayName="功能场景基类"))
class SCENEMANAGEMENTSYSTEM_API USMS_SceneBase : public UObject
{
	GENERATED_BODY()
public:
	virtual UWorld* GetWorld() const override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="场景", meta=(DisplayName="场景标记")) FGameplayTag SceneTag;
	/** true：进入已完成；false：稍后由此实例调用“通知进入场景完成”。 */
	UFUNCTION(BlueprintNativeEvent, Category="场景", meta=(DisplayName="进入场景")) bool EnterScene(FGameplayTag PreviousSceneTag);
	virtual bool EnterScene_Implementation(FGameplayTag PreviousSceneTag);
	/** true：退出已完成；false：保持当前实例，等待“通知离开场景完成”。 */
	UFUNCTION(BlueprintNativeEvent, Category="场景", meta=(DisplayName="离开场景")) bool LeaveScene(FGameplayTag NextSceneTag);
	virtual bool LeaveScene_Implementation(FGameplayTag NextSceneTag);
	UFUNCTION(BlueprintCallable, Category="场景", meta=(DisplayName="通知进入场景完成")) bool NotifyEnterCompleted();
	UFUNCTION(BlueprintCallable, Category="场景", meta=(DisplayName="通知离开场景完成")) bool NotifyLeaveCompleted();
private:
	friend class USMS_SceneManager;
	TWeakObjectPtr<USMS_SceneManager> Manager;
};
