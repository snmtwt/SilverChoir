#pragma once
#include "CoreMinimal.h"

struct FMenuExitSequence
{
	static TArray<int32> Order(int32 Selected)
	{
		if (Selected < 0 || Selected > 3) { return {}; }
		TArray<int32> Result;
		for (int32 Index = 3; Index >= 0; --Index) { if (Index != Selected) { Result.Add(Index); } }
		Result.Add(Selected);
		return Result;
	}
	static bool ShouldStart(uint64 StartFrame, uint64 CurrentFrame, int32 Rank)
	{
		return CurrentFrame >= StartFrame && CurrentFrame - StartFrame >= static_cast<uint64>(Rank * 2);
	}
};
