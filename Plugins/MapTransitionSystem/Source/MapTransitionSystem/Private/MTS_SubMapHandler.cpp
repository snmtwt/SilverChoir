#include "MTS_SubMapHandler.h"

bool UMTS_SubMapHandler::GetSubMapInfo(FMTS_SubMapInfo& OutMap) const
{
    OutMap = FMTS_SubMapInfo();
    return IsValid(Subsystem) && Subsystem->GetSubMapHandler(AssignedMapID) == this
        && Subsystem->GetSubMapByKey(AssignedMapID, OutMap);
}

bool UMTS_SubMapHandler::IsSubMapReady() const
{
    FMTS_SubMapInfo Map;
    return GetSubMapInfo(Map) && Map.State == EMTS_SubMapState::Loaded
        && Map.LoadingPayload.Phase == EMTS_MapTransitionPhase::Completed;
}

bool UMTS_SubMapHandler::GetWorldTransform(const FTransform& LocalTransform, FTransform& OutWorldTransform) const
{
    OutWorldTransform = FTransform::Identity;
    FMTS_SubMapInfo Map;
    if (!LocalTransform.IsValid() || !GetSubMapInfo(Map) || !Map.Transform.IsValid()) return false;
    OutWorldTransform = LocalTransform * Map.Transform;
    return OutWorldTransform.IsValid();
}

bool UMTS_SubMapHandler::RejectOperation(const FText& Error, FText& OutError)
{
    LastHandlerError = Error;
    OutError = Error;
    return false;
}

void UMTS_SubMapHandler::ReleaseNativeResources()
{
    if (bNativeResourcesReleased) return;
    bNativeResourcesReleased = true;
    OnReleaseResources();
}

bool UMTS_SubMapHandler::UpdateLoadingProgress(float Progress, const FText& Content)
{ return Subsystem && Subsystem->GetSubMapHandler(AssignedMapID)==this && Subsystem->SetSubMapProgress(AssignedMapID,Progress,Content); }
bool UMTS_SubMapHandler::FinishLoading()
{ return Subsystem && Subsystem->GetSubMapHandler(AssignedMapID)==this && Subsystem->NotifySubMapReady(AssignedMapID); }
bool UMTS_SubMapHandler::FailLoading(const FText& Error)
{ return Subsystem && Subsystem->GetSubMapHandler(AssignedMapID)==this && Subsystem->ReportSubMapFailure(AssignedMapID,Error); }
bool UMTS_SubMapHandler::UnloadMap()
{ return Subsystem && Subsystem->GetSubMapHandler(AssignedMapID)==this && Subsystem->UnloadSubMapByID(AssignedMapID); }

UWorld* UMTS_SubMapHandler::GetWorld() const
{
	return IsValid(Subsystem) ? Subsystem->GetWorld() : nullptr;
}
