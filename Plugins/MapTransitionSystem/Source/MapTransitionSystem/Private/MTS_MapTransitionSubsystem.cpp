#include "MTS_MapTransitionSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "MTS_MapLoadingWidget.h"
#include "MTS_MapTransitionHandler.h"
#include "TimerManager.h"
#include "MTS_SubMapSubsystem.h"

void UMTS_MapTransitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UMTS_MapTransitionSubsystem::HandlePreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMTS_MapTransitionSubsystem::HandlePostLoadMapWithWorld);
}

void UMTS_MapTransitionSubsystem::Deinitialize()
{
	ClearSubMapProgressPlan();
	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FinalizeMapReadyTimerHandle);
		World->GetTimerManager().ClearTimer(CompletionDelayTimerHandle);
		World->GetTimerManager().ClearTimer(ProgressSmoothingTimerHandle);
	}

	HideLoadingScreen();
	ReleaseActiveHandler();
	Super::Deinitialize();
}

UMTS_MapTransitionHandler* UMTS_MapTransitionSubsystem::CreateTransitionHandler(
	TSubclassOf<UMTS_MapTransitionHandler> TransitionHandlerClass)
{
	if (!TransitionHandlerClass)
	{
		return nullptr;
	}

	return NewObject<UMTS_MapTransitionHandler>(this, TransitionHandlerClass);
}

bool UMTS_MapTransitionSubsystem::SwitchMapByObject(
	TSoftObjectPtr<UWorld> TargetMap,
	TSubclassOf<UMTS_MapTransitionHandler> TransitionHandlerClass,
	TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass,
	float LoadingCompletionDelaySeconds,
	bool bRequireManualMapReadyNotification,
	float AutomaticProgressMax)
{
	FMTS_MapTransitionRequest Request;
	Request.TargetMap = TargetMap;
	Request.TransitionHandlerClass = TransitionHandlerClass;
	Request.LoadingWidgetClass = LoadingWidgetClass;
	Request.LoadingTitle = FText::FromString(TEXT("正在加载"));
	Request.LoadingSubtitle = FText::FromString(TEXT("正在切换地图"));
	Request.LoadingTip = FText::GetEmpty();
	Request.LoadingCompletionDelaySeconds = FMath::Max(0.0f, LoadingCompletionDelaySeconds);
	Request.bRequireManualMapReadyNotification = bRequireManualMapReadyNotification;
	Request.AutomaticProgressMax = AutomaticProgressMax;
	return SwitchMap(Request);
}

bool UMTS_MapTransitionSubsystem::SwitchMapByHandlerObject(
	TSoftObjectPtr<UWorld> TargetMap,
	UMTS_MapTransitionHandler* TransitionHandler,
	TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass,
	float LoadingCompletionDelaySeconds,
	bool bRequireManualMapReadyNotification,
	float AutomaticProgressMax)
{
	if (!IsValid(TransitionHandler))
	{
		return false;
	}

	FMTS_MapTransitionRequest Request;
	Request.TargetMap = TargetMap;
	Request.TransitionHandlerClass = TransitionHandler ? TransitionHandler->GetClass() : nullptr;
	Request.LoadingWidgetClass = LoadingWidgetClass;
	Request.LoadingTitle = FText::FromString(TEXT("正在加载"));
	Request.LoadingSubtitle = FText::FromString(TEXT("正在切换地图"));
	Request.LoadingTip = FText::GetEmpty();
	Request.LoadingCompletionDelaySeconds = FMath::Max(0.0f, LoadingCompletionDelaySeconds);
	Request.bRequireManualMapReadyNotification = bRequireManualMapReadyNotification;
	Request.AutomaticProgressMax = AutomaticProgressMax;
	return StartTransition(Request, TransitionHandler);
}

bool UMTS_MapTransitionSubsystem::SwitchMap(const FMTS_MapTransitionRequest& Request)
{
	return StartTransition(Request, nullptr);
}

