#pragma once
#include "CoreMinimal.h"
#include "EHBSurfaceRoomCoverage.generated.h"

struct FEHBFloorFinishRegion;
struct FEHBNodeRoomBoundary;

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceRoomShare
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="EasyHouse|Room") FGuid RoomGuid;
 UPROPERTY(BlueprintReadOnly,Category="EasyHouse|Room") double AreaCm2=0;
 UPROPERTY(BlueprintReadOnly,Category="EasyHouse|Room") double SurfaceFraction=0;
 UPROPERTY(BlueprintReadOnly,Category="EasyHouse|Room") double RoomFraction=0;
};

/** Spatial coverage, not a generation binding, support relation or visibility decision.
 * A surface may cover zero, one or several rooms on its assigned floor. */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBSurfaceRoomCoverage
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="EasyHouse|Room") FGuid ElementGuid;
 UPROPERTY(BlueprintReadOnly,Category="EasyHouse|Room") int32 FloorIndex=0;
 UPROPERTY(BlueprintReadOnly,Category="EasyHouse|Room") double AreaCm2=0;
 UPROPERTY(BlueprintReadOnly,Category="EasyHouse|Room") double OutsideAreaCm2=0;
 UPROPERTY(BlueprintReadOnly,Category="EasyHouse|Room") TArray<FEHBSurfaceRoomShare> Rooms;
};

struct EASYHOUSEBUILDER_API FEHBSurfaceRoomCoverageSolver
{
 // Input polygons are building-local XY footprints. Z is deliberately ignored;
 // floor assignment separates stories. Callers must validate horizontal surfaces.
 // Union of surface regions avoids double counting. Failure clears all output.
 static bool Build(FGuid ElementGuid,int32 FloorIndex,const TArray<FEHBFloorFinishRegion>& Regions,
  const TArray<FEHBNodeRoomBoundary>& Rooms,FEHBSurfaceRoomCoverage& Out,FName& Status);
};
