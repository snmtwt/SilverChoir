#pragma once
#include "CoreMinimal.h"

/** Wall-local structural prism. Left is +Thickness/2, right is -Thickness/2. */
struct FEHBWallOpeningMeasureDomain
{
 double StartLeft=0,StartRight=0,EndLeft=0,EndRight=0,Height=0,Thickness=0;
};

/** Independent area oracle: analytically integrate admissible edge parameters
 * across wall thickness, without reading or constructing a triangle mesh.
 * Input loops are the normalized opening union boundaries, including holes.
 * Output is physical single-sided reveal area; boundary exits add no face. */
struct EASYHOUSEBUILDER_API FEHBWallOpeningMeasure
{
 static bool RevealArea(const FEHBWallOpeningMeasureDomain& Domain,
  const TArray<TArray<FVector2d>>& UnionBoundaries,double& OutArea,FName& Status);
};
