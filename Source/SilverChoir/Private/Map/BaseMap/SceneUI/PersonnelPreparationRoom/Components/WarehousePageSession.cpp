#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/WarehousePageSession.h"

#include "SubSystem/PlayerSubSystem/PlayerManagerBase.h"
#include "Components/SIS_UnitInventoryComponent.h"
#include "Components/SIS_PlayerInventoryManager.h"
#include "Widget/SlotContainerByType/SIS_InventorySlotContainer.h"
#include "Widget/ItemWidget/SIS_ItemDragDropOp.h"
#include "Widget/ItemWidget/SIS_ItemIcon.h"
#include "Engine/World.h"

bool UWarehousePageSession::Initialize(UPlayerManagerBase* Store, USIS_UnitInventoryComponent* Inventory,
    USIS_PlayerInventoryManager* InDragManager, USIS_InventorySlotContainer* Container)
{
    if (!IsValid(Store) || !IsValid(Inventory) || !IsValid(InDragManager) || !IsValid(Container)) return false;
    if (Manager && (PrimaryInventory!=Inventory || Manager!=Store)) Close();
    Manager=Store; PrimaryInventory=Inventory; DragManager=InDragManager; View=Container;
    if (!DisplayInventory)
    {
        CurrentPage=Manager->ActiveInventoryPage;
        if (!Manager->IsInventoryPageInitialized(Inventory))
        {
            TArray<FSIS_ItemData> Items;
            if (!Manager->ReadInventoryPage(CurrentPage,Items)) return false;
            Inventory->LoadRuntimeDataFromItemDatas(Items);
            Manager->MarkInventoryPageInitialized(Inventory);
        }
        Show(Inventory);
    }
    return true;
}

bool UWarehousePageSession::SavePage(int32 PageIndex, USIS_UnitInventoryComponent* Inventory)
{
    if (!IsValid(Manager) || !IsValid(Inventory)) return false;
    TArray<FSIS_ItemData> Items;
    Inventory->GetAllItemDataForSerialization(true,Items); // Empty is a valid page, despite SIS returning false.
    return Manager->WriteInventoryPage(PageIndex,Items);
}

void UWarehousePageSession::Show(USIS_UnitInventoryComponent* Inventory)
{
    if (DisplayInventory) DisplayInventory->UnitInventorySlotContainer=nullptr;
    DisplayInventory=Inventory;
    View->ClearAllItemIcons();
    Inventory->RegisterInventorySlotContainer(View);
    Inventory->LoadItemsToInventoryWidget();
}

bool UWarehousePageSession::IsDragging() const
{
    return IsValid(DragManager) && IsValid(DragManager->GetCurrentDragItemIcon());
}

void UWarehousePageSession::Update()
{
    if (!Manager || !DragManager) return;
    auto* Current=DragManager->GetCurrentDragItemIcon();
    if (Drag && Drag!=Current) FinishDrag(); // SIS has finished its synchronous standalone movement by now.
    if (!Drag && Current)
    {
        Drag=Current;
        SourcePage=Current->LocalItemInstancePtr.IsValid() &&
            Current->LocalItemInstancePtr->OwnerInventory.Get()==PrimaryInventory ? CurrentPage : INDEX_NONE;
    }
    if (!Current) ResetHover();
}

