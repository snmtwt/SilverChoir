#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BattleUnitSelectionComponent.generated.h"

class AUnitPawnBase;
class AGameMainMapPlayerController;
class AGameMainMapGameState;
class SWidget;
struct FUnitData;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBattleUnitSelectionChanged, const TArray<AUnitPawnBase*>&, SelectedUnits);

/** Local tactical selection. Blueprint routes input; unit data owns the selected state. */
UCLASS(ClassGroup=(Battle), meta=(BlueprintSpawnableComponent, DisplayName="战斗单位框选"))
class SILVERCHOIR_API UBattleUnitSelectionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UBattleUnitSelectionComponent();
    /** Reuse battle/map-transition/UI ownership rules for world-space commands. */
    bool CanIssueWorldCommand(bool bCheckPointer = true) const;
    UFUNCTION(BlueprintCallable, Category="战斗|单位选择", meta=(DisplayName="开始单位框选")) bool BeginBoxSelection();
    UFUNCTION(BlueprintCallable, Category="战斗|单位选择", meta=(DisplayName="完成单位框选")) void CompleteBoxSelection();
    UFUNCTION(BlueprintCallable, Category="战斗|单位选择", meta=(DisplayName="取消单位框选")) void CancelBoxSelection();
    UFUNCTION(BlueprintCallable, Category="战斗|单位选择", meta=(DisplayName="清空单位选择")) void ClearUnitSelection();
    UFUNCTION(BlueprintPure, Category="战斗|单位选择", meta=(DisplayName="获取已选单位")) TArray<AUnitPawnBase*> GetSelectedUnits() const;
    /** Viewport pixel coordinates, independent of UMG DPI. Returns visible/selectable subclasses only. */
    UFUNCTION(BlueprintPure, Category="战斗|单位选择", meta=(DisplayName="查询框内单位"))
    TArray<AUnitPawnBase*> FindUnitsInRectangle(FVector2D Start, FVector2D End) const;
    /** Also available to Blueprint UI commands. Invalid/inactive-map units are ignored. */
    UFUNCTION(BlueprintCallable, Category="战斗|单位选择", meta=(DisplayName="设置所选单位"))
    void SetSelectedUnits(const TArray<AUnitPawnBase*>& Units);
    /** UI business entry: selection may precede creation of the actual Pawn. */
    UFUNCTION(BlueprintCallable, Category="战斗|单位选择", meta=(DisplayName="根据单位ID单选单位"))
    bool SelectUnitById(FGuid UnitId);
    UPROPERTY(BlueprintAssignable, Category="战斗|单位选择", meta=(DisplayName="所选单位已改变")) FBattleUnitSelectionChanged OnSelectionChanged;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗|框选样式", meta=(DisplayName="拖拽阈值（像素）",ClampMin="1")) float DragThreshold = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗|框选样式", meta=(DisplayName="必须完整框住单位")) bool bRequireFullContainment = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗|框选样式", meta=(DisplayName="框选边线颜色")) FLinearColor BorderColor = FLinearColor(.05f,.8f,.94f,1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="战斗|框选样式", meta=(DisplayName="框选填充颜色")) FLinearColor FillColor = FLinearColor(.025f,.4f,.55f,.09f);
    UPROPERTY(BlueprintReadOnly, Transient, Category="战斗|单位选择") bool bSelecting = false;
    bool GetSelectionRectangle(FVector2D& OutStart, FVector2D& OutEnd) const;
    virtual void TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
protected:
    /** Viewport pixels. Overridable by native input adapters and runtime tests. */
    virtual bool ReadSelectionPointer(FVector2D& OutPosition) const;
    virtual bool IsPointerOverUI() const;
private:
    struct FSelectionSnapshot
    {
        TArray<TSharedPtr<FUnitData>> SelectedData;
        TArray<TWeakObjectPtr<AUnitPawnBase>> UnboundUnits;
        TSharedPtr<FUnitData> DataWithoutPawn;
        TWeakObjectPtr<AGameMainMapGameState> MapState;
        FName MapID;
    };
    AGameMainMapPlayerController* Controller() const;
    bool CanSelectInWorld() const;
    bool IsEligible(const AUnitPawnBase* Unit) const;
    bool ProjectUnit(const AUnitPawnBase* Unit, FBox2D& OutBounds) const;
    void UpdatePointer(bool bApplyLiveSelection = true);
    void FinishGesture();
    FSelectionSnapshot CaptureSelectionSnapshot() const;
    void RestoreSelectionSnapshot(const FSelectionSnapshot& Snapshot);
    void ProcessDeferredSelectionChanges();
    void RemoveOverlay();
    void ApplySelection(const TArray<AUnitPawnBase*>& Units, const TSharedPtr<FUnitData>& DataWithoutPawn, bool bRestoringSelection = false);
    UFUNCTION() void HandleSelectedUnitEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    FVector2D StartPosition = FVector2D::ZeroVector, EndPosition = FVector2D::ZeroVector;
    bool bDragged = false;
    bool bPreviewApplied = false;
    bool bChangingSelection = false;
    bool bProcessingDeferredChanges = false;
    bool bPendingClear = false;
    uint64 SelectionRevision = 0;
    TOptional<FSelectionSnapshot> DragStartSelection;
    TOptional<FSelectionSnapshot> PendingRestoreSelection;
    TSharedPtr<SWidget> Overlay;
    // Retain shared data until unselection even if its Pawn is unloaded first.
    TMap<TWeakObjectPtr<AUnitPawnBase>, TSharedPtr<FUnitData>> TrackedUnits;
    TSharedPtr<FUnitData> SelectedDataWithoutPawn;
};
