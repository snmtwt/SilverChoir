// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class SilverChoir : ModuleRules
{
	public SilverChoir(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "DeveloperSettings", "UMG", "SlateCore", "MapTransitionSystem", "GameplayTags", "EasyHouseBuilder" });

		PublicDependencyModuleNames.AddRange(new string[] { "FreeCameraSystem", "StrategyInventorySystem", "Mover", "HMS_Mover", "HybridMotionSystem", "AIModule", "NavigationSystem", "EnhancedInput", "GridStrategyMapSystem" });
		PublicDependencyModuleNames.Add("SceneManagementSystem");
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "H5UIPlugin", "GameTimeSystem" });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
