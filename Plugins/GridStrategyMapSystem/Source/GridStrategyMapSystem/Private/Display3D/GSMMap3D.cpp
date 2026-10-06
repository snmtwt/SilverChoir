#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GridStrategyMapSystem/Display3D/GSMBoardMeshComponent.h"
#include "GridStrategyMapSystem/Data/GSMMapData.h"
#include "GridStrategyMapSystem/Data/GSMMapDataAsset.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Data/GSMNavigationMoveData.h"
#include "GridStrategyMapSystem/Display3D/GSMPathArrow3D.h"
#include "GridStrategyMapSystem/Data/GSMSettings.h"
#include "GridStrategyMapSystem/Data/GSMMapSubsystem.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "StaticMeshResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogGSM, Log, All);

namespace
{
	const FName MapTerrainBoundsMaskEnabledParameterName(TEXT("GSM_MapBoundsMaskEnabled"));
	const FName MapTerrainBoundsCenterParameterName(TEXT("GSM_MapBoundsCenter"));
	const FName MapTerrainBoundsAxisXParameterName(TEXT("GSM_MapBoundsAxisX"));
	const FName MapTerrainBoundsAxisYParameterName(TEXT("GSM_MapBoundsAxisY"));
	const FName MapTerrainBoundsHalfSizeParameterName(TEXT("GSM_MapBoundsHalfSize"));
	const FName MapTerrainBoundsFeatherParameterName(TEXT("GSM_MapBoundsFeather"));
	const FName DecalReceiverStencilEnabledParameterName(TEXT("GSM_DecalReceiverStencilEnabled"));
	const FName DecalReceiverStencilValueParameterName(TEXT("GSM_DecalReceiverStencilValue"));
	constexpr int32 MaxVisibleTerrainHeightSamples = 50000;

	bool CalculateBarycentric2D(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		FVector& OutBarycentric
	)
	{
		const FVector2D V0 = B - A;
		const FVector2D V1 = C - A;
		const FVector2D V2 = Point - A;
		const double D00 = FVector2D::DotProduct(V0, V0);
		const double D01 = FVector2D::DotProduct(V0, V1);
		const double D11 = FVector2D::DotProduct(V1, V1);
		const double D20 = FVector2D::DotProduct(V2, V0);
		const double D21 = FVector2D::DotProduct(V2, V1);
		const double Denominator = D00 * D11 - D01 * D01;
		if (FMath::IsNearlyZero(Denominator))
		{
			return false;
		}

		const double V = (D11 * D20 - D01 * D21) / Denominator;
		const double W = (D00 * D21 - D01 * D20) / Denominator;
		const double U = 1.0 - V - W;
		OutBarycentric = FVector(U, V, W);
		return true;
	}

	FBox BuildRotatedScaledMeshBoundsBox(const FBoxSphereBounds& MeshBounds, const FVector& Scale, const FRotator& Rotation)
	{
		const FVector Min = MeshBounds.Origin - MeshBounds.BoxExtent;
		const FVector Max = MeshBounds.Origin + MeshBounds.BoxExtent;
		FBox BoundsBox(ForceInit);

		for (int32 XIndex = 0; XIndex < 2; ++XIndex)
		{
			for (int32 YIndex = 0; YIndex < 2; ++YIndex)
			{
				for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
				{
					const FVector Corner(
						XIndex == 0 ? Min.X : Max.X,
						YIndex == 0 ? Min.Y : Max.Y,
						ZIndex == 0 ? Min.Z : Max.Z
					);
					const FVector ScaledCorner(
						Corner.X * Scale.X,
						Corner.Y * Scale.Y,
						Corner.Z * Scale.Z
					);
					BoundsBox += Rotation.RotateVector(ScaledCorner);
				}
			}
		}

		return BoundsBox;
	}
}

AGSMMap3D::AGSMMap3D()
{
	PrimaryActorTick.bCanEverTick = false;

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootSceneComponent);

	BoardMeshComponent = CreateDefaultSubobject<UGSMBoardMeshComponent>(TEXT("BoardMesh"));
	BoardMeshComponent->SetupAttachment(RootSceneComponent);

	MapTerrainMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MapTerrainMesh"));
	MapTerrainMeshComponent->SetupAttachment(RootSceneComponent);
	MapTerrainMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MapTerrainMeshComponent->SetGenerateOverlapEvents(false);
	MapTerrainMeshComponent->SetCanEverAffectNavigation(false);
	MapTerrainMeshComponent->SetReceivesDecals(true);
	MapTerrainMeshComponent->SetCastShadow(true);

	// 默认采用 axial 六边形坐标的 6 个邻接方向。
	HexWalkingNeighborOffsets = {
		FIntPoint(1, 0),
		FIntPoint(-1, 0),
		FIntPoint(0, 1),
		FIntPoint(0, -1),
		FIntPoint(1, -1),
		FIntPoint(-1, 1)
	};
}

void AGSMMap3D::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (BoardMeshComponent)
	{
		BoardMeshComponent->RebuildBoardMesh();
	}

	if (bUseBoardGrooveAsMapBounds)
	{
		ApplyBoardGrooveBoundsToMapBounds(false);
	}

	RefreshMapTerrainMesh();
	RefreshCoordinateLabels();
}

void AGSMMap3D::BeginPlay()
{
	bClearingOrEndingMap = false;
	Super::BeginPlay();

	RegisterWithMapSubsystem();
	ConfigurePlayerControllerClickEvents();

	if (bLoadOnBeginPlay && MapData)
	{
		LoadMapFromData();
	}
	else if (bLoadOnBeginPlay)
	{
		UE_LOG(LogGSM, Warning, TEXT("Map %s did not load because no map data was registered."), *GetName());
	}
}

void AGSMMap3D::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bClearingOrEndingMap = true;
	ClearSelectedTile();
	UnregisterFromMapSubsystem();
	Super::EndPlay(EndPlayReason);
}

void AGSMMap3D::ConfigurePlayerControllerClickEvents() const
{
	if (!bAutoConfigurePlayerClickEvents && !bAutoConfigurePlayerHoverEvents)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator ControllerIt = World->GetPlayerControllerIterator(); ControllerIt; ++ControllerIt)
	{
		APlayerController* PlayerController = ControllerIt->Get();
		if (!IsValid(PlayerController))
		{
			continue;
		}

		if (bAutoConfigurePlayerHoverEvents)
		{
			PlayerController->bEnableMouseOverEvents = true;
		}

		if (!bAutoConfigurePlayerClickEvents)
		{
			continue;
		}

		PlayerController->bEnableClickEvents = true;

		if (!PlayerController->ClickEventKeys.Contains(EKeys::LeftMouseButton))
		{
			PlayerController->ClickEventKeys.Add(EKeys::LeftMouseButton);
		}

		if (bEnableRightMouseTileClick && !PlayerController->ClickEventKeys.Contains(EKeys::RightMouseButton))
		{
			PlayerController->ClickEventKeys.Add(EKeys::RightMouseButton);
		}
	}
}

bool AGSMMap3D::LoadMapFromConfig()
{
	if (!MapConfig)
	{
		UE_LOG(LogGSM, Warning, TEXT("LoadMapFromConfig failed: MapConfig is not set on %s."), *GetName());
		return false;
	}

	ApplyPathArrowClassFromMapConfig();

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (bUseBoardGrooveAsMapBounds)
	{
		ApplyBoardGrooveBoundsToMapBounds(false);
	}

	ClearMapTiles();

	int32 GridColumnCount = 1;
	int32 GridRowCount = 1;
	GSMLayout::GetGridSizeFromTileEntries(
		MapConfig->TileEntries,
		MapConfig->EditorGridColumns,
		MapConfig->EditorGridRows,
		GridColumnCount,
		GridRowCount
	);

	const float SquareTileSize = GSMLayout::FixedSquareTileSize;
	RuntimeBaseSquareTileSize = SquareTileSize;

	RuntimeUnscaledMapContentSize = GSMLayout::GetGridContentSize(
		GridColumnCount,
		GridRowCount,
		SquareTileSize
	);
	RuntimeMapFitScaleMultiplier = CalculateMapFitScaleMultiplier();
	CurrentMapScale = FMath::Clamp(
		MapConfig->DefaultViewScale,
		GetEffectiveMinMapScale(),
		GetEffectiveMaxMapScale());

	const FVector GrooveFloorCenter = BoardMeshComponent
		? BoardMeshComponent->GetLocalGrooveFloorCenter()
		: FVector::ZeroVector;

	TArray<FGSMTileEntry> RuntimeTileEntries;
	RuntimeTileEntries.Reserve(MapConfig->TileEntries.Num());

	for (int32 TileIndex = 0; TileIndex < MapConfig->TileEntries.Num(); ++TileIndex)
	{
		FGSMTileEntry RuntimeTileEntry = MapConfig->TileEntries[TileIndex];
		RuntimeTileEntry.TileId = MakeRuntimeTileId(RuntimeTileEntry, TileIndex);

		FTransform RuntimeTileTransform = RuntimeTileEntry.LocalTransform;
		RuntimeTileTransform.SetLocation(GSMLayout::MakeTopLeftGridTileLocalLocation(
			RuntimeTileEntry.GridCoordinate,
			GridColumnCount,
			GridRowCount,
			SquareTileSize,
			GrooveFloorCenter.Z
		));
		RuntimeTileEntry.LocalTransform = RuntimeTileTransform;

		FGSMRegionDefinition RegionDefinition;
		const bool bHasRegionDefinition = MapConfig->GetRegionDefinition(RuntimeTileEntry.RegionId, RegionDefinition);

		TSubclassOf<AGSMTile3D> TileClass = MapConfig->ResolveTileActorClass(RuntimeTileEntry);
		if (!TileClass)
		{
			TileClass = AGSMTile3D::StaticClass();
		}

		const FTransform ScaledTileLocalTransform = MakeScaledTileLocalTransform(RuntimeTileEntry.LocalTransform);
		const FTransform WorldTransform = ScaledTileLocalTransform * GetActorTransform();

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.OverrideLevel = GetLevel();
		SpawnParameters.SpawnCollisionHandlingOverride = TileSpawnCollisionHandling;

		AGSMTile3D* TileActor = World->SpawnActor<AGSMTile3D>(
			TileClass,
			WorldTransform,
			SpawnParameters
		);

		if (!TileActor)
		{
			UE_LOG(
				LogGSM,
				Warning,
				TEXT("LoadMapFromConfig failed to spawn tile %s on %s."),
				*RuntimeTileEntry.TileId.ToString(),
				*GetName()
			);
			continue;
		}

		TileActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		TileActor->SetActorRelativeTransform(ScaledTileLocalTransform);
		TileActor->SetRuntimeMapScale(GetEffectiveMapContentScale());
		TileActor->SetRuntimeSquareTileSize(GetScaledRuntimeSquareTileSize());
		TileActor->SetTileStaticMeshVisualEnabled(false);
		TileActor->InitializeMapTile(
			RuntimeTileEntry,
			bHasRegionDefinition ? RegionDefinition.bCanBeClicked : true
		);
		if (MapData)
		{
			TileActor->BindTileData(MapData->GetTileById(RuntimeTileEntry.TileId));
		}
		TileActor->SetDecalReceiverStencil(
			MapConfig ? MapConfig->bUseCustomStencilForMapDecals : bUseCustomStencilForMapDecals,
			MapConfig ? MapConfig->MapDecalReceiverStencilValue : MapDecalReceiverStencilValue,
			MapConfig ? MapConfig->bMarkTileMeshesAsDecalReceivers : bMarkTileMeshesAsDecalReceivers
		);

		SpawnedTiles.Add(TileActor);
		SpawnedTilesById.Add(RuntimeTileEntry.TileId, TileActor);
		SpawnedTilesByGridCoordinate.Add(RuntimeTileEntry.GridCoordinate, TileActor);
		RuntimeTileEntries.Add(RuntimeTileEntry);
	}

	const FVector2D ViewportCenter = GetMapScaleViewportCenter();
	FVector2D DesiredFocusLocation = FVector2D::ZeroVector;
	bool bHasDesiredFocusLocation = false;

	const FIntPoint DefaultFocusCoordinate(
		FMath::RoundToInt(MapConfig->DefaultViewCenter.X) - 1,
		FMath::RoundToInt(MapConfig->DefaultViewCenter.Y) - 1);
	if (MapConfig->DefaultViewCenter.X >= 1.0f && MapConfig->DefaultViewCenter.Y >= 1.0f)
	{
		if (const TWeakObjectPtr<AGSMTile3D>* FocusTilePtr = SpawnedTilesByGridCoordinate.Find(DefaultFocusCoordinate))
		{
			if (const AGSMTile3D* FocusTile = FocusTilePtr->Get())
			{
				const FVector FocusLocalLocation = FocusTile->GetActorTransform().GetRelativeTransform(GetActorTransform()).GetLocation();
				DesiredFocusLocation = FVector2D(FocusLocalLocation.X, FocusLocalLocation.Y);
				bHasDesiredFocusLocation = true;
			}
		}
	}

	if (!bHasDesiredFocusLocation)
	{
		FBox2D ContentBounds(ForceInit);
		if (GetSpawnedTileContentBoundsLocal(ContentBounds))
		{
			DesiredFocusLocation = ContentBounds.GetCenter();
		}
	}

	MoveSpawnedTilesByLocalDelta(ViewportCenter - DesiredFocusLocation);
	ConstrainSpawnedTilesToScaleViewport();
	RefreshMapTerrainMesh();
	RefreshMapBoundsVisibility();
	ConfigurePlayerControllerClickEvents();

	return true;
}

bool AGSMMap3D::LoadMapFromData()
{
	if (!IsValid(MapData) || !IsValid(MapData->GetMapDataAsset()))
	{
		UE_LOG(LogGSM, Warning, TEXT("LoadMapFromData failed on %s: map data is invalid."), *GetName());
		return false;
	}
	MapGuid = MapData->GetMapGuid();
	MapConfig = MapData->GetMapDataAsset();
	return LoadMapFromConfig();
}

void AGSMMap3D::RebuildMapFromConfig()
{
	LoadMapFromConfig();
}

void AGSMMap3D::GenerateMapFromEditorConfig()
{
	if (BoardMeshComponent)
	{
		BoardMeshComponent->RebuildBoardMesh();
	}

	if (bUseBoardGrooveAsMapBounds)
	{
		ApplyBoardGrooveBoundsToMapBounds(false);
	}

	RebuildMapFromConfig();
}

void AGSMMap3D::RebuildBoardMesh()
{
	if (BoardMeshComponent)
	{
		BoardMeshComponent->RebuildBoardMesh();
	}
}

