// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Definitions/EHBBuildingTypes.h"
#include "EHBElementRelations.generated.h"

class AActor;

/**
 * Stable behavioral capabilities used by generic element queries.
 * Element type answers "what is it"; capabilities answer "what may it do".
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EEHBElementCapability : uint8
{
	None = 0 UMETA(Hidden),
	Structural = 1 << 0 UMETA(DisplayName = "Structural"),
	CanSupport = 1 << 1 UMETA(DisplayName = "Can Support"),
	RequiresSupport = 1 << 2 UMETA(DisplayName = "Requires Support"),
	RoomBoundary = 1 << 3 UMETA(DisplayName = "Can Bound Rooms"),
	CanHost = 1 << 4 UMETA(DisplayName = "Can Host Elements"),
	HostedElement = 1 << 5 UMETA(DisplayName = "Hosted Element"),
	SurfaceFinish = 1 << 6 UMETA(DisplayName = "Surface Finish"),
	BoundaryAccessory = 1 << 7 UMETA(DisplayName = "Boundary Accessory")
};
ENUM_CLASS_FLAGS(EEHBElementCapability);

/**
 * Relation direction is always Source -> Target.
 * For example, a structural support relation reads "Source supports Target".
 */
UENUM(BlueprintType)
enum class EEHBElementRelationType : uint8
{
	None UMETA(DisplayName = "None"),
	TopologyConnection UMETA(DisplayName = "Topology Connection"),
	StructuralSupport UMETA(DisplayName = "Structural Support"),
	HostedElement UMETA(DisplayName = "Hosted Element"),
	PhysicalContact UMETA(DisplayName = "Physical Contact"),
	SurfaceFinish UMETA(DisplayName = "Surface Finish"),
	BoundaryAttachment UMETA(DisplayName = "Boundary Attachment"),
	LogicalDependency UMETA(DisplayName = "Logical Dependency")
};

UENUM(BlueprintType)
enum class EEHBRelationEndpointKind : uint8
{
	BuildingElement UMETA(DisplayName = "Building Element"),
	ExternalActor UMETA(DisplayName = "External Actor"),
	WorldGround UMETA(DisplayName = "World Ground"),
	/** Reserved for explicit node-authority migration; not an element or a physical host. */
	WallNode UMETA(Hidden, DisplayName = "Wall Connection Node")
};

/** Common semantic surfaces. SurfaceName remains available for element-specific ports. */
UENUM(BlueprintType)
enum class EEHBElementSurfaceKind : uint8
{
	WholeElement UMETA(DisplayName = "Whole Element"),
	Top UMETA(DisplayName = "Top"),
	Bottom UMETA(DisplayName = "Bottom"),
	Side UMETA(DisplayName = "Side"),
	LeftSide UMETA(DisplayName = "Left Side"),
	RightSide UMETA(DisplayName = "Right Side"),
	Start UMETA(DisplayName = "Start"),
	End UMETA(DisplayName = "End"),
	Perimeter UMETA(DisplayName = "Perimeter"),
	Opening UMETA(DisplayName = "Opening"),
	Custom UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class EEHBRelationOrigin : uint8
{
	UserAuthored UMETA(DisplayName = "User Authored"),
	AutoDetected UMETA(DisplayName = "Auto Detected"),
	SystemGenerated UMETA(DisplayName = "System Generated"),
	ImportedLegacy UMETA(DisplayName = "Imported Legacy")
};

UENUM(BlueprintType)
enum class EEHBRelationQueryDirection : uint8
{
	Outgoing UMETA(DisplayName = "Outgoing"),
	Incoming UMETA(DisplayName = "Incoming"),
	Both UMETA(DisplayName = "Both")
};

UENUM(BlueprintType)
enum class EEHBFloorAssignmentPolicy : uint8
{
	Automatic UMETA(DisplayName = "Automatic"),
	Explicit UMETA(DisplayName = "Explicit")
};

UENUM(BlueprintType)
enum class EEHBFloorAssignmentSource : uint8
{
	Unassigned UMETA(DisplayName = "Unassigned"),
	Explicit UMETA(DisplayName = "Explicit"),
	DerivedFromSupport UMETA(DisplayName = "Derived From Support"),
	ImportedLegacy UMETA(DisplayName = "Imported Legacy")
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBElementRelationEndpoint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	EEHBRelationEndpointKind Kind = EEHBRelationEndpointKind::BuildingElement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	FGuid ElementGuid;

