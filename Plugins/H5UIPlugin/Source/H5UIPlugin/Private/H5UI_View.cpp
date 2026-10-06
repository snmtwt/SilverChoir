#include "H5UI_View.h"

#include "SH5UI_View.h"
#include "H5UI_EventHandler.h"
#include "H5UI_Settings.h"
#include "Engine/Texture.h"
#include "JsonObjectConverter.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "H5UI_View"

namespace
{
	TArray<TWeakObjectPtr<UH5UI_View>> GH5UI_LiveViews;
	FH5UI_NativeUIEventDelegate GH5UI_NativeUIEvent;
	FH5UI_ViewReleasedDelegate GH5UI_ViewReleased;

	void CompactLiveViews()
	{
		GH5UI_LiveViews.RemoveAll([](const TWeakObjectPtr<UH5UI_View>& View)
		{
			return !View.IsValid();
		});
	}
}

UH5UI_View::UH5UI_View(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, URL(TEXT("h5ui://plugin/demo.rml"))
	, bAutoLoad(true)
	, bReceiveInput(true)
	, bConsumeInput(true)
	, bPassThroughTransparentMouseInput(true)
	, bPassThroughKeyboardWithoutEditableFocus(true)
	, bEnableJavaScript(true)
	, TargetFrameRate(GetDefault<UH5UI_Settings>()->ActiveUpdateRate)
	, DefaultSize(640.0f, 360.0f)
	, RenderScale(1.0f)
	, ViewState(EH5UI_ViewState::Unloaded)
{
}

bool UH5UI_View::LoadURL(const FString& NewURL)
{
	DeactivateEventHandlers(false);
	URL = NewURL;
	ViewState = EH5UI_ViewState::Loading;
	return MyHtmlView.IsValid() && MyHtmlView->LoadURL(URL);
}

bool UH5UI_View::LoadString(const FString& Html, const FString& BaseURL)
{
	DeactivateEventHandlers(false);
	ViewState = EH5UI_ViewState::Loading;
	return MyHtmlView.IsValid() && MyHtmlView->LoadString(Html, BaseURL);
}

bool UH5UI_View::Reload()
{
	DeactivateEventHandlers(false);
	ViewState = EH5UI_ViewState::Loading;
	return MyHtmlView.IsValid() && MyHtmlView->Reload();
}

void UH5UI_View::Close()
{
	DeactivateEventHandlers(false);
	if (MyHtmlView)
	{
		MyHtmlView->Close();
	}
	ViewState = EH5UI_ViewState::Unloaded;
}

void UH5UI_View::SetHtmlRenderScale(float NewRenderScale)
{
	RenderScale = FMath::Clamp(NewRenderScale, 0.5f, 4.0f);
	if (MyHtmlView)
	{
		MyHtmlView->SetRenderScale(RenderScale);
	}
}

bool UH5UI_View::FocusElementById(const FString& ElementId)
{
	return MyHtmlView.IsValid() && MyHtmlView->FocusElementById(ElementId);
}

bool UH5UI_View::ExecuteJavaScript(const FString& Script, FString& Result, FString& Error)
{
	Result.Reset();
	Error.Reset();
	return MyHtmlView.IsValid() && MyHtmlView->ExecuteJavaScript(Script, Result, Error);
}

bool UH5UI_View::DispatchHtmlEvent(FName EventName, const FString& Detail)
{
	return MyHtmlView.IsValid() && MyHtmlView->DispatchHtmlEvent(EventName.ToString(), Detail);
}

bool UH5UI_View::DispatchIFrameEvent(FName EventName, const FString& Detail)
{
	return EventName != NAME_None && MyHtmlView.IsValid() &&
		MyHtmlView->DispatchIFrameEvent(EventName.ToString(), Detail);
}

bool UH5UI_View::SendIFrameTestData(const FString& Message)
{
	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("message"), Message);
	Payload->SetStringField(TEXT("source"), TEXT("Unreal"));
	Payload->SetStringField(TEXT("sentAtUtc"), FDateTime::UtcNow().ToIso8601());
	FString PayloadJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadJson);
	return FJsonSerializer::Serialize(Payload, Writer) &&
		DispatchIFrameEvent(TEXT("CommanderOS.TestFromUE"), PayloadJson);
}