void AGSMMap3D::RefreshMapTerrainMesh()
{
	if (!MapTerrainMeshComponent)
	{
		return;
	}

	const bool bUseWholeMapTerrainMesh = MapConfig != nullptr;

	if (bUseWholeMapTerrainMesh)
	{
		MapTerrainMeshComponent->SetStaticMesh(MapConfig->WholeMapTerrainMesh);
		MapTerrainMeshComponent->EmptyOverrideMaterials();
		const int32 MaterialCount = MapTerrainMeshComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < MapConfig->WholeMapTerrainMaterialOverrides.Num()
			&& MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			if (UMaterialInterface* MaterialOverride = MapConfig->WholeMapTerrainMaterialOverrides[MaterialIndex])
			{
				MapTerrainMeshComponent->SetMaterial(MaterialIndex, MaterialOverride);
			}
		}
	}
	else
	{
		MapTerrainMeshComponent->SetStaticMesh(nullptr);
		MapTerrainMeshComponent->EmptyOverrideMaterials();
	}
	MapTerrainMaterialInstances.Reset();

	MapTerrainMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MapTerrainMeshComponent->SetGenerateOverlapEvents(false);
	MapTerrainMeshComponent->SetCanEverAffectNavigation(false);

	const bool bHasTerrainMesh = bUseWholeMapTerrainMesh && MapTerrainMeshComponent->GetStaticMesh() != nullptr;
	MapTerrainMeshComponent->SetVisibility(bHasTerrainMesh);
	MapTerrainMeshComponent->SetHiddenInGame(!bHasTerrainMesh);

	if (bHasTerrainMesh)
	{
		EnsureMapTerrainMaterialInstances();
		UpdateMapTerrainMeshTransform();
	}

	ApplyDecalReceiverStencilSettings();
	FVector MaskWorldCenter, MaskWorldAxisX, MaskWorldAxisY;
	FVector2D MaskWorldHalfSize;
	GetMapMaterialBoundsMaskParameters(MaskWorldCenter, MaskWorldAxisX, MaskWorldAxisY, MaskWorldHalfSize);
	ApplyMapTerrainMaterialBoundsMask(MaskWorldCenter, MaskWorldAxisX, MaskWorldAxisY, MaskWorldHalfSize,
		MaterialBoundsMaskFeather, bUseMapBounds && bUpdateTileMaterialBoundsMask);
	RefreshSpawnedTilePieces();
}

void AGSMMap3D::ApplyBoardGrooveBoundsToMapBounds(bool bRefreshTiles)
{
	if (!BoardMeshComponent)
	{
		return;
	}

	MapBounds = BoardMeshComponent->GetLocalGrooveBounds();
	bUseMapBounds = true;

	if (bRefreshTiles)
	{
		RefreshMapBoundsVisibility();
	}
}

FGSMQuadBounds AGSMMap3D::GetBoardGrooveMapBounds() const
{
	return BoardMeshComponent ? BoardMeshComponent->GetLocalGrooveBounds() : MapBounds;
}

FName AGSMMap3D::GetMapId() const
{
	if (MapGuid.IsValid())
	{
		return FName(*MapGuid.ToString(EGuidFormats::DigitsWithHyphens));
	}
	return MapId.IsNone() ? GetFName() : MapId;
}

bool AGSMMap3D::GetMouseHitOnMapGroove(APlayerController* PlayerController, FVector& OutHitLocation) const
{
	OutHitLocation = FVector::ZeroVector;

	if (!IsValid(PlayerController) || !BoardMeshComponent)
	{
		return false;
	}

	FVector WorldRayOrigin = FVector::ZeroVector;
	FVector WorldRayDirection = FVector::ZeroVector;
	if (!PlayerController->DeprojectMousePositionToWorld(WorldRayOrigin, WorldRayDirection))
	{
		return false;
	}

	const FTransform ActorTransform = GetActorTransform();
	const FVector LocalRayOrigin = ActorTransform.InverseTransformPosition(WorldRayOrigin);
	const FVector LocalRayDirection = ActorTransform.InverseTransformVectorNoScale(WorldRayDirection).GetSafeNormal();
	if (LocalRayDirection.IsNearlyZero() || FMath::IsNearlyZero(LocalRayDirection.Z))
	{
		return false;
	}

	const float GroovePlaneZ = BoardMeshComponent->GetLocalGrooveFloorCenter().Z;
	const float HitDistance = (GroovePlaneZ - LocalRayOrigin.Z) / LocalRayDirection.Z;
	if (HitDistance < 0.0f)
	{
		return false;
	}

	const FVector LocalHitLocation = LocalRayOrigin + LocalRayDirection * HitDistance;
	if (!BoardMeshComponent->IsLocalLocationInsideGroove(LocalHitLocation))
	{
		return false;
	}

	OutHitLocation = ActorTransform.TransformPosition(LocalHitLocation);
	return true;
}

void AGSMMap3D::HandleBoardMeshChanged()
{
	if (bUseBoardGrooveAsMapBounds && BoardMeshComponent)
	{
		MapBounds = BoardMeshComponent->GetLocalGrooveBounds();
	}

	const float PreviousMapScale = CurrentMapScale;
	const float OldMapContentScale = GetEffectiveMapContentScale();
	const float ClampedScale = FMath::Clamp(CurrentMapScale, GetEffectiveMinMapScale(), GetEffectiveMaxMapScale());
	CurrentMapScale = ClampedScale;
	RuntimeMapFitScaleMultiplier = CalculateMapFitScaleMultiplier();
	if (!FMath::IsNearlyEqual(OldMapContentScale, GetEffectiveMapContentScale()))
	{
		const FVector LocalPivot = BoardMeshComponent
			? BoardMeshComponent->GetLocalGrooveFloorCenter()
			: FVector::ZeroVector;
		ApplyMapScaleToSpawnedTiles(OldMapContentScale, LocalPivot);
	}
	ConstrainSpawnedTilesToScaleViewport();
	RefreshMapTerrainMesh();
	NotifySpawnedTilesMapScaleChanged(PreviousMapScale);

	if (bUseMapBounds)
	{
		RefreshMapBoundsVisibility();
	}
}

void AGSMMap3D::ClearMapTiles()
{
	if (bClearingOrEndingMap) return;
	TGuardValue<bool> CleanupGuard(bClearingOrEndingMap, true);
	ResetTilePathNavigationState();

	if (bClearPathVisualWhenMapReloads)
	{
		ClearNavigationPath();
	}

	ClearSelectedTile();
	ClearCoordinateLabels();

	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (IsValid(TileActor))
		{
			TileActor->Destroy();
		}
	}

	SpawnedTiles.Reset();
	SpawnedTilesById.Reset();
	SpawnedTilesByGridCoordinate.Reset();
}

void AGSMMap3D::NotifyAllTilesUpdate()
{
	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (IsValid(TileActor))
		{
			TileActor->UpdateTile3D();
		}
	}
}

void AGSMMap3D::SetMapConfig(UGSMMapDataAsset* NewMapConfig, bool bReloadMap)
{
	MapConfig = NewMapConfig;
	ApplyPathArrowClassFromMapConfig();

	if (bReloadMap)
	{
		if (MapConfig)
		{
			LoadMapFromConfig();
		}
		else
		{
			ClearMapTiles();
			RefreshMapTerrainMesh();
		}
	}
	else
	{
		RefreshMapTerrainMesh();
	}
}

void AGSMMap3D::ApplyPathArrowClassFromMapConfig()
{
	if (!IsValid(MapConfig) || !MapConfig->PathArrowActorClass)
	{
		return;
	}

	const TSubclassOf<AGSMPathArrow3D> ConfiguredArrowClass = MapConfig->PathArrowActorClass;
	if (PathArrowActorClass == ConfiguredArrowClass)
	{
		return;
	}

	if (IsValid(PathArrowActor) && !PathArrowActor->IsA(ConfiguredArrowClass))
	{
		PathArrowActor->Destroy();
		PathArrowActor = nullptr;
	}

	PathArrowActorClass = ConfiguredArrowClass;
}

void AGSMMap3D::SetMapBounds(const FGSMQuadBounds& NewMapBounds, bool bEnableBounds, bool bRefreshTiles)
{
	MapBounds = NewMapBounds;
	bUseMapBounds = bEnableBounds;

	const float PreviousMapScale = CurrentMapScale;
	const float OldMapContentScale = GetEffectiveMapContentScale();
	const float ClampedScale = FMath::Clamp(CurrentMapScale, GetEffectiveMinMapScale(), GetEffectiveMaxMapScale());
	CurrentMapScale = ClampedScale;
	RuntimeMapFitScaleMultiplier = CalculateMapFitScaleMultiplier();
	if (!FMath::IsNearlyEqual(OldMapContentScale, GetEffectiveMapContentScale()))
	{
		const FVector2D BoundsCenter = GetMapScaleViewportCenter();
		ApplyMapScaleToSpawnedTiles(OldMapContentScale, FVector(BoundsCenter.X, BoundsCenter.Y, 0.0f));
	}
	ConstrainSpawnedTilesToScaleViewport();
	RefreshMapTerrainMesh();
	NotifySpawnedTilesMapScaleChanged(PreviousMapScale);

	if (bRefreshTiles)
	{
		RefreshMapBoundsVisibility();
	}
}

void AGSMMap3D::SetMapBoundsEnabled(bool bNewUseMapBounds, bool bRefreshTiles)
{
	bUseMapBounds = bNewUseMapBounds;

	const float PreviousMapScale = CurrentMapScale;
	const float OldMapContentScale = GetEffectiveMapContentScale();
	const float ClampedScale = FMath::Clamp(CurrentMapScale, GetEffectiveMinMapScale(), GetEffectiveMaxMapScale());
	CurrentMapScale = ClampedScale;
	RuntimeMapFitScaleMultiplier = CalculateMapFitScaleMultiplier();
	if (!FMath::IsNearlyEqual(OldMapContentScale, GetEffectiveMapContentScale()))
	{
		const FVector2D BoundsCenter = GetMapScaleViewportCenter();
		ApplyMapScaleToSpawnedTiles(OldMapContentScale, FVector(BoundsCenter.X, BoundsCenter.Y, 0.0f));
	}
	ConstrainSpawnedTilesToScaleViewport();
	RefreshMapTerrainMesh();
	NotifySpawnedTilesMapScaleChanged(PreviousMapScale);

	if (bRefreshTiles)
	{
		RefreshMapBoundsVisibility();
	}
}

void AGSMMap3D::RefreshMapBoundsVisibility()
{
	const bool bUseMaterialBoundsMask = bUseMapBounds && bUpdateTileMaterialBoundsMask;
	const FTransform ActorTransform = GetActorTransform();
	const FBox2D BoundsBox = MapBounds.GetBoundsBox();
	const double TileVisibilityPadding = FMath::Max(1.0, GetScaledRuntimeSquareTileSize() * 0.5);

	FVector MaskWorldCenter, MaskWorldAxisX, MaskWorldAxisY;
	FVector2D MaskWorldHalfSize;
	GetMapMaterialBoundsMaskParameters(MaskWorldCenter, MaskWorldAxisX, MaskWorldAxisY, MaskWorldHalfSize);

	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (!IsValid(TileActor))
		{
			continue;
		}

		bool bInsideBounds = true;
		if (bUseMapBounds)
		{
			const FVector LocalTileLocation = ActorTransform.InverseTransformPosition(TileActor->GetActorLocation());
			bInsideBounds =
				LocalTileLocation.X >= BoundsBox.Min.X - TileVisibilityPadding &&
				LocalTileLocation.X <= BoundsBox.Max.X + TileVisibilityPadding &&
				LocalTileLocation.Y >= BoundsBox.Min.Y - TileVisibilityPadding &&
				LocalTileLocation.Y <= BoundsBox.Max.Y + TileVisibilityPadding;
		}

		TileActor->SetHiddenByMapBounds(!bInsideBounds);
		TileActor->SetMaterialBoundsMask(
			MaskWorldCenter,
			MaskWorldAxisX,
			MaskWorldAxisY,
			MaskWorldHalfSize,
			MaterialBoundsMaskFeather,
			bUseMaterialBoundsMask
		);
		TileActor->SetDecalReceiverStencil(
			MapConfig ? MapConfig->bUseCustomStencilForMapDecals : bUseCustomStencilForMapDecals,
			MapConfig ? MapConfig->MapDecalReceiverStencilValue : MapDecalReceiverStencilValue,
			MapConfig ? MapConfig->bMarkTileMeshesAsDecalReceivers : bMarkTileMeshesAsDecalReceivers
		);
	}

	ApplyMapTerrainMaterialBoundsMask(
		MaskWorldCenter,
		MaskWorldAxisX,
		MaskWorldAxisY,
		MaskWorldHalfSize,
		MaterialBoundsMaskFeather,
		bUseMaterialBoundsMask
	);
	ApplyDecalReceiverStencilSettings();

	if (PathArrowActor)
	{
		PathArrowActor->SetMaterialBoundsMask(
			MaskWorldCenter,
			MaskWorldAxisX,
			MaskWorldAxisY,
			MaskWorldHalfSize,
			MaterialBoundsMaskFeather,
			bUseMaterialBoundsMask
		);
	}

	RefreshCoordinateLabels();
}

void AGSMMap3D::GetMapMaterialBoundsMaskParameters(FVector& OutWorldCenter, FVector& OutWorldAxisX,
	FVector& OutWorldAxisY, FVector2D& OutWorldHalfSize) const
{
	const FTransform ActorTransform = GetActorTransform();
	OutWorldCenter = ActorTransform.GetLocation();
	OutWorldAxisX = ActorTransform.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();
	OutWorldAxisY = ActorTransform.TransformVectorNoScale(FVector::RightVector).GetSafeNormal();
	OutWorldHalfSize = FVector2D::ZeroVector;
	if (bUseMapBounds)
	{
		const FVector2D LocalCenter =
			(MapBounds.CornerA + MapBounds.CornerB + MapBounds.CornerC + MapBounds.CornerD) * 0.25;
		const FVector2D LocalEdgeX = MapBounds.CornerB - MapBounds.CornerA;
		const FVector2D LocalEdgeY = MapBounds.CornerD - MapBounds.CornerA;
		const FVector WorldEdgeX = ActorTransform.TransformVector(FVector(LocalEdgeX.X, LocalEdgeX.Y, 0.0));
		const FVector WorldEdgeY = ActorTransform.TransformVector(FVector(LocalEdgeY.X, LocalEdgeY.Y, 0.0));
		OutWorldCenter = ActorTransform.TransformPosition(FVector(LocalCenter.X, LocalCenter.Y, 0.0));
		OutWorldAxisX = WorldEdgeX.GetSafeNormal(SMALL_NUMBER, OutWorldAxisX);
		OutWorldAxisY = WorldEdgeY.GetSafeNormal(SMALL_NUMBER, OutWorldAxisY);
		OutWorldHalfSize = FVector2D(WorldEdgeX.Size() * 0.5, WorldEdgeY.Size() * 0.5);
	}
}

void AGSMMap3D::EnsureMapTerrainMaterialInstances()
{
	if (!MapTerrainMeshComponent || !MapTerrainMeshComponent->GetStaticMesh())
	{
		MapTerrainMaterialInstances.Reset();
		return;
	}

	const int32 MaterialCount = MapTerrainMeshComponent->GetNumMaterials();
	MapTerrainMaterialInstances.SetNum(MaterialCount);
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		UMaterialInterface* Material = MapTerrainMeshComponent->GetMaterial(MaterialIndex);
		UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(Material);
		if (!MaterialInstance && Material)
		{
			MaterialInstance = MapTerrainMeshComponent->CreateDynamicMaterialInstance(MaterialIndex);
		}
		MapTerrainMaterialInstances[MaterialIndex] = MaterialInstance;
	}
}

