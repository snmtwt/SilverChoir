#include "Components/EHBLocalBuildingViewComponent.h"

#include "Actors/EHBElementActorBase.h"
#include "Components/EHBRoomTrackerComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "GameFramework/PlayerController.h"

UEHBLocalBuildingViewComponent::UEHBLocalBuildingViewComponent()
{
 PrimaryComponentTick.bCanEverTick=true;
 PrimaryComponentTick.TickInterval=0.1f;
 bAutoActivate=true;
}

bool UEHBLocalBuildingViewComponent::SetTrackedUnit(AActor* Unit)
{
 ResetView();
 if(auto* Previous=Tracker.Get()) RemoveTickPrerequisiteComponent(Previous);
 Tracker.Reset();
 if(!Unit) { LastStatus=TEXT("NoTarget"); return true; }
 if(!IsValid(Unit) || Unit->IsActorBeingDestroyed() || Unit->GetWorld()!=GetWorld())
 { LastStatus=TEXT("InvalidTarget"); return false; }
 auto* NewTracker=Unit->FindComponentByClass<UEHBRoomTrackerComponent>();
 if(!NewTracker) { LastStatus=TEXT("MissingRoomTracker"); return false; }
 Tracker=NewTracker;
 AddTickPrerequisiteComponent(NewTracker);
 RefreshView();
 return true;
}

void UEHBLocalBuildingViewComponent::ApplyHiddenComponents(APlayerController* Controller, const TSet<TWeakObjectPtr<UPrimitiveComponent>>& Desired)
{
 if(AppliedController.Get()!=Controller) ResetView();
 AppliedController=Controller;
 for(auto It=AddedHiddenComponents.CreateIterator(); It; ++It)
 {
  if(!Desired.Contains(*It) || !It->IsValid())
  {
   Controller->HiddenPrimitiveComponents.RemoveSingle(*It);
   It.RemoveCurrent();
  }
 }
 for(const auto& Component:Desired)
 {
  if(!Controller->HiddenPrimitiveComponents.Contains(Component))
  {
   Controller->HiddenPrimitiveComponents.Add(Component);
   AddedHiddenComponents.Add(Component);
  }
 }
}

void UEHBLocalBuildingViewComponent::ResetView()
{
 if(auto* Controller=AppliedController.Get())
  for(const auto& Component:AddedHiddenComponents) Controller->HiddenPrimitiveComponents.RemoveSingle(Component);
 AddedHiddenComponents.Reset();
 AppliedController.Reset();
 HiddenElementCount=0;
}

void UEHBLocalBuildingViewComponent::RefreshView()
{
 auto Clear=[this](FName Status){ResetView(); LastStatus=Status;};
 auto* Controller=Cast<APlayerController>(GetOwner());
 if(!Controller) { Clear(TEXT("RequiresPlayerController")); return; }
 if(!bViewEnabled) { Clear(TEXT("Disabled")); return; }
 if(!Controller->IsLocalController() || Controller->GetNetMode()==NM_DedicatedServer)
 { Clear(TEXT("NotLocalView")); return; }
 // Multiple controllers are independent; multiple writers on one controller are not.
 if(Controller->FindComponentByClass<UEHBLocalBuildingViewComponent>()!=this)
 { Clear(TEXT("DuplicateViewComponent")); return; }
 auto* Source=Tracker.Get();
 if(!Source || !IsValid(Source->GetOwner()) || Source->GetOwner()->IsActorBeingDestroyed() || !Source->IsRegistered())
 { Clear(TEXT("NoTarget")); return; }
 const auto& Room=Source->CurrentRoom;
 auto* Building=Room.Building.Get();
 if(Room.Status!=EEHBRoomLocationStatus::Inside || !IsValid(Building) || Building->IsActorBeingDestroyed())
 { Clear(TEXT("Outside")); return; }

 // Scan only the focused building at the component interval, not all buildings
 // or every unit/camera frame. No geometry rebuild occurs when the view changes.
 TSet<TWeakObjectPtr<UPrimitiveComponent>> Desired;
 int32 ElementsHidden=0;
 for(auto* Element:Building->QueryElements(FEHBElementQuery()))
 {
  if(!IsValid(Element) || Element->IsActorBeingDestroyed()) continue;
  const bool Roof=Element->ElementType==EEHBBuildingElementType::Roof || Element->FloorRole==EEHBBuildingFloorElementRole::Roof;
  const bool Hide=(bHideRoofs && Roof)
   || (bHideHigherFloors && Element->FloorIndex>Room.FloorIndex)
   || (bHideCurrentCeiling && Element->FloorIndex==Room.FloorIndex && Element->FloorRole==EEHBBuildingFloorElementRole::FloorCeiling);
  if(!Hide) continue;
  ++ElementsHidden;
  TInlineComponentArray<UPrimitiveComponent*> Components;
  Element->GetComponents(Components,true);
  for(auto* Component:Components) if(IsValid(Component) && Component->IsRegistered()) Desired.Add(Component);
 }
 ApplyHiddenComponents(Controller,Desired);
 HiddenElementCount=ElementsHidden;
 LastStatus=TEXT("Applied");
}

void UEHBLocalBuildingViewComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
 RefreshView();
}

void UEHBLocalBuildingViewComponent::Deactivate()
{
 ResetView();
 Super::Deactivate();
}

void UEHBLocalBuildingViewComponent::OnUnregister()
{
 ResetView();
 Super::OnUnregister();
}

void UEHBLocalBuildingViewComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
 ResetView();
 Super::EndPlay(EndPlayReason);
}
