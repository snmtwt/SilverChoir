#pragma once
#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "HMS_ApproachAndEnterTask.generated.h"
class UHMS_SmartObjectInteractionProfile;
class UHMS_SmartObjectInteractionComponent;

USTRUCT()
struct HYBRIDMOTIONSYSTEM_API FHMS_ApproachAndEnterTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Parameter")
	TObjectPtr<UHMS_SmartObjectInteractionProfile> Profile;
	UPROPERTY(Transient)
	TObjectPtr<UHMS_SmartObjectInteractionComponent> Interaction;
};

/** Runs navigation and pose search concurrently, then hands moving control to the selected montage. */
USTRUCT(meta=(DisplayName="HMS Approach and Enter (Pose Search)", Category="HMS|Smart Object|Interaction"))
struct HYBRIDMOTIONSYSTEM_API FHMS_ApproachAndEnterTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FHMS_ApproachAndEnterTaskInstanceData;
	FHMS_ApproachAndEnterTask() { bShouldCallTick = true; bShouldCopyBoundPropertiesOnTick = false; }
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};