void AGSMMap3D::ApplyMapTerrainMaterialBoundsMask(
	const FVector& WorldCenter,
	const FVector& WorldAxisX,
	const FVector& WorldAxisY,
	const FVector2D& WorldHalfSize,
	float Feather,
	bool bEnabled
)
{
	EnsureMapTerrainMaterialInstances();
	for (UMaterialInstanceDynamic* MaterialInstance : MapTerrainMaterialInstances)
	{
		if (!MaterialInstance)
		{
			continue;
		}
		MaterialInstance->SetScalarParameterValue(MapTerrainBoundsMaskEnabledParameterName, bEnabled ? 1.0f : 0.0f);
		MaterialInstance->SetVectorParameterValue(
			MapTerrainBoundsCenterParameterName,
			FLinearColor(WorldCenter.X, WorldCenter.Y, WorldCenter.Z, 0.0f)
		);
		MaterialInstance->SetVectorParameterValue(
			MapTerrainBoundsAxisXParameterName,
			FLinearColor(WorldAxisX.X, WorldAxisX.Y, WorldAxisX.Z, 0.0f)
		);
		MaterialInstance->SetVectorParameterValue(
			MapTerrainBoundsAxisYParameterName,
			FLinearColor(WorldAxisY.X, WorldAxisY.Y, WorldAxisY.Z, 0.0f)
		);
		MaterialInstance->SetVectorParameterValue(
			MapTerrainBoundsHalfSizeParameterName,
			FLinearColor(WorldHalfSize.X, WorldHalfSize.Y, 0.0f, 0.0f)
		);
		MaterialInstance->SetScalarParameterValue(MapTerrainBoundsFeatherParameterName, FMath::Max(0.0f, Feather));
	}
}

void AGSMMap3D::ApplyDecalReceiverStencilSettings()
{
	const bool bUseStencil = MapConfig ? MapConfig->bUseCustomStencilForMapDecals : bUseCustomStencilForMapDecals;
	const int32 StencilValue = FMath::Clamp(MapConfig ? MapConfig->MapDecalReceiverStencilValue : MapDecalReceiverStencilValue, 0, 255);
	const bool bMarkTerrain = MapConfig ? MapConfig->bMarkWholeMapTerrainAsDecalReceiver : bMarkWholeMapTerrainAsDecalReceiver;

	if (MapTerrainMeshComponent)
	{
		const bool bTerrainReceivesMapDecals = bMarkTerrain && MapTerrainMeshComponent->GetStaticMesh() != nullptr;
		MapTerrainMeshComponent->SetReceivesDecals(bTerrainReceivesMapDecals);
		MapTerrainMeshComponent->SetRenderCustomDepth(bUseStencil && bTerrainReceivesMapDecals);
		MapTerrainMeshComponent->SetCustomDepthStencilValue(StencilValue);
	}

	EnsureMapTerrainMaterialInstances();
	for (UMaterialInstanceDynamic* MaterialInstance : MapTerrainMaterialInstances)
	{
		if (MaterialInstance)
		{
			MaterialInstance->SetScalarParameterValue(DecalReceiverStencilEnabledParameterName, bUseStencil ? 1.0f : 0.0f);
			MaterialInstance->SetScalarParameterValue(DecalReceiverStencilValueParameterName, static_cast<float>(StencilValue));
		}
	}
}

bool AGSMMap3D::GetMapTerrainSurfaceWorldLocationAtWorldLocation(
	const FVector& WorldLocation,
	FVector& OutSurfaceWorldLocation
) const
{
	OutSurfaceWorldLocation = WorldLocation;

	const UStaticMesh* TerrainMesh = MapTerrainMeshComponent ? MapTerrainMeshComponent->GetStaticMesh() : nullptr;
	if (!MapTerrainMeshComponent || !MapTerrainMeshComponent->IsVisible() || !TerrainMesh || !TerrainMesh->GetRenderData() || TerrainMesh->GetRenderData()->LODResources.IsEmpty())
	{
		return false;
	}

	const FTransform TerrainTransform = MapTerrainMeshComponent->GetComponentTransform();
	const FVector TerrainLocalQuery = TerrainTransform.InverseTransformPosition(WorldLocation);
	const FVector2D QueryXY(TerrainLocalQuery.X, TerrainLocalQuery.Y);

	const FStaticMeshLODResources& LODResources = TerrainMesh->GetRenderData()->LODResources[0];
	const FPositionVertexBuffer& PositionVertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
	const FIndexArrayView Indices = LODResources.IndexBuffer.GetArrayView();
	const uint32 VertexCount = PositionVertexBuffer.GetNumVertices();
	if (VertexCount == 0 || Indices.Num() < 3)
	{
		return false;
	}

	bool bFoundTriangle = false;
	double BestTriangleDistanceSquared = TNumericLimits<double>::Max();
	double BestLocalZ = TerrainLocalQuery.Z;

	double NearestVertexDistanceSquared = TNumericLimits<double>::Max();
	double NearestVertexLocalZ = TerrainLocalQuery.Z;

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
	{
		const FVector3f VertexPosition3f = PositionVertexBuffer.VertexPosition(VertexIndex);
		const FVector2D VertexXY(VertexPosition3f.X, VertexPosition3f.Y);
		const double DistanceSquared = FVector2D::DistSquared(QueryXY, VertexXY);
		if (DistanceSquared < NearestVertexDistanceSquared)
		{
			NearestVertexDistanceSquared = DistanceSquared;
			NearestVertexLocalZ = VertexPosition3f.Z;
		}
	}

	for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
	{
		const uint32 IndexA = Indices[Index];
		const uint32 IndexB = Indices[Index + 1];
		const uint32 IndexC = Indices[Index + 2];
		if (IndexA >= VertexCount || IndexB >= VertexCount || IndexC >= VertexCount)
		{
			continue;
		}

		const FVector3f A3f = PositionVertexBuffer.VertexPosition(IndexA);
		const FVector3f B3f = PositionVertexBuffer.VertexPosition(IndexB);
		const FVector3f C3f = PositionVertexBuffer.VertexPosition(IndexC);
		const FVector2D A(A3f.X, A3f.Y);
		const FVector2D B(B3f.X, B3f.Y);
		const FVector2D C(C3f.X, C3f.Y);

		FVector Barycentric;
		if (!CalculateBarycentric2D(QueryXY, A, B, C, Barycentric))
		{
			continue;
		}

		constexpr double BarycentricTolerance = 0.0001;
		if (Barycentric.X < -BarycentricTolerance || Barycentric.Y < -BarycentricTolerance || Barycentric.Z < -BarycentricTolerance)
		{
			continue;
		}

		const FVector2D TriangleCenter = (A + B + C) / 3.0;
		const double DistanceSquared = FVector2D::DistSquared(QueryXY, TriangleCenter);
		if (!bFoundTriangle || DistanceSquared < BestTriangleDistanceSquared)
		{
			bFoundTriangle = true;
			BestTriangleDistanceSquared = DistanceSquared;
			BestLocalZ =
				static_cast<double>(A3f.Z) * Barycentric.X +
				static_cast<double>(B3f.Z) * Barycentric.Y +
				static_cast<double>(C3f.Z) * Barycentric.Z;
		}
	}

	const double ResolvedLocalZ = bFoundTriangle ? BestLocalZ : NearestVertexLocalZ;
	const FVector SurfaceLocalLocation(TerrainLocalQuery.X, TerrainLocalQuery.Y, ResolvedLocalZ);
	const FVector SurfaceWorldLocation = TerrainTransform.TransformPosition(SurfaceLocalLocation);
	OutSurfaceWorldLocation = FVector(WorldLocation.X, WorldLocation.Y, SurfaceWorldLocation.Z);
	return bFoundTriangle || NearestVertexDistanceSquared < TNumericLimits<double>::Max();
}

bool AGSMMap3D::SampleMapTerrainWorldHeightAtWorldLocation(
	const FVector& WorldLocation,
	float& OutWorldZ
) const
{
	FVector SurfaceWorldLocation = WorldLocation;
	const bool bSampled = GetMapTerrainSurfaceWorldLocationAtWorldLocation(WorldLocation, SurfaceWorldLocation);
	OutWorldZ = static_cast<float>(SurfaceWorldLocation.Z);
	return bSampled;
}

FVector AGSMMap3D::GetNavigationPathPointWorldLocation(AGSMTile3D* TileActor) const
{
	if (!IsValid(TileActor))
	{
		return FVector::ZeroVector;
	}

	const FVector TileWorldLocation = TileActor->GetActorLocation();
	if (!bUseTerrainHeightForNavigationPath)
	{
		return TileWorldLocation;
	}

	FVector SurfaceWorldLocation = TileWorldLocation;
	if (GetMapTerrainSurfaceWorldLocationAtWorldLocation(TileWorldLocation, SurfaceWorldLocation))
	{
		return SurfaceWorldLocation;
	}

	return TileWorldLocation;
}

float AGSMMap3D::AddZoomAtWorldLocation(const FVector& WorldPivotLocation, float ZoomDelta)
{
	const float NewScale = GetCurrentMapScale() + ZoomDelta * ZoomStep;
	return SetMapScaleAtWorldLocation(WorldPivotLocation, NewScale);
}

float AGSMMap3D::SetMapScaleAtWorldLocation(const FVector& WorldPivotLocation, float NewMapScale)
{
	const float OldScale = FMath::Max(CurrentMapScale, 1.0f);
	const float OldMapContentScale = GetEffectiveMapContentScale();
	const float ClampedNewScale = FMath::Clamp(NewMapScale, GetEffectiveMinMapScale(), GetEffectiveMaxMapScale());
	const FVector ResolvedWorldPivotLocation = ResolveMapZoomPivotWorldLocation(WorldPivotLocation);

	if (FMath::IsNearlyEqual(OldScale, ClampedNewScale))
	{
		ConstrainSpawnedTilesToScaleViewport();
		RefreshMapTerrainMesh();
		RefreshMapBoundsVisibility();
		RefreshNavigationPathVisual();
		return OldScale;
	}

	const FVector LocalPivot = GetActorTransform().InverseTransformPosition(ResolvedWorldPivotLocation);
	CurrentMapScale = ClampedNewScale;
	ApplyMapScaleToSpawnedTiles(OldMapContentScale, LocalPivot);
	ConstrainSpawnedTilesToScaleViewport();
	RefreshMapTerrainMesh();
	RefreshMapBoundsVisibility();
	RefreshNavigationPathVisual();
	NotifySpawnedTilesMapScaleChanged(OldScale);

	return ClampedNewScale;
}

FVector AGSMMap3D::ResolveMapZoomPivotWorldLocation(const FVector& DesiredWorldPivotLocation) const
{
	if (BoardMeshComponent)
	{
		const FVector DesiredLocalPivot = GetActorTransform().InverseTransformPosition(DesiredWorldPivotLocation);
		if (BoardMeshComponent->IsLocalLocationInsideGroove(DesiredLocalPivot))
		{
			return DesiredWorldPivotLocation;
		}

		return GetActorTransform().TransformPosition(BoardMeshComponent->GetLocalGrooveFloorCenter());
	}

	if (!bUseMapBounds || IsWorldLocationInsideMapBounds(DesiredWorldPivotLocation))
	{
		return DesiredWorldPivotLocation;
	}

	const FVector2D LocalBoundsCenter = MapBounds.GetBoundsBox().GetCenter();
	return GetActorTransform().TransformPosition(FVector(LocalBoundsCenter.X, LocalBoundsCenter.Y, 0.0));
}

void AGSMMap3D::BeginDragMapAtWorldLocation(const FVector& WorldDragStartLocation)
{
	bIsDraggingMap = true;
	DragStartWorldLocation = WorldDragStartLocation;
	DragPreviousWorldLocation = WorldDragStartLocation;
	AccumulatedDragDistance = 0.0f;
	bDragMovedBeyondClickTolerance = false;
	bDragClickSuppressionConsumed = false;
	bSuppressNextTileClickAfterDrag = false;

	const FVector LocalDragStartLocation = GetActorTransform().InverseTransformPosition(WorldDragStartLocation);
	DragPlaneLocalZ = LocalDragStartLocation.Z;
}

bool AGSMMap3D::BeginDragMapWithMouse(APlayerController* PlayerController)
{
	FVector WorldDragStartLocation = FVector::ZeroVector;
	if (!GetMouseHitOnMapGroove(PlayerController, WorldDragStartLocation))
	{
		return false;
	}

	BeginDragMapAtWorldLocation(WorldDragStartLocation);
	return true;
}

AGSMTile3D* AGSMMap3D::GetTileUnderMouse(APlayerController* PlayerController, FVector& OutWorldHitLocation) const
{
	OutWorldHitLocation = FVector::ZeroVector;
	FVector RayStart, RayDirection;
	if (!IsValid(PlayerController) || !GetMouseHitOnMapGroove(PlayerController, OutWorldHitLocation)
		|| !PlayerController->DeprojectMousePositionToWorld(RayStart, RayDirection)) { return nullptr; }
	const FVector RayEnd = RayStart + RayDirection * PlayerController->HitResultTraceDistance;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(GSMMapOnlyMousePick), true);
	AGSMTile3D* NearestTile = nullptr;
	double NearestDistance = TNumericLimits<double>::Max();
	for (AGSMTile3D* Tile : SpawnedTiles)
	{
		if (!IsValid(Tile) || Tile->IsActorBeingDestroyed() || Tile->IsHiddenByMapBounds()) continue;
		FHitResult Hit;
		if (Tile->ActorLineTraceSingle(Hit, RayStart, RayEnd, ECC_Visibility, Params)
			&& Hit.Distance < NearestDistance && IsWorldLocationInsideMapBounds(Hit.ImpactPoint))
		{
			NearestDistance = Hit.Distance; NearestTile = Tile; OutWorldHitLocation = Hit.ImpactPoint;
		}
	}
	return NearestTile;
}

bool AGSMMap3D::GetMouseLocationOnDragPlane(APlayerController* PlayerController, FVector& OutWorldLocation) const
{
	OutWorldLocation = FVector::ZeroVector;

	if (!IsValid(PlayerController))
	{
		return false;
	}

	FVector WorldRayOrigin = FVector::ZeroVector;
	FVector WorldRayDirection = FVector::ZeroVector;
	if (!PlayerController->DeprojectMousePositionToWorld(WorldRayOrigin, WorldRayDirection))
	{
		return false;
	}

	const FTransform ActorTransform = GetActorTransform();
	const FVector LocalRayOrigin = ActorTransform.InverseTransformPosition(WorldRayOrigin);
	const FVector LocalRayDirection = ActorTransform.InverseTransformVectorNoScale(WorldRayDirection).GetSafeNormal();
	if (LocalRayDirection.IsNearlyZero() || FMath::IsNearlyZero(LocalRayDirection.Z))
	{
		return false;
	}

	const float PlaneLocalZ = bIsDraggingMap
		? DragPlaneLocalZ
		: (BoardMeshComponent ? BoardMeshComponent->GetLocalGrooveFloorCenter().Z : 0.0f);
	const float HitDistance = (PlaneLocalZ - LocalRayOrigin.Z) / LocalRayDirection.Z;
	if (HitDistance < 0.0f)
	{
		return false;
	}

	const FVector LocalHitLocation = LocalRayOrigin + LocalRayDirection * HitDistance;
	if (!BoardMeshComponent || !BoardMeshComponent->IsLocalLocationInsideGroove(LocalHitLocation))
	{
		return false;
	}

	OutWorldLocation = ActorTransform.TransformPosition(LocalHitLocation);
	return true;
}

