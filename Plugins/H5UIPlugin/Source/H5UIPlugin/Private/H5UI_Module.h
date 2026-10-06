#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FH5UI_FileInterface;
class FH5UI_RenderInterface;
class FH5UI_SystemInterface;

namespace Rml
{
	class ElementInstancer;
}

class FH5UI_Module final : public IModuleInterface
{
public:
	static FH5UI_Module& Get();
	static bool IsAvailable();

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	FH5UI_RenderInterface& GetRenderInterface() const;
	FH5UI_FileInterface& GetFileInterface() const;
	FString GetPluginResourceRoot() const;

private:
	void LoadConfiguredFonts();

	TUniquePtr<FH5UI_SystemInterface> SystemInterface;
	TUniquePtr<FH5UI_FileInterface> FileInterface;
	TUniquePtr<FH5UI_RenderInterface> RenderInterface;
	TUniquePtr<Rml::ElementInstancer> VideoElementInstancer;
	TUniquePtr<Rml::ElementInstancer> CanvasElementInstancer;
	TArray<TArray<uint8>> LoadedFontData;
	FString PluginResourceRoot;
	bool bRmlInitialised = false;
};
