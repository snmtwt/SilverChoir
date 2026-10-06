#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"

#include "Components/BoxComponent.h"
#include "Components/DecalComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "GridStrategyMapSystem/Data/GSMPieceData.h"
#include "GridStrategyMapSystem/Data/GSMSettings.h"
#include "GridStrategyMapSystem/Data/GSMMapSubsystem.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Display3D/GSMPiece3D.h"
#include "GridStrategyMapSystem/Display2D/GSMTileContextMenu.h"
#include "GridStrategyMapSystem/Display2D/GSMTileMenuButton.h"
#include "GridStrategyMapSystem/Display2D/GSMTileMenuTypes.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Blueprint/UserWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogGSMTile3D, Log, All);

namespace
{
	const FName GMapBoundsMaskEnabledParameterName(TEXT("GSM_MapBoundsMaskEnabled"));
	const FName GMapBoundsCenterParameterName(TEXT("GSM_MapBoundsCenter"));
	const FName GMapBoundsAxisXParameterName(TEXT("GSM_MapBoundsAxisX"));
	const FName GMapBoundsAxisYParameterName(TEXT("GSM_MapBoundsAxisY"));
	const FName GMapBoundsHalfSizeParameterName(TEXT("GSM_MapBoundsHalfSize"));
	const FName GMapBoundsFeatherParameterName(TEXT("GSM_MapBoundsFeather"));
	const FName GDecalReceiverStencilEnabledParameterName(TEXT("GSM_DecalReceiverStencilEnabled"));
	const FName GDecalReceiverStencilValueParameterName(TEXT("GSM_DecalReceiverStencilValue"));

	void ConfigureMouseOnlyCollision(UPrimitiveComponent* Component, ECollisionEnabled::Type CollisionEnabled)
	{
		if (!Component)
		{
			return;
		}

		Component->SetSimulatePhysics(false);
		Component->SetNotifyRigidBodyCollision(false);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCollisionEnabled(CollisionEnabled);
		Component->SetCollisionResponseToAllChannels(ECR_Ignore);
		Component->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Component->CanCharacterStepUpOn = ECB_No;
	}
}

AGSMTile3D::AGSMTile3D()
{
	PrimaryActorTick.bCanEverTick = false;

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootSceneComponent);

	TileMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TileMesh"));
	TileMeshComponent->SetupAttachment(RootSceneComponent);
	ConfigureMouseOnlyCollision(TileMeshComponent, ECollisionEnabled::QueryOnly);

	TileCollisionComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("TileCollision"));
	TileCollisionComponent->SetupAttachment(RootSceneComponent);
	TileCollisionComponent->SetRelativeLocation(TileCollisionRelativeLocation);
	TileCollisionComponent->SetBoxExtent(FVector(
		GSMLayout::FixedSquareTileSize * 0.5f,
		GSMLayout::FixedSquareTileSize * 0.5f,
		FMath::Max(1.0f, TileCollisionHeight) * 0.5f));
	ConfigureMouseOnlyCollision(TileCollisionComponent, ECollisionEnabled::QueryOnly);

	EdgeDecalComponent = CreateDefaultSubobject<UDecalComponent>(TEXT("EdgeDecal"));
	EdgeDecalComponent->SetupAttachment(RootSceneComponent);
	EdgeDecalComponent->SetRelativeLocation(EdgeDecalRelativeLocation);
	EdgeDecalComponent->SetRelativeRotation(EdgeDecalRelativeRotation);
	EdgeDecalComponent->DecalSize = EdgeDecalSize;
}

void AGSMTile3D::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshTileVisuals();
}

void AGSMTile3D::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BindTileData(nullptr);
	ClearMapPieces();
	Super::EndPlay(EndPlayReason);
}

void AGSMTile3D::BindTileData(UGSMTileData* InTileData)
{
	if (TileData == InTileData)
	{
		return;
	}
	if (TileData)
	{
		TileData->OnPieceAdded.RemoveDynamic(this, &AGSMTile3D::HandlePieceDataAdded);
		TileData->OnPieceUpdated.RemoveDynamic(this, &AGSMTile3D::HandlePieceDataUpdated);
		TileData->OnPieceRemoved.RemoveDynamic(this, &AGSMTile3D::HandlePieceDataRemoved);
		TileData->UnregisterTile3D(this);
	}

	ClearMapPieces();
	bTileInitializationNotified = false;
	TileData = InTileData;
	if (!TileData)
	{
		return;
	}

	TileData->OnPieceAdded.AddUniqueDynamic(this, &AGSMTile3D::HandlePieceDataAdded);
	TileData->OnPieceUpdated.AddUniqueDynamic(this, &AGSMTile3D::HandlePieceDataUpdated);
	TileData->OnPieceRemoved.AddUniqueDynamic(this, &AGSMTile3D::HandlePieceDataRemoved);
	TileData->RegisterTile3D(this);
	TryNotifyTileInitialized();
}

void AGSMTile3D::OnTileInitialized_Implementation(UGSMTileData* InitializedTileData)
{
}

float AGSMTile3D::GetCurrentMapScale() const
{
	const AGSMMap3D* OwningMap3D = OwningMap.IsValid()
		? OwningMap.Get()
		: Cast<AGSMMap3D>(GetOwner());
	return OwningMap3D ? OwningMap3D->GetCurrentMapScale() : 1.0f;
}

int32 AGSMTile3D::GetCurrentMapScaleLevel() const
{
	const AGSMMap3D* OwningMap3D = OwningMap.IsValid()
		? OwningMap.Get()
		: Cast<AGSMMap3D>(GetOwner());
	return OwningMap3D ? OwningMap3D->GetCurrentMapScaleLevel() : 0;
}

void AGSMTile3D::OnMapScaleChanged_Implementation(float PreviousMapScale, float NewMapScale)
{
}

void AGSMTile3D::OnMapScaleLevelChanged_Implementation(
	int32 PreviousScaleLevel,
	int32 NewScaleLevel,
	float PreviousMapScale,
	float NewMapScale)
{
}

void AGSMTile3D::NotifyMapScaleChanged(
	float PreviousMapScale,
	float NewMapScale,
	int32 PreviousScaleLevel,
	int32 NewScaleLevel)
{
	OnMapScaleChanged(PreviousMapScale, NewMapScale);

	if (PreviousScaleLevel != NewScaleLevel && IsValid(this))
	{
		OnMapScaleLevelChanged(
			PreviousScaleLevel,
			NewScaleLevel,
			PreviousMapScale,
			NewMapScale);
		if (IsValid(this))
		{
			HandleMapScaleLevelChangedNative(
				PreviousScaleLevel,
				NewScaleLevel,
				PreviousMapScale,
				NewMapScale);
		}
	}
}

void AGSMTile3D::TryNotifyTileInitialized()
{
	if (bTileInitializationNotified
		|| !bMapTileInitialized
		|| !IsValid(TileData)
		|| TileData->GetTile3D() != this)
	{
		return;
	}

	bTileInitializationNotified = true;
	OnTileInitialized(TileData);
	if (IsValid(this))
	{
		HandleTileInitializedNative(TileData);
	}
}

