using UnrealBuildTool;
public class FreeCameraSystem : ModuleRules
{
    public FreeCameraSystem(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore" });
        PrivateDependencyModuleNames.Add("ApplicationCore");
    }
}
