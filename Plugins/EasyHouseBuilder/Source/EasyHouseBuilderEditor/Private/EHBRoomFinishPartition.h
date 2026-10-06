#pragma once
#include "CoreMinimal.h"

namespace EHBRoomFinishPartition
{
 struct FSource
 {
  FGuid ElementGuid;
  TArray<FVector> OwnerRoomPolygon;
  TArray<FVector> OriginalPolygon;
 };
 // Value-only XY partition in building coordinates. The original room boundary
 // allocates the removed-wall strip, independently of actor order or area rank.
 bool Build(const TArray<FVector>& Destination,const TArray<FSource>& Sources,
  TMap<FGuid,TArray<FVector>>& Out,FName& Status);
}