void AGSMTile3D::HandlePieceDataAdded(UGSMPieceData* PieceData, const FGSMPiecePlacement& Placement)
{
	if (!IsValid(PieceData))
	{
		return;
	}
	const FName PieceId(*PieceData->GetPieceGuid().ToString(EGuidFormats::DigitsWithHyphens));
	AGSMPiece3D* Piece3D = GetMapPieceById(PieceId);
	if (!Piece3D)
	{
		Piece3D = AddMapPieceWithId(PieceId, PieceData->GetPiece3DClass(), Placement.RelativeTileXY,
			Placement.RelativeTileYaw, Placement.DefaultScale);
	}
	if (Piece3D)
	{
		Piece3D->BindPieceData(PieceData);
		Piece3D->SetRelativeTileZ(Placement.RelativeTileZ);
		RefreshMapPieceTransform(Piece3D);
	}
	OnPieceDataAdded(PieceData, Placement.RelativeTileXY, Placement.RelativeTileYaw);
	if (IsValid(this))
	{
		HandlePieceDataAddedNative(PieceData);
	}
}

void AGSMTile3D::HandlePieceDataUpdated(UGSMPieceData* PieceData)
{
	if (!IsValid(PieceData))
	{
		return;
	}
	const FName PieceId(*PieceData->GetPieceGuid().ToString(EGuidFormats::DigitsWithHyphens));
	AGSMPiece3D* Piece3D = GetMapPieceById(PieceId);
	if (!Piece3D || (PieceData->GetPiece3DClass() && !Piece3D->IsA(PieceData->GetPiece3DClass())))
	{
		if (Piece3D)
		{
			RemoveMapPiece(Piece3D);
		}
		HandlePieceDataAdded(PieceData, PieceData->GetPlacement());
		return;
	}
	const FGSMPiecePlacement Placement = PieceData->GetPlacement();
	Piece3D->SetRelativeTileXY(Placement.RelativeTileXY);
	Piece3D->SetRelativeTileZ(Placement.RelativeTileZ);
	Piece3D->SetRelativeTileYaw(Placement.RelativeTileYaw);
	Piece3D->SetDefaultPieceScale(Placement.DefaultScale);
	RefreshMapPieceTransform(Piece3D);
	OnPieceDataUpdated(PieceData);
	if (IsValid(this))
	{
		HandlePieceDataUpdatedNative(PieceData);
	}
}

void AGSMTile3D::HandlePieceDataRemoved(UGSMPieceData* PieceData)
{
	if (IsValid(PieceData))
	{
		const FName PieceId(*PieceData->GetPieceGuid().ToString(EGuidFormats::DigitsWithHyphens));
		RemoveMapPieceById(PieceId);
	}
	OnPieceDataRemoved(PieceData);
	if (IsValid(this))
	{
		HandlePieceDataRemovedNative(PieceData);
	}
}

void AGSMTile3D::OnPieceDataAdded_Implementation(UGSMPieceData* PieceData, FVector2D RelativeTileXY, float RelativeTileYaw)
{
}

void AGSMTile3D::OnPieceDataUpdated_Implementation(UGSMPieceData* PieceData)
{
}

void AGSMTile3D::OnPieceDataRemoved_Implementation(UGSMPieceData* PieceData)
{
}

void AGSMTile3D::UpdateTile3D_Implementation()
{
	RefreshTileVisuals();
	RefreshMapPieces();
}

void AGSMTile3D::NotifyActorOnReleased(FKey ButtonReleased)
{
	Super::NotifyActorOnReleased(ButtonReleased);
	if (ButtonReleased == EKeys::RightMouseButton)
	{
		RequestMapTileRightClick(GetBestClickLocationFromCursor());
		return;
	}

	if (AGSMMap3D* GridMap = GetOwningGridMap())
	{
		if (GridMap->ConsumeTileClickSuppressionAfterDrag())
		{
			return;
		}
	}

	RequestMapTileClick(GetBestClickLocationFromCursor());
}

void AGSMTile3D::NotifyActorBeginCursorOver()
{
	Super::NotifyActorBeginCursorOver();
	RequestMapTileHover();
}

void AGSMTile3D::InitializeMapTile(
	const FGSMTileEntry& InTileEntry,
	bool bInRegionAllowsClick
)
{
	AGSMMap3D* OwnerMap = Cast<AGSMMap3D>(GetOwner());
	OwningMap = OwnerMap;
	OwningMapId = IsValid(OwnerMap) ? OwnerMap->GetMapId() : NAME_None;
	TileId = InTileEntry.TileId;
	RegionId = InTileEntry.RegionId;
	DisplayName = InTileEntry.DisplayName;
	GridCoordinate = InTileEntry.GridCoordinate;
	bCanReceiveClick = InTileEntry.bCanBeClicked;
	bRegionAllowsClick = bInRegionAllowsClick;
	RuntimeNavigationSettings = InTileEntry.Navigation;

	RefreshTileVisuals();
	ApplyHiddenMaterialScalar();
	RefreshCollisionForHiddenState();
	bMapTileInitialized = true;
	TryNotifyTileInitialized();
}

void AGSMTile3D::RefreshTileVisuals()
{
	if (TileMeshComponent)
	{
		if (TileMesh)
		{
			TileMeshComponent->SetStaticMesh(TileMesh);
		}

		const bool bHasStaticTileMesh = bUseTileStaticMeshVisual && TileMeshComponent->GetStaticMesh() != nullptr;
		TileMeshComponent->SetVisibility(bHasStaticTileMesh);
		TileMeshComponent->SetHiddenInGame(!bHasStaticTileMesh);

		if (TileMaterial)
		{
			TileMeshComponent->SetMaterial(0, TileMaterial);
		}
	}

	if (TileCollisionComponent)
	{
		TileCollisionComponent->SetRelativeLocation(TileCollisionRelativeLocation);
	}

	EdgeDecalComponent->SetVisibility(bUseEdgeDecal);
	EdgeDecalComponent->SetRelativeLocation(EdgeDecalRelativeLocation);
	EdgeDecalComponent->SetRelativeRotation(EdgeDecalRelativeRotation);
	ApplyRuntimeSquareTileSize();

	if (EdgeDecalMaterial)
	{
		EdgeDecalComponent->SetDecalMaterial(EdgeDecalMaterial);
	}

	EdgeDecalMaterialInstance = nullptr;
	if (CanCreateRuntimeMaterialInstances() && EdgeDecalComponent->GetDecalMaterial())
	{
		EdgeDecalMaterialInstance = Cast<UMaterialInstanceDynamic>(EdgeDecalComponent->GetDecalMaterial());
		if (!EdgeDecalMaterialInstance)
		{
			EdgeDecalMaterialInstance = EdgeDecalComponent->CreateDynamicMaterialInstance();
		}
	}

	CacheMaterialInstances();
	ApplyEdgeDecalRuntimeScale();
	ApplySelectionDecalColor();
	ApplyDecalReceiverStencilSettings();
	ApplyHiddenMaterialScalar();
	ApplyMaterialBoundsMaskParameters();
	ApplyDecalReceiverStencilMaterialParameters();
	RefreshCollisionForHiddenState();
}

bool AGSMTile3D::CanCreateRuntimeMaterialInstances() const
{
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return false;
	}

	return GetWorld() != nullptr;
}