bool UMTS_MapTransitionSubsystem::StartTransition(
	const FMTS_MapTransitionRequest& Request,
	UMTS_MapTransitionHandler* SuppliedHandler)
{
	// Reject competing requests without tearing down the active transition.
	if (bTransitionInProgress) { return false; }
	if (!Request.HasValidTargetMap() || (SuppliedHandler && !IsValid(SuppliedHandler)))
	{
		FailTransition(FText::FromString(TEXT("地图切换请求无效，或已有切换正在进行。")));
		return false;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		FailTransition(FText::FromString(TEXT("GameInstance 不可用。")));
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FinalizeMapReadyTimerHandle);
		World->GetTimerManager().ClearTimer(CompletionDelayTimerHandle);
		World->GetTimerManager().ClearTimer(ProgressSmoothingTimerHandle);
	}

	ActiveRequest = Request;
	ClearSubMapProgressPlan();
	ActiveRequest.AutomaticProgressMax = FMath::IsFinite(Request.AutomaticProgressMax)
		? FMath::Clamp(Request.AutomaticProgressMax, 0.0f, 1.0f) : 1.0f;
	ActiveRequest.LoadingCompletionDelaySeconds = FMath::Max(0.0f, ActiveRequest.LoadingCompletionDelaySeconds);
	ActiveRequest.ProgressSmoothSpeed = FMath::Max(0.0f, ActiveRequest.ProgressSmoothSpeed);
	CurrentPayload = FMTS_MapTransitionPayload();
	TargetProgress = 0.0f;
	CurrentPayload.LoadingTitle = ActiveRequest.LoadingTitle;
	CurrentPayload.LoadingSubtitle = ActiveRequest.LoadingSubtitle;
	CurrentPayload.LoadingTip = ActiveRequest.LoadingTip;
	CurrentPayload.bShouldBlockInput = true;
	bTransitionInProgress = true;
	bMapReadyReceived = false;

	if (SuppliedHandler)
	{
		ActiveHandler = SuppliedHandler;
	}
	else
	{
		TSubclassOf<UMTS_MapTransitionHandler> HandlerClass = Request.TransitionHandlerClass;
		if (!HandlerClass)
		{
			HandlerClass = UMTS_MapTransitionHandler::StaticClass();
		}
		ActiveHandler = CreateTransitionHandler(HandlerClass);
	}
	if (!ActiveHandler)
	{
		FailTransition(FText::FromString(TEXT("无法创建地图资源处理类。")));
		return false;
	}

	ActiveRequest.bInitializeMapFromHandler |= ActiveHandler->bInitializeSubMapsAfterMainMap;
	ActiveHandler->InitializeTransitionHandler(GameInstance, ActiveRequest);

	SetTransitionPhase(EMTS_MapTransitionPhase::Preloading, 0.05f, FText::FromString(TEXT("正在预加载地图资源")));
	ShowLoadingScreen();

	if (!ActiveHandler->PreloadMapResources(this, ActiveRequest))
	{
		FailTransition(FText::FromString(TEXT("地图资源预加载失败。")));
		return false;
	}

	const FName MapName = ResolveMapName(ActiveRequest);
	if (MapName.IsNone())
	{
		FailTransition(FText::FromString(TEXT("无法解析目标地图名。")));
		return false;
	}

	SetTransitionPhase(EMTS_MapTransitionPhase::OpeningMap, 0.30f, FText::FromString(TEXT("正在打开地图")));
	UGameplayStatics::OpenLevel(GameInstance, MapName, ActiveRequest.bAbsoluteTravel);
	return true;
}

void UMTS_MapTransitionSubsystem::MarkMapReady(FName ReadySource)
{
	if (!bTransitionInProgress || bMapReadyReceived)
	{
		return;
	}
	if (!SubMapWeights.IsEmpty() && CompletedSubMaps.Num() != SubMapWeights.Num())
	{
		DeferredReadySource = ReadySource.IsNone() ? FName(TEXT("SubMapsReady")) : ReadySource;
		return;
	}

	bMapReadyReceived = true;
	CurrentPayload.ReadySource = ReadySource;
	SetTransitionPhase(EMTS_MapTransitionPhase::ApplyingLoadedResources, 0.90f, FText::FromString(TEXT("正在应用地图资源")));

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &UMTS_MapTransitionSubsystem::FinalizeMapReady);
		return;
	}

	FinalizeMapReady();
}

