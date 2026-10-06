#include "MTS_SubMapSubsystem.h"
#include "MTS_SubMapDataAsset.h"
#include "MTS_SubMapHandler.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"


bool UMTS_SubMapSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMTS_SubMapSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMTS_SubMapSubsystem, STATGROUP_Tickables);
}

bool UMTS_SubMapSubsystem::LoadSubMap(const UMTS_SubMapDataAsset* MapAsset, FVector Location, FRotator Rotation, FText& OutError)
{
	return LoadSubMapWithHandler(MapAsset, Location, Rotation, nullptr, OutError);
}

bool UMTS_SubMapSubsystem::LoadSubMapWithHandler(const UMTS_SubMapDataAsset* MapAsset, FVector Location,
    FRotator Rotation, TSubclassOf<UMTS_SubMapHandler> HandlerClass, FText& OutError, float AutomaticProgressMax)
{
    FMTS_SubMapLoadRequest Request;
    Request.MapAsset=const_cast<UMTS_SubMapDataAsset*>(MapAsset); Request.Location=Location; Request.Rotation=Rotation;
    Request.AutomaticProgressMax=AutomaticProgressMax; Request.bRequireManualReady=false;
    return BeginLoad(Request,CreateSubMapHandler(HandlerClass ? HandlerClass.Get() : UMTS_SubMapHandler::StaticClass()),{},false,OutError);
}

TArray<FMTS_SubMapInfo> UMTS_SubMapSubsystem::GetSubMaps() const
{
	TArray<FMTS_SubMapInfo> Result;
	Maps.GenerateValueArray(Result);
	return Result;
}

bool UMTS_SubMapSubsystem::GetSubMapByKey(FName Key, FMTS_SubMapInfo& OutMap) const
{
	OutMap = FMTS_SubMapInfo();
	const auto* Map = Maps.Find(Key);
	if (!Map) { return false; }
	OutMap = *Map;
	if (IsValid(OutMap.StreamingLevel))
	{
		OutMap.bIsVisible = OutMap.StreamingLevel->IsLevelVisible();
		OutMap.bShouldBeVisible = OutMap.StreamingLevel->GetShouldBeVisibleFlag();
	}
	return true;
}

bool UMTS_SubMapSubsystem::SetSubMapVisibleByKey(FName Key, bool bVisible, FText& OutError)
{
	OutError = FText();
	auto* Map = Maps.Find(Key);
	if (!Map || !IsValid(Map->StreamingLevel))
	{
		OutError = FText::FromString(TEXT("未找到此 Key 对应的子地图。")); return false;
	}
	if (Map->State != EMTS_SubMapState::Loaded || Map->LoadingPayload.Phase != EMTS_MapTransitionPhase::Completed)
	{
		OutError = FText::FromString(TEXT("子地图尚未完成加载或正在卸载，不能切换显示。")); return false;
	}
	Map->bShouldBeVisible = bVisible;
	Map->StreamingLevel->SetShouldBeVisible(bVisible);
	return true;
}
TArray<FName> UMTS_SubMapSubsystem::GetAllSubMapIDs() const
{
	TArray<FName> Result;
	Maps.GenerateKeyArray(Result);
	return Result;
}
TArray<FName> UMTS_SubMapSubsystem::GetAllSubMapNames() const
{
	TArray<FName> Result;
	for (const auto& Pair : Maps) { Result.Add(Pair.Value.MapName); }
	return Result;
}
TArray<FMTS_SubMapInfo> UMTS_SubMapSubsystem::GetSubMapsByTag(FGameplayTag Tag, bool bExactMatch) const
{
	TArray<FMTS_SubMapInfo> Result;
	if (!Tag.IsValid()) { return Result; }
	for (const auto& Pair : Maps)
	{
		if (bExactMatch ? Pair.Value.MapTags.HasTagExact(Tag) : Pair.Value.MapTags.HasTag(Tag))
		{
			Result.Add(Pair.Value);
		}
	}
	return Result;
}

