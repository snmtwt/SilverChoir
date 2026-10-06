#include "SubSystem/GameMapTransitionSystem/GameMapSubMapHandler.h"

#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadLibrary.h"
#include "SubSystem/PlayerSquadSubSystem/PlayerSquadManagerBase.h"
#include "SubSystem/PlayerUnitSubSystem/UnitSpawner.h"
#include "SubSystem/PlayerUnitSubSystem/UnitSquadSpawner.h"
#include "Engine/Engine.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameMapSubMapHandler, Log, All);

UGameMapSubMapHandler::UGameMapSubMapHandler()
{
    OverviewSceneTag = FGameplayTag::RequestGameplayTag(TEXT("GameScene.BaseOverview"), false);
}

bool UGameMapSubMapHandler::ReportGameMapError(const FText& Error, FText& OutError)
{
    const FText Reason = Error.IsEmpty() ? NSLOCTEXT("GameMapHandler", "UnknownError", "游戏地图操作失败，未提供错误详情。") : Error;
    const FString Message = FString::Printf(TEXT("GameMapSubMapHandler: 游戏地图 [%s]：%s"), *GetMapID().ToString(), *Reason.ToString());
    UE_LOG(LogGameMapSubMapHandler, Error, TEXT("%s"), *Message);
    if (GEngine) GEngine->AddOnScreenDebugMessage(INDEX_NONE, 8.f, FColor::Red, Message);
    return RejectOperation(Reason, OutError);
}

bool UGameMapSubMapHandler::ValidateActiveMap(FMTS_SubMapInfo& OutMap, FText& OutError) const
{
    OutError = FText::GetEmpty();
    if (!GetWorld() || GetWorld()->bIsTearingDown || !GetSubMapInfo(OutMap)
        || (OutMap.State != EMTS_SubMapState::Loading && OutMap.State != EMTS_SubMapState::Loaded)
        || OutMap.LoadingPayload.Phase == EMTS_MapTransitionPhase::Failed)
    {
        OutError = NSLOCTEXT("GameMapHandler", "InactiveHandler", "处理对象不再属于当前子地图，或子地图正在卸载、已失败。");
        return false;
    }
    return true;
}

void UGameMapSubMapHandler::PrepareTileEntry()
{
    FText Error;
    FMTS_SubMapInfo Map;
    if (!ValidateActiveMap(Map, Error))
    {
        ReportGameMapError(Error, Error);
        return;
    }
    if (bEnteringMap)
    {
        ReportGameMapError(NSLOCTEXT("GameMapHandler", "EnteringMap", "正在提交本次地图进入，请勿重复进入。"), Error);
        return;
    }
    ClearHandlerError();
    if (!WaitForEntryScene(OverviewSceneTag, Error))
    {
        // New loads report through OnSubMapLoadFailed; a resident map remains loaded on entry failure.
        const FText EntryError = Error;
        if (!FailLoading(EntryError)) ReportGameMapError(EntryError, Error);
    }
}

void UGameMapSubMapHandler::OnSubMapLoaded_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map)
{
    if (!IsValid(MapSubsystem) || Map.MapID != GetMapID() || MapSubsystem->GetSubMapHandler(Map.MapID) != this) return;
    PrepareTileEntry();
}

void UGameMapSubMapHandler::OnEntrySceneReady_Implementation(FGameplayTag SceneTag)
{
    FText Error;
    FMTS_SubMapInfo Map;
    if (!ValidateActiveMap(Map, Error))
    {
        ReportGameMapError(Error, Error);
        return;
    }
    if (bEnteringMap || SceneTag != OverviewSceneTag || IsWaitingForEntryScene())
    {
        ReportGameMapError(NSLOCTEXT("GameMapHandler", "InvalidEntryCallback", "进入前场景尚未就绪、标签不匹配，或正在提交地图进入。"), Error);
        return;
    }
    TGuardValue<bool> EntryGuard(bEnteringMap, true);
    const bool bResidentMap = IsSubMapReady();
    if (!CommitMapEntry(LocalEntryTransform, MapType, Error))
    {
        ReportGameMapError(Error, Error);
        return;
    }
    // Initial entry activates from OnSubMapReady. CommitMapEntry activates a resident map immediately.
    if (bResidentMap && !RefreshBattleUI(Error)) ReportGameMapError(Error, Error);
}

