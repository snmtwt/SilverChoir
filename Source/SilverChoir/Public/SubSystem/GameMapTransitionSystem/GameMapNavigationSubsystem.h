#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MTS_SubMapSubsystem.h"
#include "GameMapNavigationSubsystem.generated.h"

/** Recover navigation when the first bounds arrive through an MTS streamed level. */
UCLASS()
class SILVERCHOIR_API UGameMapNavigationSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
    UFUNCTION() void HandleMapState(const FMTS_SubMapInfo& Map,const FText& Error);
    UFUNCTION() void HandleMapVisibility(const FMTS_SubMapInfo& Map);
    void RefreshMissingNavigation(const FMTS_SubMapInfo& Map);
};