void AGSMTile3D::SetRuntimeSquareTileSize(float NewTileSize)
{
	RuntimeSquareTileSize = FMath::Max(1.0f, NewTileSize);
	ApplyRuntimeSquareTileSize();
	RefreshMapPieces();
}

void AGSMTile3D::SetTileStaticMeshVisualEnabled(bool bNewEnabled)
{
	if (bUseTileStaticMeshVisual == bNewEnabled)
	{
		return;
	}

	bUseTileStaticMeshVisual = bNewEnabled;
	RefreshTileVisuals();
}

void AGSMTile3D::SetRuntimeMapScale(float NewMapScale)
{
	RuntimeMapScale = FMath::Max(NewMapScale, KINDA_SMALL_NUMBER);
	ApplyRuntimeSquareTileSize();
	RefreshMapPieces();
}

AGSMPiece3D* AGSMTile3D::AddMapPiece(
	TSubclassOf<AGSMPiece3D> PieceClass,
	FVector2D RelativeTileXY,
	float RelativeTileYaw,
	float DefaultScale)
{
	return AddMapPieceWithId(NAME_None, PieceClass, RelativeTileXY, RelativeTileYaw, DefaultScale);
}

AGSMPiece3D* AGSMTile3D::AddMapPieceWithId(
	FName PieceId,
	TSubclassOf<AGSMPiece3D> PieceClass,
	FVector2D RelativeTileXY,
	float RelativeTileYaw,
	float DefaultScale)
{
	UWorld* World = GetWorld();
	if (!World || !PieceClass || PieceClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}

	RemoveInvalidMapPieces();

	const FName ResolvedPieceId = PieceId.IsNone() ? MakeUniqueMapPieceId(PieceClass) : PieceId;
	if (DoesMapPieceIdExist(ResolvedPieceId))
	{
		UE_LOG(
			LogGSMTile3D,
			Warning,
			TEXT("AddMapPieceWithId failed on tile %s: piece id %s already exists."),
			*GetName(),
			*ResolvedPieceId.ToString()
		);
		return nullptr;
	}

	FVector SpawnWorldLocation = GetActorLocation();
	ResolveMapPieceWorldLocation(RelativeTileXY, 0.0f, SpawnWorldLocation);

	const float SafeDefaultScale = FMath::Max(DefaultScale, KINDA_SMALL_NUMBER);
	const FVector SpawnWorldScale = FVector(SafeDefaultScale) * FMath::Max(RuntimeMapScale, KINDA_SMALL_NUMBER) * GetActorScale3D();
	const FRotator SpawnWorldRotation = GetActorRotation() + FRotator(0.0f, RelativeTileYaw, 0.0f);
	const FTransform SpawnTransform(SpawnWorldRotation, SpawnWorldLocation, SpawnWorldScale);

	AGSMPiece3D* PieceActor = World->SpawnActorDeferred<AGSMPiece3D>(
		PieceClass,
		SpawnTransform,
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn
	);
	if (!PieceActor)
	{
		return nullptr;
	}

	PieceActor->SetPieceId(ResolvedPieceId);
	PieceActor->InitializeGridMapPiece(this, RelativeTileXY, RelativeTileYaw, SafeDefaultScale);
	PieceActor->FinishSpawning(SpawnTransform);
	PieceActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);

	MapPieceActors.Add(PieceActor);
	RefreshMapPieceTransform(PieceActor);
	ApplyMapPieceHiddenState(PieceActor);
	return PieceActor;
}

AGSMPiece3D* AGSMTile3D::AddExistingMapPiece(
	AGSMPiece3D* PieceActor,
	FVector2D RelativeTileXY,
	float RelativeTileYaw,
	float DefaultScale)
{
	if (!IsValid(PieceActor))
	{
		return nullptr;
	}

	RemoveInvalidMapPieces();

	const FName ExistingPieceId = PieceActor->GetPieceId();
	const FName ResolvedPieceId = ExistingPieceId.IsNone()
		? MakeUniqueMapPieceId(PieceActor->GetClass())
		: ExistingPieceId;
	if (DoesMapPieceIdExist(ResolvedPieceId, PieceActor))
	{
		UE_LOG(
			LogGSMTile3D,
			Warning,
			TEXT("AddExistingMapPiece failed on tile %s: piece id %s already exists."),
			*GetName(),
			*ResolvedPieceId.ToString()
		);
		return nullptr;
	}

	if (AGSMTile3D* ExistingTile = PieceActor->GetOwningGridMapTile())
	{
		if (ExistingTile != this)
		{
			ExistingTile->UnregisterMapPiece(PieceActor);
		}
	}

	UnregisterMapPiece(PieceActor);

	const float SafeDefaultScale = FMath::Max(DefaultScale, KINDA_SMALL_NUMBER);

	PieceActor->SetOwner(this);
	PieceActor->SetPieceId(ResolvedPieceId);
	PieceActor->InitializeGridMapPiece(this, RelativeTileXY, RelativeTileYaw, SafeDefaultScale);
	PieceActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);

	MapPieceActors.Add(PieceActor);
	RefreshMapPieceTransform(PieceActor);
	ApplyMapPieceHiddenState(PieceActor);
	return PieceActor;
}

bool AGSMTile3D::BatchAddMapPieces(
	const TArray<FGSMTilePieceSpawnRequest>& PieceRequests,
	TArray<AGSMPiece3D*>& OutPieces)
{
	OutPieces.Reset();

	bool bAllSucceeded = true;
	for (const FGSMTilePieceSpawnRequest& PieceRequest : PieceRequests)
	{
		AGSMPiece3D* PieceActor = AddMapPieceWithId(
			PieceRequest.PieceId,
			PieceRequest.PieceClass,
			PieceRequest.RelativeTileXY,
			PieceRequest.RelativeTileYaw,
			PieceRequest.DefaultScale
		);
		if (PieceActor)
		{
			OutPieces.Add(PieceActor);
		}
		else
		{
			bAllSucceeded = false;
		}
	}

	return bAllSucceeded && OutPieces.Num() == PieceRequests.Num();
}

bool AGSMTile3D::RemoveMapPiece(AGSMPiece3D* PieceActor)
{
	if (!IsValid(PieceActor))
	{
		RemoveInvalidMapPieces();
		return false;
	}

	const int32 RemovedCount = MapPieceActors.Remove(PieceActor);
	if (RemovedCount <= 0)
	{
		return false;
	}

	NotifyMapPieceDetachedFromTile(PieceActor);
	PieceActor->Destroy();
	return true;
}

bool AGSMTile3D::RemoveMapPieceById(FName PieceId)
{
	return RemoveMapPiece(GetMapPieceById(PieceId));
}

bool AGSMTile3D::MoveMapPieceByIdToTile(
	FName PieceId,
	AGSMTile3D* TargetTile,
	FVector2D RelativeTileXY,
	float RelativeTileYaw,
	float DefaultScale,
	AGSMPiece3D*& OutPiece)
{
	OutPiece = nullptr;
	if (PieceId.IsNone() || !IsValid(TargetTile))
	{
		return false;
	}

	const FName SourceMapId = GetOwningGridMapId();
	const FName TargetMapId = TargetTile->GetOwningGridMapId();
	if (SourceMapId.IsNone() || TargetMapId.IsNone() || SourceMapId != TargetMapId)
	{
		return false;
	}

	return MoveMapPieceByIdToTileId(
		PieceId,
		TargetTile->GetTileId(),
		RelativeTileXY,
		RelativeTileYaw,
		DefaultScale,
		OutPiece
	);
}

