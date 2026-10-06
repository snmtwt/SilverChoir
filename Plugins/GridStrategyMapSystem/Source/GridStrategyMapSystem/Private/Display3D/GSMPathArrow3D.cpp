#include "GridStrategyMapSystem/Display3D/GSMPathArrow3D.h"

#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/StaticMesh.h"
#include "GridStrategyMapSystem/Display3D/GSMMap3D.h"
#include "GridStrategyMapSystem/Data/GSMTileData.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName PathBoundsMaskEnabledParameterName(TEXT("GSM_MapBoundsMaskEnabled"));
	const FName PathBoundsCenterParameterName(TEXT("GSM_MapBoundsCenter"));
	const FName PathBoundsAxisXParameterName(TEXT("GSM_MapBoundsAxisX"));
	const FName PathBoundsAxisYParameterName(TEXT("GSM_MapBoundsAxisY"));
	const FName PathBoundsHalfSizeParameterName(TEXT("GSM_MapBoundsHalfSize"));
	const FName PathBoundsFeatherParameterName(TEXT("GSM_MapBoundsFeather"));
}

AGSMPathArrow3D::AGSMPathArrow3D()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultBodyMesh(
		TEXT("/GridStrategyMapSystem/Meshs/GSM_PathArrow_Body_Rect_Matched_100cm.GSM_PathArrow_Body_Rect_Matched_100cm"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultStartCapMesh(
		TEXT("/GridStrategyMapSystem/Meshs/GSM_PathArrow_StartCap_Semicircle_Matched_18cm.GSM_PathArrow_StartCap_Semicircle_Matched_18cm"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultArrowHeadMesh(
		TEXT("/GridStrategyMapSystem/Meshs/GSM_PathArrow_Head_70cm.GSM_PathArrow_Head_70cm"));

	BodyMesh = DefaultBodyMesh.Object;
	StartCapMesh = DefaultStartCapMesh.Object;
	ArrowHeadMesh = DefaultArrowHeadMesh.Object;

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootSceneComponent);

	PathSplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("PathSpline"));
	PathSplineComponent->SetupAttachment(RootSceneComponent);
	PathSplineComponent->SetClosedLoop(false);
	PathSplineComponent->SetDrawDebug(false);

	StartCapMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StartCapMesh"));
	StartCapMeshComponent->SetupAttachment(RootSceneComponent);
	ConfigureMeshComponent(StartCapMeshComponent, nullptr);

	EndArrowMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EndArrowMesh"));
	EndArrowMeshComponent->SetupAttachment(RootSceneComponent);
	ConfigureMeshComponent(EndArrowMeshComponent, nullptr);

	PathRibbonMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PathRibbonMesh"));
	PathRibbonMeshComponent->SetupAttachment(RootSceneComponent);
	PathRibbonMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PathRibbonMeshComponent->SetGenerateOverlapEvents(false);
	PathRibbonMeshComponent->SetCanEverAffectNavigation(false);
	PathRibbonMeshComponent->SetReceivesDecals(false);
	PathRibbonMeshComponent->SetRenderCustomDepth(false);
	PathRibbonMeshComponent->SetCastShadow(bCastShadow);
	PathRibbonMeshComponent->SetVisibility(false);
	PathRibbonMeshComponent->SetHiddenInGame(true);
}

void AGSMPathArrow3D::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ConfigureMeshComponent(StartCapMeshComponent, StartCapMesh);
	ConfigureMeshComponent(EndArrowMeshComponent, ArrowHeadMesh);
	DisableDecalReceivingOnComponents();

	if (CachedWorldLocations.Num() >= 2)
	{
		RebuildPathFromWorldLocations(CachedWorldLocations);
	}
}

bool AGSMPathArrow3D::ShowPathFromResult(const FGSMPathResult& PathResult)
{
	CachedPathTiles.Reset();
	for (UGSMTileData* PathTileData : PathResult.PathTiles)
	{
		AGSMTile3D* PathTile = PathTileData ? PathTileData->GetTile3D() : nullptr;
		if (IsValid(PathTile))
		{
			CachedPathTiles.Add(PathTile);
		}
	}

	if (!CachedPathTiles.IsEmpty())
	{
		return RefreshPathFromCachedTiles();
	}

	return ShowPathFromWorldLocations(PathResult.PathWorldLocations);
}

bool AGSMPathArrow3D::ShowPathFromWorldLocations(const TArray<FVector>& WorldLocations)
{
	CachedPathTiles.Reset();
	CachedWorldLocations = WorldLocations;
	return RebuildPathFromWorldLocations(CachedWorldLocations);
}

bool AGSMPathArrow3D::ShowPathFromTiles(const TArray<AGSMTile3D*>& PathTiles)
{
	CachedPathTiles.Reset();
	for (AGSMTile3D* PathTile : PathTiles)
	{
		if (IsValid(PathTile))
		{
			CachedPathTiles.Add(PathTile);
		}
	}

	return RefreshPathFromCachedTiles();
}

bool AGSMPathArrow3D::RefreshPathFromCachedTiles()
{
	TArray<FVector> WorldLocations;
	for (const TWeakObjectPtr<AGSMTile3D>& PathTilePtr : CachedPathTiles)
	{
		if (AGSMTile3D* PathTile = PathTilePtr.Get())
		{
			if (AGSMMap3D* OwningMap = PathTile->GetOwningGridMap())
			{
				WorldLocations.Add(OwningMap->GetNavigationPathPointWorldLocation(PathTile));
			}
			else
			{
				WorldLocations.Add(PathTile->GetActorLocation());
			}
		}
	}

	if (WorldLocations.Num() >= 2)
	{
		CachedWorldLocations = WorldLocations;
		return RebuildPathFromWorldLocations(CachedWorldLocations);
	}

	if (CachedWorldLocations.Num() >= 2)
	{
		return RebuildPathFromWorldLocations(CachedWorldLocations);
	}

	ClearPath();
	return false;
}

