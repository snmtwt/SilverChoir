#include "SubSystem/GameMapTransitionSystem/GameMapNavigationSubsystem.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/NavMeshBoundsVolume.h"

bool UGameMapNavigationSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type==EWorldType::Game || Type==EWorldType::PIE;
}
void UGameMapNavigationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    auto* Maps=Collection.InitializeDependency<UMTS_SubMapSubsystem>();
    Maps->OnSubMapStateChanged.AddUniqueDynamic(this,&ThisClass::HandleMapState);
    Maps->OnSubMapVisibilityChanged.AddUniqueDynamic(this,&ThisClass::HandleMapVisibility);
}
void UGameMapNavigationSubsystem::Deinitialize()
{
    if(auto* Maps=GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>())
    {
        Maps->OnSubMapStateChanged.RemoveDynamic(this,&ThisClass::HandleMapState);
        Maps->OnSubMapVisibilityChanged.RemoveDynamic(this,&ThisClass::HandleMapVisibility);
    }
    Super::Deinitialize();
}
void UGameMapNavigationSubsystem::HandleMapState(const FMTS_SubMapInfo& Map,const FText& Error)
{
    RefreshMissingNavigation(Map);
}
void UGameMapNavigationSubsystem::HandleMapVisibility(const FMTS_SubMapInfo& Map)
{
    RefreshMissingNavigation(Map);
}
void UGameMapNavigationSubsystem::RefreshMissingNavigation(const FMTS_SubMapInfo& Map)
{
    if(Map.State!=EMTS_SubMapState::Loaded || !Map.bIsVisible || !Map.StreamingLevel)return;
    auto* Level=Map.StreamingLevel->GetLoadedLevel();
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if(!Level || !Level->bIsVisible || !Nav || IsValid(Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)))return;
    // UE may process bounds before visibility, skip auto-creation because a sublevel
    // Recast actor is pending registration, then discard that actor when it becomes
    // visible. Resubmit after visibility to let UE create persistent navigation data
    // and populate its geometry through the normal asynchronous generation pipeline.
    int32 Count=0;
    for(AActor* Actor:Level->Actors)
        if(auto* Bounds=Cast<ANavMeshBoundsVolume>(Actor))
        {
            Nav->OnNavigationBoundsUpdated(Bounds);
            ++Count;
        }
    if(Count>0) UE_LOG(LogTemp,Display,TEXT("GameMapNavigation: requeued %d bounds after visibility for %s"),Count,*Map.MapID.ToString());
}
