#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FCS_FreeCameraPawn.h"
#include "PlayerLibrary.generated.h"

class UPlayerManagerBase;
class UPlayerSubsystem;
class APawn;
class APlayerCameraPawn;

/** 蓝图调用入口：解析世界上下文并转发到玩家子系统，不保存运行时数据。 */
UCLASS(meta = (DisplayName = "玩家蓝图函数库"))
class SILVERCHOIR_API UPlayerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns a copy translated by LocationOffset. Angles, arm length and all speeds are preserved.
	 * Translation only: apply once to a map-local authored state, not to an already-world-space capture. */
	UFUNCTION(BlueprintPure, Category="玩家|相机", meta=(DisplayName="为相机状态添加位置偏移", AutoCreateRefTerm="LocationOffset"))
	static FFCS_CameraState AddCameraStateLocationOffset(const FFCS_CameraState& CameraState, FVector LocationOffset);

	UFUNCTION(BlueprintPure, Category = "玩家", meta = (WorldContext = "WorldContextObject", DisplayName = "获取玩家子系统"))
	static UPlayerSubsystem* GetPlayerSubsystem(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "玩家", meta = (WorldContext = "WorldContextObject", DisplayName = "获取玩家处理类"))
	static UPlayerManagerBase* GetPlayerManager(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="玩家", meta=(WorldContext="WorldContextObject", DisplayName="获取玩家 Pawn"))
	static APawn* GetPlayerPawn(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="玩家", meta=(WorldContext="WorldContextObject", DisplayName="获取玩家相机"))
	static APlayerCameraPawn* GetPlayerCamera(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="玩家", meta=(WorldContext="WorldContextObject", DisplayName="获取玩家 Pawn 类"))
	static TSubclassOf<APawn> GetPlayerPawnClass(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "玩家", meta = (WorldContext = "WorldContextObject", DisplayName = "玩家系统是否就绪"))
	static bool IsPlayerSystemReady(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "玩家|游戏", meta = (WorldContext = "WorldContextObject", DisplayName = "初始化游戏"))
	static bool InitializeGame(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "玩家|游戏", meta = (WorldContext = "WorldContextObject", DisplayName = "加载游戏"))
	static bool LoadGame(const UObject* WorldContextObject, const FString& SlotName, int32 UserIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "玩家|游戏", meta = (WorldContext = "WorldContextObject", DisplayName = "保存游戏"))
	static bool SaveGame(const UObject* WorldContextObject, const FString& SlotName, int32 UserIndex = 0);


	UFUNCTION(BlueprintCallable, Category = "玩家|游戏", meta = (WorldContext = "WorldContextObject", DisplayName = "加载新游戏数据"))
	static void LoadNewGameData(const UObject* WorldContextObject);
};