void AGSMPathArrow3D::ClearPath()
{
	CachedPathTiles.Reset();
	CachedWorldLocations.Reset();
	ActiveBodySplineMeshCount = 0;

	if (PathSplineComponent)
	{
		PathSplineComponent->ClearSplinePoints(false);
		PathSplineComponent->UpdateSpline();
	}

	HideBodySplineMeshes();
	HidePathRibbonMesh();

	if (StartCapMeshComponent)
	{
		StartCapMeshComponent->SetVisibility(false);
		StartCapMeshComponent->SetHiddenInGame(true);
	}

	if (EndArrowMeshComponent)
	{
		EndArrowMeshComponent->SetVisibility(false);
		EndArrowMeshComponent->SetHiddenInGame(true);
	}
}

void AGSMPathArrow3D::SetPathVisible(bool bNewVisible)
{
	SetActorHiddenInGame(!bNewVisible);
	SetActorEnableCollision(false);

	if (StartCapMeshComponent)
	{
		StartCapMeshComponent->SetVisibility(bNewVisible && StartCapMeshComponent->GetStaticMesh() != nullptr);
		StartCapMeshComponent->SetHiddenInGame(!bNewVisible);
	}

	if (EndArrowMeshComponent)
	{
		EndArrowMeshComponent->SetVisibility(bNewVisible && EndArrowMeshComponent->GetStaticMesh() != nullptr);
		EndArrowMeshComponent->SetHiddenInGame(!bNewVisible);
	}

	for (int32 BodyIndex = 0; BodyIndex < BodySplineMeshComponents.Num(); ++BodyIndex)
	{
		USplineMeshComponent* BodyComponent = BodySplineMeshComponents[BodyIndex];
		if (BodyComponent)
		{
			const bool bBodyVisible = bNewVisible && BodyIndex < ActiveBodySplineMeshCount;
			BodyComponent->SetVisibility(bBodyVisible);
			BodyComponent->SetHiddenInGame(!bBodyVisible);
		}
	}

	if (PathRibbonMeshComponent)
	{
		const bool bRibbonVisible = bNewVisible && bPathRibbonMeshActive;
		PathRibbonMeshComponent->SetVisibility(bRibbonVisible);
		PathRibbonMeshComponent->SetHiddenInGame(!bRibbonVisible);
	}
}

