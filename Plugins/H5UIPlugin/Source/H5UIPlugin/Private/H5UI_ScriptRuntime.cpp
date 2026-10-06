#include "H5UI_ScriptRuntime.h"
#include "H5UI_CanvasElement.h"
#include "H5UI_CanvasScripting.h"

#include "HAL/PlatformTime.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementText.h"
#include "RmlUi/Core/Elements/ElementFormControl.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"
#include "RmlUi/Core/Event.h"
#include "RmlUi/Core/EventListener.h"
#include "RmlUi/Core/ObserverPtr.h"
#include "RmlUi/Core/Property.h"
#include "RmlUi/Core/StyleSheetSpecification.h"
#include "RmlUi/Core/StringUtilities.h"
#include "quickjs.h"

DEFINE_LOG_CATEGORY_STATIC(LogH5UI_JavaScript, Log, All);

namespace
{
	Rml::String ScriptToRmlString(const FString& Value)
	{
		return Rml::String(TCHAR_TO_UTF8(*Value));
	}

	FString ScriptFromRmlString(const Rml::String& Value)
	{
		return UTF8_TO_TCHAR(Value.c_str());
	}

	JSValue NewJavaScriptString(JSContext* Context, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		return JS_NewStringLen(Context, Utf8.Get(), Utf8.Length());
	}

	FString JavaScriptValueToString(JSContext* Context, JSValueConst Value)
	{
		const char* Utf8 = JS_ToCString(Context, Value);
		if (!Utf8)
		{
			return FString();
		}
		const FString Result = UTF8_TO_TCHAR(Utf8);
		JS_FreeCString(Context, Utf8);
		return Result;
	}

	FString JavaScriptValueToPayload(JSContext* Context, JSValueConst Value)
	{
		if (JS_IsUndefined(Value) || JS_IsNull(Value))
		{
			return FString();
		}
		if (JS_IsString(Value))
		{
			return JavaScriptValueToString(Context, Value);
		}

		if (JS_IsObject(Value))
		{
			JSValue Json = JS_JSONStringify(Context, Value, JS_UNDEFINED, JS_UNDEFINED);
			if (!JS_IsException(Json) && !JS_IsUndefined(Json))
			{
				const FString Result = JavaScriptValueToString(Context, Json);
				JS_FreeValue(Context, Json);
				return Result;
			}
			JS_FreeValue(Context, Json);
		}
		return JavaScriptValueToString(Context, Value);
	}

	FString GetElementTextContent(Rml::Element* Element)
	{
		if (!Element)
		{
			return FString();
		}
		if (Rml::ElementText* Text = rmlui_dynamic_cast<Rml::ElementText*>(Element))
		{
			return ScriptFromRmlString(Text->GetText());
		}

		FString Result;
		for (int32 ChildIndex = 0; ChildIndex < Element->GetNumChildren(); ++ChildIndex)
		{
			Result += GetElementTextContent(Element->GetChild(ChildIndex));
		}
		return Result;
	}

	bool ReadJavaScriptBoolProperty(JSContext* Context, JSValueConst Object, const char* Name, bool DefaultValue)
	{
		if (!JS_IsObject(Object))
		{
			return DefaultValue;
		}
		JSValue Value = JS_GetPropertyStr(Context, Object, Name);
		if (JS_IsException(Value) || JS_IsUndefined(Value))
		{
			JS_FreeValue(Context, Value);
			return DefaultValue;
		}
		const int32 BoolValue = JS_ToBool(Context, Value);
		JS_FreeValue(Context, Value);
		return BoolValue < 0 ? DefaultValue : BoolValue != 0;
	}
}

struct FH5UI_ScriptRuntime::FImpl
{
	struct FElementHandle
	{
		FElementHandle(FImpl& InOwner, Rml::Element& InElement)
			: Owner(&InOwner)
			, Element(InElement.GetObserverPtr())
		{
		}

		FImpl* Owner;
		Rml::ObserverPtr<Rml::Element> Element;
	};

	struct FTimer
	{
		int32 Id = 0;
		double DueTimeSeconds = 0.0;
		double IntervalSeconds = 0.0;
		bool bRepeat = false;
		bool bAnimationFrame = false;
		uint64 EarliestTick = 0;
		JSValue Callback = JS_UNDEFINED;
		TArray<JSValue> Arguments;
	};

	class FH5UI_Listener final : public Rml::EventListener
	{
	public:
		FH5UI_Listener(
			FImpl& InOwner,
			Rml::Element* InElement,
			const Rml::String& InType,
			JSValueConst InCallback,
			bool bInCapture,
			bool bInOnce)
			: Owner(InOwner)
			, Element(InElement->GetObserverPtr())
			, Type(InType)
			, NativeType(InType == "wheel" ? Rml::String("mousescroll") : InType)
			, Callback(JS_DupValue(InOwner.Context, InCallback))
			, bCapture(bInCapture)
			, bOnce(bInOnce)
		{
			if (Rml::Element* Target = Element.get())
			{
				Target->AddEventListener(NativeType, this, bCapture);
			}
		}

		virtual ~FH5UI_Listener() override
		{
			if (Rml::Element* Target = Element.get())
			{
				Target->RemoveEventListener(NativeType, this, bCapture);
			}
			if (Owner.Context)
			{
				JS_FreeValue(Owner.Context, Callback);
			}
		}

		virtual void ProcessEvent(Rml::Event& Event) override
		{
			Owner.CallEventListener(*this, Event);
		}

		FImpl& Owner;
		Rml::ObserverPtr<Rml::Element> Element;
		Rml::String Type;
		Rml::String NativeType;
		JSValue Callback = JS_UNDEFINED;
		bool bCapture = false;
		bool bOnce = false;
		bool bPendingRemoval = false;
	};

	explicit FImpl(
		FH5UI_ScriptRuntime::FEmitCallback InEmit,
		FH5UI_ScriptRuntime::FGetDataCallback InGetData,
		FH5UI_ScriptRuntime::FSetDataCallback InSetData,
		FH5UI_ScriptRuntime::FErrorCallback InError,
		FH5UI_ScriptRuntime::FActivityCallback InActivity)
		: EmitCallback(MoveTemp(InEmit))
		, GetDataCallback(MoveTemp(InGetData))
		, SetDataCallback(MoveTemp(InSetData))
		, ErrorCallback(MoveTemp(InError))
		, ActivityCallback(MoveTemp(InActivity))
	{
	}

	~FImpl()
	{
		Shutdown();
	}

	bool Initialize(
		Rml::ElementDocument* InDocument,
		const FString& InSourceURL,
		int64 MemoryLimitBytes,
		double InExecutionTimeLimitMilliseconds,
		double InPromiseJobTimeLimitMilliseconds,
		double InInitialExecutionTimeLimitMilliseconds,
		int32 InMaxCallbacksPerFrame)
	{
		Shutdown();
		Document = InDocument;
		SourceURL = InSourceURL;
		ExecutionTimeLimitMilliseconds = FMath::Clamp(InExecutionTimeLimitMilliseconds, 0.1, 100.0);
		PromiseJobTimeLimitMilliseconds = FMath::Clamp(InPromiseJobTimeLimitMilliseconds, 0.1, 100.0);
		InitialExecutionTimeLimitMilliseconds = FMath::Clamp(InInitialExecutionTimeLimitMilliseconds, 1.0, 500.0);
		MaxCallbacksPerFrame = FMath::Clamp(InMaxCallbacksPerFrame, 1, 10000);

		Runtime = JS_NewRuntime();
		if (!Runtime)
		{
			ReportError(TEXT("Unable to create the QuickJS runtime."));
			return false;
		}
		JS_SetMemoryLimit(Runtime, static_cast<size_t>(FMath::Max<int64>(1024 * 1024, MemoryLimitBytes)));
		JS_SetMaxStackSize(Runtime, 1024 * 1024);
		JS_SetInterruptHandler(Runtime, &FImpl::InterruptHandler, this);

		Context = JS_NewContext(Runtime);
		if (!Context)
		{
			ReportError(TEXT("Unable to create the QuickJS context."));
			Shutdown();
			return false;
		}
		JS_SetContextOpaque(Context, this);

		JS_NewClassID(Runtime, &ElementClassId);
		JSClassDef ElementClass{};
		ElementClass.class_name = "SilverHTMLElement";
		ElementClass.finalizer = &FImpl::ElementFinalizer;
		if (JS_NewClass(Runtime, ElementClassId, &ElementClass) < 0)
		{
			ReportError(TEXT("Unable to register the JavaScript Element class."));
			Shutdown();
			return false;
		}

		JSValue ElementPrototype = JS_NewObject(Context);
		SetFunction(ElementPrototype, "__get", &FImpl::ElementGet, 1);
		SetFunction(ElementPrototype, "__set", &FImpl::ElementSet, 2);
		SetFunction(ElementPrototype, "getAttribute", &FImpl::ElementGetAttribute, 1);
		SetFunction(ElementPrototype, "setAttribute", &FImpl::ElementSetAttribute, 2);
		SetFunction(ElementPrototype, "hasAttribute", &FImpl::ElementHasAttribute, 1);
		SetFunction(ElementPrototype, "removeAttribute", &FImpl::ElementRemoveAttribute, 1);
		SetFunction(ElementPrototype, "querySelector", &FImpl::ElementQuerySelector, 1);
		SetFunction(ElementPrototype, "querySelectorAll", &FImpl::ElementQuerySelectorAll, 1);
		SetFunction(ElementPrototype, "matches", &FImpl::ElementMatches, 1);
		SetFunction(ElementPrototype, "closest", &FImpl::ElementClosest, 1);
		SetFunction(ElementPrototype, "getBoundingClientRect", &FImpl::ElementGetBoundingClientRect, 0);
		SetFunction(ElementPrototype, "addEventListener", &FImpl::ElementAddEventListener, 2);
		SetFunction(ElementPrototype, "removeEventListener", &FImpl::ElementRemoveEventListener, 2);
		SetFunction(ElementPrototype, "dispatchEvent", &FImpl::ElementDispatchEvent, 1);
		SetFunction(ElementPrototype, "click", &FImpl::ElementClick, 0);
		SetFunction(ElementPrototype, "focus", &FImpl::ElementFocus, 0);
		SetFunction(ElementPrototype, "blur", &FImpl::ElementBlur, 0);
		SetFunction(ElementPrototype, "scrollIntoView", &FImpl::ElementScrollIntoView, 0);
		SetFunction(ElementPrototype, "appendChild", &FImpl::ElementAppendChild, 1);
		SetFunction(ElementPrototype, "cloneNode", &FImpl::ElementCloneNode, 1);
		SetFunction(ElementPrototype, "insertBefore", &FImpl::ElementInsertBefore, 2);
		SetFunction(ElementPrototype, "replaceChild", &FImpl::ElementReplaceChild, 2);
		SetFunction(ElementPrototype, "removeChild", &FImpl::ElementRemoveChild, 1);
		SetFunction(ElementPrototype, "remove", &FImpl::ElementRemove, 0);
		SetFunction(ElementPrototype, "createElement", &FImpl::DocumentCreateElement, 1);
		SetFunction(ElementPrototype, "createTextNode", &FImpl::DocumentCreateTextNode, 1);
		SetFunction(ElementPrototype, "createComment", &FImpl::DocumentCreateComment, 1);
		SetFunction(ElementPrototype, "elementFromPoint", &FImpl::DocumentElementFromPoint, 2);
		SetFunction(ElementPrototype, "__getStyle", &FImpl::ElementGetStyle, 1);
		SetFunction(ElementPrototype, "__getComputedStyle", &FImpl::ElementGetComputedStyle, 1);
		SetFunction(ElementPrototype, "__setStyle", &FImpl::ElementSetStyle, 2);
		SetFunction(ElementPrototype, "__removeStyle", &FImpl::ElementRemoveStyle, 1);
		SetFunction(ElementPrototype, "__canvas", &FImpl::CanvasCommand, 1);
		JS_SetClassProto(Context, ElementClassId, ElementPrototype);

		JSValue Global = JS_GetGlobalObject(Context);
		JS_SetPropertyStr(Context, Global, "document", WrapElement(Document));
		SetFunction(Global, "__silverConsole", &FImpl::ConsoleMessage, 1);
		SetFunction(Global, "__silverStopPropagation", &FImpl::StopEventPropagation, 1);
		SetFunction(Global, "__silverNow", &FImpl::PerformanceNow, 0);
		SetFunction(Global, "clearTimeout", &FImpl::ClearTimer, 1);
		SetFunction(Global, "clearInterval", &FImpl::ClearTimer, 1);
		SetFunction(Global, "cancelAnimationFrame", &FImpl::ClearTimer, 1);
		SetMagicFunction(Global, "setTimeout", &FImpl::SetTimer, 2, 0);
		SetMagicFunction(Global, "setInterval", &FImpl::SetTimer, 2, 1);
		SetMagicFunction(Global, "requestAnimationFrame", &FImpl::SetTimer, 1, 2);
		SetFunction(Global, "__silverUeEmit", &FImpl::UeEmit, 2);
		SetFunction(Global, "__silverUeEmitTyped", &FImpl::UeEmitTyped, 4);
		SetFunction(Global, "__silverUeGetData", &FImpl::UeGetData, 1);
		SetFunction(Global, "__silverUeSetData", &FImpl::UeSetData, 2);
		JS_FreeValue(Context, Global);

		FString BootstrapResult;
		FString BootstrapError;
		// The runtime bootstrap defines the DOM facade used by every page and is
		// comparable to a page's initial script bundle, not a frame-time callback.
		// Using the regular callback budget here made startup intermittently fail
		// under editor load before any page script or event handler could run.
		if (!EvaluateInternal(
			GetBootstrapSource() + H5UICanvasBootstrap(),
			TEXT("h5ui://runtime/bootstrap.js"),
			BootstrapResult,
			BootstrapError,
			false,
			InitialExecutionTimeLimitMilliseconds))
		{
			ReportError(FString::Printf(TEXT("JavaScript bootstrap failed: %s"), *BootstrapError));
			Shutdown();
			return false;
		}
		Global = JS_GetGlobalObject(Context);
		JSValue EventConstructor = JS_GetPropertyStr(Context, Global, "Event");
		EventPrototype = JS_GetPropertyStr(Context, EventConstructor, "prototype");
		JS_FreeValue(Context, EventConstructor);
		JS_FreeValue(Context, Global);
		StartTimeSeconds = FPlatformTime::Seconds();
		return true;
	}

