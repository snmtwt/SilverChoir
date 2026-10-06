#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SMS_SceneSubsystem.generated.h"
class USMS_SceneManager;

/** 随地图 World 存活；流式子地图共享同一管理器，OpenLevel 时释放。 */
UCLASS(BlueprintType, meta=(DisplayName="场景管理子系统"))
class SCENEMANAGEMENTSYSTEM_API USMS_SceneSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	UFUNCTION(BlueprintPure, Category="场景") USMS_SceneManager* GetManager() const { return Manager; }
	UFUNCTION(BlueprintPure, Category="场景") bool IsReady() const;
	UFUNCTION(BlueprintPure, Category="场景") FText GetInitializationError() const { return InitializationError; }
private:
	UPROPERTY(Transient) TObjectPtr<USMS_SceneManager> Manager;
	UPROPERTY(Transient) FText InitializationError;
};
