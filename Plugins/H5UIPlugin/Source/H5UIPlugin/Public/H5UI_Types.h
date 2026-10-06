#pragma once

#include "CoreMinimal.h"
#include "H5UI_Types.generated.h"

UENUM(BlueprintType)
enum class EH5UI_ViewState : uint8
{
	Unloaded,
	Loading,
	Ready,
	Failed
};

USTRUCT(BlueprintType)
struct H5UIPLUGIN_API FH5UI_Event
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin")
	FName Name = NAME_None;

	/** Selects the event-handler class configured in H5 UI Plugin settings. NAME_None is a legacy untyped event. */
	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin")
	FName EventType = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin")
	FString Payload;

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin")
	FString ElementId;
};

USTRUCT(BlueprintType)
struct H5UIPLUGIN_API FH5UI_PerformanceStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	float UpdateMilliseconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	float SubmitMilliseconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	float JavaScriptMilliseconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	int64 JavaScriptHeapBytes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	int32 JavaScriptTimers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	int32 DrawBatches = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	int32 Vertices = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	int32 Triangles = 0;

	/** Vertices constrained by the CPU overflow-safety clip before Slate submission. */
	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	int32 ClampedVertices = 0;

	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	FIntPoint ViewSize = FIntPoint::ZeroValue;

	/** Estimated physical raster resolution after Slate/UMG DPI and RenderScale are applied. */
	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	FIntPoint RenderSize = FIntPoint::ZeroValue;

	/** Automatic physical-pixel scale derived from the widget's final on-screen Slate transform. */
	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	float ScreenPixelScale = 1.0f;

	/** User-provided resolution multiplier. */
	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	float RenderScale = 1.0f;

	/** Effective raster pixel ratio, including both automatic and user-provided scales. */
	UPROPERTY(BlueprintReadOnly, Category = "H5 UI Plugin|Performance")
	float EffectivePixelRatio = 1.0f;

};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FH5UI_ViewReadyDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FH5UI_ViewFailedDelegate, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FH5UI_UIEventDelegate, const FH5UI_Event&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FH5UI_JavaScriptErrorDelegate, const FString&, Error);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FH5UI_StatsDelegate, const FH5UI_PerformanceStats&, Stats);
