#pragma once
#include "CoreMinimal.h"
#include "EHBLogicalSurface.generated.h"

/** Persistent identity of a semantic port, owned by the element rather than a render component.
 * Always qualify a legacy SurfaceGuid with its ElementGuid; old assets may reuse component GUIDs. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBLogicalSurfaceIdentity
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") FName Name;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") int32 SubIndex=INDEX_NONE;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") FGuid SurfaceGuid;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBLogicalSurfaceHole
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Surface") TArray<FVector2D> Vertices;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBLogicalSurfaceRegion
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Surface") TArray<FVector2D> Boundary;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Surface") TArray<FEHBLogicalSurfaceHole> Holes;
};

/** Value geometry before external opening operations and visual offsets/expansion.
 * PlaneToBuilding is a rigid right-handed frame; X/Y coordinates and thickness are cm.
 * Holes are authored base holes. Querying this definition does not imply an opening
 * command or structural support solver has accepted the surface as a host. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBLogicalSurfaceDefinition
{
 GENERATED_BODY()
 /** Empty for an unattached element, in which case PlaneToBuilding is in world space. */
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") FGuid BuildingGuid;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") FGuid ElementGuid;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") FGuid SurfaceGuid;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") FName SourceName;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") int32 SourceSubIndex=INDEX_NONE;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") int32 FloorIndex=INDEX_NONE;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Surface") FTransform PlaneToBuilding=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Surface") TArray<FEHBLogicalSurfaceRegion> Regions;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Surface") double Thickness=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") bool bStructural=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Surface") bool bCanSupport=false;

 /** Strict planar-domain validation; no triangulation or renderer is required. */
 bool Validate(FName& Status) const;
 double GetAreaCm2() const;
 FVector ToBuilding(const FVector2D& Point) const{return PlaneToBuilding.TransformPosition(FVector(Point.X,Point.Y,0));}
 /** Orthogonal projection; signed distance follows the plane normal. */
 bool Project(const FVector& BuildingPoint,FVector2D& PlanePoint,double& SignedDistance) const;
 bool ContainsProjectedPoint(const FVector2D& Point) const;
};