void UGameMapSubMapHandler::OnSubMapReady_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map)
{
    if (!IsValid(MapSubsystem) || Map.MapID != GetMapID() || MapSubsystem->GetSubMapHandler(Map.MapID) != this) return;
    FText Error;
    if (bEnteringMap)
    {
        ReportGameMapError(NSLOCTEXT("GameMapHandler", "ReadyReentry", "正在处理地图就绪事件，请勿重复激活。"), Error);
        return;
    }
    TGuardValue<bool> EntryGuard(bEnteringMap, true);
    FMTS_SubMapInfo Current;
    if (!ValidateActiveMap(Current, Error) || !ActivateThisMap(LocalEntryTransform, MapType, Error)
        || !RefreshBattleUI(Error)) ReportGameMapError(Error, Error);
}

bool UGameMapSubMapHandler::RefreshBattleUI(FText& OutError)
{
    FMTS_SubMapInfo Map;
    if (!ValidateActiveMap(Map, OutError)) return false;
    if (MapType != EGameMainMapType::Battle) return true;
    const auto* State = GetWorld()->GetGameState<AGameMainMapGameState>();
    if (!State || State->ActiveMapID != GetMapID() || State->GetCurrentMapType() != MapType)
    {
        OutError = NSLOCTEXT("GameMapHandler", "MapChangedBeforeUI", "初始化战斗UI前活动地图已改变。");
        return false;
    }
    const TArray<FGuid> Roster = SquadIds;
    bool bFoundLocalController = false;
    for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        auto* Controller = Cast<AGameMainMapPlayerController>(It->Get());
        if (!IsValid(Controller) || !Controller->IsLocalController()) continue;
        bFoundLocalController = true;
        if (!IsValid(Controller->BattleWidget))
        {
            OutError = NSLOCTEXT("GameMapHandler", "MissingBattleWidget", "地图已激活，但主地图玩家控制器未创建战斗UI。");
            return false;
        }
        if (!Controller->BattleWidget->InitializeBattleUI(Roster, OutError)) return false;
        // Blueprint UI callbacks may unload the map; do not update another controller after that.
        if (!ValidateActiveMap(Map, OutError)) return false;
        if (!IsValid(State) || State->ActiveMapID != GetMapID() || State->GetCurrentMapType() != MapType)
        {
            OutError = NSLOCTEXT("GameMapHandler", "MapChangedDuringUI", "初始化战斗UI期间活动地图已改变。");
            return false;
        }
    }
    if (!bFoundLocalController && GetWorld()->GetNetMode() != NM_DedicatedServer)
    {
        OutError = NSLOCTEXT("GameMapHandler", "MissingController", "无法初始化战斗UI：没有本地主地图玩家控制器。");
        return false;
    }
    return true;
}

void UGameMapSubMapHandler::OnSubMapLoadFailed_Implementation(UMTS_SubMapSubsystem* MapSubsystem,
    const FMTS_SubMapInfo& Map, const FText& Error)
{
    if (!IsValid(MapSubsystem) || Map.MapID != GetMapID() || MapSubsystem->GetSubMapHandler(Map.MapID) != this) return;
    CancelEntrySceneWait();
    FText Ignored;
    ReportGameMapError(FText::Format(NSLOCTEXT("GameMapHandler", "LoadFailed", "子地图加载或进入失败：{0}"), Error), Ignored);
}

bool UGameMapSubMapHandler::ValidateSquadSpawner(AUnitSquadSpawner* SquadSpawner, const FMTS_SubMapInfo& Map, FText& OutError) const
{
    if (!IsValid(SquadSpawner) || SquadSpawner->IsActorBeingDestroyed())
    {
        OutError = NSLOCTEXT("GameMapHandler", "InvalidSpawner", "小队生成器无效或已销毁；请显式修正注册列表。");
        return false;
    }
    if (SquadSpawner->GetWorld() != GetWorld() || !IsValid(Map.StreamingLevel)
        || !Map.StreamingLevel->GetLoadedLevel() || SquadSpawner->GetLevel() != Map.StreamingLevel->GetLoadedLevel())
    {
        OutError = NSLOCTEXT("GameMapHandler", "WrongSpawnerLevel", "小队生成器必须位于本处理对象所管理的子地图实例中。");
        return false;
    }
    return true;
}

bool UGameMapSubMapHandler::RegisterSquadSpawner(AUnitSquadSpawner* SquadSpawner, FText& OutError)
{
    OutError = FText::GetEmpty();
    ClearHandlerError();
    FMTS_SubMapInfo Map;
    if (!ValidateActiveMap(Map, OutError) || !ValidateSquadSpawner(SquadSpawner, Map, OutError))
        return ReportGameMapError(OutError, OutError);
    if (RegisteredSquadSpawners.Contains(TWeakObjectPtr<AUnitSquadSpawner>(SquadSpawner))) return true;
    if (bSpawningSquads)
        return ReportGameMapError(NSLOCTEXT("GameMapHandler", "RegisterDuringSpawn", "生成小队期间不能修改生成器注册列表。"), OutError);
    RegisteredSquadSpawners.Add(SquadSpawner);
    return true;
}

