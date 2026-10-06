#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"

#include "Data/Units/UnitStructs.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadSettings.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadSubsystem.h"

namespace
{
    UPlayerSquadManagerBase* RequireSquadManager(const UObject* WorldContextObject, FText& OutError)
    {
        OutError = FText::GetEmpty();
        UPlayerSquadManagerBase* Manager = UPlayerSquadLibrary::GetPlayerSquadManager(WorldContextObject);
        if (!Manager)
        {
            OutError = NSLOCTEXT("PlayerSquad", "SystemNotReady", "玩家小队系统尚未就绪。");
        }
        return Manager;
    }
}

UPlayerSquadSubsystem* UPlayerSquadLibrary::GetPlayerSquadSubsystem(const UObject* WorldContextObject)
{
    UWorld* World = GEngine && IsValid(WorldContextObject)
        ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
    return Instance ? Instance->GetSubsystem<UPlayerSquadSubsystem>() : nullptr;
}

UPlayerSquadManagerBase* UPlayerSquadLibrary::GetPlayerSquadManager(const UObject* WorldContextObject)
{
    UPlayerSquadSubsystem* System = GetPlayerSquadSubsystem(WorldContextObject);
    return System ? System->GetManager() : nullptr;
}

bool UPlayerSquadLibrary::IsPlayerSquadSystemReady(const UObject* WorldContextObject)
{
    return GetPlayerSquadManager(WorldContextObject) != nullptr;
}

FText UPlayerSquadLibrary::GetNextSquadName(const UObject* WorldContextObject)
{
    const UPlayerSquadManagerBase* Manager = GetPlayerSquadManager(WorldContextObject);
    return Manager ? Manager->GetNextSquadName() : FText::GetEmpty();
}

bool UPlayerSquadLibrary::CreateSquad(const UObject* WorldContextObject, FText SquadName, UTexture2D* SquadIcon, FName TileId, FGuid& OutSquadId, FText& OutError)
{
    OutSquadId.Invalidate();
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->CreateSquad(SquadName, SquadIcon, TileId, OutSquadId, OutError);
}

bool UPlayerSquadLibrary::CommitSquadDraft(const UObject* WorldContextObject, const FSquadData& Draft, const FSquadData& OriginalSquad, bool bCreateNew, FGuid& OutSquadId, FText& OutError)
{
    OutSquadId.Invalidate();
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->CommitSquadDraft(Draft, OriginalSquad, bCreateNew, OutSquadId, OutError);
}

bool UPlayerSquadLibrary::SetSquadVehicle(const UObject* WorldContextObject, FGuid SquadId,
    const FVehicleData& Vehicle, TArray<FGuid>& OutRemoved, FText& OutError)
{
    OutRemoved.Reset();
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->SetSquadVehicle(SquadId, Vehicle, OutRemoved, OutError);
}

bool UPlayerSquadLibrary::ClearSquadVehicle(const UObject* WorldContextObject, FGuid SquadId,
    TArray<FGuid>& OutRemoved, FText& OutError)
{
    OutRemoved.Reset();
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->ClearSquadVehicle(SquadId, OutRemoved, OutError);
}

int32 UPlayerSquadLibrary::GetSquadMaxMemberCount(const UObject* WorldContextObject, FGuid SquadId)
{
    UPlayerSquadManagerBase* Manager = GetPlayerSquadManager(WorldContextObject);
    return Manager ? Manager->GetSquadMaxMemberCount(SquadId) : 0;
}

bool UPlayerSquadLibrary::UpdateSquadInfo(const UObject* WorldContextObject, FGuid SquadId, FText SquadName, UTexture2D* SquadIcon, FText& OutError)
{
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->UpdateSquadInfo(SquadId, SquadName, SquadIcon, OutError);
}

bool UPlayerSquadLibrary::SetSquadCaptain(const UObject* WorldContextObject, FGuid SquadId, FGuid UnitId, FText& OutError)
{
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->SetSquadCaptain(SquadId, UnitId, OutError);
}

bool UPlayerSquadLibrary::SetSquadIconSource(const UObject* WorldContextObject, FGuid SquadId, ESquadIconSource IconSource, FText& OutError)
{
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->SetSquadIconSource(SquadId, IconSource, OutError);
}

UTexture2D* UPlayerSquadLibrary::GetSquadIcon(const UObject* WorldContextObject, FGuid SquadId)
{
    UPlayerSquadManagerBase* Manager = GetPlayerSquadManager(WorldContextObject);
    return Manager ? Manager->GetSquadIcon(SquadId) : nullptr;
}

