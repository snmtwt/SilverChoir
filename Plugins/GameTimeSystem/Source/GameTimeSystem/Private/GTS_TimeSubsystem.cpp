#include "GTS_TimeSubsystem.h"

#include "GTS_TimeManager.h"
#include "GTS_TimeSettings.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

void UGTS_TimeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    TimeTextBindings.Reset();
    UpdatingTimeTextBlocks.Reset();
    TimeTextPruneElapsed = 0.0f;
    const UGTS_TimeSettings* Settings = GetDefault<UGTS_TimeSettings>();
    UClass* ManagerType = Settings->ManagerClass.IsNull()
        ? UGTS_TimeManager::StaticClass() : Settings->ManagerClass.LoadSynchronous();
    if (!ManagerType || !ManagerType->IsChildOf(UGTS_TimeManager::StaticClass())
        || ManagerType->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        InitializationError = NSLOCTEXT("GTS", "InvalidManager", "时间管理类无效，请检查游戏时间系统项目设置。");
        return;
    }
    Manager = NewObject<UGTS_TimeManager>(this, ManagerType);
    Manager->Initialize(GetGameInstance(), Settings->InitialTime, Settings->InitialTimeScale,
        Settings->bInitiallyPaused, Settings->MaxCallbacksPerTick);
    if (!Manager->IsInitialized())
    {
        InitializationError = NSLOCTEXT("GTS", "InvalidConfig", "时间初始化失败，请检查初始日期及非负时间流速。");
        Manager = nullptr;
        return;
    }
    bPauseWithGame = Settings->bPauseWithGame;
    Manager->OnTimeChanged.AddUniqueDynamic(this, &UGTS_TimeSubsystem::HandleTimeChanged);
    // Core ticker receives frame time before World TimeDilation is applied.
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UGTS_TimeSubsystem::TickClock));
}

void UGTS_TimeSubsystem::Deinitialize()
{
    FTSTicker::RemoveTicker(TickerHandle);
    TickerHandle.Reset();
    TimeTextBindings.Empty();
    UpdatingTimeTextBlocks.Empty();
    TimeTextPruneElapsed = 0.0f;
    if (Manager)
    {
        Manager->OnTimeChanged.RemoveDynamic(this, &UGTS_TimeSubsystem::HandleTimeChanged);
        Manager->Shutdown();
        Manager = nullptr;
    }
    Super::Deinitialize();
}

bool UGTS_TimeSubsystem::IsReady() const
{
    return Manager && Manager->IsInitialized();
}

bool UGTS_TimeSubsystem::TickClock(float RealDeltaSeconds)
{
    // 暂停时仍清除已销毁控件；这里不进行日期格式化或逐帧 SetText。
    if (!TimeTextBindings.IsEmpty() && FMath::IsFinite(RealDeltaSeconds) && RealDeltaSeconds > 0.0f)
    {
        TimeTextPruneElapsed += RealDeltaSeconds;
        if (TimeTextPruneElapsed >= 1.0f)
        {
            TimeTextPruneElapsed = 0.0f;
            PruneTimeTextBindings();
        }
    }
    UWorld* World = GetWorld();
    if (IsReady() && World && World->IsGameWorld() && World->HasBegunPlay()
        && !World->bIsTearingDown && (!bPauseWithGame || !World->IsPaused()))
    {
        Manager->AdvanceTime(RealDeltaSeconds);
    }
    return true;
}

bool UGTS_TimeSubsystem::RegisterTimeTextBlock(UTextBlock* TextBlock, const FText& Format)
{
    check(IsInGameThread());
    if (!IsReady() || !IsValid(TextBlock) || TextBlock->IsTemplate())
    {
        return false;
    }
    const UWorld* WidgetWorld = TextBlock->GetWorld();
    if (WidgetWorld && (WidgetWorld->bIsTearingDown || WidgetWorld->GetGameInstance() != GetGameInstance()))
    {
        return false;
    }
    PruneTimeTextBindings();
    TimeTextBindings.Add(TWeakObjectPtr<UTextBlock>(TextBlock), Format);
    RefreshTimeTextBlock(TextBlock, Manager->GetCurrentTime(), Format);
    return true;
}

bool UGTS_TimeSubsystem::UnregisterTimeTextBlock(UTextBlock* TextBlock)
{
    check(IsInGameThread());
    return TextBlock && TimeTextBindings.Remove(TWeakObjectPtr<UTextBlock>(TextBlock)) > 0;
}

bool UGTS_TimeSubsystem::IsTimeTextBlockRegistered(UTextBlock* TextBlock) const
{
    check(IsInGameThread());
    return IsValid(TextBlock) && TimeTextBindings.Contains(TWeakObjectPtr<UTextBlock>(TextBlock));
}

