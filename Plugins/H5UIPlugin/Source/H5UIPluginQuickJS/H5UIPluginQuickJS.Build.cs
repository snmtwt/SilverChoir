using UnrealBuildTool;
using System.IO;

public class H5UIPluginQuickJS : ModuleRules
{
	public H5UIPluginQuickJS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bEnableExceptions = false;
		CStandard = CStandardVersion.C11;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
		CppCompileWarningSettings.UnreachableCodeWarningLevel = WarningLevel.Off;

		PublicDependencyModuleNames.Add("Core");

		string QuickJSRoot = Path.Combine(ModuleDirectory, "Private", "QuickJS");
		PublicSystemIncludePaths.Add(QuickJSRoot);

		PrivateDefinitions.Add("BUILDING_QJS_SHARED=1");
		PublicDefinitions.Add("USING_QJS_SHARED=1");
		PrivateDefinitions.Add("_CRT_SECURE_NO_WARNINGS=1");
	}
}
