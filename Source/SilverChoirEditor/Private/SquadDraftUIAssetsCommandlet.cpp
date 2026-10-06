#include "SquadDraftUIAssetsCommandlet.h"

#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Font.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"

USquadDraftUIAssetsCommandlet::USquadDraftUIAssetsCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

namespace SquadDraftUIAssets
{
constexpr const TCHAR* RoomPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/WBP_SquadMeetingRoom");
constexpr const TCHAR* ButtonPath = TEXT("/Game/System/UIBasic/WBP_ShellButton");

bool Check(bool bCondition, const FString& Detail)
{
    if (!bCondition) UE_LOG(LogTemp, Error, TEXT("SQUAD_DRAFT_UI_CHECK_FAILED %s"), *Detail);
    return bCondition;
}

FLinearColor Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }

template<class T> T* Find(UWidgetBlueprint* BP, const TCHAR* Name)
{
    T* Widget = BP && BP->WidgetTree ? Cast<T>(BP->WidgetTree->FindWidget(Name)) : nullptr;
    Check(Widget != nullptr, FString::Printf(TEXT("required widget missing or wrong type: %s"), Name));
    return Widget;
}

FString Placement(const UWidget* Widget)
{
    const auto* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
    if (!Slot) return TEXT("None");
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

struct FDesigner
{
    UWidgetTree* Tree;
    UClass* ButtonClass;

    template<class T> T* New(const TCHAR* Name)
    {
        return Tree->ConstructWidget<T>(T::StaticClass(), Name);
    }

    UCanvasPanel* Canvas(const TCHAR* Name)
    {
        auto* Result = New<UCanvasPanel>(Name);
        Result->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        return Result;
    }

    UCanvasPanelSlot* Place(UCanvasPanel* Parent, UWidget* Widget, FAnchors Anchors, FMargin Offsets, int32 Z = 0)
    {
        if (Widget->GetParent()) Widget->RemoveFromParent();
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

    UTextBlock* Text(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Value, int32 Size,
        const TCHAR* Hex, FAnchors Anchors, FMargin Offsets)
    {
        auto* Result = New<UTextBlock>(Name);
        Result->SetText(FText::FromString(Value));
        Result->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), Size, TEXT("Regular")));
        Result->SetColorAndOpacity(Color(Hex));
        Result->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(Parent, Result, Anchors, Offsets, 2);
        return Result;
    }

    UBasicButtonWidget* Button(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Caption, FAnchors Anchors, FMargin Offsets, int32 FontSize = 12)
    {
        auto* Result = Tree->ConstructWidget<UBasicButtonWidget>(ButtonClass, Name);
        Result->ButtonText = FText::FromString(Caption);
        Result->ButtonSubtitle = FText::GetEmpty();
        Result->ButtonIndex = FText::GetEmpty();
        Result->ContentMode = EBasicButtonContent::TextOnly;
        Result->MinimumSize = FVector2D::ZeroVector;
        Result->Font.Size = FontSize;
        Result->SetVisibility(ESlateVisibility::Visible);
        Place(Parent, Result, Anchors, Offsets, 2);
        return Result;
    }
};

struct FOriginalWidgets
{
    UCanvasPanel* Root = nullptr;
    UCanvasPanel* Management = nullptr;
    UScrollBox* Scroll = nullptr;
    UVerticalBox* Sections = nullptr;
    UCanvasPanel* Identity = nullptr;
    USizeBox* IdentitySize = nullptr;
    UImage* IconPlate = nullptr;
    UImage* IconImage = nullptr;
    UTextBlock* IdentityLabel = nullptr;
    UTextBlock* Location = nullptr;
    UEditableTextBox* Name = nullptr;
    UWrapBox* IconOptions = nullptr;
    UBasicButtonWidget* CaptainButton = nullptr;
    UBasicButtonWidget* LegacySave = nullptr;
    TArray<UWidget*> Hide;