void UMTS_MapTransitionSubsystem::SetTransitionProgress(float Progress, const FText& LoadingContent)
{
	if (!bTransitionInProgress || !FMath::IsFinite(Progress)
		|| CurrentPayload.Phase == EMTS_MapTransitionPhase::Completed
		|| CurrentPayload.Phase == EMTS_MapTransitionPhase::Failed) { return; }
	CurrentPayload.LoadingContent = LoadingContent;
	SetTargetProgress(Progress);
}

void UMTS_MapTransitionSubsystem::NotifyMapLoadCompleted(FName ReadySource)
{
	MarkMapReady(ReadySource);
}

void UMTS_MapTransitionSubsystem::HandlePreLoadMap(const FString& MapName)
{
	if (!bTransitionInProgress)
	{
		return;
	}

	SetTransitionPhase(EMTS_MapTransitionPhase::OpeningMap, 0.45f, FText::FromString(TEXT("正在加载地图包")));
}

void UMTS_MapTransitionSubsystem::HandlePostLoadMapWithWorld(UWorld* LoadedWorld)
{
	if (!bTransitionInProgress)
	{
		return;
	}

	SetTransitionPhase(EMTS_MapTransitionPhase::WaitingForMapReady, 0.80f, FText::FromString(TEXT("等待地图初始化")));
	ShowLoadingScreen();
	if (ActiveRequest.bInitializeMapFromHandler) { SetTargetProgress(ActiveRequest.AutomaticProgressMax); }
	if (ActiveHandler) { ActiveHandler->OnMainMapLoaded(this, ActiveRequest); }
	if (!bTransitionInProgress) { return; }

	if (!ActiveRequest.bRequireManualMapReadyNotification)
	{
		MarkMapReady(TEXT("PostLoadMapWithWorld"));
	}
}

void UMTS_MapTransitionSubsystem::FinalizeMapReady()
{
	if (!bTransitionInProgress)
	{
		return;
	}

	if (ActiveHandler)
	{
		ActiveHandler->OnMapLoaded(this, ActiveRequest);
		// Handler lifetime and loading UI completion do not depend on progress.
		ReleaseActiveHandler();
	}

	if (ActiveRequest.LoadingCompletionDelaySeconds > 0.0f)
	{
		SetTransitionPhase(
			EMTS_MapTransitionPhase::ApplyingLoadedResources,
			0.90f,
			FText::FromString(TEXT("地图资源应用完成"))
		);

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				CompletionDelayTimerHandle,
				this,
				&UMTS_MapTransitionSubsystem::CompleteTransition,
				ActiveRequest.LoadingCompletionDelaySeconds,
				false
			);
			return;
		}
	}

	CompleteTransition();
}

void UMTS_MapTransitionSubsystem::FailTransition(const FText& FailureReason)
{
	const TArray<FName> FailedBatch = MoveTemp(BatchMapIDs);
	ClearSubMapProgressPlan();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FinalizeMapReadyTimerHandle);
		World->GetTimerManager().ClearTimer(CompletionDelayTimerHandle);
		World->GetTimerManager().ClearTimer(ProgressSmoothingTimerHandle);
	}

	if (ActiveHandler)
	{
		ActiveHandler->OnMapTransitionFailed(this, ActiveRequest, FailureReason);
	}

	SetTransitionPhase(EMTS_MapTransitionPhase::Failed, 1.0f, FailureReason);
	bTransitionInProgress = false;
	bMapReadyReceived = false;
	ReleaseActiveHandler();
	HideLoadingScreen();
	if (GetWorld())
	{
		if (auto* Subsystem = GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>())
		{ for (FName ID : FailedBatch) { Subsystem->UnloadSubMapByID(ID); } }
	}
}

