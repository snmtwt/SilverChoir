#include "OperationsCommandRoomUIAssetsCommandlet.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Font.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"

UOperationsCommandRoomUIAssetsCommandlet::UOperationsCommandRoomUIAssetsCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

namespace OperationsUIAssets
{
constexpr const TCHAR* RoomPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/OperationsCommandRoom/WBP_OperationsCommandRoom");
FLinearColor Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }

bool Check(bool Condition, const TCHAR* Message)
{
    if (!Condition) UE_LOG(LogTemp, Error, TEXT("OPERATIONS_UI_CHECK_FAILED %s"), Message);
    return Condition;
}

void Inspect(UWidgetBlueprint* BP)
{
    UE_LOG(LogTemp, Display, TEXT("OPERATIONS_UI_ASSET %s parent=%s"), *BP->GetPathName(), *GetNameSafe(BP->ParentClass));
    BP->WidgetTree->ForEachWidget([](UWidget* W)
    {
        UE_LOG(LogTemp, Display, TEXT("OPERATIONS_UI_WIDGET %s class=%s visibility=%d"),
            *W->GetName(), *W->GetClass()->GetName(), int32(W->GetVisibility()));
    });
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
        for (const UEdGraphNode* Node : Graph->Nodes)
            UE_LOG(LogTemp, Display, TEXT("OPERATIONS_UI_NODE %s %s %s"), *Graph->GetName(),
                *Node->GetClass()->GetName(), *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
}

struct FDesigner
{
    UWidgetTree* Tree;
    UCanvasPanelSlot* Place(UCanvasPanel* Parent, UWidget* W, FAnchors Anchors, FMargin Offsets)
    {
        auto* Slot = Parent->AddChildToCanvas(W);
        Slot->SetAnchors(Anchors);
        Slot->SetOffsets(Offsets);
        return Slot;
    }
    UCanvasPanel* Panel(const TCHAR* Name)
    {
        auto* Result = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), Name);
        Result->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        return Result;
    }
    UImage* Box(UCanvasPanel* Parent, const FName Name, const TCHAR* Hex, FAnchors Anchors, FMargin Offsets, bool HitTest = false)
    {
        auto* W = Tree->ConstructWidget<UImage>(UImage::StaticClass(), Name);
        W->SetColorAndOpacity(Color(Hex));
        W->SetVisibility(HitTest ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
        Place(Parent, W, Anchors, Offsets);
        return W;
    }
    UTextBlock* Text(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Value, int32 Size,
        const TCHAR* Hex, FMargin Offsets, FAnchors Anchors = FAnchors(0,0))
    {
        auto* W = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
        W->SetText(FText::FromString(Value));
        W->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), Size, TEXT("Regular")));
        W->SetColorAndOpacity(Color(Hex));
        W->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(Parent, W, Anchors, Offsets);
        return W;
    }
    void Frame(UCanvasPanel* Parent, const FString& Prefix)
    {
        Box(Parent, *(Prefix + TEXT("Background")), TEXT("07111DF2"), FAnchors(0,0,1,1), FMargin(0), true);
        Box(Parent, *(Prefix + TEXT("TopRule")), TEXT("294554"), FAnchors(0,0,1,0), FMargin(0,0,0,1));
        Box(Parent, *(Prefix + TEXT("BottomRule")), TEXT("294554"), FAnchors(0,1,1,1), FMargin(0,-1,0,1));
        Box(Parent, *(Prefix + TEXT("LeftRule")), TEXT("294554"), FAnchors(0,0,0,1), FMargin(0,0,1,0));
        Box(Parent, *(Prefix + TEXT("RightRule")), TEXT("294554"), FAnchors(1,0,1,1), FMargin(-1,0,1,0));
        Box(Parent, *(Prefix + TEXT("Accent")), TEXT("42BCD9"), FAnchors(0,0), FMargin(0,14,2,22));
    }
};
}