	void Shutdown()
	{
		if (!Runtime && !Context)
		{
			Document = nullptr;
			return;
		}

		bShuttingDown = true;
		ActiveEvents.Reset();
		Listeners.Reset();
		for (TPair<int32, FTimer>& Pair : Timers)
		{
			FreeTimer(Pair.Value);
		}
		Timers.Reset();
		ActiveTimerIds.Reset();
		CancelledTimerIds.Reset();

		if (Context)
		{
			for (TPair<Rml::Element*, JSValue>& Pair : ElementObjects)
			{
				JS_FreeValue(Context, Pair.Value);
			}
			ElementObjects.Reset();
			JS_FreeValue(Context, EventPrototype);
			EventPrototype = JS_UNDEFINED;
			JS_SetContextOpaque(Context, nullptr);
		}
		DetachedElements.Reset();

		if (Context)
		{
			JS_FreeContext(Context);
			Context = nullptr;
		}
		if (Runtime)
		{
			JS_FreeRuntime(Runtime);
			Runtime = nullptr;
		}

		Document = nullptr;
		ElementClassId = 0;
		bShuttingDown = false;
	}

	bool Execute(const FString& Script, const FString& InSourceURL, FString& OutResult, FString& OutError)
	{
		return EvaluateInternal(Script, InSourceURL, OutResult, OutError, true);
	}

	bool ExecutePageScripts(const TArray<FH5UI_ScriptSource>& Scripts)
	{
		bool bAllSucceeded = true;
		for (const FH5UI_ScriptSource& Script : Scripts)
		{
			FString PaddedSource;
			PaddedSource.Reserve(Script.Code.Len() + FMath::Max(0, Script.SourceLine - 1));
			for (int32 Line = 1; Line < Script.SourceLine; ++Line)
			{
				PaddedSource.AppendChar(TEXT('\n'));
			}
			PaddedSource += Script.Code;

			FString Result;
			FString Error;
			if (!EvaluateInternal(PaddedSource, Script.SourceURL, Result, Error, true, InitialExecutionTimeLimitMilliseconds))
			{
				bAllSucceeded = false;
			}
		}
		return bAllSucceeded;
	}

	void BindInlineEventHandlers()
	{
		FString Result;
		FString Error;
		EvaluateInternal(
			TEXT("globalThis.__silverBindInlineHandlers();"),
			TEXT("h5ui://runtime/inline-events.js"),
			Result,
			Error,
			true);
	}

	void DispatchDocumentReady()
	{
		if (!Document)
		{
			return;
		}
		const Rml::Dictionary Parameters;
		Document->DispatchEvent("DOMContentLoaded", Parameters, false, false);
		Document->DispatchEvent("load", Parameters, false, false);
		DrainPendingJobs();
	}

	bool Tick(double CurrentTimeSeconds)
	{
		if (!Context)
		{
			return false;
		}

		++TickSerial;
		const double StartSeconds = FPlatformTime::Seconds();
		bool bExecuted = false;
		int32 CallbackCount = 0;
		while (CallbackCount < MaxCallbacksPerFrame)
		{
			int32 DueTimerId = INDEX_NONE;
			double EarliestDue = TNumericLimits<double>::Max();
			for (const TPair<int32, FTimer>& Pair : Timers)
			{
				// A request made from rAF is for the next frame, even if the caller's
				// clock is ahead of wall time. Never spin the animation in one tick.
				if (Pair.Value.bAnimationFrame && Pair.Value.EarliestTick > TickSerial) { continue; }
				if (Pair.Value.DueTimeSeconds < EarliestDue)
				{
					EarliestDue = Pair.Value.DueTimeSeconds;
					DueTimerId = Pair.Key;
				}
			}
			if (DueTimerId == INDEX_NONE || EarliestDue > CurrentTimeSeconds)
			{
				break;
			}

			FTimer Timer;
			if (!Timers.RemoveAndCopyValue(DueTimerId, Timer))
			{
				continue;
			}
			ActiveTimerIds.Add(Timer.Id);
			CancelledTimerIds.Remove(Timer.Id);

			TArray<JSValue> CallArguments;
			if (Timer.bAnimationFrame)
			{
				CallArguments.Add(JS_NewFloat64(Context, (CurrentTimeSeconds - StartTimeSeconds) * 1000.0));
			}
			else
			{
				CallArguments.Reserve(Timer.Arguments.Num());
				for (JSValueConst Argument : Timer.Arguments)
				{
					CallArguments.Add(JS_DupValue(Context, Argument));
				}
			}

			JSValue Global = JS_GetGlobalObject(Context);
			FString Error;
			CallFunctionWithBudget(
				Timer.Callback,
				Global,
				CallArguments,
				TEXT("timer callback"),
				Error);
			JS_FreeValue(Context, Global);
			for (JSValue Argument : CallArguments)
			{
				JS_FreeValue(Context, Argument);
			}

			const bool bCancelled = CancelledTimerIds.Contains(Timer.Id);
			ActiveTimerIds.Remove(Timer.Id);
			CancelledTimerIds.Remove(Timer.Id);
			if (Timer.bRepeat && !bCancelled)
			{
				Timer.DueTimeSeconds = CurrentTimeSeconds + Timer.IntervalSeconds;
				Timers.Add(Timer.Id, MoveTemp(Timer));
			}
			else
			{
				FreeTimer(Timer);
			}

			++CallbackCount;
			bExecuted = true;
		}

		bExecuted |= DrainPendingJobs();
		LastExecutionMilliseconds = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		if (bExecuted && ActivityCallback)
		{
			ActivityCallback();
		}
		return bExecuted;
	}

	double GetNextWakeTimeSeconds() const
	{
		double Result = TNumericLimits<double>::Max();
		for (const TPair<int32, FTimer>& Pair : Timers)
		{
			Result = FMath::Min(Result, Pair.Value.DueTimeSeconds);
		}
		return Result;
	}

	int64 GetMemoryUsageBytes() const
	{
		if (!Runtime)
		{
			return 0;
		}
		JSMemoryUsage Usage{};
		JS_ComputeMemoryUsage(Runtime, &Usage);
		return static_cast<int64>(Usage.memory_used_size);
	}

	void SetFunction(JSValueConst Object, const char* Name, JSCFunction* Function, int32 Length)
	{
		JS_SetPropertyStr(Context, Object, Name, JS_NewCFunction(Context, Function, Name, Length));
	}

	void SetMagicFunction(JSValueConst Object, const char* Name, JSCFunctionMagic* Function, int32 Length, int32 Magic)
	{
		JS_SetPropertyStr(
			Context,
			Object,
			Name,
			JS_NewCFunctionMagic(Context, Function, Name, Length, JS_CFUNC_generic_magic, Magic));
	}

	JSValue WrapElement(Rml::Element* Element)
	{
		if (!Context || !Element)
		{
			return JS_NULL;
		}
		if (JSValue* Existing = ElementObjects.Find(Element))
		{
			FElementHandle* ExistingHandle = static_cast<FElementHandle*>(JS_GetOpaque(*Existing, ElementClassId));
			if (ExistingHandle && ExistingHandle->Element.get() == Element)
			{
				return JS_DupValue(Context, *Existing);
			}
			JS_FreeValue(Context, *Existing);
			ElementObjects.Remove(Element);
		}

		JSValue Object = JS_NewObjectClass(Context, ElementClassId);
		if (JS_IsException(Object))
		{
			return Object;
		}
		FElementHandle* Handle = new FElementHandle(*this, *Element);
		JS_SetOpaque(Object, Handle);
		ElementObjects.Add(Element, JS_DupValue(Context, Object));
		return Object;
	}

	Rml::Element* GetElement(JSContext* InContext, JSValueConst Object, bool bThrow = true) const
	{
		FElementHandle* Handle = static_cast<FElementHandle*>(JS_GetOpaque(Object, ElementClassId));
		Rml::Element* Element = Handle && Handle->Owner == this ? Handle->Element.get() : nullptr;
		if (!Element && bThrow)
		{
			JS_ThrowTypeError(InContext, "Element is no longer attached to a live H5 UI document.");
		}
		return Element;
	}

	void MarkActivity() const
	{
		if (ActivityCallback)
		{
			ActivityCallback();
		}
	}

	void ReportError(const FString& Error) const
	{
		UE_LOG(LogH5UI_JavaScript, Error, TEXT("%s"), *Error);
		if (ErrorCallback)
		{
			ErrorCallback(Error);
		}
	}

	FString TakeException()
	{
		JSValue Exception = JS_GetException(Context);
		FString Message = JavaScriptValueToString(Context, Exception);
		JSValue Stack = JS_GetPropertyStr(Context, Exception, "stack");
		if (!JS_IsException(Stack) && !JS_IsUndefined(Stack))
		{
			const FString StackText = JavaScriptValueToString(Context, Stack);
			if (!StackText.IsEmpty() && !StackText.Equals(Message))
			{
				Message += TEXT("\n") + StackText;
			}
		}
		JS_FreeValue(Context, Stack);
		JS_FreeValue(Context, Exception);
		return Message.IsEmpty() ? TEXT("Unknown JavaScript exception.") : Message;
	}

	bool EvaluateInternal(
		const FString& Script,
		const FString& InSourceURL,
		FString& OutResult,
		FString& OutError,
		bool bReportError,
		double BudgetMilliseconds = -1.0)
	{
		OutResult.Reset();
		OutError.Reset();
		if (!Context)
		{
			OutError = TEXT("JavaScript is not initialized for this view.");
			if (bReportError)
			{
				ReportError(OutError);
			}
			return false;
		}

		const FTCHARToUTF8 SourceUtf8(*Script);
		const FTCHARToUTF8 URLUtf8(*InSourceURL);
		const double StartSeconds = FPlatformTime::Seconds();
		const double EffectiveBudgetMilliseconds = BudgetMilliseconds > 0.0 ? BudgetMilliseconds : ExecutionTimeLimitMilliseconds;
		ExecutionDeadlineSeconds = StartSeconds + EffectiveBudgetMilliseconds / 1000.0;
		bExecutionActive = true;
		JSValue Result = JS_Eval(
			Context,
			SourceUtf8.Get(),
			SourceUtf8.Length(),
			URLUtf8.Get(),
			JS_EVAL_TYPE_GLOBAL);
		bExecutionActive = false;
		LastExecutionMilliseconds = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);

