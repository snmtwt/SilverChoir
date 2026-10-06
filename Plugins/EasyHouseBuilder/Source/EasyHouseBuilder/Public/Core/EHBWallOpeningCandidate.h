#pragma once
#include "Core/EHBWallSurfaceHosts.h"
#include "Core/EHBPreparedWallOpening.h"

struct EASYHOUSEBUILDER_API FEHBWallOpeningCandidateSource
{
 FEHBWallSurfaceHostSource Host;
 FTransform BuildingToWorld=FTransform::Identity;
 bool bGenerateStartCap=true,bGenerateEndCap=true;
 // Existing door/window connection outlines in candidate wall-local XZ.
 // Derived values only; preserve legacy base extension and triangulation input.
 TArray<TArray<FVector2d>> ConnectionOpenings;
};

struct EASYHOUSEBUILDER_API FEHBWallOpeningCandidate
{
 /** Complete source-driven preview, including independent coverage/reveal checks
  * and building-local contact faces. Never moves an Actor or assigns live revisions.
  * Failure clears Out; Candidate may alias Out.Sources. Not a commit token. */
 static bool Build(const FEHBWallOpeningCandidateSource& Source,
  const TArray<FEHBCutOperation>& Candidate,FEHBPreparedWallOpening& Out,FName& Status);
};
