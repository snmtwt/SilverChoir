#pragma once

#include "Commandlets/Commandlet.h"
#include "RaisedImageButtonAssetsCommandlet.generated.h"

/** Creates/validates the reusable image button, or explicitly upgrades its owned arrow textures. */
UCLASS()
class URaisedImageButtonAssetsCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    URaisedImageButtonAssetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