int32 UGTS_TimeSubsystem::GetRegisteredTimeTextBlockCount() const
{
    check(IsInGameThread());
    int32 Count = 0;
    for (const TPair<TWeakObjectPtr<UTextBlock>, FText>& Binding : TimeTextBindings)
    {
        if (Binding.Key.IsValid()) ++Count;
    }
    return Count;
}

FText UGTS_TimeSubsystem::FormatTimeText(FDateTime Time, const FText& Format)
{
    if (Time.GetTicks() < FDateTime::MinValue().GetTicks() || Time.GetTicks() > FDateTime::MaxValue().GetTicks())
    {
        return FText::GetEmpty();
    }

    FText Weekday;
    switch (Time.GetDayOfWeek())
    {
    case EDayOfWeek::Monday: Weekday = NSLOCTEXT("GTS", "Monday", "星期一"); break;
    case EDayOfWeek::Tuesday: Weekday = NSLOCTEXT("GTS", "Tuesday", "星期二"); break;
    case EDayOfWeek::Wednesday: Weekday = NSLOCTEXT("GTS", "Wednesday", "星期三"); break;
    case EDayOfWeek::Thursday: Weekday = NSLOCTEXT("GTS", "Thursday", "星期四"); break;
    case EDayOfWeek::Friday: Weekday = NSLOCTEXT("GTS", "Friday", "星期五"); break;
    case EDayOfWeek::Saturday: Weekday = NSLOCTEXT("GTS", "Saturday", "星期六"); break;
    case EDayOfWeek::Sunday: Weekday = NSLOCTEXT("GTS", "Sunday", "星期日"); break;
    default: break;
    }

    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Year"), FText::FromString(FString::Printf(TEXT("%04d"), Time.GetYear())));
    Arguments.Add(TEXT("Month"), FText::FromString(FString::Printf(TEXT("%02d"), Time.GetMonth())));
    Arguments.Add(TEXT("Day"), FText::FromString(FString::Printf(TEXT("%02d"), Time.GetDay())));
    Arguments.Add(TEXT("Hour"), FText::FromString(FString::Printf(TEXT("%02d"), Time.GetHour())));
    Arguments.Add(TEXT("Minute"), FText::FromString(FString::Printf(TEXT("%02d"), Time.GetMinute())));
    Arguments.Add(TEXT("Second"), FText::FromString(FString::Printf(TEXT("%02d"), Time.GetSecond())));
    Arguments.Add(TEXT("Weekday"), Weekday);
    return FText::Format(Format, Arguments);
}

void UGTS_TimeSubsystem::RefreshTimeTextBlock(UTextBlock* TextBlock, FDateTime Time, const FText& Format)
{
    if (!IsValid(TextBlock)) return;
    const TWeakObjectPtr<UTextBlock> Key(TextBlock);
    // 原生文本变更通知可能重新登记同一控件；防止 SetText 递归，最新格式留待下次时间更新。
    if (UpdatingTimeTextBlocks.Contains(Key)) return;
    const FText NewText = FormatTimeText(Time, Format);
    if (!TextBlock->GetText().EqualTo(NewText))
    {
        const TStrongObjectPtr<UGTS_TimeSubsystem> KeepSubsystemAlive(this);
        const TStrongObjectPtr<UTextBlock> KeepTextAlive(TextBlock);
        UpdatingTimeTextBlocks.Add(Key);
        ON_SCOPE_EXIT { UpdatingTimeTextBlocks.Remove(Key); };
        TextBlock->SetText(NewText);
    }
}

void UGTS_TimeSubsystem::HandleTimeChanged(FDateTime CurrentTime)
{
    // 由整秒变化/显式设置事件驱动，不为每个控件创建 Tick。
    const TStrongObjectPtr<UGTS_TimeSubsystem> KeepAlive(this);
    TArray<TWeakObjectPtr<UTextBlock>> Keys;
    TimeTextBindings.GetKeys(Keys);
    for (const TWeakObjectPtr<UTextBlock>& Key : Keys)
    {
        if (!IsReady()) break;
        UTextBlock* TextBlock = Key.Get();
        if (!IsValid(TextBlock))
        {
            TimeTextBindings.Remove(Key);
            continue;
        }
        const FText* RegisteredFormat = TimeTextBindings.Find(Key);
        if (!RegisteredFormat) continue;
        // SetText 的通知可能取消/新增绑定甚至结束子系统；不跨过该调用保留 Map 元素引用。
        const FText FormatCopy = *RegisteredFormat;
        RefreshTimeTextBlock(TextBlock, CurrentTime, FormatCopy);
    }
}

void UGTS_TimeSubsystem::PruneTimeTextBindings()
{
    for (auto It = TimeTextBindings.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid()) It.RemoveCurrent();
    }
}