bool UMTS_SubMapSubsystem::UnloadSubMapByID(FName MapID)
{
	FMTS_SubMapInfo* Info = Maps.Find(MapID);
	if (!Info || Info->State == EMTS_SubMapState::Unloading || Info->State == EMTS_SubMapState::Failed || Info->State == EMTS_SubMapState::Unloaded) { return false; }
	Info->State = EMTS_SubMapState::Unloading;
	Info->bShouldBeVisible = false;
	FMTS_SubMapInfo Snapshot = *Info;
	const FText CancelReason = FText::FromString(TEXT("子地图加载被卸载请求取消。"));
	if (Snapshot.LoadingPayload.Phase != EMTS_MapTransitionPhase::Completed && Snapshot.LoadingPayload.Phase != EMTS_MapTransitionPhase::Failed)
	{
		UpdateProgress(MapID, EMTS_MapTransitionPhase::Failed, Snapshot.LoadingPayload.Progress, CancelReason, false);
		Snapshot.LoadingPayload.Phase = EMTS_MapTransitionPhase::Failed;
		Snapshot.LoadingPayload.LoadingContent = CancelReason;
		FailHandler(MapID, Snapshot, CancelReason);
	}
    // Blueprint cleanup runs while the old level and handler are still accessible.
    if(auto* Handler=Handlers.FindRef(MapID).Get())
    {
        Handler->ReleaseNativeResources();
        Handler->OnSubMapUnloading(this,Snapshot);
    }
    Info=Maps.Find(MapID);
    if(Info && IsValid(Info->StreamingLevel))
    {
        Info->StreamingLevel->SetShouldBeVisible(false);
        Info->StreamingLevel->SetShouldBeLoaded(false);
        Info->StreamingLevel->SetIsRequestingUnloadAndRemoval(true);
    }
    ReleasePresentation(MapID);
	BroadcastState(Snapshot);
	return true;
}
int32 UMTS_SubMapSubsystem::UnloadSubMapsByName(FName MapName)
{
	if (MapName.IsNone()) { return 0; }
	const TArray<FMTS_SubMapInfo> Snapshot = GetSubMaps();
	int32 Count = 0;
	for (const FMTS_SubMapInfo& Info : Snapshot)
	{
		if (Info.MapName == MapName) { Count += UnloadSubMapByID(Info.MapID) ? 1 : 0; }
	}
	return Count;
}
int32 UMTS_SubMapSubsystem::UnloadSubMapsByTag(FGameplayTag Tag, bool bExactMatch)
{
	const TArray<FMTS_SubMapInfo> Snapshot = GetSubMapsByTag(Tag, bExactMatch);
	int32 Count = 0;
	for (const FMTS_SubMapInfo& Info : Snapshot) { Count += UnloadSubMapByID(Info.MapID) ? 1 : 0; }
	return Count;
}

void UMTS_SubMapSubsystem::BroadcastState(const FMTS_SubMapInfo& Map, const FText& Error)
{
	// 蓝图回调可再次加载/卸载，不能向回调暴露 TMap 中可能失效的引用。
	const FMTS_SubMapInfo Snapshot = Map;
	OnSubMapStateChanged.Broadcast(Snapshot, Error);
}

void UMTS_SubMapSubsystem::FailHandler(FName MapID, const FMTS_SubMapInfo& Map, const FText& Error)
{
    auto* Op=Operations.Find(MapID);
    if(!Op || Op->bFailureNotified)return;
    Op->bFailureNotified=true;
	if (auto* Handler=Handlers.FindRef(MapID).Get(); IsValid(Handler))
	{
		const FMTS_SubMapInfo Snapshot = Map;
        Handler->LastHandlerError = Error;
        Handler->ReleaseNativeResources();
		Handler->OnSubMapLoadFailed(this, Snapshot, Error);
	}
    ReleasePresentation(MapID);
}