bool UH5UI_View::RegisterEventHandler(FName EventType, UH5UI_EventHandler* EventHandler)
{
	if (EventType == NAME_None || !IsValid(EventHandler))
	{
		return false;
	}

	if (UH5UI_View* ExistingView = EventHandler->GetH5UIView(); ExistingView && ExistingView != this)
	{
		UE_LOG(LogTemp, Warning, TEXT("H5 UI event handler '%s' is already registered to view '%s'."),
			*EventHandler->GetName(), *ExistingView->GetName());
		return false;
	}

	if (const TObjectPtr<UH5UI_EventHandler>* ExistingHandler = RegisteredEventHandlers.Find(EventType))
	{
		if (ExistingHandler->Get() == EventHandler)
		{
			if (ViewState == EH5UI_ViewState::Ready)
			{
				EventHandler->Activate();
			}
			return true;
		}
		(*ExistingHandler)->UnbindFromView();
	}

	EventHandler->BindToView(this, EventType);
	RegisteredEventHandlers.Add(EventType, EventHandler);
	if (ViewState == EH5UI_ViewState::Ready)
	{
		EventHandler->Activate();
	}
	return true;
}

bool UH5UI_View::UnregisterEventHandler(FName EventType, UH5UI_EventHandler* EventHandler)
{
	TObjectPtr<UH5UI_EventHandler>* RegisteredHandler = RegisteredEventHandlers.Find(EventType);
	if (!RegisteredHandler || (EventHandler && RegisteredHandler->Get() != EventHandler))
	{
		return false;
	}

	(*RegisteredHandler)->UnbindFromView();
	RegisteredEventHandlers.Remove(EventType);
	return true;
}

UH5UI_EventHandler* UH5UI_View::GetEventHandler(FName EventType) const
{
	if (const TObjectPtr<UH5UI_EventHandler>* Handler = RegisteredEventHandlers.Find(EventType))
	{
		return Handler->Get();
	}
	if (const TObjectPtr<UH5UI_EventHandler>* Handler = ConfiguredEventHandlers.Find(EventType))
	{
		return Handler->Get();
	}
	return nullptr;
}

bool UH5UI_View::IsEventHandlerDynamicallyRegistered(
	FName EventType,
	const UH5UI_EventHandler* EventHandler) const
{
	if (const TObjectPtr<UH5UI_EventHandler>* RegisteredHandler = RegisteredEventHandlers.Find(EventType))
	{
		return RegisteredHandler->Get() == EventHandler;
	}
	return false;
}

bool UH5UI_View::IsScreenPositionInteractive(
	const FVector2D& ScreenPosition,
	FVector2D& DocumentPosition)
{
	DocumentPosition = FVector2D::ZeroVector;
	if (!MyHtmlView.IsValid() || ViewState != EH5UI_ViewState::Ready)
	{
		return false;
	}

	const FGeometry Geometry = GetCachedGeometry();
	const FVector2D LocalSize = Geometry.GetLocalSize();
	if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
	{
		return false;
	}

	const FVector2D SlateLocalPosition = Geometry.AbsoluteToLocal(ScreenPosition);
	if (SlateLocalPosition.X < 0.0f || SlateLocalPosition.Y < 0.0f ||
		SlateLocalPosition.X >= LocalSize.X || SlateLocalPosition.Y >= LocalSize.Y)
	{
		return false;
	}

	const bool bInteractive = MyHtmlView->IsPointerInteractingAt(SlateLocalPosition);
	DocumentPosition = MyHtmlView->LocalToDocumentPosition(SlateLocalPosition);
	return bInteractive;
}

void UH5UI_View::GetLiveViews(TArray<UH5UI_View*>& OutViews)
{
	CompactLiveViews();
	OutViews.Reset(GH5UI_LiveViews.Num());
	for (const TWeakObjectPtr<UH5UI_View>& WeakView : GH5UI_LiveViews)
	{
		if (UH5UI_View* View = WeakView.Get())
		{
			OutViews.Add(View);
		}
	}
}

