using UnrealBuildTool;

public class SilverChoirEditor : ModuleRules
{
    public SilverChoirEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "InputCore", "UnrealEd", "UMG", "UMGEditor", "MaterialEditor", "GameplayTags", "StrategyInventorySystem",
            "Slate", "SlateCore", "Kismet", "KismetCompiler", "BlueprintGraph", "AssetRegistry", "AssetTools", "SilverChoir", "H5UIPlugin", "MapTransitionSystem", "SceneManagementSystem", "GridStrategyMapSystem", "GameTimeSystem", "EnhancedInput", "InputBlueprintNodes", "FreeCameraSystem", "MovieScene", "MovieSceneTracks", "NavigationSystem", "HMS_Mover", "HybridMotionSystem"
        });
    }
}