bool AGSMMap3D::DragMapToMousePosition(APlayerController* PlayerController, FVector& OutMapContentCenter)
{
	OutMapContentCenter = GetSpawnedTileContentCenterWorldLocation();

	if (!bIsDraggingMap)
	{
		return false;
	}

	FVector WorldDragLocation = FVector::ZeroVector;
	if (!GetMouseLocationOnDragPlane(PlayerController, WorldDragLocation))
	{
		return false;
	}

	OutMapContentCenter = DragMapToWorldLocation(WorldDragLocation);
	return true;
}

FVector AGSMMap3D::DragMapToWorldLocation(const FVector& CurrentWorldDragLocation)
{
	if (!bIsDraggingMap)
	{
		BeginDragMapAtWorldLocation(CurrentWorldDragLocation);
		return GetSpawnedTileContentCenterWorldLocation();
	}

	const FVector LocalPreviousDragLocation = GetActorTransform().InverseTransformPosition(DragPreviousWorldLocation);
	const FVector LocalCurrentDragLocation = GetActorTransform().InverseTransformPosition(CurrentWorldDragLocation);
	const FVector LocalDragDelta = LocalCurrentDragLocation - LocalPreviousDragLocation;
	AccumulatedDragDistance += FVector2D(LocalDragDelta.X, LocalDragDelta.Y).Size();
	bDragMovedBeyondClickTolerance = AccumulatedDragDistance
		> FMath::Max(TileClickDragSuppressionDistance, 0.0f);

	MoveSpawnedTilesByLocalDelta(FVector2D(LocalDragDelta.X, LocalDragDelta.Y));
	RefreshMapBoundsVisibility();
	DragPreviousWorldLocation = CurrentWorldDragLocation;

	return GetSpawnedTileContentCenterWorldLocation();
}

FVector AGSMMap3D::PanMapByWorldDelta(const FVector& WorldDelta)
{
	const FVector LocalDelta = GetActorTransform().InverseTransformVectorNoScale(WorldDelta);

	MoveSpawnedTilesByLocalDelta(FVector2D(LocalDelta.X, LocalDelta.Y));
	RefreshMapBoundsVisibility();

	return GetSpawnedTileContentCenterWorldLocation();
}

void AGSMMap3D::EndDragMap()
{
	bSuppressNextTileClickAfterDrag = bDragMovedBeyondClickTolerance
		&& !bDragClickSuppressionConsumed;
	bIsDraggingMap = false;
	DragStartWorldLocation = FVector::ZeroVector;
	DragPreviousWorldLocation = FVector::ZeroVector;
	DragPlaneLocalZ = 0.0f;
	AccumulatedDragDistance = 0.0f;
	bDragMovedBeyondClickTolerance = false;
	bDragClickSuppressionConsumed = false;
}

bool AGSMMap3D::ConsumeTileClickSuppressionAfterDrag()
{
	if (bIsDraggingMap && bDragMovedBeyondClickTolerance)
	{
		bDragClickSuppressionConsumed = true;
		bSuppressNextTileClickAfterDrag = false;
		return true;
	}

	if (bSuppressNextTileClickAfterDrag)
	{
		bSuppressNextTileClickAfterDrag = false;
		return true;
	}

	return false;
}

bool AGSMMap3D::IsWorldLocationInsideMapBounds(const FVector& WorldLocation) const
{
	const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
	return IsLocalLocationInsideMapBounds(LocalLocation);
}

bool AGSMMap3D::IsLocalLocationInsideMapBounds(const FVector& LocalLocation) const
{
	if (!bUseMapBounds)
	{
		return true;
	}

	if (bUseBoardGrooveAsMapBounds && BoardMeshComponent)
	{
		return BoardMeshComponent->IsLocalLocationInsideGroove(LocalLocation);
	}

	return MapBounds.ContainsPoint(FVector2D(LocalLocation.X, LocalLocation.Y));
}

AGSMTile3D* AGSMMap3D::GetTileById(FName TileId) const
{
	if (const TWeakObjectPtr<AGSMTile3D>* TilePtr = SpawnedTilesById.Find(TileId))
	{
		if (TilePtr->IsValid())
		{
			return TilePtr->Get();
		}
	}

	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (IsValid(TileActor) && TileActor->GetTileId() == TileId)
		{
			return TileActor;
		}
	}

	return nullptr;
}

AGSMTile3D* AGSMMap3D::GetTileByGridCoordinate(FIntPoint GridCoordinate) const
{
	if (const TWeakObjectPtr<AGSMTile3D>* TilePtr = SpawnedTilesByGridCoordinate.Find(GridCoordinate))
	{
		if (TilePtr->IsValid())
		{
			return TilePtr->Get();
		}
	}

	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (IsValid(TileActor) && TileActor->GetGridCoordinate() == GridCoordinate)
		{
			return TileActor;
		}
	}

	return nullptr;
}

AGSMTile3D* AGSMMap3D::GetNearestTileToWorldLocation(
	const FVector& WorldLocation,
	bool bRequireWalkable,
	bool bRespectMapBounds
) const
{
	AGSMTile3D* BestTile = nullptr;
	double BestDistanceSquared = TNumericLimits<double>::Max();

	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (!IsValid(TileActor))
		{
			continue;
		}

		if (bRespectMapBounds && TileActor->IsHiddenByMapBounds())
		{
			continue;
		}

		if (bRequireWalkable && !TileActor->CanWalkThrough())
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(WorldLocation, TileActor->GetActorLocation());
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestTile = TileActor;
		}
	}

	return BestTile;
}

bool AGSMMap3D::FindPathByTileIds(
	FName StartTileId,
	FName GoalTileId,
	EGSMNavigationMode NavigationMode,
	FGSMPathResult& OutPath
) const
{
	if (MapData)
	{
		return MapData->FindPathByTileIds(StartTileId, GoalTileId, NavigationMode, OutPath);
	}
	return FindPathByTiles(GetTileById(StartTileId), GetTileById(GoalTileId), NavigationMode, OutPath);
}

bool AGSMMap3D::FindPathByWorldLocations(
	const FVector& StartWorldLocation,
	const FVector& GoalWorldLocation,
	EGSMNavigationMode NavigationMode,
	FGSMPathResult& OutPath
) const
{
	const bool bRequireWalkableStartTile = NavigationMode == EGSMNavigationMode::WalkingOnly;
	const bool bRequireWalkableGoalTile = NavigationMode != EGSMNavigationMode::Flying;
	AGSMTile3D* StartTile = GetNearestTileToWorldLocation(StartWorldLocation, bRequireWalkableStartTile, true);
	AGSMTile3D* GoalTile = GetNearestTileToWorldLocation(GoalWorldLocation, bRequireWalkableGoalTile, true);

	return FindPathByTiles(StartTile, GoalTile, NavigationMode, OutPath);
}

bool AGSMMap3D::FindPathByTiles(
	AGSMTile3D* StartTile,
	AGSMTile3D* GoalTile,
	EGSMNavigationMode NavigationMode,
	FGSMPathResult& OutPath
) const
{
	if (MapData)
	{
		return MapData->FindPathByTiles(
			IsValid(StartTile) ? StartTile->GetTileData() : nullptr,
			IsValid(GoalTile) ? GoalTile->GetTileData() : nullptr,
			NavigationMode,
			OutPath
		);
	}
	OutPath.Reset();

	if (!IsValid(StartTile) || !IsValid(GoalTile))
	{
		OutPath.FailureReason = FText::FromString(TEXT("Start or goal tile is invalid."));
		return false;
	}

	const FName StartTileId = StartTile->GetTileId();
	const FName GoalTileId = GoalTile->GetTileId();
	if (StartTileId.IsNone() || GoalTileId.IsNone())
	{
		OutPath.FailureReason = FText::FromString(TEXT("Start or goal tile has no TileId."));
		return false;
	}

	const auto TileBelongsToMap = [this](const AGSMTile3D* TileActor)
	{
		return SpawnedTiles.ContainsByPredicate([TileActor](const TObjectPtr<AGSMTile3D>& SpawnedTile)
		{
			return SpawnedTile.Get() == TileActor;
		});
	};

	if (!TileBelongsToMap(StartTile) || !TileBelongsToMap(GoalTile))
	{
		OutPath.FailureReason = FText::FromString(TEXT("Start or goal tile does not belong to this map."));
		return false;
	}

	const auto CanEnterTile = [this](AGSMTile3D* TileActor, EGSMNavigationLinkType LinkType)
	{
		if (!IsValid(TileActor))
		{
			return false;
		}

		if (!bAllowNavigationThroughHiddenTiles && !bIsTilePathNavigationActive && TileActor->IsHiddenByMapBounds())
		{
			return false;
		}

		const FGSMTileNavigationSettings NavigationSettings = TileActor->GetNavigationSettings();
		if (LinkType == EGSMNavigationLinkType::Flying)
		{
			return true;
		}

		return LinkType == EGSMNavigationLinkType::Road
			? true
			: NavigationSettings.bCanWalkThrough;
	};

	const auto CanEnterTileByMode = [&CanEnterTile](AGSMTile3D* TileActor, EGSMNavigationMode Mode)
	{
		if (Mode == EGSMNavigationMode::Flying)
		{
			return CanEnterTile(TileActor, EGSMNavigationLinkType::Flying);
		}

		if (Mode == EGSMNavigationMode::WalkingOnly)
		{
			return CanEnterTile(TileActor, EGSMNavigationLinkType::Walking);
		}

		if (Mode == EGSMNavigationMode::RoadOnly)
		{
			return CanEnterTile(TileActor, EGSMNavigationLinkType::Road);
		}

		return CanEnterTile(TileActor, EGSMNavigationLinkType::Walking)
			|| CanEnterTile(TileActor, EGSMNavigationLinkType::Road);
	};

	if (!CanEnterTileByMode(StartTile, NavigationMode) || !CanEnterTileByMode(GoalTile, NavigationMode))
	{
		OutPath.FailureReason = FText::FromString(TEXT("Start or goal tile cannot be entered by the selected navigation mode."));
		return false;
	}

	if (NavigationMode != EGSMNavigationMode::Flying
		&& !GoalTile->GetNavigationSettings().bCanWalkThrough)
	{
		OutPath.FailureReason = FText::FromString(TEXT("Non-flying navigation requires a walkable goal tile."));
		return false;
	}

	if (StartTileId == GoalTileId)
	{
		OutPath.bSuccess = true;
		OutPath.PathTileIds.Add(StartTileId);
		OutPath.PathTiles.Add(StartTile->GetTileData());
		OutPath.PathWorldLocations.Add(GetNavigationPathPointWorldLocation(StartTile));
		return true;
	}

	struct FNavigationCandidateEdge
	{
		FName TargetTileId = NAME_None;
		float Cost = 1.0f;
		EGSMNavigationLinkType LinkType = EGSMNavigationLinkType::Walking;
	};

	const auto AddWalkingEdge = [this, &CanEnterTile](
		AGSMTile3D* CurrentTile,
		AGSMTile3D* TargetTile,
		TArray<FNavigationCandidateEdge>& OutEdges
	)
	{
		if (!IsValid(CurrentTile) || !IsValid(TargetTile))
		{
			return;
		}

		if (!IsValidWalkingNeighborCoordinate(CurrentTile->GetGridCoordinate(), TargetTile->GetGridCoordinate()))
		{
			return;
		}

		if (!CanEnterTile(TargetTile, EGSMNavigationLinkType::Walking))
		{
			return;
		}

		FNavigationCandidateEdge Edge;
		Edge.TargetTileId = TargetTile->GetTileId();
		Edge.Cost = FMath::Max(TargetTile->GetNavigationSettings().WalkEnterCost, MinimumNavigationCost);
		Edge.LinkType = EGSMNavigationLinkType::Walking;
		OutEdges.Add(Edge);
	};

	const auto AddRoadEdge = [this, &CanEnterTile](
		AGSMTile3D* TargetTile,
		const FGSMRoadConnection& RoadConnection,
		TArray<FNavigationCandidateEdge>& OutEdges
	)
	{
		if (!CanEnterTile(TargetTile, EGSMNavigationLinkType::Road))
		{
			return;
		}

		FNavigationCandidateEdge Edge;
		Edge.TargetTileId = TargetTile->GetTileId();
		Edge.Cost = RoadConnection.CostOverride >= 0.0f
			? RoadConnection.CostOverride
			: TargetTile->GetNavigationSettings().RoadEnterCost;
		Edge.Cost = FMath::Max(Edge.Cost, MinimumNavigationCost);
		Edge.LinkType = EGSMNavigationLinkType::Road;
		OutEdges.Add(Edge);
	};

	const auto AddFlyingEdge = [this, &CanEnterTile](
		AGSMTile3D* CurrentTile,
		AGSMTile3D* TargetTile,
		TArray<FNavigationCandidateEdge>& OutEdges
	)
	{
		if (!IsValid(CurrentTile) || !IsValid(TargetTile))
		{
			return;
		}

		if (!IsValidWalkingNeighborCoordinate(CurrentTile->GetGridCoordinate(), TargetTile->GetGridCoordinate()))
		{
			return;
		}

		if (!CanEnterTile(TargetTile, EGSMNavigationLinkType::Flying))
		{
			return;
		}

		FNavigationCandidateEdge Edge;
		Edge.TargetTileId = TargetTile->GetTileId();
		Edge.Cost = MinimumNavigationCost;
		Edge.LinkType = EGSMNavigationLinkType::Flying;
		OutEdges.Add(Edge);
	};

	const auto GatherEdges = [this, NavigationMode, &AddWalkingEdge, &AddRoadEdge, &AddFlyingEdge](
		AGSMTile3D* CurrentTile,
		TArray<FNavigationCandidateEdge>& OutEdges
	)
	{
		if (!IsValid(CurrentTile))
		{
			return;
		}

		const FGSMTileNavigationSettings CurrentNavigationSettings = CurrentTile->GetNavigationSettings();

		if (NavigationMode == EGSMNavigationMode::Flying)
		{
			for (AGSMTile3D* OtherTile : SpawnedTiles)
			{
				AddFlyingEdge(CurrentTile, OtherTile, OutEdges);
			}
			return;
		}

		if (NavigationMode != EGSMNavigationMode::RoadOnly && CurrentNavigationSettings.bCanWalkThrough)
		{
			if (CurrentNavigationSettings.WalkingNeighborTileIds.Num() > 0)
			{
				for (FName NeighborTileId : CurrentNavigationSettings.WalkingNeighborTileIds)
				{
					AddWalkingEdge(CurrentTile, GetTileById(NeighborTileId), OutEdges);
				}
			}
			else if (bAutoGenerateWalkingLinksFromGrid)
			{
				for (const FIntPoint& NeighborOffset : HexWalkingNeighborOffsets)
				{
					AddWalkingEdge(CurrentTile, GetTileByGridCoordinate(CurrentTile->GetGridCoordinate() + NeighborOffset), OutEdges);
				}
			}
		}

		if (NavigationMode != EGSMNavigationMode::WalkingOnly)
		{
			for (const FGSMRoadConnection& RoadConnection : CurrentNavigationSettings.RoadConnections)
			{
				AddRoadEdge(GetTileById(RoadConnection.TargetTileId), RoadConnection, OutEdges);
			}

			for (AGSMTile3D* OtherTile : SpawnedTiles)
			{
				if (!IsValid(OtherTile) || OtherTile == CurrentTile)
				{
					continue;
				}

				for (const FGSMRoadConnection& RoadConnection : OtherTile->GetNavigationSettings().RoadConnections)
				{
					if (RoadConnection.bBidirectional && RoadConnection.TargetTileId == CurrentTile->GetTileId())
					{
						AddRoadEdge(OtherTile, RoadConnection, OutEdges);
					}
				}
			}
		}
	};

	TMap<FName, float> BestCosts;
	TMap<FName, FName> CameFrom;
	TMap<FName, EGSMNavigationLinkType> CameViaLinkType;
	TSet<FName> OpenSet;
	TSet<FName> ClosedSet;

	BestCosts.Add(StartTileId, 0.0f);
	OpenSet.Add(StartTileId);

	while (OpenSet.Num() > 0)
	{
		FName CurrentTileId = NAME_None;
		float CurrentBestCost = TNumericLimits<float>::Max();

		for (const FName& CandidateTileId : OpenSet)
		{
			const float* CandidateCost = BestCosts.Find(CandidateTileId);
			if (CandidateCost && *CandidateCost < CurrentBestCost)
			{
				CurrentBestCost = *CandidateCost;
				CurrentTileId = CandidateTileId;
			}
		}

		if (CurrentTileId.IsNone())
		{
			break;
		}

		if (CurrentTileId == GoalTileId)
		{
			break;
		}

		OpenSet.Remove(CurrentTileId);
		ClosedSet.Add(CurrentTileId);

		AGSMTile3D* CurrentTile = GetTileById(CurrentTileId);
		TArray<FNavigationCandidateEdge> CandidateEdges;
		GatherEdges(CurrentTile, CandidateEdges);

		for (const FNavigationCandidateEdge& Edge : CandidateEdges)
		{
			if (Edge.TargetTileId.IsNone() || ClosedSet.Contains(Edge.TargetTileId))
			{
				continue;
			}

			const float NewCost = CurrentBestCost + Edge.Cost;
			const float ExistingCost = BestCosts.Contains(Edge.TargetTileId)
				? BestCosts[Edge.TargetTileId]
				: TNumericLimits<float>::Max();

			if (NewCost < ExistingCost)
			{
				BestCosts.Add(Edge.TargetTileId, NewCost);
				CameFrom.Add(Edge.TargetTileId, CurrentTileId);
				CameViaLinkType.Add(Edge.TargetTileId, Edge.LinkType);
				OpenSet.Add(Edge.TargetTileId);
			}
		}
	}

	if (!BestCosts.Contains(GoalTileId))
	{
		OutPath.FailureReason = FText::FromString(TEXT("No path was found."));
		return false;
	}

	TArray<FName> ReversedTileIds;
	TArray<EGSMNavigationLinkType> ReversedLinkTypes;

	FName WalkBackTileId = GoalTileId;
	ReversedTileIds.Add(WalkBackTileId);

	while (WalkBackTileId != StartTileId)
	{
		if (const EGSMNavigationLinkType* LinkType = CameViaLinkType.Find(WalkBackTileId))
		{
			ReversedLinkTypes.Add(*LinkType);
		}

		const FName* ParentTileId = CameFrom.Find(WalkBackTileId);
		if (!ParentTileId)
		{
			OutPath.FailureReason = FText::FromString(TEXT("Path reconstruction failed."));
			return false;
		}

		WalkBackTileId = *ParentTileId;
		ReversedTileIds.Add(WalkBackTileId);
	}

	for (int32 Index = ReversedTileIds.Num() - 1; Index >= 0; --Index)
	{
		const FName PathTileId = ReversedTileIds[Index];
		AGSMTile3D* PathTile = GetTileById(PathTileId);

		OutPath.PathTileIds.Add(PathTileId);
		OutPath.PathTiles.Add(IsValid(PathTile) ? PathTile->GetTileData() : nullptr);
		OutPath.PathWorldLocations.Add(IsValid(PathTile) ? GetNavigationPathPointWorldLocation(PathTile) : FVector::ZeroVector);
	}

	for (int32 Index = ReversedLinkTypes.Num() - 1; Index >= 0; --Index)
	{
		OutPath.PathLinkTypes.Add(ReversedLinkTypes[Index]);
	}

	for (int32 PathIndex = 0; PathIndex < OutPath.PathTiles.Num(); ++PathIndex)
	{
		UGSMTileData* PathTile = OutPath.PathTiles[PathIndex];
		if (!IsValid(PathTile))
		{
			continue;
		}

		const bool bRequiresWalking =
			NavigationMode == EGSMNavigationMode::WalkingOnly ||
			(PathIndex > 0 && OutPath.PathLinkTypes.IsValidIndex(PathIndex - 1) && OutPath.PathLinkTypes[PathIndex - 1] == EGSMNavigationLinkType::Walking) ||
			(PathIndex < OutPath.PathLinkTypes.Num() && OutPath.PathLinkTypes[PathIndex] == EGSMNavigationLinkType::Walking);

		if (bRequiresWalking && !PathTile->GetNavigationSettings().bCanWalkThrough)
		{
			OutPath.Reset();
			OutPath.FailureReason = FText::FromString(FString::Printf(
				TEXT("Path validation rejected non-walkable tile %s."),
				*PathTile->GetTileId().ToString()
			));
			return false;
		}
	}

	OutPath.bSuccess = true;
	OutPath.TotalCost = BestCosts[GoalTileId];

	return true;
}

