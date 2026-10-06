using UnrealBuildTool;

public class CoverSmartObjects : ModuleRules
{
    public CoverSmartObjects(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "SmartObjectsModule", "GameplayTags", "NavigationSystem", "AIModule" });
        PrivateDependencyModuleNames.Add("WorldConditions");
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
    }
}
