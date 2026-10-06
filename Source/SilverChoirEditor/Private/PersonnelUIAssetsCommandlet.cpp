#include "PersonnelUIAssetsCommandlet.h"
#include "Animation/WidgetAnimation.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Spacer.h"
#include "UIBasic/ContentFitBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Widget/SlotContainerByType/SIS_InventorySlotContainer.h"
#include "Components/ScrollBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/ProgressBar.h"
#include "Engine/Font.h"
#include "Sound/SoundBase.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/PersonnelPreparationRoomWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelListEntryWidget.h"

extern bool BuildWarehouseInventoryFlow(UWidgetBlueprint* BP);
extern bool FixWarehouseInitialPage(UWidgetBlueprint* BP);
extern bool MigrateWarehouseNativeSession(UWidgetBlueprint* BP);

UPersonnelUIAssetsCommandlet::UPersonnelUIAssetsCommandlet() { IsClient=false; IsEditor=true; LogToConsole=true; }
namespace PersonnelAssets
{
UWidgetBlueprint* Make(const FString& Path,UClass* Parent)
{
    if (auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*Path)) return BP;
    auto* Factory=NewObject<UWidgetBlueprintFactory>(); Factory->ParentClass=Parent;
    auto* BP=Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(),CreatePackage(*Path),*FPackageName::GetShortName(Path),RF_Public|RF_Standalone,nullptr,GWarn));
    if (BP) FAssetRegistryModule::AssetCreated(BP); return BP;
}
bool Save(UWidgetBlueprint* BP)
{
    TSet<FName> LiveNames;
    BP->WidgetTree->ForEachWidget([&](UWidget* W) { LiveNames.Add(W->GetFName()); });
    for (UWidgetAnimation* A:BP->Animations) LiveNames.Add(A->GetFName());
    for (auto It=BP->WidgetVariableNameToGuidMap.CreateIterator(); It; ++It)
        if (!LiveNames.Contains(It.Key())) It.RemoveCurrent();
    BP->WidgetTree->ForEachWidget([&](UWidget* W) { if (!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName())) BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewGuid()); });
    for (const FName Name:LiveNames) if (!BP->WidgetVariableNameToGuidMap.Contains(Name)) BP->WidgetVariableNameToGuidMap.Add(Name,FGuid::NewGuid());
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status==BS_Error) return false;
    FString File=FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    if (FPaths::FileExists(File))
    {
        FString Backup=FPaths::ProjectSavedDir()/TEXT("PersonnelUIBackups")/FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))/FPaths::GetCleanFilename(File);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup),true);
        if (IFileManager::Get().Copy(*Backup,*File)!=COPY_OK) return false;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
    BP->MarkPackageDirty(); return UPackage::SavePackage(BP->GetOutermost(),BP,*File,Args);
}
struct FDesigner
{
    UWidgetTree* T;
    UCanvasPanelSlot* Place(UCanvasPanel* P,UWidget* W,FAnchors A,FMargin M,int Z=0)
    {
        auto* S=P->AddChildToCanvas(W); S->SetAnchors(A); S->SetOffsets(M); S->SetZOrder(Z); return S;
    }
    UCanvasPanel* Panel(const TCHAR* Name)
    {
        auto* P=T->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),Name); P->SetVisibility(ESlateVisibility::SelfHitTestInvisible); return P;
    }
    UImage* Box(UCanvasPanel* P,const TCHAR* Name,const TCHAR* Hex,FAnchors A,FMargin M)
    {
        auto* W=T->ConstructWidget<UImage>(UImage::StaticClass(),Name);
        W->SetColorAndOpacity(FLinearColor(FColor::FromHex(Hex))); W->SetVisibility(ESlateVisibility::HitTestInvisible); Place(P,W,A,M); return W;
    }
    UTextBlock* Text(UCanvasPanel* P,const TCHAR* Name,const TCHAR* Value,int Size,const TCHAR* Hex,FAnchors A,FMargin M,bool Wrap=false)
    {
        auto* W=T->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name); W->SetText(FText::FromString(Value));
        W->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")),Size,TEXT("Regular")));
        W->SetColorAndOpacity(FLinearColor(FColor::FromHex(Hex))); W->SetAutoWrapText(Wrap);
        W->SetVisibility(ESlateVisibility::HitTestInvisible); Place(P,W,A,M); return W;
    }
};
}
int32 UPersonnelUIAssetsCommandlet::Main(const FString& Params)
{
    using namespace PersonnelAssets;
    if (Params.Contains(TEXT("NativeInventorySession")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room || !MigrateWarehouseNativeSession(Room) || !Save(Room)) return 1;
        UE_LOG(LogTemp,Display,TEXT("NATIVE_INVENTORY_SESSION_OK"));return 0;
    }
    if (Params.Contains(TEXT("FixInitialInventory")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room || !FixWarehouseInitialPage(Room) || !Save(Room)) return 1;
        UE_LOG(LogTemp,Display,TEXT("INITIAL_INVENTORY_FIXED")); return 0;
    }
    if (Params.Contains(TEXT("AuthorInventoryPageFlow")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room || !BuildWarehouseInventoryFlow(Room) || !Save(Room)) return 1;
        UE_LOG(LogTemp,Display,TEXT("INVENTORY_PAGE_FLOW_OK")); return 0;
    }
    if (Params.Contains(TEXT("CleanInventoryOrphans")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room) return 1;
        for (const TCHAR* Name:{TEXT("WidgetSwitcher_84"),TEXT("PlayerInventory_2"),TEXT("PlayerInventory_3"),TEXT("PlayerInventory_4"),TEXT("PlayerInventory_5")})
            if (auto* W=FindObject<UWidget>(Room->WidgetTree,Name))
            {
                W->RemoveFromParent();
                W->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
            }
        FDesigner D{Room->WidgetTree};
        auto* Panel=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("WarehousePanel")));
        if (!Panel) return 1;
        if (!D.T->FindWidget(TEXT("WarehouseShellFill")))
        {
            auto* Fill=D.Box(Panel,TEXT("WarehouseShellFill"),TEXT("071019F5"),FAnchors(0,0,1,1),FMargin(0));
            CastChecked<UCanvasPanelSlot>(Fill->Slot)->SetZOrder(-3);
            auto* Content=D.Box(Panel,TEXT("WarehouseContentFill"),TEXT("0B1A26F0"),FAnchors(0,0,1,1),FMargin(82,76,16,44));
            CastChecked<UCanvasPanelSlot>(Content->Slot)->SetZOrder(-2);
        }
        if (!Save(Room)) return 1;
        UE_LOG(LogTemp,Display,TEXT("CLEAN_INVENTORY_OK")); return 0;
    }
    if (Params.Contains(TEXT("SingleInventoryDesign")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room) return 1;
        FDesigner D{Room->WidgetTree};
        auto* Panel=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("WarehousePanel")));
        auto* Inventory=Cast<USIS_InventorySlotContainer>(D.T->FindWidget(TEXT("PlayerInventory_1")));
        auto* Pages=Cast<UWidgetSwitcher>(D.T->FindWidget(TEXT("WidgetSwitcher_84")));
        auto* Background=Cast<UBorder>(D.T->FindWidget(TEXT("WarehouseBackground")));
        auto* Tabs=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/UIBasic/WBP_SelectionButton"));
        if (!Panel || !Inventory || !Pages || !Background || !Tabs || D.T->FindWidget(TEXT("InventoryTab01"))) return 1;
        // Preserve the user's first container and its authored configuration.
        for (int32 Index=2;Index<=5;++Index)
        {
            auto* W=D.T->FindWidget(FName(*FString::Printf(TEXT("PlayerInventory_%d"),Index)));
            if (W) { D.T->RemoveWidget(W); W->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional); }
        }
        Inventory->RemoveFromParent();
        D.T->RemoveWidget(Pages);
        Pages->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
        auto* Scroll=D.T->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("WarehouseInventoryScroll"));
        Scroll->AddChild(Inventory);
        Background->SetContent(Scroll);
        Background->SetBrushColor(FLinearColor(FColor::FromHex(TEXT("07111BF0"))));
        Background->SetPadding(FMargin(10));
        if (auto* Slot=Cast<UCanvasPanelSlot>(D.T->FindWidget(TEXT("HorizontalBox_0"))->Slot))
            Slot->SetOffsets(FMargin(82,76,16,44));
        D.Text(Panel,TEXT("InventoryPageTitle"),TEXT("库存 / 01"),18,TEXT("D5E6EF"),FAnchors(0,0,1,0),FMargin(20,14,16,28));
        D.Text(Panel,TEXT("WarehouseSubtitle"),TEXT("STORAGE / EQUIPMENT BAY"),8,TEXT("587F94"),FAnchors(0,0,1,0),FMargin(20,45,16,16));
        D.Box(Panel,TEXT("WarehouseHeaderRule"),TEXT("234355"),FAnchors(0,0,1,0),FMargin(20,66,16,1));
        D.Box(Panel,TEXT("WarehouseRailLine"),TEXT("193344"),FAnchors(0,0,0,1),FMargin(72,76,1,44));
        for (int32 Index=0;Index<5;++Index)
        {
            const FName Name(*FString::Printf(TEXT("InventoryTab%02d"),Index+1));
            auto* B=D.T->ConstructWidget<USelectionButtonWidget>(Tabs->GeneratedClass.Get(),Name);
            B->ButtonText=FText::FromString(FString::Printf(TEXT("%02d"),Index+1));
            B->ChoiceID=FName(*FString::Printf(TEXT("Inventory%02d"),Index+1));
            B->Font.Size=14; B->MinimumSize=FVector2D(48,44); B->SetSelected(Index==0);
            D.Place(Panel,B,FAnchors(0,0),FMargin(16,76+Index*56,48,44));
        }
        D.Text(Panel,TEXT("WarehouseFooter"),TEXT("01 — 05   /   库存分区"),9,TEXT("587F94"),FAnchors(0,1,1,1),FMargin(20,-30,16,20));
        if (!Save(Room)) return 1;
        UE_LOG(LogTemp,Display,TEXT("SINGLE_INVENTORY_DESIGN_OK")); return 0;
    }
    if (Params.Contains(TEXT("EquipmentEqualGaps")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room) return 1;
        for (const TCHAR* Name:{TEXT("VerticalBox"),TEXT("VerticalBox_1")})
        {
            auto* Column=Cast<UVerticalBox>(Room->WidgetTree->FindWidget(Name));
            if (!Column) return 1;
            auto* OldSlot=Cast<UCanvasPanelSlot>(Column->Slot);
            if (!OldSlot) return 1; // One-time migration; do not overwrite an already migrated hierarchy.
            auto* Parent=CastChecked<UCanvasPanel>(Column->GetParent());
            auto Anchors=OldSlot->GetAnchors(); auto Offsets=OldSlot->GetOffsets();
            auto Alignment=OldSlot->GetAlignment(); const int32 Z=OldSlot->GetZOrder();
            auto Children=Column->GetAllChildren();
            if (Children.Num()!=5) return 1;
            TArray<FMargin> Paddings;
            for (UWidget* W:Children) Paddings.Add(CastChecked<UVerticalBoxSlot>(W->Slot)->GetPadding());
            Column->ClearChildren();
            for (int32 I=0;I<Children.Num();++I)
            {
                auto* S=Column->AddChildToVerticalBox(Children[I]);
                S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                S->SetPadding(Paddings[I]); S->SetHorizontalAlignment(HAlign_Fill); S->SetVerticalAlignment(VAlign_Top);
                if (I+1<Children.Num())
                {
                    auto* Gap=Room->WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(),FName(*(FString(Name)+FString::Printf(TEXT("_Gap%d"),I))));
                    Gap->SetSize(FVector2D(0,12));
                    Column->AddChildToVerticalBox(Gap)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                }
            }
            Column->RemoveFromParent();
            auto* Fit=Room->WidgetTree->ConstructWidget<UContentFitBox>(UContentFitBox::StaticClass(),FName(*(FString(Name)+TEXT("_Fit"))));
            Fit->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            auto* ContentSlot=CastChecked<USizeBoxSlot>(Fit->AddChild(Column));
            ContentSlot->SetHorizontalAlignment(HAlign_Fill); ContentSlot->SetVerticalAlignment(VAlign_Fill);
            auto* Slot=Parent->AddChildToCanvas(Fit);
            Anchors.Minimum.Y=0; Anchors.Maximum.Y=1; Offsets.Bottom=0; Alignment.Y=0;
            Slot->SetAnchors(Anchors); Slot->SetOffsets(Offsets); Slot->SetAlignment(Alignment); Slot->SetZOrder(Z); Slot->SetAutoSize(true);
        }
        if (!Save(Room)) return 1;
        UE_LOG(LogTemp,Display,TEXT("EQUIPMENT_EQUAL_GAPS_OK")); return 0;
    }
    if (Params.Contains(TEXT("EquipmentNaturalSize")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room) return 1;
        int32 Count=0;
        for (const TCHAR* Name:{TEXT("VerticalBox"),TEXT("VerticalBox_1")})
        {
            auto* Column=Cast<UVerticalBox>(Room->WidgetTree->FindWidget(Name));
            if (!Column) return 1;
            auto* Slot=Cast<UCanvasPanelSlot>(Column->Slot);
            if (!Slot) return 1;
            auto Anchors=Slot->GetAnchors(); auto Offsets=Slot->GetOffsets();
            Anchors.Minimum.Y=0; Anchors.Maximum.Y=0;
            Slot->SetAnchors(Anchors); Slot->SetOffsets(Offsets); Slot->SetAutoSize(true);
            auto Alignment=Slot->GetAlignment(); Alignment.Y=0; Slot->SetAlignment(Alignment);
            for (UWidget* Child:Column->GetAllChildren())
            {
                auto* ChildSlot=CastChecked<UVerticalBoxSlot>(Child->Slot);
                ChildSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                ChildSlot->SetVerticalAlignment(VAlign_Top);
                ++Count;
            }
        }
        if (!Save(Room)) return 1;
        UE_LOG(LogTemp,Display,TEXT("EQUIPMENT_NATURAL_SIZE_OK containers=%d"),Count); return 0;
    }
    if (Params.Contains(TEXT("RestoreNationalityRow")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room) return 1;
        if (auto* Old=Room->WidgetTree->FindWidget(TEXT("SelectedMilitaryRank"))) Room->WidgetTree->RemoveWidget(Old);
        auto* Nationality=Room->WidgetTree->FindWidget(TEXT("SelectedNationality"));
        if (!Nationality) return 1;
        auto* Slot=CastChecked<UCanvasPanelSlot>(Nationality->Slot);
        Slot->SetAnchors(FAnchors(0,0,1,0)); Slot->SetOffsets(FMargin(20,44,20,28));
        if (!Save(Room)) return 1;
        UE_LOG(LogTemp,Display,TEXT("NATIONALITY_ROW_RESTORED")); return 0;
    }
    if (Params.Contains(TEXT("PolishProfile")) || Params.Contains(TEXT("RosterProfileV3")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room) return 1;
        FDesigner D{Room->WidgetTree};
        auto* Identity=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("IdentityPanel")));
        auto* Profile=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("ProfilePage")));
        if (!Identity || !Profile) return 1;
        auto Move=[&](const TCHAR* Name,FAnchors A,FMargin M)
        {
            if (auto* W=D.T->FindWidget(Name)) if (auto* S=Cast<UCanvasPanelSlot>(W->Slot)) { S->SetAnchors(A); S->SetOffsets(M); }
        };
        auto Style=[&](const TCHAR* Name,int Size,const TCHAR* Hex)
        {
            if (auto* W=Cast<UTextBlock>(D.T->FindWidget(Name)))
            { auto F=W->GetFont(); F.Size=Size; W->SetFont(F); W->SetColorAndOpacity(FLinearColor(FColor::FromHex(Hex))); }
        };
        auto Box=[&](UCanvasPanel* Parent,const TCHAR* Name,const TCHAR* Hex,FAnchors A,FMargin M,int Z=-1)
        {
            auto* W=Cast<UImage>(D.T->FindWidget(Name));
            if (!W) W=D.Box(Parent,Name,Hex,A,M);
            W->SetColorAndOpacity(FLinearColor(FColor::FromHex(Hex)));
            Move(Name,A,M); CastChecked<UCanvasPanelSlot>(W->Slot)->SetZOrder(Z);
        };
        auto Label=[&](UCanvasPanel* Parent,const TCHAR* Name,const TCHAR* Value,FAnchors A,FMargin M)
        {
            if (!D.T->FindWidget(Name)) D.Text(Parent,Name,Value,9,TEXT("537C98"),A,M);
            Move(Name,A,M);
        };
        Move(TEXT("IdentityPanel"),FAnchors(0,0,1,0),FMargin(0,52,0,214));
        if (auto* Plate=D.T->FindWidget(TEXT("IdentityPlate"))) CastChecked<UCanvasPanelSlot>(Plate->Slot)->SetZOrder(-3);
        if (auto* Accent=D.T->FindWidget(TEXT("IdentityAccent"))) Accent->SetVisibility(ESlateVisibility::Collapsed);
        Label(Identity,TEXT("RecordEyebrow"),TEXT("PERSONNEL / 身份档案"),FAnchors(0,0,1,0),FMargin(20,14,20,18));
        Box(Identity,TEXT("PortraitBacking"),TEXT("244657"),FAnchors(0,0),FMargin(19,43,106,106));
        Move(TEXT("DetailPortrait"),FAnchors(0,0),FMargin(20,44,104,104));
        Move(TEXT("IdentityCaption"),FAnchors(0,0),FMargin(20,156,108,18));
        Style(TEXT("IdentityCaption"),8,TEXT("537C98"));
        Move(TEXT("SelectedMeta"),FAnchors(0,0,1,0),FMargin(144,42,20,34));
        Style(TEXT("SelectedMeta"),22,TEXT("DCE9F5"));
        Move(TEXT("SelectedName"),FAnchors(0,0,1,0),FMargin(144,84,20,26));
        Style(TEXT("SelectedName"),13,TEXT("9CB4C5"));
        Move(TEXT("SelectedAge"),FAnchors(0,0,.72f,0),FMargin(144,124,0,24));
        Move(TEXT("SelectedGender"),FAnchors(.72f,0,1,0),FMargin(0,124,16,24));
        Style(TEXT("SelectedAge"),11,TEXT("7798AC")); Style(TEXT("SelectedGender"),11,TEXT("7798AC"));
        Box(Identity,TEXT("LocationStrip"),TEXT("0D202C"),FAnchors(0,0,1,0),FMargin(20,180,20,26));
        Box(Identity,TEXT("LocationMark"),TEXT("329ACA"),FAnchors(0,0),FMargin(28,190,3,6),0);
        Move(TEXT("SelectedLocation"),FAnchors(0,0,1,0),FMargin(40,182,28,22));
        Style(TEXT("SelectedLocation"),10,TEXT("7798AC"));
        Move(TEXT("IdentityContentDivider"),FAnchors(0,0,1,0),FMargin(20,278,20,1));
        Move(TEXT("DetailPages"),FAnchors(0,0,1,1),FMargin(0,294,0,0));
        if (auto* Plate=D.T->FindWidget(TEXT("PagePlate0"))) CastChecked<UCanvasPanelSlot>(Plate->Slot)->SetZOrder(-3);
        Move(TEXT("SelectedNationality"),FAnchors(0,0,1,0),FMargin(20,8,20,26));
        Style(TEXT("SelectedNationality"),11,TEXT("7798AC"));
        Label(Profile,TEXT("BiographyIndex"),TEXT("01 / BIOGRAPHY"),FAnchors(0,0,1,0),FMargin(20,58,20,18));
        Move(TEXT("PageHeading0"),FAnchors(0,0,1,0),FMargin(20,82,20,30));
        Style(TEXT("PageHeading0"),17,TEXT("DCE9F5"));
        Move(TEXT("PageRule0"),FAnchors(0,0,1,0),FMargin(20,120,20,1));
        Box(Profile,TEXT("BiographyCard"),TEXT("091722"),FAnchors(0,0,1,0),FMargin(20,134,20,122));
        Move(TEXT("BiographyText"),FAnchors(0,0,1,0),FMargin(34,150,34,92));
        Style(TEXT("BiographyText"),12,TEXT("9CB4C5"));
        Label(Profile,TEXT("ActionLogIndex"),TEXT("02 / ACTIVITY LOG"),FAnchors(0,0,1,0),FMargin(20,286,20,18));
        Move(TEXT("BiographyHeading"),FAnchors(0,0,1,0),FMargin(20,310,20,30));
        Style(TEXT("BiographyHeading"),17,TEXT("DCE9F5"));
        Move(TEXT("ActionLogRule"),FAnchors(0,0,1,0),FMargin(20,348,20,1));
        Box(Profile,TEXT("ActionLogCard"),TEXT("091722"),FAnchors(0,0,1,0),FMargin(20,362,20,122));
        Box(Profile,TEXT("LogRail"),TEXT("244657"),FAnchors(0,0),FMargin(34,382,1,78),0);
        Box(Profile,TEXT("LogMarker"),TEXT("537C98"),FAnchors(0,0),FMargin(32,383,5,5),0);
        Move(TEXT("ActionLogText"),FAnchors(0,0,1,0),FMargin(48,378,34,90));
        Style(TEXT("ActionLogText"),12,TEXT("7798AC"));
        if (auto* Extent=Cast<USizeBox>(D.T->FindWidget(TEXT("ProfilePageExtent")))) Extent->SetHeightOverride(510);
        if (Params.Contains(TEXT("RosterProfileV3")))
        {
            auto Field=[&](UCanvasPanel* Parent,const TCHAR* Name,const TCHAR* Value,int Size,FAnchors A,FMargin M)
            {
                auto* W=Cast<UTextBlock>(D.T->FindWidget(Name));
                if (!W) W=D.Text(Parent,Name,Value,Size,TEXT("9CB4C5"),A,M,true);
                else if (W->GetParent()!=Parent) { W->RemoveFromParent(); D.Place(Parent,W,A,M); }
                Move(Name,A,M); Style(Name,Size,TEXT("9CB4C5"));
                return W;
            };
            // Identity persists across all tabs; demographic fields belong only to ProfilePage.
            Field(Identity,TEXT("SelectedSquad"),TEXT("所属小队  —"),13,FAnchors(0,0,1,0),FMargin(144,124,16,28));
            Style(TEXT("SelectedName"),13,TEXT("9CB4C5"));
            Field(Profile,TEXT("SelectedAge"),TEXT("年龄  —"),12,FAnchors(0,0,.5f,0),FMargin(20,8,8,28));
            Field(Profile,TEXT("SelectedGender"),TEXT("性别  —"),12,FAnchors(.5f,0,1,0),FMargin(8,8,20,28));
            Field(Profile,TEXT("SelectedNationality"),TEXT("国籍  —"),12,FAnchors(0,0,1,0),FMargin(20,44,20,28));
            Move(TEXT("BiographyIndex"),FAnchors(0,0,1,0),FMargin(20,94,20,18));
            Move(TEXT("PageHeading0"),FAnchors(0,0,1,0),FMargin(20,118,20,30));
            Move(TEXT("PageRule0"),FAnchors(0,0,1,0),FMargin(20,156,20,1));
            Move(TEXT("BiographyCard"),FAnchors(0,0,1,0),FMargin(20,170,20,122));
            Move(TEXT("BiographyText"),FAnchors(0,0,1,0),FMargin(34,186,34,92));
            Label(Profile,TEXT("EvaluationIndex"),TEXT("02 / EVALUATION"),FAnchors(0,0,1,0),FMargin(20,316,20,18));
            Field(Profile,TEXT("EvaluationHeading"),TEXT("评价"),17,FAnchors(0,0,1,0),FMargin(20,340,20,30));
            Style(TEXT("EvaluationHeading"),17,TEXT("DCE9F5"));
            Box(Profile,TEXT("EvaluationRule"),TEXT("183A4D"),FAnchors(0,0,1,0),FMargin(20,378,20,1));
            Box(Profile,TEXT("EvaluationCard"),TEXT("091722"),FAnchors(0,0,1,0),FMargin(20,392,20,100));
            Field(Profile,TEXT("EvaluationText"),TEXT("暂无评价"),12,FAnchors(0,0,1,0),FMargin(34,408,34,70));
            if (auto* W=Cast<UTextBlock>(D.T->FindWidget(TEXT("ActionLogIndex")))) W->SetText(FText::FromString(TEXT("03 / SERVICE RECORD")));
            Move(TEXT("ActionLogIndex"),FAnchors(0,0,1,0),FMargin(20,516,20,18));
            if (auto* W=Cast<UTextBlock>(D.T->FindWidget(TEXT("BiographyHeading")))) W->SetText(FText::FromString(TEXT("服役记录")));
            Move(TEXT("BiographyHeading"),FAnchors(0,0,1,0),FMargin(20,540,20,30));
            Move(TEXT("ActionLogRule"),FAnchors(0,0,1,0),FMargin(20,578,20,1));
            Move(TEXT("ActionLogCard"),FAnchors(0,0,1,0),FMargin(20,592,20,184));
            Field(Profile,TEXT("ServiceDurationText"),TEXT("服役时间  —"),12,FAnchors(0,0,1,0),FMargin(34,608,34,26));
            Field(Profile,TEXT("BattleRecordText"),TEXT("参与战斗  —"),12,FAnchors(0,0,1,0),FMargin(34,642,34,26));
            Field(Profile,TEXT("InjuryRecordText"),TEXT("负伤记录  —"),12,FAnchors(0,0,1,0),FMargin(34,676,34,26));
            if (auto* W=Cast<UTextBlock>(D.T->FindWidget(TEXT("ActionLogText")))) W->SetText(FText::FromString(TEXT("暂无服役记录")));
            Move(TEXT("ActionLogText"),FAnchors(0,0,1,0),FMargin(48,730,34,30));
            Move(TEXT("LogRail"),FAnchors(0,0),FMargin(34,724,1,36));
            Move(TEXT("LogMarker"),FAnchors(0,0),FMargin(32,737,5,5));
            if (auto* Extent=Cast<USizeBox>(D.T->FindWidget(TEXT("ProfilePageExtent")))) Extent->SetHeightOverride(800);
            if (D.T->FindWidget(TEXT("SelectedAge"))->GetParent()!=Profile || D.T->FindWidget(TEXT("SelectedSquad"))->GetParent()!=Identity) return 1;
        }
        if (!Save(Room)) return 1;
        UE_LOG(LogTemp,Display,TEXT("PERSONNEL_PROFILE_POLISH_OK")); return 0;
    }
    if (Params.Contains(TEXT("SharedProfileLayout")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room) return 1;
        FDesigner D{Room->WidgetTree};
        auto* Identity=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("IdentityPanel")));
        auto* Profile=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("ProfilePage")));
        auto* Portrait=D.T->FindWidget(TEXT("DetailPortrait"));
        auto* Pages=Cast<UWidgetSwitcher>(D.T->FindWidget(TEXT("DetailPages")));
        if (!Identity || !Profile || !Portrait || !Pages) return 1;
        auto Place=[&](UWidget* W,UCanvasPanel* Parent,FAnchors A,FMargin M)
        {
            W->RemoveFromParent(); D.Place(Parent,W,A,M);
        };
        auto Move=[&](const TCHAR* Name,FAnchors A,FMargin M)
        {
            if (auto* W=D.T->FindWidget(Name)) if (auto* Slot=Cast<UCanvasPanelSlot>(W->Slot)) { Slot->SetAnchors(A); Slot->SetOffsets(M); }
        };
        auto Text=[&](UCanvasPanel* P,const TCHAR* Name,const TCHAR* Value,int Size,FAnchors A,FMargin M)
        {
            auto* W=Cast<UTextBlock>(D.T->FindWidget(Name));
            if (!W) W=D.Text(P,Name,Value,Size,TEXT("9CB4C5"),A,M,true);
            else { Place(W,P,A,M); W->SetText(FText::FromString(Value)); auto F=W->GetFont(); F.Size=Size; W->SetFont(F); }
            W->SetVisibility(ESlateVisibility::HitTestInvisible);
            return W;
        };
        Move(TEXT("IdentityPanel"),FAnchors(0,0,1,0),FMargin(0,58,0,184));
        Place(Portrait,Identity,FAnchors(0,0),FMargin(16,18,112,112));
        Text(Identity,TEXT("IdentityCaption"),TEXT("OPERATOR / 人员"),9,FAnchors(0,0),FMargin(16,140,112,22));
        Text(Identity,TEXT("SelectedMeta"),TEXT("代号  —"),17,FAnchors(0,0,1,0),FMargin(144,16,12,32));
        Text(Identity,TEXT("SelectedName"),TEXT("姓名  —"),13,FAnchors(0,0,1,0),FMargin(144,56,12,28));
        Text(Identity,TEXT("SelectedAge"),TEXT("年龄  —"),11,FAnchors(0,0,.70f,0),FMargin(144,96,0,26));
        Text(Identity,TEXT("SelectedGender"),TEXT("性别  —"),11,FAnchors(.70f,0,1,0),FMargin(4,96,12,26));
        Text(Identity,TEXT("SelectedLocation"),TEXT("当前位置  —"),11,FAnchors(0,0,1,0),FMargin(144,136,12,30));
        if (auto* Status=D.T->FindWidget(TEXT("SelectedStatus"))) Status->SetVisibility(ESlateVisibility::Collapsed);
        // This panel is a sibling of the switcher: changing tabs never hides identity.
        Move(TEXT("DetailPages"),FAnchors(0,0,1,1),FMargin(0,254,0,0));
        for (const TCHAR* Name:{TEXT("ProfileText"),TEXT("HealthText"),TEXT("MoraleText"),TEXT("HealthBar"),TEXT("MoraleBar")})
            if (auto* W=D.T->FindWidget(Name)) W->SetVisibility(ESlateVisibility::Collapsed);
        if (!D.T->FindWidget(TEXT("IdentityContentDivider")))
            D.Box(CastChecked<UCanvasPanel>(Identity->GetParent()),TEXT("IdentityContentDivider"),TEXT("305366"),FAnchors(0,0,1,0),FMargin(0,246,0,1));
        Text(Profile,TEXT("SelectedNationality"),TEXT("国籍  —"),12,FAnchors(0,0,1,0),FMargin(20,16,20,28));
        Text(Profile,TEXT("PageHeading0"),TEXT("简历"),16,FAnchors(0,0,1,0),FMargin(20,64,20,30));
        Move(TEXT("PageRule0"),FAnchors(0,0,1,0),FMargin(20,102,20,1));
        Text(Profile,TEXT("BiographyText"),TEXT("暂无简历"),12,FAnchors(0,0,1,0),FMargin(20,120,20,160));
        Text(Profile,TEXT("BiographyHeading"),TEXT("行动日志"),16,FAnchors(0,0,1,0),FMargin(20,302,20,30));
        if (!D.T->FindWidget(TEXT("ActionLogRule"))) D.Box(Profile,TEXT("ActionLogRule"),TEXT("183A4D"),FAnchors(0,0,1,0),FMargin(20,340,20,1));
        Text(Profile,TEXT("ActionLogText"),TEXT("暂无行动记录"),12,FAnchors(0,0,1,0),FMargin(20,358,20,220));
        if (auto* Size=Cast<USizeBox>(D.T->FindWidget(TEXT("ProfilePageExtent")))) Size->SetHeightOverride(600);
        if (auto* Pack=D.T->FindWidget(TEXT("BackpackPanel"))) Pack->SetVisibility(ESlateVisibility::Collapsed);
        if (!Save(Room)) return 1;
        if (Portrait->GetParent()!=Identity || Identity->GetParent()!=Pages->GetParent()) return 1;
        UE_LOG(LogTemp,Display,TEXT("SHARED_PROFILE_LAYOUT_OK")); return 0;
    }
    if (Params.Contains(TEXT("Warehouse")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room) return 1;
        FDesigner D{Room->WidgetTree};
        auto* Roster=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("RosterPanel")));
        if (!Roster || !Cast<UCanvasPanel>(Roster->GetParent())) return 1;
        if (D.T->FindWidget(TEXT("WarehousePanel"))) { UE_LOG(LogTemp,Error,TEXT("Warehouse already authored; refusing to overwrite.")); return 1; }
        auto* Warehouse=D.Panel(TEXT("WarehousePanel"));
        auto* RosterSlot=CastChecked<UCanvasPanelSlot>(Roster->Slot);
        auto* WarehouseSlot=D.Place(CastChecked<UCanvasPanel>(Roster->GetParent()),Warehouse,RosterSlot->GetAnchors(),RosterSlot->GetOffsets(),RosterSlot->GetZOrder());
        WarehouseSlot->SetAlignment(RosterSlot->GetAlignment());
        D.Box(Warehouse,TEXT("WarehouseBackground"),TEXT("040C14F5"),FAnchors(0,0,1,1),FMargin(0));
        Warehouse->SetVisibility(ESlateVisibility::Collapsed);
        for (int32 I=0; I<2; ++I)
        {
            const FName Original=I?TEXT("人员列表滑出"):TEXT("人员列表滑入");
            UWidgetAnimation* Source=nullptr;
            for (UWidgetAnimation* A:Room->Animations) if (A->GetFName()==Original) Source=A;
            if (!Source) return 1;
            const FName NewName=I?TEXT("仓库滑出"):TEXT("仓库滑入");
            auto* Animation=DuplicateObject<UWidgetAnimation>(Source,Room,NewName);
            Animation->SetDisplayLabel(NewName.ToString());
            for (auto& Binding:Animation->AnimationBindings) if (Binding.WidgetName==TEXT("RosterPanel")) Binding.WidgetName=TEXT("WarehousePanel");
            Room->Animations.Add(Animation);
        }
        const auto* Schema=GetDefault<UEdGraphSchema_K2>();
        auto Link=[&](UEdGraphPin* A,UEdGraphPin* B){ check(A && B); check(Schema->TryCreateConnection(A,B)); };
        for (int32 I=0; I<2; ++I)
        {
            const bool Enter=I==0;
            UEdGraphNode* Left=nullptr; UEdGraphNode* Right=nullptr;
            for (UEdGraph* G:Room->UbergraphPages) for (UEdGraphNode* N:G->Nodes)
            {
                if (N->GetName()==(Enter?TEXT("K2Node_CallFunction_1"):TEXT("K2Node_CallFunction_3"))) Left=N;
                if (N->GetName()==(Enter?TEXT("K2Node_PlayAnimation2_1"):TEXT("K2Node_PlayAnimation2_0"))) Right=N;
            }
            if (!Left || !Right) return 1;
            auto* Graph=Left->GetGraph();
            auto Call=[&](UClass* Owner,FName Function,int X,int Y)
            {
                FGraphNodeCreator<UK2Node_CallFunction> C(*Graph); auto* N=C.CreateNode();
                N->SetFromFunction(Owner->FindFunctionByName(Function)); N->NodePosX=X; N->NodePosY=Y; C.Finalize(); return N;
            };
            auto* Selector=Call(UPersonnelPreparationRoomWidget::StaticClass(),GET_FUNCTION_NAME_CHECKED(UPersonnelPreparationRoomWidget,GetCurrentListAnimation),Left->NodePosX,Left->NodePosY+260);
            Selector->FindPinChecked(TEXT("bEntering"))->DefaultValue=Enter?TEXT("true"):TEXT("false");
            auto* Async=DuplicateObject<UEdGraphNode>(Right,Graph,MakeUniqueObjectName(Graph,Right->GetClass(),TEXT("ListAnimationWithFinished")));
            for (auto* P:Async->Pins) P->LinkedTo.Reset();
            Async->CreateNewGuid(); Async->NodePosX=Left->NodePosX; Async->NodePosY=Left->NodePosY;
            Graph->AddNode(Async,false,false);
            const auto Inputs=Left->FindPinChecked(TEXT("execute"))->LinkedTo;
            for (auto* P:Inputs) Link(P,Async->FindPinChecked(TEXT("execute")));
            Link(Async->FindPinChecked(TEXT("then")),Right->FindPinChecked(TEXT("execute")));
            for (auto* P:Right->FindPinChecked(TEXT("Widget"))->LinkedTo) Link(P,Async->FindPinChecked(TEXT("Widget")));
            Link(Selector->GetReturnValuePin(),Async->FindPinChecked(TEXT("InAnimation")));
            FBlueprintEditorUtils::RemoveNode(Room,Left,true);
            // Both concurrent animations report to a gate; Blueprint still issues the manual notification.
            Right->FindPinChecked(TEXT("Finished"))->BreakAllPinLinks();
            for (int32 Panel=0; Panel<2; ++Panel)
            {
                auto* A=Panel?Right:Async;
                const int X=A->NodePosX+400, Y=A->NodePosY+500+Panel*240;
                auto* Record=Call(UPersonnelPreparationRoomWidget::StaticClass(),GET_FUNCTION_NAME_CHECKED(UPersonnelPreparationRoomWidget,RecordPanelAnimationFinished),X,Y);
                Record->FindPinChecked(TEXT("bListPanel"))->DefaultValue=Panel?TEXT("false"):TEXT("true");
                Record->FindPinChecked(TEXT("bEntering"))->DefaultValue=Enter?TEXT("true"):TEXT("false");
                FGraphNodeCreator<UK2Node_IfThenElse> C(*Graph); auto* Branch=C.CreateNode(); Branch->NodePosX=X+300; Branch->NodePosY=Y; C.Finalize();
                auto* Complete=Call(UBaseSceneWidget::StaticClass(),Enter?GET_FUNCTION_NAME_CHECKED(UBaseSceneWidget,NotifyLoadCompleted):GET_FUNCTION_NAME_CHECKED(UBaseSceneWidget,NotifyUnloadCompleted),X+520,Y);
                Link(A->FindPinChecked(TEXT("Finished")),Record->GetExecPin());
                Link(Record->GetThenPin(),Branch->GetExecPin()); Link(Record->GetReturnValuePin(),Branch->GetConditionPin());
                Link(Branch->GetThenPin(),Complete->GetExecPin());
            }
        }
        if (!Save(Room)) return 1;
        UE_LOG(LogTemp,Display,TEXT("PERSONNEL_WAREHOUSE_OK")); return 0;
    }
    if (Params.Contains(TEXT("Inspect")))
    {
        auto* Room=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
        if (!Room) return 1;
        Room->WidgetTree->ForEachWidget([](UWidget* W)
        {
            FString Detail;
            if (auto* S=Cast<UCanvasPanelSlot>(W->Slot))
            {
                auto A=S->GetAnchors(); auto M=S->GetOffsets();
                Detail=FString::Printf(TEXT(" anchors=%s/%s offsets=%g,%g,%g,%g auto=%d"),*A.Minimum.ToString(),*A.Maximum.ToString(),M.Left,M.Top,M.Right,M.Bottom,S->GetAutoSize());
            }
            if (auto* S=Cast<USizeBox>(W)) Detail+=FString::Printf(TEXT(" height=%g minheight=%g width=%g"),S->GetHeightOverride(),S->GetMinDesiredHeight(),S->GetWidthOverride());
            UE_LOG(LogTemp,Display,TEXT("LAYOUT %s class=%s parent=%s%s"),*W->GetName(),*W->GetClass()->GetName(),*GetNameSafe(W->GetParent()),*Detail);
        });
        for (UWidgetAnimation* A:Room->Animations) UE_LOG(LogTemp,Display,TEXT("ROOM_ANIMATION %s"),*A->GetName());
        Room->WidgetTree->ForEachWidget([](UWidget* W){ UE_LOG(LogTemp,Display,TEXT("ROOM_WIDGET %s parent=%s"),*W->GetName(),*GetNameSafe(W->GetParent())); });
        TArray<UEdGraph*> InspectGraphs;Room->GetAllGraphs(InspectGraphs);
        for (UEdGraph* G:InspectGraphs) for (UEdGraphNode* N:G->Nodes)
        {
            UE_LOG(LogTemp,Display,TEXT("ROOM_NODE [%s] %s %s"),*G->GetName(),*N->GetName(),*N->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            for (auto* P:N->Pins)
            {
                FString Links; for (auto* L:P->LinkedTo) Links+=L->GetOwningNode()->GetName()+TEXT(".")+L->PinName.ToString()+TEXT(" ");
                UE_LOG(LogTemp,Display,TEXT("ROOM_PIN %s default=%s object=%s links=%s"),*P->PinName.ToString(),*P->DefaultValue,*GetNameSafe(P->DefaultObject),*Links);
            }
        }
        return 0;
    }
    auto* Tabs=Make(TEXT("/Game/System/UIBasic/WBP_SelectionButton"),USelectionButtonWidget::StaticClass());
    auto* Portrait=Make(TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/Components/WBP_PersonnelPortrait"),UPersonnelPortraitWidget::StaticClass());
    if (!Portrait) return 1;
    if (!Portrait->WidgetTree->RootWidget)
    {
        FDesigner D{Portrait->WidgetTree};
        auto* Root=D.Panel(TEXT("PortraitRoot")); D.T->RootWidget=Root;
        D.Box(Root,TEXT("PortraitPlate"),TEXT("102635"),FAnchors(0,0,1,1),FMargin(0));
        auto* Placeholder=D.Panel(TEXT("PortraitPlaceholder")); D.Place(Root,Placeholder,FAnchors(0,0,1,1),FMargin(0));
        auto* Head=D.Box(Placeholder,TEXT("Head"),TEXT("41657A"),FAnchors(.35f,.20f,.65f,.50f),FMargin(0));
        FSlateBrush Round=Head->GetBrush(); Round.DrawAs=ESlateBrushDrawType::RoundedBox; Round.OutlineSettings.RoundingType=ESlateBrushRoundingType::HalfHeightRadius; Head->SetBrush(Round);
        auto* Shoulders=D.Box(Placeholder,TEXT("Shoulders"),TEXT("304F64"),FAnchors(.18f,.55f,.82f,.87f),FMargin(0)); Shoulders->SetBrush(Round);
        D.Box(Root,TEXT("PortraitRule"),TEXT("329ACA"),FAnchors(0,1,1,1),FMargin(0,-1,0,1));
        auto* Image=D.Box(Root,TEXT("PortraitImage"),TEXT("FFFFFFFF"),FAnchors(0,0,1,1),FMargin(0)); Image->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (!Save(Portrait)) return 1;
    auto* Row=Make(TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/Components/WBP_PersonnelListEntry"),UPersonnelListEntryWidget::StaticClass());
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom"));
    if (!Tabs || !Row || !BP) return 1;
    Row->ParentClass=UPersonnelListEntryWidget::StaticClass();
    for (auto* ButtonBP : {Tabs,Row})
    {
        const bool IsRow=ButtonBP==Row;
        if (!ButtonBP->WidgetTree->RootWidget)
        {
            FDesigner D{ButtonBP->WidgetTree};
            auto* Size=D.T->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ButtonSize"));
            Size->SetVisibility(ESlateVisibility::HitTestInvisible); D.T->RootWidget=Size;
            auto* Canvas=D.Panel(TEXT("ButtonLayout")); Canvas->SetVisibility(ESlateVisibility::HitTestInvisible); Size->AddChild(Canvas);
            if (IsRow)
            {
                D.Text(Canvas,TEXT("IndexLabel"),TEXT("SC-001"),9,TEXT("5586A0"),FAnchors(0,0),FMargin(18,9,130,18));
                D.Text(Canvas,TEXT("Label"),TEXT("渡鸦 / 林曜"),17,TEXT("DCE9F5"),FAnchors(0,0,1,0),FMargin(18,27,16,28));
                D.Text(Canvas,TEXT("DetailLabel"),TEXT("突击兵 · Lv.08 · 待命"),11,TEXT("7798AC"),FAnchors(0,0,1,0),FMargin(18,59,16,22));
            }
            else D.Text(Canvas,TEXT("Label"),TEXT("选项"),14,TEXT("9CB4C5"),FAnchors(0,.5,1,.5),FMargin(4,-12,4,26))->SetJustification(ETextJustify::Center);
        }
        if (!IsRow)
        {
            FDesigner D{ButtonBP->WidgetTree};
            if (!D.T->FindWidget(TEXT("LabelAlignment")))
            {
                auto* Label=Cast<UTextBlock>(D.T->FindWidget(TEXT("Label")));
                Label->RemoveFromParent();
                auto* Alignment=D.T->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("LabelAlignment"));
                Alignment->SetVisibility(ESlateVisibility::HitTestInvisible);
                D.Place(Cast<UCanvasPanel>(D.T->FindWidget(TEXT("ButtonLayout"))),Alignment,FAnchors(0,0,1,1),FMargin(4,0,4,0));
                auto* Slot=Cast<USizeBoxSlot>(Alignment->AddChild(Label));
                Slot->SetHorizontalAlignment(HAlign_Center); Slot->SetVerticalAlignment(VAlign_Center);
                Label->SetJustification(ETextJustify::Center);
            }
        }
        if (IsRow)
        {
            FDesigner D{ButtonBP->WidgetTree};
            auto* Canvas=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("ButtonLayout")));
            if (!D.T->FindWidget(TEXT("ListPortrait")))
                D.Place(Canvas,D.T->ConstructWidget<UPersonnelPortraitWidget>(Portrait->GeneratedClass.Get(),TEXT("ListPortrait")),FAnchors(0,0),FMargin(12,11,68,68));
            for (const TCHAR* Name : {TEXT("IndexLabel"),TEXT("Label"),TEXT("DetailLabel")})
                if (auto* W=D.T->FindWidget(Name)) if (auto* S=Cast<UCanvasPanelSlot>(W->Slot)) { auto M=S->GetOffsets(); M.Left=94; S->SetOffsets(M); }
        }
        if (!Save(ButtonBP)) return 1;
        auto* Defaults=Cast<USelectionButtonWidget>(ButtonBP->GeneratedClass->GetDefaultObject());
        Defaults->MinimumSize=FVector2D(0,IsRow?90:40); Defaults->Font.Size=IsRow?15:14;
        Defaults->bUseTabStyle=!IsRow;
        Defaults->HoverSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuHover_Electronic"));
        Defaults->PressSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuPress_Terminal"));
        if (!Save(ButtonBP)) return 1;
    }
    if (!BP->WidgetTree->FindWidget(TEXT("PersonnelLayout")))
    {
        // The previously scaffolded ContentRoot is empty. Refuse to erase user additions.
        if (auto* Existing=Cast<UPanelWidget>(BP->WidgetTree->RootWidget); Existing && Existing->GetChildrenCount()>0)
        {
            UE_LOG(LogTemp,Error,TEXT("Personnel UI already contains user layout; preserving it.")); return 1;
        }
        FDesigner D{BP->WidgetTree};
        auto* Root=D.Panel(TEXT("PersonnelLayout")); D.T->RootWidget=Root;
        auto* Left=D.Panel(TEXT("RosterPanel")); D.Place(Root,Left,FAnchors(0,0,.27f,1),FMargin(24,20,20,20));
        auto* Right=D.Panel(TEXT("PersonnelPanel")); D.Place(Root,Right,FAnchors(.27f,0,1,1),FMargin(0,20,24,20));
        D.Box(Left,TEXT("RosterPlate"),TEXT("040C14F5"),FAnchors(0,0,1,1),FMargin(0));
        D.Box(Left,TEXT("RosterAccent"),TEXT("2E7D9B"),FAnchors(0,0),FMargin(0,0,3,26));
        D.Text(Left,TEXT("RosterCount"),TEXT("人员名册   06 / 08"),18,TEXT("DCE9F5"),FAnchors(0,0,1,0),FMargin(18,4,16,30));
        auto Choice=[&](UCanvasPanel* P,const TCHAR* Name,const TCHAR* Label,const TCHAR* ID,FAnchors A,FMargin M)
        {
            auto* B=D.T->ConstructWidget<USelectionButtonWidget>(Tabs->GeneratedClass.Get(),Name);
            B->ButtonText=FText::FromString(Label); B->ChoiceID=ID; D.Place(P,B,A,M); return B;
        };
        Choice(Left,TEXT("StandbyFilter"),TEXT("待命"),TEXT("Standby"),FAnchors(0,0,.5,0),FMargin(16,48,4,40));
        Choice(Left,TEXT("AllFilter"),TEXT("全部"),TEXT("All"),FAnchors(.5,0,1,0),FMargin(4,48,16,40));
        auto* List=D.T->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("PersonnelList"));
        D.Place(Left,List,FAnchors(0,0,1,1),FMargin(16,108,8,42));
        D.Text(Left,TEXT("EmptyRoster"),TEXT("当前筛选下暂无人员"),13,TEXT("7798AC"),FAnchors(0,0,1,0),FMargin(24,136,24,40),true)->SetVisibility(ESlateVisibility::Collapsed);
        D.Text(Left,TEXT("DataSourceLabel"),TEXT("演示数据 / SAMPLE ROSTER"),9,TEXT("537C98"),FAnchors(0,1,1,1),FMargin(18,-26,18,22));

        Choice(Right,TEXT("ProfileTab"),TEXT("人员档案"),TEXT("Profile"),FAnchors(0,0,.333333f,0),FMargin(0,0,8,40));
        Choice(Right,TEXT("EquipmentTab"),TEXT("装备配置"),TEXT("Equipment"),FAnchors(.333333f,0,.666667f,0),FMargin(0,0,8,40));
        Choice(Right,TEXT("TrainingTab"),TEXT("技能训练"),TEXT("Training"),FAnchors(.666667f,0,1,0),FMargin(0,0,0,40));
        auto* Identity=D.Panel(TEXT("IdentityPanel")); D.Place(Right,Identity,FAnchors(0,0,1,0),FMargin(0,58,0,108));
        D.Box(Identity,TEXT("IdentityPlate"),TEXT("081723F5"),FAnchors(0,0,1,1),FMargin(0));
        D.Box(Identity,TEXT("IdentityAccent"),TEXT("329ACA"),FAnchors(0,0,0,1),FMargin(0,0,3,0));
        D.Text(Identity,TEXT("IdentityCaption"),TEXT("SILVER CHOIR / OPERATOR RECORD"),9,TEXT("537C98"),FAnchors(0,0),FMargin(24,12,500,20));
        D.Text(Identity,TEXT("SelectedName"),TEXT("渡鸦  /  林曜"),30,TEXT("DCE9F5"),FAnchors(0,0,1,0),FMargin(24,33,140,44));
        D.Text(Identity,TEXT("SelectedMeta"),TEXT("SC-001 / 突击兵 / LEVEL 08"),11,TEXT("7798AC"),FAnchors(0,0,1,0),FMargin(24,80,20,24));
        D.Text(Identity,TEXT("SelectedStatus"),TEXT("● 待命"),12,TEXT("55AEC7"),FAnchors(1,0),FMargin(-114,42,100,24));

        auto* Pages=D.T->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(),TEXT("DetailPages"));
        Pages->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        D.Place(Right,Pages,FAnchors(0,0,.63f,1),FMargin(0,184,16,0));
        const TCHAR* Names[]={TEXT("ProfilePage"),TEXT("EquipmentPage"),TEXT("TrainingPage")};
        const TCHAR* Titles[]={TEXT("档案概要"),TEXT("当前装备"),TEXT("技能与训练")};
        for (int32 I=0; I<3; ++I)
        {
            auto* P=D.Panel(Names[I]); auto* Slot=Cast<UWidgetSwitcherSlot>(Pages->AddChild(P));
            Slot->SetHorizontalAlignment(HAlign_Fill); Slot->SetVerticalAlignment(VAlign_Fill);
            D.Box(P,*FString::Printf(TEXT("PagePlate%d"),I),TEXT("040C14F5"),FAnchors(0,0,1,1),FMargin(0));
            D.Text(P,*FString::Printf(TEXT("PageHeading%d"),I),Titles[I],19,TEXT("DCE9F5"),FAnchors(0,0,1,0),FMargin(24,20,24,32));
            D.Box(P,*FString::Printf(TEXT("PageRule%d"),I),TEXT("183A4D"),FAnchors(0,0,1,0),FMargin(24,64,24,1));
            if (I==0)
            {
                D.Text(P,TEXT("ProfileText"),TEXT("人员编号    SC-001"),15,TEXT("9CB4C5"),FAnchors(0,0,1,0),FMargin(24,88,24,210),true);
                D.Text(P,TEXT("BiographyHeading"),TEXT("履历摘要"),13,TEXT("5586A0"),FAnchors(0,0,1,0),FMargin(24,292,24,26));
                D.Text(P,TEXT("BiographyText"),TEXT("演示履历"),13,TEXT("9CB4C5"),FAnchors(0,0,1,1),FMargin(24,330,24,162),true);
                D.Text(P,TEXT("HealthText"),TEXT("健康状态"),12,TEXT("9CB4C5"),FAnchors(0,1,1,1),FMargin(24,-132,24,24));
                D.Text(P,TEXT("MoraleText"),TEXT("士气评估"),12,TEXT("9CB4C5"),FAnchors(0,1,1,1),FMargin(24,-74,24,24));
                for (int32 J=0; J<2; ++J)
                {
                    auto* Bar=D.T->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),J?TEXT("MoraleBar"):TEXT("HealthBar"));
                    Bar->SetFillColorAndOpacity(FLinearColor(FColor::FromHex(J?TEXT("477DAB"):TEXT("389EAF"))));
                    Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
                    D.Place(P,Bar,FAnchors(0,1,1,1),FMargin(24,J?-40:-98,24,4));
                }
            }
            else
            {
                D.Text(P,I==1?TEXT("EquipmentText"):TEXT("TrainingText"),TEXT("等待人员信息"),17,TEXT("9CB4C5"),FAnchors(0,0,1,1),FMargin(24,92,24,108),true);
                D.Text(P,*FString::Printf(TEXT("PageNote%d"),I),I==1?TEXT("装备预览 · 当前为演示配置"):TEXT("训练预览 · 正式训练功能尚未接入"),11,TEXT("537C98"),FAnchors(0,1,1,1),FMargin(24,-66,24,44),true);
            }
        }
        auto* Pack=D.Panel(TEXT("BackpackPanel")); D.Place(Right,Pack,FAnchors(.63f,0,1,1),FMargin(0,184,0,0));
        D.Box(Pack,TEXT("BackpackPlate"),TEXT("040C14F5"),FAnchors(0,0,1,1),FMargin(0));
        D.Text(Pack,TEXT("BackpackHeading"),TEXT("随身背包"),19,TEXT("DCE9F5"),FAnchors(0,0,1,0),FMargin(20,20,20,32));
        D.Text(Pack,TEXT("InventoryCount"),TEXT("已占用 5 / 8"),11,TEXT("537C98"),FAnchors(0,0,1,0),FMargin(20,58,20,22));
        for (int32 I=0; I<8; ++I)
        {
            const float X=(I%2)*.5f; const float Y=100+(I/2)*110;
            auto* Cell=D.Panel(*FString::Printf(TEXT("InventoryCell%d"),I));
            D.Place(Pack,Cell,FAnchors(X,0,X+.5f,0),FMargin(I%2?5:20,Y,I%2?20:5,98));
            D.Box(Cell,*FString::Printf(TEXT("CellPlate%d"),I),TEXT("0A1925"),FAnchors(0,0,1,1),FMargin(0));
            D.Box(Cell,*FString::Printf(TEXT("CellRule%d"),I),TEXT("254757"),FAnchors(0,1,1,1),FMargin(0,-1,0,1));
            D.Text(Cell,*FString::Printf(TEXT("CellIndex%d"),I),*FString::Printf(TEXT("%02d / SUPPLY"),I+1),9,TEXT("41657A"),FAnchors(0,0,1,0),FMargin(10,10,10,20));
            D.Text(Cell,*FString::Printf(TEXT("Inventory%d"),I),TEXT("空槽"),12,TEXT("9CB4C5"),FAnchors(0,0,1,1),FMargin(10,39,10,8),true);
        }
        D.Text(Pack,TEXT("BackpackNote"),TEXT("个人携行物资\n随人员选择同步显示"),10,TEXT("537C98"),FAnchors(0,1,1,1),FMargin(20,-58,20,42),true);
    }
    if (Params.Contains(TEXT("ModelLayout")))
    {
        FDesigner D{BP->WidgetTree};
        auto* Root=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("PersonnelLayout")));
        auto* Left=D.T->FindWidget(TEXT("RosterPanel"));
        auto* Right=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("PersonnelPanel")));
        auto* Pages=Cast<UWidgetSwitcher>(D.T->FindWidget(TEXT("DetailPages")));
        auto* Pack=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("BackpackPanel")));
        if (!Root || !Left || !Right || !Pages || !Pack) return 1;
        auto Reposition=[](UWidget* W,FAnchors A,FMargin M)
        {
            if (auto* S=Cast<UCanvasPanelSlot>(W->Slot)) { S->SetAnchors(A); S->SetOffsets(M); }
        };
        Reposition(Left,FAnchors(0,0,.25f,1),FMargin(24,20,12,20));
        Reposition(D.T->FindWidget(TEXT("StandbyFilter")),FAnchors(0,0,.5f,0),FMargin(16,48,1,36));
        Reposition(D.T->FindWidget(TEXT("AllFilter")),FAnchors(.5f,0,1,0),FMargin(1,48,16,36));
        Reposition(D.T->FindWidget(TEXT("ProfileTab")),FAnchors(0,0,.333333f,0),FMargin(0,0,2,40));
        Reposition(D.T->FindWidget(TEXT("EquipmentTab")),FAnchors(.333333f,0,.666667f,0),FMargin(0,0,2,40));
        Reposition(Right,FAnchors(.75f,0,1,1),FMargin(12,20,24,20));
        if (!D.T->FindWidget(TEXT("ModelDisplayArea")))
        {
            auto* Model=D.Panel(TEXT("ModelDisplayArea"));
            D.Place(Root,Model,FAnchors(.25f,0,.75f,1),FMargin(12,20,12,20));
            D.Text(Model,TEXT("ModelPreviewCaption"),TEXT("人员模型展示区"),11,TEXT("41657A"),FAnchors(0,1,1,1),FMargin(20,-34,20,24))->SetJustification(ETextJustify::Center);
        }
        Reposition(Pages,FAnchors(0,0,1,1),FMargin(0,184,0,280));
        for (const TCHAR* Name : {TEXT("ProfilePage"),TEXT("EquipmentPage"),TEXT("TrainingPage")})
        {
            auto* Page=D.T->FindWidget(Name);
            if (!Page) return 1;
            const FName ScrollName(*(FString(Name)+TEXT("Scroll")));
            if (!D.T->FindWidget(ScrollName))
            {
                Page->RemoveFromParent();
                auto* Scroll=D.T->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),ScrollName);
                auto* Size=D.T->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*(FString(Name)+TEXT("Extent"))));
                Size->SetHeightOverride(680); Size->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
                Size->AddChild(Page); Scroll->AddChild(Size);
                auto* S=Cast<UWidgetSwitcherSlot>(Pages->AddChild(Scroll));
                S->SetHorizontalAlignment(HAlign_Fill); S->SetVerticalAlignment(VAlign_Fill);
            }
        }
        Pages->SetActiveWidgetIndex(0);
        auto* Profile=Cast<UCanvasPanel>(D.T->FindWidget(TEXT("ProfilePage")));
        if (!D.T->FindWidget(TEXT("DetailPortrait")))
            D.Place(Profile,D.T->ConstructWidget<UPersonnelPortraitWidget>(Portrait->GeneratedClass.Get(),TEXT("DetailPortrait")),FAnchors(0,0),FMargin(24,84,112,112));
        Cast<USizeBox>(D.T->FindWidget(TEXT("ProfilePageExtent")))->SetHeightOverride(900);
        Reposition(D.T->FindWidget(TEXT("ProfileText")),FAnchors(0,0,1,0),FMargin(24,220,24,190));
        Reposition(D.T->FindWidget(TEXT("BiographyHeading")),FAnchors(0,0,1,0),FMargin(24,430,24,26));
        Reposition(D.T->FindWidget(TEXT("BiographyText")),FAnchors(0,0,1,1),FMargin(24,465,24,162));
        Reposition(Pack,FAnchors(0,1,1,1),FMargin(0,-260,0,260));
        auto FontSize=[&](const TCHAR* Name,int32 Size)
        {
            if (auto* T=Cast<UTextBlock>(D.T->FindWidget(Name))) { auto F=T->GetFont(); F.Size=Size; T->SetFont(F); }
        };
        for (const TCHAR* Name : {TEXT("ProfileTab"),TEXT("EquipmentTab"),TEXT("TrainingTab")})
            if (auto* B=Cast<USelectionButtonWidget>(D.T->FindWidget(Name))) B->Font.Size=12;
        FontSize(TEXT("SelectedName"),22);
        Reposition(D.T->FindWidget(TEXT("SelectedName")),FAnchors(0,0,1,0),FMargin(20,33,20,40));
        auto* Caption=Cast<UTextBlock>(D.T->FindWidget(TEXT("IdentityCaption")));
        Caption->SetText(FText::FromString(TEXT("OPERATOR / 人员")));
        Reposition(Caption,FAnchors(0,0),FMargin(20,12,170,20));
        Reposition(D.T->FindWidget(TEXT("SelectedStatus")),FAnchors(1,0),FMargin(-90,12,80,22));
        FontSize(TEXT("SelectedStatus"),10);
        FontSize(TEXT("SelectedMeta"),10);
        Reposition(D.T->FindWidget(TEXT("SelectedMeta")),FAnchors(0,0,1,0),FMargin(20,82,20,22));
        for (int32 I=0; I<3; ++I) FontSize(*FString::Printf(TEXT("PageHeading%d"),I),16);
        FontSize(TEXT("ProfileText"),13); FontSize(TEXT("EquipmentText"),14); FontSize(TEXT("TrainingText"),14);
        FontSize(TEXT("BiographyText"),12);
        FontSize(TEXT("BackpackHeading"),16);
        Reposition(D.T->FindWidget(TEXT("BackpackHeading")),FAnchors(0,0),FMargin(16,12,160,28));
        Reposition(D.T->FindWidget(TEXT("InventoryCount")),FAnchors(1,0),FMargin(-112,18,96,22));
        FontSize(TEXT("InventoryCount"),10);
        for (int32 I=0; I<8; ++I)
        {
            const float X=(I%4)*.25f;
            Reposition(D.T->FindWidget(*FString::Printf(TEXT("InventoryCell%d"),I)),FAnchors(X,0,X+.25f,0),FMargin(I%4?3:16,58+(I/4)*80,I%4==3?16:3,72));
            auto* Label=D.T->FindWidget(*FString::Printf(TEXT("Inventory%d"),I));
            Reposition(Label,FAnchors(0,0,1,1),FMargin(7,26,7,6));
            FontSize(*FString::Printf(TEXT("Inventory%d"),I),10);
            auto* Index=Cast<UTextBlock>(D.T->FindWidget(*FString::Printf(TEXT("CellIndex%d"),I)));
            Index->SetText(FText::FromString(FString::Printf(TEXT("%02d"),I+1)));
            Reposition(Index,FAnchors(0,0,1,0),FMargin(7,5,7,18));
        }
        auto* Note=Cast<UTextBlock>(D.T->FindWidget(TEXT("BackpackNote")));
        Note->SetText(FText::FromString(TEXT("个人携行物资")));
        Reposition(Note,FAnchors(0,1,1,1),FMargin(16,-30,16,22));
    }
    else if (Params.Contains(TEXT("Spacing")))
    {
        for (const TCHAR* Name : {TEXT("RosterPanel"),TEXT("PersonnelPanel")})
            if (auto* W=BP->WidgetTree->FindWidget(Name))
                if (auto* S=Cast<UCanvasPanelSlot>(W->Slot))
                    S->SetOffsets(FString(Name)==TEXT("RosterPanel")?FMargin(24,20,20,20):FMargin(0,20,24,20));
    }
    if (!Save(BP)) return 1;
    Cast<UPersonnelPreparationRoomWidget>(BP->GeneratedClass->GetDefaultObject())->PersonnelRowClass=Row->GeneratedClass.Get();
    if (!Save(BP)) return 1;
    UE_LOG(LogTemp,Display,TEXT("PERSONNEL_UI_ASSETS_OK")); return 0;
}
