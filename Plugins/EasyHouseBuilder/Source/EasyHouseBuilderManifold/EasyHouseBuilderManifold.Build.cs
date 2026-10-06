// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class EasyHouseBuilderManifold : ModuleRules
{
	public EasyHouseBuilderManifold(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bWarningsAsErrors = false;

		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.MSVCDeprecationWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.DeprecationWarningLevel = WarningLevel.Off;

		PrivateDefinitions.AddRange(
			new string[]
			{
				"MANIFOLD_PAR=-1",
				"MANIFOLD_NO_IOSTREAM=1",
				"MANIFOLD_NO_FILESYSTEM=1",
				"TRACY_ENABLE=0",
				"TRACY_MEMORY_USAGE=0",
				"_SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING=1",
			}
			);

		PublicIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "Private", "ThirdParty", "manifold", "include"),
			}
			);

		PrivateIncludePaths.AddRange(
			new string[]
			{
				Path.Combine(ModuleDirectory, "Private", "ThirdParty", "manifold", "src"),
			}
			);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"GeometryCore",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DynamicMesh",
				"GeometryAlgorithms",
			}
			);
	}
}
