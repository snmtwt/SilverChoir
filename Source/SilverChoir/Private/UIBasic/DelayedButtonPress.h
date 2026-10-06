#pragma once

#include "CoreMinimal.h"

/** Frame-based input state shared by mouse and keyboard; independent of frame rate. */
struct FDelayedButtonPress
{
	bool bActive = false;
	bool bReleased = false;
	bool bPressedVisual = false;
	uint64 StartFrame = 0;
	uint64 VisualFrame = 0;

	bool Begin(uint64 Frame)
	{
		if (bActive) { return false; }
		bActive = true;
		bReleased = false;
		bPressedVisual = false;
		StartFrame = Frame;
		return true;
	}

	void Release(bool bInside)
	{
		if (!bInside) { Cancel(); }
		else if (bActive) { bReleased = true; }
	}

	/** Returns true once, after a valid release and at least one frame of pressed feedback. */
	bool Advance(uint64 Frame)
	{
		if (!bActive) { return false; }
		if (!bPressedVisual && Frame >= StartFrame + 2)
		{
			bPressedVisual = true;
			VisualFrame = Frame;
		}
		if (bPressedVisual && bReleased && Frame > VisualFrame)
		{
			Cancel();
			return true;
		}
		return false;
	}

	void Cancel() { *this = FDelayedButtonPress(); }
};
