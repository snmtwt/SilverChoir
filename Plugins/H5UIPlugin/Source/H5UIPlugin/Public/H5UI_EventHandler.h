#pragma once

#include "CoreMinimal.h"
#include "H5UI_Types.h"
#include "UObject/Object.h"
#include "H5UI_EventHandler.generated.h"

class UH5UI_View;

/** Per-view receiver for typed events emitted by an H5 UI page. */
UCLASS(Blueprintable, BlueprintType)
class H5UIPLUGIN_API UH5UI_EventHandler : public UObject
{
	GENERATED_BODY()

public:
	/** The H5 UI view that owns this handler instance. */
	UFUNCTION(BlueprintPure, Category = "H5 UI Plugin|Events")
	UH5UI_View* GetH5UIView() const;

	/** The event-type key that selected this handler from project settings. */
	UFUNCTION(BlueprintPure, Category = "H5 UI Plugin|Events")
	FName GetEventType() const;

	/** Sets an optional game-specific context object for this handler. */
	UFUNCTION(BlueprintCallable, Category = "H5 UI Plugin|Context",
		meta = (DisplayName = "Set H5UI Event Handler Context"))
	void SetContextObject(UObject* InContextObject);

	/** Returns this handler's game-specific context object. Defaults to its owning H5 UI View. */
	UFUNCTION(BlueprintPure, Category = "H5 UI Plugin|Context",
		meta = (DisplayName = "Get H5UI Event Handler Context"))
	UObject* GetContextObject() const;

	/** Called after the handler is created for a ready H5 UI view. */
	UFUNCTION(BlueprintNativeEvent, Category = "H5 UI Plugin|Lifecycle")
	void OnH5UIStarted(UH5UI_View* View, FName InEventType);

	/** Called before this handler is released during close, reload, or destruction. */
	UFUNCTION(BlueprintNativeEvent, Category = "H5 UI Plugin|Lifecycle")
	void OnH5UIStopped();

private:
	friend class UH5UI_View;

	void BindToView(UH5UI_View* InView, FName InEventType);
	void Activate();
	void Deactivate();
	void UnbindFromView();
	bool DispatchEvent(const FH5UI_Event& Event);

	TWeakObjectPtr<UH5UI_View> OwningView;
	FName EventType = NAME_None;

protected:
	UPROPERTY(BlueprintReadOnly, Transient, Category = "H5 UI Plugin|Context")
	TObjectPtr<UObject> ContextObject = nullptr;

private:
	bool bActive = false;
};
