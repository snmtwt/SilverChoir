#include "BattleModeAssets.h"

#include "MainMapBlueprintBuilder.h"
#include "Map/BattleMap/BattleHUDWidgets.h"
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/BattleMap/BattleModePanels.h"
#include "Map/BattleMap/BattleModeScrollBox.h"
#include "Animation/WidgetAnimation.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace BattleModeAssets
{
const FString Folder = TEXT("/Game/System/Map/BattleMap/UI/");
FLinearColor Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }

UWidget* RenameWidget(UWidgetBlueprint* BP, const TCHAR* OldName, const TCHAR* NewName)
{
    UWidget* Widget = BP->WidgetTree->FindWidget(NewName);
    if (!Widget)
    {
        Widget = BP->WidgetTree->FindWidget(OldName);
        if (!Widget || !Widget->Rename(NewName, BP->WidgetTree, REN_DontCreateRedirectors | REN_NonTransactional))
            return nullptr;
    }
    Widget->bIsVariable = true;
    if (const FGuid* Guid = BP->WidgetVariableNameToGuidMap.Find(OldName))
    {
        const FGuid Existing = *Guid;
        BP->WidgetVariableNameToGuidMap.Remove(OldName);
        BP->WidgetVariableNameToGuidMap.Add(NewName, Existing);
    }
    for (auto& Binding : BP->Bindings)
        if (Binding.ObjectName == OldName) Binding.ObjectName = NewName;
    for (UWidgetAnimation* Animation : BP->Animations)
        for (auto& Binding : Animation->AnimationBindings)
        {
            if (Binding.WidgetName == OldName) Binding.WidgetName = NewName;
            if (Binding.SlotWidgetName == OldName) Binding.SlotWidgetName = NewName;
        }
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (auto* Variable = Cast<UK2Node_Variable>(Node);
                Variable && Variable->VariableReference.IsSelfContext() && Variable->VariableReference.GetMemberName() == OldName)
            {
                Variable->VariableReference.SetSelfMember(NewName);
                Variable->ReconstructNode();
            }
        }
    return Widget;
}

