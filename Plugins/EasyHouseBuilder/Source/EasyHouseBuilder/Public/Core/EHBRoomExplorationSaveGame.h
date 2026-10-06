#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Core/EHBRoomRuntimeSubsystem.h"
#include "EHBRoomExplorationSaveGame.generated.h"

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBObserverExploration
{
 GENERATED_BODY()
 UPROPERTY(SaveGame,EditAnywhere,BlueprintReadWrite,Category="EHB|Rooms") FName Observer;
 UPROPERTY(SaveGame,EditAnywhere,BlueprintReadWrite,Category="EHB|Rooms") TArray<FEHBRoomAddress> Rooms;
};

/** Actor-free snapshot; the subsystem validates the entire snapshot before applying it. */
UCLASS(BlueprintType)
class EASYHOUSEBUILDER_API UEHBRoomExplorationSaveGame : public USaveGame
{
 GENERATED_BODY()
public:
 UPROPERTY(SaveGame,VisibleAnywhere,BlueprintReadOnly,Category="EHB|Rooms") int32 SchemaVersion=1;
 UPROPERTY(SaveGame,EditAnywhere,BlueprintReadWrite,Category="EHB|Rooms") TArray<FEHBObserverExploration> Observers;
};