FH5UI_NativeUIEventDelegate& UH5UI_View::OnAnyNativeUIEvent()
{
	return GH5UI_NativeUIEvent;
}

FH5UI_ViewReleasedDelegate& UH5UI_View::OnAnyViewReleased()
{
	return GH5UI_ViewReleased;
}

void UH5UI_View::SetDataString(FName Name, const FString& Value)
{
	const FString Key = Name.ToString();
	PendingModelValues.FindOrAdd(Key) = Value;
	if (MyHtmlView)
	{
		MyHtmlView->SetData(Key, Value);
	}
}

void UH5UI_View::SetDataNumber(FName Name, double Value)
{
	SetDataString(Name, FString::SanitizeFloat(Value));
}

void UH5UI_View::SetDataBoolean(FName Name, bool bValue)
{
	SetDataString(Name, bValue ? TEXT("true") : TEXT("false"));
}

bool UH5UI_View::SetPageData(const int32& PageData, FName EventName)
{
	// The Blueprint VM routes the wildcard structure through execSetPageData.
	return false;
}

DEFINE_FUNCTION(UH5UI_View::execSetPageData)
{
	Stack.StepCompiledIn<FProperty>(nullptr);
	FProperty* PageDataProperty = Stack.MostRecentProperty;
	void* PageDataAddress = Stack.MostRecentPropertyAddress;
	P_GET_PROPERTY(FNameProperty, EventName);
	P_FINISH;

	bool bSuccess = false;
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(PageDataProperty);
		StructProperty && PageDataAddress)
	{
		P_NATIVE_BEGIN;
		bSuccess = P_THIS->SetPageDataFromStruct(StructProperty->Struct, PageDataAddress, EventName);
		P_NATIVE_END;
	}
	else
	{
		FFrame::KismetExecutionMessage(
			TEXT("Set Page Data (Struct) requires a valid structure input."),
			ELogVerbosity::Error);
	}

	*StaticCast<bool*>(RESULT_PARAM) = bSuccess;
}

bool UH5UI_View::SetPageDataFromStruct(
	const UScriptStruct* StructType,
	const void* StructData,
	FName EventName)
{
	FString Json;
	if (!SerializeStructToJsonString(StructType, StructData, Json))
	{
		return false;
	}

	SetDataString(TEXT("pageData"), Json);
	if (ViewState == EH5UI_ViewState::Ready && EventName != NAME_None)
	{
		DispatchHtmlEvent(EventName, Json);
	}
	return true;
}

bool UH5UI_View::DispatchHtmlEventFromStruct(
	FName EventName,
	const UScriptStruct* StructType,
	const void* StructData)
{
	if (EventName == NAME_None)
	{
		return false;
	}

	FString Json;
	return SerializeStructToJsonString(StructType, StructData, Json) &&
		DispatchHtmlEvent(EventName, Json);
}

bool UH5UI_View::SerializeStructToJsonString(
	const UScriptStruct* StructType,
	const void* StructData,
	FString& OutJson) const
{
	if (!StructType || !StructData)
	{
		return false;
	}

	FJsonObjectConverter::CustomExportCallback ExportCallback;
	ExportCallback.BindLambda([](FProperty* Property, const void* Value) -> TSharedPtr<FJsonValue>
	{
		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property);
			SoftObjectProperty && SoftObjectProperty->PropertyClass &&
			SoftObjectProperty->PropertyClass->IsChildOf(UTexture::StaticClass()))
		{
			const FSoftObjectPath TexturePath = SoftObjectProperty->GetPropertyValue(Value).ToSoftObjectPath();
			if (TexturePath.IsValid())
			{
				return MakeShared<FJsonValueString>(TEXT("ueasset://") + TexturePath.ToString());
			}

			// Returning nullptr asks FJsonObjectConverter to use its default object
			// serializer, which writes an empty texture as the truthy string "None".
			return MakeShared<FJsonValueString>(FString());
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
			ObjectProperty && ObjectProperty->PropertyClass &&
			ObjectProperty->PropertyClass->IsChildOf(UTexture::StaticClass()))
		{
			if (const UTexture* Texture = Cast<UTexture>(ObjectProperty->GetObjectPropertyValue(Value)))
			{
				return MakeShared<FJsonValueString>(TEXT("ueasset://") + Texture->GetPathName());
			}

			return MakeShared<FJsonValueString>(FString());
		}

		return nullptr;
	});
	if (!FJsonObjectConverter::UStructToJsonObjectString(
		StructType,
		StructData,
		OutJson,
		0,
		CPF_Transient,
		0,
		&ExportCallback,
		false))
	{
		FFrame::KismetExecutionMessage(
			*FString::Printf(TEXT("Unable to serialize %s for H5 UI Plugin."), *StructType->GetName()),
			ELogVerbosity::Error);
		return false;
	}
	return true;
}

