#include "GridStrategyMapSystem/Display3D/GSMPiece3D.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "GridStrategyMapSystem/Data/GSMPieceData.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"

AGSMPiece3D::AGSMPiece3D()
{
	PrimaryActorTick.bCanEverTick = false;

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootSceneComponent);
}

void AGSMPiece3D::BindPieceData(UGSMPieceData* InPieceData)
{
	if (PieceData == InPieceData)
	{
		return;
	}
	if (PieceData && PieceData->GetPiece3D() == this)
	{
		PieceData->SetPiece3D(nullptr);
	}
	PieceData = InPieceData;
	if (PieceData)
	{
		PieceData->SetPiece3D(this);
		SetPieceId(FName(*PieceData->GetPieceGuid().ToString(EGuidFormats::DigitsWithHyphens)));
		const FGSMPiecePlacement Placement = PieceData->GetPlacement();
		RelativeTileXY = Placement.RelativeTileXY;
		RelativeTileZ = Placement.RelativeTileZ;
		RelativeTileYaw = Placement.RelativeTileYaw;
		DefaultPieceScale = FMath::Max(Placement.DefaultScale, KINDA_SMALL_NUMBER);
	}
	ReceivePieceDataBound(PieceData);
}

void AGSMPiece3D::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	DisableDecalReceivingOnComponents();
}

void AGSMPiece3D::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	DisableDecalReceivingOnComponents();
}

void AGSMPiece3D::OnBoundPieceDataUpdated_Implementation(UGSMPieceData* UpdatedPieceData)
{
}

void AGSMPiece3D::OnBoundPieceMovedBetweenTiles_Implementation(
	UGSMPieceData* MovedPieceData,
	UGSMTileData* PreviousTileData,
	UGSMTileData* CurrentTileData)
{
}

void AGSMPiece3D::SetPieceId(FName NewPieceId)
{
	PieceId = NewPieceId;
}

bool AGSMPiece3D::ModifyPieceId(FName NewPieceId)
{
	if (PieceId == NewPieceId)
	{
		return false;
	}

	PieceId = NewPieceId;
	return true;
}

void AGSMPiece3D::InitializeGridMapPiece(
	AGSMTile3D* InOwningTile,
	FVector2D InRelativeTileXY,
	float InRelativeTileYaw,
	float InDefaultScale)
{
	OwningTile = InOwningTile;
	RelativeTileXY = InRelativeTileXY;
	RelativeTileYaw = InRelativeTileYaw;
	DefaultPieceScale = FMath::Max(InDefaultScale, KINDA_SMALL_NUMBER);
	DisableDecalReceivingOnComponents();
}

void AGSMPiece3D::SetRelativeTileXY(FVector2D NewRelativeTileXY)
{
	RelativeTileXY = NewRelativeTileXY;
}

void AGSMPiece3D::SetRelativeTileZ(float NewRelativeTileZ)
{
	RelativeTileZ = NewRelativeTileZ;
}

void AGSMPiece3D::SetRelativeTileYaw(float NewRelativeTileYaw)
{
	RelativeTileYaw = NewRelativeTileYaw;
}

void AGSMPiece3D::SetDefaultPieceScale(float NewDefaultScale)
{
	DefaultPieceScale = FMath::Max(NewDefaultScale, KINDA_SMALL_NUMBER);
}

bool AGSMPiece3D::MoveToOwningTileEdgeTowardTile(
	FName TargetTileId,
	float EdgeInset,
	bool bFaceTargetTile)
{
	AGSMTile3D* Tile = OwningTile.Get();
	if (!IsValid(Tile))
	{
		return false;
	}

	FVector2D EdgeRelativeTileXY = FVector2D::ZeroVector;
	float TargetWorldYaw = 0.0f;
	if (!Tile->CalculateEdgeRelativeTileXYAndWorldYawTowardTile(
		TargetTileId,
		EdgeRelativeTileXY,
		TargetWorldYaw,
		FMath::Max(0.0f, EdgeInset)))
	{
		return false;
	}

	const float NewRelativeTileYaw = bFaceTargetTile
		? FRotator::NormalizeAxis(TargetWorldYaw - Tile->GetActorRotation().Yaw)
		: RelativeTileYaw;
	return Tile->AdjustMapPieceRelativeTileXYByActor(
		this,
		EdgeRelativeTileXY,
		NewRelativeTileYaw);
}

void AGSMPiece3D::ApplyTilePieceTransform(
	const FVector& WorldLocation,
	const FRotator& WorldRotation,
	const FVector& WorldScale,
	float RuntimeMapScale)
{
	SetActorLocation(WorldLocation);
	SetActorRotation(WorldRotation);
	SetActorScale3D(WorldScale);
	OnTilePieceTransformUpdated(WorldLocation, WorldRotation, WorldScale, RuntimeMapScale);
}

void AGSMPiece3D::SetHiddenByGridMapBounds(bool bNewHiddenByGridMapBounds)
{
	if (bHiddenByGridMapBounds == bNewHiddenByGridMapBounds)
	{
		return;
	}

	bHiddenByGridMapBounds = bNewHiddenByGridMapBounds;
	if (bHiddenByGridMapBounds)
	{
		bActorCollisionEnabledBeforeGridMapBoundsHidden = GetActorEnableCollision();
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
		return;
	}

	SetActorHiddenInGame(false);
	SetActorEnableCollision(bActorCollisionEnabledBeforeGridMapBoundsHidden);
}

void AGSMPiece3D::DisableDecalReceivingOnComponents()
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!PrimitiveComponent)
		{
			continue;
		}

		PrimitiveComponent->SetReceivesDecals(false);
	}
}

void AGSMPiece3D::OnTilePieceTransformUpdated_Implementation(
	const FVector& WorldLocation,
	const FRotator& WorldRotation,
	const FVector& WorldScale,
	float RuntimeMapScale)
{
}
