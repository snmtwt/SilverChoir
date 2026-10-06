// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EasyHouseBuilderEditor : ModuleRules
{
	public EasyHouseBuilderEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"DesktopPlatform",
				"Engine",
				"ApplicationCore",
				"AssetRegistry",
				"AssetTools",
				"BlueprintGraph",
				"EditorFramework",
				"DynamicMesh",
				"GeometryCore",
				"HTTP",
				"InputCore",
				"Json",
				"JsonUtilities",
				"KismetCompiler",
				"LevelEditor",
				"MeshDescription",
				"EasyHouseBuilder",
				"Projects",
				"PropertyEditor",
				"RenderCore",
				"Slate",
				"SlateCore",
				"StaticMeshDescription",
				"ToolsetRegistry",
				"UnrealEd"
			}
		);
	}
}
