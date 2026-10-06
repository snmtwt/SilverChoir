#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EHBRoomRuntimeSubsystem.generated.h"

class AEHBBuildingActorBase;
class UEHBRoomExplorationSaveGame;

UENUM(BlueprintType)
enum class EEHBRoomLocationStatus : uint8 { Outside, Inside, Ambiguous, Invalid };

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBRoomAddress
{
 GENERATED_BODY()
 UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="EHB|Rooms") FGuid BuildingGuid;
 UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="EHB|Rooms") FGuid RoomGuid;
 bool operator==(const FEHBRoomAddress& Other) const { return BuildingGuid==Other.BuildingGuid&&RoomGuid==Other.RoomGuid; }
 friend uint32 GetTypeHash(const FEHBRoomAddress& Value) { return HashCombine(GetTypeHash(Value.BuildingGuid),GetTypeHash(Value.RoomGuid)); }
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBRoomLocation
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly, Category="EHB|Rooms") EEHBRoomLocationStatus Status=EEHBRoomLocationStatus::Outside;
 UPROPERTY(BlueprintReadOnly, Category="EHB|Rooms") TObjectPtr<AEHBBuildingActorBase> Building=nullptr;
 UPROPERTY(BlueprintReadOnly, Category="EHB|Rooms") FEHBRoomAddress Identity;
 UPROPERTY(BlueprintReadOnly, Category="EHB|Rooms") int32 FloorIndex=INDEX_NONE;
 bool operator==(const FEHBRoomLocation& Other) const { return Status==Other.Status&&Building==Other.Building&&Identity==Other.Identity&&FloorIndex==Other.FloorIndex; }
};

/** Shared building registry and per-observer exploration; no actor scan per unit tick. */
UCLASS()
class EASYHOUSEBUILDER_API UEHBRoomRuntimeSubsystem : public UWorldSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void PostInitialize() override;
 virtual void Deinitialize() override;
 UFUNCTION(BlueprintCallable, Category="EHB|Rooms") FEHBRoomLocation QueryLocation(FVector WorldLocation,const FEHBRoomLocation& Previous);
 UFUNCTION(BlueprintCallable, Category="EHB|Rooms") void MarkExplored(FName Observer,const FEHBRoomAddress& Room);
 UFUNCTION(BlueprintPure, Category="EHB|Rooms") bool IsExplored(FName Observer,const FEHBRoomAddress& Room) const;
 UFUNCTION(BlueprintCallable, Category="EHB|Rooms") TArray<FEHBRoomAddress> ExportExploration(FName Observer) const;
 UFUNCTION(BlueprintCallable, Category="EHB|Rooms") void ImportExploration(FName Observer,const TArray<FEHBRoomAddress>& Rooms,bool bReplace);
 UFUNCTION(BlueprintCallable, Category="EHB|Rooms") void ClearExploration(FName Observer);
 UFUNCTION(BlueprintCallable,Category="EHB|Rooms|Save") UEHBRoomExplorationSaveGame* CreateExplorationSave() const;
 UFUNCTION(BlueprintCallable,Category="EHB|Rooms|Save") bool RestoreExplorationSave(const UEHBRoomExplorationSaveGame* Save,bool bReplaceExisting,FName& Status);
 UFUNCTION(BlueprintCallable,Category="EHB|Rooms|Save") bool SaveExplorationToSlot(const FString& SlotName,int32 UserIndex,FName& Status) const;
 UFUNCTION(BlueprintCallable,Category="EHB|Rooms|Save") bool LoadExplorationFromSlot(const FString& SlotName,int32 UserIndex,bool bReplaceExisting,FName& Status);
 void RegisterBuilding(AEHBBuildingActorBase* Building);
private:
 struct FEntry { TWeakObjectPtr<AEHBBuildingActorBase> Building; FBox Bounds=FBox(ForceInit); int32 Revision=INDEX_NONE; };
 TArray<FEntry> Buildings;
 TMap<FName,TSet<FEHBRoomAddress>> Exploration;
 FDelegateHandle SpawnHandle;
 void OnActorSpawned(AActor* Actor);
};
