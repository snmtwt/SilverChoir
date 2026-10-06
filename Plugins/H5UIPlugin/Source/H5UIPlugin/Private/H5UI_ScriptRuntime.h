#pragma once

#include "CoreMinimal.h"

namespace Rml
{
	class ElementDocument;
}

struct FH5UI_ScriptSource
{
	FString Code;
	FString SourceURL;
	int32 SourceLine = 1;
};

class FH5UI_ScriptRuntime final
{
public:
	using FEmitCallback = TFunction<void(const FString&, const FString&, const FString&, const FString&)>;
	using FGetDataCallback = TFunction<FString(const FString&)>;
	using FSetDataCallback = TFunction<void(const FString&, const FString&)>;
	using FErrorCallback = TFunction<void(const FString&)>;
	using FActivityCallback = TFunction<void()>;

	FH5UI_ScriptRuntime(
		FEmitCallback InEmit,
		FGetDataCallback InGetData,
		FSetDataCallback InSetData,
		FErrorCallback InError,
		FActivityCallback InActivity);
	~FH5UI_ScriptRuntime();

	bool Initialize(
		Rml::ElementDocument* InDocument,
		const FString& InSourceURL,
		int64 MemoryLimitBytes,
		double ExecutionTimeLimitMilliseconds,
		double PromiseJobTimeLimitMilliseconds,
		double InitialExecutionTimeLimitMilliseconds,
		int32 MaxCallbacksPerFrame);
	void Shutdown();

	bool Execute(const FString& Script, const FString& SourceURL, FString& OutResult, FString& OutError);
	bool ExecutePageScripts(const TArray<FH5UI_ScriptSource>& Scripts);
	void BindInlineEventHandlers();
	void DispatchDocumentReady();
	void SetViewportMetrics(
		const FIntPoint& ViewSize,
		const FIntPoint& RenderSize,
		float DevicePixelRatio,
		float RenderScale,
		float EffectivePixelRatio);
	void DispatchViewportResize();
	bool FlushPendingJobs();
	bool Tick(double CurrentTimeSeconds);

	double GetNextWakeTimeSeconds() const;
	float GetLastExecutionMilliseconds() const;
	int64 GetMemoryUsageBytes() const;
	int32 GetTimerCount() const;
	bool IsValid() const;

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
	FIntPoint LastViewportSize = FIntPoint(-1, -1);
};
