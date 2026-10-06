#include "Map/BattleMap/BattleHUDWidgets.h"
#include "Map/BattleMap/BattleHUDLibrary.h"
#include "UIBasic/PortraitLibrary.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/Texture2D.h"

void UBattleHUDVisual::NativeTick(const FGeometry& G, float Delta)
{
    Super::NativeTick(G, Delta);
    if (Kind == EBattleVisualKind::ECG)
    {
        Phase = FMath::Fmod(Phase + Delta * .42f, 1.f);
        if (const auto Widget = GetCachedWidget()) { Widget->Invalidate(EInvalidateWidgetReason::Paint); }
    }
}
int32 UBattleHUDVisual::NativePaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& E, int32 Layer, const FWidgetStyle& Style, bool Enabled) const
{
    Layer = Super::NativePaint(Args,G,Cull,E,Layer,Style,Enabled) + 1;
    const FVector2D Size = G.GetLocalSize();
    FLinearColor Color = Tint * Style.GetColorAndOpacityTint();
    if (!Enabled) Color.A *= .35f;
    auto Line = [&](TArray<FVector2D> Points, FLinearColor C, float Thickness = 1.5f)
    {
        for (FVector2D& P : Points) P *= Size;
        FSlateDrawElement::MakeLines(E,Layer,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,C,true,Thickness);
    };
    auto Box = [&](FVector2D P, FVector2D S, FLinearColor C)
    { FSlateDrawElement::MakeBox(E,Layer,G.ToPaintGeometry(FVector2f(S * Size),FSlateLayoutTransform(FVector2f(P * Size))),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,C); };
    auto Circle = [&](FVector2D Center, float Radius, FLinearColor C)
    { TArray<FVector2D> P; for(int32 I=0;I<=20;++I){float A=I*2.f*PI/20;P.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius);} Line(P,C); };
    if (Image && Kind != EBattleVisualKind::ECG)
    {
        FSlateBrush B; B.SetResourceObject(Image); B.ImageSize=FVector2D(Image->GetSizeX(),Image->GetSizeY()); B.DrawAs=ESlateBrushDrawType::Image;
        FSlateDrawElement::MakeBox(E,Layer,G.ToPaintGeometry(),&B,ESlateDrawEffect::None,Style.GetColorAndOpacityTint());
        if (Kind != EBattleVisualKind::MiniMap) return Layer;
    }
    if (Kind == EBattleVisualKind::ECG)
    {
        const float H=UBattleHUDLibrary::SanitizeRatio(HealthRatio);
        FLinearColor Wave=H<=0.f?FLinearColor(.8f,.15f,.18f,1):H<.3f?FLinearColor(1,.18f,.16f,1):H<.7f?FLinearColor(1,.55f,.06f,1):FLinearColor(.03f,.83f,.96f,1);
        Line({{0,.55f},{1,.55f}},Wave.CopyWithNewOpacity(.16f),1.f);
        TArray<FVector2D> P;
        for(int32 I=0;I<=70;++I)
        {
            const float X=I/70.f, T=FMath::Fmod(X+Phase,1.f);
            float V=0.f;
            if(H>0.f){V=.08f*FMath::Exp(-FMath::Square((T-.22f)*30.f))-.14f*FMath::Exp(-FMath::Square((T-.39f)*70.f))+.68f*FMath::Exp(-FMath::Square((T-.43f)*65.f))-.28f*FMath::Exp(-FMath::Square((T-.48f)*60.f))+.12f*FMath::Exp(-FMath::Square((T-.68f)*20.f));}
            P.Add({X,.62f-V*.75f});
        }
        Line(P,Wave,1.25f); return Layer;
    }
    if (Kind == EBattleVisualKind::MiniMap)
    {
        if (!Image)
        {
            Box({0,0},{1,1},FLinearColor(.007f,.027f,.039f,1));
            // Deliberately schematic test map; no expensive world capture required.
            Box({.42f,0},{.10f,1},FLinearColor(.06f,.12f,.15f,1)); Box({0,.4f},{1,.09f},FLinearColor(.06f,.12f,.15f,1));
            for(auto P:{FVector2D(.08f,.08f),FVector2D(.64f,.08f),FVector2D(.08f,.65f),FVector2D(.65f,.66f)})
            { Box(P,{.23f,.20f},FLinearColor(.025f,.064f,.087f,1)); Line({P,P+FVector2D(.23f,0),P+FVector2D(.23f,.20f),P+FVector2D(0,.20f),P},FLinearColor(.08f,.18f,.23f,1),1); }
        }
        if(bShowGrid)for(int I=1;I<8;++I){float V=I/8.f;Line({{V,0},{V,1}},FLinearColor(.08f,.22f,.28f,.3f),.5f);Line({{0,V},{1,V}},FLinearColor(.08f,.22f,.28f,.3f),.5f);}
        for(int32 I=0;I<Markers.Num();++I)
        {
            const FVector2D P=(Markers[I]-Center)*Zoom+FVector2D(.5f,.5f);
            if(P.X<.02f||P.X>.98f||P.Y<.02f||P.Y>.98f)continue;
            const FLinearColor C=I==SelectedMarker?FLinearColor(0,1,1,1):FLinearColor(.35f,.74f,.87f,1);
            Line({P+FVector2D(0,-.022f),P+FVector2D(.014f,0),P+FVector2D(0,.022f),P+FVector2D(-.014f,0),P+FVector2D(0,-.022f)},C,2);
            if(I==SelectedMarker)Circle(P,.04f,C.CopyWithNewOpacity(.5f));
        }
        return Layer;
    }
    auto Stroke=[&](std::initializer_list<FVector2D> P){Line(TArray<FVector2D>(P),Color,1.7f);};
    switch(Glyph)
    {
    case EBattleGlyph::Standing: case EBattleGlyph::Person:
        Circle({.5f,.19f},.08f,Color);Stroke({{.5f,.30f},{.5f,.60f},{.34f,.90f}});Stroke({{.5f,.6f},{.67f,.9f}});Stroke({{.27f,.54f},{.5f,.34f},{.72f,.54f}});break;
    case EBattleGlyph::Crouching:
        Circle({.48f,.20f},.08f,Color);Stroke({{.47f,.31f},{.38f,.56f},{.68f,.62f},{.56f,.88f},{.76f,.88f}});Stroke({{.42f,.42f},{.70f,.42f},{.8f,.35f}});Stroke({{.39f,.56f},{.24f,.75f},{.41f,.88f}});break;
    case EBattleGlyph::Prone:
        Circle({.77f,.45f},.08f,Color);Stroke({{.14f,.66f},{.35f,.6f},{.62f,.65f},{.70f,.53f},{.91f,.56f}});Stroke({{.35f,.6f},{.26f,.47f},{.1f,.49f}});break;
    case EBattleGlyph::Backpack: case EBattleGlyph::Cargo:
        Stroke({{.25f,.23f},{.75f,.23f},{.79f,.86f},{.21f,.86f},{.25f,.23f}});Stroke({{.36f,.23f},{.36f,.1f},{.64f,.1f},{.64f,.23f}});Stroke({{.29f,.55f},{.7f,.55f},{.7f,.77f},{.29f,.77f},{.29f,.55f}});break;
    case EBattleGlyph::Stealth:
        Stroke({{.06f,.5f},{.27f,.29f},{.5f,.22f},{.73f,.29f},{.94f,.5f},{.73f,.71f},{.5f,.78f},{.27f,.71f},{.06f,.5f}});Circle({.5f,.5f},.16f,Color);break;
    case EBattleGlyph::Hand: case EBattleGlyph::EmptyHand: case EBattleGlyph::Pickup:
        Stroke({{.28f,.5f},{.16f,.42f},{.12f,.53f},{.28f,.82f},{.65f,.85f},{.79f,.63f},{.79f,.32f},{.7f,.29f},{.65f,.57f},{.63f,.18f},{.53f,.17f},{.51f,.54f},{.46f,.12f},{.37f,.15f},{.4f,.55f},{.3f,.23f},{.21f,.27f},{.28f,.5f}});break;
    case EBattleGlyph::Medkit:
        Stroke({{.15f,.31f},{.85f,.31f},{.85f,.81f},{.15f,.81f},{.15f,.31f}});Stroke({{.37f,.31f},{.37f,.18f},{.63f,.18f},{.63f,.31f}});Stroke({{.5f,.42f},{.5f,.7f}});Stroke({{.35f,.56f},{.65f,.56f}});break;
    case EBattleGlyph::Grenade: case EBattleGlyph::Smoke:
        Stroke({{.36f,.26f},{.64f,.26f},{.74f,.73f},{.62f,.89f},{.36f,.89f},{.24f,.72f},{.36f,.26f}});Stroke({{.42f,.26f},{.42f,.13f},{.67f,.13f},{.8f,.44f}});Stroke({{.28f,.56f},{.71f,.56f}});break;
    case EBattleGlyph::Tool:
        Stroke({{.20f,.86f},{.58f,.45f},{.5f,.24f},{.65f,.1f},{.65f,.29f},{.83f,.3f},{.91f,.14f},{.93f,.4f},{.71f,.5f},{.34f,.94f},{.20f,.86f}});break;
    case EBattleGlyph::Vehicle:
        // Frontal tactical rover: bold tyres, sloped armoured cabin and a split windshield.
        // A compact silhouette remains readable in the narrow member-mode rail.
        Box({.12f,.65f},{.16f,.24f},Color);Box({.72f,.65f},{.16f,.24f},Color);
        Line({{.16f,.71f},{.16f,.47f},{.29f,.17f},{.71f,.17f},{.84f,.47f},{.84f,.71f},{.16f,.71f}},Color,1.4f);
        Line({{.29f,.40f},{.35f,.26f},{.65f,.26f},{.71f,.40f},{.29f,.40f}},Color,1.2f);
        Line({{.5f,.26f},{.5f,.40f}},Color.CopyWithNewOpacity(Color.A*.65f),1.f);
        Box({.23f,.51f},{.14f,.085f},Color);Box({.63f,.51f},{.14f,.085f},Color);
        Line({{.42f,.58f},{.58f,.58f}},Color,1.4f);
        Line({{.13f,.73f},{.87f,.73f}},Color,1.8f);break;
    case EBattleGlyph::Squad:
        Circle({.5f,.2f},.07f,Color);Circle({.25f,.35f},.06f,Color);Circle({.75f,.35f},.06f,Color);Stroke({{.37f,.61f},{.38f,.35f},{.63f,.35f},{.64f,.61f}});Stroke({{.12f,.77f},{.13f,.5f},{.35f,.5f},{.37f,.77f}});Stroke({{.65f,.77f},{.66f,.5f},{.88f,.5f},{.89f,.77f}});break;
    case EBattleGlyph::Rifle:
        Stroke({{.06f,.40f},{.23f,.45f},{.3f,.32f},{.73f,.32f},{.73f,.43f},{.96f,.43f},{.96f,.51f},{.7f,.51f},{.66f,.63f},{.51f,.63f},{.55f,.87f},{.47f,.88f},{.38f,.63f},{.25f,.59f},{.07f,.75f},{.06f,.40f}});Stroke({{.43f,.32f},{.43f,.17f},{.55f,.17f},{.55f,.32f}});break;
    case EBattleGlyph::Pistol:
        Stroke({{.2f,.28f},{.83f,.28f},{.83f,.47f},{.56f,.47f},{.43f,.85f},{.22f,.85f},{.3f,.48f},{.2f,.48f},{.2f,.28f}});break;
    case EBattleGlyph::Enter: case EBattleGlyph::Exit:
        Stroke({{.54f,.13f},{.85f,.13f},{.85f,.87f},{.54f,.87f}});Stroke({{.1f,.5f},{.68f,.5f},{.48f,.3f}});Stroke({{.68f,.5f},{.48f,.7f}});break;
    case EBattleGlyph::Locate:
        Circle({.5f,.5f},.3f,Color);Circle({.5f,.5f},.08f,Color);Stroke({{.5f,.05f},{.5f,.3f}});Stroke({{.5f,.7f},{.5f,.95f}});Stroke({{.05f,.5f},{.3f,.5f}});Stroke({{.7f,.5f},{.95f,.5f}});break;
    case EBattleGlyph::Plus: Stroke({{.5f,.2f},{.5f,.8f}}); [[fallthrough]];
    case EBattleGlyph::Minus: Stroke({{.2f,.5f},{.8f,.5f}});break;
    case EBattleGlyph::Layers:
        Stroke({{.1f,.35f},{.5f,.12f},{.9f,.35f},{.5f,.58f},{.1f,.35f}});Stroke({{.1f,.55f},{.5f,.78f},{.9f,.55f}});break;
    default: Stroke({{.23f,.38f},{.5f,.67f},{.77f,.38f}});break;
    }
    return Layer;
}
void UBattleHUDButton::NativePreConstruct()
{
    Super::NativePreConstruct();
    if(Symbol)
    {
        Symbol->Glyph=Glyph;
        Symbol->SetVisibility(bShowGlyph?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Symbol->Slot))
            CanvasSlot->SetPosition({-10.f,bVerticalLabel?-28.f:ButtonText.IsEmpty()?-10.f:-18.f});
    }
    if(Label && bVerticalLabel)
    {
        if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Label->Slot))
        { CanvasSlot->SetAnchors(FAnchors(.5f,.5f));CanvasSlot->SetOffsets(FMargin(-10,2,20,38)); }
    }
}
void UBattleMemberCardWidget::NativeConstruct(){Super::NativeConstruct();RefreshMember();}
void UBattleMemberCardWidget::SetMember(const FBattleMemberView& Data)
{
    Member=Data; ChoiceID=FName(*Data.UnitId.ToString()); RefreshMember();
}
void UBattleMemberCardWidget::RefreshMember()
{
    if(Portrait)Portrait->SetBrush(UPortraitLibrary::GetSquarePortrait(Member.Profile));
    if(MemberName)MemberName->SetText(Member.Profile.CodeName);
    if(StaminaBar)StaminaBar->SetPercent(UBattleHUDLibrary::SanitizeRatio(Member.Stamina));
    if(MoraleBar)MoraleBar->SetPercent(UBattleHUDLibrary::SanitizeRatio(Member.Morale));
    const float H=UBattleHUDLibrary::SanitizeRatio(Member.Health);
    if(Heartbeat)Heartbeat->HealthRatio=H;
    if(HealthText){HealthText->SetText(FText::FromString(H<=0?TEXT("失能"):H<.3f?TEXT("危急"):H<.7f?TEXT("负伤"):TEXT("稳定")));HealthText->SetColorAndOpacity(H<.7f?FLinearColor(1,.58f,.12f,1):FLinearColor(.12f,.8f,.9f,1));}
    const bool Linked=Member.HasLinkedHands();
    if(SplitHands)SplitHands->SetVisibility(Linked?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(MergedHands)MergedHands->SetVisibility(Linked?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(LeftWeapon){LeftWeapon->Glyph=Member.LeftFallback;LeftWeapon->Image=Member.LeftItemImage;}
    if(RightWeapon){RightWeapon->Glyph=Member.RightFallback;RightWeapon->Image=Member.RightItemImage;}
    if(LinkedWeapon){LinkedWeapon->Glyph=Member.LeftFallback;LinkedWeapon->Image=Member.LeftItemImage;}
}
