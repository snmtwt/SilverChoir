#pragma once
#include "CoreMinimal.h"
#include "EHBCommittedEdit.generated.h"

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBEditElementChange
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") FGuid ElementGuid;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") FBox BeforeBounds=FBox(ForceInit);
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") FBox AfterBounds=FBox(ForceInit);
};

// Latest successful command receipt, not a global revision of all legacy editing.
// Sequence follows undo/redo. StateId distinguishes new branches at the same sequence.
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBCommittedEdit
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") int64 Sequence=0;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") FGuid BuildingGuid;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") FGuid StateId;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") FGuid ParentStateId;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") FName Command;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") TArray<FGuid> NodeGuids;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") TArray<FGuid> RoomGuids;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") TArray<FEHBEditElementChange> Elements;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") TArray<FGuid> UpdatedRelationGuids;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") TArray<FGuid> RemovedRelationGuids;
 UPROPERTY(BlueprintReadOnly,Category="EHB|Editing") bool bRequiresFullSpatialRefresh=false;
 bool Intersects(const FBox& BuildingLocalQuery) const
 {
  if(bRequiresFullSpatialRefresh)return true;
  for(const auto& E:Elements)if((E.BeforeBounds.IsValid&&E.BeforeBounds.Intersect(BuildingLocalQuery))||(E.AfterBounds.IsValid&&E.AfterBounds.Intersect(BuildingLocalQuery)))return true;
  return false;
 }
};