AGSMPathArrow3D* AGSMMap3D::GetOrCreatePathArrowActor()
{
	if (IsValid(PathArrowActor))
	{
		if (PathArrowActorClass && !PathArrowActor->IsA(PathArrowActorClass))
		{
			PathArrowActor->Destroy();
			PathArrowActor = nullptr;
		}
		else
		{
			return PathArrowActor.Get();
		}
	}

	if (IsValid(PathArrowActor))
	{
		return PathArrowActor.Get();
	}

	if (!PathArrowActorClass)
	{
		UE_LOG(
			LogGSM,
			Warning,
			TEXT("GetOrCreatePathArrowActor failed on %s: PathArrowActorClass is not configured."),
			*GetName()
		);
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.OverrideLevel = GetLevel();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	PathArrowActor = World->SpawnActor<AGSMPathArrow3D>(
		PathArrowActorClass,
		GetActorTransform(),
		SpawnParameters
	);

	if (PathArrowActor)
	{
		PathArrowActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	}

	return PathArrowActor.Get();
}

AGSMPathArrow3D* AGSMMap3D::ShowNavigationPath(const FGSMPathResult& PathResult)
{
	if (!PathResult.bSuccess)
	{
		ClearNavigationPath();
		return nullptr;
	}

	AGSMPathArrow3D* ArrowActor = GetOrCreatePathArrowActor();
	if (!ArrowActor)
	{
		return nullptr;
	}

	FGSMPathResult DisplayPath = PathResult;
	DisplayPath.PathWorldLocations.Reset(PathResult.PathTiles.Num());
	for (UGSMTileData* TileData : PathResult.PathTiles)
	{
		AGSMTile3D* Tile3D = TileData ? TileData->GetTile3D() : nullptr;
		DisplayPath.PathWorldLocations.Add(Tile3D
			? GetNavigationPathPointWorldLocation(Tile3D)
			: GetActorTransform().TransformPosition(TileData ? TileData->GetLocalTransform().GetLocation() : FVector::ZeroVector));
	}

	if (!ArrowActor->ShowPathFromResult(DisplayPath))
	{
		return nullptr;
	}

	RefreshMapBoundsVisibility();
	return ArrowActor;
}

bool AGSMMap3D::BeginTilePathNavigation(
	AGSMTile3D* StartTile,
	TSubclassOf<AGSMPathArrow3D> InPathArrowActorClass,
	EGSMNavigationMode NavigationMode,
	UGSMNavigationMoveData* NavigationMoveData
)
{
	if (!IsValid(StartTile))
	{
		UE_LOG(LogGSM, Warning, TEXT("BeginTilePathNavigation failed on %s: StartTile is invalid."), *GetName());
		return false;
	}

	const bool bStartTileBelongsToMap = SpawnedTiles.ContainsByPredicate([StartTile](const TObjectPtr<AGSMTile3D>& SpawnedTile)
	{
		return SpawnedTile.Get() == StartTile;
	});
	if (!bStartTileBelongsToMap)
	{
		UE_LOG(
			LogGSM,
			Warning,
			TEXT("BeginTilePathNavigation failed on %s: StartTile %s does not belong to this map."),
			*GetName(),
			*GetNameSafe(StartTile)
		);
		return false;
	}

	if (InPathArrowActorClass)
	{
		if (PathArrowActorClass != InPathArrowActorClass)
		{
			if (IsValid(PathArrowActor) && !PathArrowActor->IsA(InPathArrowActorClass))
			{
				PathArrowActor->Destroy();
				PathArrowActor = nullptr;
			}

			PathArrowActorClass = InPathArrowActorClass;
		}
	}

	if (!PathArrowActorClass)
	{
		UE_LOG(
			LogGSM,
			Warning,
			TEXT("BeginTilePathNavigation failed on %s: PathArrowActorClass is not configured."),
			*GetName()
		);
		return false;
	}

	ConfigurePlayerControllerClickEvents();
	ClearNavigationPath();
	ResetTilePathNavigationState();

	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (IsValid(TileActor))
		{
			TileActor->SetNavigationInteractionOverride(true);
		}
	}

	bIsTilePathNavigationActive = true;
	TilePathNavigationStartTile = StartTile;
	TilePathNavigationMode = NavigationMode;
	TilePathNavigationMoveData = NavigationMoveData
		? NavigationMoveData
		: NewObject<UGSMNavigationMoveData>(this);
	if (TilePathNavigationMoveData)
	{
		TilePathNavigationMoveData->ClearNavigationPathData();
	}
	return true;
}

bool AGSMMap3D::BeginTilePathNavigationWithCompletion(
	AGSMTile3D* StartTile,
	EGSMNavigationMode NavigationMode,
	const FGSMNavigationFinished& NavigationFinishedEvent)
{
	if (!BeginTilePathNavigation(StartTile, nullptr, NavigationMode, nullptr))
	{
		return false;
	}

	TilePathNavigationFinishedDelegate = NavigationFinishedEvent;
	return true;
}

bool AGSMMap3D::CompleteTilePathNavigation(AGSMTile3D* GoalTile)
{
	if (!bIsTilePathNavigationActive || !IsValid(GoalTile))
	{
		return false;
	}

	AGSMTile3D* StartTile = TilePathNavigationStartTile.Get();
	const bool bDifferentTiles = IsValid(StartTile) && StartTile != GoalTile;
	const bool bHoverPathValid = bDifferentTiles && HandleTileHoverNavigation(GoalTile);
	const bool bGoalWalkabilityValid = TilePathNavigationMode == EGSMNavigationMode::Flying
		|| GoalTile->CanWalkThrough();
	const bool bNavigationValid = bHoverPathValid
		&& bGoalWalkabilityValid
		&& CurrentTilePathNavigationResult.bSuccess
		&& CurrentTilePathNavigationResult.PathTiles.Num() >= 2;

	TArray<UGSMTileData*> NavigationPath;
	if (bNavigationValid)
	{
		for (UGSMTileData* PathTile : CurrentTilePathNavigationResult.PathTiles)
		{
			if (IsValid(PathTile))
			{
				NavigationPath.Add(PathTile);
			}
		}
	}

	const FGSMNavigationFinished FinishedDelegate = TilePathNavigationFinishedDelegate;
	EndTilePathNavigationAndGetMoveData(true);
	FinishedDelegate.ExecuteIfBound(NavigationPath, bNavigationValid);
	return true;
}

bool AGSMMap3D::BeginFixedTilePathNavigation(
	AGSMTile3D* StartTile,
	AGSMTile3D* GoalTile,
	EGSMNavigationMode NavigationMode,
	TArray<UGSMTileData*>& OutPathTiles)
{
	OutPathTiles.Reset();
	ClearNavigationPath();
	ResetTilePathNavigationState();

	if (!IsValid(MapData) || !IsValid(StartTile) || !IsValid(GoalTile))
	{
		UE_LOG(
			LogGSM,
			Warning,
			TEXT("BeginFixedTilePathNavigation failed on %s: map data, start tile, or goal tile is invalid."),
			*GetName());
		return false;
	}

	UGSMTileData* StartTileData = StartTile->GetTileData();
	UGSMTileData* GoalTileData = GoalTile->GetTileData();
	if (!IsValid(StartTileData) || !IsValid(GoalTileData)
		|| StartTileData->GetMapData() != MapData
		|| GoalTileData->GetMapData() != MapData)
	{
		UE_LOG(
			LogGSM,
			Warning,
			TEXT("BeginFixedTilePathNavigation failed on %s: start tile or goal tile does not belong to this map data."),
			*GetName());
		return false;
	}

	FGSMPathResult PathResult;
	if (!MapData->FindPathByTiles(StartTileData, GoalTileData, NavigationMode, PathResult)
		|| PathResult.PathTiles.Num() < 2)
	{
		UE_LOG(
			LogGSM,
			Warning,
			TEXT("BeginFixedTilePathNavigation failed on %s: no usable path from %s to %s. Reason: %s"),
			*GetName(),
			*StartTileData->GetTileId().ToString(),
			*GoalTileData->GetTileId().ToString(),
			*PathResult.FailureReason.ToString());
		return false;
	}

	if (!PathArrowActorClass)
	{
		UE_LOG(
			LogGSM,
			Warning,
			TEXT("BeginFixedTilePathNavigation failed on %s: PathArrowActorClass is not configured."),
			*GetName());
		return false;
	}

	bIsTilePathNavigationActive = true;
	TilePathNavigationStartTile = StartTile;
	TilePathNavigationTargetTile = GoalTile;
	TilePathNavigationMode = NavigationMode;
	TilePathNavigationMoveData = NewObject<UGSMNavigationMoveData>(this);
	CacheTilePathNavigationResult(PathResult);

	if (!ShowNavigationPath(PathResult))
	{
		ResetTilePathNavigationState();
		ClearNavigationPath();
		return false;
	}

	for (UGSMTileData* PathTile : PathResult.PathTiles)
	{
		if (IsValid(PathTile))
		{
			OutPathTiles.Add(PathTile);
		}
	}

	return OutPathTiles.Num() >= 2;
}

bool AGSMMap3D::EndTilePathNavigation(
	TArray<UGSMTileData*>& OutPathTiles,
	bool bClearPathArrow
)
{
	OutPathTiles.Reset();
	UGSMNavigationMoveData* NavigationMoveData = EndTilePathNavigationAndGetMoveData(bClearPathArrow);
	if (!NavigationMoveData)
	{
		return false;
	}

	OutPathTiles = NavigationMoveData->GetPathTiles();
	return NavigationMoveData->HasNavigationPath();
}

UGSMNavigationMoveData* AGSMMap3D::EndTilePathNavigationAndGetMoveData(
	bool bClearPathArrow
)
{
	const bool bHadActiveNavigation = bIsTilePathNavigationActive;
	if (!bHadActiveNavigation && !TilePathNavigationMoveData)
	{
		if (bClearPathArrow)
		{
			ClearNavigationPath();
		}
		return nullptr;
	}

	UGSMNavigationMoveData* FinishedMoveData = TilePathNavigationMoveData.Get();
	if (!FinishedMoveData)
	{
		FinishedMoveData = NewObject<UGSMNavigationMoveData>(this);
	}

	if (FinishedMoveData)
	{
		FinishedMoveData->SetNavigationPathFromResult(CurrentTilePathNavigationResult);
	}

	const bool bHasValidMovementPath = FinishedMoveData && FinishedMoveData->HasNavigationPath();
	if (FinishedMoveData && !bHasValidMovementPath)
	{
		FinishedMoveData->ClearNavigationPathData();
	}

	ResetTilePathNavigationState(false);

	if (bClearPathArrow)
	{
		ClearNavigationPath();
	}

	return bHasValidMovementPath ? FinishedMoveData : nullptr;
}

