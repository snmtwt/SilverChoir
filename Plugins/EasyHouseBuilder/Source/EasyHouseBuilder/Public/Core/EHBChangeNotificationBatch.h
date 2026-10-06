#pragma once
#include "CoreMinimal.h"
#include "Definitions/EHBCommittedEdit.h"

class AEHBBuildingActorBase;

// A command owns the actual transaction. Publish only after it commits; call
// Rollback only after its state has been restored. Not a geometry transaction.
class EASYHOUSEBUILDER_API FEHBChangeNotificationBatch
{
public:
 explicit FEHBChangeNotificationBatch(AEHBBuildingActorBase& InBuilding);
 ~FEHBChangeNotificationBatch();
 FEHBChangeNotificationBatch(const FEHBChangeNotificationBatch&)=delete;
 FEHBChangeNotificationBatch& operator=(const FEHBChangeNotificationBatch&)=delete;
 bool IsActive() const { return bActive; }
 // Record inside the successful command transaction, before publication.
 // Explicit authored changes include source/binding edits with unchanged bounds.
 bool RecordCommittedEdit(FName Command,const TArray<FGuid>& Nodes,const TArray<FGuid>& Rooms,const TArray<FGuid>& AuthoredElements={});
 void Publish();
 void Rollback();
private:
 friend class AEHBBuildingActorBase;
 TWeakObjectPtr<AEHBBuildingActorBase> Building;
 bool bActive=false;
 bool bRecordedEdit=false;
 FEHBCommittedEdit OriginalEdit;
 TMap<FGuid,FBox> OriginalBounds;
 TMap<FGuid,TArray<FGuid>> OriginalRelationElements;
 int32 OriginalGraphRevision=0;
 TMap<FGuid,int32> OriginalGeometryRevisions;
 TMap<FGuid,TPair<int32,int32>> OriginalRelationRevisions;
 TSet<FGuid> ChangedRelations;
 TMap<FGuid,bool> ChangedGeometry;
};