		if (JS_IsException(Result))
		{
			OutError = TakeException();
			JS_FreeValue(Context, Result);
			if (bReportError)
			{
				ReportError(OutError);
			}
			return false;
		}

		OutResult = JavaScriptValueToPayload(Context, Result);
		JS_FreeValue(Context, Result);
		DrainPendingJobs();
		MarkActivity();
		return true;
	}

	bool CallFunctionWithBudget(
		JSValueConst Function,
		JSValueConst ThisValue,
		TArray<JSValue>& Arguments,
		const FString& Label,
		FString& OutError)
	{
		const double StartSeconds = FPlatformTime::Seconds();
		ExecutionDeadlineSeconds = StartSeconds + ExecutionTimeLimitMilliseconds / 1000.0;
		bExecutionActive = true;
		JSValue Result = JS_Call(
			Context,
			Function,
			ThisValue,
			Arguments.Num(),
			Arguments.GetData());
		bExecutionActive = false;
		LastExecutionMilliseconds = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		if (JS_IsException(Result))
		{
			OutError = FString::Printf(TEXT("%s failed: %s"), *Label, *TakeException());
			JS_FreeValue(Context, Result);
			ReportError(OutError);
			return false;
		}
		JS_FreeValue(Context, Result);
		return true;
	}

	bool DrainPendingJobs()
	{
		if (!Runtime || !Context)
		{
			return false;
		}
		bool bExecuted = false;
		const double Deadline = FPlatformTime::Seconds() + PromiseJobTimeLimitMilliseconds / 1000.0;
		for (int32 JobIndex = 0; JobIndex < MaxCallbacksPerFrame && FPlatformTime::Seconds() < Deadline; ++JobIndex)
		{
			JSContext* JobContext = nullptr;
			ExecutionDeadlineSeconds = Deadline;
			bExecutionActive = true;
			const int32 Result = JS_ExecutePendingJob(Runtime, &JobContext);
			bExecutionActive = false;
			if (Result == 0)
			{
				break;
			}
			bExecuted = true;
			if (Result < 0)
			{
				ReportError(FString::Printf(TEXT("Promise job failed: %s"), *TakeException()));
				break;
			}
		}
		return bExecuted;
	}

	void FreeTimer(FTimer& Timer)
	{
		if (!Context)
		{
			return;
		}
		JS_FreeValue(Context, Timer.Callback);
		Timer.Callback = JS_UNDEFINED;
		for (JSValue Argument : Timer.Arguments)
		{
			JS_FreeValue(Context, Argument);
		}
		Timer.Arguments.Reset();
	}

	void CallEventListener(FH5UI_Listener& Listener, Rml::Event& Event)
	{
		if (!Context || Listener.bPendingRemoval)
		{
			return;
		}

		++DispatchDepth;
		const int32 NativeEventId = NextNativeEventId++;
		ActiveEvents.Add(NativeEventId, &Event);

		JSValue EventObject = JS_NewObject(Context);
		if (!JS_IsUndefined(EventPrototype))
		{
			JS_SetPrototype(Context, EventObject, EventPrototype);
		}
		JS_SetPropertyStr(Context, EventObject, "type", NewJavaScriptString(Context, ScriptFromRmlString(Listener.Type)));
		JS_SetPropertyStr(Context, EventObject, "target", WrapElement(Event.GetTargetElement()));
		JS_SetPropertyStr(Context, EventObject, "currentTarget", WrapElement(Event.GetCurrentElement()));
		JS_SetPropertyStr(Context, EventObject, "eventPhase", JS_NewInt32(Context, static_cast<int32>(Event.GetPhase())));
		JS_SetPropertyStr(Context, EventObject, "__nativeId", JS_NewInt32(Context, NativeEventId));
		const Rml::String Detail = Event.GetParameter<Rml::String>("detail", "");
		JS_SetPropertyStr(Context, EventObject, "detail", NewJavaScriptString(Context, ScriptFromRmlString(Detail)));
		JS_SetPropertyStr(Context, EventObject, "clientX", JS_NewFloat64(Context, Event.GetParameter<float>("mouse_x", 0.0f)));
		JS_SetPropertyStr(Context, EventObject, "clientY", JS_NewFloat64(Context, Event.GetParameter<float>("mouse_y", 0.0f)));
		JS_SetPropertyStr(Context, EventObject, "screenX", JS_NewFloat64(Context, Event.GetParameter<float>("mouse_x", 0.0f)));
		JS_SetPropertyStr(Context, EventObject, "screenY", JS_NewFloat64(Context, Event.GetParameter<float>("mouse_y", 0.0f)));
		JS_SetPropertyStr(Context, EventObject, "button", JS_NewInt32(Context, Event.GetParameter<int>("button", 0)));
		JS_SetPropertyStr(Context, EventObject, "deltaX", JS_NewFloat64(Context, Event.GetParameter<float>("wheel_delta_x", 0.0f)));
		JS_SetPropertyStr(Context, EventObject, "deltaY", JS_NewFloat64(Context, Event.GetParameter<float>("wheel_delta_y", 0.0f)));
		JS_SetPropertyStr(Context, EventObject, "ctrlKey", JS_NewBool(Context, Event.GetParameter<int>("ctrl_key", 0) != 0));
		JS_SetPropertyStr(Context, EventObject, "shiftKey", JS_NewBool(Context, Event.GetParameter<int>("shift_key", 0) != 0));
		JS_SetPropertyStr(Context, EventObject, "altKey", JS_NewBool(Context, Event.GetParameter<int>("alt_key", 0) != 0));
		JS_SetPropertyStr(Context, EventObject, "metaKey", JS_NewBool(Context, Event.GetParameter<int>("meta_key", 0) != 0));

		TArray<JSValue> Arguments;
		Arguments.Add(JS_DupValue(Context, EventObject));
		JSValue ThisObject = WrapElement(Event.GetCurrentElement());
		FString Error;
		CallFunctionWithBudget(Listener.Callback, ThisObject, Arguments, TEXT("event listener"), Error);
		JS_FreeValue(Context, ThisObject);
		JS_FreeValue(Context, Arguments[0]);
		JS_FreeValue(Context, EventObject);

		ActiveEvents.Remove(NativeEventId);
		if (Listener.bOnce)
		{
			Listener.bPendingRemoval = true;
		}
		--DispatchDepth;
		if (DispatchDepth == 0)
		{
			FlushRemovedListeners();
		}
	}

	void FlushRemovedListeners()
	{
		for (int32 ListenerIndex = Listeners.Num() - 1; ListenerIndex >= 0; --ListenerIndex)
		{
			if (Listeners[ListenerIndex]->bPendingRemoval || !Listeners[ListenerIndex]->Element)
			{
				Listeners.RemoveAt(ListenerIndex);
			}
		}
	}

	static FImpl* GetSelf(JSContext* InContext)
	{
		return static_cast<FImpl*>(JS_GetContextOpaque(InContext));
	}

	static int InterruptHandler(JSRuntime*, void* Opaque)
	{
		FImpl* Self = static_cast<FImpl*>(Opaque);
		return Self && Self->bExecutionActive && FPlatformTime::Seconds() > Self->ExecutionDeadlineSeconds ? 1 : 0;
	}

	static void ElementFinalizer(JSRuntime*, JSValueConst Value)
	{
		FElementHandle* Handle = static_cast<FElementHandle*>(JS_GetOpaque(Value, JS_GetClassID(Value)));
		delete Handle;
	}

	static JSValue CanvasCommand(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		auto* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		auto* Canvas = Element ? rmlui_dynamic_cast<FH5UI_CanvasElement*>(Element) : nullptr;
		if (!Canvas) { return JS_ThrowTypeError(InContext, "Canvas command requires a live canvas element."); }
		return H5UICanvasCall(InContext, *Canvas, ArgCount, Arguments);
	}

	static JSValue ElementGet(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 1)
		{
			return JS_EXCEPTION;
		}
		const FString Name = JavaScriptValueToString(InContext, Arguments[0]);
		if (Name == TEXT("id"))
		{
			return NewJavaScriptString(InContext, ScriptFromRmlString(Element->GetId()));
		}
		if (Name == TEXT("className"))
		{
			return NewJavaScriptString(InContext, ScriptFromRmlString(Element->GetClassNames()));
		}
		if (Name == TEXT("innerHTML"))
		{
			return NewJavaScriptString(InContext, ScriptFromRmlString(Element->GetInnerRML()));
		}
		if (Name == TEXT("textContent"))
		{
			return NewJavaScriptString(InContext, GetElementTextContent(Element));
		}
		if (Name == TEXT("tagName"))
		{
			return NewJavaScriptString(InContext, ScriptFromRmlString(Element->GetTagName()).ToUpper());
		}
		if (Name == TEXT("value"))
		{
			if (Rml::ElementFormControl* FormControl = rmlui_dynamic_cast<Rml::ElementFormControl*>(Element))
			{
				return NewJavaScriptString(InContext, ScriptFromRmlString(FormControl->GetValue()));
			}
			return NewJavaScriptString(InContext, ScriptFromRmlString(Element->GetAttribute<Rml::String>("value", "")));
		}
		if (Name == TEXT("checked"))
		{
			return JS_NewBool(InContext, Element->HasAttribute("checked"));
		}
		if (Name == TEXT("disabled"))
		{
			return JS_NewBool(InContext, Element->HasAttribute("disabled"));
		}
		if (Name == TEXT("src") || Name == TEXT("alt") || Name == TEXT("href") ||
			Name == TEXT("title") || Name == TEXT("draggable"))
		{
			return NewJavaScriptString(
				InContext,
				ScriptFromRmlString(Element->GetAttribute<Rml::String>(ScriptToRmlString(Name), "")));
		}
		if (Name == TEXT("parentElement") || Name == TEXT("parentNode"))
		{
			Rml::Element* Parent = Element->GetParentNode();
			return Parent && Parent != Element->GetOwnerDocument()->GetParentNode() ? Self->WrapElement(Parent) : JS_NULL;
		}
		if (Name == TEXT("children") || Name == TEXT("childNodes"))
		{
			JSValue Result = JS_NewArray(InContext);
			uint32 ArrayIndex = 0;
			for (int32 ChildIndex = 0; ChildIndex < Element->GetNumChildren(); ++ChildIndex)
			{
				Rml::Element* Child = Element->GetChild(ChildIndex);
				if (Child && (Name == TEXT("childNodes") || Child->GetTagName() != "#text"))
				{
					JS_SetPropertyUint32(InContext, Result, ArrayIndex++, Self->WrapElement(Child));
				}
			}
			return Result;
		}
		if (Name == TEXT("firstChild"))
		{
			return Element->GetNumChildren() > 0 ? Self->WrapElement(Element->GetChild(0)) : JS_NULL;
		}
		if (Name == TEXT("firstElementChild"))
		{
			for (int32 ChildIndex = 0; ChildIndex < Element->GetNumChildren(); ++ChildIndex)
			{
				Rml::Element* Child = Element->GetChild(ChildIndex);
				if (Child && Child->GetTagName() != "#text")
				{
					return Self->WrapElement(Child);
				}
			}
			return JS_NULL;
		}
		if (Name == TEXT("lastChild"))
		{
			return Element->GetNumChildren() > 0 ? Self->WrapElement(Element->GetChild(Element->GetNumChildren() - 1)) : JS_NULL;
		}
		if (Name == TEXT("lastElementChild"))
		{
			for (int32 ChildIndex = Element->GetNumChildren() - 1; ChildIndex >= 0; --ChildIndex)
			{
				Rml::Element* Child = Element->GetChild(ChildIndex);
				if (Child && Child->GetTagName() != "#text")
				{
					return Self->WrapElement(Child);
				}
			}
			return JS_NULL;
		}
		if (Name == TEXT("nextSibling"))
		{
			return Self->WrapElement(Element->GetNextSibling());
		}
		if (Name == TEXT("previousSibling"))
		{
			return Self->WrapElement(Element->GetPreviousSibling());
		}
		if (Name == TEXT("nodeType"))
		{
			const Rml::String& TagName = Element->GetTagName();
			return JS_NewInt32(InContext, TagName == "#text" ? 3 : (TagName == "silver-comment" ? 8 : 1));
		}
		if (Name == TEXT("nodeName"))
		{
			const Rml::String& TagName = Element->GetTagName();
			return NewJavaScriptString(InContext, TagName == "#text" ? TEXT("#text") : ScriptFromRmlString(TagName).ToUpper());
		}
		if (Name == TEXT("childElementCount"))
		{
			int32 Count = 0;
			for (int32 ChildIndex = 0; ChildIndex < Element->GetNumChildren(); ++ChildIndex)
			{
				Count += Element->GetChild(ChildIndex)->GetTagName() != "#text" ? 1 : 0;
			}
			return JS_NewInt32(InContext, Count);
		}
		if (Name == TEXT("ownerDocument") || Name == TEXT("body") || Name == TEXT("documentElement"))
		{
			return Self->WrapElement(Self->Document);
		}
		if (Name == TEXT("isConnected"))
		{
			Rml::Element* Cursor = Element;
			while (Cursor && Cursor != Self->Document)
			{
				Cursor = Cursor->GetParentNode();
			}
			return JS_NewBool(InContext, Cursor == Self->Document);
		}
		if (Name == TEXT("scrollTop") || Name == TEXT("scrollLeft") || Name == TEXT("scrollWidth") ||
			Name == TEXT("scrollHeight") || Name == TEXT("clientWidth") || Name == TEXT("clientHeight") ||
			Name == TEXT("offsetWidth") || Name == TEXT("offsetHeight"))
		{
			if (Rml::ElementDocument* OwnerDocument = Element->GetOwnerDocument())
			{
				OwnerDocument->UpdateDocument();
			}
		}
		if (Name == TEXT("scrollTop"))
		{
			return JS_NewFloat64(InContext, Element->GetScrollTop());
		}
		if (Name == TEXT("scrollLeft"))
		{
			return JS_NewFloat64(InContext, Element->GetScrollLeft());
		}
		if (Name == TEXT("scrollWidth"))
		{
			return JS_NewFloat64(InContext, Element->GetScrollWidth());
		}
		if (Name == TEXT("scrollHeight"))
		{
			return JS_NewFloat64(InContext, Element->GetScrollHeight());
		}
		if (Name == TEXT("clientWidth"))
		{
			return JS_NewFloat64(InContext, Element->GetClientWidth());
		}
		if (Name == TEXT("clientHeight"))
		{
			return JS_NewFloat64(InContext, Element->GetClientHeight());
		}
		if (Name == TEXT("offsetWidth"))
		{
			return JS_NewFloat64(InContext, Element->GetOffsetWidth());
		}
		if (Name == TEXT("offsetHeight"))
		{
			return JS_NewFloat64(InContext, Element->GetOffsetHeight());
		}
		return JS_UNDEFINED;
	}

	static JSValue ElementSet(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 2)
		{
			return JS_EXCEPTION;
		}
		const FString Name = JavaScriptValueToString(InContext, Arguments[0]);
		const FString Value = JavaScriptValueToString(InContext, Arguments[1]);
		if (Name == TEXT("id"))
		{
			Element->SetId(ScriptToRmlString(Value));
		}
		else if (Name == TEXT("className"))
		{
			Element->SetClassNames(ScriptToRmlString(Value));
		}
		else if (Name == TEXT("innerHTML"))
		{
			Element->SetInnerRML(ScriptToRmlString(Value));
		}
		else if (Name == TEXT("textContent"))
		{
			Element->SetInnerRML(Rml::StringUtilities::EncodeRml(ScriptToRmlString(Value)));
		}
		else if (Name == TEXT("value"))
		{
			if (Rml::ElementFormControl* FormControl = rmlui_dynamic_cast<Rml::ElementFormControl*>(Element))
			{
				FormControl->SetValue(ScriptToRmlString(Value));
			}
			else
			{
				Element->SetAttribute("value", ScriptToRmlString(Value));
			}
		}
		else if (Name == TEXT("checked") || Name == TEXT("disabled"))
		{
			const bool bEnabled = JS_ToBool(InContext, Arguments[1]) > 0;
			if (bEnabled)
			{
				Element->SetAttribute(ScriptToRmlString(Name), "");
			}
			else
			{
				Element->RemoveAttribute(ScriptToRmlString(Name));
			}
		}
		else if (Name == TEXT("src") || Name == TEXT("alt") || Name == TEXT("href") ||
			Name == TEXT("title") || Name == TEXT("draggable"))
		{
			Element->SetAttribute(ScriptToRmlString(Name), ScriptToRmlString(Value));
		}
		else if (Name == TEXT("scrollTop") || Name == TEXT("scrollLeft"))
		{
			double NumericValue = 0.0;
			if (JS_ToFloat64(InContext, &NumericValue, Arguments[1]) < 0)
			{
				return JS_EXCEPTION;
			}
			if (Name == TEXT("scrollTop"))
			{
				Element->SetScrollTop(static_cast<float>(NumericValue));
			}
			else
			{
				Element->SetScrollLeft(static_cast<float>(NumericValue));
			}
		}
		Self->MarkActivity();
		return JS_DupValue(InContext, Arguments[1]);
	}

	static JSValue ElementGetAttribute(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 1)
		{
			return JS_EXCEPTION;
		}
		const Rml::String Name = ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0]));
		return Element->HasAttribute(Name)
			? NewJavaScriptString(InContext, ScriptFromRmlString(Element->GetAttribute<Rml::String>(Name, "")))
			: JS_NULL;
	}

	static JSValue ElementSetAttribute(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 2)
		{
			return JS_EXCEPTION;
		}
		Element->SetAttribute(
			ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0])),
			ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[1])));
		Self->MarkActivity();
		return JS_UNDEFINED;
	}

	static JSValue ElementHasAttribute(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		return Element && ArgCount > 0
			? JS_NewBool(InContext, Element->HasAttribute(ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0]))))
			: JS_EXCEPTION;
	}

	static JSValue ElementRemoveAttribute(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 1)
		{
			return JS_EXCEPTION;
		}
		Element->RemoveAttribute(ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0])));
		Self->MarkActivity();
		return JS_UNDEFINED;
	}

	static JSValue ElementQuerySelector(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 1)
		{
			return JS_EXCEPTION;
		}
		return Self->WrapElement(Element->QuerySelector(ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0]))));
	}

	static JSValue ElementQuerySelectorAll(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 1)
		{
			return JS_EXCEPTION;
		}
		Rml::ElementList Matches;
		Element->QuerySelectorAll(Matches, ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0])));
		JSValue Result = JS_NewArray(InContext);
		for (uint32 MatchIndex = 0; MatchIndex < static_cast<uint32>(Matches.size()); ++MatchIndex)
		{
			JS_SetPropertyUint32(InContext, Result, MatchIndex, Self->WrapElement(Matches[MatchIndex]));
		}
		return Result;
	}

	static JSValue ElementMatches(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		return Element && ArgCount > 0
			? JS_NewBool(InContext, Element->Matches(ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0]))))
			: JS_EXCEPTION;
	}

	static JSValue ElementClosest(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		return Element && ArgCount > 0
			? Self->WrapElement(Element->Closest(ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0]))))
			: JS_EXCEPTION;
	}

	static JSValue ElementGetBoundingClientRect(
		JSContext* InContext,
		JSValueConst ThisValue,
		int ArgCount,
		JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element)
		{
			return JS_EXCEPTION;
		}
		if (Rml::ElementDocument* OwnerDocument = Element->GetOwnerDocument())
		{
			OwnerDocument->UpdateDocument();
		}

		const Rml::Vector2f Position = Element->GetAbsoluteOffset(Rml::BoxArea::Border);
		const Rml::Vector2f Size = Element->GetBox().GetSize(Rml::BoxArea::Border);
		JSValue Rect = JS_NewObject(InContext);
		JS_SetPropertyStr(InContext, Rect, "x", JS_NewFloat64(InContext, Position.x));
		JS_SetPropertyStr(InContext, Rect, "y", JS_NewFloat64(InContext, Position.y));
		JS_SetPropertyStr(InContext, Rect, "left", JS_NewFloat64(InContext, Position.x));
		JS_SetPropertyStr(InContext, Rect, "top", JS_NewFloat64(InContext, Position.y));
		JS_SetPropertyStr(InContext, Rect, "width", JS_NewFloat64(InContext, Size.x));
		JS_SetPropertyStr(InContext, Rect, "height", JS_NewFloat64(InContext, Size.y));
		JS_SetPropertyStr(InContext, Rect, "right", JS_NewFloat64(InContext, Position.x + Size.x));
		JS_SetPropertyStr(InContext, Rect, "bottom", JS_NewFloat64(InContext, Position.y + Size.y));
		return Rect;
	}

	static JSValue ElementAddEventListener(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 2 || !JS_IsFunction(InContext, Arguments[1]))
		{
			return JS_ThrowTypeError(InContext, "addEventListener requires an event name and a function.");
		}
		bool bCapture = false;
		bool bOnce = false;
		if (ArgCount > 2)
		{
			if (JS_IsBool(Arguments[2]))
			{
				bCapture = JS_ToBool(InContext, Arguments[2]) > 0;
			}
			else
			{
				bCapture = ReadJavaScriptBoolProperty(InContext, Arguments[2], "capture", false);
				bOnce = ReadJavaScriptBoolProperty(InContext, Arguments[2], "once", false);
			}
		}
		const Rml::String Type = ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0]));
		Self->Listeners.Add(MakeUnique<FH5UI_Listener>(*Self, Element, Type, Arguments[1], bCapture, bOnce));
		return JS_UNDEFINED;
	}

	static JSValue ElementRemoveEventListener(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 2)
		{
			return JS_EXCEPTION;
		}
		const Rml::String Type = ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0]));
		const bool bCapture = ArgCount > 2 && (JS_IsBool(Arguments[2])
			? JS_ToBool(InContext, Arguments[2]) > 0
			: ReadJavaScriptBoolProperty(InContext, Arguments[2], "capture", false));
		for (const TUniquePtr<FH5UI_Listener>& Listener : Self->Listeners)
		{
			if (Listener->Element.get() == Element && Listener->Type == Type && Listener->bCapture == bCapture &&
				JS_IsStrictEqual(InContext, Listener->Callback, Arguments[1]))
			{
				Listener->bPendingRemoval = true;
				break;
			}
		}
		if (Self->DispatchDepth == 0)
		{
			Self->FlushRemovedListeners();
		}
		return JS_UNDEFINED;
	}

	static JSValue ElementDispatchEvent(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 1 || !JS_IsObject(Arguments[0]))
		{
			return JS_ThrowTypeError(InContext, "dispatchEvent requires an Event object.");
		}
		JSValue TypeValue = JS_GetPropertyStr(InContext, Arguments[0], "type");
		const FString Type = JavaScriptValueToString(InContext, TypeValue);
		JS_FreeValue(InContext, TypeValue);
		JSValue DetailValue = JS_GetPropertyStr(InContext, Arguments[0], "detail");
		const FString Detail = JavaScriptValueToPayload(InContext, DetailValue);
		JS_FreeValue(InContext, DetailValue);
		if (Type.IsEmpty())
		{
			return JS_ThrowTypeError(InContext, "Event type cannot be empty.");
		}
		Rml::Dictionary Parameters;
		Parameters["detail"] = ScriptToRmlString(Detail);
		Self->MarkActivity();
		return JS_NewBool(InContext, Element->DispatchEvent(ScriptToRmlString(Type), Parameters));
	}

	static JSValue ElementClick(JSContext* InContext, JSValueConst ThisValue, int, JSValueConst*)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element)
		{
			return JS_EXCEPTION;
		}
		Element->Click();
		Self->MarkActivity();
		return JS_UNDEFINED;
	}

	static JSValue ElementFocus(JSContext* InContext, JSValueConst ThisValue, int, JSValueConst*)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		return Element ? JS_NewBool(InContext, Element->Focus(true)) : JS_EXCEPTION;
	}

	static JSValue ElementBlur(JSContext* InContext, JSValueConst ThisValue, int, JSValueConst*)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element)
		{
			return JS_EXCEPTION;
		}
		Element->Blur();
		return JS_UNDEFINED;
	}

	static JSValue ElementScrollIntoView(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element)
		{
			return JS_EXCEPTION;
		}
		const bool bAlignTop = ArgCount == 0 || JS_ToBool(InContext, Arguments[0]) != 0;
		Element->ScrollIntoView(bAlignTop);
		Self->MarkActivity();
		return JS_UNDEFINED;
	}

	Rml::ElementPtr DetachElement(Rml::Element* Element)
	{
		if (!Element || Element == Document)
		{
			return Rml::ElementPtr();
		}
		if (Rml::Element* Parent = Element->GetParentNode())
		{
			return Parent->RemoveChild(Element);
		}
		Rml::ElementPtr Result;
		DetachedElements.RemoveAndCopyValue(Element, Result);
		return Result;
	}

	static JSValue ElementAppendChild(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Parent = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		Rml::Element* Child = Self && ArgCount > 0 ? Self->GetElement(InContext, Arguments[0]) : nullptr;
		if (!Parent || !Child || Parent == Child)
		{
			return JS_ThrowTypeError(InContext, "appendChild requires a different live Element.");
		}
		Rml::ElementPtr OwnedChild = Self->DetachElement(Child);
		if (!OwnedChild)
		{
			return JS_ThrowInternalError(InContext, "Unable to transfer child element ownership.");
		}
		Parent->AppendChild(MoveTemp(OwnedChild));
		Self->MarkActivity();
		return JS_DupValue(InContext, Arguments[0]);
	}

	static JSValue ElementCloneNode(JSContext* InContext, JSValueConst ThisValue, int, JSValueConst*)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element)
		{
			return JS_EXCEPTION;
		}

		Rml::ElementPtr Clone = Element->Clone();
		if (!Clone)
		{
			return JS_ThrowInternalError(InContext, "Unable to clone Element.");
		}

		Rml::Element* RawClone = Clone.get();
		Self->DetachedElements.Add(RawClone, MoveTemp(Clone));
		Self->MarkActivity();
		return Self->WrapElement(RawClone);
	}

	static JSValue ElementInsertBefore(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Parent = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		Rml::Element* Child = Self && ArgCount > 0 ? Self->GetElement(InContext, Arguments[0]) : nullptr;
		Rml::Element* Anchor = Self && ArgCount > 1 && !JS_IsNull(Arguments[1]) && !JS_IsUndefined(Arguments[1])
			? Self->GetElement(InContext, Arguments[1])
			: nullptr;
		if (!Parent || !Child || Parent == Child || (Anchor && Anchor->GetParentNode() != Parent))
		{
			return JS_ThrowTypeError(InContext, "insertBefore requires a live child and an anchor owned by the parent.");
		}
		Rml::ElementPtr OwnedChild = Self->DetachElement(Child);
		if (!OwnedChild)
		{
			return JS_ThrowInternalError(InContext, "Unable to transfer child element ownership.");
		}
		if (Anchor)
		{
			Parent->InsertBefore(MoveTemp(OwnedChild), Anchor);
		}
		else
		{
			Parent->AppendChild(MoveTemp(OwnedChild));
		}
		Self->MarkActivity();
		return JS_DupValue(InContext, Arguments[0]);
	}

	static JSValue ElementReplaceChild(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Parent = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		Rml::Element* Child = Self && ArgCount > 0 ? Self->GetElement(InContext, Arguments[0]) : nullptr;
		Rml::Element* Replaced = Self && ArgCount > 1 ? Self->GetElement(InContext, Arguments[1]) : nullptr;
		if (!Parent || !Child || !Replaced || Parent == Child || Replaced->GetParentNode() != Parent)
		{
			return JS_ThrowTypeError(InContext, "replaceChild requires a live child and a direct child to replace.");
		}
		Rml::ElementPtr OwnedChild = Self->DetachElement(Child);
		if (!OwnedChild)
		{
			return JS_ThrowInternalError(InContext, "Unable to transfer child element ownership.");
		}
		Rml::ElementPtr Removed = Parent->ReplaceChild(MoveTemp(OwnedChild), Replaced);
		Self->DetachedElements.Add(Replaced, MoveTemp(Removed));
		Self->MarkActivity();
		return JS_DupValue(InContext, Arguments[1]);
	}

	static JSValue ElementRemoveChild(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Parent = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		Rml::Element* Child = Self && ArgCount > 0 ? Self->GetElement(InContext, Arguments[0]) : nullptr;
		if (!Parent || !Child || Child->GetParentNode() != Parent)
		{
			return JS_ThrowTypeError(InContext, "removeChild requires a direct child Element.");
		}
		Rml::ElementPtr OwnedChild = Parent->RemoveChild(Child);
		Self->DetachedElements.Add(Child, MoveTemp(OwnedChild));
		Self->MarkActivity();
		return JS_DupValue(InContext, Arguments[0]);
	}

	static JSValue ElementRemove(JSContext* InContext, JSValueConst ThisValue, int, JSValueConst*)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element)
		{
			return JS_EXCEPTION;
		}
		if (Rml::Element* Parent = Element->GetParentNode())
		{
			Rml::ElementPtr OwnedElement = Parent->RemoveChild(Element);
			Self->DetachedElements.Add(Element, MoveTemp(OwnedElement));
			Self->MarkActivity();
		}
		return JS_UNDEFINED;
	}

	static JSValue DocumentCreateElement(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		if (!Self || !Self->Document || ArgCount < 1)
		{
			return JS_EXCEPTION;
		}
		Rml::ElementPtr Element = Self->Document->CreateElement(ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0])));
		if (!Element)
		{
			return JS_ThrowInternalError(InContext, "Unable to create the requested element.");
		}
		Rml::Element* RawElement = Element.get();
		Self->DetachedElements.Add(RawElement, MoveTemp(Element));
		return Self->WrapElement(RawElement);
	}

	static JSValue DocumentCreateTextNode(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		if (!Self || !Self->Document || ArgCount < 1)
		{
			return JS_EXCEPTION;
		}
		Rml::ElementPtr Element = Self->Document->CreateTextNode(ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0])));
		if (!Element)
		{
			return JS_ThrowInternalError(InContext, "Unable to create a text node.");
		}
		Rml::Element* RawElement = Element.get();
		Self->DetachedElements.Add(RawElement, MoveTemp(Element));
		return Self->WrapElement(RawElement);
	}

	static JSValue DocumentCreateComment(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		if (!Self || !Self->Document)
		{
			return JS_EXCEPTION;
		}
		Rml::ElementPtr Element = Self->Document->CreateElement("silver-comment");
		if (!Element)
		{
			return JS_ThrowInternalError(InContext, "Unable to create a comment anchor.");
		}
		Element->SetProperty("display", "none");
		if (ArgCount > 0)
		{
			Element->SetAttribute("data-comment", ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0])));
		}
		Rml::Element* RawElement = Element.get();
		Self->DetachedElements.Add(RawElement, MoveTemp(Element));
		return Self->WrapElement(RawElement);
	}

	static JSValue DocumentElementFromPoint(
		JSContext* InContext,
		JSValueConst,
		int ArgCount,
		JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		if (!Self || !Self->Document || ArgCount < 2)
		{
			return JS_NULL;
		}

		double X = 0.0;
		double Y = 0.0;
		if (JS_ToFloat64(InContext, &X, Arguments[0]) < 0 ||
			JS_ToFloat64(InContext, &Y, Arguments[1]) < 0)
		{
			return JS_EXCEPTION;
		}

		Rml::Context* RmlContext = Self->Document->GetContext();
		if (!RmlContext)
		{
			return JS_NULL;
		}

		Rml::Element* HitElement = RmlContext->GetElementAtPoint(Rml::Vector2f(
			static_cast<float>(X),
			static_cast<float>(Y)));
		return HitElement ? Self->WrapElement(HitElement) : JS_NULL;
	}

	static JSValue ElementGetStyle(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 1)
		{
			return JS_EXCEPTION;
		}
		// HTMLElement.style exposes only the element's inline declaration. Returning
		// the cascaded value here breaks framework directives such as Vue v-show:
		// they may preserve a stylesheet's current display value as an inline style,
		// permanently overriding display:flex/grid when the element is shown again.
		const Rml::PropertyId PropertyId = Rml::StyleSheetSpecification::GetPropertyId(
			ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0])));
		const Rml::PropertyMap& InlineProperties = Element->GetLocalStyleProperties();
		const auto PropertyIterator = InlineProperties.find(PropertyId);
		const Rml::Property* Property = PropertyIterator != InlineProperties.end() ? &PropertyIterator->second : nullptr;
		return Property ? NewJavaScriptString(InContext, ScriptFromRmlString(Property->ToString())) : NewJavaScriptString(InContext, FString());
	}

	static JSValue ElementSetStyle(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 2)
		{
			return JS_EXCEPTION;
		}
		const Rml::String PropertyName = ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0]));
		const FString PropertyValue = JavaScriptValueToString(InContext, Arguments[1]);
		// Assigning an empty string to a CSSStyleDeclaration property removes that
		// inline declaration in browsers. This is how Vue restores stylesheet layout
		// after v-show changes display to none.
		const bool bSucceeded = PropertyValue.IsEmpty()
			? (Element->RemoveProperty(PropertyName), true)
			: Element->SetProperty(PropertyName, ScriptToRmlString(PropertyValue));
		Self->MarkActivity();
		return JS_NewBool(InContext, bSucceeded);
	}

	static JSValue ElementGetComputedStyle(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 1)
		{
			return JS_EXCEPTION;
		}
		const Rml::Property* Property = Element->GetProperty(
			ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0])));
		return Property ? NewJavaScriptString(InContext, ScriptFromRmlString(Property->ToString())) : NewJavaScriptString(InContext, FString());
	}

	static JSValue ElementRemoveStyle(JSContext* InContext, JSValueConst ThisValue, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		Rml::Element* Element = Self ? Self->GetElement(InContext, ThisValue) : nullptr;
		if (!Element || ArgCount < 1)
		{
			return JS_EXCEPTION;
		}
		Element->RemoveProperty(ScriptToRmlString(JavaScriptValueToString(InContext, Arguments[0])));
		Self->MarkActivity();
		return JS_UNDEFINED;
	}

	static JSValue ConsoleMessage(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments)
	{
		const FString Level = ArgCount > 0 ? JavaScriptValueToString(InContext, Arguments[0]) : TEXT("log");
		FString Message;
		for (int32 Index = 1; Index < ArgCount; ++Index)
		{
			if (!Message.IsEmpty())
			{
				Message += TEXT(" ");
			}
			Message += JavaScriptValueToPayload(InContext, Arguments[Index]);
		}
		if (Level == TEXT("error"))
		{
			UE_LOG(LogH5UI_JavaScript, Error, TEXT("%s"), *Message);
		}
		else if (Level == TEXT("warn"))
		{
			UE_LOG(LogH5UI_JavaScript, Warning, TEXT("%s"), *Message);
		}
		else
		{
			UE_LOG(LogH5UI_JavaScript, Log, TEXT("%s"), *Message);
		}
		return JS_UNDEFINED;
	}

	static JSValue StopEventPropagation(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		int32 EventId = 0;
		if (!Self || ArgCount < 1 || JS_ToInt32(InContext, &EventId, Arguments[0]) < 0)
		{
			return JS_EXCEPTION;
		}
		if (Rml::Event** Event = Self->ActiveEvents.Find(EventId))
		{
			(*Event)->StopPropagation();
		}
		return JS_UNDEFINED;
	}

	static JSValue PerformanceNow(JSContext* InContext, JSValueConst, int, JSValueConst*)
	{
		FImpl* Self = GetSelf(InContext);
		return JS_NewFloat64(InContext, Self ? (FPlatformTime::Seconds() - Self->StartTimeSeconds) * 1000.0 : 0.0);
	}

	static JSValue SetTimer(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments, int Magic)
	{
		FImpl* Self = GetSelf(InContext);
		if (!Self || ArgCount < 1 || !JS_IsFunction(InContext, Arguments[0]))
		{
			return JS_ThrowTypeError(InContext, "Timer callback must be a function.");
		}
		double DelayMilliseconds = 0.0;
		if (Magic != 2 && ArgCount > 1 && JS_ToFloat64(InContext, &DelayMilliseconds, Arguments[1]) < 0)
		{
			return JS_EXCEPTION;
		}

		FTimer Timer;
		Timer.Id = Self->NextTimerId++;
		Timer.IntervalSeconds = FMath::Max(0.0, DelayMilliseconds) / 1000.0;
		Timer.DueTimeSeconds = FPlatformTime::Seconds() + (Magic == 2 ? 0.0 : Timer.IntervalSeconds);
		Timer.bRepeat = Magic == 1;
		Timer.bAnimationFrame = Magic == 2;
		Timer.EarliestTick = Self->TickSerial + 1;
		Timer.Callback = JS_DupValue(InContext, Arguments[0]);
		const int32 FirstArgumentIndex = Magic == 2 ? 1 : 2;
		for (int32 Index = FirstArgumentIndex; Index < ArgCount; ++Index)
		{
			Timer.Arguments.Add(JS_DupValue(InContext, Arguments[Index]));
		}
		Self->Timers.Add(Timer.Id, MoveTemp(Timer));
		Self->MarkActivity();
		return JS_NewInt32(InContext, Self->NextTimerId - 1);
	}

	static JSValue ClearTimer(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		int32 TimerId = 0;
		if (!Self || ArgCount < 1 || JS_ToInt32(InContext, &TimerId, Arguments[0]) < 0)
		{
			return JS_EXCEPTION;
		}
		if (Self->ActiveTimerIds.Contains(TimerId))
		{
			Self->CancelledTimerIds.Add(TimerId);
		}
		FTimer Timer;
		if (Self->Timers.RemoveAndCopyValue(TimerId, Timer))
		{
			Self->FreeTimer(Timer);
		}
		return JS_UNDEFINED;
	}

	static JSValue UeEmit(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		if (!Self || ArgCount < 1)
		{
			return JS_ThrowTypeError(InContext, "ue.emit requires an event name.");
		}
		const FString Name = JavaScriptValueToString(InContext, Arguments[0]);
		const FString Payload = ArgCount > 1 ? JavaScriptValueToPayload(InContext, Arguments[1]) : FString();
		const FString ElementId = ArgCount > 2 ? JavaScriptValueToString(InContext, Arguments[2]) : FString();
		if (Self->EmitCallback)
		{
			Self->EmitCallback(FString(), Name, Payload, ElementId);
		}
		return JS_UNDEFINED;
	}

	static JSValue UeEmitTyped(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		if (!Self || ArgCount < 2)
		{
			return JS_ThrowTypeError(InContext, "Typed H5 UI events require an event type and function name.");
		}
		if (Self->EmitCallback)
		{
			const FString EventType = JavaScriptValueToString(InContext, Arguments[0]);
			const FString Name = JavaScriptValueToString(InContext, Arguments[1]);
			const FString Payload = ArgCount > 2 ? JavaScriptValueToPayload(InContext, Arguments[2]) : FString();
			const FString ElementId = ArgCount > 3 ? JavaScriptValueToString(InContext, Arguments[3]) : FString();
			Self->EmitCallback(EventType, Name, Payload, ElementId);
		}
		return JS_UNDEFINED;
	}

	static JSValue UeGetData(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		if (!Self || ArgCount < 1)
		{
			return JS_UNDEFINED;
		}
		return NewJavaScriptString(
			InContext,
			Self->GetDataCallback ? Self->GetDataCallback(JavaScriptValueToString(InContext, Arguments[0])) : FString());
	}

	static JSValue UeSetData(JSContext* InContext, JSValueConst, int ArgCount, JSValueConst* Arguments)
	{
		FImpl* Self = GetSelf(InContext);
		if (!Self || ArgCount < 2)
		{
			return JS_ThrowTypeError(InContext, "ue.setData requires a name and value.");
		}
		if (Self->SetDataCallback)
		{
			Self->SetDataCallback(
				JavaScriptValueToString(InContext, Arguments[0]),
				JavaScriptValueToPayload(InContext, Arguments[1]));
		}
		Self->MarkActivity();
		return JS_UNDEFINED;
	}

	static const FString& GetBootstrapSource()
	{
		static const FString Source = []()
		{
			FString Result = UTF8_TO_TCHAR(R"JS(
(function () {
  'use strict';
  const documentObject = globalThis.document;
  const elementPrototype = Object.getPrototypeOf(documentObject);
  function Element() { throw new TypeError('Element objects are created by H5 UI Plugin.'); }
  Element.prototype = elementPrototype;
  Object.defineProperty(Element.prototype, 'constructor', { value: Element });
  globalThis.Element = Element;
  globalThis.HTMLElement = Element;
  globalThis.Node = Element;

  const writableProperties = ['id', 'className', 'innerHTML', 'textContent', 'value', 'checked', 'disabled',
    'src', 'alt', 'href', 'title', 'draggable', 'scrollTop', 'scrollLeft'];
  const readonlyProperties = ['tagName', 'nodeName', 'nodeType', 'parentElement', 'parentNode', 'children', 'childNodes',
    'firstChild', 'lastChild', 'firstElementChild', 'lastElementChild', 'nextSibling', 'previousSibling', 'childElementCount', 'ownerDocument', 'isConnected',
    'scrollWidth', 'scrollHeight', 'clientWidth', 'clientHeight', 'offsetWidth', 'offsetHeight'];
  for (const name of writableProperties) {
    Object.defineProperty(Element.prototype, name, {
      configurable: true,
      get() { return this.__get(name); },
      set(value) { this.__set(name, value); }
    });
  }
  for (const name of readonlyProperties) {
    Object.defineProperty(Element.prototype, name, { configurable: true, get() { return this.__get(name); } });
  }
  Object.defineProperty(Element.prototype, 'innerText', {
    configurable: true,
    get() { return this.textContent; },
    set(value) { this.textContent = value; }
  });
  Object.defineProperty(documentObject, 'body', { configurable: true, get() { return documentObject; } });
  Object.defineProperty(documentObject, 'documentElement', { configurable: true, get() { return documentObject; } });
  Object.defineProperty(documentObject, 'readyState', { configurable: true, get() { return 'complete'; } });
  documentObject.getElementById = function (id) { return this.querySelector('#' + String(id)); };
  documentObject.createElementNS = function (namespace, tagName) { return this.createElement(tagName); };
  Element.prototype.contains = function (candidate) {
    for (let node = candidate; node; node = node.parentNode) if (node === this) return true;
    return false;
  };

  const styleObjects = new WeakMap();
  const computedStyleObjects = new WeakMap();
  const toCssName = (name) => String(name).replace(/[A-Z]/g, c => '-' + c.toLowerCase());
  Object.defineProperty(Element.prototype, 'style', {
    configurable: true,
    get() {
      let style = styleObjects.get(this);
      if (!style) {
        const element = this;
        const methods = {
          getPropertyValue(name) { return element.__getStyle(String(name)); },
          setProperty(name, value) { element.__setStyle(String(name), value == null ? '' : String(value)); },
          removeProperty(name) { const old = element.__getStyle(String(name)); element.__removeStyle(String(name)); return old; }
        };
        style = new Proxy(methods, {
          get(target, name) { return name in target ? target[name] : element.__getStyle(toCssName(name)); },
          set(target, name, value) { element.__setStyle(toCssName(name), value == null ? '' : String(value)); return true; }
        });
        styleObjects.set(this, style);
      }
      return style;
    }
  });

  globalThis.getComputedStyle = function (element) {
    if (!element || typeof element.__getComputedStyle !== 'function') throw new TypeError('getComputedStyle expects an Element');
    let style = computedStyleObjects.get(element);
    if (!style) {
      const methods = { getPropertyValue(name) { return element.__getComputedStyle(String(name)); } };
      style = new Proxy(methods, {
        get(target, name) { return name in target ? target[name] : element.__getComputedStyle(toCssName(name)); }
      });
      computedStyleObjects.set(element, style);
    }
    return style;
  };

  Object.defineProperty(Element.prototype, 'classList', {
    configurable: true,
    get() {
      const element = this;
      const read = () => new Set(String(element.className || '').split(/\s+/).filter(Boolean));
      const write = values => { element.className = Array.from(values).join(' '); };
      return {
        add(...names) { const values = read(); names.forEach(name => values.add(String(name))); write(values); },
        remove(...names) { const values = read(); names.forEach(name => values.delete(String(name))); write(values); },
        contains(name) { return read().has(String(name)); },
        toggle(name, force) {
          const values = read(); const key = String(name);
          const enabled = force === undefined ? !values.has(key) : !!force;
          enabled ? values.add(key) : values.delete(key); write(values); return enabled;
        },
        replace(oldName, newName) {
          const values = read(); const existed = values.delete(String(oldName));
          if (existed) values.add(String(newName)); write(values); return existed;
        }
      };
    }
  });

  Object.defineProperty(Element.prototype, 'dataset', {
    configurable: true,
    get() {
      const element = this;
      const toDataName = name => 'data-' + String(name).replace(/[A-Z]/g, c => '-' + c.toLowerCase());
      return new Proxy({}, {
        get(target, name) { const value = element.getAttribute(toDataName(name)); return value === null ? undefined : value; },
        set(target, name, value) { element.setAttribute(toDataName(name), String(value)); return true; },
        deleteProperty(target, name) { element.removeAttribute(toDataName(name)); return true; }
      });
    }
  });

  const assignedHandlers = new WeakMap();
  const eventTypes = ['click', 'change', 'input', 'submit', 'focus', 'blur', 'keydown', 'keyup', 'mousedown', 'mouseup'];
  for (const type of eventTypes) {
    Object.defineProperty(Element.prototype, 'on' + type, {
      configurable: true,
      get() { return assignedHandlers.get(this)?.[type] || null; },
      set(callback) {
        let handlers = assignedHandlers.get(this);
        if (!handlers) { handlers = {}; assignedHandlers.set(this, handlers); }
        if (handlers[type]) this.removeEventListener(type, handlers[type]);
        handlers[type] = typeof callback === 'function' ? callback : null;
        if (handlers[type]) this.addEventListener(type, handlers[type]);
      }
    });
  }

  class Event {
    constructor(type, options = {}) {
      this.type = String(type);
      this.detail = options.detail;
      this.bubbles = options.bubbles !== false;
      this.cancelable = !!options.cancelable;
      this.defaultPrevented = false;
      this.__nativeId = 0;
    }
    stopPropagation() { __silverStopPropagation(this.__nativeId || 0); }
    preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
  }
  class CustomEvent extends Event {
    constructor(type, options = {}) { super(type, options); this.detail = options.detail; }
  }
  globalThis.Event = Event;
  globalThis.CustomEvent = CustomEvent;

  globalThis.window = globalThis;
  globalThis.self = globalThis;
  globalThis.performance = Object.freeze({ now: () => __silverNow() });
  globalThis.console = Object.freeze({
    log: (...args) => __silverConsole('log', ...args),
    info: (...args) => __silverConsole('info', ...args),
    warn: (...args) => __silverConsole('warn', ...args),
    error: (...args) => __silverConsole('error', ...args),
    debug: (...args) => __silverConsole('debug', ...args)
  });
  globalThis.ue = Object.freeze({
    emit: (name, payload = '', elementId = '') => __silverUeEmit(String(name), payload, String(elementId)),
    getData: name => __silverUeGetData(String(name)),
    setData: (name, value) => __silverUeSetData(String(name), value)
  });
  globalThis.addEventListener = (...args) => documentObject.addEventListener(...args);
  globalThis.removeEventListener = (...args) => documentObject.removeEventListener(...args);
  globalThis.dispatchEvent = (...args) => documentObject.dispatchEvent(...args);
  globalThis.queueMicrotask = callback => Promise.resolve().then(callback);

  globalThis.__silverBindInlineHandlers = function () {
    for (const type of eventTypes) {
      for (const element of documentObject.querySelectorAll('[on' + type + ']')) {
        const source = element.getAttribute('on' + type);
        if (source) element.addEventListener(type, new Function('event', source));
      }
    }
  };

  /*
   * First-class free-move (standard mouse events — works in H5UI and desktop browsers).
   * Not HTML5 DnD. Not RmlUi-only drag events.
   *
   * Markup:
   *   data-h5ui-move-root   — element that receives left/top
   *   data-h5ui-move-handle — press-and-drag handle
   *
   * H5UI.drag.bind(handle, { getRoot, canStart, onStart, onMove, onEnd, clamp })
   * Buttons inside a handle should be BUTTON/INPUT or data-win-action so they are ignored.
   */
  (function createFreeMoveDrag() {
    const bound = new WeakMap();
    const doc = documentObject;

    function eventPoint(event) {
      if (!event) return { x: 0, y: 0 };
      if (typeof event.clientX === 'number') return { x: event.clientX, y: event.clientY || 0 };
      if (event.touches && event.touches[0]) {
        return { x: event.touches[0].clientX || 0, y: event.touches[0].clientY || 0 };
      }
      return { x: 0, y: 0 };
    }

    function resolveRoot(handle, options) {
      if (options && typeof options.getRoot === 'function') return options.getRoot(handle);
      if (handle.hasAttribute && handle.hasAttribute('data-h5ui-move-root')) return handle;
      const selector = handle.getAttribute && handle.getAttribute('data-h5ui-move-target');
      if (selector) return doc.querySelector(selector);
      let node = handle.parentElement;
      while (node) {
        if (node.hasAttribute && node.hasAttribute('data-h5ui-move-root')) return node;
        node = node.parentElement;
      }
      return null;
    }

    function measureParentOrigin(root) {
      const parent = root.parentElement;
      if (!parent || typeof parent.getBoundingClientRect !== 'function') {
        return { x: 0, y: 0, width: 0, height: 0 };
      }
      const rect = parent.getBoundingClientRect();
      return { x: rect.left || 0, y: rect.top || 0, width: rect.width || 0, height: rect.height || 0 };
    }

    function pinRoot(root) {
      const rect = root.getBoundingClientRect();
      const origin = measureParentOrigin(root);
      const left = (rect.left || 0) - origin.x;
      const top = (rect.top || 0) - origin.y;
      const width = rect.width || 0;
      const height = rect.height || 0;
      root.style.position = 'absolute';
      root.style.left = left + 'px';
      root.style.top = top + 'px';
      root.style.width = width + 'px';
      root.style.height = height + 'px';
      root.style.right = 'auto';
      root.style.bottom = 'auto';
      return { left: left, top: top, width: width, height: height };
    }
			)JS");
			Result += UTF8_TO_TCHAR(R"JS(

    function isControlTarget(target, handle) {
      let node = target;
      while (node && node !== handle) {
        if (node.getAttribute) {
          if (node.getAttribute('data-win-action') || node.getAttribute('data-h5ui-no-move') === 'true') {
            return true;
          }
        }
        const tag = (node.tagName || '').toUpperCase();
        if (tag === 'BUTTON' || tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA' || tag === 'A' || tag === 'LABEL') {
          return true;
        }
        node = node.parentNode;
      }
      return false;
    }

    function setHandleArmed(handle, armed) {
      if (!handle) return;
      if (armed) handle.setAttribute('data-h5ui-move-armed', 'true');
      else handle.removeAttribute('data-h5ui-move-armed');
    }

    function bind(handle, options) {
      if (!handle || typeof handle.addEventListener !== 'function') return null;
      options = options || {};
      const existing = bound.get(handle);
      if (existing) {
        existing.state.options = options;
        existing.state.armed = options.armed !== false;
        setHandleArmed(handle, existing.state.armed);
        return existing.controller;
      }

      const state = {
        options: options,
        armed: options.armed !== false,
        session: null,
        onDocMove: null,
        onDocUp: null
      };

      function endSession(event) {
        if (state.onDocMove) {
          doc.removeEventListener('mousemove', state.onDocMove);
          if (doc.removeEventListener) doc.removeEventListener('touchmove', state.onDocMove);
        }
        if (state.onDocUp) {
          doc.removeEventListener('mouseup', state.onDocUp);
          if (doc.removeEventListener) doc.removeEventListener('touchend', state.onDocUp);
        }
        state.onDocMove = null;
        state.onDocUp = null;
        const session = state.session;
        if (!session || !session.root) {
          state.session = null;
          return;
        }
        session.root.classList.remove('is-dragging');
        handle.classList.remove('is-dragging-handle');
        const rect = session.root.getBoundingClientRect();
        const origin = measureParentOrigin(session.root);
        const left = (rect.left || 0) - origin.x;
        const top = (rect.top || 0) - origin.y;
        if (typeof state.options.onEnd === 'function') {
          state.options.onEnd({
            root: session.root,
            handle: handle,
            left: left,
            top: top,
            width: session.width,
            height: session.height,
            event: event
          });
        }
        state.session = null;
      }

      function moveSession(event) {
        const session = state.session;
        if (!session || !session.root) return;
        const point = eventPoint(event);
        let left = session.originLeft + (point.x - session.startX);
        let top = session.originTop + (point.y - session.startY);
        if (typeof state.options.clamp === 'function') {
          const clamped = state.options.clamp({
            left: left,
            top: top,
            width: session.width,
            height: session.height,
            root: session.root,
            handle: handle,
            event: event
          });
          if (clamped) {
            if (typeof clamped.left === 'number') left = clamped.left;
            if (typeof clamped.top === 'number') top = clamped.top;
          }
        }
        session.root.style.left = left + 'px';
        session.root.style.top = top + 'px';
        if (typeof state.options.onMove === 'function') {
          state.options.onMove({
            root: session.root,
            handle: handle,
            left: left,
            top: top,
            width: session.width,
            height: session.height,
            event: event
          });
        }
      }

      function onPointerDown(event) {
        if (!state.armed) return;
        if (typeof event.button === 'number' && event.button !== 0) return;
        if (isControlTarget(event.target, handle)) return;
        const root = resolveRoot(handle, state.options);
        if (!root) return;
        if (typeof state.options.canStart === 'function' && !state.options.canStart(root, handle, event)) {
          return;
        }
        if (event.preventDefault) event.preventDefault();
        if (event.stopPropagation) event.stopPropagation();

        const geometry = pinRoot(root);
        const point = eventPoint(event);
        state.session = {
          root: root,
          handle: handle,
          startX: point.x,
          startY: point.y,
          originLeft: geometry.left,
          originTop: geometry.top,
          width: geometry.width,
          height: geometry.height
        };
        root.classList.add('is-dragging');
        handle.classList.add('is-dragging-handle');
        if (typeof state.options.onStart === 'function') {
          state.options.onStart({
            root: root,
            handle: handle,
            left: geometry.left,
            top: geometry.top,
            width: geometry.width,
            height: geometry.height,
            event: event
          });
        }

        state.onDocMove = function (ev) { moveSession(ev); };
        state.onDocUp = function (ev) { endSession(ev); };
        doc.addEventListener('mousemove', state.onDocMove);
        doc.addEventListener('mouseup', state.onDocUp);
        if (doc.addEventListener) {
          doc.addEventListener('touchmove', state.onDocMove);
          doc.addEventListener('touchend', state.onDocUp);
        }
      }

      handle.addEventListener('mousedown', onPointerDown);
      handle.addEventListener('touchstart', onPointerDown);
      setHandleArmed(handle, state.armed);

      const controller = {
        setArmed: function (armed) {
          state.armed = !!armed;
          setHandleArmed(handle, state.armed);
          if (!state.armed && state.session) endSession(null);
        },
        updateOptions: function (next) {
          state.options = next || {};
          state.armed = state.options.armed !== false;
          setHandleArmed(handle, state.armed);
        },
        unbind: function () {
          if (state.session) endSession(null);
          handle.removeEventListener('mousedown', onPointerDown);
          handle.removeEventListener('touchstart', onPointerDown);
          setHandleArmed(handle, false);
          bound.delete(handle);
          handle.__h5uiMoveBound = false;
        }
      };

      bound.set(handle, { controller: controller, state: state });
      handle.__h5uiMoveBound = true;
      return controller;
    }

    function bindTree(root, options) {
      if (!root) return [];
      const controllers = [];
      if (root.hasAttribute && root.hasAttribute('data-h5ui-move-handle')) {
        const c = bind(root, options);
        if (c) controllers.push(c);
      }
      if (typeof root.querySelectorAll === 'function') {
        const list = root.querySelectorAll('[data-h5ui-move-handle]');
        for (let i = 0; i < list.length; i += 1) {
          const c = bind(list[i], options);
          if (c) controllers.push(c);
        }
      }
      return controllers;
    }

    const api = {
      bind: bind,
      bindTree: bindTree,
      pin: pinRoot,
      setArmed: setHandleArmed,
      resolveRoot: resolveRoot
    };

    globalThis.__h5uiDrag = api;
    if (!globalThis.H5UI) globalThis.H5UI = {};
    globalThis.H5UI.drag = api;
  })();

})();
)JS");
			return Result;
		}();
		return Source;
	}

	static const FString& GetExtendedComponentSource()
	{
		static const FString Source = UTF8_TO_TCHAR(R"JS(
(function () {
  'use strict';
  const documentObject = globalThis.document;

  const componentInitAttribute = 'data-h5ui-component-initialized';
  let activeDragItem = null;
  const isTrueAttribute = (element, name) => element.hasAttribute(name) && element.getAttribute(name) !== 'false';
  const setState = (element, state, enabled) => {
    element.classList.toggle(state, !!enabled);
    if (enabled) element.setAttribute('data-h5ui-' + state, 'true');
    else element.removeAttribute('data-h5ui-' + state);
  };
  const dispatchComponentEvent = (element, type, detail) =>
    element.dispatchEvent(new CustomEvent(type, { bubbles: true, detail }));
  const initializeOnce = (element, callback) => {
    if (element.hasAttribute(componentInitAttribute)) return;
    element.setAttribute(componentInitAttribute, 'true');
    callback(element);
  };
  const getOptionValue = option => option.getAttribute('value') || option.textContent || '';
  const syncDisabledState = element => setState(element, 'disabled', isTrueAttribute(element, 'disabled'));
  const syncValidityState = group => {
    const required = isTrueAttribute(group, 'required');
    const hasValue = String(group.getAttribute('value') || '') !== '';
    setState(group, 'valid', !required || hasValue);
    setState(group, 'invalid', required && !hasValue);
  };

  globalThis.__silverInitializeExtendedElements = function () {
    for (const tag of ['input', 'textarea', 'select', 'option', 'button']) {
      for (const input of documentObject.querySelectorAll(tag)) {
        initializeOnce(input, element => {
          syncDisabledState(element);
          element.addEventListener('change', () => {
            syncDisabledState(element);
            setState(element, 'checked', !!element.checked);
          });
        });
      }
    }

    for (const area of documentObject.querySelectorAll('drag-area')) {
      initializeOnce(area, element => element.classList.add('drag-area'));
    }
    for (const tag of ['drag-item', 'handle']) {
      for (const item of documentObject.querySelectorAll(tag)) {
        initializeOnce(item, element => {
          element.style.drag = 'drag';
          const dragItem = element.closest('drag-item') || element;
          element.addEventListener('dragstart', () => {
            activeDragItem = dragItem;
            setState(dragItem, 'dragging', true);
          });
          element.addEventListener('dragend', () => {
            setState(dragItem, 'dragging', false);
            if (activeDragItem === dragItem) activeDragItem = null;
          });
        });
      }
    }
    for (const cell of documentObject.querySelectorAll('drag-cell')) {
      initializeOnce(cell, element => {
        element.style.drag = 'drag-drop';
        setState(element, 'occupied', isTrueAttribute(element, 'occupied'));
        element.addEventListener('dragover', () => setState(element, 'hot', true));
        element.addEventListener('dragout', () => setState(element, 'hot', false));
        element.addEventListener('dragdrop', () => {
          setState(element, 'hot', false);
          if (!activeDragItem || activeDragItem === element) return;
          const previousCell = activeDragItem.closest('drag-cell');
          element.appendChild(activeDragItem);
          if (previousCell && previousCell !== element) {
            previousCell.removeAttribute('occupied');
            setState(previousCell, 'occupied', false);
          }
          element.setAttribute('occupied', 'true');
          setState(element, 'occupied', true);
          dispatchComponentEvent(element, 'drop', { item: activeDragItem, source: previousCell });
        });
      });
    }

    for (const group of documentObject.querySelectorAll('radio-group')) {
      initializeOnce(group, element => {
        const select = option => {
          if (!option || isTrueAttribute(option, 'disabled')) return;
          const options = element.querySelectorAll('radio-option');
          for (const candidate of options) {
            const selected = candidate === option;
            setState(candidate, 'selected', selected);
            if (selected) candidate.setAttribute('selected', 'true');
            else candidate.removeAttribute('selected');
          }
          const value = getOptionValue(option);
          element.setAttribute('value', value);
          syncValidityState(element);
          dispatchComponentEvent(element, 'change', { value, option });
        };
        const options = element.querySelectorAll('radio-option');
        for (const option of options) {
          syncDisabledState(option);
          setState(option, 'selected', isTrueAttribute(option, 'selected'));
          option.addEventListener('click', event => { if (event) event.stopPropagation(); select(option); });
        }
        const selected = element.querySelector('radio-option.selected') || element.querySelector('radio-option[selected]');
        if (selected) select(selected); else syncValidityState(element);
      });
    }

    for (const group of documentObject.querySelectorAll('checkbox-group')) {
      initializeOnce(group, element => {
        const sync = () => {
          const selected = [];
          for (const option of element.querySelectorAll('checkbox-option')) {
            if (option.classList.contains('selected')) selected.push(getOptionValue(option));
          }
          const value = selected.join(',');
          element.setAttribute('value', value);
          syncValidityState(element);
          dispatchComponentEvent(element, 'change', { value, values: selected });
        };
        for (const option of element.querySelectorAll('checkbox-option')) {
          syncDisabledState(option);
          setState(option, 'selected', isTrueAttribute(option, 'selected') || isTrueAttribute(option, 'checked'));
          option.addEventListener('click', () => {
            if (isTrueAttribute(option, 'disabled')) return;
            setState(option, 'selected', !option.classList.contains('selected'));
            sync();
          });
        }
        sync();
      });
    }

    for (const dropdown of documentObject.querySelectorAll('dropdown')) {
      initializeOnce(dropdown, element => {
        const select = option => {
          if (!option || isTrueAttribute(option, 'disabled')) return;
          for (const candidate of element.querySelectorAll('dropdown-option')) {
            setState(candidate, 'selected', candidate === option);
          }
          const value = getOptionValue(option);
          element.setAttribute('value', value);
          setState(element, 'open', false);
          dispatchComponentEvent(element, 'change', { value, option });
        };
        setState(element, 'open', isTrueAttribute(element, 'open'));
        element.addEventListener('click', () => setState(element, 'open', !element.classList.contains('open')));
        for (const option of element.querySelectorAll('dropdown-option')) {
          syncDisabledState(option);
          setState(option, 'selected', isTrueAttribute(option, 'selected'));
          option.addEventListener('click', event => { if (event) event.stopPropagation(); select(option); });
        }
        const selected = element.querySelector('dropdown-option.selected') || element.querySelector('dropdown-option[selected]');
        if (selected) select(selected);
      });
    }

    for (const password of documentObject.querySelectorAll('password')) {
      initializeOnce(password, element => {
        let input = element.querySelector('input');
        if (!input) {
          input = documentObject.createElement('input');
          input.setAttribute('type', 'password');
          const placeholder = element.getAttribute('placeholder');
          if (placeholder !== null) input.setAttribute('placeholder', placeholder);
          const value = element.getAttribute('value');
          if (value !== null) input.setAttribute('value', value);
          element.appendChild(input);
        }
        input.setAttribute('type', 'password');
        input.addEventListener('input', () => {
          element.setAttribute('value', input.value || '');
          dispatchComponentEvent(element, 'input', { value: input.value || '' });
        });
      });
    }

    for (const modal of documentObject.querySelectorAll('modal')) {
      initializeOnce(modal, element => {
        const sync = open => {
          const wasOpen = element.classList.contains('open');
          setState(element, 'open', open);
          if (wasOpen !== !!open) dispatchComponentEvent(element, open ? 'open' : 'close', {});
        };
        sync(isTrueAttribute(element, 'open'));
        for (const close of element.querySelectorAll('[data-modal-close]')) {
          close.addEventListener('click', () => sync(false));
        }
      });
    }

    for (const tooltip of documentObject.querySelectorAll('tooltips')) {
      initializeOnce(tooltip, element => {
        const show = visible => setState(element, 'open', visible);
        element.addEventListener('mouseover', () => show(true));
        element.addEventListener('mouseout', () => show(false));
        element.addEventListener('focus', () => show(true));
        element.addEventListener('blur', () => show(false));
      });
    }

    for (const watch of documentObject.querySelectorAll('animation-watch')) {
      initializeOnce(watch, element => {
        element.addEventListener('animationstart', () => setState(element, 'animating', true));
        element.addEventListener('animationend', () => setState(element, 'animating', false));
        element.addEventListener('animationcancel', () => setState(element, 'animating', false));
      });
    }
  };
  globalThis.H5UIComponents = Object.freeze({
    refresh: () => globalThis.__silverInitializeExtendedElements(),
    setState: (element, state, enabled) => setState(element, String(state), !!enabled)
  });
})();
)JS");
		return Source;
	}

	JSRuntime* Runtime = nullptr;
	JSContext* Context = nullptr;
	Rml::ElementDocument* Document = nullptr;
	JSClassID ElementClassId = 0;
	JSValue EventPrototype = JS_UNDEFINED;
	TMap<Rml::Element*, JSValue> ElementObjects;
	TMap<Rml::Element*, Rml::ElementPtr> DetachedElements;
	TArray<TUniquePtr<FH5UI_Listener>> Listeners;
	TMap<int32, FTimer> Timers;
	TSet<int32> ActiveTimerIds;
	TSet<int32> CancelledTimerIds;
	TMap<int32, Rml::Event*> ActiveEvents;
	FString SourceURL;
	double ExecutionTimeLimitMilliseconds = 4.0;
	double PromiseJobTimeLimitMilliseconds = 16.0;
	double InitialExecutionTimeLimitMilliseconds = 500.0;
	double ExecutionDeadlineSeconds = 0.0;
	double StartTimeSeconds = 0.0;
	float LastExecutionMilliseconds = 0.0f;
	int32 MaxCallbacksPerFrame = 100;
	int32 NextTimerId = 1;
	int32 NextNativeEventId = 1;
	int32 DispatchDepth = 0;
	bool bExecutionActive = false;
	bool bShuttingDown = false;
	FH5UI_ScriptRuntime::FEmitCallback EmitCallback;
	FH5UI_ScriptRuntime::FGetDataCallback GetDataCallback;
	FH5UI_ScriptRuntime::FSetDataCallback SetDataCallback;
	FH5UI_ScriptRuntime::FErrorCallback ErrorCallback;
	FH5UI_ScriptRuntime::FActivityCallback ActivityCallback;
	uint64 TickSerial = 0;
};

