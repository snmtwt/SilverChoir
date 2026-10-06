#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Map/GameMainMap/GameMainMapGameState.h"
#include "GameMainMapLibrary.generated.h"

class AGameMainMapGameMode;
class AGameMainMapPlayerController;
class AGameMainMapPlayerState;
class AFCS_FreeCameraPawn;

/** 从调用者所属世界获取主地图对象；不跨 PIE 世界缓存对象。 */
UCLASS(meta=(DisplayName="主地图函数库"))
class SILVERCHOIR_API UGameMainMapLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="主地图|获取", meta=(WorldContext="WorldContextObject", DisplayName="获取主地图对象"))
    static void GetGameMainMapObjects(const UObject* WorldContextObject, AGameMainMapGameMode*& GameMode,
        AGameMainMapGameState*& GameState, AGameMainMapPlayerController*& PlayerController,
        AGameMainMapPlayerState*& PlayerState, APawn*& PlayerPawn, int32 PlayerIndex = 0);
    UFUNCTION(BlueprintPure, Category="主地图|获取", meta=(WorldContext="WorldContextObject", DisplayName="获取主地图 GameMode"))
    static AGameMainMapGameMode* GetGameMainMapGameMode(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="主地图|获取", meta=(WorldContext="WorldContextObject", DisplayName="获取主地图 GameState"))
    static AGameMainMapGameState* GetGameMainMapGameState(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure, Category="主地图|获取", meta=(WorldContext="WorldContextObject", DisplayName="获取主地图玩家控制器"))
    static AGameMainMapPlayerController* GetGameMainMapPlayerController(const UObject* WorldContextObject, int32 PlayerIndex = 0);
    UFUNCTION(BlueprintPure, Category="主地图|获取", meta=(WorldContext="WorldContextObject", DisplayName="获取主地图玩家状态"))
    static AGameMainMapPlayerState* GetGameMainMapPlayerState(const UObject* WorldContextObject, int32 PlayerIndex = 0);
    UFUNCTION(BlueprintPure, Category="主地图|获取", meta=(WorldContext="WorldContextObject", DisplayName="获取主地图玩家 Pawn"))
    static APawn* GetGameMainMapPlayerPawn(const UObject* WorldContextObject, int32 PlayerIndex = 0);
    UFUNCTION(BlueprintPure, Category="主地图|获取", meta=(WorldContext="WorldContextObject", DisplayName="获取主地图玩家相机"))
    static AFCS_FreeCameraPawn* GetGameMainMapPlayerCamera(const UObject* WorldContextObject, int32 PlayerIndex = 0);
    /** 无主地图 GameState 时返回 None。GameMode 在客户端上可以为空。 */
    UFUNCTION(BlueprintPure, Category="主地图|地图类型", meta=(WorldContext="WorldContextObject", DisplayName="获取当前地图类型"))
    static EGameMainMapType GetCurrentMapType(const UObject* WorldContextObject);
};
