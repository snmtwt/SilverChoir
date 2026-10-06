#include "SquadCompactUIAssetsCommandlet.h"

#include "Animation/WidgetAnimation.h"
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
#include "Components/WrapBox.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
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

USquadCompactUIAssetsCommandlet::USquadCompactUIAssetsCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

namespace SquadCompactUIAssets
{
constexpr const TCHAR* RoomPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/WBP_SquadMeetingRoom");
constexpr const TCHAR* MemberPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/Components/WBP_SquadMemberCard");
constexpr float CardWidth = 122.f;
constexpr float CardHeight = 204.f;
constexpr float PickerWidth = 280.f;
constexpr float PickerHeight = 300.f;

bool Check(bool bCondition, const FString& Detail)
{
    if (!bCondition) UE_LOG(LogTemp, Error, TEXT("SQUAD_COMPACT_UI_CHECK_FAILED %s"), *Detail);
    return bCondition;
}

FLinearColor Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }

template<class T> T* Find(UWidgetBlueprint* BP, const TCHAR* Name)
{
    T* Result = BP && BP->WidgetTree ? Cast<T>(BP->WidgetTree->FindWidget(Name)) : nullptr;
    Check(Result != nullptr, FString::Printf(TEXT("%s: required widget missing or wrong type: %s"), *GetNameSafe(BP), Name));
    return Result;
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

// Change the existing canvas slot in place: widget names and animation slot references remain stable.
bool Place(UWidget* Widget, FAnchors Anchors, FMargin Offsets, int32 Z = 0)
{
    auto* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
    if (!Check(Slot != nullptr, TEXT("required canvas slot: ") + GetNameSafe(Widget))) return false;
    Slot->SetAnchors(Anchors);
    Slot->SetOffsets(Offsets);
    Slot->SetAlignment(FVector2D::ZeroVector);
    Slot->SetAutoSize(false);
    Slot->SetZOrder(Z);
    return true;
}

void FontSize(UTextBlock* Text, int32 Size)
{
    FSlateFontInfo Font = Text->GetFont();
    Font.Size = Size;
    Text->SetFont(Font);
}

struct FRoomWidgets
{
    UCanvasPanel* Root = nullptr;
    UCanvasPanel* Picker = nullptr;
    UImage* Scrim = nullptr;
    UScaleBox* Fit = nullptr;
    USizeBox* Extent = nullptr;
    UCanvasPanel* Card = nullptr;
    UImage* Background = nullptr;
    UTextBlock* Title = nullptr;
    UTextBlock* Caption = nullptr;
    UImage* Divider = nullptr;
    UScrollBox* Scroll = nullptr;
    UWrapBox* Options = nullptr;
    UBasicButtonWidget* Close = nullptr;
    UBasicButtonWidget* Captain = nullptr;
    UTextBlock* Hint = nullptr;
    UCanvasPanel* MemberStrip = nullptr;
    UScrollBox* MemberList = nullptr;

    bool Read(UWidgetBlueprint* BP)
    {
        Root = Find<UCanvasPanel>(BP, TEXT("SquadRoomLayout"));
        Picker = Find<UCanvasPanel>(BP, TEXT("IconPickerPanel"));
        Scrim = Find<UImage>(BP, TEXT("IconPickerScrim"));
        Fit = Find<UScaleBox>(BP, TEXT("IconPickerFit"));
        Extent = Find<USizeBox>(BP, TEXT("IconPickerExtent"));
        Card = Find<UCanvasPanel>(BP, TEXT("IconPickerCard"));
        Background = Find<UImage>(BP, TEXT("IconPickerBackground"));
        Title = Find<UTextBlock>(BP, TEXT("IconPickerTitle"));
        Caption = Find<UTextBlock>(BP, TEXT("IconPickerCaption"));
        Divider = Find<UImage>(BP, TEXT("IconPickerDivider"));
        Scroll = Find<UScrollBox>(BP, TEXT("IconPickerScroll"));
        Options = Find<UWrapBox>(BP, TEXT("IconOptions"));
        Close = Find<UBasicButtonWidget>(BP, TEXT("CloseIconPickerButton"));
        Captain = Find<UBasicButtonWidget>(BP, TEXT("UseCaptainPortraitButton"));
        Hint = Find<UTextBlock>(BP, TEXT("IconPickerHint"));
        MemberStrip = Find<UCanvasPanel>(BP, TEXT("MemberStripPanel"));
        MemberList = Find<UScrollBox>(BP, TEXT("MemberList"));
        return Root && Picker && Scrim && Fit && Extent && Card && Background && Title && Caption && Divider && Scroll
            && Options && Close && Captain && Hint && MemberStrip && MemberList
            && Check(Picker->GetParent() == Root && Fit->GetParent() == Picker && Extent->GetParent() == Fit
                && Card->GetParent() == Extent && Options->GetParent() == Scroll && Scroll->GetParent() == Card
                && Close->GetParent() == Card && Captain->GetParent() == Card && MemberList->GetParent() == MemberStrip,
                TEXT("unexpected picker/member-strip hierarchy; refusing a destructive rebuild"));
    }
};

struct FMemberWidgets
{
    USizeBox* Size = nullptr;
    UCanvasPanel* Layout = nullptr;
    UImage* Plate = nullptr;
    UImage* TopRule = nullptr;
    UWidget* Portrait = nullptr;
    UTextBlock* Name = nullptr;
    UTextBlock* Captain = nullptr;
    UBasicButtonWidget* Promote = nullptr;
    UBasicButtonWidget* Remove = nullptr;

    bool Read(UWidgetBlueprint* BP)
    {
        Size = Find<USizeBox>(BP, TEXT("MemberCardSize"));
        Layout = Find<UCanvasPanel>(BP, TEXT("MemberCardLayout"));
        Plate = Find<UImage>(BP, TEXT("MemberPlate"));
        TopRule = Find<UImage>(BP, TEXT("MemberTopRule"));
        Portrait = Find<UWidget>(BP, TEXT("MemberPortrait"));
        Name = Find<UTextBlock>(BP, TEXT("MemberName"));
        Captain = Find<UTextBlock>(BP, TEXT("CaptainLabel"));
        Promote = Find<UBasicButtonWidget>(BP, TEXT("PromoteButton"));
        Remove = Find<UBasicButtonWidget>(BP, TEXT("RemoveButton"));
        return Size && Layout && Plate && TopRule && Portrait && Name && Captain && Promote && Remove
            && Check(Size == BP->WidgetTree->RootWidget && Layout->GetParent() == Size && Name->GetParent() == Layout
                && Portrait->GetParent() == Layout && Captain->GetParent() == Layout && Promote->GetParent() == Layout
                && Remove->GetParent() == Layout, TEXT("unexpected member-card hierarchy; preserving the authored widget"));
    }
};

bool PatchRoom(FRoomWidgets& W)
{
    W.Picker->SetVisibility(ESlateVisibility::Collapsed);
    W.Scrim->SetVisibility(ESlateVisibility::Collapsed);
    W.Fit->SetStretch(EStretch::ScaleToFit);
    W.Fit->SetStretchDirection(EStretchDirection::DownOnly);
    W.Fit->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    W.Extent->SetWidthOverride(PickerWidth);
    W.Extent->SetHeightOverride(PickerHeight);
    W.Extent->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    W.Card->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    // Only the card catches empty-area clicks. The full-screen overlay never blocks room controls.
    W.Background->SetVisibility(ESlateVisibility::Visible);
    auto* FitSlot = Cast<UScaleBoxSlot>(W.Extent->Slot);
    auto* OptionSlot = Cast<UScrollBoxSlot>(W.Options->Slot);
    auto* StripSlot = Cast<UCanvasPanelSlot>(W.MemberStrip->Slot);
    if (!Check(FitSlot && OptionSlot && StripSlot, TEXT("required scale/scroll/member-strip slots missing"))) return false;
    FitSlot->SetHorizontalAlignment(HAlign_Fill);
    FitSlot->SetVerticalAlignment(VAlign_Fill);
    if (!Place(W.Picker, FAnchors(0, 0, 1, 1), FMargin(0), 100)
        || !Place(W.Fit, FAnchors(0, 0), FMargin(0, 0, PickerWidth, PickerHeight), 1)
        || !Place(W.Title, FAnchors(0, 0, 1, 0), FMargin(14, 13, 50, 28), 2)
        || !Place(W.Caption, FAnchors(0, 0, 1, 0), FMargin(14, 42, 14, 18), 2)
        || !Place(W.Close, FAnchors(1, 0), FMargin(-40, 12, 26, 26), 2)
        || !Place(W.Divider, FAnchors(0, 0, 1, 0), FMargin(14, 65, 14, 1))
        || !Place(W.Scroll, FAnchors(0, 0), FMargin(65, 77, 150, 144))
        || !Place(W.Captain, FAnchors(0, 1, 1, 1), FMargin(14, -66, 14, 34), 2)
        || !Place(W.Hint, FAnchors(0, 1, 1, 1), FMargin(10, -24, 10, 18), 2)) return false;
    FontSize(W.Title, 16);
    FontSize(W.Caption, 9);
    W.Caption->SetText(FText::FromString(TEXT("SQUAD EMBLEM / 预设队徽")));
    W.Close->MinimumSize = FVector2D::ZeroVector;
    W.Close->Font.Size = 16;
    // The generic button's text padding exceeds this small hit target. Center a decorative glyph
    // over it instead, retaining the existing button, sound, input behavior and delegate binding.
    W.Close->ButtonText = FText::GetEmpty();
    UWidgetTree* Tree = W.Card->GetTypedOuter<UWidgetTree>();
    UTextBlock* CloseGlyph = Cast<UTextBlock>(Tree->FindWidget(TEXT("IconPickerCloseGlyph")));
    if (!CloseGlyph)
    {
        CloseGlyph = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("IconPickerCloseGlyph"));
        W.Card->AddChildToCanvas(CloseGlyph);
    }
    CloseGlyph->SetText(FText::FromString(TEXT("×")));
    CloseGlyph->SetFont(W.Close->Font);
    CloseGlyph->SetColorAndOpacity(W.Close->ButtonForegroundColor);
    CloseGlyph->SetVisibility(ESlateVisibility::HitTestInvisible);
    if (!Place(CloseGlyph, FAnchors(1, 0), FMargin(-27, 25, 0, 0), 3)) return false;
    auto* GlyphSlot = CastChecked<UCanvasPanelSlot>(CloseGlyph->Slot);
    GlyphSlot->SetAlignment(FVector2D(0.5, 0.5));
    GlyphSlot->SetAutoSize(true);
    W.Options->SetInnerSlotPadding(FVector2D(10));
    W.Options->SetHorizontalAlignment(HAlign_Center);
    W.Options->SetExplicitWrapSize(true);
    W.Options->SetWrapSize(140);
    OptionSlot->SetHorizontalAlignment(HAlign_Center);
    OptionSlot->SetPadding(FMargin(0));
    W.Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
    W.Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
    W.Captain->MinimumSize = FVector2D::ZeroVector;
    W.Captain->Font.Size = 11;
    W.Captain->ButtonText = FText::FromString(TEXT("使用队长头像"));
    FontSize(W.Hint, 8);
    W.Hint->SetText(FText::FromString(TEXT("保存小队后应用所选队徽")));
    W.Hint->SetJustification(ETextJustify::Center);

    // The compact card is taller than the old card. Retain the same bottom anchors and horizontal layout.
    const FAnchors StripAnchors = StripSlot->GetAnchors();
    if (!Check(FMath::IsNearlyEqual(StripAnchors.Minimum.Y, 1.f) && FMath::IsNearlyEqual(StripAnchors.Maximum.Y, 1.f),
        TEXT("member strip is no longer bottom-anchored"))) return false;
    FMargin StripOffsets = StripSlot->GetOffsets();
    const float NewHeight = FMath::Max(284.f, StripOffsets.Bottom);
    StripOffsets.Top -= NewHeight - StripOffsets.Bottom;
    StripOffsets.Bottom = NewHeight;
    StripSlot->SetOffsets(StripOffsets);
    return true;
}