bool AGSMTile3D::MoveMapPieceByIdToTileId(
	FName PieceId,
	FName TargetTileId,
	FVector2D RelativeTileXY,
	float RelativeTileYaw,
	float DefaultScale,
	AGSMPiece3D*& OutPiece)
{
	OutPiece = nullptr;
	if (PieceId.IsNone() || TargetTileId.IsNone() || OwningMapId.IsNone())
	{
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UGSMMapSubsystem* MapSubsystem = GameInstance->GetSubsystem<UGSMMapSubsystem>())
			{
				return MapSubsystem->MoveMapPieceToTile(
					OwningMapId,
					PieceId,
					NAME_None,
					TargetTileId,
					nullptr,
					RelativeTileXY,
					RelativeTileYaw,
					DefaultScale,
					OutPiece
				);
			}
		}
	}

	return false;
}

void AGSMTile3D::ClearMapPieces()
{
	TArray<TObjectPtr<AGSMPiece3D>> ExistingPieces = MapPieceActors;
	MapPieceActors.Reset();

	for (AGSMPiece3D* PieceActor : ExistingPieces)
	{
		if (IsValid(PieceActor))
		{
			PieceActor->Destroy();
		}
	}
}

bool AGSMTile3D::SetMapPieceRelativeTileXY(FName PieceId, FVector2D NewRelativeTileXY, float NewRelativeTileYaw)
{
	AGSMPiece3D* PieceActor = GetMapPieceById(PieceId);
	if (!IsValid(PieceActor))
	{
		return false;
	}

	if (!OwningMapId.IsNone() && !TileId.IsNone())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UGSMMapSubsystem* MapSubsystem = GameInstance->GetSubsystem<UGSMMapSubsystem>())
				{
					AGSMPiece3D* OutPiece = nullptr;
					if (MapSubsystem->MoveMapPieceToTile(
						OwningMapId,
						PieceId,
						TileId,
						TileId,
						nullptr,
						NewRelativeTileXY,
						NewRelativeTileYaw,
						PieceActor->GetDefaultPieceScale(),
						OutPiece))
					{
						return true;
					}
				}
			}
		}
	}

	PieceActor->SetRelativeTileXY(NewRelativeTileXY);
	PieceActor->SetRelativeTileYaw(NewRelativeTileYaw);
	return RefreshMapPieceTransform(PieceActor);
}

bool AGSMTile3D::AdjustMapPieceRelativeTileXYById(FName PieceId, FVector2D NewRelativeTileXY, float NewRelativeTileYaw)
{
	return SetMapPieceRelativeTileXY(PieceId, NewRelativeTileXY, NewRelativeTileYaw);
}

bool AGSMTile3D::AdjustMapPieceRelativeTileXYByActor(
	AGSMPiece3D* PieceActor,
	FVector2D NewRelativeTileXY,
	float NewRelativeTileYaw)
{
	if (!IsValid(PieceActor))
	{
		RemoveInvalidMapPieces();
		return false;
	}

	if (PieceActor->GetOwningGridMapTile() != this && !MapPieceActors.Contains(PieceActor))
	{
		return false;
	}

	const FName PieceId = PieceActor->GetPieceId();
	if (!PieceId.IsNone())
	{
		return SetMapPieceRelativeTileXY(PieceId, NewRelativeTileXY, NewRelativeTileYaw);
	}

	PieceActor->SetRelativeTileXY(NewRelativeTileXY);
	PieceActor->SetRelativeTileYaw(NewRelativeTileYaw);
	return RefreshMapPieceTransform(PieceActor);
}

bool AGSMTile3D::CalculateEdgeRelativeTileXYAndWorldYawTowardTile(
	FName NextTargetTileId,
	FVector2D& OutRelativeTileXY,
	float& OutWorldYaw,
	float EdgeInset) const
{
	OutRelativeTileXY = FVector2D::ZeroVector;
	OutWorldYaw = 0.0f;
	if (NextTargetTileId.IsNone())
	{
		return false;
	}

	const AGSMMap3D* GridMap = GetOwningGridMap();
	AGSMTile3D* TargetTile = GridMap ? GridMap->GetTileById(NextTargetTileId) : nullptr;
	if (!IsValid(TargetTile) || TargetTile == this)
	{
		return false;
	}

	const FVector WorldDelta = TargetTile->GetActorLocation() - GetActorLocation();
	if (WorldDelta.IsNearlyZero())
	{
		return false;
	}

	const FVector LocalDelta = GetActorTransform().InverseTransformVectorNoScale(WorldDelta);
	FVector2D LocalDirection(LocalDelta.X, LocalDelta.Y);
	if (!LocalDirection.Normalize())
	{
		return false;
	}

	const float SafeMapScale = FMath::Max(RuntimeMapScale, KINDA_SMALL_NUMBER);
	const float UnscaledTileSize = RuntimeSquareTileSize / SafeMapScale;
	const float BoundaryHalfExtent = FMath::Max(
		0.0f,
		UnscaledTileSize * 0.5f - FMath::Max(0.0f, EdgeInset));
	const float DominantDirectionComponent = FMath::Max(
		FMath::Abs(LocalDirection.X),
		FMath::Abs(LocalDirection.Y));
	const float BoundaryDistance = DominantDirectionComponent > KINDA_SMALL_NUMBER
		? BoundaryHalfExtent / DominantDirectionComponent
		: 0.0f;
	OutRelativeTileXY = LocalDirection * BoundaryDistance;
	OutWorldYaw = WorldDelta.Rotation().Yaw;
	return true;
}

bool AGSMTile3D::CalculateEdgeRelativeTileXYTowardTile(
	FName NextTargetTileId,
	FVector2D& OutRelativeTileXY,
	float EdgeInset) const
{
	float UnusedWorldYaw = 0.0f;
	return CalculateEdgeRelativeTileXYAndWorldYawTowardTile(NextTargetTileId, OutRelativeTileXY, UnusedWorldYaw, EdgeInset);
}

bool AGSMTile3D::CalculateWorldYawTowardTile(FName NextTargetTileId, float& OutWorldYaw) const
{
	FVector2D UnusedRelativeTileXY = FVector2D::ZeroVector;
	OutWorldYaw = 0.0f;
	return CalculateEdgeRelativeTileXYAndWorldYawTowardTile(NextTargetTileId, UnusedRelativeTileXY, OutWorldYaw, 0.0f);
}

AGSMPiece3D* AGSMTile3D::GetMapPieceById(FName PieceId) const
{
	if (PieceId.IsNone())
	{
		return nullptr;
	}

	for (AGSMPiece3D* PieceActor : MapPieceActors)
	{
		if (IsValid(PieceActor) && PieceActor->GetPieceId() == PieceId)
		{
			return PieceActor;
		}
	}

	return nullptr;
}