bool Backup(const FString& PackagePath, const FString& BackupFolder)
{
    const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
    if (!FPaths::FileExists(File)) return true;
    const FString Destination = BackupFolder / (FPackageName::GetShortName(PackagePath) + TEXT(".uasset"));
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
    BP->ForEachSourceWidget([&](UWidget* Widget)
    {
        if (!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
            BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
    });
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    return MainMapBP::Compile(BP);
}

void StyleFrame(UBorder* Border, const TCHAR* Fill = TEXT("07151EF2"))
{
    if (!Border) return;
    // Rounded-box outline tint is independent from the Border's brush color.
    // Replace both to remove the old bright two-pixel frames, retaining padding.
    FSlateBrush Brush;
    Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
    Brush.TintColor = Color(Fill);
    Brush.OutlineSettings.Color = Color(TEXT("294555CC"));
    Brush.OutlineSettings.Width = 1.f;
    Brush.OutlineSettings.CornerRadii = FVector4(0, 0, 0, 0);
    Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
    Border->SetBrush(Brush);
    Border->SetBrushColor(FLinearColor::White);
    Border->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void StyleButton(UBattleHUDButton* Button)
{
    Button->MinimumSize = FVector2D::ZeroVector;
    Button->BackgroundColor = Color(TEXT("081C28F5"));
    Button->BorderColor = Color(TEXT("294B60FF"));
    Button->BorderThickness = 1.f;
    Button->AccentColor = Color(TEXT("26D9ECFF"));
    Button->ButtonForegroundColor = Color(TEXT("A6C6D4FF"));
    Button->Font.Size = 10;
    Button->ButtonSubtitle = FText::GetEmpty();
    Button->ButtonIndex = FText::GetEmpty();
    Button->bUseTabStyle = true;
    Button->bShowGlyph = true;
    Button->SetVisibility(ESlateVisibility::Visible);
    Button->SetIsFocusable(true);
}

UWidgetBlueprint* MakeModeButton()
{
    const FString Path = Folder + TEXT("Components/WBP_BattleModeButton");
    UWidgetBlueprint* BP = LoadObject<UWidgetBlueprint>(nullptr, *Path);
    if (!BP)
    {
        auto* Factory = NewObject<UWidgetBlueprintFactory>();
        Factory->ParentClass = UBattleHUDButton::StaticClass();
        BP = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(), CreatePackage(*Path),
            *FPackageName::GetShortName(Path), RF_Public | RF_Standalone, nullptr, GWarn));
        if (!BP) return nullptr;
        FAssetRegistryModule::AssetCreated(BP);
    }
    if (!BP->WidgetTree->RootWidget)
    {
        // Dedicated compact presentation leaves the shared minimap/vehicle
        // button asset unchanged. VerticalBox slots avoid native canvas offsets.
        UWidgetTree* Tree = BP->WidgetTree;
        auto* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ModeButtonRoot"));
        Root->SetVisibility(ESlateVisibility::HitTestInvisible);
        Tree->RootWidget = Root;
        auto* Content = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ModeButtonContent"));
        auto* ContentSlot = Root->AddChildToCanvas(Content);
        ContentSlot->SetAnchors(FAnchors(.5f, .5f));
        ContentSlot->SetAlignment(FVector2D(.5f, .5f));
        ContentSlot->SetAutoSize(true);
        ContentSlot->SetOffsets(FMargin(0));
        auto* IconSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ModeIconSize"));
        IconSize->SetWidthOverride(16.f);
        IconSize->SetHeightOverride(18.f);
        auto* IconSlot = Content->AddChildToVerticalBox(IconSize);
        IconSlot->SetHorizontalAlignment(HAlign_Center);
        IconSlot->SetPadding(FMargin(0, 0, 0, 5));
        auto* Symbol = Tree->ConstructWidget<UBattleHUDVisual>(UBattleHUDVisual::StaticClass(), TEXT("Symbol"));
        Symbol->SetVisibility(ESlateVisibility::HitTestInvisible);
        Symbol->Tint = Color(TEXT("8AC4D8FF"));
        Symbol->Glyph = EBattleGlyph::Person;
        IconSize->AddChild(Symbol);
        auto* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
        Label->SetText(FText::FromString(TEXT("成\n员")));
        Label->SetJustification(ETextJustify::Center);
        Label->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), 10));
        Label->SetColorAndOpacity(Color(TEXT("A6C6D4FF")));
        Label->SetVisibility(ESlateVisibility::HitTestInvisible);
        Content->AddChildToVerticalBox(Label)->SetHorizontalAlignment(HAlign_Center);
    }
    if (!Compile(BP)) return nullptr;
    auto* Defaults = CastChecked<UBattleHUDButton>(BP->GeneratedClass->GetDefaultObject());
    StyleButton(Defaults);
    Defaults->bVerticalLabel = true;
    Defaults->ButtonText = FText::FromString(TEXT("成\n员"));
    Defaults->DesignSizeMode = EDesignPreviewSizeMode::Custom;
    Defaults->DesignTimeSize = FVector2D(26.f, 78.f);
    return BP;
}

bool UpgradePanel(UWidgetBlueprint* BP, bool bSquad)
{
    if (!RenameWidget(BP, TEXT("ScrollBox_小队列表"), TEXT("SquadScrollBox"))
        || !RenameWidget(BP, TEXT("队员模式_小队列表"), TEXT("SquadList"))
        || !RenameWidget(BP, TEXT("ScrollBox_成员列表"), bSquad ? TEXT("SquadCardScrollBox") : TEXT("MemberScrollBox"))
        || !RenameWidget(BP, TEXT("HorizontalBox_91"), bSquad ? TEXT("SquadCardList") : TEXT("MemberList"))
        || !RenameWidget(BP, TEXT("显示车辆信息按钮"), TEXT("VehicleButton")))
        return false;
    BP->ParentClass = bSquad ? UBattleSquadModeWidget::StaticClass() : UBattleMemberModeWidget::StaticClass();
    for (const TCHAR* Name : {TEXT("Border_130"), TEXT("Border"), TEXT("Border_2")})
        StyleFrame(Cast<UBorder>(BP->WidgetTree->FindWidget(Name)));
    if (auto* Button = Cast<UBattleHUDButton>(BP->WidgetTree->FindWidget(TEXT("VehicleButton"))))
    {
        StyleButton(Button);
        Button->ChoiceID = TEXT("Vehicle");
        Button->Glyph = EBattleGlyph::Vehicle;
        Button->SetToolTipText(FText::FromString(TEXT("小队车辆")));
    }
    return Compile(BP);
}

