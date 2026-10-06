using UnrealBuildTool;
public class SceneManagementSystem : ModuleRules
{
    public SceneManagementSystem(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "GameplayTags", "DeveloperSettings" });
    }
}
