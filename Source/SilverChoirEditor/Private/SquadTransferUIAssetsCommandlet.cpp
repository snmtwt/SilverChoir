#include "SquadTransferUIAssetsCommandlet.h"

#include "Animation/WidgetAnimation.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Data/Squads/SquadStructs.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/DataTable.h"
#include "Engine/Font.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"

USquadTransferUIAssetsCommandlet::USquadTransferUIAssetsCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

namespace SquadTransferUIAssets
{
constexpr const TCHAR* RoomPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/WBP_SquadMeetingRoom");
constexpr const TCHAR* ButtonPath = TEXT("/Game/System/UIBasic/WBP_ShellButton");
constexpr const TCHAR* NamesPath = TEXT("/Game/System/SubSystem/PlayerSquadSubSystem/DT_SquadNames");

bool Check(bool bCondition, const FString& Detail)
{
    if (!bCondition) UE_LOG(LogTemp, Error, TEXT("SQUAD_TRANSFER_UI_CHECK_FAILED %s"), *Detail);
    return bCondition;
}

FLinearColor Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }

template<class T> T* Find(UWidgetBlueprint* BP, const TCHAR* Name)
{
    T* Widget = BP && BP->WidgetTree ? Cast<T>(BP->WidgetTree->FindWidget(Name)) : nullptr;
    Check(Widget != nullptr, FString::Printf(TEXT("%s: required widget missing or wrong type: %s"), *GetNameSafe(BP), Name));
    return Widget;
}

FString Placement(const UWidget* Widget)
{
    const auto* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
    if (!Slot) return GetNameSafe(Widget ? Widget->GetParent() : nullptr);
    const FAnchors Anchors = Slot->GetAnchors();
    const FMargin Offsets = Slot->GetOffsets();
    return FString::Printf(TEXT("%s|%g,%g,%g,%g|%g,%g,%g,%g|%s|%d|%d"),
        *GetNameSafe(Widget->GetParent()), Anchors.Minimum.X, Anchors.Minimum.Y, Anchors.Maximum.X, Anchors.Maximum.Y,
        Offsets.Left, Offsets.Top, Offsets.Right, Offsets.Bottom, *Slot->GetAlignment().ToString(), Slot->GetAutoSize(), Slot->GetZOrder());
}

FString GraphFingerprint(UBlueprint* BP)
{
    TArray<FString> Records;
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            const FString Prefix = Graph->GetName() + TEXT("|") + Node->NodeGuid.ToString() + TEXT("|");
            Records.Add(Prefix + Node->GetClass()->GetName() + TEXT("|") + Node->NodeComment);
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                const FString PinPrefix = Prefix + Pin->PinName.ToString() + TEXT("|") + FString::FromInt(int32(Pin->Direction)) + TEXT("|");
                Records.Add(PinPrefix + Pin->DefaultValue + TEXT("|") + GetPathNameSafe(Pin->DefaultObject));
                for (const UEdGraphPin* Other : Pin->LinkedTo)
                    Records.Add(PinPrefix + Other->GetOwningNode()->NodeGuid.ToString() + TEXT(".") + Other->PinName.ToString());
            }
        }
    Records.Sort();
    return FString::Join(Records, TEXT("\n"));
}

FString AnimationFingerprint(UWidgetBlueprint* BP)
{
    TArray<FString> Records;
    for (const UWidgetAnimation* Animation : BP->Animations)
    {
        if (!Animation) continue;
        Records.Add(FString::Printf(TEXT("%s|%g|%g|%s"), *Animation->GetName(), Animation->GetStartTime(), Animation->GetEndTime(), *GetPathNameSafe(Animation->MovieScene)));
        for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
            Records.Add(FString::Printf(TEXT("%s|%s|%s|%s|%d"), *Animation->GetName(), *Binding.WidgetName.ToString(),
                *Binding.SlotWidgetName.ToString(), *Binding.AnimationGuid.ToString(), Binding.bIsRootWidget));
    }
    Records.Sort();
    return FString::Join(Records, TEXT("\n"));
}

