#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/BattlePersonnelCardTestObserver.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/BattleMap/BattleResourceBar.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "SubSystem/PlayerUnitSubSystem/PlayerUnitLibrary.h"
#include "UIBasic/ECGWidget.h"
#include "H5UI_View.h"
#include "Widget/SlotContainerByType/SIS_MirrorSlotContainer.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include <limits>

namespace BattlePersonnelCardTest
{
constexpr float CardWidth = 234.f;
constexpr float CardHeight = 148.f;

class FRuntime : public IAutomationLatentCommand
{
public:
    explicit FRuntime(FAutomationTestBase* InTest) : Test(InTest) {}
    virtual ~FRuntime() override { Cleanup(); }

    virtual bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 60.)
        {
            Test->AddError(FString::Printf(TEXT("Personnel card preview timed out at stage %d"), Stage));
            return true;
        }
        if (Stage == 0)
        {
            if (!GEngine || !GEngine->GameViewport) return false;
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                if (Context.WorldType == EWorldType::Game && Context.World())
                    PC = Cast<AGameMainMapPlayerController>(Context.World()->GetFirstPlayerController());
            }
            if (!PC.IsValid()) return false;
            if (!CreatePreview()) return true;
            Advance();
            return false;
        }
        if (FPlatformTime::Seconds() - StageStarted < .5) return false;

        switch (Stage)
        {
        case 1:
            if (!ValidateBindingsAndLayout()) return true;
            ValidateSnapshotSetters();
            BeforeSelectionSize = Cards[0]->GetCachedGeometry().GetLocalSize();
            BeforeSelectionDesiredSize = Cards[0]->GetDesiredSize();
            PointerClick(Cards[0]);
            Advance();
            return false;
        case 2:
            Test->TestEqual(TEXT("One real Slate press/release emits exactly one selection request"), Observer->Requests, 1);
            Test->TestEqual(TEXT("Selection request identifies the clicked member"), Observer->LastChoice,
                FName(*Fixtures[0].UnitId.ToString()));
            Test->TestFalse(TEXT("The card does not select itself after a request"), Cards[0]->IsMemberSelected());
            Test->TestFalse(TEXT("Delayed click has completed"), Cards[0]->IsPressPending());
            // This is the owning roster's response, intentionally outside the card.
            Cards[0]->SetSelected(true);
            Cards[0]->SetMember(Fixtures[0]);
            Advance();
            return false;
        case 3:
            Test->TestEqual(TEXT("Selection event is not repeated on subsequent frames"), Observer->Requests, 1);
            Test->TestTrue(TEXT("SetMember preserves owner-controlled selection"), Cards[0]->IsMemberSelected());
            Test->TestTrue(TEXT("Selection does not alter allocated size"),
                Cards[0]->GetCachedGeometry().GetLocalSize().Equals(BeforeSelectionSize, .1f));
            Test->TestTrue(TEXT("Selection does not alter desired size"),
                Cards[0]->GetDesiredSize().Equals(BeforeSelectionDesiredSize, .1f));
            Cards[0]->SetSelected(false);
            Cards[1]->SetSelected(true);
            Advance();
            return false;
        case 4:
            if (!MonitorsReady()) return false;
            ValidateECGAndInventory();
            ValidateAuthoritativeInventoryUnchanged();
            Capture();
            Advance();
            return false;
        case 5:
            if (FScreenshotRequest::IsScreenshotRequested()
                || IFileManager::Get().FileSize(*ScreenshotPath) <= 0
                || IFileManager::Get().GetTimeStamp(*ScreenshotPath) == PreviousScreenshotTime)
            {
                return false;
            }
            if (!Test->HasAnyErrors())
                Test->AddInfo(TEXT("PERSONNEL_CARD_RUNTIME_OK original 234x148 layout, native bars, owner selection, single pointer event, sanitized vitals, ECG flatline and interactive untouched mirror slots"));
            Test->AddInfo(FString::Printf(TEXT("Personnel card screenshot: %s"), *ScreenshotPath));
            Cleanup();
            return true;
        default:
            return true;
        }
    }

