#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/Units/UnitStructs.h"
#include "BattleHUDTypes.generated.h"

UENUM(BlueprintType)
enum class EBattleDrawer : uint8 { None, Member, Vehicle };
UENUM(BlueprintType)
enum class EBattleControlMode : uint8
{
    Member UMETA(DisplayName="成员模式"),
    Squad UMETA(DisplayName="小队模式")
};
UENUM(BlueprintType)
enum class EBattlePosture : uint8 { Standing, Crouched, Prone };
UENUM(BlueprintType)
enum class EBattleCommand : uint8
{
    Stand, Crouch, Prone, Backpack, Stealth, Interact, Pickup, Quick1, Quick2, Quick3, Quick4,
    BoardVehicle, LeaveVehicle, VehicleCargo, LocateVehicle, MemberMode, SquadMode,
    MapFollow, MapLayers, MapZoomIn, MapZoomOut, MapCollapse
};
UENUM(BlueprintType)
enum class EBattleGlyph : uint8
{
    Standing, Crouching, Prone, Backpack, Stealth, Hand, Pickup, Medkit, Grenade, Smoke, Tool,
    Vehicle, Person, Squad, Rifle, Pistol, EmptyHand, Enter, Exit, Cargo, Locate, Plus, Minus, Layers, Chevron
};

/** UI snapshots only. Unit/vehicle authority stays with the existing player managers and inventory plugin. */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FBattleMemberView
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="成员") FGuid UnitId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="成员") FUnitProfile Profile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态") float Health = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态") float Stamina = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态") float Morale = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态") EBattlePosture Posture = EBattlePosture::Standing;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="状态") bool bStealth = false;
    /** Identical non-empty hand item IDs mean one item occupies both hands. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="装备") FGuid LeftItemId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="装备") FGuid RightItemId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="装备") TObjectPtr<UTexture2D> LeftItemImage;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="装备") TObjectPtr<UTexture2D> RightItemImage;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="装备") EBattleGlyph LeftFallback = EBattleGlyph::EmptyHand;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="装备") EBattleGlyph RightFallback = EBattleGlyph::Pistol;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="快捷物品") TArray<int32> QuickItemCounts = {2, 2, 2, 1};
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小地图") FVector2D MapPosition = FVector2D(.5,.5);
    bool HasLinkedHands() const { return LeftItemId.IsValid() && LeftItemId == RightItemId; }
};

USTRUCT(BlueprintType)
struct SILVERCHOIR_API FBattleVehicleView
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="车辆") FGuid VehicleId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="车辆") FText Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="车辆") TObjectPtr<UTexture2D> Image;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="车辆") float Condition = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="车辆") float Fuel = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="车辆") int32 Occupants = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="车辆") int32 Seats = 6;
};

USTRUCT(BlueprintType)
struct SILVERCHOIR_API FBattleSquadView
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队") FGuid SquadId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队") FText Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队") TObjectPtr<UTexture2D> Icon;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队") TArray<FBattleMemberView> Members;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="小队") FBattleVehicleView Vehicle;
};

UCLASS(BlueprintType)
class SILVERCHOIR_API UBattleHUDTestData : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="测试UI") TArray<FBattleSquadView> Squads;
};

USTRUCT(BlueprintType)
struct SILVERCHOIR_API FBattleDockLayout
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="布局") TArray<float> MemberX;
    UPROPERTY(BlueprintReadOnly, Category="布局") float MemberWidth = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="布局") float DrawerX = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="布局") float DrawerWidth = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="布局") float VehicleButtonX = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="布局") float ModeButtonsX = 0.f;
};