FH5UI_ScriptRuntime::FH5UI_ScriptRuntime(
	FEmitCallback InEmit,
	FGetDataCallback InGetData,
	FSetDataCallback InSetData,
	FErrorCallback InError,
	FActivityCallback InActivity)
	: Impl(MakeUnique<FImpl>(
		MoveTemp(InEmit),
		MoveTemp(InGetData),
		MoveTemp(InSetData),
		MoveTemp(InError),
		MoveTemp(InActivity)))
{
}

FH5UI_ScriptRuntime::~FH5UI_ScriptRuntime() = default;

bool FH5UI_ScriptRuntime::Initialize(
	Rml::ElementDocument* InDocument,
	const FString& InSourceURL,
	int64 MemoryLimitBytes,
	double ExecutionTimeLimitMilliseconds,
	double PromiseJobTimeLimitMilliseconds,
	double InitialExecutionTimeLimitMilliseconds,
	int32 MaxCallbacksPerFrame)
{
	return Impl->Initialize(
		InDocument,
		InSourceURL,
		MemoryLimitBytes,
		ExecutionTimeLimitMilliseconds,
		PromiseJobTimeLimitMilliseconds,
		InitialExecutionTimeLimitMilliseconds,
		MaxCallbacksPerFrame);
}

void FH5UI_ScriptRuntime::Shutdown()
{
	Impl->Shutdown();
	LastViewportSize = FIntPoint(-1, -1);
}

