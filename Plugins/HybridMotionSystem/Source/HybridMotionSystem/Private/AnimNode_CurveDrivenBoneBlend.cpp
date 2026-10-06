#include "AnimNode_CurveDrivenBoneBlend.h"

#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimTrace.h"

void FAnimNode_CurveDrivenBoneBlend::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Initialize_AnyThread);
	FAnimNode_Base::Initialize_AnyThread(Context);

	BasePose.Initialize(Context);
	OverlayPose.Initialize(Context);
}

void FAnimNode_CurveDrivenBoneBlend::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(CacheBones_AnyThread);

	BasePose.CacheBones(Context);
	OverlayPose.CacheBones(Context);

	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();
	RebuildRuleBoneCache(RequiredBones);
}

void FAnimNode_CurveDrivenBoneBlend::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Update_AnyThread);

	// 如果你的节点里有 PinShownByDefault 的可暴露输入，这句通常要保留
	GetEvaluateGraphExposedInputs().Execute(Context);

	BasePose.Update(Context);
	OverlayPose.Update(Context);
}

void FAnimNode_CurveDrivenBoneBlend::Evaluate_AnyThread(FPoseContext& Output)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(Evaluate_AnyThread);
	//ANIM_MT_SCOPE_CYCLE_COUNTER(CurveDrivenBoneBlend, !IsInGameThread());

	// 没规则时直接输出 BasePose
	if (BlendSetting.CurveBoneRules.IsEmpty())
	{
		BasePose.Evaluate(Output);
		return;
	}

	FPoseContext BasePoseContext(Output);
	FPoseContext OverlayPoseContext(Output);

	BasePose.Evaluate(BasePoseContext);
	OverlayPose.Evaluate(OverlayPoseContext);

	const FBoneContainer& BoneContainer = BasePoseContext.Pose.GetBoneContainer();
	const int32 NumBones = BoneContainer.GetBoneIndicesArray().Num();

	CurrentBoneBlendWeights.Reset();
	CurrentBoneBlendWeights.AddZeroed(NumBones);

	// 默认所有骨骼都不混 Overlay
	for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
	{
		CurrentBoneBlendWeights[BoneIdx].SourceIndex = 0;   // 只有一个 OverlayPose，所以 SourceIndex 固定为 0
		CurrentBoneBlendWeights[BoneIdx].BlendWeight = 0.0f;
	}

	// 逐规则读取 OverlayPose 的曲线值，写入逐骨权重
	for (int32 RuleIndex = 0; RuleIndex < BlendSetting.CurveBoneRules.Num(); ++RuleIndex)
	{
		if (!CachedAffectedBoneIndices.IsValidIndex(RuleIndex))
		{
			continue;
		}

		const FHMS_CurveDrivenBoneRule& Rule = BlendSetting.CurveBoneRules[RuleIndex];

		if (Rule.CurveName.IsNone())
		{
			continue;
		}

		const float CurveValue = ReadCurveValueSafe(OverlayPoseContext.Curve, Rule.CurveName);
		const float FinalAlpha = FMath::Clamp(CurveValue * Rule.Multiplier, 0.0f, 1.0f);

		// 0 值直接跳过
		if (FinalAlpha <= ZERO_ANIMWEIGHT_THRESH)
		{
			continue;
		}

		const TArray<int32>& AffectedBones = CachedAffectedBoneIndices[RuleIndex];
		for (const int32 BoneIdx : AffectedBones)
		{
			if (!CurrentBoneBlendWeights.IsValidIndex(BoneIdx))
			{
				continue;
			}

			// 多条规则命中同一骨骼时，先用 Max 处理
			CurrentBoneBlendWeights[BoneIdx].SourceIndex = 0;
			CurrentBoneBlendWeights[BoneIdx].BlendWeight =
				FMath::Max(CurrentBoneBlendWeights[BoneIdx].BlendWeight, FinalAlpha);
		}
	}

	// 如果所有规则结果都是 0，直接输出 BasePose
	bool bHasAnyBlendWeight = false;
	for (const FPerBoneBlendWeight& Weight : CurrentBoneBlendWeights)
	{
		if (Weight.BlendWeight > ZERO_ANIMWEIGHT_THRESH)
		{
			bHasAnyBlendWeight = true;
			break;
		}
	}

	if (!bHasAnyBlendWeight)
	{
		Output = MoveTemp(BasePoseContext);
		return;
	}

	// 构造单个 OverlayPose 的数组输入，复用 UE 原生 per-bone blend
	TArray<FCompactPose> BlendPoses;
	TArray<FBlendedCurve> BlendCurves;
	TArray<UE::Anim::FStackAttributeContainer> BlendAttributes;

	BlendPoses.SetNum(1);
	BlendCurves.SetNum(1);
	BlendAttributes.SetNum(1);

	BlendPoses[0].MoveBonesFrom(OverlayPoseContext.Pose);
	BlendCurves[0].MoveFrom(OverlayPoseContext.Curve);
	BlendAttributes[0].MoveFrom(OverlayPoseContext.CustomAttributes);

	FAnimationRuntime::EBlendPosesPerBoneFilterFlags BlendFlags =
		FAnimationRuntime::EBlendPosesPerBoneFilterFlags::None;

	if (bMeshSpaceRotationBlend)
	{
		BlendFlags |= FAnimationRuntime::EBlendPosesPerBoneFilterFlags::MeshSpaceRotation;
	}

	if (bMeshSpaceScaleBlend)
	{
		BlendFlags |= FAnimationRuntime::EBlendPosesPerBoneFilterFlags::MeshSpaceScale;
	}

	FAnimationPoseData OutAnimationPoseData(Output);

	FAnimationRuntime::BlendPosesPerBoneFilter(
		BasePoseContext.Pose,
		BlendPoses,
		BasePoseContext.Curve,
		BlendCurves,
		BasePoseContext.CustomAttributes,
		BlendAttributes,
		OutAnimationPoseData,
		CurrentBoneBlendWeights,
		BlendFlags,
		CurveBlendOption
	);
}