	/** Separate identity namespace; legacy endpoints default to an invalid node ID. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Relation")
	FGuid NodeGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	TSoftObjectPtr<AActor> ExternalActor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	EEHBElementSurfaceKind SurfaceKind = EEHBElementSurfaceKind::WholeElement;

	/** Element-specific stable port name, such as Wall.Start, Railing.Post.3 or Slab.Top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	FName SurfaceName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	int32 SubIndex = INDEX_NONE;

	bool IsValid() const;
	bool RefersToElement(const FGuid& InElementGuid) const;
	bool RefersToNode(const FGuid& InNodeGuid) const;
	bool IsEquivalentTo(const FEHBElementRelationEndpoint& Other) const;

	static FEHBElementRelationEndpoint MakeNode(const FGuid& InNodeGuid);

	static FEHBElementRelationEndpoint MakeElement(
		const FGuid& InElementGuid,
		EEHBElementSurfaceKind InSurfaceKind = EEHBElementSurfaceKind::WholeElement,
		FName InSurfaceName = NAME_None,
		int32 InSubIndex = INDEX_NONE);
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBElementRelation
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Relation")
	FGuid RelationGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	EEHBElementRelationType Type = EEHBElementRelationType::None;

	/** Relation semantics read from Source to Target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	FEHBElementRelationEndpoint Source;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	FEHBElementRelationEndpoint Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	EEHBRelationOrigin Origin = EEHBRelationOrigin::UserAuthored;

	/** Geometry-dependent relations become stale after either endpoint changes shape or transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	bool bGeometryDependent = false;

	/** Structural support relations may opt out of automatic floor propagation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	bool bAffectsFloorAssignment = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation")
	bool bEnabled = true;

	/** Contact data uses building-local space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation|Contact")
	FVector ContactPoint = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation|Contact")
	FVector ContactNormal = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation|Contact", meta = (ClampMin = "0.0"))
	float ContactArea = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation|Contact")
	FTransform TargetRelativeToSource = FTransform::Identity;

	/** Extensible payload for values such as distance along a host wall or coverage ratio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation|Metadata")
	TMap<FName, double> NumericMetadata;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Relation|Metadata")
	TMap<FName, FString> StringMetadata;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Relation|Validation")
	int32 SourceGeometryRevision = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Relation|Validation")
	int32 TargetGeometryRevision = 0;

	bool IsEquivalentTo(const FEHBElementRelation& Other) const;
	bool InvolvesElement(const FGuid& ElementGuid) const;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBRelationQuery
{
	GENERATED_BODY()

	/** Invalid ElementGuid and NodeGuid means all endpoints; setting both rejects the query. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	FGuid ElementGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	FGuid NodeGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	EEHBRelationQueryDirection Direction = EEHBRelationQueryDirection::Both;

	/** Empty means all relation types. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	TArray<EEHBElementRelationType> Types;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	TArray<EEHBRelationOrigin> Origins;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	TArray<EEHBElementSurfaceKind> SourceSurfaceKinds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	TArray<EEHBElementSurfaceKind> TargetSurfaceKinds;

	/** NAME_None means any source surface name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	FName SourceSurfaceName = NAME_None;

	/** NAME_None means any target surface name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	FName TargetSurfaceName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	bool bIncludeDisabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	bool bIncludeStale = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	bool bGeometryDependentOnly = false;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBElementQuery
{
	GENERATED_BODY()

	/** INDEX_NONE means all floors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	int32 FloorIndex = INDEX_NONE;

	/** Empty means all element types. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	TArray<EEHBBuildingElementType> ElementTypes;

	/** Empty means all floor roles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	TArray<EEHBBuildingFloorElementRole> FloorRoles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query", meta = (Bitmask, BitmaskEnum = "/Script/EasyHouseBuilder.EEHBElementCapability"))
	int32 RequiredCapabilities = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query", meta = (Bitmask, BitmaskEnum = "/Script/EasyHouseBuilder.EEHBElementCapability"))
	int32 ExcludedCapabilities = 0;

	/** Every requested semantic tag must be present on the element. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query")
	TArray<FName> RequiredSemanticTags;
};

UENUM(BlueprintType)
enum class EEHBRelationValidationSeverity : uint8
{
	Info UMETA(DisplayName = "Info"),
	Warning UMETA(DisplayName = "Warning"),
	Error UMETA(DisplayName = "Error")
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBRelationValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	EEHBRelationValidationSeverity Severity = EEHBRelationValidationSeverity::Error;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	FGuid RelationGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Validation")
	FString Message;
};