AGSMPiece3D* AGSMTile3D::GetMapPieceByIdOnOwningMap(FName PieceId) const
{
	if (PieceId.IsNone())
	{
		return nullptr;
	}

	if (AGSMPiece3D* LocalPiece = GetMapPieceById(PieceId))
	{
		return LocalPiece;
	}

	if (OwningMapId.IsNone())
	{
		return nullptr;
	}

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (const UGSMMapSubsystem* MapSubsystem = GameInstance->GetSubsystem<UGSMMapSubsystem>())
			{
				FGSMRuntimeData MapData;
				if (MapSubsystem->GetMapRuntimeData(OwningMapId, MapData))
				{
					for (const FGSMLegacyPieceRecord& PieceData : MapData.Pieces)
					{
						if (PieceData.PieceId == PieceId)
						{
							if (AGSMTile3D* TileActor = MapSubsystem->GetTileByIdOnMap(OwningMapId, PieceData.TileId))
							{
								return TileActor->GetMapPieceById(PieceId);
							}
							return nullptr;
						}
					}
				}
			}
		}
	}

	return nullptr;
}

TArray<AGSMPiece3D*> AGSMTile3D::GetMapPieces() const
{
	TArray<AGSMPiece3D*> ValidPieces;
	ValidPieces.Reserve(MapPieceActors.Num());

	for (AGSMPiece3D* PieceActor : MapPieceActors)
	{
		if (IsValid(PieceActor))
		{
			ValidPieces.Add(PieceActor);
		}
	}

	return ValidPieces;
}

void AGSMTile3D::RefreshMapPieces()
{
	RemoveInvalidMapPieces();

	for (AGSMPiece3D* PieceActor : MapPieceActors)
	{
		RefreshMapPieceTransform(PieceActor);
	}
}

void AGSMTile3D::SetMaterialBoundsMask(
	const FVector& WorldCenter,
	const FVector& WorldAxisX,
	const FVector& WorldAxisY,
	const FVector2D& WorldHalfSize,
	float Feather,
	bool bEnabled
)
{
	bMaterialBoundsMaskEnabled = bEnabled;
	MaterialBoundsMaskCenter = WorldCenter;
	MaterialBoundsMaskAxisX = WorldAxisX.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
	MaterialBoundsMaskAxisY = WorldAxisY.GetSafeNormal(SMALL_NUMBER, FVector::RightVector);
	MaterialBoundsMaskHalfSize = FVector2D(FMath::Max(0.0, WorldHalfSize.X), FMath::Max(0.0, WorldHalfSize.Y));
	MaterialBoundsMaskFeather = FMath::Max(0.0f, Feather);

	ApplyMaterialBoundsMaskParameters();
}

void AGSMTile3D::SetDecalReceiverStencil(bool bEnabled, int32 StencilValue, bool bMarkTileMeshAsReceiver)
{
	bUseDecalReceiverStencil = bEnabled;
	bMarkTileMeshAsDecalReceiver = bMarkTileMeshAsReceiver;
	DecalReceiverStencilValue = FMath::Clamp(StencilValue, 0, 255);
	ApplyDecalReceiverStencilSettings();
	ApplyDecalReceiverStencilMaterialParameters();
}

bool AGSMTile3D::RequestMapTileClick(const FVector& WorldHitLocation)
{
	if (!CanReceiveMapTileClick())
	{
		return false;
	}

	if (AGSMMap3D* GridMap = GetOwningGridMap())
	{
		return GridMap->HandleTileClickRequest(this, WorldHitLocation);
	}

	if (!CanAcceptMapTileClick(WorldHitLocation))
	{
		return false;
	}

	NotifyMapTileClickAccepted(WorldHitLocation);
	return true;
}

bool AGSMTile3D::SetAsNavigationStart(
	EGSMNavigationMode NavigationMode,
	FGSMNavigationFinished NavigationFinishedEvent)
{
	AGSMMap3D* GridMap = GetOwningGridMap();
	return IsValid(GridMap)
		&& GridMap->BeginTilePathNavigationWithCompletion(this, NavigationMode, NavigationFinishedEvent);
}

bool AGSMTile3D::RequestMapTileHover()
{
	if (bHiddenByMapBounds && !bNavigationInteractionOverride)
	{
		return false;
	}

	OnMapTileHovered();

	if (AGSMMap3D* GridMap = GetOwningGridMap())
	{
		return GridMap->HandleTileHoverNavigation(this);
	}

	return false;
}

bool AGSMTile3D::RequestMapTileRightClick(const FVector& WorldHitLocation)
{
	if (!CanReceiveMapTileClick())
	{
		return false;
	}

	if (AGSMMap3D* GridMap = GetOwningGridMap())
	{
		if (!GridMap->CanAcceptTileClick(this, WorldHitLocation))
		{
			return false;
		}
	}

	if (!CanAcceptMapTileRightClick(WorldHitLocation))
	{
		return false;
	}

	NotifyMapTileRightClickAccepted(WorldHitLocation);
	return true;
}

UGSMTileContextMenu* AGSMTile3D::ShowMapTileContextMenu(const FVector& WorldHitLocation)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return nullptr;
	}

	TSubclassOf<UGSMTileContextMenu> MenuClass = TileContextMenuClass;
	if (!MenuClass)
	{
		MenuClass = UGSMSettings::GetDefaultTileContextMenuClass();
	}
	if (!MenuClass)
	{
		UE_LOG(
			LogGSMTile3D,
			Warning,
			TEXT("ShowMapTileContextMenu failed on %s: TileContextMenuClass is not configured. Create a Blueprint subclass of UGSMTileContextMenu and assign it on the tile or GridStrategyMapSystem settings."),
			*GetName()
		);
		return nullptr;
	}

	if (MenuClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(
			LogGSMTile3D,
			Warning,
			TEXT("ShowMapTileContextMenu failed on %s: %s is abstract. Use a Blueprint subclass that implements the menu UI."),
			*GetName(),
			*GetNameSafe(MenuClass.Get())
		);
		return nullptr;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	const bool bHasMousePosition = PlayerController->GetMousePosition(MouseX, MouseY);

	FGSMTileMenuContext MenuContext;
	MenuContext.PlayerController = PlayerController;
	MenuContext.TileActor = this;
	MenuContext.OwningMap = GetOwningGridMap();
	MenuContext.WorldHitLocation = WorldHitLocation;
	MenuContext.ScreenPosition = bHasMousePosition ? FVector2D(MouseX, MouseY) : FVector2D::ZeroVector;

	UGSMTileContextMenu* ContextMenu = CreateWidget<UGSMTileContextMenu>(PlayerController, MenuClass);
	if (!ContextMenu)
	{
		return nullptr;
	}

	ContextMenu->InitializeMapTileMenu(MenuContext);
	ContextMenu->SetPositionInViewport(MenuContext.ScreenPosition, true);
	ContextMenu->AddToViewport(TileContextMenuZOrder);
	return ContextMenu;
}

UGSMTileContextMenu* AGSMTile3D::ShowMapTileContextMenuWithButtons(
	const FVector& WorldHitLocation,
	const TArray<UGSMTileMenuButton*>& MenuButtons,
	bool bClearExistingButtons
)
{
	UGSMTileContextMenu* ContextMenu = ShowMapTileContextMenu(WorldHitLocation);
	if (!ContextMenu)
	{
		return nullptr;
	}

	if (bClearExistingButtons || !MenuButtons.IsEmpty())
	{
		ContextMenu->BatchAddMapTileMenuButtons(MenuButtons, bClearExistingButtons);
	}

	return ContextMenu;
}