bool PatchMember(UWidgetBlueprint* BP, FMemberWidgets& W)
{
    W.Size->SetWidthOverride(CardWidth);
    W.Size->SetHeightOverride(CardHeight);
    W.Name->SetAutoWrapText(false);
    W.Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    W.Name->SetJustification(ETextJustify::Center);
    W.Name->SetClipping(EWidgetClipping::ClipToBounds);
    W.Name->SetVisibility(ESlateVisibility::Visible);
    W.Name->SetToolTipText(W.Name->GetText());
    FontSize(W.Name, 12);
    FontSize(W.Captain, 10);
    W.Captain->SetAutoWrapText(false);
    W.Captain->SetJustification(ETextJustify::Center);
    W.Captain->SetColorAndOpacity(Color(TEXT("B69B61")));
    W.Portrait->SetVisibility(ESlateVisibility::HitTestInvisible);
    for (UBasicButtonWidget* Button : {W.Promote, W.Remove})
    {
        Button->MinimumSize = FVector2D::ZeroVector;
        Button->Font.Size = 10;
    }
    if (!Place(W.Name, FAnchors(0, 0, 1, 0), FMargin(8, 10, 8, 24), 2)
        || !Place(W.Portrait, FAnchors(0, 0), FMargin(10, 44, 52, 52), 1)
        || !Place(W.Captain, FAnchors(0, 0, 1, 0), FMargin(67, 59, 6, 24), 2)
        || !Place(W.Promote, FAnchors(0, 0, 1, 0), FMargin(8, 116, 8, 32), 2)
        || !Place(W.Remove, FAnchors(0, 0, 1, 0), FMargin(8, 156, 8, 32), 2)) return false;
    UWidget* ExistingAccent = BP->WidgetTree->FindWidget(TEXT("CaptainAccent"));
    auto* Accent = Cast<UImage>(ExistingAccent);
    if (!Check(!ExistingAccent || (Accent && Accent->GetParent() == W.Layout), TEXT("CaptainAccent exists with incompatible type or parent"))) return false;
    if (!Accent)
    {
        Accent = BP->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CaptainAccent"));
        W.Layout->AddChildToCanvas(Accent);
    }
    Accent->SetColorAndOpacity(Color(TEXT("B69B61")));
    Accent->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Place(Accent, FAnchors(0, 0, 1, 0), FMargin(0, 0, 0, 2), 3);
}

