#pragma once
#include "CoreMinimal.h"
#include "Definitions/EHBCommittedEdit.h"

enum class EEHBRoomDependencyKind : uint8 { Floor, Slab };
struct EASYHOUSEBUILDER_API FEHBRoomDependencyBinding
{
 FGuid ElementGuid,RoomGuid,AnchorWallGuid;
 EEHBRoomDependencyKind Kind=EEHBRoomDependencyKind::Floor;
 bool operator==(const FEHBRoomDependencyBinding& B) const {return ElementGuid==B.ElementGuid&&RoomGuid==B.RoomGuid&&AnchorWallGuid==B.AnchorWallGuid&&Kind==B.Kind;}
};
struct EASYHOUSEBUILDER_API FEHBRoomDependencyMembers
{
 FGuid RoomGuid;
 TArray<FGuid> Floors,Slabs,AnchoredSlabs;
};
struct EASYHOUSEBUILDER_API FEHBRoomDependencyCacheStats
{
 int32 FullBuilds=0,ReceiptUpdates=0,Reuses=0;
 int64 SourceEntriesRead=0;
 FName LastReason;
};
// Membership only. Callers read current provenance, floor indices and room geometry.
// Actual binding descriptors are compared on every query: receipts are not global authority.
class EASYHOUSEBUILDER_API FEHBRoomDependencyCache
{
public:
 bool Update(FGuid Building,const FEHBCommittedEdit& Receipt,const TArray<FEHBRoomDependencyBinding>& Input,bool bEditInFlight=false);
 FEHBRoomDependencyMembers Query(FGuid Room,const TArray<FGuid>& Walls) const;
 const FEHBRoomDependencyCacheStats& GetStats() const {return Stats;}
private:
 bool bValid=false;
 FGuid BuildingGuid,StateId;
 TMap<FGuid,FEHBRoomDependencyBinding> Sources;
 TMap<FGuid,TArray<FGuid>> FloorsByRoom,SlabsByRoom,SlabsByAnchor;
 FEHBRoomDependencyCacheStats Stats;
 void Add(const FEHBRoomDependencyBinding& Binding);
 void Remove(const FEHBRoomDependencyBinding& Binding);
};