void CopyButtonConfiguration(const UBattleHUDButton* From, UBattleHUDButton* To)
{
    // Preserve inherited button options, sounds and delegates, but never copy
    // UUserWidget's WidgetTree or transient widget bindings into the new class.
    for (TFieldIterator<FProperty> It(UBattleHUDButton::StaticClass()); It; ++It)
    {
        const UClass* Owner = It->GetOwnerClass();
        if (Owner && Owner->IsChildOf(UBasicButtonWidget::StaticClass())
            && !It->HasAnyPropertyFlags(CPF_Transient)
            && !It->HasMetaData(TEXT("BindWidget")) && !It->HasMetaData(TEXT("BindWidgetOptional")))
            It->CopyCompleteValue_InContainer(To, From);
    }
    To->SetRenderTransform(From->GetRenderTransform());
    To->SetRenderTransformPivot(From->GetRenderTransformPivot());
    To->SetRenderOpacity(From->GetRenderOpacity());
    To->SetClipping(From->GetClipping());
    To->SetIsEnabled(From->GetIsEnabled());
}

bool ReplaceModeButton(UWidgetBlueprint* BP, const TCHAR* Name, UClass* Class, bool bSquad)
{
    auto* Button = Cast<UBattleHUDButton>(BP->WidgetTree->FindWidget(Name));
    if (!Button) return false;
    if (Button->GetClass() != Class)
    {
        UPanelWidget* Parent = Button->GetParent();
        if (!Parent) return false;
        auto* Old = Button;
        if (!Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional)) return false;
        Button = BP->WidgetTree->ConstructWidget<UBattleHUDButton>(Class, Name);
        CopyButtonConfiguration(Old, Button);
        if (!Parent->ReplaceChild(Old, Button)) return false;
        Old->Slot = nullptr;
    }
    Button->bIsVariable = true;
    StyleButton(Button);
    Button->ChoiceID = bSquad ? TEXT("SquadMode") : TEXT("MemberMode");
    Button->Glyph = bSquad ? EBattleGlyph::Squad : EBattleGlyph::Person;
    Button->bVerticalLabel = true;
    Button->ButtonText = FText::FromString(bSquad ? TEXT("小\n队") : TEXT("成\n员"));
    Button->SetToolTipText(FText::FromString(bSquad ? TEXT("切换到小队模式") : TEXT("切换到成员模式")));
    return true;
}

bool ReplaceModePages(UWidgetBlueprint* BP)
{
    auto* Old = Cast<UScrollBox>(BP->WidgetTree->FindWidget(TEXT("ModePages")));
    if (!Old) return false;
    if (Old->IsA<UBattleModeScrollBox>()) return true;
    UPanelWidget* Parent = Old->GetParent();
    if (!Parent) return false;
    if (!Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional)) return false;
    auto* Pages = BP->WidgetTree->ConstructWidget<UBattleModeScrollBox>(UBattleModeScrollBox::StaticClass(), TEXT("ModePages"));
    Pages->bIsVariable = true;
    Pages->SetOrientation(Old->GetOrientation());
    Pages->SetScrollAnimationInterpolationSpeed(Old->GetScrollAnimationInterpolationSpeed());
    Pages->SetRenderTransform(Old->GetRenderTransform());
    Pages->SetRenderOpacity(Old->GetRenderOpacity());
    Pages->SetVisibility(Old->GetVisibility());
    FScrollBoxStyle PageStyle = Old->GetWidgetStyle();
    const FSlateNoResource NoShadow;
    PageStyle.SetTopShadowBrush(NoShadow).SetBottomShadowBrush(NoShadow)
        .SetLeftShadowBrush(NoShadow).SetRightShadowBrush(NoShadow)
        .SetHorizontalScrolledContentPadding(FMargin(0))
        .SetVerticalScrolledContentPadding(FMargin(0));
    Pages->SetWidgetStyle(PageStyle);
    if (!Parent->ReplaceChild(Old, Pages)) return false;
    Old->Slot = nullptr;
    const TArray<UWidget*> Children = Old->GetAllChildren();
    for (UWidget* Child : Children)
    {
        const auto* SourceSlot = CastChecked<UScrollBoxSlot>(Child->Slot);
        const FMargin Padding = SourceSlot->GetPadding();
        const EHorizontalAlignment Horizontal = SourceSlot->GetHorizontalAlignment();
        const EVerticalAlignment Vertical = SourceSlot->GetVerticalAlignment();
        const FSlateChildSize Size = SourceSlot->GetSize();
        Old->RemoveChild(Child);
        auto* Slot = CastChecked<UScrollBoxSlot>(Pages->AddChild(Child));
        Slot->SetPadding(Padding);
        Slot->SetHorizontalAlignment(Horizontal);
        Slot->SetVerticalAlignment(Vertical);
        Slot->SetSize(Size);
    }
    return true;
}

