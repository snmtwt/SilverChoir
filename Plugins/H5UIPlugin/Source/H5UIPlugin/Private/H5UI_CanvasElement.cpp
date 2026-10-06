#include "H5UI_CanvasElement.h"
#include "H5UI_Interfaces.h"
#include "H5UI_Module.h"
#include "Engine/Texture2D.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/MeshUtilities.h"
#include "RmlUi/Core/RenderManager.h"
#include "RmlUi/Core/PropertyDictionary.h"
#include "RmlUi/Core/StyleSheetSpecification.h"

FLinearColor FH5UI_CanvasBrush::At(FVector2D P) const
{
    if(Stops.IsEmpty())return Color;
    const FVector2D D=End-Start;
    const double T=D.SizeSquared()>UE_SMALL_NUMBER?FVector2D::DotProduct(P-Start,D)/D.SizeSquared():0;
    if(T<=Stops[0].Key)return Stops[0].Value;
    for(int32 I=1;I<Stops.Num();++I)if(T<=Stops[I].Key)
        return FMath::Lerp(Stops[I-1].Value,Stops[I].Value,float((T-Stops[I-1].Key)/FMath::Max(UE_SMALL_NUMBER,Stops[I].Key-Stops[I-1].Key)));
    return Stops.Last().Value;
}

