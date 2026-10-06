#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PlayerSquadSubsystem.generated.h"

class UPlayerSquadManagerBase;

/** 玩家小队协调层。先初始化玩家单位子系统，再创建唯一的小队处理类。 */
UCLASS(BlueprintType, NotBlueprintable, meta = (DisplayName = "玩家小队子系统"))
class SILVERCHOIR_API UPlayerSquadSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintPure, Category = "玩家小队", meta = (DisplayName = "玩家小队系统是否就绪"))
    bool IsReady() const;

    UFUNCTION(BlueprintPure, Category = "玩家小队", meta = (DisplayName = "获取玩家小队处理类"))
    UPlayerSquadManagerBase* GetManager() const;

    UFUNCTION(BlueprintPure, Category = "玩家小队", meta = (DisplayName = "获取玩家小队系统初始化错误"))
    FText GetInitializationError() const { return InitializationError; }

private:
    UPROPERTY(Transient)
    TObjectPtr<UPlayerSquadManagerBase> Manager;

    UPROPERTY(Transient)
    FText InitializationError;
};
