#include "H5UI_EventHandler.h"

#include "Dom/JsonValue.h"
#include "H5UI_View.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

namespace
{
	FString JsonValueToString(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid() || Value->IsNull())
		{
			return FString();
		}

		switch (Value->Type)
		{
		case EJson::String:
			return Value->AsString();
		case EJson::Number:
			return FString::SanitizeFloat(Value->AsNumber());
		case EJson::Boolean:
			return Value->AsBool() ? TEXT("true") : TEXT("false");
		default:
			return FString();
		}
	}

	bool GetNumberArgument(const TSharedPtr<FJsonValue>& Value, double& OutNumber)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Number)
		{
			OutNumber = Value->AsNumber();
			return true;
		}
		return LexTryParseString(OutNumber, *JsonValueToString(Value));
	}

	bool GetBoolArgument(const TSharedPtr<FJsonValue>& Value, bool& bOutValue)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Boolean)
		{
			bOutValue = Value->AsBool();
			return true;
		}
		return LexTryParseString(bOutValue, *JsonValueToString(Value));
	}

	bool AssignArgument(FProperty* Property, void* Address, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			StringProperty->SetPropertyValue(Address, JsonValueToString(Value));
			return true;
		}
		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			NameProperty->SetPropertyValue(Address, FName(*JsonValueToString(Value)));
			return true;
		}
		if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			TextProperty->SetPropertyValue(Address, FText::FromString(JsonValueToString(Value)));
			return true;
		}
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			bool bValue = false;
			if (!GetBoolArgument(Value, bValue))
			{
				OutError = FString::Printf(TEXT("Parameter '%s' requires a boolean."), *Property->GetName());
				return false;
			}
			BoolProperty->SetPropertyValue(Address, bValue);
			return true;
		}
		if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			double Number = 0.0;
			if (!GetNumberArgument(Value, Number))
			{
				OutError = FString::Printf(TEXT("Parameter '%s' requires a number."), *Property->GetName());
				return false;
			}
			if (NumericProperty->IsInteger())
			{
				NumericProperty->SetIntPropertyValue(Address, FMath::RoundToInt64(Number));
			}
			else
			{
				NumericProperty->SetFloatingPointPropertyValue(Address, Number);
			}
			return true;
		}
		if (CastField<FArrayProperty>(Property) || CastField<FStructProperty>(Property))
		{
			FText FailReason;
			if (FJsonObjectConverter::JsonValueToUProperty(
				Value,
				Property,
				Address,
				0,
				0,
				false,
				&FailReason))
			{
				return true;
			}

			OutError = FString::Printf(
				TEXT("Parameter '%s' could not be converted to '%s': %s"),
				*Property->GetName(),
				*Property->GetCPPType(),
				*FailReason.ToString());
			return false;
		}

		OutError = FString::Printf(
			TEXT("Parameter '%s' has unsupported type '%s'. Supported types are strings, names, text, booleans, numbers, structs, and arrays."),
			*Property->GetName(), *Property->GetClass()->GetName());
		return false;
	}
}

UH5UI_View* UH5UI_EventHandler::GetH5UIView() const
{
	return OwningView.Get();
}

FName UH5UI_EventHandler::GetEventType() const
{
	return EventType;
}

void UH5UI_EventHandler::SetContextObject(UObject* InContextObject)
{
	ContextObject = InContextObject;
}

UObject* UH5UI_EventHandler::GetContextObject() const
{
	return ContextObject.Get();
}

void UH5UI_EventHandler::OnH5UIStarted_Implementation(UH5UI_View*, FName)
{
}

void UH5UI_EventHandler::OnH5UIStopped_Implementation()
{
}

void UH5UI_EventHandler::BindToView(UH5UI_View* InView, FName InEventType)
{
	if (OwningView.Get() != InView || EventType != InEventType)
	{
		Deactivate();
	}
	OwningView = InView;
	EventType = InEventType;
	if (!ContextObject)
	{
		ContextObject = InView;
	}
}

void UH5UI_EventHandler::Activate()
{
	if (!bActive && OwningView.IsValid() && EventType != NAME_None)
	{
		bActive = true;
		OnH5UIStarted(OwningView.Get(), EventType);
	}
}

void UH5UI_EventHandler::Deactivate()
{
	if (bActive)
	{
		bActive = false;
		OnH5UIStopped();
	}
}

void UH5UI_EventHandler::UnbindFromView()
{
	Deactivate();
	OwningView.Reset();
	EventType = NAME_None;
}

bool UH5UI_EventHandler::DispatchEvent(const FH5UI_Event& Event)
{
	if (Event.Name == NAME_None)
	{
		return false;
	}

	UFunction* Function = FindFunction(Event.Name);
	if (!Function)
	{
		UE_LOG(LogTemp, Warning, TEXT("H5 UI event type '%s' has no function named '%s' on '%s'."),
			*Event.EventType.ToString(), *Event.Name.ToString(), *GetClass()->GetName());
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> Arguments;
	if (!Event.Payload.IsEmpty())
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Event.Payload);
		if (!FJsonSerializer::Deserialize(Reader, Arguments))
		{
			Arguments.Reset();
			Arguments.Add(MakeShared<FJsonValueString>(Event.Payload));
		}
	}

	TArray<FProperty*> InputParameters;
	for (TFieldIterator<FProperty> It(Function); It; ++It)
	{
		FProperty* Property = *It;
		if (Property->HasAnyPropertyFlags(CPF_Parm) &&
			!Property->HasAnyPropertyFlags(CPF_ReturnParm) &&
			(!Property->HasAnyPropertyFlags(CPF_OutParm) ||
				Property->HasAnyPropertyFlags(CPF_ConstParm)))
		{
			InputParameters.Add(Property);
		}
	}

	if (Arguments.Num() != InputParameters.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("H5 UI event '%s.%s' supplied %d parameters but '%s' requires %d."),
			*Event.EventType.ToString(), *Event.Name.ToString(), Arguments.Num(),
			*Function->GetName(), InputParameters.Num());
		return false;
	}

	TArray<uint8> Parameters;
	Parameters.SetNumZeroed(Function->ParmsSize);
	for (TFieldIterator<FProperty> It(Function); It; ++It)
	{
		if ((*It)->HasAnyPropertyFlags(CPF_Parm))
		{
			(*It)->InitializeValue_InContainer(Parameters.GetData());
		}
	}

	bool bSuccess = true;
	FString Error;
	for (int32 Index = 0; Index < InputParameters.Num(); ++Index)
	{
		FProperty* Property = InputParameters[Index];
		if (!AssignArgument(Property, Property->ContainerPtrToValuePtr<void>(Parameters.GetData()), Arguments[Index], Error))
		{
			bSuccess = false;
			UE_LOG(LogTemp, Warning, TEXT("H5 UI event '%s.%s' was not invoked: %s"),
				*Event.EventType.ToString(), *Event.Name.ToString(), *Error);
			break;
		}
	}

	if (bSuccess)
	{
		ProcessEvent(Function, Parameters.GetData());
	}

	for (TFieldIterator<FProperty> It(Function); It; ++It)
	{
		if ((*It)->HasAnyPropertyFlags(CPF_Parm))
		{
			(*It)->DestroyValue_InContainer(Parameters.GetData());
		}
	}
	return bSuccess;
}
