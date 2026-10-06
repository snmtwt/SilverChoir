#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GridStrategyMapSystem/Data/GSMMapData.h"
#include "GridStrategyMapSystem/Data/GSMMapDataAsset.h"
#include "GridStrategyMapSystem/Data/GSMMapSubsystem.h"
#include "GridStrategyMapSystem/Data/GSMPieceData.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UnrealType.h"

namespace
{
	UGSMMapDataAsset* MakeTestMapAsset()
	{
		UGSMMapDataAsset* Asset = NewObject<UGSMMapDataAsset>();
		for (int32 X = 0; X < 3; ++X)
		{
			FGSMTileEntry& Entry = Asset->TileEntries.AddDefaulted_GetRef();
			Entry.GridCoordinate = FIntPoint(X, 0);
			Entry.TileId = FName(*FString::Printf(TEXT("Tile_%d_0"), X));
			Entry.Navigation.bCanWalkThrough = true;
		}
		return Asset;
	}

	UGSMMapSubsystem* MakeTestMapSubsystem()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>();
		return NewObject<UGSMMapSubsystem>(GameInstance);
	}

	struct FScopedGSMTestWorld
	{
		FScopedGSMTestWorld()
		{
			const UWorld::InitializationValues InitializationValues = UWorld::InitializationValues()
				.AllowAudioPlayback(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::Num, &InitializationValues);
			if (World && GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
			}
		}

		~FScopedGSMTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine) GEngine->DestroyWorldContext(World);
			}
		}

		UWorld* World = nullptr;
	};

	UGSMMapDataAsset* MakeTestTerrainMapAsset()
	{
		UGSMMapDataAsset* Asset = MakeTestMapAsset();
		Asset->DefaultTileActorClass = AGSMTile3D::StaticClass();
		UStaticMesh* TerrainMesh = NewObject<UStaticMesh>(Asset);
		UMaterial* Material = UMaterial::GetDefaultMaterial(MD_Surface);
		TerrainMesh->GetStaticMaterials().Add(FStaticMaterial(Material));
		TerrainMesh->GetStaticMaterials().Add(FStaticMaterial(Material));
		Asset->WholeMapTerrainMesh = TerrainMesh;
		return Asset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGSMCoordinateLabelConsistencyTest,
	"GSM.Data.CoordinateLabelConsistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGSMCoordinateLabelConsistencyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("First column uses A"), GSMLayout::MakeColumnLabel(0), FString(TEXT("A")));
	TestEqual(TEXT("Twenty-sixth column uses Z"), GSMLayout::MakeColumnLabel(25), FString(TEXT("Z")));
	TestEqual(TEXT("Columns continue with AA"), GSMLayout::MakeColumnLabel(26), FString(TEXT("AA")));
	TestEqual(TEXT("Grid coordinate uses editor and 3D label format"),
		GSMLayout::MakeTileCoordinateLabel(FIntPoint(3, 3)), FString(TEXT("D4")));
	TestEqual(TEXT("Axes can independently use numeric and letter labels"),
		GSMLayout::MakeTileCoordinateLabel(
			FIntPoint(3, 3),
			EGSMCoordinateLabelType::Numbers,
			EGSMCoordinateLabelType::Letters),
		FString(TEXT("4D")));

	UGSMMapDataAsset* Asset = NewObject<UGSMMapDataAsset>();
	FGSMTileEntry& Entry = Asset->TileEntries.AddDefaulted_GetRef();
	Entry.GridCoordinate = FIntPoint(3, 3);
	Entry.Navigation.bCanWalkThrough = true;

	UGSMMapData* MapData = nullptr;
	const FGuid MapGuid = MakeTestMapSubsystem()->CreateMapData(Asset, true, MapData);
	TestTrue(TEXT("Runtime map is created for coordinate fallback test"), MapGuid.IsValid());
	TestNotNull(TEXT("Runtime fallback tile ID uses D4"), MapData ? MapData->GetTileById(TEXT("D4")) : nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGSMMapCreationAndNavigationTest,
	"GSM.Data.MapCreationAndNavigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGSMMapCreationAndNavigationTest::RunTest(const FString& Parameters)
{
	UGSMMapSubsystem* Subsystem = MakeTestMapSubsystem();
	UGSMMapDataAsset* FirstAsset = MakeTestMapAsset();
	FGSMRoadConnection RoadToSecond;
	RoadToSecond.TargetTileId = TEXT("Tile_1_0");
	RoadToSecond.bBidirectional = true;
	FirstAsset->TileEntries[0].Navigation.RoadConnections.Add(RoadToSecond);
	FGSMRoadConnection RoadToThird;
	RoadToThird.TargetTileId = TEXT("Tile_2_0");
	RoadToThird.bBidirectional = true;
	FirstAsset->TileEntries[1].Navigation.RoadConnections.Add(RoadToThird);

	UGSMMapData* FirstMap = nullptr;
	const FGuid FirstGuid = Subsystem->CreateMapData(FirstAsset, false, FirstMap);

	TestTrue(TEXT("First map GUID is valid"), FirstGuid.IsValid());
	TestEqual(TEXT("First non-explicit map becomes default"), Subsystem->GetDefaultMapData(), FirstMap);
	TestEqual(TEXT("Default map remains queryable by GUID"), Subsystem->GetMapDataByGuid(FirstGuid), FirstMap);

	FGSMPathResult Path;
	TestTrue(TEXT("Data-only walking path succeeds"),
		FirstMap && FirstMap->FindPathByTileIds(TEXT("Tile_0_0"), TEXT("Tile_2_0"), EGSMNavigationMode::WalkingOnly, Path));
	TestEqual(TEXT("Path contains all three tile data objects"), Path.PathTiles.Num(), 3);
	TestTrue(TEXT("Path does not require a 3D map"), FirstMap && FirstMap->GetMap3D() == nullptr);
	TestTrue(TEXT("Road-only navigation is enabled by explicit road connections"),
		FirstMap && FirstMap->FindPathByTileIds(TEXT("Tile_0_0"), TEXT("Tile_2_0"), EGSMNavigationMode::RoadOnly, Path));
	TestEqual(TEXT("Road-only path follows both explicit links"), Path.PathTiles.Num(), 3);

	UGSMMapData* SecondMap = nullptr;
	const FGuid SecondGuid = Subsystem->CreateMapData(MakeTestMapAsset(), false, SecondMap);
	TestTrue(TEXT("Second map GUID is valid"), SecondGuid.IsValid());
	TestEqual(TEXT("Second map uses GUID map lookup"), Subsystem->GetMapDataByGuid(SecondGuid), SecondMap);
	TestEqual(TEXT("Default map is unchanged"), Subsystem->GetDefaultMapData(), FirstMap);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGSMNavigationModeRulesTest,
	"GSM.Data.NavigationModeRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGSMNavigationModeRulesTest::RunTest(const FString& Parameters)
{
	UGSMMapSubsystem* Subsystem = MakeTestMapSubsystem();

	UGSMMapDataAsset* CostAsset = MakeTestMapAsset();
	FGSMRoadConnection CheapDirectRoad;
	CheapDirectRoad.TargetTileId = TEXT("Tile_2_0");
	CheapDirectRoad.CostOverride = 0.1f;
	CheapDirectRoad.bBidirectional = true;
	CostAsset->TileEntries[0].Navigation.RoadConnections.Add(CheapDirectRoad);

	UGSMMapData* CostMap = nullptr;
	Subsystem->CreateMapData(CostAsset, false, CostMap);

	FGSMPathResult Path;
	TestTrue(TEXT("Walking-only navigation succeeds through walkable adjacent tiles"),
		CostMap && CostMap->FindPathByTileIds(TEXT("Tile_0_0"), TEXT("Tile_2_0"), EGSMNavigationMode::WalkingOnly, Path));
	TestEqual(TEXT("Walking-only navigation ignores the direct road"), Path.PathTiles.Num(), 3);
	TestTrue(TEXT("Walking-only path uses only walking links"),
		Path.PathLinkTypes.Num() == 2
		&& Path.PathLinkTypes[0] == EGSMNavigationLinkType::Walking
		&& Path.PathLinkTypes[1] == EGSMNavigationLinkType::Walking);

	TestTrue(TEXT("Road-preferred navigation succeeds"),
		CostMap && CostMap->FindPathByTileIds(TEXT("Tile_0_0"), TEXT("Tile_2_0"), EGSMNavigationMode::RoadPreferred, Path));
	TestEqual(TEXT("Road-preferred navigation selects the lowest-cost direct road"), Path.PathTiles.Num(), 2);
	TestTrue(TEXT("Road-preferred direct path records a road link"),
		Path.PathLinkTypes.Num() == 1 && Path.PathLinkTypes[0] == EGSMNavigationLinkType::Road);

	TestFalse(TEXT("Road-only navigation rejects a target without a complete road connection"),
		CostMap && CostMap->FindPathByTileIds(TEXT("Tile_0_0"), TEXT("Tile_1_0"), EGSMNavigationMode::RoadOnly, Path));
	TestTrue(TEXT("Road-only navigation accepts a fully road-connected target"),
		CostMap && CostMap->FindPathByTileIds(TEXT("Tile_0_0"), TEXT("Tile_2_0"), EGSMNavigationMode::RoadOnly, Path));
	TestTrue(TEXT("Road-only path contains only road links"),
		Path.PathLinkTypes.Num() == 1 && Path.PathLinkTypes[0] == EGSMNavigationLinkType::Road);

	UGSMMapDataAsset* BlockedGoalAsset = MakeTestMapAsset();
	BlockedGoalAsset->TileEntries[1].Navigation.bCanWalkThrough = false;
	BlockedGoalAsset->TileEntries[2].Navigation.bCanWalkThrough = false;
	BlockedGoalAsset->TileEntries[0].Navigation.RoadConnections.Add(CheapDirectRoad);

	UGSMMapData* BlockedGoalMap = nullptr;
	Subsystem->CreateMapData(BlockedGoalAsset, false, BlockedGoalMap);

	TestFalse(TEXT("Walking-only navigation rejects a non-walkable goal"),
		BlockedGoalMap && BlockedGoalMap->FindPathByTileIds(TEXT("Tile_0_0"), TEXT("Tile_2_0"), EGSMNavigationMode::WalkingOnly, Path));
	TestFalse(TEXT("Road-preferred navigation rejects a non-walkable goal"),
		BlockedGoalMap && BlockedGoalMap->FindPathByTileIds(TEXT("Tile_0_0"), TEXT("Tile_2_0"), EGSMNavigationMode::RoadPreferred, Path));
	TestFalse(TEXT("Road-only navigation rejects a non-walkable goal"),
		BlockedGoalMap && BlockedGoalMap->FindPathByTileIds(TEXT("Tile_0_0"), TEXT("Tile_2_0"), EGSMNavigationMode::RoadOnly, Path));
	TestTrue(TEXT("Flying navigation ignores roads and walkability"),
		BlockedGoalMap && BlockedGoalMap->FindPathByTileIds(TEXT("Tile_0_0"), TEXT("Tile_2_0"), EGSMNavigationMode::Flying, Path));
	TestEqual(TEXT("Flying navigation follows adjacent tiles rather than the direct road"), Path.PathTiles.Num(), 3);
	TestTrue(TEXT("Flying path contains only flying links"),
		Path.PathLinkTypes.Num() == 2
		&& Path.PathLinkTypes[0] == EGSMNavigationLinkType::Flying
		&& Path.PathLinkTypes[1] == EGSMNavigationLinkType::Flying);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGSMPieceLifecycleTest,
	"GSM.Data.PieceLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGSMPieceLifecycleTest::RunTest(const FString& Parameters)
{
	UGSMMapSubsystem* Subsystem = MakeTestMapSubsystem();
	UGSMMapData* MapData = nullptr;
	const FGuid MapGuid = Subsystem->CreateMapData(MakeTestMapAsset(), true, MapData);

	FGSMPiecePlacement Placement;
	Placement.RelativeTileXY = FVector2D(12.0, -8.0);
	Placement.RelativeTileYaw = 45.0f;
	UGSMPieceData* PieceData = nullptr;
	const FGuid PieceGuid = MapData->AddPieceToTileById(
		TEXT("Tile_0_0"), FGuid(), nullptr, nullptr, Placement, PieceData);

	TestTrue(TEXT("Generated piece GUID is valid"), PieceGuid.IsValid());
	TestEqual(TEXT("Piece records map GUID"), PieceData ? PieceData->GetMapGuid() : FGuid(), MapGuid);
	TestEqual(TEXT("Piece records source tile"), PieceData ? PieceData->GetTileId() : NAME_None, FName(TEXT("Tile_0_0")));
	UGSMTileData* SourceTileData = MapData->GetTileById(TEXT("Tile_0_0"));
	UGSMTileData* TargetTileData = MapData->GetTileById(TEXT("Tile_2_0"));
	int32 TileChangedCount = 0;
	UGSMTileData* NotifiedPreviousTileData = nullptr;
	UGSMTileData* NotifiedCurrentTileData = nullptr;
	PieceData->OnPieceTileChangedNative.AddLambda(
		[&TileChangedCount, &NotifiedPreviousTileData, &NotifiedCurrentTileData](
			UGSMPieceData* ChangedPieceData,
			UGSMTileData* PreviousTileData,
			UGSMTileData* CurrentTileData)
		{
			++TileChangedCount;
			NotifiedPreviousTileData = PreviousTileData;
			NotifiedCurrentTileData = CurrentTileData;
		});

	FGSMPiecePlacement TargetPlacement = Placement;
	TargetPlacement.RelativeTileXY = FVector2D(-20.0, 15.0);
	UGSMPieceData* MovedPiece = nullptr;
	TestTrue(TEXT("Piece moves using only piece GUID and target tile ID"),
		MapData->MovePieceByGuidToTileId(PieceGuid, TEXT("Tile_2_0"), TargetPlacement, MovedPiece));
	TestEqual(TEXT("Move updates owning tile"), MovedPiece ? MovedPiece->GetTileId() : NAME_None, FName(TEXT("Tile_2_0")));
	TestEqual(TEXT("Move updates placement"), MovedPiece ? MovedPiece->GetPlacement().RelativeTileXY : FVector2D::ZeroVector,
		TargetPlacement.RelativeTileXY);
	TestEqual(TEXT("Cross-tile move emits one tile-change notification"), TileChangedCount, 1);
	TestEqual(TEXT("Tile-change notification contains source tile"), NotifiedPreviousTileData, SourceTileData);
	TestEqual(TEXT("Tile-change notification contains target tile"), NotifiedCurrentTileData, TargetTileData);

	TargetPlacement.RelativeTileYaw = 90.0f;
	TestTrue(TEXT("Placement can update without leaving the current tile"),
		MapData->MovePieceDataToTile(MovedPiece, TargetTileData, TargetPlacement));
	TestEqual(TEXT("Same-tile placement update does not emit a cross-tile notification"), TileChangedCount, 1);

	TestTrue(TEXT("Piece can be removed by GUID"), MapData->RemovePieceByGuid(PieceGuid));
	TestNull(TEXT("Removed piece is no longer queryable"), MapData->GetPieceByGuid(PieceGuid));
	TestFalse(TEXT("An externally referenced removed piece is invalidated"), PieceData->IsPieceDataValid());
	TestFalse(TEXT("Removed piece GUID is invalidated"), PieceData->GetPieceGuid().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGSMDeepMapDataClearTest,
	"GSM.Data.DeepMapDataClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGSMDeepMapDataClearTest::RunTest(const FString& Parameters)
{
	UGSMMapSubsystem* Subsystem = MakeTestMapSubsystem();
	UGSMMapData* DefaultMap = nullptr;
	UGSMMapData* SecondaryMap = nullptr;
	const FGuid DefaultGuid = Subsystem->CreateMapData(MakeTestMapAsset(), true, DefaultMap);
	const FGuid SecondaryGuid = Subsystem->CreateMapData(MakeTestMapAsset(), false, SecondaryMap);

	UGSMTileData* RetainedTile = DefaultMap->GetTileById(TEXT("Tile_0_0"));
	UGSMPieceData* RetainedPiece = nullptr;
	const FGuid PieceGuid = DefaultMap->AddPieceToTileById(
		TEXT("Tile_0_0"), FGuid(), nullptr, nullptr, FGSMPiecePlacement(), RetainedPiece);

	TestTrue(TEXT("Deep-clear test default GUID is valid"), DefaultGuid.IsValid());
	TestTrue(TEXT("Deep-clear test secondary GUID is valid"), SecondaryGuid.IsValid());
	TestTrue(TEXT("Deep-clear test piece GUID is valid"), PieceGuid.IsValid());
	TestEqual(TEXT("Subsystem contains two maps before clearing"), Subsystem->GetMapDataCount(), 2);

	TestEqual(TEXT("ClearAllMapData reports both maps"), Subsystem->ClearAllMapData(), 2);
	TestEqual(TEXT("Subsystem contains no maps after clearing"), Subsystem->GetMapDataCount(), 0);
	TestNull(TEXT("Default map slot is cleared"), Subsystem->GetDefaultMapData());
	TestNull(TEXT("Default GUID lookup is cleared"), Subsystem->GetMapDataByGuid(DefaultGuid));
	TestNull(TEXT("Secondary GUID lookup is cleared"), Subsystem->GetMapDataByGuid(SecondaryGuid));

	TestFalse(TEXT("Retained map reference is invalidated"), DefaultMap->IsMapDataValid());
	TestEqual(TEXT("Retained map has no tiles"), DefaultMap->GetTiles().Num(), 0);
	TestEqual(TEXT("Retained map has no pieces"), DefaultMap->GetPieces().Num(), 0);
	TestFalse(TEXT("Retained tile reference is invalidated"), RetainedTile->IsTileDataValid());
	TestFalse(TEXT("Retained piece reference is invalidated"), RetainedPiece->IsPieceDataValid());
	TestNull(TEXT("Retained piece no longer owns a tile"), RetainedPiece->GetTileData());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGSMIndependentMapDataTest,
	"GSM.Data.IndependentMapData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGSMIndependentMapDataTest::RunTest(const FString& Parameters)
{
	UGSMMapSubsystem* Subsystem = MakeTestMapSubsystem();
	UGSMMapData* IndependentMap = nullptr;
	const FGuid IndependentGuid = Subsystem->CreateIndependentMapData(MakeTestMapAsset(), IndependentMap);
	TestTrue(TEXT("Independent map has a valid GUID"), IndependentGuid.IsValid());
	TestNull(TEXT("Independent map does not occupy the empty default slot"), Subsystem->GetDefaultMapData());
	TestEqual(TEXT("Independent map is available by GUID"), Subsystem->GetMapDataByGuid(IndependentGuid), IndependentMap);

	UGSMMapData* DefaultMap = nullptr;
	Subsystem->CreateMapData(MakeTestMapAsset(), true, DefaultMap);
	TestEqual(TEXT("A later default map occupies its own slot"), Subsystem->GetDefaultMapData(), DefaultMap);
	TestTrue(TEXT("Creating the default preserves independent map validity"), IndependentMap && IndependentMap->IsMapDataValid());
	TestEqual(TEXT("Both maps remain registered"), Subsystem->GetMapDataCount(), 2);
	TestEqual(TEXT("Independent GUID lookup survives default creation"), Subsystem->GetMapDataByGuid(IndependentGuid), IndependentMap);
	TestTrue(TEXT("Removing independent map succeeds"), Subsystem->RemoveMapData(IndependentGuid));
	TestEqual(TEXT("Removing independent map preserves default map"), Subsystem->GetDefaultMapData(), DefaultMap);
	TestTrue(TEXT("Default map remains valid"), DefaultMap && DefaultMap->IsMapDataValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGSMTerrainMaterialSlotsTest,
	"GSM.Display3D.TerrainMaterialSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGSMTerrainMaterialSlotsTest::RunTest(const FString& Parameters)
{
	FScopedGSMTestWorld TestWorld;
	AGSMMap3D* Map = TestWorld.World ? TestWorld.World->SpawnActor<AGSMMap3D>() : nullptr;
	if (!TestNotNull(TEXT("Terrain test map was spawned"), Map))
	{
		return false;
	}

	UGSMMapDataAsset* Asset = MakeTestTerrainMapAsset();
	Asset->MapDecalReceiverStencilValue = 93;
	UMaterial* OverrideMaterial = NewObject<UMaterial>(Asset);
	Asset->WholeMapTerrainMaterialOverrides.SetNum(2);
	Asset->WholeMapTerrainMaterialOverrides[1] = OverrideMaterial;
	Map->SetMapConfig(Asset, true);
	FGSMQuadBounds Bounds;
	Bounds.CornerA = FVector2D(-200.0, -100.0);
	Bounds.CornerB = FVector2D(200.0, -100.0);
	Bounds.CornerC = FVector2D(200.0, 100.0);
	Bounds.CornerD = FVector2D(-200.0, 100.0);
	Map->SetMapBounds(Bounds, true, true);

	UStaticMeshComponent* Terrain = Map->GetMapTerrainMeshComponent();
	TestEqual(TEXT("Both terrain material slots are retained"), Terrain->GetNumMaterials(), 2);
	for (int32 MaterialIndex = 0; MaterialIndex < 2; ++MaterialIndex)
	{
		UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Terrain->GetMaterial(MaterialIndex));
		if (!TestNotNull(FString::Printf(TEXT("Terrain slot %d has a dynamic material"), MaterialIndex), Material))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("Terrain slot %d enables clipping"), MaterialIndex),
			Material->K2_GetScalarParameterValue(TEXT("GSM_MapBoundsMaskEnabled")), 1.0f);
		TestEqual(FString::Printf(TEXT("Terrain slot %d receives the correct bounds"), MaterialIndex),
			Material->K2_GetVectorParameterValue(TEXT("GSM_MapBoundsHalfSize")), FLinearColor(200.0f, 100.0f, 0.0f, 0.0f));
		TestEqual(FString::Printf(TEXT("Terrain slot %d receives the stencil value"), MaterialIndex),
			Material->K2_GetScalarParameterValue(TEXT("GSM_DecalReceiverStencilValue")), 93.0f);
		UMaterialInterface* ExpectedParent = MaterialIndex == 1 ? OverrideMaterial : UMaterial::GetDefaultMaterial(MD_Surface);
		TestEqual(FString::Printf(TEXT("Terrain slot %d preserves its source material"), MaterialIndex),
			Material->Parent.Get(), ExpectedParent);
	}
	TestEqual(TEXT("Material override does not modify the original mesh"),
		Asset->WholeMapTerrainMesh->GetMaterial(1), static_cast<UMaterialInterface*>(UMaterial::GetDefaultMaterial(MD_Surface)));
	Map->RefreshMapTerrainMesh();
	UMaterialInstanceDynamic* RefreshedOverride = Cast<UMaterialInstanceDynamic>(Terrain->GetMaterial(1));
	if (TestNotNull(TEXT("Refreshing terrain recreates the override MID"), RefreshedOverride))
	{
		TestEqual(TEXT("Refreshing terrain keeps the configured material override"),
			RefreshedOverride->Parent.Get(), static_cast<UMaterialInterface*>(OverrideMaterial));
		TestEqual(TEXT("Refreshing terrain preserves clipping"),
			RefreshedOverride->K2_GetScalarParameterValue(TEXT("GSM_MapBoundsMaskEnabled")), 1.0f);
		TestEqual(TEXT("Refreshing terrain preserves clipping bounds"),
			RefreshedOverride->K2_GetVectorParameterValue(TEXT("GSM_MapBoundsHalfSize")), FLinearColor(200.0f, 100.0f, 0.0f, 0.0f));
	}

	Map->SetMapBoundsEnabled(false, true);
	for (int32 MaterialIndex = 0; MaterialIndex < 2; ++MaterialIndex)
	{
		if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Terrain->GetMaterial(MaterialIndex)))
		{
			TestEqual(FString::Printf(TEXT("Terrain slot %d disables clipping"), MaterialIndex),
				Material->K2_GetScalarParameterValue(TEXT("GSM_MapBoundsMaskEnabled")), 0.0f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGSMTerrainClearLifecycleTest,
	"GSM.Display3D.TerrainClearLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FGSMTerrainClearLifecycleTest::RunTest(const FString& Parameters)
{
	FScopedGSMTestWorld TestWorld;
	AGSMMap3D* Map = TestWorld.World ? TestWorld.World->SpawnActor<AGSMMap3D>() : nullptr;
	if (!TestNotNull(TEXT("Terrain lifecycle test map was spawned"), Map))
	{
		return false;
	}

	UGSMMapDataAsset* Asset = MakeTestTerrainMapAsset();
	Map->SetMapConfig(Asset, true);
	UStaticMeshComponent* Terrain = Map->GetMapTerrainMeshComponent();
	TestNotNull(TEXT("Config creates a terrain mesh"), Terrain->GetStaticMesh().Get());
	TestNotNull(TEXT("Config creates tile displays"), Map->GetTileById(TEXT("Tile_0_0")));
	Map->SetMapConfig(nullptr, true);
	TestNull(TEXT("Clearing config removes the terrain mesh"), Terrain->GetStaticMesh().Get());
	TestFalse(TEXT("Clearing config hides the terrain component"), Terrain->IsVisible());
	TestNull(TEXT("Clearing config removes tile displays"), Map->GetTileById(TEXT("Tile_0_0")));

	UGSMMapSubsystem* Subsystem = MakeTestMapSubsystem();
	UGSMMapData* MapData = nullptr;
	const FGuid MapGuid = Subsystem->CreateMapData(Asset, true, MapData);
	if (!TestTrue(TEXT("Terrain lifecycle map data is valid"), MapGuid.IsValid() && MapData != nullptr))
	{
		return false;
	}
	// Supply the two-way binding normally assigned by BeginPlay without initializing unrelated game subsystems.
	FObjectPropertyBase* MapDataProperty = FindFProperty<FObjectPropertyBase>(AGSMMap3D::StaticClass(), TEXT("MapData"));
	FObjectPropertyBase* Map3DProperty = FindFProperty<FObjectPropertyBase>(UGSMMapData::StaticClass(), TEXT("Map3D"));
	if (!TestNotNull(TEXT("Map data binding property exists"), MapDataProperty)
		|| !TestNotNull(TEXT("Map display binding property exists"), Map3DProperty))
	{
		return false;
	}
	MapDataProperty->SetObjectPropertyValue_InContainer(Map, MapData);
	Map3DProperty->SetObjectPropertyValue_InContainer(MapData, Map);
	TestTrue(TEXT("Registered data restores the terrain and tiles"), Map->LoadMapFromData());
	TestNotNull(TEXT("Terrain is present before deep clear"), Terrain->GetStaticMesh().Get());
	MapData->ClearAllData();
	TestNull(TEXT("Deep clear removes the terrain mesh"), Terrain->GetStaticMesh().Get());
	TestFalse(TEXT("Deep clear hides the terrain component"), Terrain->IsVisible());
	TestNull(TEXT("Deep clear releases the 3D data reference"), Map->GetMapData());
	TestNull(TEXT("Deep clear removes tile displays"), Map->GetTileById(TEXT("Tile_0_0")));
	return true;
}

#endif
