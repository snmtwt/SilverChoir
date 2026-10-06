using UnrealBuildTool;

public class HybridMotionSystemEditor : ModuleRules
{
    public HybridMotionSystemEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTags",
            "HybridMotionSystem"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "AnimationEditor",
			"AnimationBlueprintLibrary",
			"MotionWarping",
            "AnimGraph",
            "AnimationWarpingEditor",
            "BlueprintGraph",
            "KismetCompiler",
            "UnrealEd",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "Persona",
            "AnimationEditor",
            "EditorFramework",
            "AssetRegistry",
            "PropertyEditor",
            "Chooser",
            "PoseSearch",
            "StateTreeModule",
            "StateTreeEditorModule",
            "PropertyBindingUtils",
            "GameplayInteractionsModule",
            "SmartObjectsModule",
            "IKRig",
            "IKRigEditor"
        });
    }
}