void AGSMPathArrow3D::SetMaterialBoundsMask(
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

bool AGSMPathArrow3D::RebuildPathFromWorldLocations(const TArray<FVector>& WorldLocations)
{
	if (!PathSplineComponent || WorldLocations.Num() < 2)
	{
		ClearPath();
		return false;
	}

	TArray<FVector> LocalPoints;
	LocalPoints.Reserve(WorldLocations.Num());

	const FTransform ActorTransform = GetActorTransform();
	for (const FVector& WorldLocation : WorldLocations)
	{
		FVector LocalLocation = ActorTransform.InverseTransformPosition(WorldLocation);
		LocalLocation.Z += PathHeightOffset;

		if (LocalPoints.IsEmpty() || !LocalPoints.Last().Equals(LocalLocation, KINDA_SMALL_NUMBER))
		{
			LocalPoints.Add(LocalLocation);
		}
	}

	if (LocalPoints.Num() < 2)
	{
		ClearPath();
		return false;
	}

	TArray<FVector> TrimmedLocalPoints;
	FVector StartDirection = FVector::ForwardVector;
	FVector EndDirection = FVector::ForwardVector;
	if (!BuildTrimmedLocalPath(LocalPoints, TrimmedLocalPoints, StartDirection, EndDirection))
	{
		ClearPath();
		return false;
	}

	RebuildVisualComponents(TrimmedLocalPoints, StartDirection, EndDirection);
	SetPathVisible(true);
	return true;
}

bool AGSMPathArrow3D::BuildTrimmedLocalPath(
	const TArray<FVector>& LocalPoints,
	TArray<FVector>& OutTrimmedPoints,
	FVector& OutStartDirection,
	FVector& OutEndDirection
) const
{
	OutTrimmedPoints.Reset();

	const float TotalLength = CalculatePolylineLength(LocalPoints);
	if (TotalLength <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float SafeMinBodyLength = FMath::Max(1.0f, MinBodyLength);
	const float PathLengthScale = GetPathLengthScale();
	float StartDistance = FMath::Max(0.0f, StartBodyOffsetFromPathStart * PathLengthScale);
	float EndTrimDistance = FMath::Max(0.0f, EndBodyOffsetFromPathEnd * PathLengthScale);

	if (StartDistance + EndTrimDistance + SafeMinBodyLength > TotalLength)
	{
		const float AvailableTrimLength = FMath::Max(0.0f, TotalLength - SafeMinBodyLength);
		const float RequestedTrimLength = FMath::Max(KINDA_SMALL_NUMBER, StartDistance + EndTrimDistance);
		StartDistance = AvailableTrimLength * (StartDistance / RequestedTrimLength);
		EndTrimDistance = AvailableTrimLength * (EndTrimDistance / RequestedTrimLength);
	}

	const float EndDistance = FMath::Max(StartDistance + SafeMinBodyLength, TotalLength - EndTrimDistance);
	if (EndDistance <= StartDistance)
	{
		return false;
	}

	OutStartDirection = GetDirectionAtPolylineDistance(LocalPoints, StartDistance).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	OutEndDirection = GetDirectionAtPolylineDistance(LocalPoints, EndDistance).GetSafeNormal(UE_SMALL_NUMBER, OutStartDirection);

	OutTrimmedPoints.Add(GetPointAtPolylineDistance(LocalPoints, StartDistance));

	float RunningDistance = 0.0f;
	for (int32 PointIndex = 1; PointIndex < LocalPoints.Num() - 1; ++PointIndex)
	{
		RunningDistance += FVector::Dist(LocalPoints[PointIndex - 1], LocalPoints[PointIndex]);
		if (RunningDistance > StartDistance + KINDA_SMALL_NUMBER && RunningDistance < EndDistance - KINDA_SMALL_NUMBER)
		{
			OutTrimmedPoints.Add(LocalPoints[PointIndex]);
		}
	}

	OutTrimmedPoints.Add(GetPointAtPolylineDistance(LocalPoints, EndDistance));
	if (OutTrimmedPoints.Num() < 2)
	{
		return false;
	}

	TArray<FVector> RoundedLocalPoints;
	if (BuildRoundedLocalPath(OutTrimmedPoints, RoundedLocalPoints))
	{
		OutTrimmedPoints = MoveTemp(RoundedLocalPoints);
	}

	if (OutTrimmedPoints.Num() >= 2)
	{
		OutStartDirection = (OutTrimmedPoints[1] - OutTrimmedPoints[0]).GetSafeNormal(UE_SMALL_NUMBER, OutStartDirection);
		OutEndDirection = (OutTrimmedPoints.Last() - OutTrimmedPoints[OutTrimmedPoints.Num() - 2]).GetSafeNormal(UE_SMALL_NUMBER, OutEndDirection);
	}

	return OutTrimmedPoints.Num() >= 2;
}

bool AGSMPathArrow3D::BuildRoundedLocalPath(const TArray<FVector>& InPoints, TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();

	if (!bRoundPathCorners || PathCornerRadius <= KINDA_SMALL_NUMBER || InPoints.Num() < 3)
	{
		return false;
	}

	const int32 SafeCornerSegments = FMath::Clamp(PathCornerSegments, 1, 16);
	OutPoints.Reserve(InPoints.Num() + (InPoints.Num() - 2) * SafeCornerSegments);

	const auto AddPointIfDistinct = [&OutPoints](const FVector& Point)
	{
		if (OutPoints.IsEmpty() || !OutPoints.Last().Equals(Point, KINDA_SMALL_NUMBER))
		{
			OutPoints.Add(Point);
		}
	};

	AddPointIfDistinct(InPoints[0]);

	for (int32 PointIndex = 1; PointIndex < InPoints.Num() - 1; ++PointIndex)
	{
		const FVector& PreviousPoint = InPoints[PointIndex - 1];
		const FVector& CornerPoint = InPoints[PointIndex];
		const FVector& NextPoint = InPoints[PointIndex + 1];

		const FVector IncomingVector = CornerPoint - PreviousPoint;
		const FVector OutgoingVector = NextPoint - CornerPoint;
		const float IncomingLength = IncomingVector.Length();
		const float OutgoingLength = OutgoingVector.Length();
		if (IncomingLength <= KINDA_SMALL_NUMBER || OutgoingLength <= KINDA_SMALL_NUMBER)
		{
			AddPointIfDistinct(CornerPoint);
			continue;
		}

		const FVector IncomingDirection = IncomingVector / IncomingLength;
		const FVector OutgoingDirection = OutgoingVector / OutgoingLength;
		const float TurnDot = FVector::DotProduct(IncomingDirection, OutgoingDirection);
		if (FMath::Abs(TurnDot) > 0.999f)
		{
			AddPointIfDistinct(CornerPoint);
			continue;
		}

		const float CornerTrimDistance = FMath::Min3(
			FMath::Max(0.0f, PathCornerRadius),
			IncomingLength * 0.45f,
			OutgoingLength * 0.45f
		);
		if (CornerTrimDistance <= KINDA_SMALL_NUMBER)
		{
			AddPointIfDistinct(CornerPoint);
			continue;
		}

		const FVector ArcStart = CornerPoint - IncomingDirection * CornerTrimDistance;
		const FVector ArcEnd = CornerPoint + OutgoingDirection * CornerTrimDistance;
		AddPointIfDistinct(ArcStart);

		for (int32 SegmentIndex = 1; SegmentIndex <= SafeCornerSegments; ++SegmentIndex)
		{
			const float Alpha = static_cast<float>(SegmentIndex) / static_cast<float>(SafeCornerSegments);
			const float OneMinusAlpha = 1.0f - Alpha;
			const FVector ArcPoint =
				ArcStart * (OneMinusAlpha * OneMinusAlpha) +
				CornerPoint * (2.0f * OneMinusAlpha * Alpha) +
				ArcEnd * (Alpha * Alpha);
			AddPointIfDistinct(ArcPoint);
		}
	}

	AddPointIfDistinct(InPoints.Last());
	return OutPoints.Num() >= 2;
}

void AGSMPathArrow3D::RebuildVisualComponents(
	const TArray<FVector>& TrimmedLocalPoints,
	const FVector& StartDirection,
	const FVector& EndDirection
)
{
	PathSplineComponent->ClearSplinePoints(false);
	for (const FVector& LocalPoint : TrimmedLocalPoints)
	{
		PathSplineComponent->AddSplinePoint(LocalPoint, ESplineCoordinateSpace::Local, false);
	}

	const bool bUseRoundedCornerSpline = bRoundPathCorners && PathCornerRadius > KINDA_SMALL_NUMBER;
	const bool bUseLinearSegments = !bUseRoundedCornerSpline && (bForceLinearSegmentsForMatchedMeshes || !bUseSmoothSpline);
	const ESplinePointType::Type SplinePointType = bUseLinearSegments
		? ESplinePointType::Linear
		: ESplinePointType::CurveClamped;
	for (int32 PointIndex = 0; PointIndex < PathSplineComponent->GetNumberOfSplinePoints(); ++PointIndex)
	{
		PathSplineComponent->SetSplinePointType(PointIndex, SplinePointType, false);
	}
	PathSplineComponent->UpdateSpline();

	if (BodyMesh)
	{
		const int32 SplinePointCount = PathSplineComponent->GetNumberOfSplinePoints();
		const bool bShouldOverlapBodySegments = bOverlapBodySegmentsAtJoints;
		const FVector EffectivePathMeshScale = GetEffectivePathMeshScale();
		int32 UsedBodySegmentCount = 0;
		if (bUseRoundedCornerSpline && BuildPathRibbonMesh(TrimmedLocalPoints))
		{
			HideBodySplineMeshes();
			UsedBodySegmentCount = 0;
		}
		else
		{
			HidePathRibbonMesh();
		for (int32 SegmentIndex = 0; SegmentIndex < SplinePointCount - 1; ++SegmentIndex)
		{
			const float StartDistance = PathSplineComponent->GetDistanceAlongSplineAtSplinePoint(SegmentIndex);
			const float EndDistance = PathSplineComponent->GetDistanceAlongSplineAtSplinePoint(SegmentIndex + 1);
			if (EndDistance - StartDistance <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			USplineMeshComponent* BodyComponent = GetOrCreateBodySplineMeshComponent(UsedBodySegmentCount);
			if (!BodyComponent)
			{
				continue;
			}

			BodyComponent->SetMobility(EComponentMobility::Movable);
			BodyComponent->SetStaticMesh(BodyMesh);
			BodyComponent->SetForwardAxis(SplineForwardAxis, false);
			BodyComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			BodyComponent->SetGenerateOverlapEvents(false);
			BodyComponent->SetCanEverAffectNavigation(false);
			BodyComponent->SetReceivesDecals(false);
			BodyComponent->SetRenderCustomDepth(false);
			BodyComponent->SetCastShadow(bCastShadow);
			BodyComponent->SetRelativeScale3D(FVector::OneVector);
			const FVector2D CrossScale =
				SplineForwardAxis == ESplineMeshAxis::X ? FVector2D(EffectivePathMeshScale.Y, EffectivePathMeshScale.Z) :
				SplineForwardAxis == ESplineMeshAxis::Y ? FVector2D(EffectivePathMeshScale.X, EffectivePathMeshScale.Z) :
				FVector2D(EffectivePathMeshScale.X, EffectivePathMeshScale.Y);
			BodyComponent->SetStartScale(CrossScale, false);
			BodyComponent->SetEndScale(CrossScale, false);
			ApplyMaterialOverride(BodyComponent);
			ApplyMaterialBoundsMaskParameters();

			FVector StartPosition = PathSplineComponent->GetLocationAtSplinePoint(SegmentIndex, ESplineCoordinateSpace::Local);
			FVector EndPosition = PathSplineComponent->GetLocationAtSplinePoint(SegmentIndex + 1, ESplineCoordinateSpace::Local);
			const FVector OriginalSegmentVector = EndPosition - StartPosition;
			const float OriginalSegmentLength = OriginalSegmentVector.Length();
			const FVector SegmentDirection = OriginalSegmentLength > KINDA_SMALL_NUMBER
				? OriginalSegmentVector / OriginalSegmentLength
				: FVector::ForwardVector;
			if (bShouldOverlapBodySegments && BodySegmentJointOverlap > 0.0f && SplinePointCount > 2)
			{
				const float RequestedOverlap = bUseRoundedCornerSpline
					? BodySegmentJointOverlap * 0.35f
					: BodySegmentJointOverlap;
				const float MaxOverlapRatio = bUseRoundedCornerSpline ? 0.2f : 0.45f;
				const float SafeOverlap = FMath::Min(RequestedOverlap, OriginalSegmentLength * MaxOverlapRatio);
				if (SegmentIndex > 0)
				{
					StartPosition -= SegmentDirection * SafeOverlap;
				}
				if (SegmentIndex < SplinePointCount - 2)
				{
					EndPosition += SegmentDirection * SafeOverlap;
				}
			}

			const FVector AdjustedSegmentVector = EndPosition - StartPosition;
			const float AdjustedSegmentLength = AdjustedSegmentVector.Length();
			const FVector AdjustedSegmentDirection = AdjustedSegmentLength > KINDA_SMALL_NUMBER
				? AdjustedSegmentVector / AdjustedSegmentLength
				: SegmentDirection;
			const FVector StartTangent = bUseLinearSegments
				? AdjustedSegmentDirection * AdjustedSegmentLength * SplineTangentScale
				: PathSplineComponent->GetTangentAtSplinePoint(SegmentIndex, ESplineCoordinateSpace::Local) * SplineTangentScale;
			const FVector EndTangent = bUseLinearSegments
				? AdjustedSegmentDirection * AdjustedSegmentLength * SplineTangentScale
				: PathSplineComponent->GetTangentAtSplinePoint(SegmentIndex + 1, ESplineCoordinateSpace::Local) * SplineTangentScale;
			BodyComponent->SetStartAndEnd(StartPosition, StartTangent, EndPosition, EndTangent, false);
			BodyComponent->SetSplineUpDir(FVector::UpVector, false);
			BodyComponent->SetVisibility(true);
			BodyComponent->SetHiddenInGame(false);
			BodyComponent->UpdateMesh();

			++UsedBodySegmentCount;
		}
		}

		HideUnusedBodySplineMeshes(UsedBodySegmentCount);
		ActiveBodySplineMeshCount = UsedBodySegmentCount;
	}
	else
	{
		ActiveBodySplineMeshCount = 0;
		HideBodySplineMeshes();
		if (bUseRoundedCornerSpline)
		{
			BuildPathRibbonMesh(TrimmedLocalPoints);
		}
		else
		{
			HidePathRibbonMesh();
		}
	}

	const FVector BodyStart = TrimmedLocalPoints[0];
	const FVector BodyEnd = TrimmedLocalPoints.Last();
	const FVector SafeStartDirection = StartDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector SafeEndDirection = EndDirection.GetSafeNormal(UE_SMALL_NUMBER, SafeStartDirection);

	ConfigureMeshComponent(StartCapMeshComponent, StartCapMesh);
	if (StartCapMeshComponent && StartCapMesh)
	{
		const FVector EffectivePathMeshScale = GetEffectivePathMeshScale();
		const FRotator StartRotation = FRotationMatrix::MakeFromXZ(SafeStartDirection, FVector::UpVector).Rotator();
		const FVector StartCapOrigin = BodyStart - StartRotation.RotateVector(GetStartCapJoinLocalOffset() * EffectivePathMeshScale);
		StartCapMeshComponent->SetRelativeLocation(StartCapOrigin);
		StartCapMeshComponent->SetRelativeRotation(StartRotation);
		StartCapMeshComponent->SetRelativeScale3D(EffectivePathMeshScale);
		StartCapMeshComponent->SetVisibility(true);
		StartCapMeshComponent->SetHiddenInGame(false);
	}

	ConfigureMeshComponent(EndArrowMeshComponent, ArrowHeadMesh);
	if (EndArrowMeshComponent && ArrowHeadMesh)
	{
		const FVector EffectivePathMeshScale = GetEffectivePathMeshScale();
		const FRotator EndRotation = FRotationMatrix::MakeFromXZ(SafeEndDirection, FVector::UpVector).Rotator();
		const FVector EndArrowOrigin = BodyEnd - EndRotation.RotateVector(GetArrowHeadTailLocalOffset() * EffectivePathMeshScale);
		EndArrowMeshComponent->SetRelativeLocation(EndArrowOrigin);
		EndArrowMeshComponent->SetRelativeRotation(EndRotation);
		EndArrowMeshComponent->SetRelativeScale3D(EffectivePathMeshScale);
		EndArrowMeshComponent->SetVisibility(true);
		EndArrowMeshComponent->SetHiddenInGame(false);
	}
}

bool AGSMPathArrow3D::BuildPathRibbonMesh(const TArray<FVector>& CenterlinePoints)
{
	if (!PathRibbonMeshComponent || CenterlinePoints.Num() < 2)
	{
		HidePathRibbonMesh();
		return false;
	}

	float BodyWidth = 28.0f;
	float BodyThickness = 6.0f;
	float RibbonTopOffset = PathRibbonExtraHeightOffset;
	float RibbonBottomOffset = PathRibbonExtraHeightOffset - BodyThickness;
	const FVector EffectivePathMeshScale = GetEffectivePathMeshScale();
	if (BodyMesh)
	{
		const FBox MeshBounds = BodyMesh->GetBoundingBox();
		if (MeshBounds.IsValid)
		{
			const float HeightScale = FMath::Max(EffectivePathMeshScale.Z, KINDA_SMALL_NUMBER);
			if (SplineForwardAxis == ESplineMeshAxis::X)
			{
				BodyWidth = MeshBounds.GetSize().Y;
			}
			else if (SplineForwardAxis == ESplineMeshAxis::Y)
			{
				BodyWidth = MeshBounds.GetSize().X;
			}
			else
			{
				BodyWidth = MeshBounds.GetSize().Y;
			}
			BodyThickness = MeshBounds.GetSize().Z * HeightScale;
			RibbonTopOffset = MeshBounds.Max.Z * HeightScale + PathRibbonExtraHeightOffset;
			RibbonBottomOffset = MeshBounds.Min.Z * HeightScale + PathRibbonExtraHeightOffset;
		}
	}
	if (PathRibbonThickness > KINDA_SMALL_NUMBER)
	{
		BodyThickness = PathRibbonThickness;
		RibbonBottomOffset = RibbonTopOffset - BodyThickness;
	}
	BodyThickness = FMath::Max(1.0f, BodyThickness);

	const float WidthScale =
		SplineForwardAxis == ESplineMeshAxis::X ? EffectivePathMeshScale.Y :
		SplineForwardAxis == ESplineMeshAxis::Y ? EffectivePathMeshScale.X :
		EffectivePathMeshScale.Y;
	const float RibbonWidthScale = FMath::Max(0.1f, PathRibbonWidthScale);
	const float HalfWidth = FMath::Max(1.0f, BodyWidth * FMath::Max(WidthScale, KINDA_SMALL_NUMBER) * RibbonWidthScale * 0.5f);

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	const int32 PointCount = CenterlinePoints.Num();
	Vertices.Reserve(PointCount * 4);
	Normals.Reserve(PointCount * 4);
	UVs.Reserve(PointCount * 4);
	VertexColors.Reserve(PointCount * 4);
	Tangents.Reserve(PointCount * 4);
	Triangles.Reserve((PointCount - 1) * 48);

	float RunningDistance = 0.0f;
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		if (PointIndex > 0)
		{
			RunningDistance += FVector::Dist(CenterlinePoints[PointIndex - 1], CenterlinePoints[PointIndex]);
		}

		FVector TangentDirection = FVector::ForwardVector;
		if (PointIndex == 0)
		{
			TangentDirection = CenterlinePoints[1] - CenterlinePoints[0];
		}
		else if (PointIndex == PointCount - 1)
		{
			TangentDirection = CenterlinePoints[PointIndex] - CenterlinePoints[PointIndex - 1];
		}
		else
		{
			const FVector PreviousDirection = (CenterlinePoints[PointIndex] - CenterlinePoints[PointIndex - 1]).GetSafeNormal();
			const FVector NextDirection = (CenterlinePoints[PointIndex + 1] - CenterlinePoints[PointIndex]).GetSafeNormal();
			TangentDirection = PreviousDirection + NextDirection;
			if (TangentDirection.IsNearlyZero())
			{
				TangentDirection = NextDirection;
			}
		}
		TangentDirection = TangentDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);

		FVector SideDirection = FVector::CrossProduct(FVector::UpVector, TangentDirection).GetSafeNormal();
		if (SideDirection.IsNearlyZero())
		{
			SideDirection = FVector::RightVector;
		}

		const FVector TopHeightOffset(0.0, 0.0, RibbonTopOffset);
		const FVector BottomHeightOffset(0.0, 0.0, RibbonBottomOffset);
		const FVector TopLeftPoint = CenterlinePoints[PointIndex] + SideDirection * HalfWidth + TopHeightOffset;
		const FVector TopRightPoint = CenterlinePoints[PointIndex] - SideDirection * HalfWidth + TopHeightOffset;
		const FVector BottomLeftPoint = CenterlinePoints[PointIndex] + SideDirection * HalfWidth + BottomHeightOffset;
		const FVector BottomRightPoint = CenterlinePoints[PointIndex] - SideDirection * HalfWidth + BottomHeightOffset;
		const float V = RunningDistance / 100.0f;

		Vertices.Add(TopLeftPoint);
		Vertices.Add(TopRightPoint);
		Vertices.Add(BottomLeftPoint);
		Vertices.Add(BottomRightPoint);
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::DownVector);
		Normals.Add(FVector::DownVector);
		UVs.Add(FVector2D(0.0f, V));
		UVs.Add(FVector2D(1.0f, V));
		UVs.Add(FVector2D(0.0f, V));
		UVs.Add(FVector2D(1.0f, V));
		VertexColors.Add(FLinearColor::White);
		VertexColors.Add(FLinearColor::White);
		VertexColors.Add(FLinearColor::White);
		VertexColors.Add(FLinearColor::White);
		Tangents.Add(FProcMeshTangent(TangentDirection, false));
		Tangents.Add(FProcMeshTangent(TangentDirection, false));
		Tangents.Add(FProcMeshTangent(TangentDirection, false));
		Tangents.Add(FProcMeshTangent(TangentDirection, false));
	}

	const auto AddDoubleSidedQuad = [&Triangles](int32 A, int32 B, int32 C, int32 D)
	{
		Triangles.Add(A);
		Triangles.Add(B);
		Triangles.Add(C);
		Triangles.Add(B);
		Triangles.Add(D);
		Triangles.Add(C);

		Triangles.Add(A);
		Triangles.Add(C);
		Triangles.Add(B);
		Triangles.Add(B);
		Triangles.Add(C);
		Triangles.Add(D);
	};

	for (int32 SegmentIndex = 0; SegmentIndex < PointCount - 1; ++SegmentIndex)
	{
		const int32 BaseIndex = SegmentIndex * 4;
		const int32 NextBaseIndex = BaseIndex + 4;

		AddDoubleSidedQuad(BaseIndex, BaseIndex + 1, NextBaseIndex, NextBaseIndex + 1);
		AddDoubleSidedQuad(BaseIndex + 2, NextBaseIndex + 2, BaseIndex + 3, NextBaseIndex + 3);
		AddDoubleSidedQuad(BaseIndex, NextBaseIndex, BaseIndex + 2, NextBaseIndex + 2);
		AddDoubleSidedQuad(BaseIndex + 1, BaseIndex + 3, NextBaseIndex + 1, NextBaseIndex + 3);
	}

	PathRibbonMeshComponent->ClearAllMeshSections();
	PathRibbonMeshComponent->CreateMeshSection_LinearColor(
		0,
		Vertices,
		Triangles,
		Normals,
		UVs,
		VertexColors,
		Tangents,
		false
	);
	PathRibbonMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PathRibbonMeshComponent->SetGenerateOverlapEvents(false);
	PathRibbonMeshComponent->SetCanEverAffectNavigation(false);
	PathRibbonMeshComponent->SetReceivesDecals(false);
	PathRibbonMeshComponent->SetRenderCustomDepth(false);
	PathRibbonMeshComponent->SetCastShadow(bCastShadow);
	if (UMaterialInterface* PathMaterial = ResolvePathMaterial())
	{
		PathRibbonMeshComponent->SetMaterial(0, PathMaterial);
	}
	ResolveOrCreatePathMaterial(PathRibbonMeshComponent);
	ApplyMaterialBoundsMaskParameters();
	PathRibbonMeshComponent->SetVisibility(true);
	PathRibbonMeshComponent->SetHiddenInGame(false);
	bPathRibbonMeshActive = true;
	return true;
}

