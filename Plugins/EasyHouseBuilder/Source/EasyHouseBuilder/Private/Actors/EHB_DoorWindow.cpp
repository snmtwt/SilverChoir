// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/EHB_DoorWindow.h"

#include "Actors/EHB_Wall.h"
#include "Components/EHBDoorWindowOpeningSplineComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	constexpr float EHBDefaultWindowOpeningSize = 120.0f;
	constexpr float EHBDefaultWindowSillHeight = 90.0f;
	constexpr float EHBDefaultDoorOpeningWidth = 90.0f;
	constexpr float EHBDefaultDoorOpeningHeight = 210.0f;
	constexpr float EHBDefaultOpeningThickness = 10.0f;

	bool DoesDoorWindowWidthUseSourceY(const FBox& MeshBounds)
	{
		const FVector MeshSize = MeshBounds.GetSize();
		return FMath::Abs(MeshSize.Y) > FMath::Abs(MeshSize.X);
	}

	FRotator MakeDoorWindowMeshRotation(const FBox& MeshBounds, bool bAutoOrientMesh)
	{
		if (!bAutoOrientMesh)
		{
			return FRotator::ZeroRotator;
		}

		return DoesDoorWindowWidthUseSourceY(MeshBounds)
			? FRotator(0.0f, -90.0f, 0.0f)
			: FRotator::ZeroRotator;
	}

	void SetQuadrilateralOpeningSpline(
		UEHBDoorWindowOpeningSplineComponent* OpeningSpline,
		float Width,
		float Height,
		bool bModifyForTransaction)
	{
		if (!OpeningSpline)
		{
			return;
		}

		const float HalfWidth = FMath::Max(1.0f, Width) * 0.5f;
		const float SafeHeight = FMath::Max(1.0f, Height);

#if WITH_EDITOR
		if (bModifyForTransaction)
		{
			OpeningSpline->Modify();
		}
#endif

		OpeningSpline->ClearSplinePoints(false);
		OpeningSpline->AddSplinePoint(FVector(-HalfWidth, 0.0f, 0.0f), ESplineCoordinateSpace::Local, false);
		OpeningSpline->AddSplinePoint(FVector(-HalfWidth, 0.0f, SafeHeight), ESplineCoordinateSpace::Local, false);
		OpeningSpline->AddSplinePoint(FVector(HalfWidth, 0.0f, SafeHeight), ESplineCoordinateSpace::Local, false);
		OpeningSpline->AddSplinePoint(FVector(HalfWidth, 0.0f, 0.0f), ESplineCoordinateSpace::Local, false);
		OpeningSpline->ConstrainToOpeningPlane();
		OpeningSpline->UpdateSpline();
	}
}

AEHB_DoorWindow::AEHB_DoorWindow()
{
	ElementType = EEHBBuildingElementType::DoorWindow;
	ElementCapabilities = static_cast<int32>(EEHBElementCapability::HostedElement);
	SemanticTags.AddUnique(TEXT("Opening.Hosted"));

	DoorWindowMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorWindowMesh"));
	DoorWindowMeshComponent->SetupAttachment(SceneRoot);
	DoorWindowMeshComponent->SetMobility(EComponentMobility::Movable);
	DoorWindowMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	DoorWindowMeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
	DoorWindowMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	DoorWindowMeshComponent->ComponentTags.AddUnique(TEXT("EHB_DoorWindow"));

	OpeningSplineComponent = CreateDefaultSubobject<UEHBDoorWindowOpeningSplineComponent>(TEXT("OpeningOutlineSpline"));
	OpeningSplineComponent->SetupAttachment(SceneRoot);
	SetQuadrilateralOpeningSpline(
		OpeningSplineComponent,
		EHBDefaultWindowOpeningSize,
		EHBDefaultWindowOpeningSize,
		false);

	InvalidPlacementIndicatorComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("InvalidPlacementIndicator"));
	InvalidPlacementIndicatorComponent->SetupAttachment(SceneRoot);
	InvalidPlacementIndicatorComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InvalidPlacementIndicatorComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	InvalidPlacementIndicatorComponent->SetHiddenInGame(true);
	InvalidPlacementIndicatorComponent->SetVisibility(false, true);
	InvalidPlacementIndicatorComponent->ShapeColor = FColor::Red;
	InvalidPlacementIndicatorComponent->SetLineThickness(4.0f);

#if WITH_EDITORONLY_DATA
	InvalidPlacementIndicatorComponent->SetIsVisualizationComponent(true);
#endif

	SelectionProxyComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("SelectionProxy"));
	SelectionProxyComponent->SetupAttachment(SceneRoot);
	SelectionProxyComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SelectionProxyComponent->SetCollisionObjectType(ECC_WorldDynamic);
	SelectionProxyComponent->SetCollisionResponseToAllChannels(ECR_Block);
	SelectionProxyComponent->SetHiddenInGame(true);
	SelectionProxyComponent->SetVisibility(false, true);
	SelectionProxyComponent->ComponentTags.AddUnique(TEXT("EHB_DoorWindowSelectionProxy"));

	Tags.AddUnique(TEXT("EHB_DoorWindow"));
}

void AEHB_DoorWindow::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildDoorWindow();
	if (OpeningSplineComponent)
	{
		OpeningSplineComponent->ConstrainToOpeningPlane();
		UpdateOpeningDimensionsFromSpline();
	}
}

void AEHB_DoorWindow::OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished)
{
	Super::OnElementActorMoved_Implementation(OldLocalTransform, NewLocalTransform, bFinished);
	if (bApplyingWallDrivenTransform)
	{
		return;
	}
	RefreshWallBindingAfterTransformChanged(bFinished);
}

void AEHB_DoorWindow::OnElementActorDeleted_Implementation()
{
	ClearWallBinding();
	Super::OnElementActorDeleted_Implementation();
}

