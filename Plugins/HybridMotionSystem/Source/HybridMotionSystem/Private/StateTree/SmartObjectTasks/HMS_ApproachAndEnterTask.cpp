#include "StateTree/SmartObjectTasks/HMS_ApproachAndEnterTask.h"
#include "SmartObject/HMS_SmartObjectInteractionComponent.h"
#include "StateTreeExecutionContext.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

EStateTreeRunStatus FHMS_ApproachAndEnterTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{
	auto& Data = Context.GetInstanceData(*this);
	AActor* Actor = Cast<AActor>(Context.GetOwner());
	Data.Interaction = Actor ? Actor->FindComponentByClass<UHMS_SmartObjectInteractionComponent>() : nullptr;
	if (!Data.Interaction)
	{
		if (const APawn* Pawn=Cast<APawn>(Actor); Pawn && Pawn->GetController())
		{ Data.Interaction=Pawn->GetController()->FindComponentByClass<UHMS_SmartObjectInteractionComponent>(); }
	}
	return Data.Interaction && Data.Interaction->BeginQueriedEntry(Data.Profile) ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}
EStateTreeRunStatus FHMS_ApproachAndEnterTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime) const
{
	auto& Data = Context.GetInstanceData(*this);
	return Data.Interaction ? Data.Interaction->TickQueriedEntry(DeltaTime) : EStateTreeRunStatus::Failed;
}
void FHMS_ApproachAndEnterTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{
	auto& Data = Context.GetInstanceData(*this);
	if (Data.Interaction) { Data.Interaction->EndQueriedEntry(); }
}