bool ValidateCompact(UWidgetBlueprint* Room, UWidgetBlueprint* Member)
{
    FRoomWidgets W;
    FMemberWidgets M;
    if (!ValidateTree(Room) || !ValidateTree(Member) || !W.Read(Room) || !M.Read(Member)) return false;
    const auto* FitSlot = Cast<UCanvasPanelSlot>(W.Fit->Slot);
    auto* Accent = Find<UImage>(Member, TEXT("CaptainAccent"));
    return FitSlot && Accent
        && Check(W.Picker->GetVisibility() == ESlateVisibility::Collapsed && W.Scrim->GetVisibility() == ESlateVisibility::Collapsed,
            TEXT("picker and legacy scrim must be collapsed by default"))
        && Check(FitSlot->GetAnchors().Minimum == FVector2D::ZeroVector && FitSlot->GetAnchors().Maximum == FVector2D::ZeroVector
            && FMath::IsNearlyEqual(FitSlot->GetSize().X, PickerWidth) && FMath::IsNearlyEqual(FitSlot->GetSize().Y, PickerHeight), TEXT("picker must use a fixed top-left canvas slot"))
        && Check(FMath::IsNearlyEqual(W.Extent->GetWidthOverride(), PickerWidth) && FMath::IsNearlyEqual(W.Extent->GetHeightOverride(), PickerHeight), TEXT("unexpected picker extent"))
        && Check(FMath::IsNearlyEqual(M.Size->GetWidthOverride(), CardWidth) && FMath::IsNearlyEqual(M.Size->GetHeightOverride(), CardHeight), TEXT("unexpected member card extent"))
        && Check(Accent->GetParent() == M.Layout, TEXT("captain accent is not part of the member card"));
}