bool AEHB_DoorWindow::ResolveCutVolumesForTarget(
	const AEHBElementActorBase* TargetElement,
	TArray<FEHBResolvedCutVolume>& OutVolumes) const
{
	OutVolumes.Reset();
	if (!TargetElement)
	{
		return false;
	}

	const TArray<FVector> LocalOutlinePoints = GetOpeningOutlineLocalPoints();
	if (LocalOutlinePoints.Num() < 3)
	{
		return false;
	}

	const FTransform SourceLocalToTargetLocal =
		GetActorTransform().GetRelativeTransform(TargetElement->GetActorTransform());

	FEHBResolvedCutVolume Volume;
	Volume.OperationGuid = ElementGuid;
	Volume.Stage = EEHBCutStage::SurfaceOpening;
	Volume.ProjectionMode = EEHBCutProjectionMode::VerticalXZ;
	Volume.OperationType = EEHBCutOperationType::Subtract;
	Volume.TargetLocalPolygon.Reserve(LocalOutlinePoints.Num());
	Volume.MinZ = TNumericLimits<float>::Max();
	Volume.MaxZ = TNumericLimits<float>::Lowest();

	for (const FVector& LocalPoint : LocalOutlinePoints)
	{
		const FVector TargetLocalPoint = SourceLocalToTargetLocal.TransformPosition(LocalPoint);
		Volume.TargetLocalPolygon.Add(TargetLocalPoint);
		Volume.MinZ = FMath::Min(Volume.MinZ, static_cast<float>(TargetLocalPoint.Z));
		Volume.MaxZ = FMath::Max(Volume.MaxZ, static_cast<float>(TargetLocalPoint.Z));
	}

	OutVolumes.Add(MoveTemp(Volume));
	return true;
}

#if WITH_EDITOR
void AEHB_DoorWindow::PostEditMove(bool bFinished)
{
	if (bApplyingWallDrivenTransform)
	{
		Super::PostEditMove(bFinished);
		return;
	}

	if (CachedPreEditLocalTransform.Equals(GetElementLocalTransform()))
	{
		Super::PostEditMove(bFinished);
		return;
	}

	const FTransform CurrentWorldTransform = GetActorTransform();
	if (!bTrackingEditorMoveDrag)
	{
		bTrackingEditorMoveDrag = true;
		EditorMoveUnsnappedWorldTransform = CurrentWorldTransform;
		EditorMoveLastValidWorldTransform =
			MakeEditorMoveWorldTransformFromElementLocal(CachedPreEditLocalTransform);
		EditorMoveLastValidWall = FindBoundWall();
		EditorMoveLastValidDistanceFromStart = DistanceFromWallStart;
		bHasEditorMoveLastValidPlacement = true;
		bEditorMovePlacementInvalid = false;
	}
	else
	{
		EditorMoveUnsnappedWorldTransform.AddToTranslation(
			CurrentWorldTransform.GetLocation() - EditorMoveLastAppliedWorldTransform.GetLocation());

		const FQuat RotationDelta = CurrentWorldTransform.GetRotation() * EditorMoveLastAppliedWorldTransform.GetRotation().Inverse();
		EditorMoveUnsnappedWorldTransform.SetRotation(
			(RotationDelta * EditorMoveUnsnappedWorldTransform.GetRotation()).GetNormalized());
		EditorMoveUnsnappedWorldTransform.SetScale3D(CurrentWorldTransform.GetScale3D());
	}

	SetActorTransform(EditorMoveUnsnappedWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);

	bEvaluatingEditorMoveSnap = true;
	EditorMoveSnapWall = FindEditorMoveSnapWall();

	bool bPlacementValid = false;
	float ValidDistanceFromStart = 0.0f;
	if (AEHB_Wall* SnapWall = EditorMoveSnapWall.Get())
	{
		const FVector SnappedLocation = SnapWall->ProjectWorldLocationToCenterAxis(
			GetActorLocation(),
			GetOpeningBottomHeight());
		SetActorLocationAndRotation(SnappedLocation, SnapWall->GetActorRotation(), false, nullptr, ETeleportType::TeleportPhysics);

		bPlacementValid = IsStillAttachedToWall(SnapWall, ValidDistanceFromStart);
		if (bPlacementValid)
		{
			EditorMoveLastValidWorldTransform = GetActorTransform();
			EditorMoveLastValidWall = SnapWall;
			EditorMoveLastValidDistanceFromStart = ValidDistanceFromStart;
			bHasEditorMoveLastValidPlacement = true;
		}
	}

	if (!bPlacementValid && !EditorMoveSnapWall.IsValid() && !EditorMoveLastValidWall.IsValid())
	{
		bPlacementValid = true;
	}

	bEditorMovePlacementInvalid = !bPlacementValid;
	SetInvalidPlacementIndicatorVisible(bEditorMovePlacementInvalid);
	EditorMoveLastAppliedWorldTransform = GetActorTransform();
	Super::PostEditMove(bFinished);

	if (bFinished && bEditorMovePlacementInvalid)
	{
		RestoreLastValidEditorMovePlacement();
	}

	EditorMoveSnapWall.Reset();
	bEvaluatingEditorMoveSnap = false;

	if (bFinished)
	{
		SetInvalidPlacementIndicatorVisible(false);
		bTrackingEditorMoveDrag = false;
		bEditorMovePlacementInvalid = false;
		bHasEditorMoveLastValidPlacement = false;
		EditorMoveLastValidWall.Reset();
		EditorMoveUnsnappedWorldTransform = FTransform::Identity;
		EditorMoveLastAppliedWorldTransform = FTransform::Identity;
		EditorMoveLastValidWorldTransform = FTransform::Identity;
		EditorMoveLastValidDistanceFromStart = 0.0f;
	}
}

void AEHB_DoorWindow::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property
		? PropertyChangedEvent.Property->GetFName()
		: NAME_None;
	const bool bSourceMeshConfigurationChanged =
		PropertyName == GET_MEMBER_NAME_CHECKED(AEHB_DoorWindow, SourceStaticMesh)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(AEHB_DoorWindow, bAutoOrientMesh);
	if (bSourceMeshConfigurationChanged)
	{
		bOpeningSplineInitializedFromSourceMesh = false;
		RefreshOpeningDimensionsFromSourceMesh();
	}

	RebuildDoorWindow();
}
#endif

