#pragma once
#include "MTS_SubMapHandler.h"
#include "MTS_MapLoadingWidget.h"
#include "MTS_SubMapTestHandler.generated.h"

UCLASS(NotBlueprintable, Transient)
class UMTS_SubMapTestHandler : public UMTS_SubMapHandler
{
	GENERATED_BODY()
public:
	int32 PreloadCalls = 0;
	int32 LoadedCalls = 0;
	int32 FailureCalls = 0;
    int32 ReadyCalls = 0;
    int32 UnloadingCalls = 0;
    int32 UnloadedCalls = 0;
    int32 NativeCleanupCalls = 0;
    bool bNativeCleanupBeforeUnload = false;
    bool bNativeCleanupBeforeFailure = false;
    FName MustBeUnloadedBeforePreload;
    bool bPreviousMapWasRemoved = false;
    bool bQueryableDuringUnload = false;
	bool bHadWorld = false;
	int32 ProgressEvents = 0;
	FMTS_MapTransitionPayload LastPayload;
	FName ID;
	static inline int32 TotalFailureCalls = 0;
	UFUNCTION()
	void ObserveProgress(FName MapID, const FMTS_MapTransitionPayload& Payload)
	{
		if (MapID == ID) { ++ProgressEvents; LastPayload = Payload; }
	}
	virtual bool PreloadSubMapResources_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) override
	{
		++PreloadCalls;
		bHadWorld = GetWorld() == MapSubsystem->GetWorld();
		ID = Map.MapID;
        FMTS_SubMapInfo Previous;
        bPreviousMapWasRemoved = !MapSubsystem->GetSubMapByKey(MustBeUnloadedBeforePreload, Previous);
		MapSubsystem->OnSubMapProgressChanged.AddDynamic(this, &UMTS_SubMapTestHandler::ObserveProgress);
		return ID != TEXT("MTS_Test_Reject");
	}
	virtual void OnSubMapLoaded_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) override { ++LoadedCalls; }
	virtual void OnSubMapLoadFailed_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map, const FText& Error) override
    { ++FailureCalls; ++TotalFailureCalls; bNativeCleanupBeforeFailure=NativeCleanupCalls==1; }
    virtual void OnSubMapReady_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) override { ++ReadyCalls; }
    virtual void OnSubMapUnloading_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) override
    { ++UnloadingCalls; bNativeCleanupBeforeUnload=NativeCleanupCalls==1; }
    virtual void OnSubMapUnloaded_Implementation(UMTS_SubMapSubsystem* MapSubsystem, const FMTS_SubMapInfo& Map) override
    { ++UnloadedCalls; bQueryableDuringUnload=MapSubsystem->GetSubMapHandler(GetMapID())==this; }
protected:
    virtual void OnReleaseResources() override { ++NativeCleanupCalls; }
};

UCLASS(NotBlueprintable, Transient)
class UMTS_SubMapTestWidget : public UMTS_MapLoadingWidget
{
    GENERATED_BODY()
};