    bool Read(UWidgetBlueprint* BP)
    {
        Root = Find<UCanvasPanel>(BP, TEXT("SquadRoomLayout"));
        Management = Find<UCanvasPanel>(BP, TEXT("ManagementPanel"));
        Scroll = Find<UScrollBox>(BP, TEXT("ManagementScroll"));
        Sections = Find<UVerticalBox>(BP, TEXT("ManagementSections"));
        Identity = Find<UCanvasPanel>(BP, TEXT("SquadIdentitySection"));
        IdentitySize = Find<USizeBox>(BP, TEXT("SquadIdentitySectionSize"));
        IconPlate = Find<UImage>(BP, TEXT("CurrentIconPlate"));
        IconImage = Find<UImage>(BP, TEXT("SquadIconImage"));
        IdentityLabel = Find<UTextBlock>(BP, TEXT("IdentityLabel"));
        Location = Find<UTextBlock>(BP, TEXT("SquadLocationText"));
        Name = Find<UEditableTextBox>(BP, TEXT("SquadNameInput"));
        IconOptions = Find<UWrapBox>(BP, TEXT("IconOptions"));
        CaptainButton = Find<UBasicButtonWidget>(BP, TEXT("UseCaptainPortraitButton"));
        LegacySave = Find<UBasicButtonWidget>(BP, TEXT("SaveSquadNameButton"));
        for (const TCHAR* NameToHide : {TEXT("SquadNameSectionSize"), TEXT("IconTitleSectionSize"), TEXT("CaptainPortraitSectionSize")})
            Hide.Add(Find<USizeBox>(BP, NameToHide));
        return Root && Management && Scroll && Sections && Identity && IdentitySize && IconPlate && IconImage && IdentityLabel && Location
            && Name && IconOptions && CaptainButton && LegacySave && !Hide.Contains(nullptr)
            && Check(Management->GetParent() == Root && Scroll->GetParent() == Management && Sections->GetParent() == Scroll
                && Identity->GetParent() == IdentitySize && IdentitySize->GetParent() == Sections,
                TEXT("unexpected management hierarchy; preserving the authored UI"));
    }
};

bool Validate(UWidgetBlueprint* BP, bool bRequirePatched)
{
    if (!Check(BP && BP->WidgetTree, TEXT("missing widget Blueprint or tree"))) return false;
    TSet<FName> WidgetNames;
    bool bUnique = true;
    BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (WidgetNames.Contains(Widget->GetFName())) bUnique = false;
        WidgetNames.Add(Widget->GetFName());
    });
    if (!Check(bUnique, TEXT("duplicate widget names"))) return false;
    for (const UWidgetAnimation* Animation : BP->Animations)
    {
        if (!Check(Animation && Animation->MovieScene, TEXT("missing animation or MovieScene"))) return false;
        for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
            if (!Binding.bIsRootWidget && !Check(WidgetNames.Contains(Binding.WidgetName), TEXT("unresolved animation widget: ") + Binding.WidgetName.ToString())) return false;
    }
    if (!bRequirePatched) return true;
    auto* Picker = Find<UCanvasPanel>(BP, TEXT("IconPickerPanel"));
    auto* Save = Find<UBasicButtonWidget>(BP, TEXT("SaveSquadButton"));
    auto* Choose = Find<UBasicButtonWidget>(BP, TEXT("ChooseSquadIconButton"));
    auto* Close = Find<UBasicButtonWidget>(BP, TEXT("CloseIconPickerButton"));
    auto* Status = Find<UTextBlock>(BP, TEXT("DraftStatusText"));
    auto* Name = Find<UEditableTextBox>(BP, TEXT("SquadNameInput"));
    auto* Identity = Find<UCanvasPanel>(BP, TEXT("SquadIdentitySection"));
    auto* Icon = Find<UImage>(BP, TEXT("SquadIconImage"));
    auto* Icons = Find<UWrapBox>(BP, TEXT("IconOptions"));
    auto* IconScroll = Find<UScrollBox>(BP, TEXT("IconPickerScroll"));
    auto* Captain = Find<UBasicButtonWidget>(BP, TEXT("UseCaptainPortraitButton"));
    auto* Card = Find<UCanvasPanel>(BP, TEXT("IconPickerCard"));
    auto* Legacy = Find<UBasicButtonWidget>(BP, TEXT("SaveSquadNameButton"));
    auto* Management = Find<UCanvasPanel>(BP, TEXT("ManagementPanel"));
    return Picker && Save && Choose && Close && Status && Name && Identity && Icon && Icons && IconScroll && Captain && Card && Legacy && Management
        && Check(Picker->GetVisibility() == ESlateVisibility::Collapsed, TEXT("icon picker must start collapsed"))
        && Check(Save->GetParent() == Management && Status->GetParent() == Management, TEXT("save controls must be fixed outside the management scroll"))
        && Check(Name->GetParent() == Identity && Choose->GetParent() == Identity && Icon->GetParent() == Identity, TEXT("name and clickable emblem must share the top identity section"))
        && Check(Icons->GetParent() == IconScroll && Captain->GetParent() == Card && Close->GetParent() == Card, TEXT("icon choices and captain option must belong to popup"))
        && Check(Icon->GetVisibility() == ESlateVisibility::HitTestInvisible, TEXT("emblem image must not intercept its button"))
        && Check(Legacy->GetVisibility() == ESlateVisibility::Collapsed, TEXT("legacy save-name action must not remain visible"));
}