FH5UI_CanvasElement::FH5UI_CanvasElement(const Rml::String& Tag):Rml::Element(Tag){ResetBitmap();}
FH5UI_CanvasElement::~FH5UI_CanvasElement(){Geometry={};CallbackTexture={};Texture.Reset();}
bool FH5UI_CanvasElement::GetIntrinsicDimensions(Rml::Vector2f& S,float& Ratio)
{S=Rml::Vector2f(float(Width),float(Height));Ratio=Height?float(Width)/Height:1.f;return true;}
void FH5UI_CanvasElement::OnAttributeChange(const Rml::ElementAttributes& A)
{
    Rml::Element::OnAttributeChange(A);
    if(A.find("width")==A.end()&&A.find("height")==A.end())return;
    Width=FMath::Clamp(GetAttribute<int>("width",300),0,MaxDimension);
    Height=FMath::Clamp(GetAttribute<int>("height",150),0,MaxDimension);
    if(int64(Width)*Height>MaxPixels)Height=MaxPixels/FMath::Max(1,Width);
    ResetBitmap();DirtyLayout();
}
void FH5UI_CanvasElement::ResetBitmap()
{
    ++Revision;Paths.Reset();PathPoints=0;
    Pixels.SetNumZeroed(Width*Height); // same-size reset must also clear existing pixels
    for(auto& P:Pixels)P=FLinearColor::Transparent;
    Coverage.Reset();Geometry={};CallbackTexture={};Texture.Reset();bDirty=true;
}
void FH5UI_CanvasElement::OnResize(){Rml::Element::OnResize();Geometry={};}
bool FH5UI_CanvasElement::ParseColor(const FString& Text,FLinearColor& Out)
{
    // RmlUi's legacy rgba parser expects integer alpha 0..255. Canvas uses
    // browser CSS alpha 0..1; preserve that meaning, including percentages.
    const FString Value=Text.TrimStartAndEnd().ToLower();
    if(Value.StartsWith(TEXT("rgba("))&&Value.EndsWith(TEXT(")")))
    {
        TArray<FString> Parts;Value.Mid(5,Value.Len()-6).ParseIntoArray(Parts,TEXT(","),false);
        if(Parts.Num()!=4)return false;
        float Channels[4];
        for(int I=0;I<4;++I)
        {
            FString Part=Parts[I].TrimStartAndEnd();const bool Percent=Part.RemoveFromEnd(TEXT("%"));
            double N;if(!LexTryParseString(N,*Part)||!FMath::IsFinite(N))return false;
            Channels[I]=float(FMath::Clamp(N/(Percent?100.:(I==3?1.:255.)),0.,1.));
        }
        Out=FLinearColor(Channels[0],Channels[1],Channels[2],Channels[3]);return true;
    }
    Rml::PropertyDictionary P;
    if(!Rml::StyleSheetSpecification::ParsePropertyDeclaration(P,"color",TCHAR_TO_UTF8(*Text)))return false;
    const auto* V=P.GetProperty(Rml::PropertyId::Color);if(!V)return false;
    const auto C=V->Get<Rml::Colourb>();Out=FLinearColor(C.red/255.f,C.green/255.f,C.blue/255.f,C.alpha/255.f);return true;
}
void FH5UI_CanvasElement::BeginPath(){Paths.Reset();PathPoints=0;}
void FH5UI_CanvasElement::MoveTo(FVector2D P)
{
    if(PathPoints>=16384)return;
    Paths.AddDefaulted_GetRef().Points.Add(P);++PathPoints;
}
void FH5UI_CanvasElement::LineTo(FVector2D P)
{
    if(Paths.IsEmpty())MoveTo(P);
    else if(PathPoints<16384){Paths.Last().Points.Add(P);++PathPoints;}
}
void FH5UI_CanvasElement::ClosePath(){if(!Paths.IsEmpty())Paths.Last().bClosed=true;}
void FH5UI_CanvasElement::BezierTo(FVector2D A,FVector2D B,FVector2D C)
{
    if(Paths.IsEmpty())MoveTo(A);
    const FVector2D P=Paths.Last().Points.Last();
    const int32 Steps=FMath::Clamp(FMath::CeilToInt(((P-A).Size()+(A-B).Size()+(B-C).Size())/3),8,128);
    for(int32 I=1;I<=Steps;++I){const double T=double(I)/Steps,U=1-T;LineTo(U*U*U*P+3*U*U*T*A+3*U*T*T*B+T*T*T*C);}
}
void FH5UI_CanvasElement::QuadraticTo(FVector2D A,FVector2D B)
{
    if(Paths.IsEmpty())MoveTo(A);
    const FVector2D P=Paths.Last().Points.Last();BezierTo(P+(A-P)*2/3,B+(A-B)*2/3,B);
}
void FH5UI_CanvasElement::Composite(int32 I,FLinearColor C,float Alpha,bool Add)
{
    C.A=FMath::Clamp(C.A*Alpha,0.f,1.f);auto& D=Pixels[I];const float Keep=Add?1.f:1-C.A;
    D=FLinearColor(FMath::Min(1.f,C.R*C.A+D.R*Keep),FMath::Min(1.f,C.G*C.A+D.G*Keep),FMath::Min(1.f,C.B*C.A+D.B*Keep),FMath::Min(1.f,C.A+D.A*Keep));
}
void FH5UI_CanvasElement::Stroke(const FH5UI_CanvasBrush& Brush,float LineWidth,float Alpha,bool Round,bool Add)
{
    if(Pixels.IsEmpty()||Paths.IsEmpty()||!FMath::IsFinite(LineWidth)||!FMath::IsFinite(Alpha)||LineWidth<=0)return;
    Coverage.SetNumZeroed(Pixels.Num());FMemory::Memzero(Coverage.GetData(),Coverage.Num()*sizeof(float));
    const double Radius=FMath::Min(2048.f,LineWidth)*.5;
    for(const auto& Path:Paths)
    {
        const int32 N=Path.Points.Num();
        for(int32 I=0;I<(Path.bClosed?N:N-1);++I)
        {
            const auto A=Path.Points[I],B=Path.Points[(I+1)%N],D=B-A;const double Length2=D.SizeSquared();
            if(Length2<UE_SMALL_NUMBER)continue;
            const int32 L=FMath::Clamp(FMath::FloorToInt(FMath::Min(A.X,B.X)-Radius-1),0,Width);
            const int32 R=FMath::Clamp(FMath::CeilToInt(FMath::Max(A.X,B.X)+Radius+1),0,Width);
            const int32 T=FMath::Clamp(FMath::FloorToInt(FMath::Min(A.Y,B.Y)-Radius-1),0,Height);
            const int32 Bottom=FMath::Clamp(FMath::CeilToInt(FMath::Max(A.Y,B.Y)+Radius+1),0,Height);
            for(int32 Y=T;Y<Bottom;++Y)for(int32 X=L;X<R;++X)
            {
                const FVector2D P(X+.5,Y+.5);const double U=FVector2D::DotProduct(P-A,D)/Length2;
                if(!Round&&!Path.bClosed&&((I==0&&U<0)||(I==N-2&&U>1)))continue;
                const double Distance=(P-(A+D*FMath::Clamp(U,0.,1.))).Size();
                const float C=float(FMath::Clamp(Radius+.5-Distance,0.,1.));
                Coverage[Y*Width+X]=FMath::Max(Coverage[Y*Width+X],C);
            }
        }
    }
    for(int32 Y=0;Y<Height;++Y)for(int32 X=0;X<Width;++X)
    {const int32 I=Y*Width+X;if(Coverage[I]>0)Composite(I,Brush.At({X+.5,Y+.5}),Alpha*Coverage[I],Add);}
    bDirty=true;
}
void FH5UI_CanvasElement::Rectangle(const TArray<FVector2D>& P,const FH5UI_CanvasBrush& Brush,float Alpha,bool Clear,bool Add)
{
    if(P.Num()!=4||Pixels.IsEmpty())return;
    double Left=P[0].X,Right=Left,Top=P[0].Y,Bottom=Top;
    for(auto V:P){Left=FMath::Min(Left,V.X);Right=FMath::Max(Right,V.X);Top=FMath::Min(Top,V.Y);Bottom=FMath::Max(Bottom,V.Y);}
    auto Inside=[&](FVector2D V)
    {
        int Sign=0;
        for(int I=0;I<4;++I){const auto A=P[I],B=P[(I+1)%4];const double Cross=(B.X-A.X)*(V.Y-A.Y)-(B.Y-A.Y)*(V.X-A.X);if(FMath::Abs(Cross)<UE_SMALL_NUMBER)continue;const int Next=Cross>0?1:-1;if(Sign&&Sign!=Next)return false;Sign=Next;}
        return Sign!=0;
    };
    const int32 L=FMath::Clamp(FMath::FloorToInt(Left),0,Width),R=FMath::Clamp(FMath::CeilToInt(Right),0,Width);
    const int32 T=FMath::Clamp(FMath::FloorToInt(Top),0,Height),B=FMath::Clamp(FMath::CeilToInt(Bottom),0,Height);
    // HUD clears and fills generally use axis-aligned rectangles. Exact coverage
    // avoids evaluating sixteen edge tests per pixel for these common operations.
    const bool AxisAligned=(P[0].X==P[1].X&&P[1].Y==P[2].Y&&P[2].X==P[3].X&&P[3].Y==P[0].Y)
        ||(P[0].Y==P[1].Y&&P[1].X==P[2].X&&P[2].Y==P[3].Y&&P[3].X==P[0].X);
    if(AxisAligned)
    {
        for(int32 Y=T;Y<B;++Y)
        {
            const float CY=float(FMath::Max(0.,FMath::Min(Bottom,double(Y+1))-FMath::Max(Top,double(Y))));
            for(int32 X=L;X<R;++X)
            {
                const float C=CY*float(FMath::Max(0.,FMath::Min(Right,double(X+1))-FMath::Max(Left,double(X))));
                if(Clear)Pixels[Y*Width+X]*=(1-C);
                else if(C>0)Composite(Y*Width+X,Brush.At({X+.5,Y+.5}),C*Alpha,Add);
            }
        }
        bDirty=true;return;
    }
    for(int32 Y=T;Y<B;++Y)for(int32 X=L;X<R;++X)
    {
        float C=0;for(double DY:{.25,.75})for(double DX:{.25,.75})if(Inside({X+DX,Y+DY}))C+=.25f;
        if(Clear)Pixels[Y*Width+X]*=(1-C);
        else if(C>0)Composite(Y*Width+X,Brush.At({X+.5,Y+.5}),C*Alpha,Add);
    }
    bDirty=true;
}
FColor FH5UI_CanvasElement::ReadPixel(int32 X,int32 Y) const
{
    if(X<0||Y<0||X>=Width||Y>=Height)return FColor::Transparent;
    const auto C=Pixels[Y*Width+X];if(C.A<=0)return FColor::Transparent;
    return FColor(FMath::RoundToInt(C.R/C.A*255),FMath::RoundToInt(C.G/C.A*255),FMath::RoundToInt(C.B/C.A*255),FMath::RoundToInt(C.A*255));
}
void FH5UI_CanvasElement::Upload()
{
    if(!bDirty||Pixels.IsEmpty())return;
    if(!Texture.IsValid())
    {
        Texture.Reset(UTexture2D::CreateTransient(Width,Height,PF_B8G8R8A8));
        if(!Texture.IsValid())return;
        Texture->SRGB=true;Texture->Filter=TF_Bilinear;Texture->AddressX=TA_Clamp;Texture->AddressY=TA_Clamp;Texture->NeverStream=true;Texture->UpdateResource();
    }
    auto* Bytes=new uint8[Pixels.Num()*4];
    for(int32 I=0;I<Pixels.Num();++I)
    {
        const auto C=Pixels[I];const float Inv=C.A>0?255.f/C.A:0;
        Bytes[I*4]=uint8(FMath::Clamp(FMath::RoundToInt(C.B*Inv),0,255));Bytes[I*4+1]=uint8(FMath::Clamp(FMath::RoundToInt(C.G*Inv),0,255));
        Bytes[I*4+2]=uint8(FMath::Clamp(FMath::RoundToInt(C.R*Inv),0,255));Bytes[I*4+3]=uint8(FMath::Clamp(FMath::RoundToInt(C.A*255),0,255));
    }
    auto* Region=new FUpdateTextureRegion2D(0,0,0,0,Width,Height);
    Texture->UpdateTextureRegions(0,1,Region,Width*4,4,Bytes,[](uint8* Data,const FUpdateTextureRegion2D* R){delete[] Data;delete R;});
    ++UploadCount;bDirty=false;
}
void FH5UI_CanvasElement::OnRender()
{
    if(!GetContext()||Pixels.IsEmpty())return;
    Upload();if(!Texture.IsValid())return;
    auto& Manager=GetContext()->GetRenderManager();
    const float Opacity=GetComputedValues().opacity();
    if(LastOpacity!=Opacity){LastOpacity=Opacity;Geometry={};}
    if(!CallbackTexture)
    {
        CallbackTexture=Manager.MakeCallbackTexture([Weak=TWeakObjectPtr<UTexture2D>(Texture.Get())](const Rml::CallbackTextureInterface& Interface)
        {
            auto* T=Weak.Get();if(!T||!FH5UI_Module::IsAvailable())return false;
            const auto Handle=FH5UI_Module::Get().GetRenderInterface().CreateExternalTexture(T);
            if(!Handle)return false;
            Interface.SetTextureHandle(Handle,Rml::Vector2i(T->GetSizeX(),T->GetSizeY()));return true;
        });
    }
    if(!Geometry)
    {
        // This external texture uses straight alpha in Slate (unlike Rml's usual premultiplied textures).
        Rml::Mesh Mesh;Rml::MeshUtilities::GenerateQuad(Mesh,Rml::Vector2f(0),GetBox().GetSize(Rml::BoxArea::Content),Rml::ColourbPremultiplied(255,255,255,uint8(FMath::Clamp(Opacity,0.f,1.f)*255)),Rml::Vector2f(0),Rml::Vector2f(1));
        Geometry=Manager.MakeGeometry(MoveTemp(Mesh));
    }
    Geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Content),CallbackTexture);
}
