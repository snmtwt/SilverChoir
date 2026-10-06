#pragma once

// Private development-test seam. Not a console variable, Blueprint or MCP API.
#if WITH_DEV_AUTOMATION_TESTS
namespace EHBWallSplitTestHooks
{
	enum class EFailurePhase : uint8 { None, AfterGeometry, AfterInheritedSlabSpawn, AfterInheritedFloorSpawn, AfterBaseline, AfterRailingSpawn, AfterEditRecord };
	extern EFailurePhase FailurePhase;
	inline bool ConsumeFailure(EFailurePhase Phase)
	{
		if (FailurePhase != Phase) return false;
		FailurePhase = EFailurePhase::None;
		return true;
	}
}
#endif