void UH5UI_View::SynchronizeModels()
{
	if (MyHtmlView)
	{
		MyHtmlView->SynchronizeModels();
	}
}

FH5UI_PerformanceStats UH5UI_View::GetPerformanceStats() const
{
	return MyHtmlView.IsValid() ? MyHtmlView->GetPerformanceStats() : FH5UI_PerformanceStats();
}

void UH5UI_View::ReleaseSlateResources(bool bReleaseChildren)
{
	DeactivateEventHandlers(false);
	Super::ReleaseSlateResources(bReleaseChildren);
	GH5UI_LiveViews.RemoveAll([this](const TWeakObjectPtr<UH5UI_View>& View)
	{
		return !View.IsValid() || View.Get() == this;
	});
	MyHtmlView.Reset();
}

void UH5UI_View::BeginDestroy()
{
	GH5UI_ViewReleased.Broadcast(this);
	DeactivateEventHandlers(true);
	Super::BeginDestroy();
}

void UH5UI_View::SetBrowserSubviewsEnabled(bool bEnabled)
{
	bEnableBrowserSubviews = bEnabled;
	SynchronizeProperties();
}

void UH5UI_View::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MyHtmlView)
	{
		MyHtmlView->Configure(
			TargetFrameRate,
			bReceiveInput,
			bConsumeInput,
			bPassThroughTransparentMouseInput,
			bPassThroughKeyboardWithoutEditableFocus,
			bEnableJavaScript,
			RenderScale,
			bEnableBrowserSubviews);
	}
}

#if WITH_EDITOR
const FText UH5UI_View::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "H5 UI Plugin");
}
#endif

TSharedRef<SWidget> UH5UI_View::RebuildWidget()
{
	CompactLiveViews();
	if (!GH5UI_LiveViews.ContainsByPredicate([this](const TWeakObjectPtr<UH5UI_View>& View)
		{
			return View.Get() == this;
		}))
	{
		GH5UI_LiveViews.Add(this);
	}

	SAssignNew(MyHtmlView, SH5UI_View)
		.OwnerWidget(this)
		.DefaultSize(DefaultSize);
	MyHtmlView->Configure(
		TargetFrameRate,
		bReceiveInput,
		bConsumeInput,
		bPassThroughTransparentMouseInput,
		bPassThroughKeyboardWithoutEditableFocus,
		bEnableJavaScript,
		RenderScale,
		bEnableBrowserSubviews);

	for (const TPair<FString, FString>& Pair : PendingModelValues)
	{
		MyHtmlView->SetData(Pair.Key, Pair.Value);
	}

	if (bAutoLoad && !URL.IsEmpty())
	{
		MyHtmlView->LoadURL(URL);
	}
	return MyHtmlView.ToSharedRef();
}

void UH5UI_View::NotifyReady()
{
	ViewState = EH5UI_ViewState::Ready;
	CreateEventHandlers();
	OnReadyForBindings.Broadcast();
}

void UH5UI_View::NotifyFailed(const FString& Error)
{
	DeactivateEventHandlers(false);
	ViewState = EH5UI_ViewState::Failed;
	OnLoadFailed.Broadcast(Error);
}

