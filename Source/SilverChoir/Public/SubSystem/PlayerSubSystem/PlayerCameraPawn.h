#pragma once
#include "CoreMinimal.h"
#include "FCS_FreeCameraPawn.h"
#include "PlayerCameraPawn.generated.h"

class USIS_UnitInventoryComponent;

/** Player-controlled free camera and its inventory. Layout/configuration remain editable in BP_PlayerPawn. */
UCLASS(Blueprintable, meta=(DisplayName="玩家相机"))
class SILVERCHOIR_API APlayerCameraPawn : public AFCS_FreeCameraPawn
{
    GENERATED_BODY()
public:
    APlayerCameraPawn();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="玩家|背包")
    TObjectPtr<USIS_UnitInventoryComponent> UnitInventoryComponent;

};
