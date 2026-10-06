#pragma once
#include "CoreMinimal.h"
#include "quickjs.h"
class FH5UI_CanvasElement;
JSValue H5UICanvasCall(JSContext* Context, FH5UI_CanvasElement& Canvas, int Count, JSValueConst* Args);
FString H5UICanvasBootstrap();
