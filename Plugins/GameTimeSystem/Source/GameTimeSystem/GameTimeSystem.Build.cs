using UnrealBuildTool;

public class GameTimeSystem : ModuleRules
{
    public GameTimeSystem(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "DeveloperSettings", "UMG" });
    }
}
