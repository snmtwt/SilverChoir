#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Map/BaseMap/BaseSandboxMap.h"
#include "GridStrategyMapSystem/Data/GSMMapDataAsset.h"
#include "GridStrategyMapSystem/Data/GSMMapSubsystem.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBaseSandboxLifecycleTest, "SilverChoir.Sandbox.Lifecycle",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FBaseSandboxLifecycleTest::RunTest(const FString& Parameters)
{
    for (const auto& Context : GEngine->GetWorldContexts())
    {
        UWorld* World = Context.World();
        if (Context.WorldType != EWorldType::Game || !World || !World->GetGameInstance()) { continue; }
        UGSMMapSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<UGSMMapSubsystem>();
        if (!TestNotNull(TEXT("Grid map subsystem"), Subsystem)) { return false; }
        const int32 OriginalCount = Subsystem->GetMapDataCount();
        UGSMMapData* OriginalDefault = Subsystem->GetDefaultMapData();
        AGSMMap3D* OriginalActive = Subsystem->GetActiveGridStrategyMap();
        UGSMMapDataAsset* Config = NewObject<UGSMMapDataAsset>();
        Config->EditorGridColumns = 1;
        Config->EditorGridRows = 1;
        Config->DefaultTileActorClass = AGSMTile3D::StaticClass();
        FGSMTileEntry Entry;
        Entry.TileId = TEXT("SandboxLifecycleTile");
        Config->TileEntries.Add(Entry);

        // Two complete load/unload cycles exercise ownership and stale-GUID cleanup.
        for (int32 Iteration = 0; Iteration < 2; ++Iteration)
        {
            const FTransform Transform(FVector(0, 0, 100000));
            ABaseSandboxMap* Sandbox = World->SpawnActorDeferred<ABaseSandboxMap>(
                ABaseSandboxMap::StaticClass(), Transform, nullptr, nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!TestNotNull(TEXT("Deferred sandbox"), Sandbox)) { return false; }
            Sandbox->SetMapConfig(Config, false);
            Sandbox->FinishSpawning(Transform);
            const FGuid CreatedGuid = Sandbox->GetMapGuid();
            TestTrue(TEXT("Placed map creates a valid dedicated GUID before registration"), CreatedGuid.IsValid());
            TestNotNull(TEXT("Placed map registers runtime data"), Sandbox->GetMapData());
            TestEqual(TEXT("Exactly one runtime map added"), Subsystem->GetMapDataCount(), OriginalCount + 1);
            if (OriginalDefault)
            {
                TestTrue(TEXT("Existing default map survives sandbox creation"), Subsystem->GetDefaultMapData() == OriginalDefault);
            }
            AGSMTile3D* Tile = Sandbox->GetTileById(Entry.TileId);
            TestNotNull(TEXT("Placed map generates its configured tile at BeginPlay"), Tile);
            Sandbox->Destroy();
            TestNull(TEXT("Destroyed sandbox releases only its runtime data"), Subsystem->GetMapDataByGuid(CreatedGuid));
            TestEqual(TEXT("Unload restores the runtime map count"), Subsystem->GetMapDataCount(), OriginalCount);
            TestTrue(TEXT("Original default map remains unchanged"), Subsystem->GetDefaultMapData() == OriginalDefault);
            TestTrue(TEXT("Generated tile is also destroyed"), !IsValid(Tile) || Tile->IsActorBeingDestroyed());
        }
        if (OriginalActive) { Subsystem->SetActiveGridStrategyMapById(OriginalActive->GetMapId()); }
        return true;
    }
    AddError(TEXT("Requires a game world with a GameInstance"));
    return false;
}

#endif