void UMTS_SubMapSubsystem::UpdateProgress(FName MapID, EMTS_MapTransitionPhase Phase, float Progress,
	const FText& Content, bool bAutomatic)
{
	FMTS_SubMapInfo* Info = Maps.Find(MapID);
	if (!Info) { return; }
	FMTS_MapTransitionPayload& Payload = Info->LoadingPayload;
	Payload.Phase = Phase;
	Payload.Progress = FMath::Max(Payload.Progress, FMath::Clamp(Progress, 0.0f, 1.0f) * (bAutomatic ? Info->AutomaticProgressMax : 1.0f));
	Payload.LoadingContent = Content;
	// Standalone foreground requests block input while their loading screen is active.
	Payload.bShouldBlockInput = ForegroundMapID==MapID && Phase!=EMTS_MapTransitionPhase::Failed;
	const FMTS_MapTransitionPayload Snapshot = Payload;
	OnSubMapProgressChanged.Broadcast(MapID, Snapshot);
}

bool UMTS_SubMapSubsystem::SetSubMapProgress(FName MapID, float Progress, const FText& LoadingContent)
{
	FMTS_SubMapInfo* Info = Maps.Find(MapID);
	if (!Info || !FMath::IsFinite(Progress) || Info->State == EMTS_SubMapState::Unloading
		|| Info->State == EMTS_SubMapState::Failed || Info->LoadingPayload.Phase == EMTS_MapTransitionPhase::Completed
		|| Info->LoadingPayload.Phase == EMTS_MapTransitionPhase::Failed) { return false; }
	UpdateProgress(MapID, Info->LoadingPayload.Phase, Progress, LoadingContent, false);
	// Final completion is handled on Tick after the loaded callback has returned.
	return true;
}