void AEHB_DoorWindow::ConfigureFromStaticMesh(UStaticMesh* InStaticMesh, EEHBDoorWindowElementKind InKind, float InSillHeight)
{
	SourceStaticMesh = InStaticMesh;
	Kind = InKind;
	SillHeight = FMath::Max(0.0f, InSillHeight);
	bOpeningSplineInitializedFromSourceMesh = false;

	RefreshOpeningDimensionsFromSourceMesh();
	RebuildDoorWindow();
}

void AEHB_DoorWindow::RefreshOpeningDimensionsFromSourceMesh()
{
	UStaticMesh* StaticMesh = SourceStaticMesh.Get();
	if (!StaticMesh && !SourceStaticMesh.IsNull())
	{
		StaticMesh = SourceStaticMesh.LoadSynchronous();
	}

	if (!StaticMesh)
	{
		return;
	}

	const FBox MeshBounds = StaticMesh->GetBoundingBox();
	const FVector MeshSize = MeshBounds.GetSize();
	const float HorizontalX = FMath::Abs(MeshSize.X);
	const float HorizontalY = FMath::Abs(MeshSize.Y);

	OpeningWidth = FMath::Max(1.0f, FMath::Max(HorizontalX, HorizontalY));
	OpeningThickness = FMath::Max(1.0f, FMath::Min(HorizontalX, HorizontalY));
	OpeningHeight = FMath::Max(1.0f, FMath::Abs(MeshSize.Z));
	SourceBoundsMin = MeshBounds.Min;
	SourceBoundsMax = MeshBounds.Max;

	SillHeight = FMath::Max(0.0f, SillHeight);
}

void AEHB_DoorWindow::RebuildDoorWindow()
{
	if (!DoorWindowMeshComponent)
	{
		return;
	}

	UStaticMesh* StaticMesh = SourceStaticMesh.Get();
	if (!StaticMesh && !SourceStaticMesh.IsNull())
	{
		StaticMesh = SourceStaticMesh.LoadSynchronous();
	}

	if (!StaticMesh)
	{
		StaticMesh = DoorWindowMeshComponent->GetStaticMesh();
		if (StaticMesh && SourceStaticMesh.IsNull())
		{
			SourceStaticMesh = StaticMesh;
		}
	}

	if (!StaticMesh)
	{
		if (OpeningSplineComponent)
		{
			OpeningSplineComponent->ConstrainToOpeningPlane();
			UpdateOpeningDimensionsFromSpline();
		}
		UpdateSelectionProxy();
		return;
	}

	DoorWindowMeshComponent->SetStaticMesh(StaticMesh);
	DoorWindowMeshComponent->SetVisibility(true, true);
	DoorWindowMeshComponent->SetHiddenInGame(false);

	const FBox MeshBounds = StaticMesh->GetBoundingBox();
	const FRotator MeshRotation = MakeDoorWindowMeshRotation(MeshBounds, bAutoOrientMesh);
	const FVector BoundsCenter = MeshBounds.GetCenter();
	const FVector RotatedCenter = MeshRotation.RotateVector(FVector(BoundsCenter.X, BoundsCenter.Y, 0.0f));

	DoorWindowMeshComponent->SetRelativeRotation(MeshRotation);
	DoorWindowMeshComponent->SetRelativeLocation(FVector(-RotatedCenter.X, -RotatedCenter.Y, -MeshBounds.Min.Z));

	if (!bOpeningSplineInitializedFromSourceMesh)
	{
		RebuildOpeningSplineFromSourceMeshBounds();
	}
	else if (OpeningSplineComponent)
	{
		OpeningSplineComponent->ConstrainToOpeningPlane();
		UpdateOpeningDimensionsFromSpline();
	}

	UpdateSelectionProxy();
}

void AEHB_DoorWindow::SetRectangularOpeningDimensions(float InWidth, float InHeight, float InBottomHeight, float InThickness)
{
	Modify();
	SillHeight = FMath::Max(0.0f, InBottomHeight);
	OpeningThickness = FMath::Max(1.0f, InThickness);
	SetQuadrilateralOpeningSpline(
		OpeningSplineComponent,
		FMath::Max(1.0f, InWidth),
		FMath::Max(1.0f, InHeight),
		true);
	HandleOpeningSplineEdited();
}

