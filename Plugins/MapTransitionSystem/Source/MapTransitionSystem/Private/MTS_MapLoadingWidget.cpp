#include "MTS_MapLoadingWidget.h"

void UMTS_MapLoadingWidget::ApplyTransitionPayload(const FMTS_MapTransitionPayload& Payload)
{
	CachedTransitionPayload = Payload;
	OnTransitionPayloadChanged(CachedTransitionPayload);
	OnTransitionProgressChanged(CachedTransitionPayload.Progress, CachedTransitionPayload.LoadingContent);
}
