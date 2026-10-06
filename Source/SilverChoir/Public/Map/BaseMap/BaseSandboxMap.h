#pragma once

#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "BaseSandboxMap.generated.h"

/** Placed base sandbox with its own transient runtime map, independent of other GSM maps. */
UCLASS(Blueprintable, meta=(DisplayName="基地瓦片沙盘"))
class SILVERCHOIR_API ABaseSandboxMap : public AGSMMap3D
{
    GENERATED_BODY()
public:
    ABaseSandboxMap();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="基地沙盘", meta=(DisplayName="启用沙盘鼠标操作"))
    bool bEnableSandboxMouseNavigation = true;

private:
    FGuid OwnedMapGuid;
};
