#pragma once

#include "Engine/EngineTypes.h"

// Must match BaseRegion in DefaultEngine.ini. Buildings ignore this query channel.
namespace RegionInteraction
{
    inline constexpr ECollisionChannel TraceChannel = ECC_GameTraceChannel2;
}
