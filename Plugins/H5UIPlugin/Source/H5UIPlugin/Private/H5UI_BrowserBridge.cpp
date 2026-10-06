#include "H5UI_BrowserBridge.h"

#include "Dom/JsonValue.h"
#include "H5UI_View.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogH5UIBrowserBridge, Log, All);

void UH5UI_BrowserBridge::Initialize(UH5UI_View* InOwner)
{
	Owner = InOwner;
}

void UH5UI_BrowserBridge::Emit(
	const FString& EventType,
	const FString& FunctionName,
	const FString& ArgumentsJson,
	const FString& ElementId)
{
	UH5UI_View* View = Owner.Get();
	if (!View || EventType.IsEmpty() || FunctionName.IsEmpty())
	{
		return;
	}

	// Reserved iframe-to-host protocol. This bypasses configured typed handlers so a CEF
	// child can notify its owning native RmlUi document without project-specific setup.
	if (EventType.Equals(TEXT("H5UIHost"), ESearchCase::CaseSensitive) &&
		FunctionName.Equals(TEXT("DispatchHtmlEvent"), ESearchCase::CaseSensitive))
	{
		TArray<TSharedPtr<FJsonValue>> Arguments;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
		FString HostEventName;
		FString Detail;
		if (!FJsonSerializer::Deserialize(Reader, Arguments) || Arguments.IsEmpty() ||
			!Arguments[0].IsValid() || !Arguments[0]->TryGetString(HostEventName) || HostEventName.IsEmpty())
		{
			UE_LOG(LogH5UIBrowserBridge, Warning,
				TEXT("Rejected malformed H5UIHost.DispatchHtmlEvent payload: %s"), *ArgumentsJson);
			return;
		}
		if (Arguments.Num() > 1 && Arguments[1].IsValid() && !Arguments[1]->IsNull())
		{
			if (!Arguments[1]->TryGetString(Detail))
			{
				UE_LOG(LogH5UIBrowserBridge, Warning,
					TEXT("H5UIHost.DispatchHtmlEvent detail must be a string: %s"), *ArgumentsJson);
				return;
			}
		}

		UE_LOG(LogH5UIBrowserBridge, Log, TEXT("Iframe -> host document: %s %s"), *HostEventName, *Detail);
		View->DispatchHtmlEvent(FName(*HostEventName), Detail);
		return;
	}

	FH5UI_Event Event;
	Event.EventType = FName(*EventType);
	Event.Name = FName(*FunctionName);
	Event.Payload = ArgumentsJson;
	Event.ElementId = ElementId;
	UE_LOG(LogH5UIBrowserBridge, Log, TEXT("Iframe -> UE: %s.%s %s"), *EventType, *FunctionName, *ArgumentsJson);
	View->NotifyUIEvent(Event);
}
