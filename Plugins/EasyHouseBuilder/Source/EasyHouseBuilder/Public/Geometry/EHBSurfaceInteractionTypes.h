// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Definitions/EHBElementRelations.h"
#include "EHBSurfaceInteractionTypes.generated.h"

class AActor;
class AEHBBuildingActorBase;
class AEHBElementActorBase;
class UEHBArchitecturalSurfaceComponent;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EEHBArchitecturalSurfaceRole : uint8
{
	Unknown UMETA(DisplayName = "Unknown"),
	WallLeftSide UMETA(DisplayName = "Wall Left Side"),
	WallRightSide UMETA(DisplayName = "Wall Right Side"),
	WallCap UMETA(DisplayName = "Wall Cap"),
	WallOpeningReveal UMETA(DisplayName = "Wall Opening Reveal"),
	PillarSide UMETA(DisplayName = "Pillar Side"),
	PillarTop UMETA(DisplayName = "Pillar Top"),
	SlabTop UMETA(DisplayName = "Slab Top"),
	SlabBottom UMETA(DisplayName = "Slab Bottom"),
	SlabOuterSide UMETA(DisplayName = "Slab Outer Side"),
	SlabHoleSide UMETA(DisplayName = "Slab Hole Side"),
	FloorFinishTop UMETA(DisplayName = "Floor Finish Top"),
	RoofTop UMETA(DisplayName = "Roof Top"),
	RoofGableEnd UMETA(DisplayName = "Roof Gable End"),
	RoofSideCap UMETA(DisplayName = "Roof Side Cap"),
	RoofLinearTrim UMETA(DisplayName = "Roof Linear Trim"),
	RailingPanelSide UMETA(DisplayName = "Railing Panel Side"),
	Custom UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class EEHBSurfaceSnapKind : uint8
{
	Point UMETA(DisplayName = "Point"),
	Edge UMETA(DisplayName = "Edge"),
	Plane UMETA(DisplayName = "Plane"),
	CenterLine UMETA(DisplayName = "Center Line"),
	OpeningEdge UMETA(DisplayName = "Opening Edge"),
	SegmentEndpoint UMETA(DisplayName = "Segment Endpoint")
};

UENUM(BlueprintType)
enum class EEHBSnapIntent : uint8
{
	PlaceFloorSlabSide UMETA(DisplayName = "Place Floor Slab Side"),
	RotateFloorSlab UMETA(DisplayName = "Rotate Floor Slab"),
	PlaceStairSide UMETA(DisplayName = "Place Stair Side"),
	PlaceDoorWindowOnWall UMETA(DisplayName = "Place Door Window On Wall"),
	PlaceRailingEndpoint UMETA(DisplayName = "Place Railing Endpoint"),
	ConnectWallToPillar UMETA(DisplayName = "Connect Wall To Pillar"),
	AlignPillarRotation UMETA(DisplayName = "Align Pillar Rotation"),
	ApplySurfaceMaterial UMETA(DisplayName = "Apply Surface Material"),
	Custom UMETA(DisplayName = "Custom")
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceEndpoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface")
	FGuid OwnerElementGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface")
	EEHBElementSurfaceKind SurfaceKind = EEHBElementSurfaceKind::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface")
	FName SurfaceName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface")
	int32 SubIndex = INDEX_NONE;

	FEHBElementRelationEndpoint ToRelationEndpoint() const
	{
		return FEHBElementRelationEndpoint::MakeElement(OwnerElementGuid, SurfaceKind, SurfaceName, SubIndex);
	}
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceHit
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Hit")
	TObjectPtr<AEHBElementActorBase> Element = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Hit")
	TObjectPtr<UEHBArchitecturalSurfaceComponent> Surface = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Hit")
	FEHBElementRelationEndpoint Endpoint;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Hit")
	FVector WorldPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Hit")
	FVector WorldNormal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Hit")
	bool bLikelyTop = false;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Hit")
	bool bLikelyVerticalSide = false;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceSnapCandidate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	EEHBSurfaceSnapKind SnapKind = EEHBSurfaceSnapKind::Point;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldNormal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldTangent = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	float Distance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FEHBSurfaceEndpoint Endpoint;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceSideSnapEdge
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldStart = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldEnd = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldOutwardNormal = FVector::RightVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldMidpoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	float MinWorldZ = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	float MaxWorldZ = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FEHBSurfaceEndpoint Endpoint;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	TObjectPtr<UEHBArchitecturalSurfaceComponent> SourceSurface = nullptr;

	bool IsValid() const
	{
		return !WorldDirection.IsNearlyZero() && !WorldOutwardNormal.IsNearlyZero();
	}
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSnapAxisFeature
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldOrigin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldForward = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldRight = FVector::RightVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldUp = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FEHBElementRelationEndpoint Endpoint;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FName FeatureName = NAME_None;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSnapPathFeature
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	TArray<FVector> WorldPolyline;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	bool bClosed = false;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FEHBElementRelationEndpoint Endpoint;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FName PathName = NAME_None;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBPathProjectionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldTangent = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	float DistanceFromStart = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	int32 SegmentIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	float SegmentAlpha = 0.0f;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSupportSurfaceFeature
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Support")
	FEHBElementRelationEndpoint Endpoint;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Support")
	FVector WorldPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Support")
	FVector WorldNormal = FVector::UpVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Support")
	int32 SourceFloorIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Support")
	int32 TargetFloorIndex = 1;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSnapQuery
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Snap")
	EEHBSnapIntent Intent = EEHBSnapIntent::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Snap")
	FVector WorldReferencePoint = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Snap")
	FVector WorldReferenceNormal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Snap")
	FTransform SourceWorldTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Snap", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxDistance = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Snap")
	TObjectPtr<AEHBBuildingActorBase> Building = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Snap")
	TObjectPtr<AActor> IgnoredActor = nullptr;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSnapResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	bool bSnapped = false;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FTransform TargetWorldTransform = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldTangent = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FVector WorldNormal = FVector::RightVector;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	FEHBElementRelationEndpoint TargetEndpoint;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Surface|Snap")
	float Score = 0.0f;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceQueryParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Query")
	TObjectPtr<AEHBBuildingActorBase> Building = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Query")
	FVector WorldCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Query")
	FVector WorldExtent = FVector(50.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Query")
	TArray<EEHBArchitecturalSurfaceRole> RequiredRoles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Query")
	TArray<EEHBElementSurfaceKind> RequiredSurfaceKinds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Query")
	TObjectPtr<AActor> IgnoredActor = nullptr;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceMaterialApplyOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Material")
	bool bApplyAllSections = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Material")
	bool bPersistAsSurfaceOverride = true;
};
