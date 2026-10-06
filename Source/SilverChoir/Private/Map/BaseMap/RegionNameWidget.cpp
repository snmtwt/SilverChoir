#include "Map/BaseMap/RegionNameWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"

TSharedRef<SWidget> URegionNameWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		RegionNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RegionNameText"));
		bUsesNativeLabel = true;
		RegionNameText->SetJustification(ETextJustify::Center);
		auto Font = RegionNameText->GetFont();
		Font.Size = 36;
		Font.TypefaceFontName = TEXT("Bold");
		// A small matching outline also thickens glyphs supplied by the Chinese fallback font.
		Font.OutlineSettings.OutlineSize = 1;
		Font.OutlineSettings.OutlineColor = LabelColor;
		RegionNameText->SetFont(Font);
		WidgetTree->RootWidget = RegionNameText;
	}
	return Super::RebuildWidget();
}
void URegionNameWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	UpdateRegionLabel(RegionName, LabelColor, bIsHighlighted);
}
void URegionNameWidget::UpdateRegionLabel(const FText& Name, FLinearColor Color, bool bHighlighted)
{
	RegionName = Name; LabelColor = Color; bIsHighlighted = bHighlighted;
	if (RegionNameText)
	{
		RegionNameText->SetText(Name);
		RegionNameText->SetColorAndOpacity(FSlateColor(Color));
		// Only recolor the native fallback; Designer widgets retain their own outline styling.
		if (bUsesNativeLabel)
		{
			auto Font = RegionNameText->GetFont();
			Font.OutlineSettings.OutlineColor = Color;
			RegionNameText->SetFont(Font);
		}
	}
	OnRegionLabelUpdated();
}
