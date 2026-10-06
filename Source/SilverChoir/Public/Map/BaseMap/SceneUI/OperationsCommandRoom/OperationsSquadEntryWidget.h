#pragma once
#include "Blueprint/UserWidget.h"
#include "Data/Squads/SquadStructs.h"
#include "OperationsSquadEntryWidget.generated.h"
class USelectionButtonWidget;
class UImage;
class UTextBlock;
class UVerticalBox;
class USizeBox;
class UPersonnelPortraitWidget;
class UPlayerSquadManagerBase;
struct FUnitData;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOperationsSquadExpansionRequested, FGuid, SquadId);

UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API UOperationsMemberEntryWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetMember(const FUnitData& Unit, bool bCaptain);
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<UPersonnelPortraitWidget> MemberPortrait;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<UTextBlock> MemberNameText;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<UTextBlock> CaptainText;
};

/** Designer-authored row; only presentation interpolation runs in Tick, data comes from canonical managers. */
UCLASS(Abstract, Blueprintable)
class SILVERCHOIR_API UOperationsSquadEntryWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetSquad(UPlayerSquadManagerBase* Manager, const FSquadData& Squad);
    UFUNCTION(BlueprintCallable, Category="小队") void RequestExpansion();
    UFUNCTION(BlueprintCallable, Category="小队") void SetExpanded(bool bValue, bool bAnimate = true);
    UPROPERTY(BlueprintAssignable, Category="小队") FOperationsSquadExpansionRequested OnExpansionRequested;
    UPROPERTY(BlueprintReadOnly, Category="小队") FGuid SquadId;
    UPROPERTY(BlueprintReadOnly, Category="小队") bool bExpanded = false;
    UPROPERTY(EditDefaultsOnly, Category="小队") TSubclassOf<UOperationsMemberEntryWidget> MemberEntryClass;
    UPROPERTY(EditDefaultsOnly, Category="小队") TObjectPtr<UTexture2D> NoVehicleTexture;
    UPROPERTY(EditDefaultsOnly, Category="小队", meta=(ClampMin="0.01")) float ExpandDuration = .2f;
    UPROPERTY(EditDefaultsOnly, Category="小队") float MemberRowHeight = 94.f;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<USelectionButtonWidget> HeaderButton;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<UImage> SquadIconImage;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<UImage> VehicleImage;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<UTextBlock> VehicleEmptyText;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<UTextBlock> SquadNameText;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<UTextBlock> MemberCountText;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<UTextBlock> ExpandText;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<USizeBox> MemberRevealSize;
    UPROPERTY(BlueprintReadOnly, Category="小队|绑定", meta=(BindWidget)) TObjectPtr<UVerticalBox> Members;
protected:
    virtual void NativeTick(const FGeometry& Geometry, float Delta) override;
private:
    float Reveal = 0.f;
    void UpdateReveal();
};