void AGSMPathArrow3D::HidePathRibbonMesh()
{
	bPathRibbonMeshActive = false;
	if (PathRibbonMeshComponent)
	{
		PathRibbonMeshComponent->ClearAllMeshSections();
		PathRibbonMeshComponent->SetVisibility(false);
		PathRibbonMeshComponent->SetHiddenInGame(true);
		PathRibbonMeshComponent->SetReceivesDecals(false);
		PathRibbonMeshComponent->SetRenderCustomDepth(false);
	}
}

void AGSMPathArrow3D::HideBodySplineMeshes()
{
	ActiveBodySplineMeshCount = 0;
	HideUnusedBodySplineMeshes(0);
}

void AGSMPathArrow3D::HideUnusedBodySplineMeshes(int32 FirstUnusedIndex)
{
	for (int32 BodyIndex = FMath::Max(0, FirstUnusedIndex); BodyIndex < BodySplineMeshComponents.Num(); ++BodyIndex)
	{
		if (USplineMeshComponent* BodyComponent = BodySplineMeshComponents[BodyIndex])
		{
			BodyComponent->SetVisibility(false);
			BodyComponent->SetHiddenInGame(true);
		}
	}
}

USplineMeshComponent* AGSMPathArrow3D::GetOrCreateBodySplineMeshComponent(int32 SegmentIndex)
{
	if (SegmentIndex < 0)
	{
		return nullptr;
	}

	while (BodySplineMeshComponents.Num() <= SegmentIndex)
	{
		const FName ComponentName = MakeUniqueObjectName(
			this,
			USplineMeshComponent::StaticClass(),
			TEXT("PathBodySplineMesh")
		);

		USplineMeshComponent* BodyComponent = NewObject<USplineMeshComponent>(this, ComponentName);
		if (!BodyComponent)
		{
			return nullptr;
		}

		BodyComponent->CreationMethod = EComponentCreationMethod::Instance;
		BodyComponent->SetMobility(EComponentMobility::Movable);
		BodyComponent->SetupAttachment(RootSceneComponent);
		AddInstanceComponent(BodyComponent);
		BodyComponent->RegisterComponent();
		BodySplineMeshComponents.Add(BodyComponent);
	}

	return BodySplineMeshComponents[SegmentIndex];
}

