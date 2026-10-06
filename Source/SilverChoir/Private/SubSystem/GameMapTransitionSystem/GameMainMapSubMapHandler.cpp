#include "SubSystem/GameMapTransitionSystem/GameMainMapSubMapHandler.h"
#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "SMS_SceneLibrary.h"
#include "SMS_SceneManager.h"
#include "Engine/World.h"

bool UGameMainMapSubMapHandler::CommitMapEntry(const FTransform& LocalEntryTransform, EGameMainMapType MapType, FText& OutError)
{
    OutError = FText();
    ClearHandlerError();
    FTransform WorldEntry;
    FMTS_SubMapInfo Map;
    if (!GetSubMapInfo(Map) || Map.State != EMTS_SubMapState::Loaded || !Map.bIsVisible
        || !GetWorldTransform(LocalEntryTransform, WorldEntry))
        return RejectOperation(NSLOCTEXT("SubMapHandler", "EntryNotLoaded", "子地图尚未显示、已卸载或进入点无效。"), OutError);
    if (MapType != EGameMainMapType::Base && MapType != EGameMainMapType::Battle)
        return RejectOperation(NSLOCTEXT("SubMapHandler", "EntryType", "请选择基地或战斗地图类型。"), OutError);
    if (IsWaitingForEntryScene())
        return RejectOperation(NSLOCTEXT("SubMapHandler", "SceneNotReady", "请等待进入前场景切换完成。"), OutError);
    if (IsSubMapReady()) return ActivateThisMap(LocalEntryTransform, MapType, OutError);
    if (!FinishLoading())
        return RejectOperation(NSLOCTEXT("SubMapHandler", "FinishRejected", "当前子地图不能提交初始化完成通知。"), OutError);
    return true;
}

bool UGameMainMapSubMapHandler::ActivateThisMap(const FTransform& LocalEntryTransform, EGameMainMapType MapType, FText& OutError)
{
    OutError = FText();
    ClearHandlerError();
    FTransform WorldEntry;
    auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AGameMainMapGameMode>() : nullptr;
    if (!IsSubMapReady() || IsWaitingForEntryScene() || !GetWorldTransform(LocalEntryTransform, WorldEntry)
        || !Mode || !Mode->ActivateLoadedMap(GetMapID(), WorldEntry, MapType))
        return RejectOperation(NSLOCTEXT("SubMapHandler", "ActivationRejected", "无法激活子地图：请检查初始化状态、进入点和主地图状态。"), OutError);
    return true;
}

bool UGameMainMapSubMapHandler::WaitForEntryScene(FGameplayTag SceneTag, FText& OutError)
{
    OutError = FText();
    ClearHandlerError();
    FMTS_SubMapInfo Map;
    if (!SceneTag.IsValid() || !GetSubMapInfo(Map) || Map.State != EMTS_SubMapState::Loaded || !Map.bIsVisible
        || Map.LoadingPayload.Phase == EMTS_MapTransitionPhase::Failed || !GetWorld() || GetWorld()->bIsTearingDown)
        return RejectOperation(NSLOCTEXT("SubMapHandler", "InvalidSceneRequest", "进入前场景标签无效，或子地图尚未显示。"), OutError);
    if (IsWaitingForEntryScene())
    {
        if (WaitingSceneTag == SceneTag) return true;
        return RejectOperation(NSLOCTEXT("SubMapHandler", "AlreadyWaiting", "处理对象正在等待另一个场景。"), OutError);
    }
    auto* Manager = USMS_SceneLibrary::GetSceneManager(this);
    if (!Manager || !Manager->IsReady() || Manager->GetTransitionState() != ESMS_SceneTransitionState::Idle)
        return RejectOperation(NSLOCTEXT("SubMapHandler", "SceneBusy", "场景管理器不可用或正在切换，请稍后重试。"), OutError);
    if (Manager->GetCurrentSceneTag() == SceneTag)
    {
        OnEntrySceneReady(SceneTag);
        return true;
    }
    WaitingSceneManager = Manager;
    WaitingSceneTag = SceneTag;
    Manager->OnSceneChanged.AddUniqueDynamic(this, &ThisClass::HandleEntrySceneChanged);
    if (!Manager->SwitchScene(SceneTag))
    {
        const FText Error = Manager->GetLastError();
        CancelEntrySceneWait();
        return RejectOperation(Error, OutError);
    }
    return true;
}

void UGameMainMapSubMapHandler::HandleEntrySceneChanged(FGameplayTag PreviousSceneTag, FGameplayTag CurrentSceneTag)
{
    if (!IsWaitingForEntryScene() || CurrentSceneTag != WaitingSceneTag) return;
    // Clear before calling Blueprint so a synchronous callback cannot leave a stale subscription.
    CancelEntrySceneWait();
    FMTS_SubMapInfo Map;
    if (GetSubMapInfo(Map) && Map.State == EMTS_SubMapState::Loaded) OnEntrySceneReady(CurrentSceneTag);
}

void UGameMainMapSubMapHandler::CancelEntrySceneWait()
{
    if (auto* Manager = WaitingSceneManager.Get())
        Manager->OnSceneChanged.RemoveDynamic(this, &ThisClass::HandleEntrySceneChanged);
    WaitingSceneManager.Reset();
    WaitingSceneTag = FGameplayTag();
}

void UGameMainMapSubMapHandler::OnReleaseResources()
{
    CancelEntrySceneWait();
    Super::OnReleaseResources();
}
