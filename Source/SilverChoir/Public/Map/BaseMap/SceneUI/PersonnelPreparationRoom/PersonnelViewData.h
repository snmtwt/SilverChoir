#pragma once
#include "CoreMinimal.h"
#include "PersonnelViewData.generated.h"
class UTexture2D;

UENUM(BlueprintType)
enum class EPersonnelRosterFilter : uint8 { Standby, All };
UENUM(BlueprintType)
enum class EPersonnelDetailPage : uint8 { Profile, Equipment, Training };
UENUM(BlueprintType)
enum class EPersonnelListDisplay : uint8
{
    Personnel UMETA(DisplayName="人员列表"),
    Warehouse UMETA(DisplayName="仓库列表")
};

/** Presentation data only; replace mock entries through SetPersonnelData later. */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FPersonnelViewData
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FName ID;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UTexture2D> Portrait;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Callsign;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Role;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bStandby = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Level = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Health = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Morale = 80;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Accuracy = 70;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Biography;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText PrimaryWeapon;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Armor;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FText> Inventory;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FText> Skills;
};
