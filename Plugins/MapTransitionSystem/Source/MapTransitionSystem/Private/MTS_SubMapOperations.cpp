#include "MTS_SubMapSubsystem.h"
#include "MTS_SubMapHandler.h"
#include "MTS_MapLoadingWidget.h"
#include "MTS_MapTransitionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"

UMTS_SubMapHandler* UMTS_SubMapSubsystem::CreateSubMapHandler(TSubclassOf<UMTS_SubMapHandler> HandlerClass)
{
    if(!IsInitialized() || !GetWorld() || GetWorld()->bIsTearingDown || !HandlerClass
        || HandlerClass->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists)) return nullptr;
    auto* Handler=NewObject<UMTS_SubMapHandler>(this,HandlerClass);
    Handler->InitializeHandler(this);
    return Handler;
}
bool UMTS_SubMapSubsystem::LoadSubMapByHandlerObject(const FMTS_SubMapLoadRequest& Request, UMTS_SubMapHandler* Handler,
    const TArray<FName>& UnloadMapIDs, FText& OutError)
{ return BeginLoad(Request,Handler,UnloadMapIDs,true,OutError); }

bool UMTS_SubMapSubsystem::BeginLoad(const FMTS_SubMapLoadRequest& Request, UMTS_SubMapHandler* Handler,
    const TArray<FName>& UnloadMapIDs, bool bForeground, FText& OutError)
{
    OutError=FText();
    auto Reject=[&](const TCHAR* Message){OutError=FText::FromString(Message);return false;};
    if(!IsInitialized() || !GetWorld() || GetWorld()->bIsTearingDown) return Reject(TEXT("当前世界不可加载子地图。"));
    const auto* Asset=Request.MapAsset.Get();
    if(!IsValid(Asset) || Asset->MapID.IsNone() || Asset->MapName.IsNone() || Asset->MapAsset.IsNull())
        return Reject(TEXT("请填写子地图资产、地图ID和地图名。"));
    if(Maps.Contains(Asset->MapID)) return Reject(TEXT("该地图ID已经存在，包括加载中及卸载中的实例。"));
    if(!IsValid(Handler) || Handler->GetOuter()!=this || !Handler->AssignedMapID.IsNone())
        return Reject(TEXT("请传入由当前子地图子系统创建、尚未使用的处理对象。"));
    if(Request.Location.ContainsNaN() || Request.Rotation.ContainsNaN() || !FMath::IsFinite(Request.AutomaticProgressMax)
        || !FMath::IsFinite(Request.ProgressSmoothSpeed) || !FMath::IsFinite(Request.CompletionDelay))
        return Reject(TEXT("子地图变换或进度配置包含无效数值。"));
    if(!FPackageName::DoesPackageExist(Asset->MapAsset.ToSoftObjectPath().GetLongPackageName()))
        return Reject(TEXT("目标子地图不存在或未包含在打包内容中。"));
    if(Request.LoadingWidgetClass && Request.LoadingWidgetClass->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists))
        return Reject(TEXT("加载界面类不可实例化。"));
    if(bForeground)
    {
        if(IsSubMapTransitionInProgress()) return Reject(TEXT("已有子地图切换正在进行。"));
        auto* GI=GetWorld()->GetGameInstance();
        auto* Main=GI?GI->GetSubsystem<UMTS_MapTransitionSubsystem>():nullptr;
        if(Main && Main->IsTransitionInProgress()) return Reject(TEXT("请先等待主地图切换完成。"));
    }
    // Validate before unloading anything. Do not infer that every non-target map is disposable.
    for(FName ID:UnloadMapIDs)
        if(ID.IsNone() || ID==Asset->MapID || !Maps.Contains(ID)) return Reject(TEXT("待卸载ID无效；请重新查询当前子地图。"));
    FMTS_SubMapInfo Info;
    Info.MapID=Asset->MapID;Info.MapName=Asset->MapName;Info.MapTags=Asset->MapTags;Info.MapAsset=Asset->MapAsset;
    Info.Transform=FTransform(Request.Rotation,Request.Location);
    Info.AutomaticProgressMax=FMath::Clamp(Request.AutomaticProgressMax,0.f,1.f);
    Info.State=UnloadMapIDs.IsEmpty()?EMTS_SubMapState::Loading:EMTS_SubMapState::WaitingForUnload;
    Info.LoadingPayload.LoadingTitle=Request.LoadingTitle;
    const FName ID=Info.MapID;
    FMTS_SubMapOperation Op;Op.Request=Request;Op.WaitingForIDs=UnloadMapIDs;Op.bForeground=bForeground;
    Op.LastUpdateTime=FPlatformTime::Seconds();
    Maps.Add(ID,Info);Operations.Add(ID,Op);Handlers.Add(ID,Handler);Handler->AssignedMapID=ID;
    if(bForeground) ForegroundMapID=ID;
    if(Request.LoadingWidgetClass)
    {
        auto* Widget=CreateWidget<UMTS_MapLoadingWidget>(GetWorld(),Request.LoadingWidgetClass);
        if(auto* Active=Operations.Find(ID)) Active->Widget=Widget;
        if(!Widget){ReportSubMapFailure(ID,FText::FromString(TEXT("无法创建加载界面。")));return Reject(TEXT("无法创建加载界面。"));}
        Widget->AddToViewport(10000);
        if(auto* Active=Operations.Find(ID))
            for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)if(auto* PC=It->Get();PC && PC->IsLocalController())
            {PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);Active->LockedControllers.Add(PC);}
    }
    if(!Maps.Contains(ID) || Maps.FindChecked(ID).State==EMTS_SubMapState::Unloading) return Reject(TEXT("子地图加载已取消。"));
    if(UnloadMapIDs.IsEmpty()) return StartStreaming(ID,OutError);
    UpdateProgress(ID,EMTS_MapTransitionPhase::Preloading,0.f,FText::FromString(TEXT("正在卸载旧瓦片区域")));
    for(FName OldID:UnloadMapIDs)
    {
        const auto* Current=Maps.Find(ID);
        if(!Current || Current->State!=EMTS_SubMapState::WaitingForUnload) return Reject(TEXT("子地图加载已取消。"));
        UnloadSubMapByID(OldID);
    }
    if(const auto* Current=Maps.Find(ID)) BroadcastState(*Current);
    return true;
}

