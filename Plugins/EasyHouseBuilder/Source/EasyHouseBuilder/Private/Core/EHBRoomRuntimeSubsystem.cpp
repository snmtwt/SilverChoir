#include "Core/EHBRoomRuntimeSubsystem.h"
#include "Core/EHBRoomExplorationSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Actors/EHB_Wall.h"
#include "Core/EHBBuildingActorBase.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void UEHBRoomRuntimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
 SpawnHandle=GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this,&UEHBRoomRuntimeSubsystem::OnActorSpawned));
 for(TActorIterator<AEHBBuildingActorBase> It(GetWorld());It;++It)RegisterBuilding(*It);
}
void UEHBRoomRuntimeSubsystem::PostInitialize()
{
 Super::PostInitialize();
 // Loaded actors may not yet be iterable during Initialize; spawned actors use the delegate.
 for(TActorIterator<AEHBBuildingActorBase> It(GetWorld());It;++It)RegisterBuilding(*It);
}
void UEHBRoomRuntimeSubsystem::Deinitialize()
{
 if(GetWorld()&&SpawnHandle.IsValid())GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
 SpawnHandle.Reset();Buildings.Reset();Exploration.Reset();Super::Deinitialize();
}
void UEHBRoomRuntimeSubsystem::OnActorSpawned(AActor* Actor) { RegisterBuilding(Cast<AEHBBuildingActorBase>(Actor)); }
void UEHBRoomRuntimeSubsystem::RegisterBuilding(AEHBBuildingActorBase* B)
{
 if(IsValid(B)&&B->GetWorld()==GetWorld()&&!Buildings.ContainsByPredicate([&](const FEntry& E){return E.Building.Get()==B;}))Buildings.AddDefaulted_GetRef().Building=B;
}
FEHBRoomLocation UEHBRoomRuntimeSubsystem::QueryLocation(FVector Position,const FEHBRoomLocation& Previous)
{
 FEHBRoomLocation Result;
 if(Position.ContainsNaN()){Result.Status=EEHBRoomLocationStatus::Invalid;return Result;}
 Buildings.RemoveAllSwap([](const FEntry& E){return !E.Building.IsValid()||E.Building->IsActorBeingDestroyed();});
 TArray<FEHBRoomLocation> Candidates;
 for(auto& Entry:Buildings)
 {
  auto* B=Entry.Building.Get();
  if(B->WallNodeAuthority.Version==2)
  {
   if(Entry.Revision!=B->RelationshipGraphRevision||!GetWorld()->IsGameWorld())
   {
    Entry.Bounds=FBox(ForceInit);for(const auto& Node:B->WallNodeAuthority.Nodes)Entry.Bounds+=Node.LocalTransform.GetLocation();Entry.Revision=B->RelationshipGraphRevision;
   }
   const auto Local=B->GetActorTransform().InverseTransformPosition(Position);
   if(Entry.Bounds.IsValid&&(Local.X<Entry.Bounds.Min.X-.5||Local.X>Entry.Bounds.Max.X+.5||Local.Y<Entry.Bounds.Min.Y-.5||Local.Y>Entry.Bounds.Max.Y+.5))continue;
  }
  const FVector QueryLocal=B->GetActorTransform().InverseTransformPosition(Position);
  for(const auto& Room:B->FindClosedLoopsContainingWorldLocation(Position))
  {
   // The authoring query deliberately chooses a nearest floor outside its height range.
   // A runtime occupant must actually lie between this room's authored wall bases/tops.
   double Bottom=TNumericLimits<double>::Max(),Top=-TNumericLimits<double>::Max();bool Complete=!Room.WallGuids.IsEmpty();
   for(FGuid WallId:Room.WallGuids)
   {
    const auto* Wall=Cast<AEHB_Wall>(B->FindElementActorByGuid(WallId));if(!Wall){Complete=false;break;}
    Bottom=FMath::Min(Bottom,FMath::Min(Wall->LocalStart.Z,Wall->LocalEnd.Z));
    Top=FMath::Max(Top,FMath::Max(Wall->LocalStart.Z,Wall->LocalEnd.Z)+Wall->Height);
   }
   if(!Complete||QueryLocal.Z<Bottom-1.0||QueryLocal.Z>Top+1.0)continue;
   auto& C= Candidates.AddDefaulted_GetRef();C.Status=EEHBRoomLocationStatus::Inside;C.Building=B;C.Identity.BuildingGuid=B->BuildingGuid;C.Identity.RoomGuid=Room.LoopGuid;C.FloorIndex=Room.FloorIndex;
  }
 }
 // A shared boundary can legitimately match two rooms. Retain only a still-valid prior match.
 if(const auto* Kept=Candidates.FindByPredicate([&](const auto& C){return C==Previous;}))return *Kept;
 if(Candidates.Num()==1)return Candidates[0];
 if(Candidates.Num()>1)Result.Status=EEHBRoomLocationStatus::Ambiguous;
 return Result;
}
void UEHBRoomRuntimeSubsystem::MarkExplored(FName Observer,const FEHBRoomAddress& Room)
{
 if(!Observer.IsNone()&&Room.BuildingGuid.IsValid()&&Room.RoomGuid.IsValid())Exploration.FindOrAdd(Observer).Add(Room);
}
bool UEHBRoomRuntimeSubsystem::IsExplored(FName Observer,const FEHBRoomAddress& Room) const
{
 const auto* Rooms=Exploration.Find(Observer);return Rooms&&Rooms->Contains(Room);
}
TArray<FEHBRoomAddress> UEHBRoomRuntimeSubsystem::ExportExploration(FName Observer) const
{
 const auto* Rooms=Exploration.Find(Observer);TArray<FEHBRoomAddress> Result;if(Rooms)Result=Rooms->Array();
 Result.Sort([](const auto& A,const auto& B){return A.BuildingGuid==B.BuildingGuid?A.RoomGuid<B.RoomGuid:A.BuildingGuid<B.BuildingGuid;});return Result;
}
void UEHBRoomRuntimeSubsystem::ImportExploration(FName Observer,const TArray<FEHBRoomAddress>& Rooms,bool Replace)
{
 if(Replace)ClearExploration(Observer);for(const auto& Room:Rooms)MarkExplored(Observer,Room);
}
void UEHBRoomRuntimeSubsystem::ClearExploration(FName Observer) { Exploration.Remove(Observer); }

