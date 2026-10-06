#pragma once
#include "CoreMinimal.h"
#include "SMS_SceneBase.h"
#include "OperationsCommandRoomScene.generated.h"

/** Compatibility type for historical asset imports. Current rooms inherit SMS_SceneBase directly. */
UCLASS(NotBlueprintable)
class SILVERCHOIR_API UOperationsCommandRoomScene : public USMS_SceneBase
{
    GENERATED_BODY()
};