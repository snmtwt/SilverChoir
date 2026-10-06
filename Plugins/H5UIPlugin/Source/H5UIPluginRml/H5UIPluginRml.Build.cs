using UnrealBuildTool;
using System.IO;

public class H5UIPluginRml : ModuleRules
{
	public H5UIPluginRml(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bUseRTTI = true;
		bEnableExceptions = false;

		PublicDependencyModuleNames.Add("Core");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "FreeType2", "UElibPNG", "zlib");

		string RmlRoot = Path.Combine(ModuleDirectory, "Private", "RmlUi");
		PublicSystemIncludePaths.Add(Path.Combine(RmlRoot, "Include"));
		PrivateIncludePaths.Add(Path.Combine(RmlRoot, "Source", "Core"));

		PrivateDefinitions.Add("RMLUI_CORE_EXPORTS=1");
		PrivateDefinitions.Add("RMLUI_FONT_ENGINE_FREETYPE=1");
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicDefinitions.Add("RMLUI_PLATFORM_WIN32=1");
			PublicDefinitions.Add("RMLUI_PLATFORM_WIN32_NATIVE=1");
		}
	}
}
