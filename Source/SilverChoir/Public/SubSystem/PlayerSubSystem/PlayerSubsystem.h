#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PlayerSubsystem.generated.h"

class UPlayerManagerBase;
class APawn;
class APlayerCameraPawn;

/** 系统协调层：随 GameInstance 跨关卡存在，拥有处理类并统一校验调用。 */
UCLASS(BlueprintType, NotBlueprintable, meta = (DisplayName = "玩家子系统"))
class SILVERCHOIR_API UPlayerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 就绪仅表示处理类已创建，不表示已开始新游戏或读取存档。 */
	UFUNCTION(BlueprintPure, Category = "玩家", meta = (DisplayName = "玩家系统是否就绪"))
	bool IsReady() const;

	UFUNCTION(BlueprintPure, Category = "玩家", meta = (DisplayName = "获取玩家系统初始化错误"))
	FText GetInitializationError() const { return InitializationError; }

	UFUNCTION(BlueprintPure, Category = "玩家", meta = (DisplayName = "获取玩家处理类"))
	UPlayerManagerBase* GetManager() const;
	UFUNCTION(BlueprintPure, Category="玩家", meta=(DisplayName="获取玩家 Pawn 类"))
	TSubclassOf<APawn> GetPlayerPawnClass() const;
	/** 按当前 World 查询，不持有跨 OpenLevel 的 Pawn 引用；主菜单中允许为空。 */
	UFUNCTION(BlueprintPure, Category="玩家", meta=(DisplayName="获取玩家 Pawn"))
	APawn* GetPlayerPawn() const;
	UFUNCTION(BlueprintPure, Category="玩家", meta=(DisplayName="获取玩家相机"))
	APlayerCameraPawn* GetPlayerCamera() const;

	UFUNCTION(BlueprintCallable, Category = "玩家|游戏", meta = (DisplayName = "初始化游戏"))
	bool InitializeGame();

	UFUNCTION(BlueprintCallable, Category = "玩家|游戏", meta = (DisplayName = "加载游戏"))
	bool LoadGame(const FString& SlotName, int32 UserIndex = 0);

	UFUNCTION(BlueprintCallable, Category = "玩家|游戏", meta = (DisplayName = "保存游戏"))
	bool SaveGame(const FString& SlotName, int32 UserIndex = 0);


	UFUNCTION(BlueprintCallable, Category = "玩家|游戏", meta = (DisplayName = "加载新游戏数据"))
	void LoadNewGameData();
private:
	UPROPERTY(Transient)
	TObjectPtr<UPlayerManagerBase> Manager;

	UPROPERTY(Transient)
	FText InitializationError;

	bool bExecutingGameOperation = false;
};
