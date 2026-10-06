using UnrealBuildTool;

public class GridStrategyMapSystem : ModuleRules
{
	public GridStrategyMapSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"InputCore",
			"ProceduralMeshComponent",
			"Slate",
			"SlateCore",
			"UMG"
		});
	}
}