bool UMTS_SubMapSubsystem::StartStreaming(FName ID,FText& OutError)
{
    auto* Active=Maps.Find(ID);auto* Handler=Handlers.FindRef(ID).Get();
    if(!Active || !Handler) return false;
    Active->State=EMTS_SubMapState::Loading;
    UpdateProgress(ID,EMTS_MapTransitionPhase::Preloading,.05f,FText::FromString(TEXT("正在预加载子地图资源")));
    Active=Maps.Find(ID);
    if(!Active || Active->State!=EMTS_SubMapState::Loading){OutError=FText::FromString(TEXT("子地图加载已取消。"));return false;}
    const FMTS_SubMapInfo Snapshot=*Active;
    const bool bPreloaded=Handler->PreloadSubMapResources(this,Snapshot);
    Active=Maps.Find(ID);
    if(!Active || Active->State!=EMTS_SubMapState::Loading){OutError=FText::FromString(TEXT("子地图加载已取消。"));return false;}
    bool bSuccess=false;
    auto* Level=bPreloaded?ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(GetWorld(),Snapshot.MapAsset,
        Snapshot.Transform.GetLocation(),Snapshot.Transform.Rotator(),bSuccess):nullptr;
    if(!bSuccess || !Level)
    {
        OutError=FText::FromString(bPreloaded?TEXT("引擎未能创建子地图实例。"):TEXT("子地图资源预加载失败。"));
        Active->State=EMTS_SubMapState::Failed;
        UpdateProgress(ID,EMTS_MapTransitionPhase::Failed,Active->LoadingPayload.Progress,OutError,false);
        Active=Maps.Find(ID);
        if(!Active)return false;
        const FMTS_SubMapInfo Failed=*Active;
        FailHandler(ID,Failed,OutError);ReleasePresentation(ID);
        Handlers.Remove(ID);Operations.Remove(ID);Maps.Remove(ID);BroadcastState(Failed,OutError);
        return false;
    }
    Active->StreamingLevel=Level;
    UpdateProgress(ID,EMTS_MapTransitionPhase::OpeningMap,.30f,FText::FromString(TEXT("正在加载子地图")));
    if(const auto* Current=Maps.Find(ID)) BroadcastState(*Current);
    return true;
}
bool UMTS_SubMapSubsystem::NotifySubMapReady(FName ID)
{
    const auto* Info=Maps.Find(ID);auto* Op=Operations.Find(ID);
    if(!Info || !Op || Info->State!=EMTS_SubMapState::Loaded || Op->bFailureNotified || Info->LoadingPayload.Phase==EMTS_MapTransitionPhase::Completed) return false;
    Op->bReadyRequested=true;
    return SetSubMapProgress(ID,1.f,FText::FromString(TEXT("子地图初始化完成")));
}
bool UMTS_SubMapSubsystem::ReportSubMapFailure(FName ID,const FText& Error)
{
    const auto* Info=Maps.Find(ID);
    if(!Info || Info->State==EMTS_SubMapState::Unloading || Info->State==EMTS_SubMapState::Failed || Info->LoadingPayload.Phase==EMTS_MapTransitionPhase::Completed) return false;
    const FText Reason=Error.IsEmpty()?FText::FromString(TEXT("子地图初始化失败。")):Error;
    UpdateProgress(ID,EMTS_MapTransitionPhase::Failed,Info->LoadingPayload.Progress,Reason,false);
    if(const auto* Current=Maps.Find(ID)) FailHandler(ID,*Current,Reason);
    UnloadSubMapByID(ID);
    return true;
}
TArray<FName> UMTS_SubMapSubsystem::GetSubMapIDsByTag(FGameplayTag Tag,bool bExactMatch) const
{
    TArray<FName> IDs;for(const auto& Map:GetSubMapsByTag(Tag,bExactMatch)) IDs.Add(Map.MapID);return IDs;
}
UMTS_MapLoadingWidget* UMTS_SubMapSubsystem::GetSubMapLoadingWidget(FName ID) const
{const auto* Op=Operations.Find(ID);return Op?Op->Widget.Get():nullptr;}