bool ValidateTree(UWidgetBlueprint* BP)
{
    if (!Check(BP && BP->WidgetTree && BP->GeneratedClass, TEXT("missing widget Blueprint, tree or generated class"))) return false;
    TSet<FName> Names;
    bool bUnique = true;
    BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (Names.Contains(Widget->GetFName())) bUnique = false;
        Names.Add(Widget->GetFName());
    });
    if (!Check(bUnique, TEXT("duplicate widget names: ") + BP->GetName())) return false;
    for (const UWidgetAnimation* Animation : BP->Animations)
    {
        if (!Check(Animation && Animation->MovieScene, TEXT("invalid widget animation"))) return false;
        for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
            if (!Binding.bIsRootWidget && !Check(Names.Contains(Binding.WidgetName), TEXT("missing animation target: ") + Binding.WidgetName.ToString())) return false;
    }
    return true;
}

bool Backup(UObject* Asset)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    if (!FPaths::FileExists(Filename)) return true;
    const FString BackupFile = FPaths::ProjectSavedDir() / TEXT("SquadTransferUIBackups") /
        (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)) / FPaths::GetCleanFilename(Filename);
    if (!Check(IFileManager::Get().MakeDirectory(*FPaths::GetPath(BackupFile), true), TEXT("cannot create backup directory"))
        || !Check(IFileManager::Get().Copy(*BackupFile, *Filename) == COPY_OK, TEXT("cannot back up original asset"))) return false;
    UE_LOG(LogTemp, Display, TEXT("SQUAD_TRANSFER_UI_BACKUP %s"), *BackupFile);
    return true;
}

bool Compile(UWidgetBlueprint* BP)
{
    BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
            BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
    });
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
    return Check(Results.NumErrors == 0 && BP->Status != BS_Error, TEXT("widget Blueprint did not compile: ") + BP->GetName());
}

bool Save(UObject* Asset)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    if (!Check(IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true), TEXT("cannot create asset directory"))) return false;
    Asset->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return Check(UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args), TEXT("could not save: ") + Asset->GetName());
}

struct FDesigner
{
    UWidgetTree* Tree;
    UClass* ButtonClass;

    template<class T> T* New(const TCHAR* Name) { return Tree->ConstructWidget<T>(T::StaticClass(), Name); }

    UCanvasPanel* Canvas(const TCHAR* Name)
    {
        auto* Result = New<UCanvasPanel>(Name);
        Result->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        return Result;
    }

    UCanvasPanelSlot* Place(UCanvasPanel* Parent, UWidget* Widget, FAnchors Anchors, FMargin Offsets, int32 Z = 0)
    {
        UCanvasPanelSlot* Slot = Parent->AddChildToCanvas(Widget);
        Slot->SetAnchors(Anchors);
        Slot->SetOffsets(Offsets);
        Slot->SetAlignment(FVector2D::ZeroVector);
        Slot->SetAutoSize(false);
        Slot->SetZOrder(Z);
        return Slot;
    }

    UImage* Plate(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Hex, FAnchors Anchors, FMargin Offsets, int32 Z = 0)
    {
        auto* Result = New<UImage>(Name);
        Result->SetColorAndOpacity(Color(Hex));
        Result->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(Parent, Result, Anchors, Offsets, Z);
        return Result;
    }

    UTextBlock* Text(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Caption, int32 FontSize, const TCHAR* Hex, FAnchors Anchors, FMargin Offsets)
    {
        auto* Result = New<UTextBlock>(Name);
        Result->SetText(FText::FromString(Caption));
        Result->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), FontSize, TEXT("Regular")));
        Result->SetColorAndOpacity(Color(Hex));
        Result->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(Parent, Result, Anchors, Offsets, 2);
        return Result;
    }

    UBasicButtonWidget* Button(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Caption, FAnchors Anchors, FMargin Offsets)
    {
        auto* Result = Tree->ConstructWidget<UBasicButtonWidget>(ButtonClass, Name);
        Result->ButtonText = FText::FromString(Caption);
        Result->ButtonSubtitle = FText::GetEmpty();
        Result->ButtonIndex = FText::GetEmpty();
        Result->ContentMode = EBasicButtonContent::TextOnly;
        Result->MinimumSize = FVector2D::ZeroVector;
        Result->Font.Size = 12;
        Result->SetVisibility(ESlateVisibility::Visible);
        Place(Parent, Result, Anchors, Offsets, 2);
        return Result;
    }
};

