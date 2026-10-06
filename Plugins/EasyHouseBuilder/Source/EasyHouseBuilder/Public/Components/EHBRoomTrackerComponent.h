#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/EHBRoomRuntimeSubsystem.h"
#include "EHBRoomTrackerComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEHBTrackedRoomChanged,const FEHBRoomLocation&,Previous,const FEHBRoomLocation&,Current);

DECLARE_MULTICAST_DELEGATE_TwoParams(FEHBTrackedRoomChangedNative,const FEHBRoomLocation&,const FEHBRoomLocation&);

/** Attach to a unit. Local room state only: visibility/collision and replication remain caller-owned. */
UCLASS(ClassGroup=(EasyHouseBuilder),meta=(BlueprintSpawnableComponent))
class EASYHOUSEBUILDER_API UEHBRoomTrackerComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEHBRoomTrackerComponent();
 FEHBTrackedRoomChangedNative OnRoomChangedNative;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="EHB|Rooms",meta=(ClampMin="0",Units="s")) float TransitionDelay=0.15f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="EHB|Rooms") FVector LocalProbeOffset=FVector::ZeroVector;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="EHB|Rooms") FName ExplorationObserver=TEXT("Player0");
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="EHB|Rooms") bool bRememberVisitedRooms=true;
 UPROPERTY(Transient,BlueprintReadOnly,Category="EHB|Rooms") FEHBRoomLocation CurrentRoom;
 UPROPERTY(BlueprintAssignable,Category="EHB|Rooms") FEHBTrackedRoomChanged OnRoomChanged;
 /** Immediate bypasses transition delay, e.g. after teleport. Otherwise DeltaSeconds advances a candidate. */
 UFUNCTION(BlueprintCallable,Category="EHB|Rooms") void RefreshRoom(float DeltaSeconds=0,bool bImmediate=true);
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
protected:
 virtual void BeginPlay() override;
private:
 UPROPERTY(Transient) FEHBRoomLocation Pending;
 float PendingSeconds=0;
};
