#include "BattleSquadEntryAssets.h"

#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/BattleMap/BattleSquadEntryWidget.h"
#include "Map/BattleMap/BattleHUDWidgets.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "HAL/FileManager.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"

namespace BattleSquadEntryAssets
{
const TCHAR* EntryPath = TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_小队项");
const TCHAR* HUDPath = TEXT("/Game/System/Map/BattleMap/UI/WBP_BattleHUD");
const TCHAR* MemberClassPath = TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_人员卡片.WBP_人员卡片_C");

bool Backup(const TCHAR* PackagePath, const FString& Folder)
{
    const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    if (!FPaths::FileExists(File)) return false;
    const FString Destination = Folder / (FPackageName::GetShortName(PackagePath) + TEXT(".uasset"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
    return IFileManager::Get().Copy(*Destination, *File) == COPY_OK;
}

bool Save(UWidgetBlueprint* BP)
{
    BP->MarkPackageDirty();
    const FString File = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(BP->GetOutermost(), BP, *File, Args);
}

bool Compile(UWidgetBlueprint* BP)
{
    BP->ForEachSourceWidget([BP](UWidget* Widget)
    {
        if (!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
            BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
    });
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    return BP->Status != BS_Error && BP->GeneratedClass;
}

bool HasPresentationBindings(UWidgetBlueprint* BP)
{
    if (!BP->Bindings.IsEmpty()) return true;
    for (const UWidgetAnimation* Animation : BP->Animations)
        if (Animation && !Animation->AnimationBindings.IsEmpty()) return true;
    TSet<FName> WidgetNames;
    BP->WidgetTree->ForEachWidget([&WidgetNames](UWidget* Widget) { WidgetNames.Add(Widget->GetFName()); });
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (const auto* Variable = Cast<UK2Node_Variable>(Node);
                Variable && Variable->VariableReference.IsSelfContext()
                && WidgetNames.Contains(Variable->VariableReference.GetMemberName())) return true;
            if (const auto* Event = Cast<UK2Node_ComponentBoundEvent>(Node);
                Event && WidgetNames.Contains(Event->GetComponentPropertyName())) return true;
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (!Pin) continue;
                if (WidgetNames.Contains(FName(*Pin->DefaultValue))) return true;
                if (Pin->DefaultObject && Pin->DefaultObject->IsIn(BP->WidgetTree)) return true;
            }
        }
    return false;
}

bool HasCompactPresentation(const UWidgetBlueprint* BP)
{
    return BP->ParentClass == UBattleSquadEntryWidget::StaticClass()
        && BP->WidgetTree->FindWidget(TEXT("SquadEntrySize"))
        && BP->WidgetTree->FindWidget(TEXT("SquadIcon"))
        && BP->WidgetTree->FindWidget(TEXT("SquadNameLabel"))
        && BP->WidgetTree->FindWidget(TEXT("MemberCountLabel"))
        && BP->WidgetTree->FindWidget(TEXT("SquadIndexLabel"))
        && BP->WidgetTree->FindWidget(TEXT("FallbackSymbol"));
}

void AddRosterBusinessEvents(UWidgetBlueprint* BP)
{
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    UEdGraph* RosterGraph = nullptr;
    TSet<FName> ExistingEvents;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph->GetFName() == TEXT("RosterBusiness")) RosterGraph = Graph;
        for (UEdGraphNode* Node : Graph->Nodes)
            if (const auto* Event = Cast<UK2Node_Event>(Node))
                ExistingEvents.Add(Event->EventReference.GetMemberName());
    }
    if (!RosterGraph)
    {
        RosterGraph = FBlueprintEditorUtils::CreateNewGraph(BP, TEXT("RosterBusiness"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(BP, RosterGraph);
    }

    const FName GuideName(TEXT("BattleRosterBusinessGuide"));
    if (!RosterGraph->Nodes.ContainsByPredicate([GuideName](const UEdGraphNode* Node) { return Node && Node->GetFName() == GuideName; }))
    {
        auto* Guide = NewObject<UEdGraphNode_Comment>(RosterGraph, GuideName);
        RosterGraph->AddNode(Guide);
        Guide->CreateNewGuid();
        Guide->NodePosX = -80;
        Guide->NodePosY = -250;
        Guide->NodeWidth = 1800;
        Guide->NodeHeight = 210;
        Guide->CommentColor = FLinearColor(.025f, .12f, .18f, 1.f);
        Guide->NodeComment = TEXT("外部业务蓝图：GetBattleWidget → 初始化战斗UI（传入本次参战的小队ID数组）。返回失败时读取错误信息；这里不自动注入测试小队。\n")
            TEXT("C++已负责解析现有小队、生成成员/小队两页的小队列表、统一单选状态，以及选队后刷新对应人员卡片；空数组用于清空列表。\n")
            TEXT("下方是提交完成后的业务通知，可接入需要的业务表现。不要在通知中再次同步初始化或切换小队；这些算法已经由C++完成。");
    }

    int32 PositionY = 40;
    for (const FName EventName : {FName(TEXT("OnBattleUIInitialized")), FName(TEXT("OnBattleSquadSelected"))})
    {
        if (!ExistingEvents.Contains(EventName))
        {
            FGraphNodeCreator<UK2Node_Event> Creator(*RosterGraph);
            auto* Event = Creator.CreateNode();
            Event->EventReference.SetExternalMember(EventName, UBattleMapWidget::StaticClass());
            Event->bOverrideFunction = true;
            Event->NodePosX = 0;
            Event->NodePosY = PositionY;
            Creator.Finalize();
        }
        PositionY += 300;
    }
}
}

bool UpgradeBattleSquadEntryAssets()
{
    using namespace BattleSquadEntryAssets;
    auto* EntryBP = LoadObject<UWidgetBlueprint>(nullptr, EntryPath);
    auto* HUDBP = LoadObject<UWidgetBlueprint>(nullptr, HUDPath);
    UClass* MemberClass = LoadClass<UBattleMemberCardWidget>(nullptr, MemberClassPath);
    auto* HoverSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuHover_Electronic"));
    auto* PressSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuPress_Terminal"));
    if (!EntryBP || !EntryBP->WidgetTree || !HUDBP || !HUDBP->GeneratedClass
        || !HUDBP->GeneratedClass->IsChildOf(UBattleMapWidget::StaticClass()) || !MemberClass || !HoverSound || !PressSound)
    {
        UE_LOG(LogTemp, Error, TEXT("BATTLE_SQUAD_ENTRY_MISSING_DEPENDENCY: existing entry, HUD, personnel card and menu sounds are required"));
        return false;
    }

    const bool bHasLayout = HasCompactPresentation(EntryBP);
    if (!bHasLayout && HasPresentationBindings(EntryBP))
    {
        UE_LOG(LogTemp, Error, TEXT("BATTLE_SQUAD_ENTRY_HAS_PRESENTATION_BINDINGS: existing tree is referenced by business logic; no asset changed"));
        return false;
    }
    const FString BackupFolder = FPaths::ProjectSavedDir() / TEXT("BattleSquadEntry/Backup")
        / (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if (!Backup(EntryPath, BackupFolder) || !Backup(HUDPath, BackupFolder)) return false;

    if (!bHasLayout)
    {
        // Graphs and variables remain intact. Only an unreferenced placeholder tree is replaced.
        if (!EntryBP->WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional)) return false;
        EntryBP->WidgetTree = NewObject<UWidgetTree>(EntryBP, TEXT("WidgetTree"), RF_Transactional);
        EntryBP->WidgetVariableNameToGuidMap.Reset();
        UBattleSquadEntryWidget::BuildDefaultWidgetTree(EntryBP->WidgetTree);
    }
    EntryBP->ParentClass = UBattleSquadEntryWidget::StaticClass();
    // Use the project's runtime font object; Slate's editor font style can lack
    // working CJK fallback in a standalone game window.
    auto* Typeface = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    for (const auto& Setting : TArray<TPair<FName,int32>>{
        {TEXT("SquadNameLabel"),11},{TEXT("MemberCountLabel"),9},{TEXT("SquadIndexLabel"),8}})
        if (auto* Label = Cast<UTextBlock>(EntryBP->WidgetTree->FindWidget(Setting.Key)))
            Label->SetFont(FSlateFontInfo(Typeface, Setting.Value));
    for (const TCHAR* Name : {TEXT("SquadIcon"), TEXT("SquadNameLabel"), TEXT("MemberCountLabel"), TEXT("SquadIndexLabel"), TEXT("FallbackSymbol")})
        EntryBP->WidgetTree->FindWidget(Name)->bIsVariable = true;
    if (!Compile(EntryBP)) return false;
    auto* EntryDefaults = CastChecked<UBattleSquadEntryWidget>(EntryBP->GeneratedClass->GetDefaultObject());
    EntryDefaults->MinimumSize = FVector2D::ZeroVector;
    EntryDefaults->ButtonText = FText::GetEmpty();
    EntryDefaults->ButtonSubtitle = FText::GetEmpty();
    EntryDefaults->ButtonIndex = FText::GetEmpty();
    EntryDefaults->bUseTabStyle = false;
    EntryDefaults->BackgroundColor = FLinearColor(FColor(7, 19, 29, 250));
    EntryDefaults->BorderColor = FLinearColor(FColor(37, 72, 89));
    EntryDefaults->AccentColor = FLinearColor(FColor(30, 211, 231));
    EntryDefaults->HoverSound = HoverSound;
    EntryDefaults->PressSound = PressSound;
    EntryDefaults->SetVisibility(ESlateVisibility::Visible);
    EntryDefaults->SetIsFocusable(true);
    EntryDefaults->DesignSizeMode = EDesignPreviewSizeMode::Desired;
    if (!Save(EntryBP)) return false;

    AddRosterBusinessEvents(HUDBP);
    // Compile after reparenting the row so existing preview instances inherit the new class safely.
    if (!Compile(HUDBP)) return false;
    auto* HUDDefaults = CastChecked<UBattleMapWidget>(HUDBP->GeneratedClass->GetDefaultObject());
    HUDDefaults->SquadEntryClass = EntryBP->GeneratedClass;
    HUDDefaults->MemberCardClass = MemberClass;
    if (!Save(HUDBP)) return false;
    UE_LOG(LogTemp, Display, TEXT("BATTLE_SQUAD_ENTRY_UPGRADE_OK size=170x52 input=SelectionButton sounds=existing_menu classes=SquadEntry/PersonnelCard backup=%s"), *BackupFolder);
    return true;
}