bool ValidateDialog(UWidgetBlueprint* BP)
{
    if (!ValidateTree(BP)) return false;
    auto* Root = Find<UCanvasPanel>(BP, TEXT("SquadRoomLayout"));
    auto* Panel = Find<UCanvasPanel>(BP, TEXT("TransferConfirmPanel"));
    auto* Scrim = Find<UImage>(BP, TEXT("TransferConfirmScrim"));
    auto* Fit = Find<UScaleBox>(BP, TEXT("TransferConfirmFit"));
    auto* Extent = Find<USizeBox>(BP, TEXT("TransferConfirmExtent"));
    auto* Card = Find<UCanvasPanel>(BP, TEXT("TransferConfirmCard"));
    auto* Body = Find<UTextBlock>(BP, TEXT("TransferConfirmText"));
    auto* Scroll = Find<UScrollBox>(BP, TEXT("TransferConfirmBodyScroll"));
    auto* Confirm = Find<UBasicButtonWidget>(BP, TEXT("ConfirmUnitTransferButton"));
    auto* Cancel = Find<UBasicButtonWidget>(BP, TEXT("CancelUnitTransferButton"));
    if (!Root || !Panel || !Scrim || !Fit || !Extent || !Card || !Body || !Scroll || !Confirm || !Cancel) return false;
    auto* PanelSlot = Cast<UCanvasPanelSlot>(Panel->Slot);
    return Check(Panel->GetParent() == Root && Scrim->GetParent() == Panel && Fit->GetParent() == Panel
            && Extent->GetParent() == Fit && Card->GetParent() == Extent && Body->GetParent() == Scroll && Scroll->GetParent() == Card
            && Confirm->GetParent() == Card && Cancel->GetParent() == Card, TEXT("unexpected transfer dialog hierarchy"))
        && Check(Panel->GetVisibility() == ESlateVisibility::Collapsed, TEXT("transfer dialog must start collapsed"))
        && Check(Scrim->GetVisibility() == ESlateVisibility::Visible, TEXT("transfer scrim must intercept clicks"))
        && Check(PanelSlot && PanelSlot->GetZOrder() >= 200, TEXT("transfer dialog must be above room controls"));
}

