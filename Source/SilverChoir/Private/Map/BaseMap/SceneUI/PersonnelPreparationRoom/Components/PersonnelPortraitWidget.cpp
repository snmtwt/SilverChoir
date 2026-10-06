#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Engine/Texture2D.h"
void UPersonnelPortraitWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    // List rows receive profile data before TakeWidget. Preserve the authored face crop.
    if (bHasProfileBrush) ApplyPortraitBrush(ProfileBrush);
    else SetPortraitTexture(PortraitTexture);
}
void UPersonnelPortraitWidget::SetPortraitTexture(UTexture2D* Texture)
{
    PortraitTexture=Texture;
    bHasProfileBrush=false;
    ApplyPortraitBrush(UPortraitLibrary::MakePortraitBrush(Texture, PortraitFormat));
}
void UPersonnelPortraitWidget::ApplyPortraitBrush(const FSlateBrush& Brush)
{
    const bool bHasTexture = Brush.GetResourceObject() != nullptr;
    if (PortraitImage)
    {
        PortraitImage->SetBrush(Brush);
        PortraitImage->SetVisibility(bHasTexture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if (PortraitPlaceholder) PortraitPlaceholder->SetVisibility(bHasTexture?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
void UPersonnelPortraitWidget::SetUnitPortrait(const FUnitProfile& Profile)
{
    PortraitTexture=PortraitFormat == EPortraitFormat::Square && Profile.SquarePortraitTexture ? Profile.SquarePortraitTexture.Get() : Profile.PortraitTexture.Get();
    ProfileBrush=PortraitFormat == EPortraitFormat::Square ? UPortraitLibrary::GetSquarePortrait(Profile) : UPortraitLibrary::GetFullPortrait(Profile);
    bHasProfileBrush=true;
    ApplyPortraitBrush(ProfileBrush);
}