bool AEHB_DoorWindow::SetWindowSillHeight(float InSillHeight)
{
	if (Kind != EEHBDoorWindowElementKind::Window)
	{
		return false;
	}

	const float NewSillHeight = FMath::Max(0.0f, InSillHeight);
	if (FMath::IsNearlyEqual(SillHeight, NewSillHeight, 0.01f))
	{
		return false;
	}

	Modify();
	SillHeight = NewSillHeight;

	if (AEHB_Wall* Wall = FindBoundWall())
	{
		Wall->Modify();
		SetActorLocationAndRotation(
			Wall->GetWorldLocationOnCenterAxisAtDistance(DistanceFromWallStart, GetOpeningBottomHeight()),
			Wall->GetActorRotation(),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		BindToWall(Wall, DistanceFromWallStart);
		Wall->RebuildWallMesh();
	}

	UpdateSelectionProxy();
	CachedPreEditLocalTransform = GetElementLocalTransform();
	NotifyElementGeometryChanged(true);
	MarkPackageDirty();
	return true;
}

void AEHB_DoorWindow::RebuildOpeningSplineFromSourceMeshBounds()
{
	if (!OpeningSplineComponent)
	{
		return;
	}

	UStaticMesh* StaticMesh = SourceStaticMesh.Get();
	if (!StaticMesh && !SourceStaticMesh.IsNull())
	{
		StaticMesh = SourceStaticMesh.LoadSynchronous();
	}

	if (!StaticMesh)
	{
		OpeningSplineComponent->ConstrainToOpeningPlane();
		UpdateOpeningDimensionsFromSpline();
		return;
	}

	const FVector MeshSize = StaticMesh->GetBoundingBox().GetSize();
	const float HorizontalX = FMath::Abs(MeshSize.X);
	const float HorizontalY = FMath::Abs(MeshSize.Y);
	const float Width = bAutoOrientMesh
		? FMath::Max(HorizontalX, HorizontalY)
		: HorizontalX;
	const float HalfWidth = FMath::Max(1.0f, Width * 0.5f);
	const float Height = FMath::Max(1.0f, FMath::Abs(MeshSize.Z));

#if WITH_EDITOR
	OpeningSplineComponent->Modify();
#endif
	OpeningSplineComponent->ClearSplinePoints(false);
	OpeningSplineComponent->AddSplinePoint(FVector(-HalfWidth, 0.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	OpeningSplineComponent->AddSplinePoint(FVector(-HalfWidth, 0.0f, Height), ESplineCoordinateSpace::Local, false);
	OpeningSplineComponent->AddSplinePoint(FVector(HalfWidth, 0.0f, Height), ESplineCoordinateSpace::Local, false);
	OpeningSplineComponent->AddSplinePoint(FVector(HalfWidth, 0.0f, 0.0f), ESplineCoordinateSpace::Local, false);
	OpeningSplineComponent->ConstrainToOpeningPlane();
	bOpeningSplineInitializedFromSourceMesh = true;
	UpdateOpeningDimensionsFromSpline();
	MarkPackageDirty();
}

void AEHB_DoorWindow::InitializeDefaultWindowOpening()
{
	Modify();
	Kind = EEHBDoorWindowElementKind::Window;
	SillHeight = EHBDefaultWindowSillHeight;
	OpeningThickness = EHBDefaultOpeningThickness;
	SetQuadrilateralOpeningSpline(
		OpeningSplineComponent,
		EHBDefaultWindowOpeningSize,
		EHBDefaultWindowOpeningSize,
		true);
	HandleOpeningSplineEdited();
}

void AEHB_DoorWindow::InitializeDefaultDoorOpening()
{
	Modify();
	Kind = EEHBDoorWindowElementKind::Door;
	SillHeight = 0.0f;
	OpeningThickness = EHBDefaultOpeningThickness;
	SetQuadrilateralOpeningSpline(
		OpeningSplineComponent,
		EHBDefaultDoorOpeningWidth,
		EHBDefaultDoorOpeningHeight,
		true);
	HandleOpeningSplineEdited();
}

TArray<FVector> AEHB_DoorWindow::GetOpeningOutlineLocalPoints() const
{
	TArray<FVector> OutlinePoints;
	if (!OpeningSplineComponent)
	{
		return OutlinePoints;
	}

	const FTransform ComponentToActor = OpeningSplineComponent->GetRelativeTransform();
	const int32 PointCount = OpeningSplineComponent->GetNumberOfSplinePoints();
	OutlinePoints.Reserve(PointCount);
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		FVector Point = OpeningSplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::Local);
		Point = ComponentToActor.TransformPosition(Point);
		Point.Y = 0.0f;
		OutlinePoints.Add(Point);
	}

	return OutlinePoints;
}

TArray<FVector> AEHB_DoorWindow::GetOpeningOutlineWorldPoints() const
{
	TArray<FVector> OutlinePoints;
	if (!OpeningSplineComponent)
	{
		return OutlinePoints;
	}

	const int32 PointCount = OpeningSplineComponent->GetNumberOfSplinePoints();
	OutlinePoints.Reserve(PointCount);
	for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
	{
		OutlinePoints.Add(OpeningSplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World));
	}

	return OutlinePoints;
}

void AEHB_DoorWindow::HandleOpeningSplineEdited()
{
	Modify();
	bOpeningSplineInitializedFromSourceMesh = true;
	if (OpeningSplineComponent)
	{
		OpeningSplineComponent->ConstrainToOpeningPlane();
	}
	UpdateOpeningDimensionsFromSpline();
	UpdateSelectionProxy();
	MarkPackageDirty();

	if (AEHB_Wall* Wall = FindBoundWall())
	{
		Wall->Modify();
		Wall->AddOrUpdateDoorWindowConnection(this, DistanceFromWallStart);
		Wall->RebuildWallMesh();
	}
}

void AEHB_DoorWindow::UpdateOpeningDimensionsFromSpline()
{
	const TArray<FVector> OutlinePoints = GetOpeningOutlineLocalPoints();
	if (OutlinePoints.Num() < 3)
	{
		return;
	}

	float MinX = TNumericLimits<float>::Max();
	float MaxX = TNumericLimits<float>::Lowest();
	float MinZ = TNumericLimits<float>::Max();
	float MaxZ = TNumericLimits<float>::Lowest();
	for (const FVector& Point : OutlinePoints)
	{
		MinX = FMath::Min(MinX, Point.X);
		MaxX = FMath::Max(MaxX, Point.X);
		MinZ = FMath::Min(MinZ, Point.Z);
		MaxZ = FMath::Max(MaxZ, Point.Z);
	}

	OpeningWidth = FMath::Max(1.0f, MaxX - MinX);
	OpeningHeight = FMath::Max(1.0f, MaxZ - MinZ);
}

void AEHB_DoorWindow::UpdateSelectionProxy()
{
	if (!SelectionProxyComponent)
	{
		return;
	}

	const float ProxyWidth = FMath::Max(10.0f, OpeningWidth);
	const float ProxyHeight = FMath::Max(10.0f, OpeningHeight);
	const float ProxyThickness = FMath::Max(8.0f, OpeningThickness);
	SelectionProxyComponent->SetBoxExtent(
		FVector(ProxyWidth * 0.5f, ProxyThickness * 0.5f, ProxyHeight * 0.5f),
		false);
	SelectionProxyComponent->SetRelativeLocation(FVector(0.0f, 0.0f, ProxyHeight * 0.5f));
}

