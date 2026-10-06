#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Styling/SlateBrush.h"
#include "Data/Units/UnitStructs.h"
#include "PortraitLibrary.generated.h"

UENUM(BlueprintType)
enum class EPortraitFormat : uint8 { Full UMETA(DisplayName="完整头像（竖版）"), Square UMETA(DisplayName="方形头像") };

/** One source image, two presentation brushes. UV cropping never duplicates or stretches a portrait. */
UCLASS(meta=(DisplayName="头像显示函数库"))
class SILVERCHOIR_API UPortraitLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="UI|头像", meta=(DisplayName="获取完整头像"))
    static FSlateBrush GetFullPortrait(const FUnitProfile& Profile);
    UFUNCTION(BlueprintPure, Category="UI|头像", meta=(DisplayName="获取方形头像"))
    static FSlateBrush GetSquarePortrait(const FUnitProfile& Profile);
    UFUNCTION(BlueprintPure, Category="UI|头像", meta=(DisplayName="从纹理获取头像画刷"))
    static FSlateBrush MakePortraitBrush(UTexture2D* Texture, EPortraitFormat Format = EPortraitFormat::Square, FVector2D Focus = FVector2D(0.5, 0.28), float SquareCropScale = 1.0f);
};