void UMTS_MapTransitionSubsystem::CompleteTransition()
{
	BatchMapIDs.Reset();
	if (!bTransitionInProgress) { return; }
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FinalizeMapReadyTimerHandle);
		World->GetTimerManager().ClearTimer(CompletionDelayTimerHandle);
		World->GetTimerManager().ClearTimer(ProgressSmoothingTimerHandle);
	}

	SetTransitionPhase(EMTS_MapTransitionPhase::Completed, 1.0f, FText::FromString(TEXT("加载完成")));
	bTransitionInProgress = false;
	bMapReadyReceived = false;
	ReleaseActiveHandler();
	TargetProgress = 0.0f;
	HideLoadingScreen();
	ClearSubMapProgressPlan();
}

bool UMTS_MapTransitionSubsystem::ConfigureSubMapProgressPlan(const TMap<FName, float>& MapWeights)
{
	if (!bTransitionInProgress || bMapReadyReceived || HasSubMapProgressPlan() || MapWeights.IsEmpty()
		|| !GetWorld() || !ActiveRequest.bRequireManualMapReadyNotification
		|| CurrentPayload.Phase != EMTS_MapTransitionPhase::WaitingForMapReady) { return false; }
	double Total = 0;
	for (const auto& Pair : MapWeights)
	{
		if (Pair.Key.IsNone() || !FMath::IsFinite(Pair.Value) || Pair.Value <= 0) { return false; }
		Total += Pair.Value;
	}
	if (!FMath::IsFinite(Total)) { return false; }
	ProgressSubMaps = GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>();
	if (!ProgressSubMaps) { return false; }
	// Register before loading; reject already-running IDs to avoid losing early events.
	for (const auto& Map : ProgressSubMaps->GetSubMaps())
	{
		if (MapWeights.Contains(Map.MapID)) { ProgressSubMaps = nullptr; return false; }
	}
	SubMapWeights = MapWeights;
	SubMapProgress.Reset(); CompletedSubMaps.Reset(); DeferredReadySource = NAME_None;
	ProgressSubMaps->OnSubMapProgressChanged.AddUniqueDynamic(this, &ThisClass::HandleSubMapProgress);
	SetTargetProgress(ActiveRequest.AutomaticProgressMax);
	return true;
}

bool UMTS_MapTransitionSubsystem::LoadSubMaps(const TArray<FMTS_SubMapLoadConfig>& Configs, FText& OutError)
{
	OutError = FText();
	if (!bTransitionInProgress || CurrentPayload.Phase != EMTS_MapTransitionPhase::WaitingForMapReady
		|| !ActiveRequest.bRequireManualMapReadyNotification || HasSubMapProgressPlan() || Configs.IsEmpty() || !GetWorld())
	{
		OutError = FText::FromString(TEXT("请在主地图加载完成后提交非空配置数组，并启用主动通知；本次尚不可有其他进度计划。")); return false;
	}
	auto* Subsystem = GetWorld()->GetSubsystem<UMTS_SubMapSubsystem>();
	if (!Subsystem) { OutError = FText::FromString(TEXT("子地图子系统不可用。")); return false; }
	TMap<FName, float> Weights;
	for (const auto& Config : Configs)
	{
		FMTS_SubMapInfo Existing;
		if (Config.MapID.IsNone() || Weights.Contains(Config.MapID) || Subsystem->GetSubMapByKey(Config.MapID, Existing)
			|| !FMath::IsFinite(Config.Weight) || Config.Weight <= 0 || Config.Location.ContainsNaN() || Config.Rotation.ContainsNaN()
			|| Config.MapAsset.IsNull() || !FPackageName::DoesPackageExist(Config.MapAsset.ToSoftObjectPath().GetLongPackageName()))
		{
			OutError = FText::FromString(FString::Printf(TEXT("子地图配置无效：%s。检查唯一 ID、地图、正权重和变换。"), *Config.MapID.ToString())); return false;
		}
		Weights.Add(Config.MapID, Config.Weight);
	}
	if (!ConfigureSubMapProgressPlan(Weights)) { OutError = FText::FromString(TEXT("无法登记子地图统一进度计划。")); return false; }
	Weights.GenerateKeyArray(BatchMapIDs);
	TArray<FName> Submitted;
	for (const auto& Config : Configs)
	{
		auto* Asset = NewObject<UMTS_SubMapDataAsset>(this);
		Asset->MapID = Config.MapID; Asset->MapName = Config.MapID; Asset->MapAsset = Config.MapAsset;
        Asset->MapTags = Config.MapTags;
		if (!bTransitionInProgress || !Subsystem->LoadSubMap(Asset, Config.Location, Config.Rotation, OutError))
		{
			if (OutError.IsEmpty()) { OutError = FText::FromString(TEXT("批量加载已中止。")); }
			ReportMapInitializationFailure(OutError);
			for (FName ID : Submitted) { Subsystem->UnloadSubMapByID(ID); }
			return false;
		}
		Submitted.Add(Config.MapID);
	}
	return true;
}

