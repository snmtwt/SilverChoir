#pragma once

#include "CoreMinimal.h"
#include "RmlUi/Core/CallbackTexture.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/Geometry.h"
#include "UObject/StrongObjectPtr.h"

class UMediaPlayer;
class UMediaTexture;

class FH5UI_VideoElement final : public Rml::Element
{
public:
	RMLUI_RTTI_DefineWithParent(FH5UI_VideoElement, Rml::Element)

	explicit FH5UI_VideoElement(const Rml::String& Tag);
	virtual ~FH5UI_VideoElement() override;

	virtual bool GetIntrinsicDimensions(Rml::Vector2f& Dimensions, float& Ratio) override;
	virtual void OnUpdate() override;
	virtual void OnRender() override;
	virtual void OnResize() override;
	virtual void OnAttributeChange(const Rml::ElementAttributes& ChangedAttributes) override;
	virtual void OnChildAdd(Rml::Element* Child) override;

private:
	void OpenMediaIfNeeded();
	void ApplyPlaybackAttributes();
	void RebuildGeometry();

	TStrongObjectPtr<UMediaPlayer> MediaPlayer;
	TStrongObjectPtr<UMediaTexture> MediaTexture;
	Rml::CallbackTexture CallbackTexture;
	Rml::Geometry Geometry;
	Rml::Vector2f GeometrySize = Rml::Vector2f(0.0f);
	FString LoadedSource;
};