bool UWarehousePageSession::SwitchPage(int32 PageIndex)
{
    if (!Manager || !PrimaryInventory || !View || !Manager->InventoryPages.IsValidIndex(PageIndex)) return false;
    Update();
    if (PageIndex==CurrentPage) return false;
    // Dynamic staging inventories need server-owned replicated identities for network play.
    // Never attempt client-side cross-page transfer with a local-only staging component.
    if (IsDragging() && PrimaryInventory->GetWorld()->GetNetMode()!=NM_Standalone) return false;
    TArray<FSIS_ItemData> Items;
    if (!Manager->ReadInventoryPage(PageIndex,Items) || !SavePage(CurrentPage,DisplayInventory)) return false;
    if (Drag) Drag->OnLeaveWidget(); // Clear old placement previews before rebuilding the slots.
    if (SourcePage!=INDEX_NONE && Drag)
    {
        if (PageIndex==SourcePage) Show(PrimaryInventory);
        else
        {
            if (!StagingInventory)
            {
                auto* Owner=PrimaryInventory->GetOwner();
                StagingInventory=NewObject<USIS_UnitInventoryComponent>(Owner,NAME_None,RF_Transient);
                StagingInventory->bInitializeFromPreset=false;
                StagingInventory->DefaultInventorySlotConfigs=PrimaryInventory->DefaultInventorySlotConfigs;
                StagingInventory->RegisterComponent();
            }
            if (StagingPage!=PageIndex)
            {
                StagingInventory->UnitInventorySlotContainer=nullptr;
                StagingInventory->LoadRuntimeDataFromItemDatas(Items);
                StagingPage=PageIndex;
            }
            Show(StagingInventory);
        }
    }
    else
    {
        PrimaryInventory->UnitInventorySlotContainer=nullptr;
        PrimaryInventory->LoadRuntimeDataFromItemDatas(Items);
        Show(PrimaryInventory);
    }
    CurrentPage=PageIndex;
    Manager->SetActiveInventoryPage(PageIndex);
    ResetHover();
    return true;
}

void UWarehousePageSession::FinishDrag()
{
    // Serialize BOTH actual components, including quantities, attachments and container contents.
    // Rejected/canceled moves leave the source intact; successful SIS moves have already removed it.
    if (SourcePage!=INDEX_NONE) SavePage(SourcePage,PrimaryInventory);
    if (StagingInventory && StagingPage!=INDEX_NONE) SavePage(StagingPage,StagingInventory);
    if (DisplayInventory==StagingInventory && StagingInventory)
    {
        TArray<FSIS_ItemData> Items;
        StagingInventory->GetAllItemDataForSerialization(true,Items);
        StagingInventory->UnitInventorySlotContainer=nullptr;
        PrimaryInventory->LoadRuntimeDataFromItemDatas(Items);
        Show(PrimaryInventory);
    }
    SavePage(CurrentPage,DisplayInventory);
    Drag=nullptr; SourcePage=INDEX_NONE; StagingPage=INDEX_NONE;
    if (StagingInventory) { StagingInventory->DestroyComponent(); StagingInventory=nullptr; }
    ResetHover();
}

void UWarehousePageSession::Close()
{
    if (DragManager && Drag && DragManager->GetCurrentDragItemIcon()==Drag)
    {
        if (Drag->SourceItemIcon) Drag->SourceItemIcon->SetIsEnabled(true);
        Drag->OnLeaveWidget();
        DragManager->ClearCurrentDragItemIcon();
    }
    if (Manager && PrimaryInventory) FinishDrag();
    if (DisplayInventory && DisplayInventory->UnitInventorySlotContainer==View) DisplayInventory->UnitInventorySlotContainer=nullptr;
    DisplayInventory=nullptr; PrimaryInventory=nullptr; View=nullptr; Manager=nullptr; DragManager=nullptr;
    CurrentPage=INDEX_NONE;
}

void UWarehousePageSession::ResetHover()
{
    HoverPage=INDEX_NONE; HoverStart=0; bHoverTriggered=false;
}

bool UWarehousePageSession::UpdateHover(int32 PageIndex, FVector2D Cursor, double Now, float Delay, float Tolerance)
{
    if (PageIndex==INDEX_NONE || PageIndex==CurrentPage) { ResetHover(); return false; }
    if (PageIndex!=HoverPage || FVector2D::Distance(Cursor,HoverOrigin)>FMath::Max(0.f,Tolerance))
    {
        HoverPage=PageIndex; HoverOrigin=Cursor; HoverStart=Now; bHoverTriggered=false;
        return false;
    }
    if (!bHoverTriggered && Now-HoverStart>=FMath::Max(0.f,Delay)) { bHoverTriggered=true; return true; }
    return false;
}
