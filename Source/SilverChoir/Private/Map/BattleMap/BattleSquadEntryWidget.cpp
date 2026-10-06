#include "Map/BattleMap/BattleSquadEntryWidget.h"

#include "Map/BattleMap/BattleHUDWidgets.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UIBasic/PixelAlignedButtonFrame.h"

UBattleSquadEntryWidget::UBattleSquadEntryWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
    MinimumSize = FVector2D::ZeroVector;
    bUseTabStyle = false;
    ButtonText = FText::GetEmpty();
    BackgroundColor = FLinearColor(FColor(7, 19, 29, 250));
    BorderColor = FLinearColor(FColor(37, 72, 89));
    AccentColor = FLinearColor(FColor(30, 211, 231));
    PreviewSquadName = NSLOCTEXT("BattleSquadEntry", "PreviewName", "阿尔法小队");
}

void UBattleSquadEntryWidget::BuildDefaultWidgetTree(UWidgetTree* Tree)
{
    if (!Tree || Tree->RootWidget) return;
    auto* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SquadEntrySize"));
    Size->SetWidthOverride(170.f);
    Size->SetHeightOverride(52.f);
    Tree->RootWidget = Size;
    auto* Padding = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SquadEntryPadding"));
    FSlateBrush EmptyBrush;
    EmptyBrush.DrawAs = ESlateBrushDrawType::NoDrawType;
    Padding->SetBrush(EmptyBrush);
    Padding->SetPadding(FMargin(10, 6, 8, 6));
    Padding->SetVerticalAlignment(VAlign_Center);
    Size->AddChild(Padding);
    auto* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SquadEntryRow"));
    Padding->AddChild(Row);

    auto* IconSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SquadIconSize"));
    IconSize->SetWidthOverride(32.f);
    IconSize->SetHeightOverride(32.f);
    Row->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
    auto* IconPlate = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SquadIconPlate"));
    IconPlate->SetBrushColor(FLinearColor(FColor(14, 39, 52)));
    IconPlate->SetPadding(FMargin(3));
    IconSize->AddChild(IconPlate);
    auto* IconLayers = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("SquadIconLayers"));
    IconPlate->AddChild(IconLayers);
    auto* Icon = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SquadIcon"));
    Icon->SetColorAndOpacity(FLinearColor::White);
    auto* IconSlot = IconLayers->AddChildToOverlay(Icon);
    IconSlot->SetHorizontalAlignment(HAlign_Fill);
    IconSlot->SetVerticalAlignment(VAlign_Fill);
    auto* Fallback = Tree->ConstructWidget<UBattleHUDVisual>(UBattleHUDVisual::StaticClass(), TEXT("FallbackSymbol"));
    Fallback->Glyph = EBattleGlyph::Squad;
    Fallback->Tint = FLinearColor(FColor(142, 198, 217));
    auto* SymbolSlot = IconLayers->AddChildToOverlay(Fallback);
    SymbolSlot->SetHorizontalAlignment(HAlign_Fill);
    SymbolSlot->SetVerticalAlignment(VAlign_Fill);

    auto* Details = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SquadDetails"));
    auto* DetailsSlot = Row->AddChildToHorizontalBox(Details);
    DetailsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    DetailsSlot->SetPadding(FMargin(8, 0, 3, 0));
    DetailsSlot->SetVerticalAlignment(VAlign_Center);
    auto* Name = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SquadNameLabel"));
    auto* Count = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MemberCountLabel"));
    auto* Index = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SquadIndexLabel"));
    auto* Typeface = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    Name->SetFont(FSlateFontInfo(Typeface, 11));
    Count->SetFont(FSlateFontInfo(Typeface, 9));
    Index->SetFont(FSlateFontInfo(Typeface, 8));
    Name->SetColorAndOpacity(FLinearColor(FColor(213, 233, 242)));
    Count->SetColorAndOpacity(FLinearColor(FColor(119, 157, 176)));
    Index->SetColorAndOpacity(FLinearColor(FColor(87, 142, 164)));
    Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    Count->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    Details->AddChildToVerticalBox(Name);
    Details->AddChildToVerticalBox(Count)->SetPadding(FMargin(0, 3, 0, 0));
    Row->AddChildToHorizontalBox(Index)->SetVerticalAlignment(VAlign_Top);
    Tree->ForEachWidget([](UWidget* Widget)
    {
        // Input, sounds and delayed click feedback belong to the outer user widget.
        Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
    });
}