private:
    FAutomationTestBase* Test;
    TWeakObjectPtr<AGameMainMapPlayerController> PC;
    TStrongObjectPtr<UCanvasPanel> Root;
    TStrongObjectPtr<UBattleHUDTestData> SourceData;
    TStrongObjectPtr<UBattlePersonnelCardTestObserver> Observer;
    TSharedPtr<SWidget> Preview;
    TArray<UBattlePersonnelCardWidget*> Cards;
    TArray<FBattleMemberView> Fixtures;
    TArray<FBattleSquadView> OriginalTestSquads;
    TMap<FGuid, TArray<FSIS_ItemData>> OriginalInventories;
    FVector2D BeforeSelectionSize;
    FVector2D BeforeSelectionDesiredSize;
    FString ScreenshotPath;
    FDateTime PreviousScreenshotTime;
    double Started = FPlatformTime::Seconds();
    double StageStarted = Started;
    int32 Stage = 0;

    void Advance() { ++Stage; StageStarted = FPlatformTime::Seconds(); }

    void Cleanup()
    {
        for (auto* Card : Cards)
        {
            if (!IsValid(Card)) continue;
            Card->CancelPendingClick();
            if (Observer.IsValid())
                Card->OnSelectionRequested.RemoveDynamic(Observer.Get(), &UBattlePersonnelCardTestObserver::RecordSelection);
        }
        if (Preview.IsValid() && GEngine && GEngine->GameViewport)
            GEngine->GameViewport->RemoveViewportWidgetContent(Preview.ToSharedRef());
        Preview.Reset();
        if (Root.IsValid()) Root->ClearChildren();
        Cards.Reset();
        Root.Reset();
        Observer.Reset();
        SourceData.Reset();
    }

    bool CreatePreview()
    {
        UClass* CardClass = LoadClass<UBattlePersonnelCardWidget>(nullptr,
            TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_人员卡片.WBP_人员卡片_C"));
        if (!Test->TestNotNull(TEXT("Chinese personnel-card Blueprint uses the native card parent"), CardClass)) return false;
        Test->TestTrue(TEXT("Card class remains compatible with Battle HUD member cards"),
            CardClass->IsChildOf(UBattleMemberCardWidget::StaticClass()));
        SourceData.Reset(LoadObject<UBattleHUDTestData>(nullptr,
            TEXT("/Game/System/Map/BattleMap/UI/Data/DA_BattleHUDTestUnits.DA_BattleHUDTestUnits")));
        if (!Test->TestNotNull(TEXT("Existing Battle HUD fixture asset is available"), SourceData.Get())) return false;
        if (!Test->TestTrue(TEXT("Fixture asset provides four portraits"),
            SourceData->Squads.Num() > 0 && SourceData->Squads[0].Members.Num() >= 4)) return false;
        OriginalTestSquads = SourceData->Squads;
        for (const FGuid Id : UPlayerUnitLibrary::GetPlayerUnitDataIDs(PC.Get()))
        {
            FUnitData Snapshot;
            if (UPlayerUnitLibrary::GetPlayerUnitDataSnapshot(PC.Get(), Id, Snapshot))
                OriginalInventories.Add(Id, Snapshot.InventoryItems);
        }

        Root.Reset(NewObject<UCanvasPanel>(PC.Get()));
        Observer.Reset(NewObject<UBattlePersonnelCardTestObserver>());
        auto* Background = NewObject<UImage>(Root.Get());
        Background->SetColorAndOpacity(FLinearColor(FColor(5, 12, 19)));
        Background->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* BackgroundSlot = Root->AddChildToCanvas(Background);
        BackgroundSlot->SetAnchors(FAnchors(0, 0, 1, 1));
        BackgroundSlot->SetOffsets(FMargin(0));

        const FVector2D ViewSize = GEngine->GameViewport->Viewport
            ? FVector2D(GEngine->GameViewport->Viewport->GetSizeXY()) : FVector2D(1920, 1080);
        const float StartX = FMath::Max(24.f, (float(ViewSize.X) - CardWidth * 4.f - 36.f * 3.f) * .5f);
        const float RowY = FMath::Max(220.f, float(ViewSize.Y) * .43f);
        AddLabel(TEXT("SILVER CHOIR  /  PERSONNEL"), {StartX, RowY - 126.f}, 13, FColor(73, 142, 165), 700.f);
        AddLabel(TEXT("人员卡片 · 状态预览"), {StartX, RowY - 96.f}, 28, FColor(212, 235, 243), 900.f);
        AddLabel(TEXT("原尺寸 234 × 148  ·  体力 / 士气  ·  原生选中状态"), {StartX, RowY - 52.f}, 12, FColor(108, 146, 163), 900.f);
        const TCHAR* Captions[] = {TEXT("未选中 / 稳定"), TEXT("选中 / 稳定"), TEXT("低状态 / 危急"), TEXT("失能 / 平线")};
        const float Health[] = {1.f, 1.f, .21f, 0.f};
        const float Stamina[] = {.92f, .74f, .18f, 0.f};
        const float Morale[] = {.86f, .9f, .23f, 0.f};
        for (int32 Index = 0; Index < 4; ++Index)
        {
            FBattleMemberView Data = SourceData->Squads[0].Members[Index];
            Data.Health = Health[Index];
            Data.Stamina = Stamina[Index];
            Data.Morale = Morale[Index];
            Fixtures.Add(Data);
            auto* Card = CreateWidget<UBattlePersonnelCardWidget>(PC.Get(), CardClass);
            if (!Test->TestNotNull(TEXT("Personnel card instance"), Card)) return false;
            Card->SetMember(Data);
            Card->SetSelected(Index == 1);
            auto* Slot = Root->AddChildToCanvas(Card);
            Slot->SetPosition({StartX + Index * (CardWidth + 36.f), RowY});
            // Use the asset's intrinsic dimensions; the fixture must not mask a
            // layout regression by forcing the expected dimensions externally.
            Slot->SetAutoSize(true);
            Cards.Add(Card);
            AddLabel(Captions[Index], {StartX + Index * (CardWidth + 36.f), RowY + CardHeight + 18.f},
                13, Index == 1 ? FColor(57, 211, 230) : FColor(126, 158, 174), CardWidth);
        }
        Cards[0]->OnSelectionRequested.AddUniqueDynamic(Observer.Get(), &UBattlePersonnelCardTestObserver::RecordSelection);
        Preview = Root->TakeWidget();
        GEngine->GameViewport->AddViewportWidgetContent(Preview.ToSharedRef(), 100);
        return true;
    }

    void AddLabel(const FString& Text, FVector2D Position, int32 FontSize, FColor Color, float Width)
    {
        auto* Label = NewObject<UTextBlock>(Root.Get());
        Label->SetText(FText::FromString(Text));
        Label->SetColorAndOpacity(FLinearColor(Color));
        Label->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), FontSize));
        Label->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Slot = Root->AddChildToCanvas(Label);
        Slot->SetPosition(Position);
        Slot->SetSize({Width, 42.f});
    }

    bool ValidateBindingsAndLayout()
    {
        bool bBindingsValid = true;
        for (auto* Card : Cards)
        {
            bBindingsValid &= Test->TestNotNull(TEXT("Portrait binding"), Card->Portrait.Get());
            bBindingsValid &= Test->TestNotNull(TEXT("MemberName binding"), Card->MemberName.Get());
            bBindingsValid &= Test->TestNotNull(TEXT("HealthText binding"), Card->HealthText.Get());
            bBindingsValid &= Test->TestNotNull(TEXT("Native stamina bar binding"), Cast<UBattleResourceBar>(Card->StaminaBar.Get()));
            bBindingsValid &= Test->TestNotNull(TEXT("Native morale bar binding"), Cast<UBattleResourceBar>(Card->MoraleBar.Get()));
            bBindingsValid &= Test->TestNotNull(TEXT("Reusable ECG binding"), Card->ECGMonitor.Get());
            if (Card->ECGMonitor)
                bBindingsValid &= Test->TestNotNull(TEXT("ECG native view binding"),
                    Cast<UH5UI_View>(Card->ECGMonitor->GetWidgetFromName(TEXT("ECGView"))));
            bBindingsValid &= Test->TestNotNull(TEXT("Original SIS mirror binding"), Cast<USIS_MirrorSlotContainer>(Card->HandEquipment.Get()));
            Test->TestTrue(TEXT("Intrinsic card dimensions remain 234x148"), Card->GetDesiredSize().Equals({CardWidth, CardHeight}, .1f));
            Test->TestTrue(TEXT("Actual card dimensions remain 234x148"), Card->GetCachedGeometry().GetLocalSize().Equals({CardWidth, CardHeight}, .1f));
            auto* Body = Cast<USizeBox>(Card->GetWidgetFromName(TEXT("SizeBox_202")));
            if (Test->TestNotNull(TEXT("Authored body size box is preserved"), Body))
            {
                Test->TestEqual(TEXT("Body width is unchanged"), Body->GetWidthOverride(), 230.f);
                Test->TestEqual(TEXT("Body height is unchanged"), Body->GetHeightOverride(), 144.f);
            }
            auto* PortraitFrame = Cast<USizeBox>(Card->GetWidgetFromName(TEXT("SizeBox_0")));
            if (Test->TestNotNull(TEXT("Authored portrait frame is preserved"), PortraitFrame))
                Test->TestTrue(TEXT("Portrait retains its 80x80 allocation"), PortraitFrame->GetCachedGeometry().GetLocalSize().Equals(FVector2f(80.f, 80.f), .1f));
            Test->TestEqual(TEXT("The card itself receives selection input"), Card->GetVisibility(), ESlateVisibility::Visible);
        }
        Test->TestFalse(TEXT("Unselected fixture starts unselected"), Cards[0]->IsMemberSelected());
        Test->TestTrue(TEXT("Selected fixture starts selected"), Cards[1]->IsMemberSelected());
        return bBindingsValid;
    }

    void ValidateSnapshotSetters()
    {
        auto* Card = Cards[1];
        FBattleMemberView Invalid = Fixtures[1];
        Invalid.Health = std::numeric_limits<float>::quiet_NaN();
        Invalid.Stamina = -2.f;
        Invalid.Morale = 4.f;
        UBattleMemberCardWidget* CompatibleCard = Card;
        CompatibleCard->SetMember(Invalid);
        Test->TestEqual(TEXT("SetMember sanitizes NaN through the existing HUD base API"), Card->Member.Health, 0.f);
        Test->TestEqual(TEXT("SetMember stores normalized stamina in its snapshot"), Card->Member.Stamina, 0.f);
        Test->TestEqual(TEXT("SetMember stores normalized morale in its snapshot"), Card->Member.Morale, 1.f);
        Test->TestEqual(TEXT("SetMember clamps negative stamina"), Card->StaminaBar->GetPercent(), 0.f);
        Test->TestEqual(TEXT("SetMember clamps excessive morale"), Card->MoraleBar->GetPercent(), 1.f);
        Card->SetMemberVitals(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), -.5f);
        Test->TestEqual(TEXT("Vitals sanitize non-finite health"), Card->Member.Health, 0.f);
        Test->TestEqual(TEXT("Vitals sanitize non-finite stamina"), Card->Member.Stamina, 0.f);
        Test->TestEqual(TEXT("Vitals clamp morale below zero"), Card->Member.Morale, 0.f);
        Test->TestEqual(TEXT("Dead state immediately clears ECG strength"), Card->ECGMonitor->HeartbeatIntensity, 0.f);
        Card->SetMemberVitals(2.f, .37f, .54f);
        Test->TestEqual(TEXT("Vitals clamp health above one"), Card->Member.Health, 1.f);
        Test->TestEqual(TEXT("Vitals preserve fractional stamina"), Card->StaminaBar->GetPercent(), .37f);
        Test->TestEqual(TEXT("Vitals preserve fractional morale"), Card->MoraleBar->GetPercent(), .54f);
        Test->TestEqual(TEXT("Vitals preserve unit identity"), Card->Member.UnitId, Fixtures[1].UnitId);
        Test->TestEqual(TEXT("Vitals preserve selection identity"), Card->ChoiceID, FName(*Fixtures[1].UnitId.ToString()));
        Test->TestTrue(TEXT("Snapshot updates retain selected state"), Card->IsMemberSelected());
        Card->SetMember(Fixtures[1]);
    }

    bool IsInHitPath(UWidget* Widget, FVector2D Position)
    {
        const auto Slate = Widget->TakeWidget();
        const auto Window = FSlateApplication::Get().FindWidgetWindow(Slate);
        if (!Window.IsValid()) return false;
        const auto Path = Window->GetHittestGrid().GetBubblePath(Position, 0.f, false);
        return Path.ContainsByPredicate([&](const FWidgetAndPointer& Entry) { return Entry.Widget == Slate; });
    }

    void PointerClick(UBattlePersonnelCardWidget* Card)
    {
        const auto Geometry = Card->GetCachedGeometry();
        const FVector2D Position = Geometry.LocalToAbsolute(FVector2f(40.f, 40.f));
        Test->TestTrue(TEXT("Card portrait area is in the real Slate hit path"), IsInHitPath(Card, Position));
        TSet<FKey> Keys;
        Keys.Add(EKeys::LeftMouseButton);
        const FPointerEvent Down(0, Position, Position, Keys, EKeys::LeftMouseButton, 0, FModifierKeysState());
        Test->TestTrue(TEXT("Card handles pointer press"), Card->TakeWidget()->OnMouseButtonDown(Geometry, Down).IsEventHandled());
        Keys.Reset();
        const FPointerEvent Up(0, Position, Position, Keys, EKeys::LeftMouseButton, 0, FModifierKeysState());
        Test->TestTrue(TEXT("Card handles pointer release"), Card->TakeWidget()->OnMouseButtonUp(Geometry, Up).IsEventHandled());
    }

    bool MonitorsReady() const
    {
        for (auto* Card : Cards)
        {
            const auto* View = Cast<UH5UI_View>(Card->ECGMonitor->GetWidgetFromName(TEXT("ECGView")));
            if (!View || View->ViewState != EH5UI_ViewState::Ready) return false;
        }
        return true;
    }

    void ValidateECGAndInventory()
    {
        for (int32 Index = 0; Index < Cards.Num(); ++Index)
        {
            auto* Card = Cards[Index];
            Test->TestEqual(TEXT("Displayed name comes from the member snapshot"),
                Card->MemberName->GetText().ToString(), Fixtures[Index].Profile.CodeName.ToString());
            Test->TestNotNull(TEXT("Fixture portrait remains available"), Card->Portrait->GetBrush().GetResourceObject());
            auto* Mirror = CastChecked<USIS_MirrorSlotContainer>(Card->HandEquipment.Get());
            Test->TestEqual(TEXT("Original inventory mirror Blueprint is retained"), Mirror->GetClass()->GetPathName(),
                FString(TEXT("/StrategyInventorySystem/Widget/Slot/WBP_MirrorSlotContainer.WBP_MirrorSlotContainer_C")));
            Test->TestFalse(TEXT("Hand equipment is not locked by card styling"), Mirror->bSlotLocked);
            Test->TestTrue(TEXT("Hand equipment retains drag-drop acceptance"), Mirror->bAcceptDraggedItems);
            Test->TestTrue(TEXT("Hand equipment remains enabled"), Mirror->GetIsEnabled());
            Test->TestNull(TEXT("Display-only snapshot does not bind a real inventory"), Mirror->OwnerInventoryComponent);
            Test->TestFalse(TEXT("Display-only snapshot does not invent an item instance"), Mirror->BelongingItemData.IsValid());
            Test->TestEqual(TEXT("Left and right mirror containers are retained"), Mirror->SlotContainerList.Num(), 2);
            for (auto* Container : Mirror->SlotContainerList)
            {
                if (!Test->TestNotNull(TEXT("Mirror child container exists"), Container)) continue;
                Test->TestEqual(TEXT("Snapshot hand IDs do not create inventory icons"), Container->ItemIconMap.Num(), 0);
                Test->TestTrue(TEXT("Mirror container owns an interactive slot"), Container->SlotMap.Num() > 0);
                for (const auto& Pair : Container->SlotMap)
                {
                    if (!Test->TestNotNull(TEXT("Mirror slot widget exists"), Pair.Value)) continue;
                    const auto Geometry = Pair.Value->GetCachedGeometry();
                    Test->TestTrue(TEXT("Inventory slot remains reachable through the Slate hit grid"),
                        IsInHitPath(Pair.Value, Geometry.LocalToAbsolute(Geometry.GetLocalSize() * .5)));
                }
            }
        }
        Test->TestEqual(TEXT("Death fixture keeps ECG intensity at zero"), Cards[3]->ECGMonitor->HeartbeatIntensity, 0.f);
        auto* DeadView = CastChecked<UH5UI_View>(Cards[3]->ECGMonitor->GetWidgetFromName(TEXT("ECGView")));
        FString Result, Error;
        Test->TestTrue(TEXT("Native ECG page has no residual waveform after death"),
            DeadView->ExecuteJavaScript(TEXT("ecgMonitor.parameters.intensity===0&&ecgMonitor.samples.every(v=>v===0)"), Result, Error)
            && Result == TEXT("true"));
    }

    void ValidateAuthoritativeInventoryUnchanged()
    {
        const TArray<FGuid> CurrentIds = UPlayerUnitLibrary::GetPlayerUnitDataIDs(PC.Get());
        Test->TestEqual(TEXT("UI fixtures do not create authoritative units"), CurrentIds.Num(), OriginalInventories.Num());
        for (const auto& Pair : OriginalInventories)
        {
            FUnitData Current;
            if (!Test->TestTrue(TEXT("Existing authoritative unit remains available"),
                UPlayerUnitLibrary::GetPlayerUnitDataSnapshot(PC.Get(), Pair.Key, Current))) continue;
            if (!Test->TestEqual(TEXT("Card interaction does not add or remove inventory entries"),
                Current.InventoryItems.Num(), Pair.Value.Num())) continue;
            for (int32 Index = 0; Index < Pair.Value.Num(); ++Index)
                Test->TestTrue(TEXT("Card interaction does not modify authoritative item data"),
                    FSIS_ItemData::StaticStruct()->CompareScriptStruct(&Pair.Value[Index], &Current.InventoryItems[Index], 0));
        }
        Test->TestEqual(TEXT("Fixture asset retains its original squads"), SourceData->Squads.Num(), OriginalTestSquads.Num());
        for (int32 Index = 0; Index < FMath::Min(SourceData->Squads.Num(), OriginalTestSquads.Num()); ++Index)
            Test->TestTrue(TEXT("UI edits do not mutate the source data asset"),
                FBattleSquadView::StaticStruct()->CompareScriptStruct(&OriginalTestSquads[Index], &SourceData->Squads[Index], 0));
    }

    void Capture()
    {
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Screenshots/PersonnelCard");
        IFileManager::Get().MakeDirectory(*Directory, true);
        ScreenshotPath = Directory / TEXT("01_States.png");
        PreviousScreenshotTime = IFileManager::Get().GetTimeStamp(*ScreenshotPath);
        FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBattlePersonnelCardRuntimeTest,
    "SilverChoir.BattleUI.PersonnelCard.RuntimeFlow",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBattlePersonnelCardRuntimeTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(BattlePersonnelCardTest::FRuntime(this));
    return true;
}
#endif
