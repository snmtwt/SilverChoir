#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimTypes.h"
#include "BoneControllers/BoneControllerTypes.h"
#include "AnimNode_CurveDrivenBoneBlend.generated.h"

/**
 * 单条规则：
 * - Bone: 要影响的起始骨骼
 * - CurveName: 从 OverlayPose 中读取的曲线名
 * - Multiplier: 曲线倍率
 * - bAffectChildren: 是否影响该骨骼以下的所有子骨骼
 */
USTRUCT(BlueprintType)
struct HYBRIDMOTIONSYSTEM_API FHMS_CurveDrivenBoneRule
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "CurveDrivenBlend")
	FBoneReference Bone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CurveDrivenBlend")
	FName CurveName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CurveDrivenBlend")
	float Multiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CurveDrivenBlend")
	bool bAffectChildren = true;
};


USTRUCT(BlueprintType)
struct HYBRIDMOTIONSYSTEM_API FHMS_BlendSetting
{
	GENERATED_BODY()

public:

	FHMS_BlendSetting() {
		InitDefault();
	}

	/** 骨骼-曲线规则 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FHMS_CurveDrivenBoneRule> CurveBoneRules;

public:

	// 初始化默认配置
	void InitDefault()
	{
		CurveBoneRules.Empty();

		// 工具函数：快速添加一条规则
		auto AddRule = [this](const FName& BoneName, const FName& CurveName, float Multiplier = 1.f, bool bAffectChildren = true)
			{
				FHMS_CurveDrivenBoneRule Rule;
				Rule.Bone.BoneName = BoneName;
				Rule.CurveName = CurveName;
				Rule.Multiplier = Multiplier;
				Rule.bAffectChildren = bAffectChildren;

				CurveBoneRules.Add(Rule);
			};

		// ===== 躯干 =====
		AddRule(TEXT("spine_01"), TEXT("spine_01"));
		AddRule(TEXT("spine_02"), TEXT("spine_02"));
		AddRule(TEXT("spine_03"), TEXT("spine_03"));
		AddRule(TEXT("spine_04"), TEXT("spine_04"));
		AddRule(TEXT("spine_05"), TEXT("spine_05"));

		// ===== 盆骨 =====
		AddRule(TEXT("pelvis"), TEXT("pelvis"));

		// ===== 左臂 =====
		AddRule(TEXT("clavicle_l"), TEXT("clavicle_l"));
		AddRule(TEXT("upperarm_l"), TEXT("upperarm_l"));
		AddRule(TEXT("lowerarm_l"), TEXT("lowerarm_l"));
		AddRule(TEXT("hand_l"), TEXT("hand_l"));

		// ===== 右臂 =====
		AddRule(TEXT("clavicle_r"), TEXT("clavicle_r"));
		AddRule(TEXT("upperarm_r"), TEXT("upperarm_r"));
		AddRule(TEXT("lowerarm_r"), TEXT("lowerarm_r"));
		AddRule(TEXT("hand_r"), TEXT("hand_r"));

		// ===== 左腿 =====
		AddRule(TEXT("thigh_l"), TEXT("thigh_l"));
		AddRule(TEXT("calf_l"), TEXT("calf_l"));
		AddRule(TEXT("foot_l"), TEXT("foot_l"));
		AddRule(TEXT("ball_l"), TEXT("ball_l"));

		// ===== 右腿 =====
		AddRule(TEXT("thigh_r"), TEXT("thigh_r"));
		AddRule(TEXT("calf_r"), TEXT("calf_r"));
		AddRule(TEXT("foot_r"), TEXT("foot_r"));
		AddRule(TEXT("ball_r"), TEXT("ball_r"));
	}
};

/**
 * BasePose 与 OverlayPose 的逐骨曲线驱动混合节点
 */
USTRUCT(BlueprintInternalUseOnly)
struct HYBRIDMOTIONSYSTEM_API FAnimNode_CurveDrivenBoneBlend : public FAnimNode_Base
{
	GENERATED_BODY()

public:
	/** 基础姿势 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Links")
	FPoseLink BasePose;

	/** 叠加姿势，曲线也从这里读取 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Links")
	FPoseLink OverlayPose;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PinShownByDefault))
	FHMS_BlendSetting BlendSetting;

	/** 是否使用 Mesh Space 旋转混合 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bMeshSpaceRotationBlend = true;

	/** 是否使用 Mesh Space 缩放混合 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	bool bMeshSpaceScaleBlend = false;

	/** 曲线混合策略 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TEnumAsByte<ECurveBlendOption::Type> CurveBlendOption = ECurveBlendOption::Override;

public:
	FAnimNode_CurveDrivenBoneBlend() = default;

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

private:
	/** 每条规则最终影响到的 RequiredBones 下标 */
	TArray<TArray<int32>> CachedAffectedBoneIndices;

	/** 当前帧逐骨混合权重 */
	TArray<FPerBoneBlendWeight> CurrentBoneBlendWeights;

private:
	void RebuildRuleBoneCache(const FBoneContainer& RequiredBones);

	static float ReadCurveValueSafe(const FBlendedCurve& Curve, FName CurveName);
};