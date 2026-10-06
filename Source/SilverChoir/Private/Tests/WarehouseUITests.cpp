#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Blueprint/WidgetTree.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/PersonnelPreparationRoomWidget.h"
#include "Widget/SlotContainerByType/SIS_InventorySlotContainer.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "SubSystem/PlayerSubSystem/PlayerLibrary.h"
#include "SubSystem/PlayerSubSystem/PlayerManagerBase.h"
#include "SubSystem/PlayerSubSystem/PlayerCameraPawn.h"
#include "Components/SIS_UnitInventoryComponent.h"
#include "Tools/SIS_InventorySystemBFL.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/WarehousePageSession.h"
#include "Components/SIS_PlayerInventoryManager.h"
#include "Settings/SIS_PluginSettings.h"
#include "Widget/ItemWidget/SIS_ItemDragDropOp.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarehouseDragTest,"SilverChoir.BaseUI.WarehouseCrossPageDrag",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FWarehouseDragTest::RunTest(const FString& Parameters)
{
    UWorld* World=nullptr;
    for (auto& C:GEngine->GetWorldContexts()) if (C.WorldType==EWorldType::Game) { World=C.World(); break; }
    if (!TestNotNull(TEXT("World"),World)) return false;
    auto* PC=World->GetFirstPlayerController();
    auto* DragManager=PC->FindComponentByClass<USIS_PlayerInventoryManager>();
    auto* Owner=World->SpawnActor<AActor>();
    auto* Inventory=NewObject<USIS_UnitInventoryComponent>(Owner);
    Inventory->bInitializeFromPreset=false;
    FSIS_InventoryGridSlotConfig Grid;Grid.ColumnsNum=5;Grid.RowsNum=8;
    Inventory->DefaultInventorySlotConfigs={Grid};Inventory->RegisterComponent();
    auto* Store=NewObject<UPlayerManagerBase>(World->GetGameInstance());
    auto* RoomClass=LoadClass<UPersonnelPreparationRoomWidget>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom.WBP_PersonnelPreparationRoom_C"));
    auto* Room=CreateWidget<UPersonnelPreparationRoomWidget>(PC,RoomClass);
    auto* Session=NewObject<UWarehousePageSession>(Room);
    FSIS_ItemData Item(true);Item.ItemLocationInfo.ParentItemId=FSIS_Guids::SpecialItemGuid;
    Item.ItemLocationInfo.RowSpan=Item.ItemLocationInfo.ColumnSpan=1;
    Store->WriteInventoryPage(0,{Item});
    if (!TestTrue(TEXT("Initialize session"),Session->Initialize(Store,Inventory,DragManager,Room->GetWarehouseInventoryContainer()))) { Owner->Destroy();return false; }
    TestFalse(TEXT("Hover begins"),Session->UpdateHover(1,{0,0},0,1,4));
    TestFalse(TEXT("Hover needs full second"),Session->UpdateHover(1,{0,0},.99,1,4));
    TestTrue(TEXT("Hover triggers at one second"),Session->UpdateHover(1,{0,0},1,1,4));
    TestFalse(TEXT("No repeat hover"),Session->UpdateHover(1,{0,0},2,1,4));
    TestFalse(TEXT("Motion resets hover"),Session->UpdateHover(1,{10,0},3,1,4));
    TestFalse(TEXT("New button resets hover"),Session->UpdateHover(2,{10,0},3.9,1,4));
    TestFalse(TEXT("Leaving resets hover"),Session->UpdateHover(INDEX_NONE,{10,0},4,1,4));
    auto StartDrag=[&]()
    {
        auto* Op=CreateWidget<USIS_ItemDragDropOp>(PC,GetDefault<USIS_PluginSettings>()->ItemDragDropOpClass);
        Op->LocalItemInstancePtr=Inventory->GetItemInstanceByItemId(Item.ItemId);
        DragManager->SetCurrentDragItemIcon(Op);Session->Update();return Op;
    };
    StartDrag();
    TestTrue(TEXT("Switch while dragging"),Session->SwitchPage(1));
    TestTrue(TEXT("Source instance stays alive"),Inventory->GetItemInstanceByItemId(Item.ItemId).IsValid());
    TestTrue(TEXT("Separate target data component"),Session->GetDisplayedInventory()!=Inventory);
    TestTrue(TEXT("Can pass through another page"),Session->SwitchPage(2));
    TestTrue(TEXT("Can return to original page"),Session->SwitchPage(0));
    TestTrue(TEXT("Original page uses original component"),Session->GetDisplayedInventory()==Inventory);
    Session->SwitchPage(1);
    DragManager->ClearCurrentDragItemIcon();Session->Update();
    TArray<FSIS_ItemData> Data;
    Store->ReadInventoryPage(0,Data);TestEqual(TEXT("Cancel retains original"),Data.Num(),1);
    Store->ReadInventoryPage(1,Data);TestEqual(TEXT("Cancel leaves target empty"),Data.Num(),0);
    Session->SwitchPage(0);StartDrag();Session->SwitchPage(1);
    FSIS_ItemMoveDataLight Move;Move.ItemId=Item.ItemId;Move.ItemLocationInfo=Item.ItemLocationInfo;
    Move.FromInventoryComponent=Inventory;Move.TargetInventoryComponent=Session->GetDisplayedInventory();
    DragManager->MoveItemsByLightData({Move}); // Use actual plugin move, not a fabricated success flag.
    TestFalse(TEXT("SIS removed the source instance"),Inventory->GetItemInstanceByItemId(Item.ItemId).IsValid());
    DragManager->ClearCurrentDragItemIcon();Session->Update();
    Store->ReadInventoryPage(0,Data);TestEqual(TEXT("Successful move removes saved source"),Data.Num(),0);
    Store->ReadInventoryPage(1,Data);if(TestEqual(TEXT("Successful move saves target"),Data.Num(),1)) TestEqual(TEXT("Move preserves item ID"),Data[0].ItemId,Item.ItemId);
    TestTrue(TEXT("Camera inventory restored after drag"),Session->GetDisplayedInventory()==Inventory);
    TestTrue(TEXT("Inventory contains moved item"),Inventory->GetItemInstanceByItemId(Item.ItemId).IsValid());
    Session->Close();Owner->Destroy();
    return true;
}