void Inspect(UWidgetBlueprint* BP)
{
    UE_LOG(LogTemp, Display, TEXT("SQUAD_COMPACT_UI_ASSET %s status=%d"), *BP->GetPathName(), int32(BP->Status));
    BP->WidgetTree->ForEachWidget([](UWidget* Widget)
    {
        UE_LOG(LogTemp, Display, TEXT("SQUAD_COMPACT_UI_WIDGET %s class=%s parent=%s visibility=%d placement=%s"),
            *Widget->GetName(), *Widget->GetClass()->GetName(), *GetNameSafe(Widget->GetParent()), int32(Widget->GetVisibility()), *Placement(Widget));
        if (const auto* Size = Cast<USizeBox>(Widget))
            UE_LOG(LogTemp, Display, TEXT("SQUAD_COMPACT_UI_SIZE %s width=%g height=%g"), *Size->GetName(), Size->GetWidthOverride(), Size->GetHeightOverride());
        if (const auto* Text = Cast<UTextBlock>(Widget))
            UE_LOG(LogTemp, Display, TEXT("SQUAD_COMPACT_UI_TEXT %s font=%.1f value=%s"), *Text->GetName(), Text->GetFont().Size, *Text->GetText().ToString());
    });
    for (const UWidgetAnimation* Animation : BP->Animations)
    {
        if (!Animation) continue;
        UE_LOG(LogTemp, Display, TEXT("SQUAD_COMPACT_UI_ANIMATION %s start=%g end=%g bindings=%d"), *Animation->GetName(), Animation->GetStartTime(), Animation->GetEndTime(), Animation->AnimationBindings.Num());
        for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
            UE_LOG(LogTemp, Display, TEXT("SQUAD_COMPACT_UI_BINDING %s widget=%s guid=%s"), *Animation->GetName(), *Binding.WidgetName.ToString(), *Binding.AnimationGuid.ToString());
    }
}