int32 UOperationsCommandRoomUIAssetsCommandlet::Main(const FString& Params)
{
    using namespace OperationsUIAssets;
    auto* BP = LoadObject<UWidgetBlueprint>(nullptr, RoomPath);
    if (!Check(BP && BP->WidgetTree && BP->ParentClass && BP->ParentClass->IsChildOf(UOperationsCommandRoomWidget::StaticClass()),
        TEXT("Missing existing command room widget or unexpected parent"))) return 1;
    Inspect(BP);
    if (!Params.Contains(TEXT("Apply"))) return 0;
    if (BP->WidgetTree->FindWidget(TEXT("OperationsCanvas")))
    {
        UE_LOG(LogTemp, Display, TEXT("OPERATIONS_UI_ALREADY_AUTHORED: preserve subsequent Designer edits"));
        return 0;
    }
    auto* Root = Cast<UOverlay>(BP->WidgetTree->RootWidget);
    auto* SelectionBP = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/System/UIBasic/WBP_SelectionButton"));
    if (!Check(Root && Root->GetChildrenCount() == 0 && SelectionBP && SelectionBP->GeneratedClass,
        TEXT("Expected empty ContentRoot and existing selection button; preserve authored content"))) return 1;
    const FString File = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    const FString Backup = FPaths::ProjectSavedDir() / TEXT("OperationsCommandRoomUI/Backups") /
        FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) / FPaths::GetCleanFilename(File);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
    if (!Check(IFileManager::Get().Copy(*Backup, *File) == COPY_OK, TEXT("Backup failed"))) return 1;

    FDesigner D{BP->WidgetTree};
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* Canvas = D.Panel(TEXT("OperationsCanvas"));
    auto* RootSlot = Root->AddChildToOverlay(Canvas);
    RootSlot->SetHorizontalAlignment(HAlign_Fill);
    RootSlot->SetVerticalAlignment(VAlign_Fill);
    auto* Right = D.Panel(TEXT("RightPanel"));
    D.Place(Canvas, Right, FAnchors(1,0,1,1), FMargin(-360,0,360,0));

    auto* Time = D.Panel(TEXT("TimeControlPanel"));
    D.Place(Right, Time, FAnchors(0,0,1,0), FMargin(0,0,0,216));
    D.Frame(Time, TEXT("TimePanel"));
    D.Text(Time, TEXT("TimePanelTitle"), TEXT("时间进度"), 16, TEXT("DCE9F5"), FMargin(20,13,170,26));
    D.Text(Time, TEXT("TimePanelCaption"), TEXT("TIME CONTROL"), 9, TEXT("668B9F"), FMargin(-140,19,120,18), FAnchors(1,0))->SetJustification(ETextJustify::Right);
    D.Box(Time, TEXT("TimeHeaderRule"), TEXT("203C4B"), FAnchors(0,0,1,0), FMargin(20,48,20,1));
    D.Text(Time, TEXT("DateText"), TEXT("2049.01.01"), 12, TEXT("8EADBE"), FMargin(20,61,210,20));
    D.Text(Time, TEXT("ClockText"), TEXT("00:00:00"), 27, TEXT("E1EEF5"), FMargin(18,83,220,38));
    D.Text(Time, TEXT("TimeStateText"), TEXT("正常 1×"), 11, TEXT("55C7DF"), FMargin(-116,96,96,22), FAnchors(1,0))->SetJustification(ETextJustify::Right);
    auto* Progress = D.Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("DayProgress"));
    FProgressBarStyle ProgressStyle;
    ProgressStyle.BackgroundImage.DrawAs = ESlateBrushDrawType::Image;
    ProgressStyle.BackgroundImage.TintColor = FSlateColor(Color(TEXT("203B4B")));
    ProgressStyle.FillImage.DrawAs = ESlateBrushDrawType::Image;
    ProgressStyle.FillImage.TintColor = FSlateColor(FLinearColor::White);
    Progress->SetWidgetStyle(ProgressStyle);
    Progress->SetFillColorAndOpacity(Color(TEXT("42BCD9")));
    Progress->SetPercent(0.f);
    Progress->SetVisibility(ESlateVisibility::HitTestInvisible);
    D.Place(Time, Progress, FAnchors(0,0,1,0), FMargin(20,130,20,3));
    D.Text(Time, TEXT("DayStartLabel"), TEXT("00:00"), 9, TEXT("58788C"), FMargin(20,138,70,16));
    D.Text(Time, TEXT("DayEndLabel"), TEXT("24:00"), 9, TEXT("58788C"), FMargin(-90,138,70,16), FAnchors(1,0))->SetJustification(ETextJustify::Right);

    auto Button = [&](const TCHAR* Name, const TCHAR* Label, const TCHAR* Choice, float X)
    {
        auto* W = D.Tree->ConstructWidget<USelectionButtonWidget>(SelectionBP->GeneratedClass.Get(), Name);
        W->ChoiceID = Choice;
        W->ButtonText = FText::FromString(Label);
        W->ButtonSubtitle = FText::GetEmpty();
        W->ButtonIndex = FText::GetEmpty();
        W->MinimumSize = FVector2D(154,36);
        W->Font.Size = 12;
        W->bUseTabStyle = true;
        W->HoverSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuHover_Electronic"));
        W->PressSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuPress_Terminal"));
        D.Place(Time, W, FAnchors(0,0), FMargin(X,166,154,36));
    };
    Button(TEXT("NormalSpeedButton"), TEXT("正常  1×"), TEXT("Normal"), 20);
    Button(TEXT("FastForwardButton"), TEXT("快进  4×"), TEXT("FastForward"), 186);

    auto* Info = D.Panel(TEXT("TileInfoPanel"));
    D.Place(Right, Info, FAnchors(0,0,1,1), FMargin(0,232,0,0));
    D.Frame(Info, TEXT("TilePanel"));
    D.Text(Info, TEXT("TilePanelTitle"), TEXT("地图信息"), 16, TEXT("DCE9F5"), FMargin(20,13,180,26));
    D.Box(Info, TEXT("TileHeaderRule"), TEXT("203C4B"), FAnchors(0,0,1,0), FMargin(20,48,20,1));
    auto* Content = D.Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("TileInfoContent"));
    Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    D.Place(Info, Content, FAnchors(0,0,1,1), FMargin(20,64,20,20));
    Info->SetVisibility(ESlateVisibility::Collapsed);

    BP->WidgetTree->ForEachWidget([BP](UWidget* W)
    {
        if (!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))
            BP->WidgetVariableNameToGuidMap.Add(W->GetFName(), FGuid::NewGuid());
    });
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
    if (!Check(Results.NumErrors == 0 && BP->Status != BS_Error, TEXT("Command room Blueprint compile failed"))) return 1;
    BP->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    if (!Check(UPackage::SavePackage(BP->GetOutermost(), BP, *File, Args), TEXT("Command room asset save failed"))) return 1;
    Inspect(BP);
    UE_LOG(LogTemp, Display, TEXT("OPERATIONS_UI_ASSETS_OK backup=%s"), *Backup);
    return 0;
}