class FWarehouseCapture : public IAutomationLatentCommand
{
    TWeakObjectPtr<UPersonnelPreparationRoomWidget> Room;
    double Start=FPlatformTime::Seconds();
    bool bCaptured=false;
public:
    FWarehouseCapture(UPersonnelPreparationRoomWidget* In):Room(In) {}
    bool Update() override
    {
        if (!Room.IsValid()) return true;
        if (!bCaptured && FPlatformTime::Seconds()-Start>2)
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/PersonnelUI/SingleWarehouse.png"),true,false);
            bCaptured=true;
        }
        if (FPlatformTime::Seconds()-Start>3) { Room->RemoveFromParent(); return true; }
        return false;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarehouseUITest,"SilverChoir.BaseUI.SingleWarehouse",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FWarehouseUITest::RunTest(const FString& Parameters)
{
    UWorld* World=nullptr;
    for (auto& C:GEngine->GetWorldContexts()) if (C.WorldType==EWorldType::Game) { World=C.World(); break; }
    if (!TestNotNull(TEXT("Game world"),World)) return false;
    auto* Manager=UPlayerLibrary::GetPlayerManager(World);
    auto* Camera=UPlayerLibrary::GetPlayerCamera(World);
    if (!TestNotNull(TEXT("Manager"),Manager) || !TestNotNull(TEXT("Camera"),Camera)) return false;
    auto* Inventory=Camera->UnitInventoryComponent.Get();
    Manager->InventoryPages.SetNum(5);
    Manager->SetActiveInventoryPage(0);
    Manager->ResetInventoryPageInitialization();
    FSIS_InventoryGridSlotConfig Grid; Grid.ColumnsNum=5; Grid.RowsNum=8;
    Inventory->DefaultInventorySlotConfigs={Grid};
    FSIS_ItemData First(true),Second(true);
    First.ItemLocationInfo.ParentItemId=FSIS_Guids::SpecialItemGuid;
    Second.ItemLocationInfo.ParentItemId=FSIS_Guids::SpecialItemGuid;
    First.ItemLocationInfo.RowSpan=First.ItemLocationInfo.ColumnSpan=1;
    Second.ItemLocationInfo.RowSpan=Second.ItemLocationInfo.ColumnSpan=1;
    Manager->WriteInventoryPage(1,{Second});
    Manager->WriteInventoryPage(0,{First});
    USIS_InventorySystemBFL::LoadItemDatasToInventoryComponent(Inventory,{},false);
    auto* Class=LoadClass<UPersonnelPreparationRoomWidget>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom.WBP_PersonnelPreparationRoom_C"));
    if (!TestNotNull(TEXT("Room class"),Class)) return false;
    auto* Room=CreateWidget<UPersonnelPreparationRoomWidget>(World->GetFirstPlayerController(),Class);
    Room->AddToViewport(10000);
    Room->OnSceneUIOpened();
    const auto InitialItems=Inventory->GetAllItemData();
    if (TestEqual(TEXT("First open loads page zero preset"),InitialItems.Num(),1)) TestEqual(TEXT("Initial ID preserved"),InitialItems[0].ItemId,First.ItemId);
    TestTrue(TEXT("Component initialization recorded"),Manager->IsInventoryPageInitialized(Inventory));
    int32 Count=0;
    Room->WidgetTree->ForEachWidget([&](UWidget* W){ if (Cast<USIS_InventorySlotContainer>(W)) ++Count; });
    TestEqual(TEXT("Exactly one warehouse inventory widget"),Count,1);
    auto* Container=Room->GetWarehouseInventoryContainer();
    for (int32 I=0;I<5;++I)
    {
        auto* Button=Cast<USelectionButtonWidget>(Room->GetWidgetFromName(FName(*FString::Printf(TEXT("InventoryTab%02d"),I+1))));
        if (TestNotNull(TEXT("Numbered button"),Button)) Button->OnSelectionRequested.Broadcast(Button->ChoiceID);
        TestEqual(TEXT("Button selects page"),Room->CurrentInventoryPage,I);
        TestTrue(TEXT("Same container reused"),Room->GetWarehouseInventoryContainer()==Container);
        TestFalse(TEXT("Repeated selection is not a page change"),Room->SelectInventoryPage(I));

    }
    TArray<FSIS_ItemData> Stored;
    TestTrue(TEXT("Read page zero"),Manager->ReadInventoryPage(0,Stored));
    if (TestEqual(TEXT("Blueprint saved previous page"),Stored.Num(),1)) TestEqual(TEXT("Saved original ID"),Stored[0].ItemId,First.ItemId);
    Manager->ReadInventoryPage(1,Stored);
    if (TestEqual(TEXT("Second page survived subsequent save"),Stored.Num(),1)) TestEqual(TEXT("Second page ID preserved"),Stored[0].ItemId,Second.ItemId);
    TestEqual(TEXT("Empty page clears live inventory"),Inventory->GetAllItemData().Num(),0);
    TestTrue(TEXT("Switch back to saved page"),Room->SelectInventoryPage(0));
    const auto Loaded=Inventory->GetAllItemData();
    if (TestEqual(TEXT("Blueprint reloaded saved data"),Loaded.Num(),1)) TestEqual(TEXT("Reload retains ID"),Loaded[0].ItemId,First.ItemId);
    Room->SelectInventoryPage(4);
    TestFalse(TEXT("Reject invalid storage index"),Manager->WriteInventoryPage(5,{First}));
    TestFalse(TEXT("Reject invalid page"),Room->SelectInventoryPage(5));
    Room->RemoveFromParent();
    TestEqual(TEXT("Selected page retained across removal"),Room->CurrentInventoryPage,4);
    USIS_InventorySystemBFL::LoadItemDatasToInventoryComponent(Inventory,{Second},false);
    Room->AddToViewport(10000);
    Room->OnSceneUIOpened();
    const auto ReopenedItems=Inventory->GetAllItemData();
    if (TestEqual(TEXT("Reopen preserves unsaved live edits"),ReopenedItems.Num(),1)) TestEqual(TEXT("Live item retained"),ReopenedItems[0].ItemId,Second.ItemId);
    TestFalse(TEXT("Reconstruct retains selected page"),Room->SelectInventoryPage(4));
    Room->GetWidgetFromName(TEXT("RosterPanel"))->SetVisibility(ESlateVisibility::Collapsed);
    Room->GetWidgetFromName(TEXT("WarehousePanel"))->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    ADD_LATENT_AUTOMATION_COMMAND(FWarehouseCapture(Room));
    return true;
}
#endif
