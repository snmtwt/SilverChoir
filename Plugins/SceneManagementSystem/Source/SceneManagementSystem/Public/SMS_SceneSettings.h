#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SMS_SceneSettings.generated.h"
class USMS_SceneManager;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="场景管理系统"))
class SCENEMANAGEMENTSYSTEM_API USMS_SceneSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	USMS_SceneSettings();
	/** 在管理类子蓝图的默认值中配置 SceneClasses。留空使用原生空配置。 */
	UPROPERTY(Config, EditAnywhere, Category="场景", meta=(DisplayName="场景管理类", AllowAbstract="false")) TSoftClassPtr<USMS_SceneManager> ManagerClass;
};
