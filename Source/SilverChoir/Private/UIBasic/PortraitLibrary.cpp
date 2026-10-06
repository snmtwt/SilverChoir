#include "UIBasic/PortraitLibrary.h"
#include "Engine/Texture2D.h"

FSlateBrush UPortraitLibrary::MakePortraitBrush(UTexture2D* Texture, EPortraitFormat Format, FVector2D Focus, float SquareCropScale)
{
    FSlateBrush Brush;
    Brush.DrawAs = Texture ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
    Brush.SetResourceObject(Texture);
    if (!Texture) return Brush;
    const FIntPoint Imported = Texture->GetImportedSize();
    const double W = FMath::Max(1, Imported.X > 0 ? Imported.X : Texture->GetSizeX());
    const double H = FMath::Max(1, Imported.Y > 0 ? Imported.Y : Texture->GetSizeY());
    Brush.ImageSize = FVector2D(W, H);
    if (Format == EPortraitFormat::Square)
    {
        const double Edge = FMath::Min(W, H) * (FMath::IsFinite(SquareCropScale) ? FMath::Clamp(SquareCropScale, .1f, 1.f) : 1.f);
        const FVector2D Span(Edge / W, Edge / H);
        if (!FMath::IsFinite(Focus.X)) Focus.X = .5;
        if (!FMath::IsFinite(Focus.Y)) Focus.Y = .28;
        const FVector2D Start(FMath::Clamp(Focus.X - Span.X / 2, 0., 1. - Span.X), FMath::Clamp(Focus.Y - Span.Y / 2, 0., 1. - Span.Y));
        Brush.SetUVRegion(FBox2f(FVector2f(Start), FVector2f(Start + Span)));
        Brush.ImageSize = FVector2D(Edge, Edge);
    }
    return Brush;
}
FSlateBrush UPortraitLibrary::GetFullPortrait(const FUnitProfile& Profile)
{ return MakePortraitBrush(Profile.PortraitTexture, EPortraitFormat::Full); }
FSlateBrush UPortraitLibrary::GetSquarePortrait(const FUnitProfile& Profile)
{
    if (Profile.SquarePortraitTexture) return MakePortraitBrush(Profile.SquarePortraitTexture, EPortraitFormat::Square, FVector2D(.5,.5));
    return MakePortraitBrush(Profile.PortraitTexture, EPortraitFormat::Square, Profile.PortraitCropFocus, Profile.PortraitCropScale);
}
