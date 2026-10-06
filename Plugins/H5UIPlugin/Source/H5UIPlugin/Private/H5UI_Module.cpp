#include "H5UI_Module.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RmlUi/Core.h"
#include "RmlUi/Core/ElementInstancer.h"
#include "RmlUi/Core/Factory.h"
#include "H5UI_Interfaces.h"
#include "H5UI_Settings.h"
#include "H5UI_VideoElement.h"
#include "H5UI_CanvasElement.h"

DEFINE_LOG_CATEGORY_STATIC(LogH5UIPlugin, Log, All);

FH5UI_Module& FH5UI_Module::Get()
{
	return FModuleManager::LoadModuleChecked<FH5UI_Module>(TEXT("H5UIPlugin"));
}

bool FH5UI_Module::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("H5UIPlugin"));
}

void FH5UI_Module::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("H5UIPlugin"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogH5UIPlugin, Error, TEXT("Unable to locate the H5UIPlugin plugin."));
		return;
	}

	PluginResourceRoot = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/UI"));
	SystemInterface = MakeUnique<FH5UI_SystemInterface>();
	FileInterface = MakeUnique<FH5UI_FileInterface>();
	RenderInterface = MakeUnique<FH5UI_RenderInterface>();
	FileInterface->SetPluginResourceRoot(PluginResourceRoot);
	RenderInterface->SetFileInterface(FileInterface.Get());

	Rml::SetSystemInterface(SystemInterface.Get());
	Rml::SetFileInterface(FileInterface.Get());
	Rml::SetRenderInterface(RenderInterface.Get());

	if (!Rml::Initialise())
	{
		UE_LOG(LogH5UIPlugin, Error, TEXT("RmlUi failed to initialise."));
		return;
	}

	bRmlInitialised = true;
	VideoElementInstancer = MakeUnique<Rml::ElementInstancerGeneric<FH5UI_VideoElement>>();
	Rml::Factory::RegisterElementInstancer("video", VideoElementInstancer.Get());
	CanvasElementInstancer = MakeUnique<Rml::ElementInstancerGeneric<FH5UI_CanvasElement>>();
	Rml::Factory::RegisterElementInstancer("canvas", CanvasElementInstancer.Get());
	LoadConfiguredFonts();
	UE_LOG(LogH5UIPlugin, Log, TEXT("H5UIPlugin initialised with the native RmlUi renderer."));
}

void FH5UI_Module::ShutdownModule()
{
	if (bRmlInitialised)
	{
		Rml::Shutdown();
		bRmlInitialised = false;
	}
	LoadedFontData.Reset();

	VideoElementInstancer.Reset();
	CanvasElementInstancer.Reset();
	RenderInterface.Reset();
	FileInterface.Reset();
	SystemInterface.Reset();
}

FH5UI_RenderInterface& FH5UI_Module::GetRenderInterface() const
{
	check(RenderInterface);
	return *RenderInterface;
}

FH5UI_FileInterface& FH5UI_Module::GetFileInterface() const
{
	check(FileInterface);
	return *FileInterface;
}

FString FH5UI_Module::GetPluginResourceRoot() const
{
	return PluginResourceRoot;
}

void FH5UI_Module::LoadConfiguredFonts()
{
	struct FFontFile
	{
		FString Path;
		Rml::String Family;
		Rml::Style::FontStyle Style = Rml::Style::FontStyle::Normal;
		Rml::Style::FontWeight Weight = Rml::Style::FontWeight::Auto;
		bool bFallback = false;
	};

	TArray<FFontFile> FontFiles;
	FontFiles.Add({
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"),
		"Roboto",
		Rml::Style::FontStyle::Normal,
		Rml::Style::FontWeight::Normal,
		false});
	FontFiles.Add({
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Light.ttf"),
		"Roboto",
		Rml::Style::FontStyle::Normal,
		static_cast<Rml::Style::FontWeight>(300),
		false});
	FontFiles.Add({
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Medium.ttf"),
		"Roboto",
		Rml::Style::FontStyle::Normal,
		static_cast<Rml::Style::FontWeight>(500),
		false});
	FontFiles.Add({
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"),
		"Roboto",
		Rml::Style::FontStyle::Normal,
		Rml::Style::FontWeight::Bold,
		false});
	FontFiles.Add({
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Black.ttf"),
		"Roboto",
		Rml::Style::FontStyle::Normal,
		static_cast<Rml::Style::FontWeight>(900),
		false});
	FontFiles.Add({
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Italic.ttf"),
		"Roboto",
		Rml::Style::FontStyle::Italic,
		Rml::Style::FontWeight::Normal,
		false});
	FontFiles.Add({
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-BoldItalic.ttf"),
		"Roboto",
		Rml::Style::FontStyle::Italic,
		Rml::Style::FontWeight::Bold,
		false});
	FontFiles.Add({
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansMono.ttf"),
		"Droid Sans Mono",
		Rml::Style::FontStyle::Normal,
		Rml::Style::FontWeight::Normal,
		false});
	FontFiles.Add({
		FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf"),
		"Silver CJK Fallback",
		Rml::Style::FontStyle::Normal,
		Rml::Style::FontWeight::Auto,
		true});

	const UH5UI_Settings* Settings = GetDefault<UH5UI_Settings>();
	for (int32 FontIndex = 0; FontIndex < Settings->FallbackFonts.Num(); ++FontIndex)
	{
		FString ResolvedPath = Settings->FallbackFonts[FontIndex].FilePath;
		if (FPaths::IsRelative(ResolvedPath))
		{
			ResolvedPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ResolvedPath);
		}
		FontFiles.Add({
			MoveTemp(ResolvedPath),
			Rml::String("SilverFallback") + std::to_string(FontIndex),
			Rml::Style::FontStyle::Normal,
			Rml::Style::FontWeight::Auto,
			true});
	}

	for (const FFontFile& Font : FontFiles)
	{
		if (!FPaths::FileExists(Font.Path))
		{
			UE_LOG(LogH5UIPlugin, Warning, TEXT("Font does not exist: %s"), *Font.Path);
			continue;
		}

		TArray<uint8>& Bytes = LoadedFontData.AddDefaulted_GetRef();
		if (!FFileHelper::LoadFileToArray(Bytes, *Font.Path) || !Rml::LoadFontFace(
			Rml::Span<const Rml::byte>(Bytes.GetData(), Bytes.Num()),
			Font.Family,
			Font.Style,
			Font.Weight,
			Font.bFallback))
		{
			UE_LOG(LogH5UIPlugin, Warning, TEXT("Failed to load font: %s"), *Font.Path);
			LoadedFontData.Pop();
		}
	}
}

IMPLEMENT_MODULE(FH5UI_Module, H5UIPlugin)