struct FAssetSnapshot
{
    UWidgetBlueprint* BP;
    FString Graph;
    FString Animations;
    FString Filename;

    explicit FAssetSnapshot(UWidgetBlueprint* InBP) : BP(InBP), Graph(GraphFingerprint(InBP)), Animations(AnimationFingerprint(InBP)),
        Filename(FPackageName::LongPackageNameToFilename(InBP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension())) {}

    bool Backup(const FString& Directory) const
    {
        const FString BackupFilename = Directory / FPaths::GetCleanFilename(Filename);
        if (!Check(IFileManager::Get().Copy(*BackupFilename, *Filename) == COPY_OK, TEXT("cannot back up asset: ") + Filename)) return false;
        UE_LOG(LogTemp, Display, TEXT("SQUAD_COMPACT_UI_BACKUP %s"), *BackupFilename);
        return true;
    }

    bool Preserved() const
    {
        return Check(Graph == GraphFingerprint(BP), TEXT("existing Blueprint graph changed: ") + BP->GetName())
            && Check(Animations == AnimationFingerprint(BP), TEXT("existing animation bindings changed: ") + BP->GetName());
    }

    bool Compile() const
    {
        if (!Preserved()) return false;
        BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            if (!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
                BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
        });
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        FCompilerResultsLog Results;
        FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
        return Check(Results.NumErrors == 0 && BP->Status != BS_Error, TEXT("modified widget Blueprint did not compile: ") + BP->GetName()) && Preserved();
    }

    bool Save() const
    {
        BP->MarkPackageDirty();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        return Check(UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args), TEXT("cannot save asset: ") + Filename);
    }
};