bool AGSMMap3D::HandleTileHoverNavigation(AGSMTile3D* HoveredTile)
{
	if (!bIsTilePathNavigationActive)
	{
		return false;
	}

	AGSMTile3D* StartTile = TilePathNavigationStartTile.Get();
	if (!IsValid(StartTile) || !IsValid(HoveredTile))
	{
		ResetTilePathNavigationState();
		ClearNavigationPath();
		return false;
	}

	if (HoveredTile->GetOwningGridMap() != this)
	{
		return false;
	}

	if (HoveredTile == StartTile)
	{
		FGSMPathResult InvalidPathResult;
		InvalidPathResult.FailureReason = FText::FromString(TEXT("Navigation start and goal tiles must be different."));
		TilePathNavigationTargetTile = HoveredTile;
		CacheTilePathNavigationResult(InvalidPathResult);
		ClearNavigationPath();
		return false;
	}

	if (TilePathNavigationTargetTile == HoveredTile)
	{
		return CurrentTilePathNavigationResult.bSuccess;
	}

	FGSMPathResult PathResult;
	if (!FindPathByTiles(StartTile, HoveredTile, TilePathNavigationMode, PathResult))
	{
		TilePathNavigationTargetTile = HoveredTile;
		CacheTilePathNavigationResult(PathResult);
		ClearNavigationPath();
		return false;
	}

	TilePathNavigationTargetTile = HoveredTile;
	CacheTilePathNavigationResult(PathResult);

	if (PathResult.PathTiles.Num() >= 2)
	{
		if (!ShowNavigationPath(PathResult))
		{
			PathResult.bSuccess = false;
			PathResult.FailureReason = FText::FromString(TEXT("Failed to generate the 3D navigation arrow."));
			CacheTilePathNavigationResult(PathResult);
			ClearNavigationPath();
			return false;
		}
	}
	else
	{
		PathResult.bSuccess = false;
		PathResult.FailureReason = FText::FromString(TEXT("Navigation path must contain different start and goal tiles."));
		CacheTilePathNavigationResult(PathResult);
		ClearNavigationPath();
		return false;
	}

	return true;
}

void AGSMMap3D::ClearNavigationPath()
{
	if (PathArrowActor)
	{
		PathArrowActor->ClearPath();
	}
}

bool AGSMMap3D::RefreshNavigationPathVisual()
{
	return PathArrowActor
		? PathArrowActor->RefreshPathFromCachedTiles()
		: false;
}

void AGSMMap3D::CacheTilePathNavigationResult(const FGSMPathResult& PathResult)
{
	CurrentTilePathNavigationResult = PathResult;
	if (TilePathNavigationMoveData)
	{
		TilePathNavigationMoveData->SetNavigationPathFromResult(PathResult);
	}
}

void AGSMMap3D::ResetTilePathNavigationState(bool bClearNavigationMoveDataPath)
{
	if (bClearNavigationMoveDataPath && TilePathNavigationMoveData)
	{
		TilePathNavigationMoveData->ClearNavigationPathData();
	}

	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (IsValid(TileActor))
		{
			TileActor->SetNavigationInteractionOverride(false);
		}
	}

	bIsTilePathNavigationActive = false;
	TilePathNavigationStartTile = nullptr;
	TilePathNavigationTargetTile = nullptr;
	TilePathNavigationMode = EGSMNavigationMode::WalkingOnly;
	CurrentTilePathNavigationResult.Reset();
	TilePathNavigationMoveData = nullptr;
	TilePathNavigationFinishedDelegate.Unbind();
}

bool AGSMMap3D::HandleTileClickRequest(AGSMTile3D* TileActor, const FVector& WorldHitLocation)
{
	if (!CanAcceptTileClick(TileActor, WorldHitLocation))
	{
		return false;
	}

	if (!TileActor->CanAcceptMapTileClick(WorldHitLocation))
	{
		return false;
	}

	if (bIsTilePathNavigationActive)
	{
		return CompleteTilePathNavigation(TileActor);
	}

	SwitchSelectedTile(TileActor);
	TileActor->NotifyMapTileClickAccepted(WorldHitLocation);
	OnTileClickAccepted(TileActor, WorldHitLocation);
	OnGridMapTileClicked.Broadcast(TileActor, WorldHitLocation);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UGSMMapSubsystem* MapSubsystem = GameInstance->GetSubsystem<UGSMMapSubsystem>())
		{
			MapSubsystem->BroadcastTileClicked(TileActor, WorldHitLocation);
		}
	}

	return true;
}

bool AGSMMap3D::CanAcceptTileClick_Implementation(AGSMTile3D* TileActor, const FVector& WorldHitLocation) const
{
	const bool bTileBelongsToMap = SpawnedTiles.ContainsByPredicate([TileActor](const TObjectPtr<AGSMTile3D>& SpawnedTile)
	{
		return SpawnedTile.Get() == TileActor;
	});

	return IsValid(TileActor)
		&& bTileBelongsToMap
		&& (!bUseMapBounds || IsWorldLocationInsideMapBounds(WorldHitLocation));
}

void AGSMMap3D::OnTileClickAccepted_Implementation(AGSMTile3D* TileActor, const FVector& WorldHitLocation)
{
}

bool AGSMMap3D::SwitchSelectedTile(AGSMTile3D* NewSelectedTile)
{
	if (NewSelectedTile)
	{
		if (bClearingOrEndingMap || IsActorBeingDestroyed() || !IsValid(NewSelectedTile) || NewSelectedTile->IsActorBeingDestroyed())
		{
			return false;
		}
		const bool bTileBelongsToMap = SpawnedTiles.ContainsByPredicate([NewSelectedTile](const TObjectPtr<AGSMTile3D>& SpawnedTile)
		{
			return SpawnedTile.Get() == NewSelectedTile;
		});

		if (!bTileBelongsToMap)
		{
			return false;
		}
	}

	if (SelectedTile.Get() == NewSelectedTile)
	{
		return true;
	}

	AGSMTile3D* PreviousTile = SelectedTile.Get();
	const uint64 ChangeRevision = ++SelectionChangeRevision;
	SelectedTile = NewSelectedTile;
	if (PreviousTile)
	{
		PreviousTile->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleSelectedTileEndPlay);
		PreviousTile->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleSelectedTileDestroyed);
	}
	if (NewSelectedTile)
	{
		NewSelectedTile->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleSelectedTileEndPlay);
		NewSelectedTile->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleSelectedTileDestroyed);
	}

	// Tile Blueprint callbacks may clear/rebuild the map or select another tile.
	// Publish ownership first and never overwrite a newer selection after a callback.
	if (IsValid(PreviousTile) && !PreviousTile->IsActorBeingDestroyed())
	{
		PreviousTile->SetSelectedByMap(false);
	}
	if (ChangeRevision != SelectionChangeRevision) return true;
	if (IsValid(NewSelectedTile) && !NewSelectedTile->IsActorBeingDestroyed())
	{
		NewSelectedTile->SetSelectedByMap(true);
	}
	if (ChangeRevision != SelectionChangeRevision) return true;
	OnSelectedTileChanged.Broadcast();

	return true;
}

void AGSMMap3D::HandleSelectedTileEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	HandleSelectedTileDestroyed(Actor);
}

void AGSMMap3D::HandleSelectedTileDestroyed(AActor* Actor)
{
	if (SelectedTile.Get() == Actor) ClearSelectedTile();
}

void AGSMMap3D::ClearSelectedTile()
{
	SwitchSelectedTile(nullptr);
}

FName AGSMMap3D::MakeRuntimeTileId(const FGSMTileEntry& TileEntry, int32 TileIndex) const
{
	(void)TileIndex;
	if (!TileEntry.TileId.IsNone())
	{
		return TileEntry.TileId;
	}

	return FName(*GSMLayout::MakeTileCoordinateLabel(
		TileEntry.GridCoordinate,
		MapConfig ? MapConfig->HorizontalLabelType : EGSMCoordinateLabelType::Letters,
		MapConfig ? MapConfig->VerticalLabelType : EGSMCoordinateLabelType::Numbers));
}

float AGSMMap3D::GetEffectiveMaxMapScale() const
{
	return FMath::Max(MaxMapScale, 1.0f);
}

float AGSMMap3D::GetEffectiveMinMapScale() const
{
	const float DesiredMinScale = bUseAutoFitMinMapScale
		? GetAutoFitMinMapScale()
		: MinMapScale;
	return FMath::Clamp(DesiredMinScale, 1.0f, GetEffectiveMaxMapScale());
}

float AGSMMap3D::GetAutoFitMinMapScale() const
{
	return 1.0f;
}

int32 AGSMMap3D::GetCurrentMapScaleLevel() const
{
	return ResolveMapScaleLevel(CurrentMapScale);
}

int32 AGSMMap3D::ResolveMapScaleLevel(float MapScale) const
{
	TArray<float> SortedUpperBounds;
	SortedUpperBounds.Reserve(MapScaleLevelUpperBounds.Num());
	for (const float UpperBound : MapScaleLevelUpperBounds)
	{
		if (FMath::IsFinite(UpperBound))
		{
			SortedUpperBounds.Add(UpperBound);
		}
	}
	SortedUpperBounds.Sort();

	for (int32 LevelIndex = 0; LevelIndex < SortedUpperBounds.Num(); ++LevelIndex)
	{
		if (MapScale <= SortedUpperBounds[LevelIndex])
		{
			return LevelIndex;
		}
	}

	return SortedUpperBounds.Num();
}

float AGSMMap3D::CalculateMapFitScaleMultiplier() const
{
	FVector2D ContentSize = RuntimeUnscaledMapContentSize;
	if (ContentSize.X <= KINDA_SMALL_NUMBER || ContentSize.Y <= KINDA_SMALL_NUMBER)
	{
		ContentSize = CalculateUnscaledMapContentSizeFromConfig();
	}

	const FVector2D ViewportSize = GetMapScaleViewportSize();
	if (ContentSize.X <= KINDA_SMALL_NUMBER
		|| ContentSize.Y <= KINDA_SMALL_NUMBER
		|| ViewportSize.X <= KINDA_SMALL_NUMBER
		|| ViewportSize.Y <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	const float FitScaleX = ViewportSize.X / ContentSize.X;
	const float FitScaleY = ViewportSize.Y / ContentSize.Y;
	return FMath::Max(FMath::Min(FitScaleX, FitScaleY), KINDA_SMALL_NUMBER);
}

float AGSMMap3D::GetEffectiveMapContentScale() const
{
	return FMath::Max(RuntimeMapFitScaleMultiplier, KINDA_SMALL_NUMBER)
		* FMath::Max(CurrentMapScale, 1.0f);
}

FVector2D AGSMMap3D::CalculateUnscaledMapContentSizeFromConfig() const
{
	int32 GridColumnCount = 1;
	int32 GridRowCount = 1;

	if (MapConfig)
	{
		GSMLayout::GetGridSizeFromTileEntries(
			MapConfig->TileEntries,
			MapConfig->EditorGridColumns,
			MapConfig->EditorGridRows,
			GridColumnCount,
			GridRowCount
		);
	}

	const float SafeTileSize = MapConfig ? GSMLayout::FixedSquareTileSize : RuntimeBaseSquareTileSize;
	return GSMLayout::GetGridContentSize(GridColumnCount, GridRowCount, SafeTileSize);
}

bool AGSMMap3D::GetMapScaleViewportBounds(FBox2D& OutBounds) const
{
	if (BoardMeshComponent)
	{
		OutBounds = BoardMeshComponent->GetLocalGrooveBounds().GetBoundsBox();
		return OutBounds.bIsValid;
	}

	if (bUseMapBounds)
	{
		OutBounds = MapBounds.GetBoundsBox();
		return OutBounds.bIsValid;
	}

	OutBounds = FBox2D(ForceInit);
	return false;
}

FVector2D AGSMMap3D::GetMapScaleViewportSize() const
{
	FBox2D ViewportBounds(ForceInit);
	if (GetMapScaleViewportBounds(ViewportBounds))
	{
		return ViewportBounds.GetSize();
	}

	return FVector2D::ZeroVector;
}

FVector2D AGSMMap3D::GetMapScaleViewportCenter() const
{
	FBox2D ViewportBounds(ForceInit);
	if (GetMapScaleViewportBounds(ViewportBounds))
	{
		return ViewportBounds.GetCenter();
	}

	return FVector2D::ZeroVector;
}

bool AGSMMap3D::GetSpawnedTileContentBoundsLocal(FBox2D& OutBounds) const
{
	OutBounds = FBox2D(ForceInit);

	if (SpawnedTiles.IsEmpty())
	{
		return false;
	}

	const float HalfTileSize = GetScaledRuntimeSquareTileSize() * 0.5f;
	bool bHasContentBounds = false;

	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (!IsValid(TileActor))
		{
			continue;
		}

		const FTransform TileRelativeTransform = TileActor->GetActorTransform().GetRelativeTransform(GetActorTransform());
		const FVector TileRelativeLocation = TileRelativeTransform.GetLocation();
		OutBounds += FVector2D(TileRelativeLocation.X - HalfTileSize, TileRelativeLocation.Y - HalfTileSize);
		OutBounds += FVector2D(TileRelativeLocation.X + HalfTileSize, TileRelativeLocation.Y + HalfTileSize);
		bHasContentBounds = true;
	}

	return bHasContentBounds;
}

bool AGSMMap3D::GetMapTerrainTargetBoundsLocal(FBox2D& OutBounds) const
{
	if (GetSpawnedTileContentBoundsLocal(OutBounds))
	{
		return true;
	}

	FVector2D ContentSize = RuntimeUnscaledMapContentSize;
	if (ContentSize.X <= KINDA_SMALL_NUMBER || ContentSize.Y <= KINDA_SMALL_NUMBER)
	{
		ContentSize = CalculateUnscaledMapContentSizeFromConfig();
	}

	ContentSize.X *= GetEffectiveMapContentScale();
	ContentSize.Y *= GetEffectiveMapContentScale();
	if (ContentSize.X <= KINDA_SMALL_NUMBER || ContentSize.Y <= KINDA_SMALL_NUMBER)
	{
		OutBounds = FBox2D(ForceInit);
		return false;
	}

	const FVector GrooveCenter = BoardMeshComponent
		? BoardMeshComponent->GetLocalGrooveFloorCenter()
		: FVector::ZeroVector;
	const FVector2D HalfSize = ContentSize * 0.5f;
	OutBounds = FBox2D(
		FVector2D(GrooveCenter.X - HalfSize.X, GrooveCenter.Y - HalfSize.Y),
		FVector2D(GrooveCenter.X + HalfSize.X, GrooveCenter.Y + HalfSize.Y)
	);
	return OutBounds.bIsValid;
}