bool BuildDialog(UWidgetBlueprint* BP)
{
    if (!ValidateTree(BP)) return false;
    if (BP->WidgetTree->FindWidget(TEXT("TransferConfirmPanel")))
    {
        UE_LOG(LogTemp, Display, TEXT("SQUAD_TRANSFER_UI_ALREADY_PATCHED preserving authored dialog"));
        return ValidateDialog(BP);
    }
    for (const TCHAR* Name : {TEXT("TransferConfirmScrim"), TEXT("TransferConfirmFit"), TEXT("TransferConfirmExtent"), TEXT("TransferConfirmCard"),
        TEXT("TransferConfirmText"), TEXT("TransferConfirmBodyScroll"), TEXT("ConfirmUnitTransferButton"), TEXT("CancelUnitTransferButton")})
        if (!Check(!BP->WidgetTree->FindWidget(Name), FString::Printf(TEXT("%s exists without the dialog root; refusing partial replacement"), Name))) return false;
    auto* Root = Find<UCanvasPanel>(BP, TEXT("SquadRoomLayout"));
    auto* ButtonBP = LoadObject<UWidgetBlueprint>(nullptr, ButtonPath);
    if (!Root || !Check(ButtonBP && ButtonBP->GeneratedClass && ButtonBP->GeneratedClass->IsChildOf(UBasicButtonWidget::StaticClass()), TEXT("shell button class unavailable"))) return false;
    const FString GraphBefore = GraphFingerprint(BP);
    const FString AnimationsBefore = AnimationFingerprint(BP);
    TMap<FName, FString> PreservedWidgets;
    BP->WidgetTree->ForEachWidget([&](UWidget* Widget) { PreservedWidgets.Add(Widget->GetFName(), Placement(Widget)); });
    if (!Backup(BP)) return false;
    FDesigner D{BP->WidgetTree, ButtonBP->GeneratedClass.Get()};
    auto* Panel = D.Canvas(TEXT("TransferConfirmPanel"));
    D.Place(Root, Panel, FAnchors(0, 0, 1, 1), FMargin(0), 200);
    D.Plate(Panel, TEXT("TransferConfirmScrim"), TEXT("01060CCC"), FAnchors(0, 0, 1, 1), FMargin(0))->SetVisibility(ESlateVisibility::Visible);
    auto* Fit = D.New<UScaleBox>(TEXT("TransferConfirmFit"));
    Fit->SetStretch(EStretch::ScaleToFit);
    Fit->SetStretchDirection(EStretchDirection::DownOnly);
    Fit->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    D.Place(Panel, Fit, FAnchors(0, 0, 1, 1), FMargin(24), 1);
    auto* Extent = D.New<USizeBox>(TEXT("TransferConfirmExtent"));
    Extent->SetWidthOverride(500);
    Extent->SetHeightOverride(340);
    Extent->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* FitSlot = CastChecked<UScaleBoxSlot>(Fit->AddChild(Extent));
    FitSlot->SetHorizontalAlignment(HAlign_Center);
    FitSlot->SetVerticalAlignment(VAlign_Center);
    auto* Card = D.Canvas(TEXT("TransferConfirmCard"));
    Extent->AddChild(Card);
    D.Plate(Card, TEXT("TransferConfirmBackground"), TEXT("071521FF"), FAnchors(0, 0, 1, 1), FMargin(0))->SetVisibility(ESlateVisibility::Visible);
    D.Plate(Card, TEXT("TransferConfirmTopRule"), TEXT("45BDE7"), FAnchors(0, 0, 1, 0), FMargin(0, 0, 0, 2));
    D.Plate(Card, TEXT("TransferConfirmBottomRule"), TEXT("285C75"), FAnchors(0, 1, 1, 1), FMargin(0, -1, 0, 1));
    D.Text(Card, TEXT("TransferConfirmTitle"), TEXT("调离确认"), 20, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(24, 22, 24, 32));
    D.Text(Card, TEXT("TransferConfirmCaption"), TEXT("PERSONNEL TRANSFER / 小队编组调整"), 9, TEXT("638EA5"), FAnchors(0, 0, 1, 0), FMargin(24, 60, 24, 20));
    D.Plate(Card, TEXT("TransferConfirmDivider"), TEXT("234355"), FAnchors(0, 0, 1, 0), FMargin(24, 91, 24, 1));
    auto* Scroll = D.New<UScrollBox>(TEXT("TransferConfirmBodyScroll"));
    Scroll->SetScrollbarThickness(FVector2D(3));
    Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
    Scroll->SetClipping(EWidgetClipping::ClipToBounds);
    D.Place(Card, Scroll, FAnchors(0, 0, 1, 1), FMargin(24, 110, 24, 108), 2);
    auto* Body = D.New<UTextBlock>(TEXT("TransferConfirmText"));
    Body->SetText(FText::FromString(TEXT("该人员已编入其他小队。\n是否将其调离原小队，转入当前小队？")));
    Body->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), 14, TEXT("Regular")));
    Body->SetColorAndOpacity(Color(TEXT("C5D9E5")));
    Body->SetAutoWrapText(true);
    Body->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* BodySlot = CastChecked<UScrollBoxSlot>(Scroll->AddChild(Body));
    BodySlot->SetHorizontalAlignment(HAlign_Fill);
    BodySlot->SetPadding(FMargin(0, 0, 6, 0));
    D.Text(Card, TEXT("TransferConfirmHint"), TEXT("保存小队后正式生效；取消或退出不会修改原小队。"), 10, TEXT("7798AC"), FAnchors(0, 1, 1, 1), FMargin(24, -92, 24, 30))->SetAutoWrapText(true);
    D.Button(Card, TEXT("CancelUnitTransferButton"), TEXT("取消"), FAnchors(0, 1, .4f, 1), FMargin(24, -60, 6, 38));
    auto* Confirm = D.Button(Card, TEXT("ConfirmUnitTransferButton"), TEXT("转入当前小队"), FAnchors(.4f, 1, 1, 1), FMargin(6, -60, 24, 38));
    Confirm->BackgroundColor = Color(TEXT("0D2636"));
    Confirm->BorderColor = Color(TEXT("3689AA"));
    Confirm->AccentColor = Color(TEXT("45BDE7"));
    Confirm->ButtonForegroundColor = Color(TEXT("DCE9F5"));
    Panel->SetVisibility(ESlateVisibility::Collapsed);
    if (!Compile(BP) || !ValidateDialog(BP)
        || !Check(GraphBefore == GraphFingerprint(BP), TEXT("existing Blueprint graph changed"))
        || !Check(AnimationsBefore == AnimationFingerprint(BP), TEXT("existing animation bindings changed"))) return false;
    for (const auto& Pair : PreservedWidgets)
        if (!Check(Pair.Value == Placement(BP->WidgetTree->FindWidget(Pair.Key)), TEXT("changed existing widget placement: ") + Pair.Key.ToString())) return false;
    return Save(BP);
}