void AGSMPathArrow3D::DisableDecalReceivingOnComponents()
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
		PrimitiveComponent->SetRenderCustomDepth(false);
	}
}

void AGSMPathArrow3D::ClearBodySplineMeshes()
{
	for (USplineMeshComponent* BodyComponent : BodySplineMeshComponents)
	{
		if (BodyComponent)
		{
			BodyComponent->DestroyComponent();
		}
	}
	BodySplineMeshComponents.Reset();
}

void AGSMPathArrow3D::ConfigureMeshComponent(UStaticMeshComponent* MeshComponent, UStaticMesh* Mesh)
{
	if (!MeshComponent)
	{
		return;
	}

	MeshComponent->SetStaticMesh(Mesh);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->SetCanEverAffectNavigation(false);
	MeshComponent->SetReceivesDecals(false);
	MeshComponent->SetRenderCustomDepth(false);
	MeshComponent->SetCastShadow(bCastShadow);
	MeshComponent->SetVisibility(Mesh != nullptr);
	MeshComponent->SetHiddenInGame(Mesh == nullptr);
	ApplyMaterialOverride(MeshComponent);
}

void AGSMPathArrow3D::ApplyMaterialOverride(UStaticMeshComponent* MeshComponent)
{
	if (MeshComponent)
	{
		if (UMaterialInterface* PathMaterial = ResolvePathMaterial())
		{
			MeshComponent->SetMaterial(0, PathMaterial);
		}
		ResolveOrCreatePathMaterial(MeshComponent);
		ApplyMaterialBoundsMaskParameters();
	}
}

