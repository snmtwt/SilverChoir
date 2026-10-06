#include "Map/BaseMap/BaseSandboxMap.h"

#include "Engine/GameInstance.h"
#include "GridStrategyMapSystem/Data/GSMMapData.h"
#include "GridStrategyMapSystem/Data/GSMMapSubsystem.h"
#include "GridStrategyMapSystem/Display3D/GSMPathArrow3D.h"

ABaseSandboxMap::ABaseSandboxMap()
{
    bUseDefaultMapData = false;
    MapId = TEXT("BaseSandbox");
    // Raise this on the placed instance only when its terrain materials support GSM bounds clipping.
    MaxMapScale = 1.0f;
}

void ABaseSandboxMap::BeginPlay()
{
    // Create data before GSM registers its display without claiming the default map slot.
    bUseDefaultMapData = false;
    MapGuid.Invalidate();
    if (UGameInstance* Instance = GetGameInstance())
    {
        if (UGSMMapSubsystem* Subsystem = Instance->GetSubsystem<UGSMMapSubsystem>())
        {
            if (MapConfig)
            {
                UGSMMapData* CreatedData = nullptr;
                OwnedMapGuid = Subsystem->CreateIndependentMapData(MapConfig, CreatedData);
                MapGuid = OwnedMapGuid;
            }
        }
    }
    Super::BeginPlay();
}

void ABaseSandboxMap::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    EndDragMap();
    ClearMapTiles();
    if (AGSMPathArrow3D* Arrow = GetPathArrowActor()) { Arrow->Destroy(); }
    // Detach the display before clearing only the runtime data created by this actor.
    Super::EndPlay(EndPlayReason);
    if (OwnedMapGuid.IsValid())
    {
        if (UGameInstance* Instance = GetGameInstance())
        {
            if (UGSMMapSubsystem* Subsystem = Instance->GetSubsystem<UGSMMapSubsystem>())
            {
                Subsystem->RemoveMapData(OwnedMapGuid);
            }
        }
    }
    OwnedMapGuid.Invalidate();
    MapGuid.Invalidate();
}
