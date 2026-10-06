using UnrealBuildTool;
using System.IO;

public class H5UIPlugin : ModuleRules
{
	public H5UIPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseRTTI = true;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"MediaAssets",
			"H5UIPluginQuickJS",
			"H5UIPluginRml",
			"Slate",
			"SlateCore",
			"UMG"
		});

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"ApplicationCore",
			"DeveloperSettings",
			"ImageCore",
			"ImageWrapper",
			"Json",
			"JsonUtilities",
			"Projects",
			"RenderCore",
			"RHI"
		});

		AddEngineThirdPartyPrivateStaticDependencies(Target, "Nanosvg");

		RuntimeDependencies.Add(
			Path.Combine(PluginDirectory, "Resources", "UI", "..."),
			StagedFileType.NonUFS);

		if (Target.ProjectFile != null)
		{
			RuntimeDependencies.Add(
				Path.Combine(Target.ProjectFile.Directory.FullName, "Content", "UI", "..."),
				StagedFileType.NonUFS);
		}
	}
}