void FAnimNode_CurveDrivenBoneBlend::GatherDebugData(FNodeDebugData& DebugData)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(GatherDebugData);

	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT("(Rules: %d)"), BlendSetting.CurveBoneRules.Num());
	DebugData.AddDebugItem(DebugLine);

	BasePose.GatherDebugData(DebugData.BranchFlow(1.0f));
	OverlayPose.GatherDebugData(DebugData.BranchFlow(1.0f));
}

void FAnimNode_CurveDrivenBoneBlend::RebuildRuleBoneCache(const FBoneContainer& RequiredBones)
{
	CachedAffectedBoneIndices.Reset();
	CachedAffectedBoneIndices.SetNum(BlendSetting.CurveBoneRules.Num());

	const TArray<FBoneIndexType>& RequiredBoneIndices = RequiredBones.GetBoneIndicesArray();
	const int32 NumRequiredBones = RequiredBoneIndices.Num();

	for (int32 RuleIndex = 0; RuleIndex < BlendSetting.CurveBoneRules.Num(); ++RuleIndex)
	{
		FHMS_CurveDrivenBoneRule& Rule = BlendSetting.CurveBoneRules[RuleIndex];

		// 初始化 FBoneReference
		Rule.Bone.Initialize(RequiredBones);

		const FCompactPoseBoneIndex RootCompactIndex = Rule.Bone.GetCompactPoseIndex(RequiredBones);
		if (RootCompactIndex == INDEX_NONE)
		{
			continue;
		}

		TArray<int32>& AffectedBones = CachedAffectedBoneIndices[RuleIndex];
		AffectedBones.Reset();

		if (Rule.bAffectChildren)
		{
			for (int32 BoneIdx = 0; BoneIdx < NumRequiredBones; ++BoneIdx)
			{
				const FCompactPoseBoneIndex TestIndex(BoneIdx);

				// 自身或子骨骼都纳入影响范围
				if (BoneIdx == RootCompactIndex.GetInt() || RequiredBones.BoneIsChildOf(TestIndex, RootCompactIndex))
				{
					AffectedBones.Add(BoneIdx);
				}
			}
		}
		else
		{
			AffectedBones.Add(RootCompactIndex.GetInt());
		}
	}
}

float FAnimNode_CurveDrivenBoneBlend::ReadCurveValueSafe(const FBlendedCurve& Curve, FName CurveName)
{
	if (CurveName.IsNone())
	{
		return 0.0f;
	}

	// 第一版直接按名称读取，简单直观。
	// 后续如果规则数量很多，再改成缓存 UID 的版本。
	return Curve.Get(CurveName);
}