void UMTS_SubMapSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const TArray<FName> IDs = GetAllSubMapIDs();
	for (FName ID : IDs)
	{
		FMTS_SubMapInfo* Info = Maps.Find(ID);
		if (!Info) { continue; }
        if(Info->State==EMTS_SubMapState::WaitingForUnload)
        {
            const auto* Op=Operations.Find(ID);
            const bool bWaiting=Op && Op->WaitingForIDs.ContainsByPredicate([&](FName OldID){return Maps.Contains(OldID);});
            if(!bWaiting){FText Error;StartStreaming(ID,Error);}
            TickPresentation(ID);continue;
        }
		ULevelStreamingDynamic* Level = Info->StreamingLevel;
		const bool bRemoved = !IsValid(Level) || !GetWorld()->GetStreamingLevels().Contains(Level);
		const bool bFailed = IsValid(Level) && Level->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad;
		if (bRemoved || bFailed)
		{
			FMTS_SubMapInfo Completed = *Info;
			Completed.State = Info->State == EMTS_SubMapState::Unloading || (!bFailed && Info->State == EMTS_SubMapState::Loaded)
				? EMTS_SubMapState::Unloaded : EMTS_SubMapState::Failed;
			if (IsValid(Level)) { Level->SetIsRequestingUnloadAndRemoval(true); }
			const FText Error = FText::FromString(TEXT("子地图异步加载失败或关卡实例被外部移除。"));
			if(Completed.State==EMTS_SubMapState::Failed)
            {Completed.LoadingPayload.Phase=EMTS_MapTransitionPhase::Failed;Completed.LoadingPayload.LoadingContent=Error;}
			*Info = Completed;
            if(Completed.State==EMTS_SubMapState::Failed)FailHandler(ID,Completed,Error);
            else if(auto* Handler=Handlers.FindRef(ID).Get())
            {
                Handler->ReleaseNativeResources();
                Handler->OnSubMapUnloaded(this,Completed);
            }
            ReleasePresentation(ID);
            Handlers.Remove(ID);Operations.Remove(ID);
			Maps.Remove(ID);
			if (Completed.State == EMTS_SubMapState::Failed) { OnSubMapProgressChanged.Broadcast(ID, Completed.LoadingPayload); }
			BroadcastState(Completed, Completed.State == EMTS_SubMapState::Failed
				? FText::FromString(TEXT("子地图异步加载失败或关卡实例被外部移除。")) : FText());
		}
		else if (Info->State == EMTS_SubMapState::Loading && Level->IsLevelLoaded() && Level->IsLevelVisible())
		{
			Info->State = EMTS_SubMapState::Loaded;
			Info->bIsVisible = true;
			UpdateProgress(ID, EMTS_MapTransitionPhase::ApplyingLoadedResources, 0.90f, FText::FromString(TEXT("正在应用子地图资源")));
			Info = Maps.Find(ID);
			if (!Info || Info->State != EMTS_SubMapState::Loaded) { continue; }
			const FMTS_SubMapInfo Snapshot = *Info;
			if (UMTS_SubMapHandler* Handler = Handlers.FindRef(ID)) { Handler->OnSubMapLoaded(this, Snapshot); }
			Info = Maps.Find(ID);
			if (Info && Info->State == EMTS_SubMapState::Loaded) { BroadcastState(*Info); }
		}
		else if (Info->State == EMTS_SubMapState::Loading && Level->IsLevelLoaded()
			&& Info->LoadingPayload.Phase == EMTS_MapTransitionPhase::OpeningMap)
		{
			UpdateProgress(ID, EMTS_MapTransitionPhase::WaitingForMapReady, 0.80f, FText::FromString(TEXT("等待子地图显示")));
		}
		Info = Maps.Find(ID);
		if (Info && Info->State == EMTS_SubMapState::Loaded && Info->LoadingPayload.Phase != EMTS_MapTransitionPhase::Completed)
		{
            const auto* Op=Operations.Find(ID);
            // New object requests have an explicit readiness flag; the automatic cap is only visual.
            // Legacy class-based loads retain their manual-progress completion convention.
			if (Op && (Op->Request.bRequireManualReady ? Op->bReadyRequested : (Op->bForeground || Info->AutomaticProgressMax >= 1.0f || Info->LoadingPayload.Progress >= 1.0f)))
			{
				UpdateProgress(ID, EMTS_MapTransitionPhase::Completed, 1.0f, FText::FromString(TEXT("子地图加载完成")), false);
                Info=Maps.Find(ID);
                if(Info && Info->State==EMTS_SubMapState::Loaded)
                {
                    const FMTS_SubMapInfo Ready=*Info;
                    if(auto* Handler=Handlers.FindRef(ID).Get())Handler->OnSubMapReady(this,Ready);
                }
			}
			else if (Info->LoadingPayload.Phase != EMTS_MapTransitionPhase::WaitingForManualProgress)
			{
				UpdateProgress(ID, EMTS_MapTransitionPhase::WaitingForManualProgress, 1.0f, FText::FromString(TEXT("等待子地图自定义初始化完成")));
			}
		}
        TickPresentation(ID);
		Info = Maps.Find(ID);
		if (Info && IsValid(Info->StreamingLevel))
		{
			const bool bVisible = Info->StreamingLevel->IsLevelVisible();
			Info->bShouldBeVisible = Info->StreamingLevel->GetShouldBeVisibleFlag();
			if (Info->bIsVisible != bVisible)
			{
				Info->bIsVisible = bVisible;
				const FMTS_SubMapInfo Snapshot = *Info;
				OnSubMapVisibilityChanged.Broadcast(Snapshot);
			}
		}
	}
}
void UMTS_SubMapSubsystem::Deinitialize()
{
    for(FName ID:GetAllSubMapIDs())
    {
        if(auto* Handler=Handlers.FindRef(ID).Get())Handler->ReleaseNativeResources();
        ReleasePresentation(ID);
    }
	for (const auto& Pair : Maps)
	{
		if (IsValid(Pair.Value.StreamingLevel))
		{
			Pair.Value.StreamingLevel->SetShouldBeVisible(false);
			Pair.Value.StreamingLevel->SetShouldBeLoaded(false);
			Pair.Value.StreamingLevel->SetIsRequestingUnloadAndRemoval(true);
		}
	}
	Maps.Empty();
	Handlers.Empty();
    Operations.Empty();ForegroundMapID=NAME_None;
	OnSubMapProgressChanged.Clear();
	OnSubMapStateChanged.Clear();
	OnSubMapVisibilityChanged.Clear();
	Super::Deinitialize();
}
