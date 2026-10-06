#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/SIS_UnitInventoryComponent.h"
#include "UObject/Package.h"
#include "Object/Items/WearableItem.h"
#include "Object/Unit/UnitPawnBase.h"
#include "Materials/Material.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWearableItemEventsTest, "SilverChoir.Inventory.WearableItem",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FWearableItemEventsTest::RunTest(const FString& Parameters)
{
    UWorld* World = nullptr;
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
    if (!TestNotNull(TEXT("Game world"), World)) return false;
    // SIS exposes AcquireItemInstance to reflection, but does not export the pool's native C++ API.
    auto* Pool = FindObject<UObject>(GetTransientPackage(), TEXT("SIS_ItemObjectPool_Singleton"));
    if (!TestNotNull(TEXT("Inventory manager initialized the SIS pool"), Pool)) return false;
    const auto Acquire = [Pool]()
    {
        struct { TSubclassOf<AActor> ItemClass; AActor* ReturnValue = nullptr; } Params;
        Params.ItemClass = AWearableItem::StaticClass();
        Pool->ProcessEvent(Pool->FindFunctionChecked(TEXT("AcquireItemInstance")), &Params);
        return Cast<AWearableItem>(Params.ReturnValue);
    };
    auto* Item = Acquire();
    if (!TestNotNull(TEXT("SIS pooled wearable event object"), Item)) return false;
    UClass* BodyClass = LoadClass<AUnitPawnBase>(nullptr,
        TEXT("/Game/System/Object/Unit/Character/BP_UnitPawn.BP_UnitPawn_C"));
    if (!TestNotNull(TEXT("Configured body class"), BodyClass)) return false;
    auto* Mesh = BodyClass->GetDefaultObject<AUnitPawnBase>()->MeshComponent->GetSkeletalMeshAsset();
    if (!TestNotNull(TEXT("Test skeletal mesh"), Mesh)) return false;
    auto* A = World->SpawnActor<AUnitPawnBase>();
    auto* B = World->SpawnActor<AUnitPawnBase>();
    ON_SCOPE_EXIT
    {
        if (A) A->Destroy();
        if (B) B->Destroy();
        Item->MeshComponent->SetStaticMesh(nullptr);
        Item->WearMesh = nullptr;
        Item->MeshComponent->EmptyOverrideMaterials();
    };
    if (!TestNotNull(TEXT("First wearer"), A) || !TestNotNull(TEXT("Second wearer"), B)) return false;
    A->MeshComponent->SetSkeletalMesh(Mesh);
    B->MeshComponent->SetSkeletalMesh(Mesh);
    auto* DisplayMesh = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!TestNotNull(TEXT("Static display mesh"), DisplayMesh)) return false;
    Item->MeshComponent->SetStaticMesh(DisplayMesh);
    Item->WearMesh = Mesh;
    auto* Material = UMaterial::GetDefaultMaterial(MD_Surface);
    Item->MeshComponent->SetMaterial(0, Material);

    const auto Clothing = [](AUnitPawnBase* Unit)
    {
        TArray<USkeletalMeshComponent*> Result;
        TArray<USkeletalMeshComponent*> All;
        Unit->GetComponents(All);
        for (auto* Component : All)
            if (Component != Unit->MeshComponent && Component->IsRegistered()) Result.Add(Component);
        return Result;
    };
    FSIS_EquipmentSlotEventData Event;
    Event.ItemData.ItemId = FGuid::NewGuid();
    Event.ItemData.ItemTemplate.ItemActorClass = AWearableItem::StaticClass();
    Event.TargetUserActor = A;
    Event.TargetInventory = A->UnitInventoryComponent;
    A->UnitInventoryComponent->ExecuteAddToEquipSlotEvent(Event);
    auto First = Clothing(A);
    if (!TestEqual(TEXT("Real SIS equip dispatch creates clothing"), First.Num(), 1)) return false;
    TestTrue(TEXT("Clothing uses unit body as leader"), First[0]->LeaderPoseComponent.Get() == A->MeshComponent);
    TestTrue(TEXT("Equipped mesh uses the dedicated WearMesh asset"), First[0]->GetSkeletalMeshAsset() == Item->WearMesh);
    TestTrue(TEXT("Equipping preserves the static display mesh"), Item->MeshComponent->GetStaticMesh() == DisplayMesh);
    TestTrue(TEXT("Display-item material overrides are not copied to clothing"), First[0]->OverrideMaterials.IsEmpty());
    TestTrue(TEXT("Repeated add succeeds"), ISIS_ItemObjectInterface::Execute_OnAddToEquipmentSlot(Item, Event));
    TestEqual(TEXT("Repeated equip cannot duplicate clothing"), Clothing(A).Num(), 1);
    TestFalse(TEXT("Repeated equip destroys prior component"), First[0]->IsRegistered());

    FSIS_EquipmentSlotEventData SecondEvent = Event;
    SecondEvent.TargetUserActor = B;
    SecondEvent.TargetInventory = B->UnitInventoryComponent;
    B->UnitInventoryComponent->ExecuteAddToEquipSlotEvent(SecondEvent);
    TestEqual(TEXT("Shared item ID can have independent preview/world wearers"), Clothing(B).Num(), 1);
    TestTrue(TEXT("Both dispatches reuse the same event object"), Acquire() == Item);

    FSIS_EquipmentSlotEventData Invalid = Event;
    Invalid.ItemData.ItemId.Invalidate();
    TestFalse(TEXT("Invalid ID rejected"), ISIS_ItemObjectInterface::Execute_OnAddToEquipmentSlot(Item, Invalid));
    Invalid = Event; Invalid.TargetUserActor.Reset(); Invalid.TargetInventory.Reset();
    TestFalse(TEXT("Missing wearer rejected"), ISIS_ItemObjectInterface::Execute_OnAddToEquipmentSlot(Item, Invalid));
    Item->WearMesh = nullptr;
    TestFalse(TEXT("Missing mesh rejected"), ISIS_ItemObjectInterface::Execute_OnAddToEquipmentSlot(Item, Event));
    TestEqual(TEXT("Failed add preserves equipped clothing"), Clothing(A).Num(), 1);
    Item->WearMesh = Mesh;

    FSIS_EquipmentSlotEventData Other = Event;
    Other.ItemData.ItemId = FGuid::NewGuid();
    A->UnitInventoryComponent->ExecuteAddToEquipSlotEvent(Other);
    TestEqual(TEXT("Different item IDs keep separate clothing components"), Clothing(A).Num(), 2);
    TestFalse(TEXT("SIS retains responsibility for bound item actor cleanup"), A->UnitInventoryComponent->ExecuteRemoveFromEquipSlotEvent(Event));
    TestEqual(TEXT("Unequip removes only that item ID"), Clothing(A).Num(), 1);
    TestEqual(TEXT("Unequip never affects another wearer"), Clothing(B).Num(), 1);
    SecondEvent.TargetUserActor.Reset();
    B->UnitInventoryComponent->ExecuteRemoveFromEquipSlotEvent(SecondEvent);
    TestTrue(TEXT("Inventory owner fallback supports removal"), Clothing(B).IsEmpty());
    A->UnitInventoryComponent->ExecuteRemoveFromEquipSlotEvent(Other);
    TestTrue(TEXT("All clothing removed"), Clothing(A).IsEmpty());
    TestFalse(TEXT("Pooled event actor is never destroyed by unequip"), Item->IsActorBeingDestroyed());
    Item->MeshComponent->SetStaticMesh(nullptr);
    TestTrue(TEXT("WearMesh equips without a configured static display mesh"), ISIS_ItemObjectInterface::Execute_OnAddToEquipmentSlot(Item, Event));
    TestEqual(TEXT("Display mesh is not required for wearable creation"), Clothing(A).Num(), 1);
    return true;
}
#endif