bool AGSMTile3D::GetMouseHitOnTile(APlayerController* PlayerController, FVector& OutHitLocation) const
{
	OutHitLocation = FVector::ZeroVector;

	if (!IsValid(PlayerController))
	{
		return false;
	}

	FHitResult HitResult;
	if (!PlayerController->GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
	{
		return false;
	}

	const bool bHitThisTile = HitResult.GetActor() == this
		&& (!bUseIndependentTileCollision
			|| HitResult.GetComponent() == TileCollisionComponent);
	if (!bHitThisTile)
	{
		return false;
	}

	OutHitLocation = HitResult.ImpactPoint;
	return true;
}

void AGSMTile3D::NotifyMapTileClickAccepted(const FVector& WorldHitLocation)
{
	OnMapTileClicked(WorldHitLocation);
}

void AGSMTile3D::NotifyMapTileRightClickAccepted(const FVector& WorldHitLocation)
{
	OnMapTileRightClicked(WorldHitLocation);
}

void AGSMTile3D::SetSelectedByMap(bool bNewSelectedByMap)
{
	if (bSelectedByMap == bNewSelectedByMap)
	{
		return;
	}

	bSelectedByMap = bNewSelectedByMap;
	ApplySelectionDecalColor();
	if (bSelectedByMap)
	{
		OnMapTileSelected();
	}
	else
	{
		OnMapTileDeselected();
	}
}

void AGSMTile3D::SetHiddenByMapBounds(bool bNewHiddenByMapBounds)
{
	if (bHiddenByMapBounds == bNewHiddenByMapBounds)
	{
		return;
	}

	bHiddenByMapBounds = bNewHiddenByMapBounds;
	SetActorHiddenInGame(bHiddenByMapBounds && bSetActorHiddenWhenOutsideBounds);

	ApplyHiddenMaterialScalar();
	RefreshCollisionForHiddenState();
	for (AGSMPiece3D* PieceActor : MapPieceActors)
	{
		ApplyMapPieceHiddenState(PieceActor);
	}
	OnMapTileHiddenStateChanged(bHiddenByMapBounds);
}

void AGSMTile3D::SetNavigationInteractionOverride(bool bNewNavigationInteractionOverride)
{
	if (bNavigationInteractionOverride == bNewNavigationInteractionOverride)
	{
		return;
	}

	bNavigationInteractionOverride = bNewNavigationInteractionOverride;
	RefreshCollisionForHiddenState();
}

AGSMMap3D* AGSMTile3D::GetOwningGridMap() const
{
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UGSMMapSubsystem* MapSubsystem = GameInstance->GetSubsystem<UGSMMapSubsystem>())
			{
				if (AGSMMap3D* GridMap = MapSubsystem->GetGridStrategyMapById(OwningMapId))
				{
					return GridMap;
				}
			}
		}
	}

	return OwningMap.Get();
}

void AGSMTile3D::SetTileClickEnabled(bool bNewCanReceiveClick)
{
	bCanReceiveClick = bNewCanReceiveClick;
	RefreshCollisionForHiddenState();
}

void AGSMTile3D::SetEdgeDecalEnabled(bool bNewEdgeDecalEnabled)
{
	bUseEdgeDecal = bNewEdgeDecalEnabled;
	EdgeDecalComponent->SetVisibility(bUseEdgeDecal);
}

bool AGSMTile3D::CanReceiveMapTileClick() const
{
	return bCanReceiveClick && bRegionAllowsClick && !bHiddenByMapBounds;
}

bool AGSMTile3D::CanAcceptMapTileClick_Implementation(const FVector& WorldHitLocation) const
{
	return CanReceiveMapTileClick();
}

bool AGSMTile3D::CanAcceptMapTileRightClick_Implementation(const FVector& WorldHitLocation) const
{
	return CanReceiveMapTileClick();
}

void AGSMTile3D::OnMapTileClicked_Implementation(const FVector& WorldHitLocation)
{
	if (AGSMMap3D* GridMap = GetOwningGridMap())
	{
		GridMap->SwitchSelectedTile(this);
	}
}

void AGSMTile3D::OnMapTileHovered_Implementation()
{
}

void AGSMTile3D::OnMapTileRightClicked_Implementation(const FVector& WorldHitLocation)
{
	ShowMapTileContextMenu(WorldHitLocation);
}

void AGSMTile3D::OnMapTileSelected_Implementation()
{
}

void AGSMTile3D::OnMapTileDeselected_Implementation()
{
}

void AGSMTile3D::CacheMaterialInstances()
{
	TileMaterialInstances.Reset();
	const bool bCanCreateDynamicMaterials = CanCreateRuntimeMaterialInstances();

	const auto CacheMeshComponentMaterialInstances = [this, bCanCreateDynamicMaterials](UMeshComponent* MeshComponent)
	{
		if (!MeshComponent)
		{
			return;
		}

		const int32 MaterialCount = MeshComponent->GetNumMaterials();
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			UMaterialInterface* Material = MeshComponent->GetMaterial(MaterialIndex);
			if (!Material)
			{
				continue;
			}

			UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(Material);
			if (!DynamicMaterial && bCanCreateDynamicMaterials)
			{
				DynamicMaterial = MeshComponent->CreateDynamicMaterialInstance(MaterialIndex, Material);
			}

			if (DynamicMaterial)
			{
				TileMaterialInstances.Add(DynamicMaterial);
			}
		}
	};

	if (bUseTileStaticMeshVisual)
	{
		CacheMeshComponentMaterialInstances(TileMeshComponent);
	}
}

void AGSMTile3D::ApplyHiddenMaterialScalar()
{
	if (HiddenScalarParameterName.IsNone())
	{
		return;
	}

	const float HiddenValue = bHiddenByMapBounds ? HiddenScalarValue : VisibleScalarValue;

	for (UMaterialInstanceDynamic* DynamicMaterial : TileMaterialInstances)
	{
		if (DynamicMaterial)
		{
			DynamicMaterial->SetScalarParameterValue(HiddenScalarParameterName, HiddenValue);
		}
	}

	if (EdgeDecalMaterialInstance)
	{
		EdgeDecalMaterialInstance->SetScalarParameterValue(HiddenScalarParameterName, HiddenValue);
	}
}

void AGSMTile3D::ApplyMaterialBoundsMaskParameters()
{
	const FLinearColor CenterValue(
		MaterialBoundsMaskCenter.X,
		MaterialBoundsMaskCenter.Y,
		MaterialBoundsMaskCenter.Z,
		0.0f
	);
	const FLinearColor AxisXValue(
		MaterialBoundsMaskAxisX.X,
		MaterialBoundsMaskAxisX.Y,
		MaterialBoundsMaskAxisX.Z,
		0.0f
	);
	const FLinearColor AxisYValue(
		MaterialBoundsMaskAxisY.X,
		MaterialBoundsMaskAxisY.Y,
		MaterialBoundsMaskAxisY.Z,
		0.0f
	);
	const FLinearColor HalfSizeValue(
		MaterialBoundsMaskHalfSize.X,
		MaterialBoundsMaskHalfSize.Y,
		0.0f,
		0.0f
	);
	const float EnabledValue = bMaterialBoundsMaskEnabled ? 1.0f : 0.0f;

	const auto ApplyToMaterial = [&] (UMaterialInstanceDynamic* DynamicMaterial)
	{
		if (!DynamicMaterial)
		{
			return;
		}

		DynamicMaterial->SetScalarParameterValue(GMapBoundsMaskEnabledParameterName, EnabledValue);
		DynamicMaterial->SetVectorParameterValue(GMapBoundsCenterParameterName, CenterValue);
		DynamicMaterial->SetVectorParameterValue(GMapBoundsAxisXParameterName, AxisXValue);
		DynamicMaterial->SetVectorParameterValue(GMapBoundsAxisYParameterName, AxisYValue);
		DynamicMaterial->SetVectorParameterValue(GMapBoundsHalfSizeParameterName, HalfSizeValue);
		DynamicMaterial->SetScalarParameterValue(GMapBoundsFeatherParameterName, MaterialBoundsMaskFeather);
	};

	for (UMaterialInstanceDynamic* DynamicMaterial : TileMaterialInstances)
	{
		ApplyToMaterial(DynamicMaterial);
	}

	ApplyToMaterial(EdgeDecalMaterialInstance);
}