UMaterialInterface* AGSMPathArrow3D::ResolveOrCreatePathMaterial(UStaticMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return nullptr;
	}

	UMaterialInterface* Material = MeshComponent->GetMaterial(0);
	if (!Material)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(Material);
	if (!DynamicMaterial)
	{
		DynamicMaterial = MeshComponent->CreateDynamicMaterialInstance(0, Material);
	}

	if (DynamicMaterial)
	{
		PathMaterialInstances.AddUnique(DynamicMaterial);
		ApplyMaterialBoundsMaskToMaterial(DynamicMaterial);
		return DynamicMaterial;
	}

	return Material;
}

UMaterialInterface* AGSMPathArrow3D::ResolveOrCreatePathMaterial(UProceduralMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return nullptr;
	}

	UMaterialInterface* Material = MeshComponent->GetMaterial(0);
	if (!Material)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* DynamicMaterial = Cast<UMaterialInstanceDynamic>(Material);
	if (!DynamicMaterial)
	{
		DynamicMaterial = UMaterialInstanceDynamic::Create(Material, this);
		if (DynamicMaterial)
		{
			MeshComponent->SetMaterial(0, DynamicMaterial);
		}
	}

	if (DynamicMaterial)
	{
		PathMaterialInstances.AddUnique(DynamicMaterial);
		ApplyMaterialBoundsMaskToMaterial(DynamicMaterial);
		return DynamicMaterial;
	}

	return Material;
}