void UMTS_SubMapSubsystem::ReleasePresentation(FName ID)
{
    auto* Op=Operations.Find(ID);if(!Op)return;
    auto* Widget=Op->Widget.Get();Op->Widget=nullptr;
    const auto Controllers=MoveTemp(Op->LockedControllers);
    if(ForegroundMapID==ID)ForegroundMapID=NAME_None;
    for(const auto& PC:Controllers)if(PC.IsValid()){PC->SetIgnoreMoveInput(false);PC->SetIgnoreLookInput(false);}
    if(Widget)Widget->RemoveFromParent();
}
void UMTS_SubMapSubsystem::TickPresentation(FName ID)
{
    auto* Op=Operations.Find(ID);const auto* Info=Maps.Find(ID);if(!Op || !Info)return;
    if(!Op->Widget){if(Info->LoadingPayload.Phase==EMTS_MapTransitionPhase::Completed)ReleasePresentation(ID);return;}
    const double Now=FPlatformTime::Seconds();
    const float Delta=FMath::Clamp(float(Now-Op->LastUpdateTime),0.f,.25f);Op->LastUpdateTime=Now;
    const float Target=Info->LoadingPayload.Progress;
    Op->DisplayProgress=Op->Request.ProgressSmoothSpeed<=0?Target:FMath::FInterpConstantTo(Op->DisplayProgress,Target,Delta,Op->Request.ProgressSmoothSpeed);
    FMTS_MapTransitionPayload Payload=Info->LoadingPayload;Payload.Progress=Op->DisplayProgress;
    Payload.bShouldBlockInput=Info->LoadingPayload.Phase!=EMTS_MapTransitionPhase::Failed;
    if(Info->LoadingPayload.Phase==EMTS_MapTransitionPhase::Completed && Op->DisplayProgress>=1.f && Op->CloseAt==0)
        Op->CloseAt=Now+FMath::Max(0.f,Op->Request.CompletionDelay);
    const bool bClose=Op->CloseAt>0 && Now>=Op->CloseAt;
    Op->Widget->ApplyTransitionPayload(Payload);
    if(bClose)ReleasePresentation(ID);
}