bool UGameMapSubMapHandler::UnregisterSquadSpawner(AUnitSquadSpawner* SquadSpawner, FText& OutError)
{
    OutError = FText::GetEmpty();
    ClearHandlerError();
    if (bSpawningSquads)
        return ReportGameMapError(NSLOCTEXT("GameMapHandler", "UnregisterDuringSpawn", "生成小队期间不能修改生成器注册列表。"), OutError);
    FMTS_SubMapInfo Map;
    if (!ValidateActiveMap(Map, OutError)) return ReportGameMapError(OutError, OutError);
    if (!SquadSpawner)
        return ReportGameMapError(NSLOCTEXT("GameMapHandler", "NullUnregister", "注销小队生成器时必须提供原生成器对象。"), OutError);
    const int32 Index = RegisteredSquadSpawners.IndexOfByKey(TWeakObjectPtr<AUnitSquadSpawner>(SquadSpawner));
    if (Index == INDEX_NONE)
        return ReportGameMapError(NSLOCTEXT("GameMapHandler", "NotRegistered", "该小队生成器尚未注册到本处理对象。"), OutError);
    // Explicit unregister is the only operation that removes a slot; never compact expired weak references.
    RegisteredSquadSpawners.RemoveAt(Index);
    return true;
}

TArray<AUnitSquadSpawner*> UGameMapSubMapHandler::GetRegisteredSquadSpawners() const
{
    TArray<AUnitSquadSpawner*> Result;
    Result.Reserve(RegisteredSquadSpawners.Num());
    for (const TWeakObjectPtr<AUnitSquadSpawner>& WeakSpawner : RegisteredSquadSpawners)
    {
        AUnitSquadSpawner* Spawner = WeakSpawner.Get();
        Result.Add(IsValid(Spawner) && !Spawner->IsActorBeingDestroyed() ? Spawner : nullptr);
    }
    return Result;
}

