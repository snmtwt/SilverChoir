#include "UIBasic/MenuLoadingWidget.h"
#include "H5UI_View.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"

void UMenuLoadingWidget::ConfigureBackground()
{
	if (!BackgroundView) { return; }
	BackgroundView->URL = BackgroundURL;
	BackgroundView->bAutoLoad = false;
	BackgroundView->bReceiveInput = false;
	BackgroundView->bConsumeInput = false;
	BackgroundView->bEnableJavaScript = false;
	BackgroundView->bEnableBrowserSubviews = false;
	BackgroundView->TargetFrameRate = 60;
	BackgroundView->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UMenuLoadingWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ConfigureBackground();
	if (BackgroundView && IsDesignTime()) { BackgroundView->SynchronizeProperties(); BackgroundView->LoadURL(BackgroundURL); }
}

void UMenuLoadingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ConfigureBackground();
	if (BackgroundView) { BackgroundView->SynchronizeProperties(); BackgroundView->LoadURL(BackgroundURL); }
	RefreshProgress();
}

void UMenuLoadingWidget::NativeDestruct()
{
	if (BackgroundView) { BackgroundView->Close(); }
	Super::NativeDestruct();
}

void UMenuLoadingWidget::RefreshProgress()
{
	const auto& Payload = CachedTransitionPayload;
	const float Progress = FMath::IsFinite(Payload.Progress) ? FMath::Clamp(Payload.Progress, 0.f, 1.f) : 0.f;
	if (LoadingTitle) { LoadingTitle->SetText(Payload.LoadingTitle.IsEmpty() ? NSLOCTEXT("SilverChoir", "LoadingTitle", "正在部署") : Payload.LoadingTitle); }
	if (LoadingStatus) { LoadingStatus->SetText(Payload.LoadingContent.IsEmpty() ? NSLOCTEXT("SilverChoir", "LoadingWait", "建立地图连接…") : Payload.LoadingContent); }
	if (LoadingPercent) { LoadingPercent->SetText(FText::FromString(FString::Printf(TEXT("%02d%%"), FMath::RoundToInt(Progress * 100)))); }
	if (LoadingProgress) { LoadingProgress->SetPercent(Progress); }
}

void UMenuLoadingWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (ContentFrame)
	{
		if (auto* CanvasSlot = Cast<UCanvasPanelSlot>(ContentFrame->Slot))
		{
			const FVector2D Size = Geometry.GetLocalSize();
			const FVector2D Desired(FMath::Min(Size.X, Size.Y * 16.0 / 9.0), Size.Y);
			if (!CanvasSlot->GetSize().Equals(Desired, .5)) { CanvasSlot->SetSize(Desired); }
		}
	}
	RefreshProgress();
}
