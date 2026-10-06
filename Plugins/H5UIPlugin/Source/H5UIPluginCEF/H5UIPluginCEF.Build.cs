using UnrealBuildTool;

public class H5UIPluginCEF : ModuleRules
{
	public H5UIPluginCEF(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"H5UIPlugin",
			"Slate",
			"SlateCore",
			"WebBrowser"
		});
	}
}
