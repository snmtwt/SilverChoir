#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Object/Unit/UnitPawnBase.h"
#include "SubSystem/PlayerUnitSubSystem/UnitDisplayStand.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/HMS_CharacterMoverComponent.h"
#include "Structs/HMS_MoverStructs.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "SubSystem/PlayerSubSystem/PlayerCameraPawn.h"
#include "SubSystem/PlayerSubSystem/PlayerLibrary.h"
#include "Components/SIS_PlayerInventoryManager.h"
#include "Components/SIS_UnitInventoryComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerCameraInventoryTest,"SilverChoir.Player.CameraInventory",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FPlayerCameraInventoryTest::RunTest(const FString& Parameters)
{
    UWorld* World=nullptr;
    for (const auto& Context:GEngine->GetWorldContexts()) if (Context.WorldType==EWorldType::Game) { World=Context.World(); break; }
    if (!TestNotNull(TEXT("Game world"),World)) return false;
    auto* PC=Cast<AGameMainMapPlayerController>(World->GetFirstPlayerController());
    if (!TestNotNull(TEXT("GameMainMap controller"),PC)) return false;
    auto* Camera=UPlayerLibrary::GetPlayerCamera(World);
    if (!TestNotNull(TEXT("Library resolves possessed player camera"),Camera)) return false;
    TestTrue(TEXT("Camera is possessed pawn"),Camera==PC->GetPawn());
    TestTrue(TEXT("Configured pawn class is camera-derived"),UPlayerLibrary::GetPlayerPawnClass(World)->IsChildOf(APlayerCameraPawn::StaticClass()));
    TestTrue(TEXT("Camera inventory initialized and owned"),IsValid(Camera->UnitInventoryComponent) && Camera->UnitInventoryComponent->GetOwner()==Camera && Camera->UnitInventoryComponent->IsRegistered());
    TestTrue(TEXT("Controller inventory manager initialized and owned"),IsValid(PC->PlayerInventoryManager) && PC->PlayerInventoryManager->GetOwner()==PC && PC->PlayerInventoryManager->IsRegistered());
    TArray<USIS_PlayerInventoryManager*> Managers; PC->GetComponents(Managers);
    TArray<USIS_UnitInventoryComponent*> Inventories; Camera->GetComponents(Inventories);
    TestEqual(TEXT("One manager on controller"),Managers.Num(),1);
    TestEqual(TEXT("One inventory on camera"),Inventories.Num(),1);
    TestNull(TEXT("Missing world is safe"),UPlayerLibrary::GetPlayerCamera(nullptr));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnitPawnBaseTest,"SilverChoir.Unit.PawnBase",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
bool FUnitPawnBaseTest::RunTest(const FString& Parameters)
{
    UWorld* World=nullptr;
    for (const auto& Context:GEngine->GetWorldContexts()) if (Context.WorldType==EWorldType::Game) { World=Context.World(); break; }
    if (!TestNotNull(TEXT("World"),World)) return false;
    FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Unit=World->SpawnActor<AUnitPawnBase>(FVector(0,0,500),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Spawn base unit"),Unit)) return false;
    TestTrue(TEXT("Capsule is root"),Unit->GetRootComponent()==Unit->CollisionComponent);
    TestTrue(TEXT("Inventory registered"),Unit->UnitInventoryComponent->IsRegistered());
    TestTrue(TEXT("Mover registered"),Unit->MoverComponent->IsRegistered());
    UClass* UnitBlueprint = LoadClass<AUnitPawnBase>(nullptr,
        TEXT("/Game/System/Object/Unit/Character/BP_UnitPawn.BP_UnitPawn_C"));
    USkeletalMesh* TestMesh = UnitBlueprint ? UnitBlueprint->GetDefaultObject<AUnitPawnBase>()->MeshComponent->GetSkeletalMeshAsset() : nullptr;
    if (TestNotNull(TEXT("Real skeletal mesh for wearable lifecycle"), TestMesh))
    {
        Unit->MeshComponent->SetSkeletalMesh(TestMesh);
        const FGuid ClothingKey = FGuid::NewGuid();
        const FGuid OtherKey = FGuid::NewGuid();
        USkeletalMeshComponent* Clothing = Unit->AddWearableMesh(TestMesh, ClothingKey);
        if (TestNotNull(TEXT("Wearable component created"), Clothing))
        {
            TestTrue(TEXT("Wearable registered and owned by unit"), Clothing->IsRegistered() && Clothing->GetOwner() == Unit);
            TestTrue(TEXT("Wearable attaches to body at identity"), Clothing->GetAttachParent() == Unit->MeshComponent && Clothing->GetRelativeTransform().Equals(FTransform::Identity));
            TestTrue(TEXT("Wearable shares body pose"), Clothing->LeaderPoseComponent.Get() == Unit->MeshComponent);
            TestTrue(TEXT("Wearable cannot block unit movement"), Clothing->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Clothing->CanEverAffectNavigation());
            TestFalse(TEXT("Wearable has no physics state"), Clothing->IsPhysicsStateCreated());
            TestFalse(TEXT("Wearable does not generate overlaps"), Clothing->GetGenerateOverlapEvents());
            TestTrue(TEXT("Wearable permits mesh-authored cloth actors"), Clothing->GetAllowClothActors());
            TestFalse(TEXT("Wearable retains cloth simulation"), Clothing->bDisableClothSimulation);
            TestNull(TEXT("Wearable has no independent animation instance"), Clothing->GetAnimInstance());
            TestTrue(TEXT("Wearable disables mesh-authored post process"), Clothing->GetDisablePostProcessBlueprint());
            TestNull(TEXT("Wearable has no post-process animation instance"), Clothing->GetPostProcessInstance());
            TestFalse(TEXT("Wearable is not independently replicated"), Clothing->GetIsReplicated());
            USkeletalMeshComponent* Other = Unit->AddWearableMesh(TestMesh, OtherKey);
            TestNotNull(TEXT("Independent second clothing key"), Other);
            TestNull(TEXT("Invalid key rejected"), Unit->AddWearableMesh(TestMesh, FGuid()));
            TestNull(TEXT("Null mesh rejected"), Unit->AddWearableMesh(nullptr, ClothingKey));
            TestTrue(TEXT("Rejected replacement preserves clothing"), Clothing->IsRegistered());
            USkeletalMeshComponent* Replacement = Unit->AddWearableMesh(TestMesh, ClothingKey);
            TestNotNull(TEXT("Same key replacement created"), Replacement);
            TestFalse(TEXT("Same key destroys previous component"), Clothing->IsRegistered());
            TestFalse(TEXT("Same key removes old instance reference"), Unit->GetInstanceComponents().Contains(Clothing));
            TestTrue(TEXT("Remove clothing by key succeeds"), Unit->RemoveWearableMesh(ClothingKey));
            TestFalse(TEXT("Removed component unregistered"), Replacement && Replacement->IsRegistered());
            TestFalse(TEXT("Repeated removal safely reports missing key"), Unit->RemoveWearableMesh(ClothingKey));
            TestTrue(TEXT("Other key remains intact"), IsValid(Other) && Other->IsRegistered());
            Unit->RemoveWearableMesh(OtherKey);
        }
    }
    Unit->SetUnitMoveVelocity(FVector(120,0,50));
    FMoverInputCmdContext Input; Unit->ProduceInput_Implementation(0,Input);
    const auto* HMS=Input.InputCollection.FindDataByType<FHMS_MoverInput>();
    if (TestNotNull(TEXT("HMS input produced"),HMS)) TestEqual(TEXT("Ground velocity"),HMS->RequestedVelocity,FVector(120,0,0));
    auto Data=UPlayerUnitLibrary::CreateUnitDataFromTemplate(FUnitTemplate()); FText Error;
    TestTrue(TEXT("Load unit record"),UPlayerUnitLibrary::LoadPlayerUnitData(World,{Data},Error));
    TestTrue(TEXT("Bind existing record"),Unit->BindUnitData(Data.UnitId));
    auto Shared=UPlayerUnitLibrary::GetUnitDataShared(World,Data.UnitId);
    TestTrue(TEXT("Canonical pointer retained"),Unit->GetUnitDataShared().Get()==Shared.Get());
    Unit->Destroy();
    TestFalse(TEXT("EndPlay releases record"),Unit->GetUnitDataShared().IsValid());
    FUnitTemplate Template;
    Template.EntityData.UnitPawnClass=AUnitPawnBase::StaticClass();
    auto PreviewData=UPlayerUnitLibrary::CreateUnitDataFromTemplate(Template);
    TestTrue(TEXT("Template copies entity class"),PreviewData.EntityData.UnitPawnClass==AUnitPawnBase::StaticClass());
    TestTrue(TEXT("Load preview data"),UPlayerUnitLibrary::LoadPlayerUnitData(World,{PreviewData},Error));
    auto* Stand=World->SpawnActor<AUnitDisplayStand>();
    if (!TestNotNull(TEXT("Display stand"),Stand)) return false;
    TestTrue(TEXT("BeginPlay registers display stand"), UPlayerUnitLibrary::GetUnitDisplayStand(World)==Stand);
    TestNull(TEXT("Missing world returns no stand"), UPlayerUnitLibrary::GetUnitDisplayStand(nullptr));
    auto* Preview=Stand->DisplayUnit(PreviewData.UnitId);
    if (TestNotNull(TEXT("Preview created"),Preview))
    {
        TestTrue(TEXT("Preview shares authoritative data"),Preview->GetUnitDataShared()==UPlayerUnitLibrary::GetUnitDataShared(World,PreviewData.UnitId));
        TestFalse(TEXT("Preview collision disabled"),Preview->GetActorEnableCollision());
        TestFalse(TEXT("Preview mover inactive"),Preview->MoverComponent->IsActive());
        TestNull(TEXT("Unknown ID rejected"),Stand->DisplayUnit(FGuid::NewGuid()));
        TestTrue(TEXT("Failed request preserves preview"),Stand->GetDisplayedUnit()==Preview);
        auto* Replacement=Stand->DisplayUnit(PreviewData.UnitId);
        TestNotNull(TEXT("Replacement created"),Replacement);
        TestTrue(TEXT("Previous preview destroyed"),Preview->IsActorBeingDestroyed());
        Stand->ClearDisplayedUnit();
        TestNull(TEXT("Clear removes preview"),Stand->GetDisplayedUnit());
        auto* Last=Stand->DisplayUnit(PreviewData.UnitId);
        Stand->Destroy();
        TestNull(TEXT("EndPlay unregisters display stand"), UPlayerUnitLibrary::GetUnitDisplayStand(World));
        if (TestNotNull(TEXT("Final preview"),Last)) TestTrue(TEXT("Stand owns preview lifetime"),Last->IsActorBeingDestroyed());
    }
    else Stand->Destroy();
    return true;
}
#endif