bool UGameMapSubMapHandler::SpawnSquads(const TArray<FGuid>& InSquadIds, FText& OutError)
{
    OutError = FText::GetEmpty();
    ClearHandlerError();
    if (bSpawningSquads)
        return ReportGameMapError(NSLOCTEXT("GameMapHandler", "SpawnReentry", "正在生成小队，请勿重复调用。"), OutError);
    // Check the authored slot count before touching any actor. Destroyed references still count as slots.
    if (InSquadIds.Num() != RegisteredSquadSpawners.Num())
        return ReportGameMapError(FText::Format(NSLOCTEXT("GameMapHandler", "SquadCountMismatch",
            "生成小队失败：小队数量（{0}）与已注册生成器数量（{1}）不一致，本次未执行生成。"),
            FText::AsNumber(InSquadIds.Num()), FText::AsNumber(RegisteredSquadSpawners.Num())), OutError);
    FMTS_SubMapInfo Map;
    if (!ValidateActiveMap(Map, OutError)) return ReportGameMapError(OutError, OutError);
    TGuardValue<bool> SpawnGuard(bSpawningSquads, true);
    const TArray<FGuid> IdSnapshot = InSquadIds;
    const TArray<TWeakObjectPtr<AUnitSquadSpawner>> SpawnerSnapshot = RegisteredSquadSpawners;
    if (IdSnapshot.IsEmpty()) return true;
    TSet<FGuid> UniqueSquadIds;
    for (const FGuid& SquadId : IdSnapshot)
    {
        if (UniqueSquadIds.Contains(SquadId))
            return ReportGameMapError(NSLOCTEXT("GameMapHandler", "DuplicateSquadId",
                "生成小队失败：小队ID数组包含重复ID，同一小队不能分配给多个生成器，本次未执行生成。"), OutError);
        UniqueSquadIds.Add(SquadId);
    }
    UPlayerSquadManagerBase* Manager = UPlayerSquadLibrary::GetPlayerSquadManager(this);
    if (!IsValid(Manager) || !Manager->IsReady())
        return ReportGameMapError(NSLOCTEXT("GameMapHandler", "MissingSquadManager", "玩家小队管理器尚未就绪，本次未执行生成。"), OutError);

    // Validate every pair before the first SpawnSquad call; common data/configuration failures cannot leave a partial batch.
    for (int32 Index = 0; Index < IdSnapshot.Num(); ++Index)
    {
        auto RejectPair = [this, Index, &OutError](const FText& Error)
        {
            return ReportGameMapError(FText::Format(NSLOCTEXT("GameMapHandler", "InvalidSquadPair",
                "第 {0} 组小队与生成器校验失败，本次未执行生成：{1}"), FText::AsNumber(Index + 1), Error), OutError);
        };
        AUnitSquadSpawner* Spawner = SpawnerSnapshot[Index].Get();
        if (!ValidateSquadSpawner(Spawner, Map, OutError)) return RejectPair(OutError);
        FSquadData Squad;
        if (!IdSnapshot[Index].IsValid() || !Manager->GetSquad(IdSnapshot[Index], Squad) || Squad.MemberUnitIds.IsEmpty())
            return RejectPair(NSLOCTEXT("GameMapHandler", "InvalidSquad", "小队不存在或没有成员。"));
        UClass* SpawnerClass = Spawner->UnitSpawnerClass.Get();
        if (!SpawnerClass || SpawnerClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
            return RejectPair(NSLOCTEXT("GameMapHandler", "InvalidUnitSpawnerClass", "未配置有效的单位生成器类。"));
        const TArray<TSharedPtr<FUnitData>> Members = Manager->GetSquadUnitsShared(IdSnapshot[Index]);
        const TArray<FTransform> Transforms = Spawner->GetFormationTransforms(Squad.MemberUnitIds.Num());
        if (Members.Num() != Squad.MemberUnitIds.Num() || Transforms.Num() != Members.Num())
            return RejectPair(NSLOCTEXT("GameMapHandler", "InvalidFormation", "小队成员数据缺失，或人数、间距、生成偏移配置无效。"));
        for (int32 MemberIndex = 0; MemberIndex < Members.Num(); ++MemberIndex)
        {
            if (!AUnitSpawner::ValidateUnitData(Members[MemberIndex], OutError)) return RejectPair(OutError);
            if (Members[MemberIndex]->UnitId != Squad.MemberUnitIds[MemberIndex] || !Transforms[MemberIndex].IsValid())
                return RejectPair(NSLOCTEXT("GameMapHandler", "InvalidMemberOrder", "小队成员顺序或单位生成位置无效。"));
        }
    }
    for (int32 Index = 0; Index < IdSnapshot.Num(); ++Index)
    {
        AUnitSquadSpawner* Spawner = SpawnerSnapshot[Index].Get();
        // Spawn callbacks may unload this map or destroy a later spawner. Stop without addressing a replacement handler.
        if (!ValidateActiveMap(Map, OutError) || !ValidateSquadSpawner(Spawner, Map, OutError)
            || !Spawner->SpawnSquad(IdSnapshot[Index], OutError))
            return ReportGameMapError(FText::Format(NSLOCTEXT("GameMapHandler", "SquadSpawnFailed",
                "第 {0} 组小队生成失败，已停止后续生成（此前已成功的小队保留）：{1}"),
                FText::AsNumber(Index + 1), OutError), OutError);
    }
    if (!ValidateActiveMap(Map, OutError)) return ReportGameMapError(OutError, OutError);
    // A later Blueprint callback can remove or reassign an earlier spawner. Report without rolling back scene changes.
    for (int32 Index = 0; Index < SpawnerSnapshot.Num(); ++Index)
    {
        AUnitSquadSpawner* Spawner = SpawnerSnapshot[Index].Get();
        if (!ValidateSquadSpawner(Spawner, Map, OutError))
            return ReportGameMapError(FText::Format(NSLOCTEXT("GameMapHandler", "SpawnerChangedAfterSpawn",
                "生成回调结束后第 {0} 个生成器已失效（此前的生成结果保留）：{1}"),
                FText::AsNumber(Index + 1), OutError), OutError);
        if (Spawner->GetSpawnedSquadId() != IdSnapshot[Index])
            return ReportGameMapError(FText::Format(NSLOCTEXT("GameMapHandler", "SquadChangedAfterSpawn",
                "生成回调结束后第 {0} 个生成器的小队已改变，此前的生成结果保留。"),
                FText::AsNumber(Index + 1)), OutError);
    }
    return true;
}

void UGameMapSubMapHandler::OnReleaseResources()
{
    Super::OnReleaseResources();
    // Actors belong to the streamed level. Clearing references must never destroy scene-owned spawners.
    RegisteredSquadSpawners.Reset();
    SquadIds.Reset();
}