bool Build(UWidgetBlueprint* Room, UWidgetBlueprint* Member)
{
    FRoomWidgets W;
    FMemberWidgets M;
    if (!ValidateTree(Room) || !ValidateTree(Member) || !W.Read(Room) || !M.Read(Member)) return false;
    const FAssetSnapshot RoomSnapshot(Room), MemberSnapshot(Member);
    TMap<FName, FString> ProtectedRoomWidgets;
    for (const TCHAR* Name : {TEXT("LeftPanel"), TEXT("ManagementPanel"), TEXT("SelectionPrompt"), TEXT("VehicleSection"),
        TEXT("SquadIdentitySection"), TEXT("SaveSquadButton"), TEXT("MemberList")})
    {
        const UWidget* Widget = Find<UWidget>(Room, Name);
        if (!Widget) return false;
        ProtectedRoomWidgets.Add(Name, Placement(Widget));
    }
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("SquadCompactUIBackups") /
        (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if (!Check(IFileManager::Get().MakeDirectory(*Directory, true), TEXT("cannot create backup directory"))
        || !RoomSnapshot.Backup(Directory) || !MemberSnapshot.Backup(Directory)) return false;
    if (!PatchRoom(W) || !PatchMember(Member, M) || !ValidateCompact(Room, Member)) return false;
    for (const auto& Pair : ProtectedRoomWidgets)
        if (!Check(Pair.Value == Placement(Room->WidgetTree->FindWidget(Pair.Key)), TEXT("changed protected room widget: ") + Pair.Key.ToString())) return false;
    // Compile and validate both before writing either asset; each original file is backed up above.
    if (!MemberSnapshot.Compile() || !RoomSnapshot.Compile() || !ValidateCompact(Room, Member)
        || !MemberSnapshot.Preserved() || !RoomSnapshot.Preserved()) return false;
    if (!MemberSnapshot.Save() || !RoomSnapshot.Save()) return false;
    Inspect(Room);
    Inspect(Member);
    UE_LOG(LogTemp, Display, TEXT("SQUAD_COMPACT_UI_PRESERVED graphs=2 animations=2"));
    return true;
}
}

int32 USquadCompactUIAssetsCommandlet::Main(const FString& Params)
{
    auto* Room = LoadObject<UWidgetBlueprint>(nullptr, SquadCompactUIAssets::RoomPath);
    auto* Member = LoadObject<UWidgetBlueprint>(nullptr, SquadCompactUIAssets::MemberPath);
    if (!SquadCompactUIAssets::ValidateTree(Room) || !SquadCompactUIAssets::ValidateTree(Member)) return 1;
    if (FParse::Param(*Params, TEXT("Inspect")))
    {
        SquadCompactUIAssets::Inspect(Room);
        SquadCompactUIAssets::Inspect(Member);
        const bool bCompact = Member->WidgetTree->FindWidget(TEXT("CaptainAccent")) != nullptr;
        const bool bValid = !bCompact || SquadCompactUIAssets::ValidateCompact(Room, Member);
        UE_LOG(LogTemp, Display, TEXT("SQUAD_COMPACT_UI_INSPECT_%s compact=%d"), bValid ? TEXT("OK") : TEXT("FAILED"), bCompact);
        return bValid ? 0 : 1;
    }
    if (!FParse::Param(*Params, TEXT("Build")))
    {
        UE_LOG(LogTemp, Error, TEXT("Use -run=SquadCompactUIAssets -Inspect or -Build."));
        return 1;
    }
    const bool bSuccess = SquadCompactUIAssets::Build(Room, Member);
    UE_LOG(LogTemp, Display, TEXT("SQUAD_COMPACT_UI_%s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));
    return bSuccess ? 0 : 1;
}