TArray<UTexture2D*> UPlayerSquadLibrary::GetPresetSquadIcons()
{
    TArray<UTexture2D*> Icons;
    for (const TSoftObjectPtr<UTexture2D>& Icon : GetDefault<UPlayerSquadSettings>()->PresetSquadIcons)
        if (UTexture2D* Loaded = Icon.LoadSynchronous()) Icons.AddUnique(Loaded);
    return Icons;
}

bool UPlayerSquadLibrary::RemoveSquad(const UObject* WorldContextObject, FGuid SquadId, FText& OutError)
{
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->RemoveSquad(SquadId, OutError);
}

bool UPlayerSquadLibrary::AddUnitToSquad(const UObject* WorldContextObject, FGuid UnitId, FGuid SquadId, FText& OutError)
{
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->AddUnitToSquad(UnitId, SquadId, OutError);
}

bool UPlayerSquadLibrary::RemoveUnitFromSquad(const UObject* WorldContextObject, FGuid UnitId, FText& OutError)
{
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->RemoveUnitFromSquad(UnitId, OutError);
}

bool UPlayerSquadLibrary::SetSquadTileId(const UObject* WorldContextObject, FGuid SquadId, FName TileId, FText& OutError)
{
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->SetSquadTileId(SquadId, TileId, OutError);
}

bool UPlayerSquadLibrary::GetSquad(const UObject* WorldContextObject, FGuid SquadId, FSquadData& OutSquad)
{
    OutSquad = FSquadData();
    UPlayerSquadManagerBase* Manager = GetPlayerSquadManager(WorldContextObject);
    return Manager && Manager->GetSquad(SquadId, OutSquad);
}

bool UPlayerSquadLibrary::GetUnitSquad(const UObject* WorldContextObject, FGuid UnitId, FSquadData& OutSquad)
{
    OutSquad = FSquadData();
    UPlayerSquadManagerBase* Manager = GetPlayerSquadManager(WorldContextObject);
    return Manager && Manager->GetUnitSquad(UnitId, OutSquad);
}

bool UPlayerSquadLibrary::GetVehicleSquad(const UObject* WorldContextObject, FGuid VehicleId, FSquadData& OutSquad)
{
    OutSquad = FSquadData();
    UPlayerSquadManagerBase* Manager = GetPlayerSquadManager(WorldContextObject);
    return Manager && Manager->GetVehicleSquad(VehicleId, OutSquad);
}

TSharedPtr<FVehicleData> UPlayerSquadLibrary::GetSquadVehicleShared(const UObject* WorldContextObject, FGuid SquadId)
{
    UPlayerSquadManagerBase* Manager = GetPlayerSquadManager(WorldContextObject);
    return Manager ? Manager->GetSquadVehicleShared(SquadId) : nullptr;
}

TArray<FGuid> UPlayerSquadLibrary::GetSquadIds(const UObject* WorldContextObject)
{
    UPlayerSquadManagerBase* Manager = GetPlayerSquadManager(WorldContextObject);
    return Manager ? Manager->GetSquadIds() : TArray<FGuid>();
}

TArray<TSharedPtr<FUnitData>> UPlayerSquadLibrary::GetSquadUnitsShared(const UObject* WorldContextObject, FGuid SquadId)
{
    UPlayerSquadManagerBase* Manager = GetPlayerSquadManager(WorldContextObject);
    return Manager ? Manager->GetSquadUnitsShared(SquadId) : TArray<TSharedPtr<FUnitData>>();
}

TArray<UUnitDataReference*> UPlayerSquadLibrary::GetSquadUnitReferences(const UObject* WorldContextObject, FGuid SquadId)
{
    const TArray<TSharedPtr<FUnitData>> Units = GetSquadUnitsShared(WorldContextObject, SquadId);
    TArray<UUnitDataReference*> References;
    References.Reserve(Units.Num());
    for (const TSharedPtr<FUnitData>& Unit : Units)
    {
        if (Unit.IsValid())
        {
            UUnitDataReference* Reference = NewObject<UUnitDataReference>();
            Reference->Initialize(Unit);
            References.Add(Reference);
        }
    }
    return References;
}

bool UPlayerSquadLibrary::LoadSquadData(const UObject* WorldContextObject, const TArray<FSquadData>& Data, FText& OutError)
{
    UPlayerSquadManagerBase* Manager = RequireSquadManager(WorldContextObject, OutError);
    return Manager && Manager->LoadSquadData(Data, OutError);
}

TArray<FSquadData> UPlayerSquadLibrary::GetAllSquads(const UObject* WorldContextObject)
{
    UPlayerSquadManagerBase* Manager = GetPlayerSquadManager(WorldContextObject);
    return Manager ? Manager->GetAllSquads() : TArray<FSquadData>();
}
