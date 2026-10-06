// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EasyHouseBuilder : ModuleRules
{
	public EasyHouseBuilder(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"GeometryCore",
				"GeometryAlgorithms",
			}
			);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DynamicMesh",
				"EasyHouseBuilderManifold",
				"PhysicsCore",
				"RenderCore",
				"RHI",
			}
			);
	}
}