void UMTS_MapTransitionSubsystem::HandleSubMapProgress(FName MapID, const FMTS_MapTransitionPayload& Payload)
{
	if (!bTransitionInProgress || !SubMapWeights.Contains(MapID)) { return; }
	if (Payload.Phase == EMTS_MapTransitionPhase::Failed)
	{
		ReportMapInitializationFailure(Payload.LoadingContent); return;
	}
	if (!FMath::IsFinite(Payload.Progress)) { return; }
	SubMapProgress.FindOrAdd(MapID) = FMath::Max(SubMapProgress.FindRef(MapID), FMath::Clamp(Payload.Progress, 0.f, 1.f));
	if (Payload.Phase == EMTS_MapTransitionPhase::Completed) { CompletedSubMaps.Add(MapID); }
	double Weighted = 0, Total = 0;
	for (const auto& Pair : SubMapWeights) { Total += Pair.Value; Weighted += Pair.Value * SubMapProgress.FindRef(Pair.Key); }
	const float Overall = ActiveRequest.AutomaticProgressMax + (1 - ActiveRequest.AutomaticProgressMax) * static_cast<float>(Weighted / Total);
	SetTransitionProgress(Overall, Payload.LoadingContent);
	if (CompletedSubMaps.Num() == SubMapWeights.Num() && !bSubMapsReadyNotified)
	{
		bSubMapsReadyNotified = true;
		if (ActiveHandler) { ActiveHandler->OnSubMapsLoaded(this); }
	}
	if (CompletedSubMaps.Num() == SubMapWeights.Num() && !DeferredReadySource.IsNone())
	{
		const FName Source = DeferredReadySource; DeferredReadySource = NAME_None;
		MarkMapReady(Source);
	}
}

void UMTS_MapTransitionSubsystem::ClearSubMapProgressPlan()
{
	if (ProgressSubMaps) { ProgressSubMaps->OnSubMapProgressChanged.RemoveDynamic(this, &ThisClass::HandleSubMapProgress); }
	ProgressSubMaps = nullptr;
	SubMapWeights.Reset(); SubMapProgress.Reset(); CompletedSubMaps.Reset(); DeferredReadySource = NAME_None;
	bSubMapsReadyNotified = false;
}

void UMTS_MapTransitionSubsystem::ReleaseActiveHandler()
{
	if (ActiveHandler)
	{
		ActiveHandler->MarkAsGarbage();
		ActiveHandler = nullptr;
	}
}

void UMTS_MapTransitionSubsystem::SetTransitionPhase(
	EMTS_MapTransitionPhase NewPhase,
	float Progress,
	const FText& LoadingContent)
{
	CurrentPayload.Phase = NewPhase;
	CurrentPayload.LoadingContent = LoadingContent;
	CurrentPayload.bShouldBlockInput =
		NewPhase != EMTS_MapTransitionPhase::None &&
		NewPhase != EMTS_MapTransitionPhase::Completed &&
		NewPhase != EMTS_MapTransitionPhase::Failed;

	SetTargetProgress(FMath::Clamp(Progress, 0.0f, 1.0f) * ActiveRequest.AutomaticProgressMax);
}

void UMTS_MapTransitionSubsystem::ReportMapInitializationFailure(const FText& Error)
{
	if (bTransitionInProgress) { FailTransition(Error); }
}