void UH5UI_View::NotifyUIEvent(const FH5UI_Event& Event)
{
	if (Event.EventType != NAME_None)
	{
		if (UH5UI_EventHandler* Handler = GetEventHandler(Event.EventType))
		{
			Handler->DispatchEvent(Event);
			return;
		}

		// Preserve typed routing when configured, but keep browser-authored pages
		// functional before a dedicated handler class exists. The event retains its
		// EventType so Blueprint can switch on StrategyControl, CommanderOS, etc.
		OnUIEvent.Broadcast(Event);
		GH5UI_NativeUIEvent.Broadcast(this, Event);
		return;
	}

	// Keep the legacy Blueprint/global event surface isolated from typed handler routes.
	OnUIEvent.Broadcast(Event);
	GH5UI_NativeUIEvent.Broadcast(this, Event);
}

void UH5UI_View::CreateEventHandlers()
{
	DeactivateEventHandlers(false);
	for (const TPair<FName, TObjectPtr<UH5UI_EventHandler>>& Pair : RegisteredEventHandlers)
	{
		Pair.Value->Activate();
	}

	auto CreateConfiguredHandler = [this](
		FName EventType,
		const TSoftClassPtr<UH5UI_EventHandler>& HandlerClassReference,
		const TCHAR* ConfigurationSource)
	{
		if (EventType == NAME_None)
		{
			UE_LOG(LogTemp, Warning, TEXT("H5 UI %s EventHandlerClasses ignores an empty event-type key."),
				ConfigurationSource);
			return;
		}
		if (RegisteredEventHandlers.Contains(EventType) || ConfiguredEventHandlers.Contains(EventType))
		{
			return;
		}

		UClass* HandlerClass = HandlerClassReference.LoadSynchronous();
		if (!HandlerClass || !HandlerClass->IsChildOf(UH5UI_EventHandler::StaticClass()))
		{
			UE_LOG(LogTemp, Warning, TEXT("H5 UI event type '%s' has no valid %s EventHandler class."),
				*EventType.ToString(), ConfigurationSource);
			return;
		}

		UH5UI_EventHandler* Handler = NewObject<UH5UI_EventHandler>(this, HandlerClass);
		Handler->BindToView(this, EventType);
		Handler->Activate();
		ConfiguredEventHandlers.Add(EventType, Handler);
	};

	for (const TPair<FName, TSoftClassPtr<UH5UI_EventHandler>>& Pair : EventHandlerClasses)
	{
		CreateConfiguredHandler(Pair.Key, Pair.Value, TEXT("view"));
	}

	const UH5UI_Settings* Settings = GetDefault<UH5UI_Settings>();
	for (const TPair<FName, TSoftClassPtr<UH5UI_EventHandler>>& Pair : Settings->EventHandlerClasses)
	{
		if (EventHandlerClasses.Contains(Pair.Key))
		{
			continue;
		}
		CreateConfiguredHandler(Pair.Key, Pair.Value, TEXT("project settings"));
	}
}

void UH5UI_View::DeactivateEventHandlers(bool bUnregisterDynamicHandlers)
{
	for (const TPair<FName, TObjectPtr<UH5UI_EventHandler>>& Pair : ConfiguredEventHandlers)
	{
		if (Pair.Value)
		{
			Pair.Value->UnbindFromView();
		}
	}
	ConfiguredEventHandlers.Reset();

	for (const TPair<FName, TObjectPtr<UH5UI_EventHandler>>& Pair : RegisteredEventHandlers)
	{
		if (Pair.Value)
		{
			if (bUnregisterDynamicHandlers)
			{
				Pair.Value->UnbindFromView();
			}
			else
			{
				Pair.Value->Deactivate();
			}
		}
	}
	if (bUnregisterDynamicHandlers)
	{
		RegisteredEventHandlers.Reset();
	}
}

void UH5UI_View::NotifyJavaScriptError(const FString& Error)
{
	OnJavaScriptError.Broadcast(Error);
}

#undef LOCTEXT_NAMESPACE
