#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EHBLocalBuildingViewComponent.generated.h"

class APlayerController;
class UEHBRoomTrackerComponent;
class UPrimitiveComponent;

/** One instance on each local PlayerController. Changes only that player's view
 * hidden-component list; never actor visibility, collision, or replication. */
UCLASS(ClassGroup=(EasyHouseBuilder), meta=(BlueprintSpawnableComponent))
class EASYHOUSEBUILDER_API UEHBLocalBuildingViewComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEHBLocalBuildingViewComponent();
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EHB|View") bool bViewEnabled=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EHB|View") bool bHideHigherFloors=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EHB|View") bool bHideRoofs=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="EHB|View") bool bHideCurrentCeiling=true;
 UPROPERTY(Transient, BlueprintReadOnly, Category="EHB|View") FName LastStatus;
 UPROPERTY(Transient, BlueprintReadOnly, Category="EHB|View") int32 HiddenElementCount=0;

 /** Unit must already have a Room Tracker. Null clears the target and restores the view. */
 UFUNCTION(BlueprintCallable, Category="EHB|View") bool SetTrackedUnit(AActor* Unit);
 UFUNCTION(BlueprintCallable, Category="EHB|View") void RefreshView();
 /** Clears only entries owned by this component. Does not forget the target. */
 UFUNCTION(BlueprintCallable, Category="EHB|View") void ResetView();
 virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
 virtual void Deactivate() override;
 virtual void OnUnregister() override;
protected:
 virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
 TWeakObjectPtr<UEHBRoomTrackerComponent> Tracker;
 TWeakObjectPtr<APlayerController> AppliedController;
 TSet<TWeakObjectPtr<UPrimitiveComponent>> AddedHiddenComponents;
 void ApplyHiddenComponents(APlayerController* Controller, const TSet<TWeakObjectPtr<UPrimitiveComponent>>& Desired);
};
