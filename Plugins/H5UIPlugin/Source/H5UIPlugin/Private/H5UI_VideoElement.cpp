#include "H5UI_VideoElement.h"

#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/Paths.h"
#include "RmlUi/Core/Core.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/MeshUtilities.h"
#include "RmlUi/Core/RenderManager.h"
#include "H5UI_Interfaces.h"
#include "H5UI_Module.h"

FH5UI_VideoElement::FH5UI_VideoElement(const Rml::String& Tag)
	: Rml::Element(Tag)
{
}

FH5UI_VideoElement::~FH5UI_VideoElement()
{
	CallbackTexture = {};
	Geometry = {};
	if (MediaPlayer.IsValid())
	{
		MediaPlayer->Close();
	}
	MediaTexture.Reset();
	MediaPlayer.Reset();
}

bool FH5UI_VideoElement::GetIntrinsicDimensions(Rml::Vector2f& Dimensions, float& Ratio)
{
	Dimensions = Rml::Vector2f(320.0f, 180.0f);
	Ratio = 16.0f / 9.0f;
	return true;
}

void FH5UI_VideoElement::OnUpdate()
{
	Rml::Element::OnUpdate();
	OpenMediaIfNeeded();
	if (MediaPlayer.IsValid() && MediaPlayer->IsPlaying() && GetContext())
	{
		GetContext()->RequestNextUpdate(0.0);
	}
}

void FH5UI_VideoElement::OnRender()
{
	OpenMediaIfNeeded();
	if (!MediaTexture.IsValid() || !GetContext())
	{
		return;
	}

	const Rml::Vector2f NewSize = GetBox().GetSize(Rml::BoxArea::Content);
	if (!Geometry || NewSize != GeometrySize)
	{
		RebuildGeometry();
	}
	if (!Geometry)
	{
		return;
	}

	Rml::RenderManager& RenderManager = GetContext()->GetRenderManager();
	if (!CallbackTexture)
	{
		CallbackTexture = RenderManager.MakeCallbackTexture(
			[WeakTexture = TWeakObjectPtr<UMediaTexture>(MediaTexture.Get())](const Rml::CallbackTextureInterface& Interface)
			{
				UMediaTexture* Texture = WeakTexture.Get();
				if (!Texture || !FH5UI_Module::IsAvailable())
				{
					return false;
				}

				const Rml::TextureHandle Handle = FH5UI_Module::Get().GetRenderInterface().CreateExternalTexture(Texture);
				if (!Handle)
				{
					return false;
				}
				Interface.SetTextureHandle(
					Handle,
					Rml::Vector2i(FMath::Max(1, Texture->GetWidth()), FMath::Max(1, Texture->GetHeight())));
				return true;
			});
	}

	Geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Content), CallbackTexture);
}

void FH5UI_VideoElement::OnResize()
{
	Rml::Element::OnResize();
	Geometry = {};
}

void FH5UI_VideoElement::OnAttributeChange(const Rml::ElementAttributes& ChangedAttributes)
{
	Rml::Element::OnAttributeChange(ChangedAttributes);
	OpenMediaIfNeeded();
	ApplyPlaybackAttributes();
}

void FH5UI_VideoElement::OnChildAdd(Rml::Element* Child)
{
	Rml::Element::OnChildAdd(Child);
	if (Child == this)
	{
		OpenMediaIfNeeded();
	}
}

void FH5UI_VideoElement::OpenMediaIfNeeded()
{
	const Rml::String SourceAttribute = GetAttribute<Rml::String>("src", "");
	if (SourceAttribute.empty())
	{
		if (!LoadedSource.IsEmpty() && MediaPlayer.IsValid())
		{
			MediaPlayer->Close();
			CallbackTexture = {};
		}
		LoadedSource.Reset();
		return;
	}

	Rml::ElementDocument* OwnerDocument = GetOwnerDocument();
	if (!OwnerDocument)
	{
		return;
	}

	Rml::String ResolvedSource = SourceAttribute;
	if (Rml::SystemInterface* SystemInterface = Rml::GetSystemInterface())
	{
		SystemInterface->JoinPath(ResolvedSource, OwnerDocument->GetSourceURL(), SourceAttribute);
	}
	const FString Source = UTF8_TO_TCHAR(ResolvedSource.c_str());
	if (Source == LoadedSource)
	{
		return;
	}

	LoadedSource = Source;
	if (!MediaPlayer.IsValid())
	{
		MediaPlayer = TStrongObjectPtr<UMediaPlayer>(NewObject<UMediaPlayer>());
		MediaTexture = TStrongObjectPtr<UMediaTexture>(NewObject<UMediaTexture>());
		MediaTexture->SetMediaPlayer(MediaPlayer.Get());
		MediaTexture->UpdateResource();
	}

	MediaPlayer->Close();
	ApplyPlaybackAttributes();

	const FString ResolvedPath = FH5UI_Module::Get().GetFileInterface().ResolvePath(Source);
	if (!ResolvedPath.IsEmpty() && FPaths::FileExists(ResolvedPath))
	{
		MediaPlayer->OpenFile(ResolvedPath);
	}
	else
	{
		MediaPlayer->OpenUrl(Source);
	}
	CallbackTexture = {};
}

void FH5UI_VideoElement::ApplyPlaybackAttributes()
{
	if (!MediaPlayer.IsValid())
	{
		return;
	}

	const bool bMuted = HasAttribute("muted");
	MediaPlayer->PlayOnOpen = HasAttribute("autoplay");
	MediaPlayer->NativeAudioOut = !bMuted;
	MediaPlayer->SetNativeVolume(bMuted ? 0.0f : 1.0f);
	MediaPlayer->SetLooping(HasAttribute("loop"));
}

void FH5UI_VideoElement::RebuildGeometry()
{
	if (!GetContext())
	{
		return;
	}

	GeometrySize = GetBox().GetSize(Rml::BoxArea::Content);
	if (GeometrySize.x <= 0.0f || GeometrySize.y <= 0.0f)
	{
		return;
	}

	Rml::Mesh Mesh;
	Rml::MeshUtilities::GenerateQuad(
		Mesh,
		Rml::Vector2f(0.0f),
		GeometrySize,
		Rml::ColourbPremultiplied(255, 255, 255, 255),
		Rml::Vector2f(0.0f, 0.0f),
		Rml::Vector2f(1.0f, 1.0f));
	Geometry = GetContext()->GetRenderManager().MakeGeometry(MoveTemp(Mesh));
}
