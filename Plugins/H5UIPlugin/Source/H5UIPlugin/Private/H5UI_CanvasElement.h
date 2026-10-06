#pragma once
#include "CoreMinimal.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/CallbackTexture.h"
#include "RmlUi/Core/Geometry.h"
#include "UObject/StrongObjectPtr.h"

class UTexture2D;

struct FH5UI_CanvasBrush
{
    FLinearColor Color = FLinearColor::Black;
    FVector2D Start, End;
    TArray<TPair<double, FLinearColor>> Stops;
    FLinearColor At(FVector2D Point) const;
};

/** Small retained bitmap for native Canvas 2D. No browser, per-frame UObject or texture-handle allocation. */
class FH5UI_CanvasElement final : public Rml::Element
{
public:
    RMLUI_RTTI_DefineWithParent(FH5UI_CanvasElement, Rml::Element)
    explicit FH5UI_CanvasElement(const Rml::String& Tag);
    virtual ~FH5UI_CanvasElement() override;
    virtual bool GetIntrinsicDimensions(Rml::Vector2f& Size, float& Ratio) override;
    virtual void OnAttributeChange(const Rml::ElementAttributes& Attributes) override;
    virtual void OnResize() override;
    virtual void OnRender() override;

    static constexpr int32 MaxDimension = 2048;
    static constexpr int32 MaxPixels = 1024 * 1024;
    int32 Width = 300, Height = 150;
    uint32 Revision = 0;
    void ResetBitmap();
    void BeginPath();
    void MoveTo(FVector2D P);
    void LineTo(FVector2D P);
    void ClosePath();
    void BezierTo(FVector2D A, FVector2D B, FVector2D C);
    void QuadraticTo(FVector2D A, FVector2D B);
    void Stroke(const FH5UI_CanvasBrush& Brush, float LineWidth, float Alpha, bool bRoundCap, bool bAdditive);
    void Rectangle(const TArray<FVector2D>& Corners, const FH5UI_CanvasBrush& Brush, float Alpha, bool bClear, bool bAdditive);
    FColor ReadPixel(int32 X, int32 Y) const;
    static bool ParseColor(const FString& Text, FLinearColor& Out);
    uint64 GetUploadCount() const { return UploadCount; }
private:
    struct FSubPath { TArray<FVector2D> Points; bool bClosed = false; };
    TArray<FSubPath> Paths;
    int32 PathPoints = 0;
    TArray<FLinearColor> Pixels; // premultiplied, sRGB components as required by the Canvas bitmap
    TArray<float> Coverage;
    TStrongObjectPtr<UTexture2D> Texture;
    Rml::CallbackTexture CallbackTexture;
    Rml::Geometry Geometry;
    bool bDirty = true;
    uint64 UploadCount = 0;
    float LastOpacity = -1.f;
    void Composite(int32 Index, FLinearColor Source, float Alpha, bool bAdditive);
    void Upload();
};