UMaterialInterface* AGSMPathArrow3D::ResolvePathMaterial() const
{
	if (MaterialOverride)
	{
		return MaterialOverride;
	}

	if (!bUseFirstMeshMaterialWhenNoOverride)
	{
		return nullptr;
	}

	const UStaticMesh* MeshesToSearch[] = {
		ArrowHeadMesh,
		StartCapMesh,
		BodyMesh
	};

	for (const UStaticMesh* Mesh : MeshesToSearch)
	{
		if (!Mesh || Mesh->GetStaticMaterials().IsEmpty())
		{
			continue;
		}

		if (UMaterialInterface* Material = Mesh->GetMaterial(0))
		{
			return Material;
		}
	}

	return nullptr;
}

void AGSMPathArrow3D::ApplyMaterialBoundsMaskParameters()
{
	for (int32 MaterialIndex = PathMaterialInstances.Num() - 1; MaterialIndex >= 0; --MaterialIndex)
	{
		UMaterialInstanceDynamic* DynamicMaterial = PathMaterialInstances[MaterialIndex];
		if (!DynamicMaterial)
		{
			PathMaterialInstances.RemoveAtSwap(MaterialIndex);
			continue;
		}

		ApplyMaterialBoundsMaskToMaterial(DynamicMaterial);
	}
}