void AGSMTile3D::ApplyDecalReceiverStencilSettings()
{
	if (!TileMeshComponent)
	{
		return;
	}

	TileMeshComponent->SetRenderCustomDepth(bUseDecalReceiverStencil && bMarkTileMeshAsDecalReceiver);
	TileMeshComponent->SetCustomDepthStencilValue(DecalReceiverStencilValue);
}

void AGSMTile3D::ApplyDecalReceiverStencilMaterialParameters()
{
	if (!EdgeDecalMaterialInstance)
	{
		return;
	}

	EdgeDecalMaterialInstance->SetScalarParameterValue(
		GDecalReceiverStencilEnabledParameterName,
		bUseDecalReceiverStencil ? 1.0f : 0.0f
	);
	EdgeDecalMaterialInstance->SetScalarParameterValue(
		GDecalReceiverStencilValueParameterName,
		static_cast<float>(DecalReceiverStencilValue)
	);
}

void AGSMTile3D::ApplySelectionDecalColor()
{
	if (!EdgeDecalComponent)
	{
		return;
	}

	EdgeDecalComponent->DecalColor = bSelectedByMap ? SelectedEdgeDecalColor : DefaultEdgeDecalColor;
	// Shared edges must not be overwritten by neighbouring unselected decals.
	// Cache only on entry so refresh/zoom cannot accumulate the sort offset.
	if (bSelectedByMap)
	{
		if (!bSelectionDecalSortApplied)
		{
			UnselectedDecalSortOrder = EdgeDecalComponent->SortOrder;
			bSelectionDecalSortApplied = true;
		}
		EdgeDecalComponent->SetSortOrder(UnselectedDecalSortOrder + FMath::Max(0, SelectedEdgeDecalSortOrderOffset));
	}
	else if (bSelectionDecalSortApplied)
	{
		EdgeDecalComponent->SetSortOrder(UnselectedDecalSortOrder);
		bSelectionDecalSortApplied = false;
	}
	ApplyEdgeDecalComponentColor();
}

void AGSMTile3D::ApplyEdgeDecalComponentColor()
{
	if (!EdgeDecalComponent || !EdgeDecalMaterialInstance)
	{
		return;
	}

	const FLinearColor DecalColor = EdgeDecalComponent->DecalColor;
	EdgeDecalMaterialInstance->SetVectorParameterValue(TEXT("BorderColor"), DecalColor);
	EdgeDecalMaterialInstance->SetVectorParameterValue(TEXT("DecalColor"), DecalColor);
	EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("SelectedAmount"), bSelectedByMap ? 1.0f : 0.0f);
}

void AGSMTile3D::ApplyEdgeDecalRuntimeScale()
{
	const float SafeTileSize = FMath::Max(1.0f, RuntimeSquareTileSize);
	const float SafeMapScale = FMath::Max(RuntimeMapScale, KINDA_SMALL_NUMBER);
	const FVector ComponentDecalSize = EdgeDecalComponent ? EdgeDecalComponent->DecalSize : EdgeDecalSize;
	const FVector BaseDecalSize(
		FMath::Max(0.1f, ComponentDecalSize.X),
		FMath::Max(0.1f, ComponentDecalSize.Y),
		FMath::Max(0.1f, ComponentDecalSize.Z)
	);

	FVector RuntimeDecalScale = FVector::OneVector;
	if (bFitEdgeDecalToRuntimeSquareTileSize)
	{
		RuntimeDecalScale.Y = SafeTileSize / FMath::Max(BaseDecalSize.Y, KINDA_SMALL_NUMBER);
		RuntimeDecalScale.Z = SafeTileSize / FMath::Max(BaseDecalSize.Z, KINDA_SMALL_NUMBER);
		RuntimeDecalScale.X = FMath::Min(RuntimeDecalScale.Y, RuntimeDecalScale.Z);
	}
	else if (bScaleEdgeDecalWithMapScale)
	{
		RuntimeDecalScale.X = SafeMapScale;
		RuntimeDecalScale.Y = SafeMapScale;
		RuntimeDecalScale.Z = SafeMapScale;
	}

	if (EdgeDecalComponent)
	{
		EdgeDecalComponent->SetRelativeScale3D(RuntimeDecalScale);
	}

	if (bApplyMapScaleToEdgeDecalMaterial && EdgeDecalMaterialInstance)
	{
		const FVector RuntimeDecalSize = BaseDecalSize * RuntimeDecalScale;
		if (bSyncEdgeDecalSizeToBorderMaterial)
		{
			const float NormalizedBorderExtent = FMath::Clamp(EdgeDecalBorderExtentScale, 0.0f, 0.5f);
			EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("BorderExtent"), NormalizedBorderExtent);
		}
		EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("MapScale"), SafeMapScale);
		EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("DecalScale"), FMath::Max(RuntimeDecalScale.Y, RuntimeDecalScale.Z));
		EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("TileSize"), SafeTileSize);
		EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("DecalWorldSize"), FMath::Min(RuntimeDecalSize.Y, RuntimeDecalSize.Z));
		EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("DecalBaseSizeX"), BaseDecalSize.X);
		EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("DecalBaseSizeY"), BaseDecalSize.Y);
		EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("DecalBaseSizeZ"), BaseDecalSize.Z);
		EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("DecalSizeX"), RuntimeDecalSize.X);
		EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("DecalSizeY"), RuntimeDecalSize.Y);
		EdgeDecalMaterialInstance->SetScalarParameterValue(TEXT("DecalSizeZ"), RuntimeDecalSize.Z);
	}
}

void AGSMTile3D::RefreshCollisionForHiddenState()
{
	const bool bShouldDisableCollision = bHiddenByMapBounds && bDisableCollisionWhenHidden && !bNavigationInteractionOverride;
	const bool bCollisionEnabled = (CanReceiveMapTileClick() || bNavigationInteractionOverride) && !bShouldDisableCollision;

	if (TileCollisionComponent)
	{
		ConfigureMouseOnlyCollision(
			TileCollisionComponent,
			(bCollisionEnabled && bUseIndependentTileCollision) ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision
		);
	}

	if (TileMeshComponent)
	{
		const bool bEnableStaticMeshCollision = bCollisionEnabled
			&& bUseTileStaticMeshVisual
			&& !bUseIndependentTileCollision
			&& TileMeshComponent->GetStaticMesh() != nullptr;
		ConfigureMouseOnlyCollision(
			TileMeshComponent,
			bEnableStaticMeshCollision ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision
		);
	}
}

