#include "Data/Squads/SquadStructs.h"

#include "UObject/Class.h"

TArray<FGuid> FSquadData::TrimMembersToCapacity()
{
    const int32 Capacity = GetMaxMemberCount();
    TArray<FGuid> RemovedIds;
    RemovedIds.Reserve(FMath::Max(0, MemberUnitIds.Num() - Capacity));
    for (int32 Index = MemberUnitIds.Num() - 1; Index >= 0 && MemberUnitIds.Num() > Capacity; --Index)
    {
        if (Capacity > 0 && MemberUnitIds[Index] == CaptainUnitId) continue;
        RemovedIds.Add(MemberUnitIds[Index]);
        MemberUnitIds.RemoveAt(Index);
    }
    if (!MemberUnitIds.Contains(CaptainUnitId))
        CaptainUnitId = MemberUnitIds.IsEmpty() ? FGuid() : MemberUnitIds[0];
    return RemovedIds;
}

bool FSquadData::MatchesSnapshot(const FSquadData& Other) const
{
    return SquadId == Other.SquadId
        && SquadName.EqualTo(Other.SquadName)
        && SquadIcon == Other.SquadIcon
        && IconSource == Other.IconSource
        && TileId == Other.TileId
        && MemberUnitIds == Other.MemberUnitIds
        && CaptainUnitId == Other.CaptainUnitId
        && FVehicleData::StaticStruct()->CompareScriptStruct(&AssignedVehicle, &Other.AssignedVehicle, 0);
}