bool EnsureNameTable()
{
    if (FPackageName::DoesPackageExist(NamesPath))
    {
        auto* Existing = LoadObject<UDataTable>(nullptr, NamesPath);
        const bool bValid = Check(Existing && Existing->GetRowStruct() == FSquadNameTemplate::StaticStruct(), TEXT("existing squad name table has an unexpected row type"));
        if (bValid) UE_LOG(LogTemp, Display, TEXT("SQUAD_NAMES_TABLE_PRESERVED rows=%d path=%s"), Existing->GetRowNames().Num(), NamesPath);
        return bValid;
    }
    struct FInitialName { const TCHAR* Id; const TCHAR* Name; };
    const FInitialName Names[] = {
        {TEXT("Alpha"), TEXT("阿尔法小队")}, {TEXT("Beta"), TEXT("贝塔小队")},
        {TEXT("Gamma"), TEXT("伽马小队")}, {TEXT("Delta"), TEXT("德尔塔小队")},
        {TEXT("Epsilon"), TEXT("艾普西龙小队")}, {TEXT("Zeta"), TEXT("泽塔小队")},
        {TEXT("Eta"), TEXT("伊塔小队")}, {TEXT("Theta"), TEXT("西塔小队")},
        {TEXT("Iota"), TEXT("约塔小队")}, {TEXT("Kappa"), TEXT("卡帕小队")},
        {TEXT("Lambda"), TEXT("拉姆达小队")}, {TEXT("Mu"), TEXT("缪小队")},
        {TEXT("Nu"), TEXT("纽小队")}, {TEXT("Xi"), TEXT("克西小队")},
        {TEXT("Omicron"), TEXT("奥米克戎小队")}, {TEXT("Pi"), TEXT("派小队")},
        {TEXT("Rho"), TEXT("柔小队")}, {TEXT("Sigma"), TEXT("西格玛小队")},
        {TEXT("Tau"), TEXT("陶小队")}, {TEXT("Upsilon"), TEXT("宇普西龙小队")},
        {TEXT("Phi"), TEXT("斐小队")}, {TEXT("Chi"), TEXT("希小队")},
        {TEXT("Psi"), TEXT("普赛小队")}, {TEXT("Omega"), TEXT("欧米伽小队")}
    };
    UPackage* Package = CreatePackage(NamesPath);
    auto* Table = NewObject<UDataTable>(Package, FName(*FPackageName::GetShortName(NamesPath)), RF_Public | RF_Standalone);
    Table->RowStruct = FSquadNameTemplate::StaticStruct();
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
    {
        FSquadNameTemplate Row;
        Row.SortOrder = (Index + 1) * 10;
        Row.SquadName = FText::ChangeKey(TEXT("SquadNames"), Names[Index].Id, FText::FromString(Names[Index].Name));
        Table->AddRow(FName(Names[Index].Id), Row);
    }
    FAssetRegistryModule::AssetCreated(Table);
    if (!Save(Table)) return false;
    UE_LOG(LogTemp, Display, TEXT("SQUAD_NAMES_TABLE_CREATED rows=%d path=%s"), Table->GetRowNames().Num(), NamesPath);
    return true;
}