void AGSMTile3D::ApplyRuntimeSquareTileSize()
{
	const float SafeTileSize = FMath::Max(1.0f, RuntimeSquareTileSize);

	if (bUseTileStaticMeshVisual && bFitMeshToRuntimeSquareTileSize && TileMeshComponent)
	{
		if (const UStaticMesh* StaticMesh = TileMeshComponent->GetStaticMesh())
		{
			const FBoxSphereBounds MeshBounds = StaticMesh->GetBounds();
			const FVector MeshSize = MeshBounds.BoxExtent * 2.0;
			const float MaxMeshSizeXY = FMath::Max(MeshSize.X, MeshSize.Y);
			if (MaxMeshSizeXY > KINDA_SMALL_NUMBER)
			{
				const float FitScale = SafeTileSize / MaxMeshSizeXY;
				const FVector CurrentScale = TileMeshComponent->GetRelativeScale3D();
				TileMeshComponent->SetRelativeScale3D(FVector(FitScale, FitScale, CurrentScale.Z));
			}
		}
	}

	ApplyEdgeDecalRuntimeScale();

	if (TileCollisionComponent)
	{
		TileCollisionComponent->SetBoxExtent(FVector(
			GSMLayout::FixedSquareTileSize * 0.5f,
			GSMLayout::FixedSquareTileSize * 0.5f,
			FMath::Max(1.0f, TileCollisionHeight) * 0.5f));
		const float CollisionXYScale = SafeTileSize / GSMLayout::FixedSquareTileSize;
		TileCollisionComponent->SetRelativeLocation(TileCollisionRelativeLocation);
		TileCollisionComponent->SetRelativeScale3D(FVector(CollisionXYScale, CollisionXYScale, 1.0f));
	}

	RefreshCollisionForHiddenState();
}

void AGSMTile3D::ApplyMapPieceHiddenState(AGSMPiece3D* PieceActor) const
{
	if (!IsValid(PieceActor))
	{
		return;
	}

	bool bPieceInsideMapBounds = true;
	if (AGSMMap3D* GridMap = GetOwningGridMap())
	{
		bPieceInsideMapBounds = GridMap->IsWorldLocationInsideMapBounds(PieceActor->GetActorLocation());
	}

	const bool bHiddenByTileBounds = bHideMapPiecesWhenTileHidden && bHiddenByMapBounds;
	PieceActor->SetHiddenByGridMapBounds(bHiddenByTileBounds || !bPieceInsideMapBounds);
}

void AGSMTile3D::NotifyMapPieceDetachedFromTile(AGSMPiece3D* PieceActor)
{
}

void AGSMTile3D::UnregisterMapPiece(AGSMPiece3D* PieceActor)
{
	if (MapPieceActors.Remove(PieceActor) > 0)
	{
		NotifyMapPieceDetachedFromTile(PieceActor);
	}
}

void AGSMTile3D::RemoveInvalidMapPieces()
{
	for (int32 PieceIndex = MapPieceActors.Num() - 1; PieceIndex >= 0; --PieceIndex)
	{
		if (!IsValid(MapPieceActors[PieceIndex]))
		{
			MapPieceActors.RemoveAt(PieceIndex);
		}
	}
}

FName AGSMTile3D::MakeUniqueMapPieceId(TSubclassOf<AGSMPiece3D> PieceClass) const
{
	const FString ClassPrefix = PieceClass ? PieceClass->GetName() : TEXT("MapPiece");
	const FString TilePrefix = TileId.IsNone() ? GetName() : TileId.ToString();

	for (int32 CandidateIndex = 1; CandidateIndex < TNumericLimits<int32>::Max(); ++CandidateIndex)
	{
		const FName CandidateId(*FString::Printf(TEXT("%s_%s_%d"), *TilePrefix, *ClassPrefix, CandidateIndex));
		if (!DoesMapPieceIdExist(CandidateId))
		{
			return CandidateId;
		}
	}

	return MakeUniqueObjectName(GetOuter(), AGSMPiece3D::StaticClass(), FName(*ClassPrefix));
}

bool AGSMTile3D::DoesMapPieceIdExist(FName PieceId, const AGSMPiece3D* IgnoredPieceActor) const
{
	if (PieceId.IsNone())
	{
		return false;
	}

	for (AGSMPiece3D* PieceActor : MapPieceActors)
	{
		if (IsValid(PieceActor) && PieceActor != IgnoredPieceActor && PieceActor->GetPieceId() == PieceId)
		{
			return true;
		}
	}

	return false;
}

bool AGSMTile3D::ResolveMapPieceWorldLocation(
	FVector2D RelativeTileXY,
	float RelativeTileZ,
	FVector& OutWorldLocation) const
{
	const float SafeMapScale = FMath::Max(RuntimeMapScale, KINDA_SMALL_NUMBER);
	const FVector TileLocalOffset(RelativeTileXY.X * SafeMapScale, RelativeTileXY.Y * SafeMapScale, 0.0);
	FVector PieceWorldLocation = GetActorLocation() + GetActorTransform().TransformVector(TileLocalOffset);

	if (AGSMMap3D* GridMap = GetOwningGridMap())
	{
		FVector SurfaceWorldLocation = PieceWorldLocation;
		if (GridMap->GetMapTerrainSurfaceWorldLocationAtWorldLocation(PieceWorldLocation, SurfaceWorldLocation))
		{
			PieceWorldLocation.Z = SurfaceWorldLocation.Z;
		}
	}

	PieceWorldLocation.Z += RelativeTileZ * SafeMapScale;
	OutWorldLocation = PieceWorldLocation;
	return true;
}

bool AGSMTile3D::RefreshMapPieceTransform(AGSMPiece3D* PieceActor)
{
	if (!IsValid(PieceActor))
	{
		return false;
	}

	FVector PieceWorldLocation = PieceActor->GetActorLocation();
	ResolveMapPieceWorldLocation(
		PieceActor->GetRelativeTileXY(),
		PieceActor->GetRelativeTileZ(),
		PieceWorldLocation);

	const float SafeMapScale = FMath::Max(RuntimeMapScale, KINDA_SMALL_NUMBER);
	const FVector PieceWorldScale = FVector(PieceActor->GetDefaultPieceScale()) * SafeMapScale * GetActorScale3D();
	const FRotator PieceWorldRotation = GetActorRotation() + FRotator(0.0f, PieceActor->GetRelativeTileYaw(), 0.0f);
	PieceActor->ApplyTilePieceTransform(PieceWorldLocation, PieceWorldRotation, PieceWorldScale, RuntimeMapScale);
	ApplyMapPieceHiddenState(PieceActor);
	return true;
}

FVector AGSMTile3D::GetBestClickLocationFromCursor() const
{
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			FHitResult HitResult;
			if (PlayerController->GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
			{
				if (HitResult.GetActor() == this
					|| HitResult.GetComponent() == TileMeshComponent
					|| HitResult.GetComponent() == TileCollisionComponent)
				{
					return HitResult.ImpactPoint;
				}
			}
		}
	}

	return GetActorLocation();
}
