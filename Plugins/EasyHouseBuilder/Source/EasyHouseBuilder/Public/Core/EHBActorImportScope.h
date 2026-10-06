#pragma once
#include "CoreMinimal.h"

/** Synchronous, game-thread-only staging for the explicit whole-building copy command.
 * Not installed on arbitrary editor paste. Imported actors retain their serialized IDs
 * until the caller publishes one complete mapping. Never span a frame or user callback. */
class EASYHOUSEBUILDER_API FEHBActorImportScope
{
public:
	FEHBActorImportScope();
	~FEHBActorImportScope();
	FEHBActorImportScope(const FEHBActorImportScope&) = delete;
	FEHBActorImportScope& operator=(const FEHBActorImportScope&) = delete;
	static bool IsActive();
};
