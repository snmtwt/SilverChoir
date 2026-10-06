#pragma once
#include "CoreMinimal.h"
#include "Cutting/EHBCutTypes.h"
class AEHB_Wall;
struct FEHBToolsetOperationResult;

struct FEHBWallOpeningCommand
{
 static FEHBToolsetOperationResult Execute(AEHB_Wall* Wall,const TArray<FEHBCutOperation>& Cuts,int32 Graph,int32 Geometry,bool Preview);
#if WITH_DEV_AUTOMATION_TESTS
 inline static int32 FailurePhase=0;
#endif
};
