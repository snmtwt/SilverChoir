#pragma once
#include "CoreMinimal.h"
#include "Core/EHBWallNodeCopy.h"
class AEHBBuildingActorBase;
class AActor;

struct FEHBBuildingCopyResult
{
	bool bSucceeded = false;
	FName Status;
	FName FailureReason;
	AEHBBuildingActorBase* Building = nullptr;
	TArray<AActor*> Actors;
	/** Published only after the imported group passes final validation. */
	FEHBWallNodeCopyDraft IdentityDraft;
};

/** Complete native wall/pillar copy with validated full-room floors and ceiling slabs. Owns its editor transaction. */
namespace EHBBuildingCopy
{
	FEHBBuildingCopyResult Execute(AEHBBuildingActorBase* Source, const FVector& WorldOffset);
	FVector SuggestedWorldOffset(const AEHBBuildingActorBase* Source);
	/** Level Editor Duplicate command bridge; does not change raw actor import or clipboard paste. */
	void RegisterDuplicateCommand();
	void UnregisterDuplicateCommand();
#if WITH_DEV_AUTOMATION_TESTS
	inline bool FailAfterImport = false;
	inline bool FailAfterApply = false;
#endif
}
