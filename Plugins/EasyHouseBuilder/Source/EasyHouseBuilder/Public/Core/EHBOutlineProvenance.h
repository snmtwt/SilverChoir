#pragma once
#include "CoreMinimal.h"
#include "EHBOutlineProvenance.generated.h"

/** Generation history, not an automatic-follow permission. Old assets remain unclassified. */
UENUM(BlueprintType)
enum class EEHBOutlineSource : uint8
{
	ManualOrUnclassified UMETA(DisplayName="手动或旧版未确认"),
	RoomBoundary UMETA(DisplayName="房间轮廓生成"),
	RoomSupportFill UMETA(DisplayName="房间与支撑填充"),
	/** Authored building-local outline retained after an enclosure is opened.
	 * No live room or wall anchor; later enclosure edits preserve this region. */
	RetainedRegion UMETA(DisplayName="拆墙后保留的独立区域")
};