TSharedRef<SWidget> UBattleSquadEntryWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildDefaultWidgetTree(WidgetTree);
        SquadIcon = Cast<UImage>(WidgetTree->FindWidget(TEXT("SquadIcon")));
        SquadNameLabel = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("SquadNameLabel")));
        MemberCountLabel = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("MemberCountLabel")));
        SquadIndexLabel = Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("SquadIndexLabel")));
        FallbackSymbol = Cast<UBattleHUDVisual>(WidgetTree->FindWidget(TEXT("FallbackSymbol")));
    }
    return Super::RebuildWidget();
}

void UBattleSquadEntryWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    RefreshSquad();
}

void UBattleSquadEntryWidget::NativeConstruct()
{
    Super::NativeConstruct();
    RefreshSquad();
}

void UBattleSquadEntryWidget::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    RefreshSquad();
}

void UBattleSquadEntryWidget::SetSquad(const FBattleSquadView& Data, int32 DisplayIndex)
{
    CancelPendingClick();
    Squad = Data;
    SquadId = Data.SquadId;
    SquadDisplayIndex = FMath::Max(0, DisplayIndex);
    ChoiceID = SquadId.IsValid() ? FName(*SquadId.ToString()) : NAME_None;
    bHasSquadData = true;
    SetIsEnabled(SquadId.IsValid());
    RefreshSquad();
}

void UBattleSquadEntryWidget::RefreshSquad()
{
    const bool bPreview = !bHasSquadData && IsDesignTime();
    FText Name = bPreview ? PreviewSquadName : Squad.Name;
    if (Name.IsEmpty()) Name = NSLOCTEXT("BattleSquadEntry", "Unnamed", "未命名小队");
    const int32 MemberCount = bPreview ? FMath::Max(0, PreviewMemberCount) : Squad.Members.Num();
    if (SquadNameLabel) SquadNameLabel->SetText(Name);
    if (MemberCountLabel)
        MemberCountLabel->SetText(FText::Format(NSLOCTEXT("BattleSquadEntry", "MemberCount", "{0} 名成员"), FText::AsNumber(MemberCount)));
    if (SquadIndexLabel) SquadIndexLabel->SetText(FText::FromString(FString::Printf(TEXT("%02d"), SquadDisplayIndex + 1)));
    if (SquadIcon)
    {
        SquadIcon->SetBrushFromTexture(Squad.Icon, false);
        SquadIcon->SetVisibility(Squad.Icon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (FallbackSymbol)
        FallbackSymbol->SetVisibility(Squad.Icon ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
    SetToolTipText(FText::Format(NSLOCTEXT("BattleSquadEntry", "Tooltip", "{0}\n{1} 名成员 · 点击查看成员"), Name, FText::AsNumber(MemberCount)));
}

int32 UBattleSquadEntryWidget::PaintButtonFrame(const FGeometry& Geometry, const FSlateRect& Culling,
    FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const
{
    const int32 ContentLayer = Super::PaintButtonFrame(Geometry, Culling, Elements, Layer, Style, bEnabled);
    if (!IsButtonSelected() && !(IsDesignTime() && bPreviewSelected)) return ContentLayer;
    const auto Frame = FPixelAlignedButtonFrame::Make(Geometry, 1.f);
    const FVector2f Size = Frame.Max - Frame.Min;
    if (Size.X <= 0.f || Size.Y <= 0.f) return ContentLayer;
    const FLinearColor Tint = Style.GetColorAndOpacityTint()
        * FLinearColor(1, 1, 1, bEnabled && GetIsEnabled() ? 1.f : .4f);
    const auto* Brush = FCoreStyle::Get().GetBrush("WhiteBrush");
    FSlateDrawElement::MakeBox(Elements, ContentLayer,
        Geometry.ToPaintGeometry(FVector2f(Frame.Thickness.X * 3.f, Size.Y), FSlateLayoutTransform(Frame.Min)),
        Brush, ESlateDrawEffect::NoPixelSnapping, AccentColor * Tint);
    FSlateDrawElement::MakeBox(Elements, ContentLayer,
        Geometry.ToPaintGeometry(FVector2f(Size.X, Frame.Thickness.Y), FSlateLayoutTransform(FVector2f(Frame.Min.X, Frame.Max.Y - Frame.Thickness.Y))),
        Brush, ESlateDrawEffect::NoPixelSnapping, AccentColor * Tint);
    return ContentLayer + 1;
}
