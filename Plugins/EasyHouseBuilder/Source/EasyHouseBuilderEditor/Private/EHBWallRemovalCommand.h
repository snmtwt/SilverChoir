#pragma once
#include "CoreMinimal.h"
class AEHBBuildingActorBase;
struct FEHBToolsetOperationResult;

namespace EHBWallRemovalCommand
{
 FEHBToolsetOperationResult Execute(AEHBBuildingActorBase* Building,const TArray<FGuid>& WallGuids,int32 ExpectedGraphRevision,bool bPreview);
#if WITH_DEV_AUTOMATION_TESTS
 inline int32 FailurePhase=0;
#endif
}
