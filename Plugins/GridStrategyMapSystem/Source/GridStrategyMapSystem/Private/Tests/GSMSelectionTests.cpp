#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/GSMSelectionTestTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GridStrategyMapSystem/Data/GSMMapDataAsset.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace GSMSelectionTests
{
    struct FFixture
    {
        UWorld* World = nullptr;
        AGSMMap3D* Map = nullptr;
        TStrongObjectPtr<UGSMSelectionTestObserver> Observer;

        FFixture() : Observer(NewObject<UGSMSelectionTestObserver>())
        {
            const UWorld::InitializationValues Values = UWorld::InitializationValues()
                .AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
            if (!World || !GEngine) return;
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            // AActor::ProcessEvent skips dynamic actor callbacks until gameplay actors are initialized.
            // Use the engine lifecycle rather than directly invoking the production destruction handler.
            World->InitializeActorsForPlay(FURL());
            Map = World->SpawnActor<AGSMMap3D>();
            if (!Map) return;
            UGSMMapDataAsset* Asset = NewObject<UGSMMapDataAsset>(Map);
            Asset->DefaultTileActorClass = AGSMSelectionTestTile::StaticClass();
            for (int32 Index = 0; Index != 3; ++Index)
            {
                FGSMTileEntry& Entry = Asset->TileEntries.AddDefaulted_GetRef();
                Entry.TileId = FName(*FString::Printf(TEXT("Selection_%d"), Index));
                Entry.GridCoordinate = FIntPoint(Index, 0);
            }
            Map->SetMapConfig(Asset, true);
            Observer->Map = Map;
            Map->OnSelectedTileChanged.AddDynamic(Observer.Get(), &UGSMSelectionTestObserver::HandleSelectionChanged);
        }

        AGSMSelectionTestTile* Tile(int32 Index) const
        {
            return Map ? Cast<AGSMSelectionTestTile>(Map->GetTileById(FName(*FString::Printf(TEXT("Selection_%d"), Index)))) : nullptr;
        }

        ~FFixture()
        {
            Observer->Action = nullptr;
            if (Map) Map->OnSelectedTileChanged.RemoveAll(Observer.Get());
            if (World)
            {
                for (TActorIterator<AGSMSelectionTestTile> It(World); It; ++It)
                {
                    It->SelectedAction = nullptr;
                    It->DeselectedAction = nullptr;
                }
                World->DestroyWorld(false);
                if (GEngine) GEngine->DestroyWorldContext(World);
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGSMSelectionLifecycleTest, "GSM.Display3D.Selection.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGSMSelectionLifecycleTest::RunTest(const FString& Parameters)
{
    GSMSelectionTests::FFixture F;
    AGSMSelectionTestTile* A = F.Tile(0);
    AGSMSelectionTestTile* B = F.Tile(1);
    if (!TestNotNull(TEXT("First tile exists"), A) || !TestNotNull(TEXT("Second tile exists"), B)) return false;
    TestTrue(TEXT("Select first tile"), F.Map->SwitchSelectedTile(A));
    TestEqual(TEXT("Selection broadcasts once"), F.Observer->Notifications, 1);
    TestEqual(TEXT("Observer reads committed selection"), F.Observer->LastObservedTile.Get(), static_cast<AGSMTile3D*>(A));
    TestTrue(TEXT("Repeated selection succeeds"), F.Map->SwitchSelectedTile(A));
    TestEqual(TEXT("Repeated selection does not broadcast"), F.Observer->Notifications, 1);
    F.Map->SwitchSelectedTile(B);
    TestTrue(TEXT("Only the new tile is highlighted"), !A->IsSelectedByMap() && B->IsSelectedByMap());
    TestTrue(TEXT("Previous tile is actually destroyed"), A->Destroy());
    TestEqual(TEXT("Destroyed previous tile cannot clear new selection"), F.Map->GetSelectedTile(), static_cast<AGSMTile3D*>(B));
    TestEqual(TEXT("Previous tile destruction sends no notification"), F.Observer->Notifications, 2);
    F.Map->ClearSelectedTile();
    TestNull(TEXT("Explicit clear removes selection"), F.Map->GetSelectedTile());
    TestFalse(TEXT("Explicit clear removes highlight"), B->IsSelectedByMap());
    TestEqual(TEXT("Explicit clear broadcasts once"), F.Observer->Notifications, 3);
    F.Map->ClearSelectedTile();
    TestEqual(TEXT("Empty clear does not broadcast"), F.Observer->Notifications, 3);
    F.Map->SwitchSelectedTile(B);
    TestTrue(TEXT("Selected tile is actually destroyed"), B->Destroy());
    TestNull(TEXT("Destroying selected tile clears selection"), F.Map->GetSelectedTile());
    TestEqual(TEXT("Selected destruction broadcasts once"), F.Observer->Notifications, 5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGSMSelectionRebuildTest, "GSM.Display3D.Selection.RebuildAndCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGSMSelectionRebuildTest::RunTest(const FString& Parameters)
{
    GSMSelectionTests::FFixture F;
    AGSMSelectionTestTile* A = F.Tile(0);
    AGSMSelectionTestTile* B = F.Tile(1);
    if (!TestNotNull(TEXT("First tile exists"), A) || !TestNotNull(TEXT("Second tile exists"), B)) return false;
    F.Map->SwitchSelectedTile(A);
    bool bReselectionAccepted = true;
    F.Observer->Action = [&]
    {
        if (!F.Map->GetSelectedTile()) bReselectionAccepted = F.Map->SwitchSelectedTile(B);
    };
    TestTrue(TEXT("Rebuild succeeds"), F.Map->LoadMapFromConfig());
    F.Observer->Action = nullptr;
    TestFalse(TEXT("Cleanup rejects selection of tiles being removed"), bReselectionAccepted);
    TestNull(TEXT("Rebuild clears the old selection"), F.Map->GetSelectedTile());
    TestEqual(TEXT("Rebuild sends one clear notification"), F.Observer->Notifications, 2);
    TestNotNull(TEXT("Rebuild creates fresh tile actors"), F.Tile(0));
    F.Map->SwitchSelectedTile(F.Tile(0));
    F.Map->ClearMapTiles();
    TestNull(TEXT("Map clear releases selection"), F.Map->GetSelectedTile());
    TestEqual(TEXT("Map clear sends one notification"), F.Observer->Notifications, 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGSMSelectionReentrancyTest, "GSM.Display3D.Selection.ReentrantCallbacks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGSMSelectionReentrancyTest::RunTest(const FString& Parameters)
{
    GSMSelectionTests::FFixture F;
    AGSMSelectionTestTile* A = F.Tile(0);
    AGSMSelectionTestTile* B = F.Tile(1);
    AGSMSelectionTestTile* C = F.Tile(2);
    if (!TestNotNull(TEXT("Three tiles exist"), C)) return false;
    A->SelectedAction = [&] { F.Map->SwitchSelectedTile(B); };
    F.Map->SwitchSelectedTile(A);
    A->SelectedAction = nullptr;
    TestEqual(TEXT("Tile selected callback retains its newer selection"), F.Map->GetSelectedTile(), static_cast<AGSMTile3D*>(B));
    TestTrue(TEXT("Tile callback does not leave an old highlight"), !A->IsSelectedByMap() && B->IsSelectedByMap());
    TestEqual(TEXT("No stale outer notification after tile callback"), F.Observer->Notifications, 1);

    B->DeselectedAction = [&] { F.Map->SwitchSelectedTile(C); };
    F.Map->SwitchSelectedTile(A);
    B->DeselectedAction = nullptr;
    TestEqual(TEXT("Deselected callback retains its newer selection"), F.Map->GetSelectedTile(), static_cast<AGSMTile3D*>(C));
    TestFalse(TEXT("Superseded requested tile is never highlighted"), A->IsSelectedByMap());

    F.Observer->Action = [&]
    {
        if (F.Map->GetSelectedTile() == A) F.Map->SwitchSelectedTile(B);
    };
    F.Map->SwitchSelectedTile(A);
    F.Observer->Action = nullptr;
    TestEqual(TEXT("Notification callback can replace selection"), F.Map->GetSelectedTile(), static_cast<AGSMTile3D*>(B));
    TestEqual(TEXT("Observer ends with current selection"), F.Observer->LastObservedTile.Get(), static_cast<AGSMTile3D*>(B));
    TestTrue(TEXT("Superseded reentrant selection is actually destroyed"), A->Destroy());
    TestEqual(TEXT("Reentrant change removed old destruction binding"), F.Map->GetSelectedTile(), static_cast<AGSMTile3D*>(B));

    B->DeselectedAction = [&] { F.Map->ClearMapTiles(); };
    F.Map->SwitchSelectedTile(C);
    TestNull(TEXT("Reentrant map cleanup leaves no stale selection"), F.Map->GetSelectedTile());
    TestNull(TEXT("Reentrant map cleanup removes display actors"), F.Map->GetTileById(TEXT("Selection_0")));
    return true;
}

#endif