void FH5UI_ScriptRuntime::SetViewportMetrics(
	const FIntPoint& ViewSize,
	const FIntPoint& RenderSize,
	float DevicePixelRatio,
	float RenderScale,
	float EffectivePixelRatio)
{
	if (!Impl || !Impl->Context)
	{
		return;
	}
	const bool bHadViewportSize = LastViewportSize.X >= 0 && LastViewportSize.Y >= 0;
	LastViewportSize = ViewSize;

	JSValue Global = JS_GetGlobalObject(Impl->Context);
	JS_SetPropertyStr(Impl->Context, Global, "innerWidth", JS_NewInt32(Impl->Context, ViewSize.X));
	JS_SetPropertyStr(Impl->Context, Global, "innerHeight", JS_NewInt32(Impl->Context, ViewSize.Y));
	JS_SetPropertyStr(Impl->Context, Global, "devicePixelRatio", JS_NewFloat64(Impl->Context, DevicePixelRatio));
	JS_SetPropertyStr(Impl->Context, Global, "silverRenderScale", JS_NewFloat64(Impl->Context, RenderScale));
	JS_SetPropertyStr(Impl->Context, Global, "silverEffectivePixelRatio", JS_NewFloat64(Impl->Context, EffectivePixelRatio));
	JS_SetPropertyStr(Impl->Context, Global, "silverRenderWidth", JS_NewInt32(Impl->Context, RenderSize.X));
	JS_SetPropertyStr(Impl->Context, Global, "silverRenderHeight", JS_NewInt32(Impl->Context, RenderSize.Y));
	JS_FreeValue(Impl->Context, Global);

	// RuntimeView only calls this method when one of the viewport metrics changed.
	// Dispatch after the first metrics assignment even when the logical dimensions
	// stayed constant (for example, when only the Slate/UMG screen scale changed).
	if (bHadViewportSize)
	{
		DispatchViewportResize();
	}
}