void Inspect(UWidgetBlueprint* BP)
{
    UE_LOG(LogTemp, Display, TEXT("SQUAD_DRAFT_UI_ASSET %s status=%d"), *BP->GetPathName(), int32(BP->Status));
    BP->WidgetTree->ForEachWidget([](UWidget* Widget)
    {
        UE_LOG(LogTemp, Display, TEXT("SQUAD_DRAFT_UI_WIDGET %s class=%s parent=%s visibility=%d placement=%s"),
            *Widget->GetName(), *Widget->GetClass()->GetName(), *GetNameSafe(Widget->GetParent()), int32(Widget->GetVisibility()), *Placement(Widget));
    });
    for (const UWidgetAnimation* Animation : BP->Animations)
        if (Animation)
        {
            UE_LOG(LogTemp, Display, TEXT("SQUAD_DRAFT_UI_ANIMATION %s start=%g end=%g bindings=%d"), *Animation->GetName(), Animation->GetStartTime(), Animation->GetEndTime(), Animation->AnimationBindings.Num());
            for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
                UE_LOG(LogTemp, Display, TEXT("SQUAD_DRAFT_UI_BINDING %s widget=%s guid=%s"), *Animation->GetName(), *Binding.WidgetName.ToString(), *Binding.AnimationGuid.ToString());
        }
}

