#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Structs/SIS_ItemStruct.h"
#include "PlayerManagerBase.generated.h"

class USIS_UnitInventoryComponent;

/** 蓝图不支持直接反射嵌套数组，使用结构体包装每页的物品数组。 */
USTRUCT(BlueprintType)
struct SILVERCHOIR_API FPlayerInventoryPage
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="玩家|库存") TArray<FSIS_ItemData> Items;
};

/** 业务处理层：由子系统持有，供 C++ 或蓝图子类扩展玩家数据与规则。 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "玩家处理类基类"))
class SILVERCHOIR_API UPlayerManagerBase : public UObject
{
	GENERATED_BODY()

public:
    UPlayerManagerBase();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, SaveGame, Category="玩家|库存", meta=(DisplayName="库存页数据"))
    TArray<FPlayerInventoryPage> InventoryPages;
    UPROPERTY(BlueprintReadOnly, SaveGame, Category="玩家|库存", meta=(DisplayName="当前装载库存页")) int32 ActiveInventoryPage=0;
    UFUNCTION(BlueprintPure, Category="玩家|库存", meta=(DisplayName="读取库存页物品")) bool ReadInventoryPage(int32 PageIndex, TArray<FSIS_ItemData>& Items) const;
    UFUNCTION(BlueprintCallable, Category="玩家|库存", meta=(DisplayName="保存库存页物品")) bool WriteInventoryPage(int32 PageIndex, const TArray<FSIS_ItemData>& Items);
    UFUNCTION(BlueprintCallable, Category="玩家|库存", meta=(DisplayName="记录当前装载库存页")) bool SetActiveInventoryPage(int32 PageIndex);
    /** 状态记录节点；物品读取和加载仍在蓝图中执行。 */
    UFUNCTION(BlueprintPure, Category="玩家|库存", meta=(DisplayName="库存组件已初始化库存页"))
    bool IsInventoryPageInitialized(USIS_UnitInventoryComponent* InventoryComponent) const;
    UFUNCTION(BlueprintCallable, Category="玩家|库存", meta=(DisplayName="标记库存页初始化完成"))
    void MarkInventoryPageInitialized(USIS_UnitInventoryComponent* InventoryComponent);
    /** 同一局内重新开始游戏或读档后调用，下一次打开仓库重新读取页数据。 */
    UFUNCTION(BlueprintCallable, Category="玩家|库存", meta=(DisplayName="重置库存页初始化状态"))
    void ResetInventoryPageInitialization();
	virtual UWorld* GetWorld() const override;

	/** 显式开始新游戏的扩展点；创建子系统时不会自动调用。 */
	UFUNCTION(BlueprintNativeEvent, Category = "玩家|游戏", meta = (DisplayName = "初始化游戏"))
	bool InitializeGame();
	virtual bool InitializeGame_Implementation();

	/** 同步存档扩展点；基础实现返回 false，子类实现实际读取。 */
	UFUNCTION(BlueprintNativeEvent, Category = "玩家|游戏", meta = (DisplayName = "加载游戏"))
	bool LoadGame(const FString& SlotName, int32 UserIndex);
	virtual bool LoadGame_Implementation(const FString& SlotName, int32 UserIndex);

	/** 同步存档扩展点；基础实现返回 false，子类实现实际写入。 */
	UFUNCTION(BlueprintNativeEvent, Category = "玩家|游戏", meta = (DisplayName = "保存游戏"))
	bool SaveGame(const FString& SlotName, int32 UserIndex);
	virtual bool SaveGame_Implementation(const FString& SlotName, int32 UserIndex);


	UFUNCTION(BlueprintNativeEvent, Category = "玩家|游戏", meta = (DisplayName = "加载新游戏数据"))
	void LoadNewGameData();
	void LoadNewGameData_Implementation();
private:
    UPROPERTY(Transient)
    TWeakObjectPtr<USIS_UnitInventoryComponent> InitializedInventoryComponent;
};