FVector AGSMMap3D::GetSpawnedTileContentCenterWorldLocation() const
{
	FBox2D ContentBounds(ForceInit);
	if (!GetSpawnedTileContentBoundsLocal(ContentBounds))
	{
		return GetActorLocation();
	}

	const FVector2D ContentCenter = ContentBounds.GetCenter();
	const float LocalZ = BoardMeshComponent
		? BoardMeshComponent->GetLocalGrooveFloorCenter().Z
		: 0.0f;
	return GetActorTransform().TransformPosition(FVector(ContentCenter.X, ContentCenter.Y, LocalZ));
}

void AGSMMap3D::GetRuntimeGridSize(int32& OutColumnCount, int32& OutRowCount) const
{
	OutColumnCount = 1;
	OutRowCount = 1;

	if (MapConfig)
	{
		GSMLayout::GetGridSizeFromTileEntries(
			MapConfig->TileEntries,
			MapConfig->EditorGridColumns,
			MapConfig->EditorGridRows,
			OutColumnCount,
			OutRowCount
		);

		return;
	}

	const float SafeBaseTileSize = FMath::Max(RuntimeBaseSquareTileSize, 1.0f);
	OutColumnCount = FMath::Max(1, FMath::RoundToInt(RuntimeUnscaledMapContentSize.X / SafeBaseTileSize));
	OutRowCount = FMath::Max(1, FMath::RoundToInt(RuntimeUnscaledMapContentSize.Y / SafeBaseTileSize));
}

void AGSMMap3D::RefreshCoordinateLabels()
{
	const auto HideLabels = [](TArray<TObjectPtr<UTextRenderComponent>>& Components)
	{
		for (UTextRenderComponent* LabelComponent : Components)
		{
			if (IsValid(LabelComponent))
			{
				LabelComponent->SetVisibility(false);
				LabelComponent->SetHiddenInGame(true);
			}
		}
	};

	FBox2D GrooveBounds(ForceInit);
	FBox2D BoardBounds(ForceInit);
	if (BoardMeshComponent)
	{
		GrooveBounds = BoardMeshComponent->GetLocalGrooveBounds().GetBoundsBox();
		BoardBounds = BoardMeshComponent->GetLocalBoardBounds().GetBoundsBox();
	}
	else if (GetMapScaleViewportBounds(GrooveBounds))
	{
		BoardBounds = GrooveBounds;
		BoardBounds.Min.X -= CoordinateLabelOffset * 2.0f;
		BoardBounds.Max.X += CoordinateLabelOffset * 2.0f;
		BoardBounds.Min.Y -= CoordinateLabelOffset * 2.0f;
		BoardBounds.Max.Y += CoordinateLabelOffset * 2.0f;
	}

	if (!bShowCoordinateLabels || !GrooveBounds.bIsValid || !BoardBounds.bIsValid)
	{
		HideLabels(ColumnCoordinateLabels);
		HideLabels(BottomColumnCoordinateLabels);
		HideLabels(RowCoordinateLabels);
		HideLabels(RightRowCoordinateLabels);
		return;
	}

	FBox2D ContentBounds(ForceInit);
	if (!GetSpawnedTileContentBoundsLocal(ContentBounds))
	{
		HideLabels(ColumnCoordinateLabels);
		HideLabels(BottomColumnCoordinateLabels);
		HideLabels(RowCoordinateLabels);
		HideLabels(RightRowCoordinateLabels);
		return;
	}

	int32 ColumnCount = 1;
	int32 RowCount = 1;
	GetRuntimeGridSize(ColumnCount, RowCount);

	ColumnCount = FMath::Max(1, ColumnCount);
	RowCount = FMath::Max(1, RowCount);

	const FVector2D GrooveSize = GrooveBounds.GetSize();
	if (GrooveSize.X <= KINDA_SMALL_NUMBER || GrooveSize.Y <= KINDA_SMALL_NUMBER)
	{
		HideLabels(ColumnCoordinateLabels);
		HideLabels(BottomColumnCoordinateLabels);
		HideLabels(RowCoordinateLabels);
		HideLabels(RightRowCoordinateLabels);
		return;
	}

	const float LabelOffset = FMath::Max(0.0f, CoordinateLabelOffset);
	const float BoardTopZ = BoardMeshComponent ? BoardMeshComponent->GetLocalBoardTopZ() : 0.0f;
	const float LocalZ = BoardTopZ + CoordinateLabelZOffset;
	const float TileSize = GetScaledRuntimeSquareTileSize();
	const float ColumnStep = TileSize;
	const float RowStep = TileSize;
	const float TopLabelY = BoardBounds.Max.Y > GrooveBounds.Max.Y
		? (GrooveBounds.Max.Y + BoardBounds.Max.Y) * 0.5f
		: GrooveBounds.Max.Y + LabelOffset;
	const float BottomLabelY = BoardBounds.Min.Y < GrooveBounds.Min.Y
		? (GrooveBounds.Min.Y + BoardBounds.Min.Y) * 0.5f
		: GrooveBounds.Min.Y - LabelOffset;
	const float LeftLabelX = BoardBounds.Max.X > GrooveBounds.Max.X
		? (GrooveBounds.Max.X + BoardBounds.Max.X) * 0.5f
		: GrooveBounds.Max.X + LabelOffset;
	const float RightLabelX = BoardBounds.Min.X < GrooveBounds.Min.X
		? (GrooveBounds.Min.X + BoardBounds.Min.X) * 0.5f
		: GrooveBounds.Min.X - LabelOffset;

	const auto RefreshColumnLabels = [this, &HideLabels, ColumnCount, ColumnStep, LocalZ, ContentBounds, GrooveBounds](
		TArray<TObjectPtr<UTextRenderComponent>>& Components,
		bool bShowSide,
		float LabelY,
		const FRotator& LabelRotation,
		const TCHAR* NamePrefix
	)
	{
		if (!bShowSide)
		{
			HideLabels(Components);
			return;
		}

		for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
		{
			UTextRenderComponent* LabelComponent = GetOrCreateCoordinateLabelComponent(
				Components,
				ColumnIndex,
				NamePrefix
			);
			const float ColumnCenterX = ContentBounds.Max.X - (static_cast<float>(ColumnIndex) + 0.5f) * ColumnStep;
			const FVector LocalLocation(
				ColumnCenterX,
				LabelY,
				LocalZ
			);
			ConfigureCoordinateLabelComponent(
				LabelComponent,
				MakeColumnCoordinateLabel(ColumnIndex),
				LocalLocation,
				LabelRotation
			);

			const bool bLabelInsideGroove =
				ColumnCenterX >= GrooveBounds.Min.X - KINDA_SMALL_NUMBER &&
				ColumnCenterX <= GrooveBounds.Max.X + KINDA_SMALL_NUMBER;
			LabelComponent->SetVisibility(bLabelInsideGroove);
			LabelComponent->SetHiddenInGame(!bLabelInsideGroove);
		}

		for (int32 Index = ColumnCount; Index < Components.Num(); ++Index)
		{
			if (IsValid(Components[Index]))
			{
				Components[Index]->SetVisibility(false);
				Components[Index]->SetHiddenInGame(true);
			}
		}
	};

	const auto RefreshRowLabels = [this, &HideLabels, RowCount, RowStep, LocalZ, ContentBounds, GrooveBounds](
		TArray<TObjectPtr<UTextRenderComponent>>& Components,
		bool bShowSide,
		float LabelX,
		const FRotator& LabelRotation,
		const TCHAR* NamePrefix
	)
	{
		if (!bShowSide)
		{
			HideLabels(Components);
			return;
		}

		for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
		{
			UTextRenderComponent* LabelComponent = GetOrCreateCoordinateLabelComponent(
				Components,
				RowIndex,
				NamePrefix
			);
			const float RowCenterY = bNumberCoordinateRowsFromTop
				? ContentBounds.Max.Y - (static_cast<float>(RowIndex) + 0.5f) * RowStep
				: ContentBounds.Min.Y + (static_cast<float>(RowIndex) + 0.5f) * RowStep;
			const FVector LocalLocation(LabelX, RowCenterY, LocalZ);
			ConfigureCoordinateLabelComponent(
				LabelComponent,
				MakeRowCoordinateLabel(RowIndex),
				LocalLocation,
				LabelRotation
			);

			const bool bLabelInsideGroove =
				RowCenterY >= GrooveBounds.Min.Y - KINDA_SMALL_NUMBER &&
				RowCenterY <= GrooveBounds.Max.Y + KINDA_SMALL_NUMBER;
			LabelComponent->SetVisibility(bLabelInsideGroove);
			LabelComponent->SetHiddenInGame(!bLabelInsideGroove);
		}

		for (int32 Index = RowCount; Index < Components.Num(); ++Index)
		{
			if (IsValid(Components[Index]))
			{
				Components[Index]->SetVisibility(false);
				Components[Index]->SetHiddenInGame(true);
			}
		}
	};

	RefreshColumnLabels(
		ColumnCoordinateLabels,
		bShowTopCoordinateLabels,
		TopLabelY,
		TopCoordinateLabelRelativeRotation,
		TEXT("TopColumnCoordinateLabel")
	);
	RefreshColumnLabels(
		BottomColumnCoordinateLabels,
		bShowBottomCoordinateLabels,
		BottomLabelY,
		BottomCoordinateLabelRelativeRotation,
		TEXT("BottomColumnCoordinateLabel")
	);
	RefreshRowLabels(
		RowCoordinateLabels,
		bShowLeftCoordinateLabels,
		LeftLabelX,
		LeftCoordinateLabelRelativeRotation,
		TEXT("LeftRowCoordinateLabel")
	);
	RefreshRowLabels(
		RightRowCoordinateLabels,
		bShowRightCoordinateLabels,
		RightLabelX,
		RightCoordinateLabelRelativeRotation,
		TEXT("RightRowCoordinateLabel")
	);
}

void AGSMMap3D::ClearCoordinateLabels()
{
	const auto DestroyLabels = [this](TArray<TObjectPtr<UTextRenderComponent>>& Components)
	{
		for (UTextRenderComponent* LabelComponent : Components)
		{
			if (IsValid(LabelComponent))
			{
				LabelComponent->DestroyComponent();
			}
		}

		Components.Reset();
	};

	DestroyLabels(ColumnCoordinateLabels);
	DestroyLabels(BottomColumnCoordinateLabels);
	DestroyLabels(RowCoordinateLabels);
	DestroyLabels(RightRowCoordinateLabels);
}

UTextRenderComponent* AGSMMap3D::GetOrCreateCoordinateLabelComponent(
	TArray<TObjectPtr<UTextRenderComponent>>& Components,
	int32 Index,
	const TCHAR* NamePrefix
)
{
	if (Index < 0)
	{
		return nullptr;
	}

	while (Components.Num() <= Index)
	{
		Components.Add(nullptr);
	}

	if (IsValid(Components[Index]))
	{
		return Components[Index];
	}

	const FName ComponentName = MakeUniqueObjectName(
		this,
		UTextRenderComponent::StaticClass(),
		FName(*FString::Printf(TEXT("%s_%d"), NamePrefix, Index))
	);

	UTextRenderComponent* LabelComponent = NewObject<UTextRenderComponent>(this, ComponentName, RF_Transient);
	if (!LabelComponent)
	{
		return nullptr;
	}

	LabelComponent->SetMobility(EComponentMobility::Movable);
	LabelComponent->SetupAttachment(RootSceneComponent);
	LabelComponent->RegisterComponent();
	Components[Index] = LabelComponent;
	return LabelComponent;
}

void AGSMMap3D::ConfigureCoordinateLabelComponent(
	UTextRenderComponent* LabelComponent,
	const FString& LabelText,
	const FVector& LocalLocation,
	const FRotator& LocalRotation
) const
{
	if (!IsValid(LabelComponent))
	{
		return;
	}

	LabelComponent->SetText(FText::FromString(LabelText));
	LabelComponent->SetWorldSize(FMath::Max(1.0f, CoordinateLabelWorldSize));
	LabelComponent->SetTextRenderColor(CoordinateLabelColor.ToFColor(true));
	LabelComponent->SetHorizontalAlignment(EHTA_Center);
	LabelComponent->SetVerticalAlignment(EVRTA_TextCenter);
	LabelComponent->SetRelativeLocation(LocalLocation);
	LabelComponent->SetRelativeRotation(LocalRotation);
	LabelComponent->SetRelativeScale3D(FVector::OneVector);
	LabelComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LabelComponent->SetGenerateOverlapEvents(false);
	LabelComponent->SetCanEverAffectNavigation(false);
	LabelComponent->SetVisibility(true);
	LabelComponent->SetHiddenInGame(false);
}

FString AGSMMap3D::MakeColumnCoordinateLabel(int32 ColumnIndex) const
{
	return GSMLayout::MakeCoordinateLabel(
		ColumnIndex,
		MapConfig ? MapConfig->HorizontalLabelType : EGSMCoordinateLabelType::Letters);
}

FString AGSMMap3D::MakeRowCoordinateLabel(int32 RowIndex) const
{
	return GSMLayout::MakeCoordinateLabel(
		RowIndex,
		MapConfig ? MapConfig->VerticalLabelType : EGSMCoordinateLabelType::Numbers);
}

FVector2D AGSMMap3D::CalculateConstrainedTileContentDeltaLocal(const FVector2D& DesiredDelta) const
{
	FBox2D ContentBounds(ForceInit);
	FBox2D ViewportBounds(ForceInit);
	if (!GetSpawnedTileContentBoundsLocal(ContentBounds) || !GetMapScaleViewportBounds(ViewportBounds))
	{
		return DesiredDelta;
	}

	FVector2D ConstrainedDelta = DesiredDelta;
	const FVector2D ContentSize = ContentBounds.GetSize();
	const FVector2D ViewportSize = ViewportBounds.GetSize();

	const auto ConstrainAxis = [this](float DesiredAxisDelta, float ContentMin, float ContentMax, float ContentSizeAxis, float ViewportMin, float ViewportMax, float ViewportSizeAxis)
	{
		if (ViewportSizeAxis <= KINDA_SMALL_NUMBER || ContentSizeAxis <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}

		if (ContentSizeAxis <= ViewportSizeAxis + KINDA_SMALL_NUMBER)
		{
			if (!bCenterTilesAtMinMapScale)
			{
				const float MinAllowedDelta = ViewportMin - ContentMin;
				const float MaxAllowedDelta = ViewportMax - ContentMax;
				return FMath::Clamp(DesiredAxisDelta, MinAllowedDelta, MaxAllowedDelta);
			}

			const float ContentCenter = (ContentMin + ContentMax) * 0.5f;
			const float ViewportCenter = (ViewportMin + ViewportMax) * 0.5f;
			return ViewportCenter - ContentCenter;
		}

		const float MinAllowedDelta = ViewportMax - ContentMax;
		const float MaxAllowedDelta = ViewportMin - ContentMin;
		return FMath::Clamp(DesiredAxisDelta, MinAllowedDelta, MaxAllowedDelta);
	};

	ConstrainedDelta.X = ConstrainAxis(
		DesiredDelta.X,
		ContentBounds.Min.X,
		ContentBounds.Max.X,
		ContentSize.X,
		ViewportBounds.Min.X,
		ViewportBounds.Max.X,
		ViewportSize.X
	);
	ConstrainedDelta.Y = ConstrainAxis(
		DesiredDelta.Y,
		ContentBounds.Min.Y,
		ContentBounds.Max.Y,
		ContentSize.Y,
		ViewportBounds.Min.Y,
		ViewportBounds.Max.Y,
		ViewportSize.Y
	);

	return ConstrainedDelta;
}

