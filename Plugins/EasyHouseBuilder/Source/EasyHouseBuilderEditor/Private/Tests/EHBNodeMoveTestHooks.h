#pragma once

// Private development seam; not available through Blueprint, console or MCP.
#if WITH_DEV_AUTOMATION_TESTS
namespace EHBNodeMoveTestHooks
{
	enum class EFailurePhase : uint8 { None, AfterFloors, AfterSlabs, AfterContacts, AfterBaseline, AfterEditRecord };
	extern EFailurePhase FailurePhase;
	extern int32 CandidateJunctionSolveCount;
	extern int32 PreparedDefinitionReuseCount;
	extern int32 DefinitionBatchSolveCount;
	inline bool ConsumeFailure(EFailurePhase Phase)
	{
		if (FailurePhase != Phase) return false;
		FailurePhase = EFailurePhase::None;
		return true;
	}
}
#endif