void AGSMPathArrow3D::ApplyMaterialBoundsMaskToMaterial(UMaterialInstanceDynamic* DynamicMaterial) const
{
	if (!DynamicMaterial)
	{
		return;
	}

	DynamicMaterial->SetScalarParameterValue(PathBoundsMaskEnabledParameterName, bMaterialBoundsMaskEnabled ? 1.0f : 0.0f);
	DynamicMaterial->SetVectorParameterValue(
		PathBoundsCenterParameterName,
		FLinearColor(MaterialBoundsMaskCenter.X, MaterialBoundsMaskCenter.Y, MaterialBoundsMaskCenter.Z, 0.0f)
	);
	DynamicMaterial->SetVectorParameterValue(
		PathBoundsAxisXParameterName,
		FLinearColor(MaterialBoundsMaskAxisX.X, MaterialBoundsMaskAxisX.Y, MaterialBoundsMaskAxisX.Z, 0.0f)
	);
	DynamicMaterial->SetVectorParameterValue(
		PathBoundsAxisYParameterName,
		FLinearColor(MaterialBoundsMaskAxisY.X, MaterialBoundsMaskAxisY.Y, MaterialBoundsMaskAxisY.Z, 0.0f)
	);
	DynamicMaterial->SetVectorParameterValue(
		PathBoundsHalfSizeParameterName,
		FLinearColor(MaterialBoundsMaskHalfSize.X, MaterialBoundsMaskHalfSize.Y, 0.0f, 0.0f)
	);
	DynamicMaterial->SetScalarParameterValue(PathBoundsFeatherParameterName, MaterialBoundsMaskFeather);
}

FVector AGSMPathArrow3D::GetStartCapJoinLocalOffset() const
{
	if (bAutoAlignEndCapsToMeshBounds && StartCapMesh)
	{
		const FBox MeshBounds = StartCapMesh->GetBoundingBox();
		if (MeshBounds.IsValid)
		{
			return FVector(MeshBounds.Max.X, 0.0, 0.0) + StartCapJoinLocalOffset;
		}
	}

	return FVector(StartCapJoinLocalX, 0.0, 0.0) + StartCapJoinLocalOffset;
}

FVector AGSMPathArrow3D::GetArrowHeadTailLocalOffset() const
{
	if (bAutoAlignEndCapsToMeshBounds && ArrowHeadMesh)
	{
		const FBox MeshBounds = ArrowHeadMesh->GetBoundingBox();
		if (MeshBounds.IsValid)
		{
			return FVector(MeshBounds.Min.X, 0.0, 0.0) + ArrowHeadTailLocalOffset;
		}
	}

	return FVector(ArrowHeadTailLocalX, 0.0, 0.0) + ArrowHeadTailLocalOffset;
}

FVector AGSMPathArrow3D::GetEffectivePathMeshScale() const
{
	const float SafeVisualScale = FMath::Max(0.1f, PathVisualScale);
	return FVector(
		PathMeshScale.X * SafeVisualScale,
		PathMeshScale.Y * SafeVisualScale,
		PathMeshScale.Z * SafeVisualScale
	);
}

float AGSMPathArrow3D::GetPathLengthScale() const
{
	const FVector EffectivePathMeshScale = GetEffectivePathMeshScale();
	const float HorizontalScale = (FMath::Abs(EffectivePathMeshScale.X) + FMath::Abs(EffectivePathMeshScale.Y)) * 0.5f;
	return FMath::Max(0.1f, HorizontalScale);
}

float AGSMPathArrow3D::CalculatePolylineLength(const TArray<FVector>& Points)
{
	float TotalLength = 0.0f;
	for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
	{
		TotalLength += FVector::Dist(Points[PointIndex - 1], Points[PointIndex]);
	}
	return TotalLength;
}

FVector AGSMPathArrow3D::GetPointAtPolylineDistance(const TArray<FVector>& Points, float Distance)
{
	if (Points.IsEmpty())
	{
		return FVector::ZeroVector;
	}

	if (Points.Num() == 1 || Distance <= 0.0f)
	{
		return Points[0];
	}

	float RunningDistance = 0.0f;
	for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
	{
		const FVector& PreviousPoint = Points[PointIndex - 1];
		const FVector& CurrentPoint = Points[PointIndex];
		const float SegmentLength = FVector::Dist(PreviousPoint, CurrentPoint);
		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		if (RunningDistance + SegmentLength >= Distance)
		{
			const float Alpha = FMath::Clamp((Distance - RunningDistance) / SegmentLength, 0.0f, 1.0f);
			return FMath::Lerp(PreviousPoint, CurrentPoint, Alpha);
		}

		RunningDistance += SegmentLength;
	}

	return Points.Last();
}

FVector AGSMPathArrow3D::GetDirectionAtPolylineDistance(const TArray<FVector>& Points, float Distance)
{
	if (Points.Num() < 2)
	{
		return FVector::ForwardVector;
	}

	float RunningDistance = 0.0f;
	for (int32 PointIndex = 1; PointIndex < Points.Num(); ++PointIndex)
	{
		const FVector Segment = Points[PointIndex] - Points[PointIndex - 1];
		const float SegmentLength = Segment.Length();
		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		if (RunningDistance + SegmentLength >= Distance)
		{
			return Segment / SegmentLength;
		}

		RunningDistance += SegmentLength;
	}

	for (int32 PointIndex = Points.Num() - 1; PointIndex > 0; --PointIndex)
	{
		const FVector Segment = Points[PointIndex] - Points[PointIndex - 1];
		if (!Segment.IsNearlyZero())
		{
			return Segment.GetSafeNormal();
		}
	}

	return FVector::ForwardVector;
}
