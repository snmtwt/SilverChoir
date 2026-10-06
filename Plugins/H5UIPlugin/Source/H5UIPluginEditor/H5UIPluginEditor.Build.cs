using UnrealBuildTool;

public class H5UIPluginEditor : ModuleRules
{
	public H5UIPluginEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"H5UIPlugin",
			"Slate",
			"SlateCore",
			"UnrealEd"
		});
	}
}
