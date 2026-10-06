#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WarehousePageSession.generated.h"

class UPlayerManagerBase;
class USIS_UnitInventoryComponent;
class USIS_PlayerInventoryManager;
class USIS_InventorySlotContainer;
class USIS_ItemDragDropOp;

/** 单机仓库会话：一个可见容器，拖拽跨页时额外保留一个数据组件。
 * 来源页保持真实 SIS 实例，拖拽结束后序列化双方的实际结果，不根据放下事件提前删物品。
 */
UCLASS(BlueprintType)
class SILVERCHOIR_API UWarehousePageSession : public UObject
{
    GENERATED_BODY()
public:
    bool Initialize(UPlayerManagerBase* Store, USIS_UnitInventoryComponent* Inventory,
        USIS_PlayerInventoryManager* InDragManager, USIS_InventorySlotContainer* Container);
    bool SwitchPage(int32 PageIndex);
    void Update();
    void Close();
    USIS_UnitInventoryComponent* GetDisplayedInventory() const { return DisplayInventory; }
    int32 GetCurrentPage() const { return CurrentPage; }
    bool IsDragging() const;
    /** 可独立测试的悬停计时：累计位移超出容差、换按钮、离开或结束拖拽均重置。 */
    bool UpdateHover(int32 PageIndex, FVector2D Cursor, double Now, float Delay, float Tolerance);
    void ResetHover();
private:
    UPROPERTY(Transient) TObjectPtr<UPlayerManagerBase> Manager;
    UPROPERTY(Transient) TObjectPtr<USIS_UnitInventoryComponent> PrimaryInventory;
    UPROPERTY(Transient) TObjectPtr<USIS_UnitInventoryComponent> StagingInventory;
    UPROPERTY(Transient) TObjectPtr<USIS_UnitInventoryComponent> DisplayInventory;
    UPROPERTY(Transient) TObjectPtr<USIS_PlayerInventoryManager> DragManager;
    UPROPERTY(Transient) TObjectPtr<USIS_InventorySlotContainer> View;
    UPROPERTY(Transient) TObjectPtr<USIS_ItemDragDropOp> Drag;
    int32 CurrentPage=INDEX_NONE;
    int32 SourcePage=INDEX_NONE;
    int32 StagingPage=INDEX_NONE;
    int32 HoverPage=INDEX_NONE;
    double HoverStart=0;
    FVector2D HoverOrigin=FVector2D::ZeroVector;
    bool bHoverTriggered=false;
    bool SavePage(int32 PageIndex, USIS_UnitInventoryComponent* Inventory);
    void Show(USIS_UnitInventoryComponent* Inventory);
    void FinishDrag();
};
