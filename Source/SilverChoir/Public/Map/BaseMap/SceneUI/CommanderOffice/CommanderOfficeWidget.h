#pragma once
#include "CoreMinimal.h"
#include "Map/BaseMap/SceneUI/BaseSceneWidget.h"
#include "CommanderOfficeWidget.generated.h"

/** 团长办公室：在子蓝图中制作内容布局与业务逻辑。 */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="团长办公室 UI"))
class SILVERCHOIR_API UCommanderOfficeWidget : public UBaseSceneWidget
{
    GENERATED_BODY()
public:
    UCommanderOfficeWidget(const FObjectInitializer& Initializer) : Super(Initializer)
    {
        SceneTitle = NSLOCTEXT("BaseSceneUI", "CommanderOffice", "团长办公室");
    }
};