float AEHB_DoorWindow::GetOpeningBottomHeight() const
{
	if (Kind == EEHBDoorWindowElementKind::Door)
	{
		return 0.0f;
	}

	return FMath::Max(0.0f, SillHeight);
}

FEHBDoorWindowOpeningData AEHB_DoorWindow::MakeOpeningData() const
{
	FEHBDoorWindowOpeningData OpeningData;
	OpeningData.Kind = Kind;
	OpeningData.Width = FMath::Max(1.0f, OpeningWidth);
	OpeningData.Height = FMath::Max(1.0f, OpeningHeight);
	OpeningData.Thickness = FMath::Max(1.0f, OpeningThickness);
	OpeningData.BottomHeight = GetOpeningBottomHeight();
	OpeningData.OwningWallGuid = OwningWallGuid;
	OpeningData.WorldTransform = GetActorTransform();
	OpeningData.LocalOutlinePoints = GetOpeningOutlineLocalPoints();
	return OpeningData;
}

void AEHB_DoorWindow::BindToWall(AEHB_Wall* Wall, float InDistanceFromWallStart)
{
	if (!Wall)
	{
		ClearWallBinding();
		return;
	}

	EnsureElementGuid();
	Wall->EnsureElementGuid();
	AEHB_Wall* PreviousWall = FindBoundWall();
	const FGuid PreviousWallGuid = OwningWallGuid;
	if (PreviousWall && PreviousWall != Wall)
	{
		PreviousWall->Modify();
		PreviousWall->RemoveDoorWindowConnectionByGuid(ElementGuid);
		PreviousWall->RebuildWallMesh();
	}

	AEHBBuildingActorBase* TargetBuilding = Wall->OwningBuilding ? Wall->OwningBuilding : OwningBuilding;
	if (TargetBuilding)
	{
		if (OwningBuilding != TargetBuilding)
		{
			const FTransform TargetLocalTransform =
				GetActorTransform().GetRelativeTransform(TargetBuilding->GetActorTransform());
			AttachToBuilding(TargetBuilding, TargetLocalTransform);
		}
		else
		{
			TargetBuilding->RegisterElementActor(this);
		}
	}

	OwningWallGuid = Wall->ElementGuid;
	const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
	DistanceFromWallStart = FMath::Clamp(InDistanceFromWallStart, 0.0f, WallLength);
	if (OwningBuilding && Wall->OwningBuilding == OwningBuilding)
	{
		if (PreviousWallGuid.IsValid() && PreviousWallGuid != Wall->ElementGuid)
		{
			OwningBuilding->RemoveRelationsBetweenElements(
				PreviousWallGuid,
				ElementGuid,
				EEHBElementRelationType::HostedElement);
		}

		FEHBElementRelation Relation;
		Relation.Type = EEHBElementRelationType::HostedElement;
		Relation.Source = FEHBElementRelationEndpoint::MakeElement(
			Wall->ElementGuid,
			EEHBElementSurfaceKind::Opening,
			TEXT("Wall.Opening"));
		Relation.Target = FEHBElementRelationEndpoint::MakeElement(ElementGuid);
		Relation.Origin = EEHBRelationOrigin::SystemGenerated;
		Relation.TargetRelativeToSource = GetActorTransform().GetRelativeTransform(Wall->GetActorTransform());
		Relation.NumericMetadata.Add(TEXT("DistanceFromWallStart"), DistanceFromWallStart);
		OwningBuilding->AddOrUpdateElementRelation(Relation, true);
	}

	Wall->AddOrUpdateDoorWindowConnection(this, DistanceFromWallStart);
	MarkPackageDirty();
}

void AEHB_DoorWindow::ApplyWallDrivenTransform(AEHB_Wall* Wall, const FTransform& WorldTransform, float InDistanceFromWallStart)
{
	if (bApplyingWallDrivenTransform)
	{
		return;
	}

	TGuardValue<bool> WallDrivenTransformGuard(bApplyingWallDrivenTransform, true);

	Modify();
	SetActorTransform(WorldTransform, false, nullptr, ETeleportType::TeleportPhysics);

	if (Wall)
	{
		Wall->EnsureElementGuid();
		if (AEHBBuildingActorBase* TargetBuilding = Wall->OwningBuilding)
		{
			if (OwningBuilding != TargetBuilding)
			{
				const FTransform TargetLocalTransform =
					WorldTransform.GetRelativeTransform(TargetBuilding->GetActorTransform());
				AttachToBuilding(TargetBuilding, TargetLocalTransform);
			}
			else
			{
				TargetBuilding->RegisterElementActor(this);
			}
		}

		OwningWallGuid = Wall->ElementGuid;
	}
	const float WallLength = Wall ? FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd) : 0.0f;
	DistanceFromWallStart = Wall && WallLength > UE_SMALL_NUMBER
		? FMath::Clamp(InDistanceFromWallStart, 0.0f, WallLength)
		: FMath::Max(0.0f, InDistanceFromWallStart);
	if (Wall && OwningBuilding && Wall->OwningBuilding == OwningBuilding)
	{
		FEHBElementRelation Relation;
		Relation.Type = EEHBElementRelationType::HostedElement;
		Relation.Source = FEHBElementRelationEndpoint::MakeElement(
			Wall->ElementGuid,
			EEHBElementSurfaceKind::Opening,
			TEXT("Wall.Opening"));
		Relation.Target = FEHBElementRelationEndpoint::MakeElement(ElementGuid);
		Relation.Origin = EEHBRelationOrigin::SystemGenerated;
		Relation.TargetRelativeToSource = WorldTransform.GetRelativeTransform(Wall->GetActorTransform());
		Relation.NumericMetadata.Add(TEXT("DistanceFromWallStart"), DistanceFromWallStart);
		OwningBuilding->AddOrUpdateElementRelation(Relation, true);
	}
	MarkPackageDirty();
}