void Inspect(UWidgetBlueprint* BP)
{
    BP->WidgetTree->ForEachWidget([](UWidget* Widget)
    {
        if (Widget->GetName().Contains(TEXT("Transfer")))
            UE_LOG(LogTemp, Display, TEXT("SQUAD_TRANSFER_UI_WIDGET %s class=%s parent=%s visibility=%d placement=%s"),
                *Widget->GetName(), *Widget->GetClass()->GetName(), *GetNameSafe(Widget->GetParent()), int32(Widget->GetVisibility()), *Placement(Widget));
    });
    if (FPackageName::DoesPackageExist(NamesPath))
    {
        auto* Table = LoadObject<UDataTable>(nullptr, NamesPath);
        if (Table && Table->GetRowStruct() == FSquadNameTemplate::StaticStruct())
        {
            TArray<TPair<FName, const FSquadNameTemplate*>> Rows;
            for (const FName Name : Table->GetRowNames())
                if (const auto* Row = Table->FindRow<FSquadNameTemplate>(Name, TEXT("SquadNamesInspect"))) Rows.Emplace(Name, Row);
            Rows.Sort([](const auto& A, const auto& B)
            {
                return A.Value->SortOrder == B.Value->SortOrder ? A.Key.LexicalLess(B.Key) : A.Value->SortOrder < B.Value->SortOrder;
            });
            for (const auto& Pair : Rows)
                UE_LOG(LogTemp, Display, TEXT("SQUAD_NAMES_TABLE_ROW id=%s order=%d name=%s"), *Pair.Key.ToString(), Pair.Value->SortOrder, *Pair.Value->SquadName.ToString());
        }
    }
}
}

int32 USquadTransferUIAssetsCommandlet::Main(const FString& Params)
{
    auto* Room = LoadObject<UWidgetBlueprint>(nullptr, SquadTransferUIAssets::RoomPath);
    if (!SquadTransferUIAssets::Check(Room && Room->WidgetTree && Room->GeneratedClass, TEXT("squad room Blueprint unavailable"))) return 1;
    if (FParse::Param(*Params, TEXT("Inspect")))
    {
        SquadTransferUIAssets::Inspect(Room);
        const bool bPatched = Room->WidgetTree->FindWidget(TEXT("TransferConfirmPanel")) != nullptr;
        bool bValid = bPatched ? SquadTransferUIAssets::ValidateDialog(Room) : SquadTransferUIAssets::ValidateTree(Room);
        auto* Table = FPackageName::DoesPackageExist(SquadTransferUIAssets::NamesPath) ? LoadObject<UDataTable>(nullptr, SquadTransferUIAssets::NamesPath) : nullptr;
        bValid &= SquadTransferUIAssets::Check(!Table || Table->GetRowStruct() == FSquadNameTemplate::StaticStruct(), TEXT("squad name table row type mismatch"));
        UE_LOG(LogTemp, Display, TEXT("SQUAD_TRANSFER_UI_INSPECT_%s patched=%d names=%d"), bValid ? TEXT("OK") : TEXT("FAILED"), bPatched, Table ? Table->GetRowNames().Num() : 0);
        return bValid ? 0 : 1;
    }
    if (!FParse::Param(*Params, TEXT("Build")))
    {
        UE_LOG(LogTemp, Error, TEXT("Use -run=SquadTransferUIAssets -Inspect or -Build."));
        return 1;
    }
    const bool bSuccess = SquadTransferUIAssets::BuildDialog(Room) && SquadTransferUIAssets::EnsureNameTable();
    if (bSuccess) SquadTransferUIAssets::Inspect(Room);
    UE_LOG(LogTemp, Display, TEXT("SQUAD_TRANSFER_UI_%s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));
    return bSuccess ? 0 : 1;
}
