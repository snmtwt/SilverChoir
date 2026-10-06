#include "Components/EHBRoomTrackerComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UEHBRoomTrackerComponent::UEHBRoomTrackerComponent()
{
 PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=0.1f;
}
void UEHBRoomTrackerComponent::BeginPlay() { Super::BeginPlay();RefreshRoom(0,true); }
void UEHBRoomTrackerComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
 Super::TickComponent(Delta,Type,Function);RefreshRoom(Delta,false);
}
void UEHBRoomTrackerComponent::RefreshRoom(float Delta,bool Immediate)
{
 if(!GetOwner()||!GetWorld()||!FMath::IsFinite(Delta)||Delta<0)return;
 auto* Rooms=GetWorld()->GetSubsystem<UEHBRoomRuntimeSubsystem>();if(!Rooms)return;
 const auto Candidate=Rooms->QueryLocation(GetOwner()->GetActorTransform().TransformPosition(LocalProbeOffset),CurrentRoom);
 if(Candidate.Status==EEHBRoomLocationStatus::Invalid){PendingSeconds=0;return;}
 if(Candidate==CurrentRoom){Pending=Candidate;PendingSeconds=0;}
 else
 {
  if(!(Candidate==Pending)){Pending=Candidate;PendingSeconds=0;}
  PendingSeconds+=Delta;
  if(Immediate||(CurrentRoom.Status==EEHBRoomLocationStatus::Inside&&!IsValid(CurrentRoom.Building))||PendingSeconds>=FMath::Max(0.f,TransitionDelay))
  {
   const auto Previous=CurrentRoom;CurrentRoom=Candidate;PendingSeconds=0;OnRoomChanged.Broadcast(Previous,CurrentRoom);OnRoomChangedNative.Broadcast(Previous,CurrentRoom);
  }
 }
 if(bRememberVisitedRooms&&CurrentRoom.Status==EEHBRoomLocationStatus::Inside)Rooms->MarkExplored(ExplorationObserver,CurrentRoom.Identity);
}
