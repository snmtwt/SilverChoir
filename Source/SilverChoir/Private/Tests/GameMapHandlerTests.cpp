#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "GameMapHandlerTestProxy.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Object/Unit/UnitPawnBase.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/UnitSquadSpawner.h"
#include "UObject/StrongObjectPtr.h"

namespace GameMapHandlerTests
{
class FRegistryFlow : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TWeakObjectPtr<UWorld> World;
    TStrongObjectPtr<UGameMapHandlerTestProxy> Handler;
    TArray<FGuid> SquadIds;
    TArray<TWeakObjectPtr<AUnitPawnBase>> UnitsAtUnload;
    TWeakObjectPtr<AUnitSquadSpawner> ForeignSpawner;
    FName MapID = TEXT("GameHandlerRegistryFixture");
    double Start = FPlatformTime::Seconds();
    int32 Stage = 0;

    void CleanupSquads()
    {
        if (!World.IsValid()) return;
        FText Error;
        if (auto* Manager = UPlayerSquadLibrary::GetPlayerSquadManager(World.Get()))
            for (FGuid Id : SquadIds) Manager->RemoveSquad(Id, Error);
        SquadIds.Reset();
        if (ForeignSpawner.IsValid()) ForeignSpawner->Destroy();
    }
    bool CheckDeployment(ULevel* Level)
    {
        auto* Units = UPlayerUnitLibrary::GetPlayerUnitManager(World.Get());
        auto* Squads = UPlayerSquadLibrary::GetPlayerSquadManager(World.Get());
        if (!Test->TestNotNull(TEXT("Unit manager"), Units) || !Test->TestNotNull(TEXT("Squad manager"), Squads)) return false;
        FText Error;
        FUnitTemplate Template;
        Template.EntityData.UnitPawnClass = AUnitPawnBase::StaticClass();
        TArray<FUnitData> Records = UPlayerUnitLibrary::CreateUnitDataBatch(Template, 2);
        for (FUnitData& Record : Records) Record.RuntimeData.TileId = MapID;
        if (!Test->TestTrue(TEXT("Load deployment fixture records"), Units->LoadUnitData(Records, Error))) return false;
        for (int32 Index = 0; Index < 2; ++Index)
        {
            FGuid Id;
            if (!Test->TestTrue(TEXT("Create deployment squad"), Squads->CreateSquad(FText::FromString(TEXT("Handler fixture")), nullptr, MapID, Id, Error))) return false;
            SquadIds.Add(Id);
            if (!Test->TestTrue(TEXT("Add deployment member"), Squads->AddUnitToSquad(Records[Index].UnitId, Id, Error))) return false;
        }
        Handler->SquadIds = SquadIds;
        FActorSpawnParameters Params;
        Params.OverrideLevel = Level;
        AUnitSquadSpawner* A = World->SpawnActor<AUnitSquadSpawner>(FVector(200000., 0., 100.), FRotator::ZeroRotator, Params);
        AUnitSquadSpawner* B = World->SpawnActor<AUnitSquadSpawner>(FVector(201000., 0., 100.), FRotator::ZeroRotator, Params);
        if (!Test->TestNotNull(TEXT("Spawner A"), A) || !Test->TestNotNull(TEXT("Spawner B"), B)) return false;
        Test->TestTrue(TEXT("Register B first"), Handler->RegisterSquadSpawner(B, Error));
        Test->TestTrue(TEXT("Register A second"), Handler->RegisterSquadSpawner(A, Error));
        Test->TestTrue(TEXT("Duplicate registration is idempotent"), Handler->RegisterSquadSpawner(B, Error));
        Test->TestTrue(TEXT("Registry preserves B,A order without duplicates"), Handler->GetRegisteredSquadSpawners() == TArray<AUnitSquadSpawner*>{B, A});

        ForeignSpawner = World->SpawnActor<AUnitSquadSpawner>();
        Test->TestFalse(TEXT("Persistent-level generator cannot register into another streamed map"), Handler->RegisterSquadSpawner(ForeignSpawner.Get(), Error));
        Test->TestFalse(TEXT("Registration rejection exposes a reason"), Error.IsEmpty());
        if (!Test->TestTrue(TEXT("Deploy both squads"), Handler->SpawnSquads(SquadIds, Error))) return false;
        Test->TestEqual(TEXT("First ID maps to first registered B"), B->GetSpawnedSquadId(), SquadIds[0]);
        Test->TestEqual(TEXT("Second ID maps to second registered A"), A->GetSpawnedSquadId(), SquadIds[1]);
        const TArray<AUnitPawnBase*> BUnits = B->GetSpawnedUnits();
        const TArray<AUnitPawnBase*> AUnits = A->GetSpawnedUnits();
        Test->TestEqual(TEXT("B spawns one real member"), BUnits.Num(), 1);
        Test->TestEqual(TEXT("A spawns one real member"), AUnits.Num(), 1);
        if (BUnits.Num() == 1) Test->TestTrue(TEXT("B uses first member shared data"), BUnits[0]->GetUnitDataShared() == Units->GetUnitDataShared(Records[0].UnitId));
        if (AUnits.Num() == 1) Test->TestTrue(TEXT("A uses second member shared data"), AUnits[0]->GetUnitDataShared() == Units->GetUnitDataShared(Records[1].UnitId));

        Test->TestFalse(TEXT("Count mismatch rejects whole request"), Handler->SpawnSquads({SquadIds[0]}, Error));
        Test->TestTrue(TEXT("Count error contains both counts"), Error.ToString().Contains(TEXT("小队数量")) && Error.ToString().Contains(TEXT("生成器数量")));
        Test->TestTrue(TEXT("Count mismatch leaves both existing teams untouched"), B->GetSpawnedUnits() == BUnits && A->GetSpawnedUnits() == AUnits);
        Test->TestEqual(TEXT("Last handler error is available"), Handler->GetLastHandlerError().ToString(), Error.ToString());
        Test->TestFalse(TEXT("Bad second ID rejects before replacing first team"), Handler->SpawnSquads({SquadIds[1], FGuid::NewGuid()}, Error));
        Test->TestTrue(TEXT("Entire batch is preflighted before mutation"), B->GetSpawnedUnits() == BUnits && A->GetSpawnedUnits() == AUnits);
        const FVector2D PreviousSpacing = A->Spacing;
        A->Spacing = FVector2D::ZeroVector;
        Test->TestFalse(TEXT("Bad later formation rejects whole batch"), Handler->SpawnSquads({SquadIds[1], SquadIds[0]}, Error));
        Test->TestTrue(TEXT("Later formation failure does not replace first team"), B->GetSpawnedUnits() == BUnits);
        A->Spacing = PreviousSpacing;
        Test->TestTrue(TEXT("Explicit unregister succeeds"), Handler->UnregisterSquadSpawner(A, Error));
        Test->TestTrue(TEXT("Explicit unregister keeps remaining order"), Handler->GetRegisteredSquadSpawners() == TArray<AUnitSquadSpawner*>{B});
        Test->TestTrue(TEXT("Re-register appends at end"), Handler->RegisterSquadSpawner(A, Error));
        B->Destroy();
        const auto Slots = Handler->GetRegisteredSquadSpawners();
        Test->TestTrue(TEXT("Destroyed registration retains null slot, never shifts ID pairing"), Slots.Num() == 2 && Slots[0] == nullptr && Slots[1] == A);
        Test->TestFalse(TEXT("Destroyed generator blocks the batch"), Handler->SpawnSquads(SquadIds, Error));
        Test->TestTrue(TEXT("Destroyed first slot never transfers first ID onto A"), A->GetSpawnedUnits() == AUnits && A->GetSpawnedSquadId() == SquadIds[1]);
        for (auto* Unit : AUnits) UnitsAtUnload.Add(Unit);
        return true;
    }
public:
    FRegistryFlow(FAutomationTestBase* InTest, UWorld* InWorld) : Test(InTest), World(InWorld) {}
    bool Update() override
    {
        if (!World.IsValid()) { Test->AddError(TEXT("Fixture world disappeared")); return true; }
        auto* Maps = World->GetSubsystem<UMTS_SubMapSubsystem>();
        if (FPlatformTime::Seconds() - Start > 60.)
        {
            Test->AddError(TEXT("Game handler registry test timed out"));
            if (Handler) Handler->UnloadMap();
            CleanupSquads();
            return true;
        }
        FMTS_SubMapInfo Map;
        FText Error;
        if (Stage == 0)
        {
            FMTS_SubMapLoadRequest Request;
            Request.MapAsset = NewObject<UMTS_SubMapDataAsset>();
            Request.MapAsset->MapID = MapID;
            Request.MapAsset->MapName = MapID;
            Request.MapAsset->MapAsset = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Engine/Maps/Entry.Entry")));
            Request.Location = FVector(200000., 0., 0.);
            Request.CompletionDelay = 0.;
            Handler.Reset(CastChecked<UGameMapHandlerTestProxy>(Maps->CreateSubMapHandler(UGameMapHandlerTestProxy::StaticClass())));
            if (!Test->TestTrue(TEXT("Load real handler-owned streaming fixture"), Maps->LoadSubMapByHandlerObject(Request, Handler.Get(), {}, Error))) return true;
            Stage = 1;
            return false;
        }
        if (Stage == 1)
        {
            if (!Maps->GetSubMapByKey(MapID, Map) || Map.LoadingPayload.Phase != EMTS_MapTransitionPhase::Completed) return false;
            CheckDeployment(Map.StreamingLevel->GetLoadedLevel());
            Test->TestTrue(TEXT("Unload through the handler"), Handler->UnloadMap());
            Test->TestTrue(TEXT("Native cleanup immediately clears registry"), Handler->GetRegisteredSquadSpawners().IsEmpty());
            Test->TestTrue(TEXT("Native cleanup clears map roster"), Handler->SquadIds.IsEmpty());
            Stage = 2;
            return false;
        }
        if (Maps->GetSubMapByKey(MapID, Map)) return false;
        for (const auto& Unit : UnitsAtUnload)
            Test->TestTrue(TEXT("Streamed-map unload destroys its spawned units"), !Unit.IsValid() || Unit->IsActorBeingDestroyed());
        Test->TestFalse(TEXT("Stale handler cannot register into unloaded map"), Handler->RegisterSquadSpawner(ForeignSpawner.Get(), Error));
        Test->TestFalse(TEXT("Stale handler cannot spawn even an empty batch"), Handler->SpawnSquads({}, Error));
        CleanupSquads();
        Test->AddInfo(TEXT("GAME_MAP_HANDLER_REGISTRY_OK ordered pairing, duplicate registration, count/preflight rejection, expired slots and unload cleanup"));
        return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameMapHandlerRegistryTest,
    "SilverChoir.GameMapHandler.SquadRegistry.RuntimeFlow", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGameMapHandlerRegistryTest::RunTest(const FString&)
{
    for (const auto& Context : GEngine->GetWorldContexts())
        if (Context.WorldType == EWorldType::Game && Context.World())
        {
            AddExpectedError(TEXT("GameMapSubMapHandler:"), EAutomationExpectedErrorFlags::Contains, 7);
            ADD_LATENT_AUTOMATION_COMMAND(GameMapHandlerTests::FRegistryFlow(this, Context.World()));
            return true;
        }
    AddError(TEXT("Run from GameMainMap -game"));
    return false;
}
#endif
