#pragma once
#include "Map/BaseMap/RegionBlock.h"
#include "RegionClickTestActor.generated.h"

UCLASS(NotBlueprintable, Transient)
class ARegionClickTestActor : public ARegionBlock
{
	GENERATED_BODY()
public:
	int32 ClickCount=0;
	virtual void ReceiveRegionClicked_Implementation() override { ++ClickCount; }
};
