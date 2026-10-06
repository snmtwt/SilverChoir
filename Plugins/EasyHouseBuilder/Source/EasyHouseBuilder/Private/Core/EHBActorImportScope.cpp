#include "Core/EHBActorImportScope.h"

namespace { int32 ImportDepth = 0; }
FEHBActorImportScope::FEHBActorImportScope() { check(IsInGameThread()); ++ImportDepth; }
FEHBActorImportScope::~FEHBActorImportScope() { check(IsInGameThread() && ImportDepth > 0); --ImportDepth; }
bool FEHBActorImportScope::IsActive() { return ImportDepth > 0; }
