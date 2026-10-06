#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MTS_SubMapBlueprintLibrary.h"
#include "MTS_MapTransitionBlueprintLibrary.h"
#include "MTS_SubMapDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LevelStreamingDynamic.h"

class FMTSWaitVisibility : public IAutomationLatentCommand
{
public:
	FMTSWaitVisibility(FAutomationTestBase* InTest, UWorld* InWorld) : Test(InTest), World(InWorld), Start(FPlatformTime::Seconds()) {}
	bool Update() override
	{
		if (!World.IsValid()) { Test->AddError(TEXT("Lost world")); return true; }
		auto* System = World->GetSubsystem<UMTS_SubMapSubsystem>();
		if (FPlatformTime::Seconds() - Start > 30) { Test->AddError(TEXT("Visibility lifecycle timed out")); System->UnloadSubMapByID(Key); return true; }
		FMTS_SubMapInfo Info; FText Error;
		const bool bFound = UMTS_SubMapBlueprintLibrary::GetSubMapByKey(World.Get(), Key, Info);
		if (Step == 0)
		{
			if (!bFound || Info.LoadingPayload.Phase != EMTS_MapTransitionPhase::Completed) { return false; }
			Instance = Info.StreamingLevel;
			Test->TestTrue(TEXT("Loaded registry entry is visible"), Info.bIsVisible);
			Test->TestTrue(TEXT("Hide accepted"), UMTS_SubMapBlueprintLibrary::HideSubMapByKey(World.Get(), Key, Error));
			Test->TestTrue(TEXT("Repeated hide is idempotent"), System->HideSubMapByKey(Key, Error));
			Step = 1;
		}
		else if (Step == 1)
		{
			if (!bFound || Info.bIsVisible) { return false; }
			FVector Location;
			Test->TestTrue(TEXT("Hidden map location remains queryable"),UMTS_MapTransitionBlueprintLibrary::GetMapLoadingLocation(World.Get(),Key,Location));
			Test->TestTrue(TEXT("Negative loading height preserved"),Location.Equals(FVector(123,-456,-20000)));
			Test->TestEqual(TEXT("Hidden map remains Loaded"), Info.State, EMTS_SubMapState::Loaded);
			Test->TestTrue(TEXT("Same streaming instance retains loaded resources"), Info.StreamingLevel == Instance.Get() && Instance->IsLevelLoaded());
			Test->TestFalse(TEXT("Hidden target persisted"), Info.bShouldBeVisible);
			Test->TestTrue(TEXT("Show accepted"), UMTS_SubMapBlueprintLibrary::ShowSubMapByKey(World.Get(), Key, Error));
			Step = 2;
		}
		else if (Step == 2)
		{
			if (!bFound || !Info.bIsVisible) { return false; }
			Test->TestTrue(TEXT("Show reuses original instance"), Info.StreamingLevel == Instance.Get());
			System->HideSubMapByKey(Key, Error);
			Test->TestTrue(TEXT("Unload works during hide request"), System->UnloadSubMapByID(Key));
			Test->TestFalse(TEXT("Cannot show unloading map"), System->ShowSubMapByKey(Key, Error));
			Step = 3;
		}
		else if (!bFound)
		{
			FVector Location(1,2,3);
			Test->TestFalse(TEXT("Removed map location fails"),UMTS_MapTransitionBlueprintLibrary::GetMapLoadingLocation(World.Get(),Key,Location));
			Test->TestTrue(TEXT("Removed location output clears"),Location.IsZero());
			Test->TestTrue(TEXT("Missing query clears output"), Info.MapID.IsNone() && Info.StreamingLevel == nullptr);
			Test->TestFalse(TEXT("Unknown key rejected"), System->ShowSubMapByKey(Key, Error));
			Test->TestFalse(TEXT("Unknown key provides error"), Error.IsEmpty());
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<ULevelStreamingDynamic> Instance;
	FName Key = TEXT("MTS_VisibilityTest");
	double Start;
	int32 Step = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMTSVisibilityTest, "MapTransitionSystem.SubMaps.RegistryVisibility", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FMTSVisibilityTest::RunTest(const FString& Parameters)
{
	for (const auto& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType != EWorldType::Game || !Context.World()) { continue; }
		auto* World = Context.World();
		auto* Asset = NewObject<UMTS_SubMapDataAsset>();
		Asset->MapID = TEXT("MTS_VisibilityTest"); Asset->MapName = TEXT("VisibilityTest");
		Asset->MapAsset = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Engine/Maps/Entry.Entry")));
		FText Error;
		auto* System = World->GetSubsystem<UMTS_SubMapSubsystem>();
		if (!TestTrue(TEXT("Load accepted"), System->LoadSubMap(Asset, FVector(123,-456,-20000), FRotator::ZeroRotator, Error))) { return false; }
		FVector Location;
		TestTrue(TEXT("Loading instance already exposes configured location"),UMTS_MapTransitionBlueprintLibrary::GetMapLoadingLocation(World,Asset->MapID,Location));
		TestTrue(TEXT("Lookup uses requested instance offset"),Location.Equals(FVector(123,-456,-20000)));
		TestFalse(TEXT("Invalid world fails safely"),UMTS_MapTransitionBlueprintLibrary::GetMapLoadingLocation(nullptr,Asset->MapID,Location));
		TestTrue(TEXT("Invalid world clears output"),Location.IsZero());
		TestFalse(TEXT("Missing ID fails safely"),UMTS_MapTransitionBlueprintLibrary::GetMapLoadingLocation(World,TEXT("NotLoaded"),Location));
		TestFalse(TEXT("Hiding during loading rejected"), System->HideSubMapByKey(Asset->MapID, Error));
		ADD_LATENT_AUTOMATION_COMMAND(FMTSWaitVisibility(this, World)); return true;
	}
	AddError(TEXT("Requires game world")); return false;
}
#endif
