#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Definitions/EHBCommittedEdit.h"
#include "EHBChangeNotificationTestObserver.generated.h"

UCLASS(Transient)
class UEHBChangeNotificationTestObserver : public UObject
{
 GENERATED_BODY()
public:
 TFunction<void(FGuid,FName,bool)> Observe;
 TFunction<void(const FEHBCommittedEdit&)> ObserveCommit;
 UFUNCTION() void Committed(const FEHBCommittedEdit& Edit) { if(ObserveCommit)ObserveCommit(Edit); }
 UFUNCTION() void Added(FGuid Id) { if(Observe)Observe(Id,TEXT("Added"),false); }
 UFUNCTION() void Removed(FGuid Id) { if(Observe)Observe(Id,TEXT("Removed"),false); }
 UFUNCTION() void Geometry(FGuid Id,bool Finished) { if(Observe)Observe(Id,TEXT("Geometry"),Finished); }
};
