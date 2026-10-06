using UnrealBuildTool;

public class MapTransitionSystem : ModuleRules
{
	public MapTransitionSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",
			"GameplayTags"
		});
	}
}