void AddModeBusinessEvent(UWidgetBlueprint* BP)
{
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
            if (const auto* Event = Cast<UK2Node_Event>(Node);
                Event && Event->EventReference.GetMemberName() == TEXT("OnControlModeChanged")) return;
    auto* Graph = FBlueprintEditorUtils::CreateNewGraph(BP, TEXT("ControlModeBusiness"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(BP, Graph);
    MainMapBP::FGraph Builder{BP, Graph};
    Builder.Comment(TEXT("C++管理模式切页、输入限制和按钮选中状态。这里连接切换模式后的游戏业务，例如更新选中对象或输入上下文；可直接访问 MemberModePanel / SquadModePanel 中的列表容器。"));
    Builder.X = 0;
    Builder.Y = 180;
    Builder.Node<UK2Node_Event>([](auto* Event)
    {
        Event->EventReference.SetExternalMember(TEXT("OnControlModeChanged"), UBattleMapWidget::StaticClass());
        Event->bOverrideFunction = true;
    });
}
}

bool UpgradeBattleModeAssets()
{
    using namespace BattleModeAssets;
    const FString MemberPath = Folder + TEXT("Components/WBP_成员模式");
    const FString SquadPath = Folder + TEXT("Components/WBP_小队模式");
    const FString HUDPath = Folder + TEXT("WBP_BattleHUD");
    const FString ButtonPath = Folder + TEXT("Components/WBP_BattleModeButton");
    const FString BackupFolder = FPaths::ProjectSavedDir() / TEXT("BattleModes/Backup") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
    for (const FString& Path : {MemberPath, SquadPath, HUDPath, ButtonPath})
        if (!Backup(Path, BackupFolder)) return false;
    auto* Member = LoadObject<UWidgetBlueprint>(nullptr, *MemberPath);
    auto* Squad = LoadObject<UWidgetBlueprint>(nullptr, *SquadPath);
    if (!Member || !Squad || !Member->WidgetTree || !Squad->WidgetTree) return false;
    if (!UpgradePanel(Member, false) || !UpgradePanel(Squad, true)) return false;
    auto* Button = MakeModeButton();
    if (!Button) return false;
    // Load the owner after compiling its two nested panels to avoid stale types.
    auto* HUD = LoadObject<UWidgetBlueprint>(nullptr, *HUDPath);
    if (!HUD || !HUD->WidgetTree) return false;
    if (!RenameWidget(HUD, TEXT("ScrollBox_模式切换"), TEXT("ModePages"))
        || !RenameWidget(HUD, TEXT("WBP_成员模式"), TEXT("MemberModePanel"))
        || !RenameWidget(HUD, TEXT("WBP_小队模式"), TEXT("SquadModePanel"))
        || !RenameWidget(HUD, TEXT("成员模式按钮"), TEXT("MemberModeButton"))
        || !RenameWidget(HUD, TEXT("小队模式按钮"), TEXT("SquadModeButton"))) return false;
    if (!ReplaceModePages(HUD)
        || !ReplaceModeButton(HUD, TEXT("MemberModeButton"), Button->GeneratedClass, false)
        || !ReplaceModeButton(HUD, TEXT("SquadModeButton"), Button->GeneratedClass, true)) return false;
    StyleFrame(Cast<UBorder>(HUD->WidgetTree->FindWidget(TEXT("Border_132"))), TEXT("06121BE8"));
    StyleFrame(Cast<UBorder>(HUD->WidgetTree->FindWidget(TEXT("Border_1"))));
    AddModeBusinessEvent(HUD);
    if (!Compile(HUD)) return false;
    for (UWidgetBlueprint* BP : {Button, Member, Squad, HUD}) if (!Save(BP)) return false;
    UE_LOG(LogTemp, Display, TEXT("BATTLE_MODE_UPGRADE_OK authored contents and slots retained; mode paging, typed list bindings, compact buttons, Blueprint business event; backup=%s"), *BackupFolder);
    return true;
}

namespace BattleModeIcons
{
bool HasSymbolReferences(const UWidgetBlueprint* BP)
{
    // Never silently break a custom binding added after the original mode upgrade.
    for (const auto& Binding : BP->Bindings)
        if (Binding.ObjectName == TEXT("Symbol")) return true;
    for (const UWidgetAnimation* Animation : BP->Animations)
        for (const auto& Binding : Animation->AnimationBindings)
            if (Binding.WidgetName == TEXT("Symbol") || Binding.SlotWidgetName == TEXT("Symbol")) return true;
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
        for (const UEdGraphNode* Node : Graph->Nodes)
            if (const auto* Variable = Cast<UK2Node_Variable>(Node);
                Variable && Variable->VariableReference.IsSelfContext()
                && Variable->VariableReference.GetMemberName() == TEXT("Symbol")) return true;
    return false;
}

UTexture2D* Import(const FString& SourceFile, const FString& PackagePath)
{
    const bool bExisting = LoadObject<UTexture2D>(nullptr, *PackagePath) != nullptr;
    auto* Factory = NewObject<UTextureFactory>();
    Factory->SuppressImportOverwriteDialog(true);
    bool bCancelled = false;
    auto* Texture = Cast<UTexture2D>(Factory->FactoryCreateFile(UTexture2D::StaticClass(), CreatePackage(*PackagePath),
        *FPackageName::GetShortName(PackagePath), RF_Public | RF_Standalone, SourceFile, nullptr, GWarn, bCancelled));
    if (!Texture || bCancelled) return nullptr;
    Texture->CompressionSettings = TC_EditorIcon;
    Texture->CompressionNoAlpha = false;
    Texture->LODGroup = TEXTUREGROUP_UI;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->MaxTextureSize = 128;
    Texture->SRGB = true;
    Texture->NeverStream = true;
    Texture->Filter = TF_Bilinear;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->PostEditChange();
    if (!bExisting) FAssetRegistryModule::AssetCreated(Texture);
    Texture->MarkPackageDirty();
    return Texture;
}

bool SaveTexture(UTexture2D* Texture)
{
    const FString File = FPackageName::LongPackageNameToFilename(Texture->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Texture->GetOutermost(), Texture, *File, Args);
}

void SetIcon(UBattleHUDButton* Button, UTexture2D* Texture)
{
    // Reuse the inherited image, sizing and selected/hover feedback implementation.
    Button->IconBrush.SetResourceObject(Texture);
    Button->IconBrush.ImageSize = FVector2D(18.f, 18.f);
    Button->IconBrush.DrawAs = ESlateBrushDrawType::Image;
    Button->IconBrush.TintColor = FLinearColor::White;
    Button->IconSize = FVector2D(18.f, 18.f);
    Button->ContentMode = EBasicButtonContent::IconAndText;
}
}

bool UpgradeBattleModeIcons()
{
    using namespace BattleModeAssets;
    const FString ButtonPath = Folder + TEXT("Components/WBP_BattleModeButton");
    const FString HUDPath = Folder + TEXT("WBP_BattleHUD");
    const FString MemberTexturePath = Folder + TEXT("Textures/T_ModeMember");
    const FString SquadTexturePath = Folder + TEXT("Textures/T_ModeSquad");
    const FString MemberFile = FPaths::ProjectContentDir() / TEXT("UI/Battle/Icons/ModeMember.png");
    const FString SquadFile = FPaths::ProjectContentDir() / TEXT("UI/Battle/Icons/ModeSquad.png");
    for (const FString& File : {MemberFile, SquadFile})
        if (!FPaths::FileExists(File))
        {
            UE_LOG(LogTemp, Error, TEXT("BATTLE_MODE_ICONS missing artwork: %s"), *File);
            return false;
        }

    auto* ButtonBP = LoadObject<UWidgetBlueprint>(nullptr, *ButtonPath);
    auto* HUD = LoadObject<UWidgetBlueprint>(nullptr, *HUDPath);
    if (!ButtonBP || !ButtonBP->WidgetTree || !ButtonBP->GeneratedClass || !HUD || !HUD->WidgetTree)
    {
        UE_LOG(LogTemp, Error, TEXT("BATTLE_MODE_ICONS requires the existing dedicated mode button and battle HUD"));
        return false;
    }
    auto* IconSize = Cast<USizeBox>(ButtonBP->WidgetTree->FindWidget(TEXT("IconSizeBox")));
    if (!IconSize) IconSize = Cast<USizeBox>(ButtonBP->WidgetTree->FindWidget(TEXT("ModeIconSize")));
    auto* Symbol = Cast<UBattleHUDVisual>(ButtonBP->WidgetTree->FindWidget(TEXT("Symbol")));
    auto* Image = Cast<UImage>(ButtonBP->WidgetTree->FindWidget(TEXT("IconImage")));
    auto* MemberButton = Cast<UBattleHUDButton>(HUD->WidgetTree->FindWidget(TEXT("MemberModeButton")));
    auto* SquadButton = Cast<UBattleHUDButton>(HUD->WidgetTree->FindWidget(TEXT("SquadModeButton")));
    if (!IconSize || (!Image && (!Symbol || Symbol->GetParent() != IconSize))
        || (Image && Image->GetParent() != IconSize)
        || !MemberButton || !SquadButton
        || MemberButton->GetClass() != ButtonBP->GeneratedClass || SquadButton->GetClass() != ButtonBP->GeneratedClass
        || !ButtonBP->GeneratedClass->IsChildOf(UBattleHUDButton::StaticClass())
        || (Symbol && BattleModeIcons::HasSymbolReferences(ButtonBP)))
    {
        UE_LOG(LogTemp, Error, TEXT("BATTLE_MODE_ICONS unexpected widget structure or custom Symbol references; no assets saved"));
        return false;
    }

    const FString BackupFolder = FPaths::ProjectSavedDir() / TEXT("BattleModeIcons/Backup")
        / (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
    for (const FString& Path : {ButtonPath, HUDPath, MemberTexturePath, SquadTexturePath})
        if (!Backup(Path, BackupFolder))
        {
            UE_LOG(LogTemp, Error, TEXT("BATTLE_MODE_ICONS could not back up %s"), *Path);
            return false;
        }
    auto* MemberTexture = BattleModeIcons::Import(MemberFile, MemberTexturePath);
    auto* SquadTexture = BattleModeIcons::Import(SquadFile, SquadTexturePath);
    if (!MemberTexture || !SquadTexture) return false;

    // Keep the content container, slot padding, label and outer button dimensions.
    // Only this dedicated button changes; shared battle glyph buttons retain Symbol.
    if (!Image)
    {
        if (!IconSize->RemoveChild(Symbol)
            || !Symbol->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional)) return false;
        ButtonBP->WidgetVariableNameToGuidMap.Remove(TEXT("Symbol"));
        Image = ButtonBP->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IconImage"));
        Image->bIsVariable = true;
        Image->SetVisibility(ESlateVisibility::HitTestInvisible);
        IconSize->AddChild(Image);
    }
    if (IconSize->GetFName() != TEXT("IconSizeBox")
        && !RenameWidget(ButtonBP, TEXT("ModeIconSize"), TEXT("IconSizeBox"))) return false;
    IconSize->bIsVariable = true;
    IconSize->SetWidthOverride(18.f);
    IconSize->SetHeightOverride(18.f);
    Image->SetBrushFromTexture(MemberTexture);
    Image->SetColorAndOpacity(MemberButton->ButtonForegroundColor);
    if (!Compile(ButtonBP)) return false;
    BattleModeIcons::SetIcon(CastChecked<UBattleHUDButton>(ButtonBP->GeneratedClass->GetDefaultObject()), MemberTexture);

    // Compilation may reinstance nested widgets; reacquire the two existing instances.
    MemberButton = Cast<UBattleHUDButton>(HUD->WidgetTree->FindWidget(TEXT("MemberModeButton")));
    SquadButton = Cast<UBattleHUDButton>(HUD->WidgetTree->FindWidget(TEXT("SquadModeButton")));
    if (!MemberButton || !SquadButton) return false;
    BattleModeIcons::SetIcon(MemberButton, MemberTexture);
    BattleModeIcons::SetIcon(SquadButton, SquadTexture);
    if (!Compile(HUD)) return false;
    if (!BattleModeIcons::SaveTexture(MemberTexture) || !BattleModeIcons::SaveTexture(SquadTexture)
        || !Save(ButtonBP) || !Save(HUD)) return false;
    UE_LOG(LogTemp, Display, TEXT("BATTLE_MODE_ICONS_UPGRADE_OK member=%s squad=%s iconSize=18x18; inherited image feedback; button slots and business graphs retained; backup=%s"),
        *MemberTexturePath, *SquadTexturePath, *BackupFolder);
    return true;
}
