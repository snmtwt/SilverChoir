#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "MTS_SubMapBlueprintLibrary.h"
#include "MTS_SubMapDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LevelStreamingDynamic.h"
#include "NativeGameplayTags.h"
#include "MTS_SubMapTestHandler.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MTS_TestParent, "MTS.Automation.SubMap");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MTS_TestChild, "MTS.Automation.SubMap.Room");

namespace MTSSubMapTests
{
UMTS_SubMapDataAsset* MakeAsset(FName ID)
{
	UMTS_SubMapDataAsset* Asset = NewObject<UMTS_SubMapDataAsset>();
	Asset->MapID = ID;
	Asset->MapName = TEXT("SharedRoom");
	Asset->MapTags.AddTag(TAG_MTS_TestChild);
	Asset->MapAsset = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Engine/Maps/Entry.Entry")));
	return Asset;
}

class FWaitForLifecycle : public IAutomationLatentCommand
{
public:
	FWaitForLifecycle(FAutomationTestBase* InTest, UWorld* InWorld)
		: Test(InTest), World(InWorld), Started(FPlatformTime::Seconds()) {}
	bool Update() override
	{
		if (!World.IsValid()) { Test->AddError(TEXT("Parent world disappeared")); return true; }
		UMTS_SubMapSubsystem* Subsystem = World->GetSubsystem<UMTS_SubMapSubsystem>();
		if (FPlatformTime::Seconds() - Started > 40.0)
		{
			Test->AddError(FString::Printf(TEXT("Submap lifecycle timed out at phase %d"), Phase));
			Subsystem->UnloadSubMapsByTag(TAG_MTS_TestParent);
			return true;
		}
		const TArray<FMTS_SubMapInfo> Maps = Subsystem->GetSubMaps();
		if (Phase == 0)
		{
			if (Maps.Num() != 2) { return false; }
			for (const auto& Info : Maps) { if (Info.State != EMTS_SubMapState::Loaded) { return false; } }
			Test->TestTrue(TEXT("Same map produces independent streaming instances"), Maps[0].StreamingLevel != Maps[1].StreamingLevel);
			UMTS_SubMapTestHandler* Handler = Cast<UMTS_SubMapTestHandler>(Subsystem->GetSubMapHandler(TEXT("MTS_Test_A")));
			if (Test->TestNotNull(TEXT("Manual remainder retains handler"), Handler))
			{
				Test->TestEqual(TEXT("Preload called exactly once"), Handler->PreloadCalls, 1);
				Test->TestEqual(TEXT("Loaded called exactly once"), Handler->LoadedCalls, 1);
				Test->TestTrue(TEXT("Handler has parent world"), Handler->bHadWorld);
				Test->TestTrue(TEXT("Progress delegate delivers loading stages"), Handler->ProgressEvents >= 3);
				Test->TestEqual(TEXT("Progress delegate reports cap"), Handler->LastPayload.Progress, 0.8f);
			}
			for (const auto& Info : Maps)
			{
				Test->TestEqual(TEXT("Independent automatic progress"), Info.LoadingPayload.Progress, Info.MapID == TEXT("MTS_Test_A") ? 0.8f : 1.0f);
			}
			Test->TestTrue(TEXT("Manual remainder accepted"), UMTS_SubMapBlueprintLibrary::SetSubMapProgress(World.Get(), TEXT("MTS_Test_A"), 0.95f, FText()));
			for (const auto& Info : Subsystem->GetSubMaps())
			{
				if (Info.MapID == TEXT("MTS_Test_A")) { Test->TestEqual(TEXT("Manual progress exceeds submap cap"), Info.LoadingPayload.Progress, 0.95f); }
			}
			Subsystem->SetSubMapProgress(TEXT("MTS_Test_A"), 1.0f, FText());
			Subsystem->Tick(0.0f);
			Test->TestTrue(TEXT("Completed submap retains the same handler"), Subsystem->GetSubMapHandler(TEXT("MTS_Test_A"))==Handler);
			if (Handler)
			{
				Test->TestEqual(TEXT("Completion delegate reports one"), Handler->LastPayload.Progress, 1.0f);
				Test->TestEqual(TEXT("Completion delegate reports completed phase"), Handler->LastPayload.Phase, EMTS_MapTransitionPhase::Completed);
			}
			for (const auto& Info : Maps)
			{
				Test->TestNotNull(TEXT("Real streaming level loaded"), Info.StreamingLevel->GetLoadedLevel());
				Test->TestTrue(TEXT("Level visible in parent world"), Info.StreamingLevel->IsLevelVisible());
				Test->TestTrue(TEXT("Requested position and rotation applied"), Info.StreamingLevel->LevelTransform.Equals(Info.Transform));
			}
			Test->TestTrue(TEXT("Unload by ID accepted"), Subsystem->UnloadSubMapByID(TEXT("MTS_Test_A")));
			Test->TestFalse(TEXT("Repeated unload rejected"), Subsystem->UnloadSubMapByID(TEXT("MTS_Test_A")));
			FText Error;
			Test->TestFalse(TEXT("Unloading ID remains reserved"), Subsystem->LoadSubMap(MakeAsset(TEXT("MTS_Test_A")), FVector::ZeroVector, FRotator::ZeroRotator, Error));
			Phase = 1;
		}
		else if (Phase == 1)
		{
			if (Maps.Num() != 1) { return false; }
			Test->TestEqual(TEXT("Other instance survives ID unload"), Maps[0].MapID, FName(TEXT("MTS_Test_B")));
			Test->TestEqual(TEXT("Unload name selects remaining instance"), Subsystem->UnloadSubMapsByName(TEXT("SharedRoom")), 1);
			Phase = 2;
		}
		else if (Phase == 2)
		{
			if (!Maps.IsEmpty()) { return false; }
			FText Error;
			Test->TestTrue(TEXT("ID reusable after actual unload"), Subsystem->LoadSubMapWithHandler(MakeAsset(TEXT("MTS_Test_A")), FVector::ZeroVector, FRotator::ZeroRotator, UMTS_SubMapTestHandler::StaticClass(), Error, 0.8f));
			UMTS_SubMapTestHandler* CancelledHandler = Cast<UMTS_SubMapTestHandler>(Subsystem->GetSubMapHandler(TEXT("MTS_Test_A")));
			Test->TestTrue(TEXT("Second instance reloads"), Subsystem->LoadSubMap(MakeAsset(TEXT("MTS_Test_B")), FVector::ZeroVector, FRotator::ZeroRotator, Error));
			Test->TestEqual(TEXT("Exact parent tag does not unload child tags"), Subsystem->UnloadSubMapsByTag(TAG_MTS_TestParent, true), 0);
			Test->TestEqual(TEXT("Hierarchical tag unload cancels both pending loads"), Subsystem->UnloadSubMapsByTag(TAG_MTS_TestParent), 2);
			if (CancelledHandler)
			{
				Test->TestEqual(TEXT("Cancellation reports failure once"), CancelledHandler->FailureCalls, 1);
				Test->TestEqual(TEXT("Cancelled handler does not report loaded"), CancelledHandler->LoadedCalls, 0);
			}
			Test->TestTrue(TEXT("Cancellation retains handler until actual unload"), Subsystem->GetSubMapHandler(TEXT("MTS_Test_A"))==CancelledHandler);
			Phase = 3;
		}
		else if (Phase == 3)
		{
			if (!Maps.IsEmpty()) { return false; }
			FText Error;
			Subsystem->LoadSubMap(MakeAsset(TEXT("MTS_Test_A")), FVector::ZeroVector, FRotator::ZeroRotator, Error);
			Subsystem->LoadSubMap(MakeAsset(TEXT("MTS_Test_B")), FVector::ZeroVector, FRotator::ZeroRotator, Error);
			Test->TestEqual(TEXT("Name unload handles every matching instance"), Subsystem->UnloadSubMapsByName(TEXT("SharedRoom")), 2);
			Phase = 4;
		}
		else if (Maps.IsEmpty())
		{
			Test->TestTrue(TEXT("IDs empty after unload"), Subsystem->GetAllSubMapIDs().IsEmpty());
			Test->TestTrue(TEXT("Names empty after unload"), Subsystem->GetAllSubMapNames().IsEmpty());
			Test->TestTrue(TEXT("Tag query empty after unload"), Subsystem->GetSubMapsByTag(TAG_MTS_TestParent).IsEmpty());
			return true;
		}
		return false;
	}
private:
	FAutomationTestBase* Test;
	TWeakObjectPtr<UWorld> World;
	double Started;
	int32 Phase = 0;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMTSSubMapLifecycleTest, "MapTransitionSystem.SubMaps.Lifecycle",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FMTSSubMapLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::Game) { World = Context.World(); break; }
	}
	if (!TestNotNull(TEXT("Run in a game world"), World)) { return false; }
	UMTS_SubMapSubsystem* Subsystem = UMTS_SubMapBlueprintLibrary::GetSubMapSubsystem(World);
	if (!TestNotNull(TEXT("World subsystem created"), Subsystem)) { return false; }
	if (!TestTrue(TEXT("Test requires no managed submaps"), Subsystem->GetSubMaps().IsEmpty())) { return false; }
	FText Error;
	TestFalse(TEXT("Null context is safe"), UMTS_SubMapBlueprintLibrary::LoadSubMap(nullptr, nullptr, FVector::ZeroVector, FRotator::ZeroRotator, Error));
	TestFalse(TEXT("Invalid request explains failure"), Error.IsEmpty());
	TestFalse(TEXT("Null asset rejected"), Subsystem->LoadSubMap(nullptr, FVector::ZeroVector, FRotator::ZeroRotator, Error));
	UMTS_SubMapDataAsset* Invalid = MTSSubMapTests::MakeAsset(NAME_None);
	TestFalse(TEXT("Empty ID rejected"), Subsystem->LoadSubMap(Invalid, FVector::ZeroVector, FRotator::ZeroRotator, Error));
	Invalid->MapID = TEXT("Missing");
	Invalid->MapAsset = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/MTS_Missing_Map.MTS_Missing_Map")));
	TestFalse(TEXT("Missing map rejected without recording instance"), Subsystem->LoadSubMap(Invalid, FVector::ZeroVector, FRotator::ZeroRotator, Error));
	TestTrue(TEXT("Rejected loads leave registry empty"), Subsystem->GetSubMaps().IsEmpty());
	UMTS_SubMapTestHandler::TotalFailureCalls = 0;
	TestFalse(TEXT("Handler can reject preload"), Subsystem->LoadSubMapWithHandler(MTSSubMapTests::MakeAsset(TEXT("MTS_Test_Reject")), FVector::ZeroVector, FRotator::ZeroRotator, UMTS_SubMapTestHandler::StaticClass(), Error));
	TestFalse(TEXT("Preload rejection explains failure"), Error.IsEmpty());
	TestEqual(TEXT("Preload rejection invokes failure once"), UMTS_SubMapTestHandler::TotalFailureCalls, 1);
	TestTrue(TEXT("Preload rejection clears ID"), Subsystem->GetSubMaps().IsEmpty());
	TestNull(TEXT("Preload rejection releases handler"), Subsystem->GetSubMapHandler(TEXT("MTS_Test_Reject")));
	TestFalse(TEXT("Unknown ID unload safe"), Subsystem->UnloadSubMapByID(TEXT("Missing")));
	TestEqual(TEXT("Invalid tag unload safe"), Subsystem->UnloadSubMapsByTag(FGameplayTag()), 0);
	const FVector Location(1234, 567, 89);
	const FRotator Rotation(0, 45, 0);
	TestTrue(TEXT("First map instance accepted"), UMTS_SubMapBlueprintLibrary::LoadSubMapWithHandler(World, MTSSubMapTests::MakeAsset(TEXT("MTS_Test_A")), Location, Rotation, UMTS_SubMapTestHandler::StaticClass(), Error, 0.8f));
	TestTrue(TEXT("Second instance of same map accepted"), Subsystem->LoadSubMap(MTSSubMapTests::MakeAsset(TEXT("MTS_Test_B")), -Location, Rotation, Error));
	TestFalse(TEXT("Duplicate ID rejected"), Subsystem->LoadSubMap(MTSSubMapTests::MakeAsset(TEXT("MTS_Test_A")), Location, Rotation, Error));
	TestEqual(TEXT("IDs include pending instances"), Subsystem->GetAllSubMapIDs().Num(), 2);
	TestEqual(TEXT("Names preserve duplicate names"), Subsystem->GetAllSubMapNames().Num(), 2);
	TestEqual(TEXT("Parent tag matches child tags"), Subsystem->GetSubMapsByTag(TAG_MTS_TestParent).Num(), 2);
	TestEqual(TEXT("Exact parent query excludes children"), Subsystem->GetSubMapsByTag(TAG_MTS_TestParent, true).Num(), 0);
	TestEqual(TEXT("Exact child query matches"), Subsystem->GetSubMapsByTag(TAG_MTS_TestChild, true).Num(), 2);
	ADD_LATENT_AUTOMATION_COMMAND(MTSSubMapTests::FWaitForLifecycle(this, World));
	return true;
}
#endif