bool Build(UWidgetBlueprint* BP)
{
    if (!Validate(BP, false)) return false;
    for (const TCHAR* Name : {TEXT("IconPickerPanel"), TEXT("SaveSquadButton"), TEXT("ChooseSquadIconButton"), TEXT("CloseIconPickerButton"), TEXT("DraftStatusText")})
        if (!Check(!BP->WidgetTree->FindWidget(Name), FString::Printf(TEXT("%s already exists; use -Inspect rather than applying this patch twice"), Name))) return false;
    FOriginalWidgets W;
    if (!W.Read(BP)) return false;
    auto* ButtonBP = LoadObject<UWidgetBlueprint>(nullptr, ButtonPath);
    if (!Check(ButtonBP && ButtonBP->GeneratedClass && ButtonBP->GeneratedClass->IsChildOf(UBasicButtonWidget::StaticClass()), TEXT("shell button class unavailable"))) return false;

    const FString GraphBefore = GraphFingerprint(BP);
    const FString AnimationsBefore = AnimationFingerprint(BP);
    TMap<FName, FString> PreservedPanels;
    for (const TCHAR* Name : {TEXT("LeftPanel"), TEXT("ManagementPanel"), TEXT("MemberStripPanel"), TEXT("SelectionPrompt")})
    {
        const UWidget* Panel = Find<UWidget>(BP, Name);
        if (!Panel) return false;
        PreservedPanels.Add(Name, Placement(Panel));
    }

    const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    const FString Backup = FPaths::ProjectSavedDir() / TEXT("SquadDraftUIBackups") /
        (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)) / FPaths::GetCleanFilename(Filename);
    if (!Check(IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true), TEXT("cannot create backup directory"))
        || !Check(IFileManager::Get().Copy(*Backup, *Filename) == COPY_OK, TEXT("cannot back up original asset"))) return false;
    UE_LOG(LogTemp, Display, TEXT("SQUAD_DRAFT_UI_BACKUP %s"), *Backup);

    FDesigner D{BP->WidgetTree, ButtonBP->GeneratedClass.Get()};
    // Preserve the animated ManagementPanel. Its body scrolls above a fixed save footer.
    auto* ScrollSlot = Cast<UCanvasPanelSlot>(W.Scroll->Slot);
    if (!Check(ScrollSlot != nullptr, TEXT("ManagementScroll requires its existing canvas slot"))) return false;
    ScrollSlot->SetAnchors(FAnchors(0, 0, 1, 1));
    ScrollSlot->SetOffsets(FMargin(18, 88, 10, 104));
    W.Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
    W.Scroll->SetClipping(EWidgetClipping::ClipToBounds);

    W.IdentitySize->SetHeightOverride(166);
    D.Place(W.Identity, W.IconPlate, FAnchors(0, 0), FMargin(0, 0, 128, 128));
    W.IconPlate->SetColorAndOpacity(Color(TEXT("0C202E")));
    auto* Choose = D.Button(W.Identity, TEXT("ChooseSquadIconButton"), TEXT(""), FAnchors(0, 0), FMargin(0, 0, 128, 128));
    Choose->BackgroundColor = Color(TEXT("0C202E"));
    Choose->BorderColor = Color(TEXT("285C75"));
    Choose->AccentColor = Color(TEXT("45BDE7"));
    Choose->SetToolTipText(FText::FromString(TEXT("更换小队标识")));
    D.Place(W.Identity, W.IconImage, FAnchors(0, 0), FMargin(8, 8, 112, 112), 3);
    W.IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    D.Plate(W.Identity, TEXT("SquadEmblemAccent"), TEXT("329ACA"), FAnchors(0, 0), FMargin(0, 127, 128, 1), 4);
    auto* EmblemHint = D.Text(W.Identity, TEXT("SquadEmblemHint"), TEXT("点击更换队徽"), 9, TEXT("638EA5"), FAnchors(0, 0), FMargin(0, 138, 128, 20));
    EmblemHint->SetJustification(ETextJustify::Center);

    W.IdentityLabel->SetText(FText::FromString(TEXT("小队名称")));
    FSlateFontInfo CaptionFont = W.IdentityLabel->GetFont();
    CaptionFont.Size = 10;
    W.IdentityLabel->SetFont(CaptionFont);
    W.IdentityLabel->SetColorAndOpacity(Color(TEXT("7798AC")));
    D.Place(W.Identity, W.IdentityLabel, FAnchors(0, 0, 1, 0), FMargin(144, 5, 0, 22));
    D.Place(W.Identity, W.Name, FAnchors(0, 0, 1, 0), FMargin(144, 34, 0, 38));
    FEditableTextBoxStyle InputStyle = W.Name->GetWidgetStyle();
    InputStyle.SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), 14, TEXT("Regular")));
    InputStyle.SetForegroundColor(FSlateColor(Color(TEXT("DCE9F5"))));
    InputStyle.SetBackgroundColor(FSlateColor(Color(TEXT("0B1B28"))));
    InputStyle.SetPadding(FMargin(9, 6));
    W.Name->SetWidgetStyle(InputStyle);
    W.Name->SetHintText(FText::FromString(TEXT("输入小队名称")));
    W.Location->SetAutoWrapText(true);
    FSlateFontInfo LocationFont = W.Location->GetFont();
    LocationFont.Size = 11;
    W.Location->SetFont(LocationFont);
    D.Place(W.Identity, W.Location, FAnchors(0, 0, 1, 0), FMargin(144, 91, 0, 50));

    // Keep old named widgets for compatibility, but remove their visual footprint.
    W.LegacySave->SetVisibility(ESlateVisibility::Collapsed);
    for (UWidget* Section : W.Hide) Section->SetVisibility(ESlateVisibility::Collapsed);

    D.Plate(W.Management, TEXT("DraftFooterPlate"), TEXT("06111BFF"), FAnchors(0, 1, 1, 1), FMargin(0, -100, 0, 100), 1);
    D.Plate(W.Management, TEXT("DraftFooterRule"), TEXT("234355"), FAnchors(0, 1, 1, 1), FMargin(18, -100, 18, 1), 2);
    D.Text(W.Management, TEXT("DraftStatusText"), TEXT("修改将在保存后生效"), 9, TEXT("7798AC"), FAnchors(0, 1, 1, 1), FMargin(18, -82, 18, 20));
    auto* Save = D.Button(W.Management, TEXT("SaveSquadButton"), TEXT("保存小队"), FAnchors(0, 1, 1, 1), FMargin(18, -54, 18, 40), 13);
    Save->BackgroundColor = Color(TEXT("0D2636"));
    Save->BorderColor = Color(TEXT("3689AA"));
    Save->AccentColor = Color(TEXT("45BDE7"));
    Save->ButtonForegroundColor = Color(TEXT("DCE9F5"));

    auto* Picker = D.Canvas(TEXT("IconPickerPanel"));
    D.Place(W.Root, Picker, FAnchors(0, 0, 1, 1), FMargin(0), 100);
    auto* Scrim = D.Plate(Picker, TEXT("IconPickerScrim"), TEXT("01070DDC"), FAnchors(0, 0, 1, 1), FMargin(0));
    // A hit-testable scrim blocks the room controls behind the modal card.
    Scrim->SetVisibility(ESlateVisibility::Visible);
    auto* Fit = D.New<UScaleBox>(TEXT("IconPickerFit"));
    Fit->SetStretch(EStretch::ScaleToFit);
    Fit->SetStretchDirection(EStretchDirection::DownOnly);
    Fit->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    D.Place(Picker, Fit, FAnchors(0, 0, 1, 1), FMargin(24), 1);
    auto* Extent = D.New<USizeBox>(TEXT("IconPickerExtent"));
    Extent->SetWidthOverride(440);
    Extent->SetHeightOverride(330);
    Extent->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    auto* FitSlot = Cast<UScaleBoxSlot>(Fit->AddChild(Extent));
    if (!Check(FitSlot != nullptr, TEXT("could not construct popup scale slot"))) return false;
    FitSlot->SetHorizontalAlignment(HAlign_Center);
    FitSlot->SetVerticalAlignment(VAlign_Center);
    auto* Card = D.Canvas(TEXT("IconPickerCard"));
    Extent->AddChild(Card);
    D.Plate(Card, TEXT("IconPickerBackground"), TEXT("071521FF"), FAnchors(0, 0, 1, 1), FMargin(0));
    D.Plate(Card, TEXT("IconPickerTopRule"), TEXT("45BDE7"), FAnchors(0, 0, 1, 0), FMargin(0, 0, 0, 2));
    D.Plate(Card, TEXT("IconPickerBottomRule"), TEXT("285C75"), FAnchors(0, 1, 1, 1), FMargin(0, -1, 0, 1));
    D.Text(Card, TEXT("IconPickerTitle"), TEXT("选择小队标识"), 20, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(24, 22, 70, 34));
    D.Text(Card, TEXT("IconPickerCaption"), TEXT("SQUAD EMBLEM / 预设标识"), 9, TEXT("638EA5"), FAnchors(0, 0, 1, 0), FMargin(24, 62, 24, 22));
    D.Button(Card, TEXT("CloseIconPickerButton"), TEXT("×"), FAnchors(1, 0, 1, 0), FMargin(-54, 20, 32, 32), 18);
    D.Plate(Card, TEXT("IconPickerDivider"), TEXT("234355"), FAnchors(0, 0, 1, 0), FMargin(24, 92, 24, 1));
    auto* IconScroll = D.New<UScrollBox>(TEXT("IconPickerScroll"));
    IconScroll->SetScrollbarThickness(FVector2D(3));
    IconScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
    D.Place(Card, IconScroll, FAnchors(0, 0, 1, 1), FMargin(24, 110, 24, 114));
    W.IconOptions->RemoveFromParent();
    W.IconOptions->SetInnerSlotPadding(FVector2D(12));
    W.IconOptions->SetHorizontalAlignment(HAlign_Center);
    W.IconOptions->SetExplicitWrapSize(false);
    auto* IconsSlot = Cast<UScrollBoxSlot>(IconScroll->AddChild(W.IconOptions));
    if (!Check(IconsSlot != nullptr, TEXT("could not move existing icon options into popup"))) return false;
    IconsSlot->SetHorizontalAlignment(HAlign_Fill);
    IconsSlot->SetPadding(FMargin(0, 4, 4, 4));
    D.Place(Card, W.CaptainButton, FAnchors(0, 1, 1, 1), FMargin(24, -94, 24, 40), 2);
    W.CaptainButton->ButtonText = FText::FromString(TEXT("使用队长头像作为标识"));
    W.CaptainButton->Font.Size = 12;
    D.Text(Card, TEXT("IconPickerHint"), TEXT("选择完成后，点击「保存小队」应用修改"), 9, TEXT("638EA5"), FAnchors(0, 1, 1, 1), FMargin(24, -36, 24, 20))->SetJustification(ETextJustify::Center);
    Picker->SetVisibility(ESlateVisibility::Collapsed);

    if (!Validate(BP, true)) return false;
    for (const auto& Pair : PreservedPanels)
        if (!Check(Pair.Value == Placement(BP->WidgetTree->FindWidget(Pair.Key)), TEXT("changed protected panel placement: ") + Pair.Key.ToString())) return false;
    if (!Check(GraphBefore == GraphFingerprint(BP), TEXT("existing Blueprint graph changed"))
        || !Check(AnimationsBefore == AnimationFingerprint(BP), TEXT("existing animations or bindings changed"))) return false;
    BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
            BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
    });
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
    if (!Check(Results.NumErrors == 0 && BP->Status != BS_Error, TEXT("modified widget Blueprint did not compile"))
        || !Check(GraphBefore == GraphFingerprint(BP), TEXT("compiler changed existing Blueprint graph"))
        || !Check(AnimationsBefore == AnimationFingerprint(BP), TEXT("compiler changed existing animation bindings"))
        || !Validate(BP, true)) return false;
    BP->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    if (!Check(UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args), TEXT("could not save squad UI"))) return false;
    Inspect(BP);
    return true;
}
}