UEHBRoomExplorationSaveGame* UEHBRoomRuntimeSubsystem::CreateExplorationSave() const
{
 auto* Save=NewObject<UEHBRoomExplorationSaveGame>();TArray<FName> Names;Exploration.GetKeys(Names);Names.Sort(FNameLexicalLess());
 for(FName Name:Names){auto& Entry=Save->Observers.AddDefaulted_GetRef();Entry.Observer=Name;Entry.Rooms=ExportExploration(Name);}return Save;
}
bool UEHBRoomRuntimeSubsystem::RestoreExplorationSave(const UEHBRoomExplorationSaveGame* Save,bool Replace,FName& Status)
{
 auto Fail=[&](const TCHAR* Why){Status=Why;return false;};
 if(!IsValid(Save))return Fail(TEXT("InvalidExplorationSave"));
 if(Save->SchemaVersion!=1)return Fail(TEXT("UnsupportedExplorationVersion"));
 TMap<FName,TSet<FEHBRoomAddress>> Candidate=Replace?TMap<FName,TSet<FEHBRoomAddress>>{}:Exploration;
 TSet<FName> Seen;
 for(const auto& Entry:Save->Observers)
 {
  if(Entry.Observer.IsNone()||Seen.Contains(Entry.Observer))return Fail(TEXT("InvalidExplorationObserver"));Seen.Add(Entry.Observer);
  auto& Rooms=Candidate.FindOrAdd(Entry.Observer);
  for(const auto& Room:Entry.Rooms)
  {
   if(!Room.BuildingGuid.IsValid()||!Room.RoomGuid.IsValid())return Fail(TEXT("InvalidRoomAddress"));Rooms.Add(Room);
  }
 }
 Exploration=MoveTemp(Candidate);Status=TEXT("Loaded");return true;
}
namespace
{
 bool ValidExplorationSlot(const FString& Name,int32 User)
 {
  return User>=0&&!Name.TrimStartAndEnd().IsEmpty()&&!Name.Contains(TEXT("/"))&&!Name.Contains(TEXT("\\"))&&!Name.Contains(TEXT(".."));
 }
}
bool UEHBRoomRuntimeSubsystem::SaveExplorationToSlot(const FString& Slot,int32 User,FName& Status) const
{
 if(!ValidExplorationSlot(Slot,User)){Status=TEXT("InvalidSlot");return false;}
 const bool Saved=UGameplayStatics::SaveGameToSlot(CreateExplorationSave(),Slot,User);Status=Saved?TEXT("Saved"):TEXT("SaveFailed");return Saved;
}
bool UEHBRoomRuntimeSubsystem::LoadExplorationFromSlot(const FString& Slot,int32 User,bool Replace,FName& Status)
{
 if(!ValidExplorationSlot(Slot,User)){Status=TEXT("InvalidSlot");return false;}
 const auto* Save=Cast<UEHBRoomExplorationSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot,User));
 if(!Save){Status=TEXT("LoadFailed");return false;}
 return RestoreExplorationSave(Save,Replace,Status);
}