void UMTS_MapTransitionSubsystem::SetTargetProgress(float Progress)
{
	const float ClampedProgress = FMath::Clamp(Progress, 0.0f, 1.0f);
	TargetProgress = FMath::Max(TargetProgress, ClampedProgress);

	if (ActiveRequest.ProgressSmoothSpeed <= 0.0f || CurrentPayload.Phase == EMTS_MapTransitionPhase::Completed || CurrentPayload.Phase == EMTS_MapTransitionPhase::Failed)
	{
		StopProgressSmoothing();
		CurrentPayload.Progress = TargetProgress;
		BroadcastPayload();
		return;
	}

	StartProgressSmoothing();
	BroadcastPayload();
}

void UMTS_MapTransitionSubsystem::StartProgressSmoothing()
{
	UWorld* World = GetWorld();
	if (!World || World->GetTimerManager().IsTimerActive(ProgressSmoothingTimerHandle))
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		ProgressSmoothingTimerHandle,
		this,
		&UMTS_MapTransitionSubsystem::TickProgressSmoothing,
		1.0f / 30.0f,
		true
	);
}

void UMTS_MapTransitionSubsystem::StopProgressSmoothing()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ProgressSmoothingTimerHandle);
	}
}

void UMTS_MapTransitionSubsystem::TickProgressSmoothing()
{
	if (!bTransitionInProgress)
	{
		StopProgressSmoothing();
		return;
	}

	constexpr float TickInterval = 1.0f / 30.0f;
	constexpr float ProgressSnapTolerance = 0.001f;

	const float PreviousProgress = CurrentPayload.Progress;
	const float NewProgress = FMath::FInterpConstantTo(
		PreviousProgress,
		TargetProgress,
		TickInterval,
		ActiveRequest.ProgressSmoothSpeed
	);

	CurrentPayload.Progress = NewProgress;
	BroadcastPayload();

	if (FMath::Abs(CurrentPayload.Progress - TargetProgress) <= ProgressSnapTolerance)
	{
		CurrentPayload.Progress = TargetProgress;
		BroadcastPayload();
		StopProgressSmoothing();
	}
}

void UMTS_MapTransitionSubsystem::BroadcastPayload()
{
	if (ActiveLoadingWidget)
	{
		ActiveLoadingWidget->ApplyTransitionPayload(CurrentPayload);
	}

	OnTransitionPayloadChanged.Broadcast(CurrentPayload);
}

void UMTS_MapTransitionSubsystem::ShowLoadingScreen()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TSubclassOf<UMTS_MapLoadingWidget> LoadingWidgetClass = ActiveRequest.LoadingWidgetClass;
	if (!LoadingWidgetClass)
	{
		return;
	}

	if (ActiveLoadingWidget && ActiveLoadingWidget->GetClass() != LoadingWidgetClass.Get())
	{
		HideLoadingScreen();
	}

	if (!ActiveLoadingWidget)
	{
		ActiveLoadingWidget = CreateWidget<UMTS_MapLoadingWidget>(World, LoadingWidgetClass);
	}

	if (ActiveLoadingWidget)
	{
		if (!ActiveLoadingWidget->IsInViewport())
		{
			ActiveLoadingWidget->AddToViewport(10000);
		}

		ActiveLoadingWidget->ApplyTransitionPayload(CurrentPayload);
	}
}

void UMTS_MapTransitionSubsystem::HideLoadingScreen()
{
	if (ActiveLoadingWidget)
	{
		ActiveLoadingWidget->RemoveFromParent();
	}

	ActiveLoadingWidget = nullptr;
}

FName UMTS_MapTransitionSubsystem::ResolveMapName(const FMTS_MapTransitionRequest& Request) const
{
	if (!Request.TargetMapName.IsNone())
	{
		return Request.TargetMapName;
	}

	if (Request.TargetMap.IsNull())
	{
		return NAME_None;
	}

	const FString ObjectPath = Request.TargetMap.ToSoftObjectPath().ToString();
	const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
	return PackageName.IsEmpty() ? NAME_None : FName(*PackageName);
}
