#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ECGWidget.generated.h"
class UH5UI_View;

/** Reusable H5UI heart monitor. Presentation only; gameplay supplies color, BPM and intensity. */
UCLASS(Abstract, Blueprintable, meta=(DisplayName="心电图"))
class SILVERCHOIR_API UECGWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetWaveColor, Category="心电图", meta=(DisplayName="颜色",ExposeOnSpawn=true))
    FLinearColor WaveColor = FLinearColor(FColor(41,220,242));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetHeartRate, Category="心电图", meta=(DisplayName="心率（次/分钟）",ClampMin="0",ClampMax="300",ExposeOnSpawn=true))
    float HeartRateBPM = 72.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetHeartbeatIntensity, Category="心电图", meta=(DisplayName="心跳强度（0为死亡平线）",ClampMin="0",ClampMax="2",ExposeOnSpawn=true))
    float HeartbeatIntensity = 1.f;
    /** Zero chooses an independent seed once per widget; nonzero reproduces the same starting phases. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetAnimationSeed, Category="心电图|动画", meta=(DisplayName="动画种子（0为自动）",ExposeOnSpawn=true))
    int32 AnimationSeed = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="心电图|样式", meta=(ClampMin="1",ClampMax="10")) float SweepSeconds=3.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="心电图|样式", meta=(ClampMin="0",ClampMax="2")) float GlowStrength=1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="心电图|样式") bool bShowGrid=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="心电图|样式") bool bShowBackground=true;
    UFUNCTION(BlueprintCallable, Category="心电图", meta=(DisplayName="设置心电图参数")) void SetECGParameters(FLinearColor Color,float BPM,float Intensity);
    UFUNCTION(BlueprintSetter, Category="心电图") void SetWaveColor(FLinearColor Color);
    UFUNCTION(BlueprintSetter, Category="心电图") void SetHeartRate(float BPM);
    UFUNCTION(BlueprintSetter, Category="心电图") void SetHeartbeatIntensity(float Intensity);
    UFUNCTION(BlueprintSetter, Category="心电图|动画", meta=(DisplayName="设置心电图动画种子")) void SetAnimationSeed(int32 Seed);
    UFUNCTION(BlueprintCallable, Category="心电图", meta=(DisplayName="刷新心电图样式")) void RefreshECG();
    virtual void SynchronizeProperties() override;
protected:
    virtual void NativePreConstruct() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="心电图") TObjectPtr<UH5UI_View> ECGView;
private:
    UFUNCTION() void HandleReady();
    void ConfigureView();
    void ResolveAnimationSeed();
    // Per-instance state, intentionally not copied/serialized with a Designer template.
    int32 ResolvedAnimationSeed = 0;
    int32 LastRequestedAnimationSeed = 0;
};