void FH5UI_ScriptRuntime::DispatchViewportResize()
{
	if (!Impl || !Impl->Context)
	{
		return;
	}

	FString Result;
	FString Error;
	Execute(
		TEXT("window.dispatchEvent(new Event('resize'));"),
		TEXT("h5ui://runtime/viewport-resize.js"),
		Result,
		Error);
}

bool FH5UI_ScriptRuntime::FlushPendingJobs()
{
	return Impl && Impl->DrainPendingJobs();
}

bool FH5UI_ScriptRuntime::Execute(
	const FString& Script,
	const FString& SourceURL,
	FString& OutResult,
	FString& OutError)
{
	return Impl->Execute(Script, SourceURL, OutResult, OutError);
}

bool FH5UI_ScriptRuntime::ExecutePageScripts(const TArray<FH5UI_ScriptSource>& Scripts)
{
	return Impl->ExecutePageScripts(Scripts);
}

void FH5UI_ScriptRuntime::BindInlineEventHandlers()
{
	Impl->BindInlineEventHandlers();
}

void FH5UI_ScriptRuntime::DispatchDocumentReady()
{
	Impl->DispatchDocumentReady();
}

bool FH5UI_ScriptRuntime::Tick(double CurrentTimeSeconds)
{
	return Impl->Tick(CurrentTimeSeconds);
}

double FH5UI_ScriptRuntime::GetNextWakeTimeSeconds() const
{
	return Impl->GetNextWakeTimeSeconds();
}

float FH5UI_ScriptRuntime::GetLastExecutionMilliseconds() const
{
	return Impl->LastExecutionMilliseconds;
}

int64 FH5UI_ScriptRuntime::GetMemoryUsageBytes() const
{
	return Impl->GetMemoryUsageBytes();
}

int32 FH5UI_ScriptRuntime::GetTimerCount() const
{
	return Impl->Timers.Num();
}

bool FH5UI_ScriptRuntime::IsValid() const
{
	return Impl->Context != nullptr;
}
