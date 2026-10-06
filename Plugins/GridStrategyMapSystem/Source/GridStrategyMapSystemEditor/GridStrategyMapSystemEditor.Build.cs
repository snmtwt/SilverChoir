using UnrealBuildTool;

public class GridStrategyMapSystemEditor : ModuleRules
{
	public GridStrategyMapSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EditorSubsystem",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"LevelEditor",
			"ContentBrowser",
			"AssetRegistry",
			"ActorPickerMode",
			"PropertyEditor",
			"InputCore",
			"UnrealEd",
			"GridStrategyMapSystem"
		});
	}
}