bool AEHB_DoorWindow::ReplaceDoorWindow(AEHB_DoorWindow* ReplacementDoorWindow, AEHB_DoorWindow* ExistingDoorWindow)
{
	if (!ReplacementDoorWindow
		|| !ExistingDoorWindow
		|| ReplacementDoorWindow == ExistingDoorWindow
		|| ReplacementDoorWindow->IsActorBeingDestroyed()
		|| ExistingDoorWindow->IsActorBeingDestroyed())
	{
		return false;
	}

	AEHB_Wall* TargetWall = ExistingDoorWindow->FindBoundWall();
	if (!TargetWall)
	{
		return false;
	}

	const float ReplacementDistanceFromStart =
		TargetWall->CalculateDistanceFromStartForWorldLocation(ReplacementDoorWindow->GetActorLocation());
	AEHBBuildingActorBase* TargetBuilding = TargetWall->OwningBuilding
		? TargetWall->OwningBuilding
		: ExistingDoorWindow->OwningBuilding;

	if (TargetBuilding)
	{
		TargetBuilding->Modify();
	}
	TargetWall->Modify();
	ReplacementDoorWindow->Modify();
	ExistingDoorWindow->Modify();

	const FVector ReplacementScale = ReplacementDoorWindow->GetActorScale3D();
	const FTransform ReplacementWorldTransform(
		TargetWall->GetActorRotation(),
		TargetWall->GetWorldLocationOnCenterAxisAtDistance(
			ReplacementDistanceFromStart,
			ReplacementDoorWindow->GetOpeningBottomHeight()),
		ReplacementScale);

	ReplacementDoorWindow->SetActorTransform(ReplacementWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	if (TargetBuilding)
	{
		ReplacementDoorWindow->AttachToBuilding(
			TargetBuilding,
			ReplacementWorldTransform.GetRelativeTransform(TargetBuilding->GetActorTransform()));
	}

	ExistingDoorWindow->ClearWallBinding();

	ReplacementDoorWindow->BindToWall(TargetWall, ReplacementDistanceFromStart);
	ReplacementDoorWindow->SetFloorAssignment(TargetWall->FloorIndex, EEHBBuildingFloorElementRole::HostedElement);
	ReplacementDoorWindow->RebuildDoorWindow();
	ReplacementDoorWindow->CachedPreEditLocalTransform = ReplacementDoorWindow->GetElementLocalTransform();
	ReplacementDoorWindow->MarkPackageDirty();

	ExistingDoorWindow->Destroy();

	TargetWall->RebuildWallMesh();
	TargetWall->MarkPackageDirty();
	if (TargetBuilding)
	{
		TargetBuilding->MarkPackageDirty();
	}

	return true;
}

void AEHB_DoorWindow::ClearWallBinding()
{
	const FGuid PreviousWallGuid = OwningWallGuid;
	if (AEHB_Wall* Wall = FindBoundWall())
	{
		Wall->Modify();
		Wall->RemoveDoorWindowConnectionByGuid(ElementGuid);
		Wall->RebuildWallMesh();
	}

	if (OwningWallGuid.IsValid() || DistanceFromWallStart > 0.0f)
	{
		Modify();
		OwningWallGuid.Invalidate();
		DistanceFromWallStart = 0.0f;
		MarkPackageDirty();
	}

	if (OwningBuilding && PreviousWallGuid.IsValid() && ElementGuid.IsValid())
	{
		OwningBuilding->RemoveRelationsBetweenElements(
			PreviousWallGuid,
			ElementGuid,
			EEHBElementRelationType::HostedElement);
	}
}

void AEHB_DoorWindow::RefreshWallBindingAfterTransformChanged(bool bFinished)
{
#if WITH_EDITOR
	if (bEvaluatingEditorMoveSnap)
	{
		if (bEditorMovePlacementInvalid)
		{
			return;
		}

		AEHB_Wall* SnapWall = EditorMoveSnapWall.Get();
		if (!SnapWall)
		{
			if (OwningWallGuid.IsValid())
			{
				ClearWallBinding();
			}
			return;
		}

		AEHB_Wall* PreviousWall = FindBoundWall();
		const float NewDistanceFromStart = SnapWall->CalculateDistanceFromStartForWorldLocation(GetActorLocation());
		const bool bChangingWall = PreviousWall != SnapWall || OwningWallGuid != SnapWall->ElementGuid;

		if (bChangingWall)
		{
			ClearWallBinding();

			Modify();
			SnapWall->Modify();
			BindToWall(SnapWall, NewDistanceFromStart);
			SnapWall->AddOrUpdateDoorWindowConnection(this, NewDistanceFromStart);
			SnapWall->RebuildWallMesh();
			if (bFinished)
			{
				if (AEHB_DoorWindow* ExistingDoorWindow = SnapWall->FindOverlappingDoorWindow(this, NewDistanceFromStart))
				{
					ReplaceDoorWindow(this, ExistingDoorWindow);
				}
			}
			return;
		}

		if (!FMath::IsNearlyEqual(DistanceFromWallStart, NewDistanceFromStart))
		{
			Modify();
			DistanceFromWallStart = NewDistanceFromStart;
			MarkPackageDirty();
		}

		// Keep the generated opening synchronized throughout the editor gizmo drag.
		SnapWall->Modify();
		SnapWall->AddOrUpdateDoorWindowConnection(this, DistanceFromWallStart);
		SnapWall->RebuildWallMesh();
		if (bFinished)
		{
			if (AEHB_DoorWindow* ExistingDoorWindow = SnapWall->FindOverlappingDoorWindow(this, DistanceFromWallStart))
			{
				ReplaceDoorWindow(this, ExistingDoorWindow);
			}
		}
		return;
	}
#endif

	if (!OwningWallGuid.IsValid())
	{
		return;
	}

	AEHB_Wall* Wall = FindBoundWall();
	if (!Wall)
	{
		ClearWallBinding();
		return;
	}

	float NewDistanceFromStart = 0.0f;
	if (!IsStillAttachedToWall(Wall, NewDistanceFromStart))
	{
		ClearWallBinding();
		return;
	}

	DistanceFromWallStart = NewDistanceFromStart;
	Wall->Modify();
	Wall->AddOrUpdateDoorWindowConnection(this, DistanceFromWallStart);
	Wall->RebuildWallMesh();
	if (bFinished)
	{
		if (AEHB_DoorWindow* ExistingDoorWindow = Wall->FindOverlappingDoorWindow(this, DistanceFromWallStart))
		{
			ReplaceDoorWindow(this, ExistingDoorWindow);
		}
		Modify();
		MarkPackageDirty();
	}
}

void AEHB_DoorWindow::RequestPreviewWallOpening(AEHB_Wall* Wall)
{
	if (!Wall)
	{
		return;
	}

	const float DistanceFromStart =
		Wall->CalculateDistanceFromStartForWorldLocation(GetActorLocation());
	Wall->SetPreviewDoorWindowOpening(this, DistanceFromStart);
}

AEHB_Wall* AEHB_DoorWindow::FindBoundWall() const
{
	if (!OwningWallGuid.IsValid())
	{
		return nullptr;
	}

	if (OwningBuilding)
	{
		if (AEHB_Wall* IndexedWall = Cast<AEHB_Wall>(OwningBuilding->FindElementActorByGuid(OwningWallGuid)))
		{
			return IndexedWall;
		}

		TArray<AActor*> AttachedActors;
		OwningBuilding->GetAttachedActors(AttachedActors);
		for (AActor* AttachedActor : AttachedActors)
		{
			AEHB_Wall* Wall = Cast<AEHB_Wall>(AttachedActor);
			if (Wall && Wall->ElementGuid == OwningWallGuid)
			{
				OwningBuilding->RegisterElementActor(Wall);
				return Wall;
			}
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AEHB_Wall> It(World); It; ++It)
	{
		AEHB_Wall* Wall = *It;
		if (!Wall
			|| Wall->IsActorBeingDestroyed()
			|| Wall->ElementGuid != OwningWallGuid)
		{
			continue;
		}

		if (OwningBuilding
			&& Wall->OwningBuilding != OwningBuilding
			&& Wall->GetOwner() != OwningBuilding
			&& Wall->GetAttachParentActor() != OwningBuilding)
		{
			continue;
		}

		if (OwningBuilding && Wall->OwningBuilding == OwningBuilding)
		{
			OwningBuilding->RegisterElementActor(Wall);
		}
		return Wall;
	}

	return nullptr;
}

bool AEHB_DoorWindow::CanPreviewAgainstWall(const AEHB_Wall* Wall, float& OutDistanceFromStart) const
{
	OutDistanceFromStart = 0.0f;
	if (!Wall)
	{
		return false;
	}

	const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
	if (WallLength <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector WallLocalLocation = Wall->GetActorTransform().InverseTransformPosition(GetActorLocation());
	const float AxisTolerance = FMath::Max(Wall->Thickness, OpeningThickness) * 0.5f + 15.0f;
	if (FMath::Abs(WallLocalLocation.Y) > AxisTolerance)
	{
		return false;
	}

	const float HeightTolerance = 20.0f;
	const float ExpectedBottomHeight = GetOpeningBottomHeight();
	if (WallLocalLocation.Z < -HeightTolerance || WallLocalLocation.Z > Wall->Height + ExpectedBottomHeight + HeightTolerance)
	{
		return false;
	}

	const FVector Segment = Wall->LocalEnd - Wall->LocalStart;
	const FVector Direction = Segment.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	FVector BuildingLocalLocation = GetActorLocation();
	if (Wall->OwningBuilding)
	{
		BuildingLocalLocation = Wall->OwningBuilding->GetActorTransform().InverseTransformPosition(GetActorLocation());
	}
	else if (OwningBuilding)
	{
		BuildingLocalLocation = OwningBuilding->GetActorTransform().InverseTransformPosition(GetActorLocation());
	}

	const float UnclampedDistance = FVector::DotProduct(BuildingLocalLocation - Wall->LocalStart, Direction);
	const float PreviewTolerance = FMath::Max(OpeningWidth, OpeningHeight) + 50.0f;
	if (UnclampedDistance < -PreviewTolerance || UnclampedDistance > WallLength + PreviewTolerance)
	{
		return false;
	}

	OutDistanceFromStart = FMath::Clamp(UnclampedDistance, 0.0f, WallLength);
	return true;
}

bool AEHB_DoorWindow::DoesOpeningFitWithinWallLength(const AEHB_Wall* Wall, float DistanceFromStart, float Tolerance) const
{
	if (!Wall)
	{
		return false;
	}

	const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
	if (WallLength <= UE_SMALL_NUMBER)
	{
		return false;
	}

	TArray<FVector> OutlinePoints = GetOpeningOutlineLocalPoints();
	if (OutlinePoints.Num() < 3)
	{
		const float HalfWidth = FMath::Max(1.0f, OpeningWidth) * 0.5f;
		OutlinePoints = {
			FVector(-HalfWidth, 0.0f, 0.0f),
			FVector(HalfWidth, 0.0f, 0.0f)
		};
	}

	const FTransform DoorWindowLocalToWall = GetActorTransform().GetRelativeTransform(Wall->GetActorTransform());
	float MinOpeningX = TNumericLimits<float>::Max();
	float MaxOpeningX = TNumericLimits<float>::Lowest();
	for (const FVector& OutlinePoint : OutlinePoints)
	{
		const FVector WallSpaceOffset = DoorWindowLocalToWall.TransformVector(OutlinePoint);
		MinOpeningX = FMath::Min(MinOpeningX, WallSpaceOffset.X);
		MaxOpeningX = FMath::Max(MaxOpeningX, WallSpaceOffset.X);
	}

	if (MinOpeningX > MaxOpeningX)
	{
		return false;
	}

	return DistanceFromStart + MinOpeningX >= -Tolerance
		&& DistanceFromStart + MaxOpeningX <= WallLength + Tolerance;
}

bool AEHB_DoorWindow::IsStillAttachedToWall(const AEHB_Wall* Wall, float& OutDistanceFromStart) const
{
	OutDistanceFromStart = 0.0f;
	if (!Wall)
	{
		return false;
	}

	const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
	if (WallLength <= UE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector WallLocalLocation = Wall->GetActorTransform().InverseTransformPosition(GetActorLocation());
	const float AxisTolerance = FMath::Max(Wall->Thickness, OpeningThickness) * 0.5f + 5.0f;
	if (FMath::Abs(WallLocalLocation.Y) > AxisTolerance)
	{
		return false;
	}

	const float HeightTolerance = 10.0f;
	const float ExpectedBottomHeight = GetOpeningBottomHeight();
	if (WallLocalLocation.Z < -HeightTolerance || WallLocalLocation.Z > Wall->Height + ExpectedBottomHeight + HeightTolerance)
	{
		return false;
	}

	const FVector Segment = Wall->LocalEnd - Wall->LocalStart;
	const FVector Direction = Segment.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	FVector BuildingLocalLocation = GetActorLocation();
	if (Wall->OwningBuilding)
	{
		BuildingLocalLocation = Wall->OwningBuilding->GetActorTransform().InverseTransformPosition(GetActorLocation());
	}
	else if (OwningBuilding)
	{
		BuildingLocalLocation = OwningBuilding->GetActorTransform().InverseTransformPosition(GetActorLocation());
	}

	const float UnclampedDistance = FVector::DotProduct(BuildingLocalLocation - Wall->LocalStart, Direction);
	if (!DoesOpeningFitWithinWallLength(Wall, UnclampedDistance, 0.1f))
	{
		return false;
	}

	OutDistanceFromStart = FMath::Clamp(UnclampedDistance, 0.0f, WallLength);
	return true;
}

#if WITH_EDITOR
AEHB_Wall* AEHB_DoorWindow::FindEditorMoveSnapWall() const
{
	if (AEHB_Wall* BoundWall = FindBoundWall())
	{
		float DistanceFromStart = 0.0f;
		if (CanPreviewAgainstWall(BoundWall, DistanceFromStart))
		{
			return BoundWall;
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const FVector HalfExtent(
		FMath::Max(1.0f, OpeningWidth * 0.5f),
		FMath::Max(1.0f, OpeningThickness * 0.5f),
		FMath::Max(1.0f, OpeningHeight * 0.5f));
	const FVector QueryCenter = GetActorTransform().TransformPosition(FVector(0.0f, 0.0f, HalfExtent.Z));

	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EasyHouseBuilder_DoorWindowEditorSnap), false, this);
	QueryParams.bReturnPhysicalMaterial = false;

	TArray<FOverlapResult> OverlapResults;
	if (!World->OverlapMultiByObjectType(
		OverlapResults,
		QueryCenter,
		GetActorQuat(),
		ObjectQueryParams,
		FCollisionShape::MakeBox(HalfExtent),
		QueryParams))
	{
		return nullptr;
	}

	AEHB_Wall* BestWall = nullptr;
	double BestDistanceSquared = TNumericLimits<double>::Max();
	for (const FOverlapResult& OverlapResult : OverlapResults)
	{
		AEHB_Wall* CandidateWall = Cast<AEHB_Wall>(OverlapResult.GetActor());
		if (!CandidateWall || (OwningBuilding && CandidateWall->OwningBuilding != OwningBuilding))
		{
			continue;
		}

		float CandidateDistanceFromStart = 0.0f;
		if (!CanPreviewAgainstWall(CandidateWall, CandidateDistanceFromStart))
		{
			continue;
		}

		const FVector ProjectedLocation = CandidateWall->ProjectWorldLocationToCenterAxis(
			GetActorLocation(),
			GetOpeningBottomHeight());
		const double DistanceSquared = FVector::DistSquared(GetActorLocation(), ProjectedLocation);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestWall = CandidateWall;
		}
	}

	return BestWall;
}

FTransform AEHB_DoorWindow::MakeEditorMoveWorldTransformFromElementLocal(const FTransform& LocalTransform) const
{
	if (const AActor* ParentActor = GetAttachParentActor())
	{
		return LocalTransform * ParentActor->GetActorTransform();
	}

	return LocalTransform;
}

void AEHB_DoorWindow::SetInvalidPlacementIndicatorVisible(bool bVisible)
{
	if (!InvalidPlacementIndicatorComponent)
	{
		return;
	}

	const FVector BoxExtent(
		FMath::Max(1.0f, OpeningWidth * 0.5f),
		FMath::Max(2.0f, OpeningThickness * 0.5f),
		FMath::Max(1.0f, OpeningHeight * 0.5f));
	InvalidPlacementIndicatorComponent->SetBoxExtent(BoxExtent, false);
	InvalidPlacementIndicatorComponent->SetRelativeLocation(FVector(0.0f, 0.0f, BoxExtent.Z));
	InvalidPlacementIndicatorComponent->SetVisibility(bVisible, true);
	InvalidPlacementIndicatorComponent->SetHiddenInGame(!bVisible);
}

void AEHB_DoorWindow::RestoreLastValidEditorMovePlacement()
{
	if (!bHasEditorMoveLastValidPlacement)
	{
		return;
	}

	Modify();
	SetActorTransform(EditorMoveLastValidWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);

	AEHB_Wall* LastValidWall = EditorMoveLastValidWall.Get();
	if (LastValidWall)
	{
		LastValidWall->Modify();
		BindToWall(LastValidWall, EditorMoveLastValidDistanceFromStart);
		LastValidWall->RebuildWallMesh();
	}

	CachedPreEditLocalTransform = GetElementLocalTransform();
	MarkPackageDirty();
}
#endif
