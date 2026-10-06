// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HybridMotionSystem : ModuleRules
{
    public HybridMotionSystem(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(new string[] { });
        PrivateIncludePaths.AddRange(new string[] { });

        // ==================== 必须是 Public 依赖 ====================
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "Chooser",
				"ProxyTable",
                "Mover",
                "PoseSearch",
                "MotionWarping",
                "GameplayTags",
                "BlendStack",
                "AnimationWarpingRuntime",
                "AnimationCore",
                "AnimGraphRuntime",
                "AnimationLocomotionLibraryRuntime",
                "SmartObjectsModule",
                "GameplayInteractionsModule",
                "StateTreeModule",
                "AIModule",
                "NavigationSystem",
                "PhysicsControl",
                // HMS_MovementStruct.h 公开包含 FHMS_MoverInput，因此消费者也必须可见该模块。
                "HMS_Mover"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Slate",
                "SlateCore"
                // ... 其他私有依赖
            }
        );

        DynamicallyLoadedModuleNames.AddRange(new string[] { });
    }
}