FVector2D AGSMMap3D::MoveSpawnedTilesByLocalDelta(const FVector2D& DesiredDelta)
{
	const FVector2D AppliedDelta = CalculateConstrainedTileContentDeltaLocal(DesiredDelta);
	if (AppliedDelta.IsNearlyZero())
	{
		return FVector2D::ZeroVector;
	}

	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (!IsValid(TileActor))
		{
			continue;
		}

		FTransform TileRelativeTransform = TileActor->GetActorTransform().GetRelativeTransform(GetActorTransform());
		FVector TileRelativeLocation = TileRelativeTransform.GetLocation();
		TileRelativeLocation.X += AppliedDelta.X;
		TileRelativeLocation.Y += AppliedDelta.Y;
		TileRelativeTransform.SetLocation(TileRelativeLocation);
		TileActor->SetActorRelativeTransform(TileRelativeTransform);
	}

	if (MapTerrainMeshComponent && MapTerrainMeshComponent->IsVisible())
	{
		FVector TerrainRelativeLocation = MapTerrainMeshComponent->GetRelativeLocation();
		TerrainRelativeLocation.X += AppliedDelta.X;
		TerrainRelativeLocation.Y += AppliedDelta.Y;
		MapTerrainMeshComponent->SetRelativeLocation(TerrainRelativeLocation);
		if (!MapConfig || MapConfig->bAlignWholeMapTerrainMeshBottomToGroovePlane)
		{
			UpdateMapTerrainMeshTransform();
		}
	}

	RefreshSpawnedTilePieces();
	RefreshNavigationPathVisual();
	return AppliedDelta;
}

void AGSMMap3D::ConstrainSpawnedTilesToScaleViewport()
{
	MoveSpawnedTilesByLocalDelta(FVector2D::ZeroVector);
}

float AGSMMap3D::GetScaledRuntimeSquareTileSize() const
{
	return FMath::Max(1.0f, RuntimeBaseSquareTileSize * GetEffectiveMapContentScale());
}

bool AGSMMap3D::IsValidWalkingNeighborCoordinate(const FIntPoint& FromCoordinate, const FIntPoint& ToCoordinate) const
{
	const FIntPoint Delta = ToCoordinate - FromCoordinate;
	return FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) == 1;
}

FTransform AGSMMap3D::MakeScaledTileLocalTransform(const FTransform& BaseLocalTransform) const
{
	FTransform ScaledLocalTransform = BaseLocalTransform;
	FVector ScaledLocalLocation = ScaledLocalTransform.GetLocation();
	const float MapContentScale = GetEffectiveMapContentScale();
	ScaledLocalLocation.X *= MapContentScale;
	ScaledLocalLocation.Y *= MapContentScale;
	ScaledLocalTransform.SetLocation(ScaledLocalLocation);
	return ScaledLocalTransform;
}

void AGSMMap3D::ApplyMapScaleToSpawnedTiles(float OldMapContentScale, const FVector& LocalPivot)
{
	const float SafeOldScale = FMath::Max(OldMapContentScale, KINDA_SMALL_NUMBER);
	const float NewMapContentScale = GetEffectiveMapContentScale();
	const float ScaleRatio = NewMapContentScale / SafeOldScale;
	const float ScaledTileSize = GetScaledRuntimeSquareTileSize();

	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (!IsValid(TileActor))
		{
			continue;
		}

		FTransform TileRelativeTransform = TileActor->GetActorTransform().GetRelativeTransform(GetActorTransform());
		FVector TileRelativeLocation = TileRelativeTransform.GetLocation();
		TileRelativeLocation.X = LocalPivot.X + (TileRelativeLocation.X - LocalPivot.X) * ScaleRatio;
		TileRelativeLocation.Y = LocalPivot.Y + (TileRelativeLocation.Y - LocalPivot.Y) * ScaleRatio;
		TileRelativeTransform.SetLocation(TileRelativeLocation);

		TileActor->SetActorRelativeTransform(TileRelativeTransform);
		TileActor->SetRuntimeMapScale(NewMapContentScale);
		TileActor->SetRuntimeSquareTileSize(ScaledTileSize);
	}

	ApplyMapScaleToTerrainMesh(OldMapContentScale, LocalPivot);
	RefreshSpawnedTilePieces();
}

void AGSMMap3D::NotifySpawnedTilesMapScaleChanged(float PreviousMapScale)
{
	if (FMath::IsNearlyEqual(PreviousMapScale, CurrentMapScale))
	{
		return;
	}

	const int32 PreviousScaleLevel = ResolveMapScaleLevel(PreviousMapScale);
	const int32 NewScaleLevel = ResolveMapScaleLevel(CurrentMapScale);
	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (IsValid(TileActor))
		{
			TileActor->NotifyMapScaleChanged(
				PreviousMapScale,
				CurrentMapScale,
				PreviousScaleLevel,
				NewScaleLevel);
		}
	}
}

void AGSMMap3D::ApplyMapScaleToTerrainMesh(float OldMapContentScale, const FVector& LocalPivot)
{
	if (!MapTerrainMeshComponent || !MapTerrainMeshComponent->IsVisible())
	{
		return;
	}

	const float SafeOldScale = FMath::Max(OldMapContentScale, KINDA_SMALL_NUMBER);
	const float ScaleRatio = GetEffectiveMapContentScale() / SafeOldScale;
	FVector TerrainRelativeLocation = MapTerrainMeshComponent->GetRelativeLocation();
	TerrainRelativeLocation.X = LocalPivot.X + (TerrainRelativeLocation.X - LocalPivot.X) * ScaleRatio;
	TerrainRelativeLocation.Y = LocalPivot.Y + (TerrainRelativeLocation.Y - LocalPivot.Y) * ScaleRatio;
	MapTerrainMeshComponent->SetRelativeLocation(TerrainRelativeLocation);

	if (MapConfig && MapConfig->bFitWholeMapTerrainMeshToTileGridBounds)
	{
		UpdateMapTerrainMeshTransform();
	}
	else
	{
		FVector TerrainScale = MapTerrainMeshComponent->GetRelativeScale3D();
		TerrainScale.X *= ScaleRatio;
		TerrainScale.Y *= ScaleRatio;
		if (!MapConfig || MapConfig->bScaleWholeMapTerrainMeshZWithXY)
		{
			TerrainScale.Z *= ScaleRatio;
		}
		MapTerrainMeshComponent->SetRelativeScale3D(TerrainScale);
	}
}

bool AGSMMap3D::FindMapTerrainVisibleRegionMinZ(
	const FVector& TerrainRelativeLocation,
	const FVector& TerrainScale,
	const FRotator& TerrainRelativeRotation,
	float& OutMinZ
) const
{
	OutMinZ = 0.0f;

	const UStaticMesh* TerrainMesh = MapTerrainMeshComponent ? MapTerrainMeshComponent->GetStaticMesh() : nullptr;
	if (!TerrainMesh || !TerrainMesh->GetRenderData() || TerrainMesh->GetRenderData()->LODResources.IsEmpty())
	{
		return false;
	}

	FBox2D ViewportBounds(ForceInit);
	const bool bHasViewportBounds = GetMapScaleViewportBounds(ViewportBounds);
	if (!bHasViewportBounds && !bUseMapBounds)
	{
		return false;
	}

	const FStaticMeshLODResources& LODResources = TerrainMesh->GetRenderData()->LODResources[0];
	const FPositionVertexBuffer& PositionVertexBuffer = LODResources.VertexBuffers.PositionVertexBuffer;
	const uint32 VertexCount = PositionVertexBuffer.GetNumVertices();
	if (VertexCount == 0)
	{
		return false;
	}

	const uint32 SampleStep = FMath::Max<uint32>(
		1,
		FMath::DivideAndRoundUp(VertexCount, static_cast<uint32>(MaxVisibleTerrainHeightSamples))
	);

	bool bFoundVisibleVertex = false;
	float MinVisibleZ = TNumericLimits<float>::Max();

	for (uint32 VertexIndex = 0; VertexIndex < VertexCount; VertexIndex += SampleStep)
	{
		const FVector3f VertexPosition3f = PositionVertexBuffer.VertexPosition(VertexIndex);
		const FVector ScaledVertex(
			static_cast<double>(VertexPosition3f.X) * TerrainScale.X,
			static_cast<double>(VertexPosition3f.Y) * TerrainScale.Y,
			static_cast<double>(VertexPosition3f.Z) * TerrainScale.Z
		);
		const FVector ActorLocalVertex = TerrainRelativeLocation + TerrainRelativeRotation.RotateVector(ScaledVertex);
		const FVector2D ActorLocalXY(ActorLocalVertex.X, ActorLocalVertex.Y);

		const bool bInsideVisibleRegion = bUseMapBounds
			? MapBounds.ContainsPoint(ActorLocalXY, 0.0f)
			: ViewportBounds.IsInside(ActorLocalXY);
		if (!bInsideVisibleRegion)
		{
			continue;
		}

		MinVisibleZ = FMath::Min(MinVisibleZ, static_cast<float>(ActorLocalVertex.Z - TerrainRelativeLocation.Z));
		bFoundVisibleVertex = true;
	}

	if (!bFoundVisibleVertex)
	{
		return false;
	}

	OutMinZ = MinVisibleZ;
	return true;
}

void AGSMMap3D::UpdateMapTerrainMeshTransform()
{
	if (!MapTerrainMeshComponent || !MapTerrainMeshComponent->GetStaticMesh())
	{
		return;
	}

	FBox2D TargetBounds(ForceInit);
	if (!GetMapTerrainTargetBoundsLocal(TargetBounds))
	{
		return;
	}

	const FBoxSphereBounds MeshBounds = MapTerrainMeshComponent->GetStaticMesh()->GetBounds();
	const FVector MeshSize = MeshBounds.BoxExtent * 2.0;
	const FVector BaseScale = MapConfig
		? MapConfig->WholeMapTerrainMeshBaseScale
		: FVector::OneVector;
	FVector TerrainScale(
		FMath::Max(BaseScale.X, KINDA_SMALL_NUMBER),
		FMath::Max(BaseScale.Y, KINDA_SMALL_NUMBER),
		FMath::Max(BaseScale.Z, KINDA_SMALL_NUMBER)
	);

	float FitScaleX = 1.0f;
	float FitScaleY = 1.0f;
	if (MapConfig && MapConfig->bFitWholeMapTerrainMeshToTileGridBounds)
	{
		const FVector2D TargetSize = TargetBounds.GetSize();
		if (MeshSize.X > KINDA_SMALL_NUMBER)
		{
			FitScaleX = TargetSize.X / MeshSize.X;
			TerrainScale.X = FitScaleX * BaseScale.X;
		}
		if (MeshSize.Y > KINDA_SMALL_NUMBER)
		{
			FitScaleY = TargetSize.Y / MeshSize.Y;
			TerrainScale.Y = FitScaleY * BaseScale.Y;
		}
		if (!MapConfig || MapConfig->bScaleWholeMapTerrainMeshZWithXY)
		{
			TerrainScale.Z = FMath::Min(FitScaleX, FitScaleY) * BaseScale.Z;
		}
	}

	const FVector2D TargetCenter = TargetBounds.GetCenter();
	const float TargetZ = BoardMeshComponent
		? BoardMeshComponent->GetLocalGrooveFloorCenter().Z
		: 0.0f;
	const FVector DesiredMeshBoundsCenter(
		TargetCenter.X,
		TargetCenter.Y,
		TargetZ
	);
	const FVector ScaledMeshBoundsOrigin(
		MeshBounds.Origin.X * TerrainScale.X,
		MeshBounds.Origin.Y * TerrainScale.Y,
		MeshBounds.Origin.Z * TerrainScale.Z
	);
	FRotator TerrainRelativeRotation = MapConfig
		? MapConfig->WholeMapTerrainMeshRelativeRotation
		: FRotator::ZeroRotator;
	TerrainRelativeRotation.Yaw += (MapConfig ? MapConfig->WholeMapTerrainMeshYawDegrees : 180.0f) + MapTerrainYawOffset;
	const FVector TerrainRelativeOffset = MapConfig
		? MapConfig->WholeMapTerrainMeshRelativeOffset
		: FVector::ZeroVector;
	const FVector TerrainHeightOffset(
		0.0,
		0.0,
		(MapConfig ? MapConfig->WholeMapTerrainMeshHeightOffset : 0.0f) + MapTerrainHeightOffset
	);
	const FVector BoundsOriginOffset = TerrainRelativeRotation.RotateVector(ScaledMeshBoundsOrigin);
	FVector TerrainRelativeLocation = DesiredMeshBoundsCenter + TerrainRelativeOffset + TerrainHeightOffset - BoundsOriginOffset;
	if (!MapConfig || MapConfig->bAlignWholeMapTerrainMeshBottomToGroovePlane)
	{
		float VisibleRegionMinZ = 0.0f;
		if (!FindMapTerrainVisibleRegionMinZ(
			TerrainRelativeLocation,
			TerrainScale,
			TerrainRelativeRotation,
			VisibleRegionMinZ
		))
		{
			const FBox RotatedScaledMeshBounds = BuildRotatedScaledMeshBoundsBox(
				MeshBounds,
				TerrainScale,
				TerrainRelativeRotation
			);
			VisibleRegionMinZ = RotatedScaledMeshBounds.Min.Z;
		}

		TerrainRelativeLocation.Z = TargetZ + TerrainRelativeOffset.Z + TerrainHeightOffset.Z - VisibleRegionMinZ;
	}

	MapTerrainMeshComponent->SetRelativeRotation(TerrainRelativeRotation);
	MapTerrainMeshComponent->SetRelativeScale3D(TerrainScale);
	MapTerrainMeshComponent->SetRelativeLocation(TerrainRelativeLocation);
}

void AGSMMap3D::RefreshSpawnedTilePieces() const
{
	for (AGSMTile3D* TileActor : SpawnedTiles)
	{
		if (IsValid(TileActor))
		{
			TileActor->RefreshMapPieces();
		}
	}
}

void AGSMMap3D::RegisterWithMapSubsystem()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UGSMMapSubsystem* MapSubsystem = GameInstance->GetSubsystem<UGSMMapSubsystem>())
		{
			UGSMMapData* RegisteredMapData = nullptr;
			if (MapSubsystem->RegisterMap3D(this, bUseDefaultMapData, MapGuid, RegisteredMapData))
			{
				MapData = RegisteredMapData;
				MapGuid = MapData->GetMapGuid();
				MapConfig = MapData->GetMapDataAsset();
				MapSubsystem->RegisterGridStrategyMap(this, true);
			}
		}
	}
}

void AGSMMap3D::UnregisterFromMapSubsystem()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UGSMMapSubsystem* MapSubsystem = GameInstance->GetSubsystem<UGSMMapSubsystem>())
		{
			MapSubsystem->UnregisterMap3D(this, MapData);
			MapSubsystem->UnregisterGridStrategyMap(this);
		}
	}
	MapData = nullptr;
}

void AGSMMap3D::HandleMapDataCleared(UGSMMapData* ClearedMapData)
{
	if (MapData != ClearedMapData)
	{
		return;
	}

	UnregisterFromMapSubsystem();
	ClearMapTiles();
	MapConfig = nullptr;
	MapGuid.Invalidate();
	RefreshMapTerrainMesh();
}