int32 USquadDraftUIAssetsCommandlet::Main(const FString& Params)
{
    auto* BP = LoadObject<UWidgetBlueprint>(nullptr, SquadDraftUIAssets::RoomPath);
    if (!SquadDraftUIAssets::Check(BP && BP->WidgetTree && BP->GeneratedClass, TEXT("squad room Blueprint unavailable"))) return 1;
    if (FParse::Param(*Params, TEXT("Inspect")))
    {
        SquadDraftUIAssets::Inspect(BP);
        const bool bPatched = BP->WidgetTree->FindWidget(TEXT("IconPickerPanel")) != nullptr;
        const bool bValid = SquadDraftUIAssets::Validate(BP, bPatched);
        UE_LOG(LogTemp, Display, TEXT("SQUAD_DRAFT_UI_INSPECT_%s patched=%d"), bValid ? TEXT("OK") : TEXT("FAILED"), bPatched);
        return bValid ? 0 : 1;
    }
    if (!FParse::Param(*Params, TEXT("Build")))
    {
        UE_LOG(LogTemp, Error, TEXT("Use -run=SquadDraftUIAssets -Inspect or -Build."));
        return 1;
    }
    const bool bSuccess = SquadDraftUIAssets::Build(BP);
    UE_LOG(LogTemp, Display, TEXT("SQUAD_DRAFT_UI_%s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));
    return bSuccess ? 0 : 1;
}
