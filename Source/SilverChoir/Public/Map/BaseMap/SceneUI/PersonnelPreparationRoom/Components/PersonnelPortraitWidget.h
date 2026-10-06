#pragma once
#include "Blueprint/UserWidget.h"
#include "UIBasic/PortraitLibrary.h"
#include "PersonnelPortraitWidget.generated.h"
class UTexture2D;
class UImage;
class UPanelWidget;

/** Shared portrait view. The Designer owns the frame (square or 3:4) and placeholder. */
UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API UPersonnelPortraitWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="头像") TObjectPtr<UTexture2D> PortraitTexture;
    UFUNCTION(BlueprintCallable, Category="头像") void SetPortraitTexture(UTexture2D* Texture);
    UFUNCTION(BlueprintCallable, Category="头像") void SetUnitPortrait(const FUnitProfile& Profile);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="头像") EPortraitFormat PortraitFormat = EPortraitFormat::Square;
protected:
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> PortraitImage;
    UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UPanelWidget> PortraitPlaceholder;
    virtual void NativePreConstruct() override;
private:
    bool bHasProfileBrush = false;
    FSlateBrush ProfileBrush;
    void ApplyPortraitBrush(const FSlateBrush& Brush);
};
