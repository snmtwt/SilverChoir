#include "Tools/HMS_CharacterSetupTools.h"
#include "Animation/AnimBlueprint.h"
#include "Engine/SkeletalMesh.h"
#include "AnimGraphNode_Base.h"
#include "K2Node_AnimNodeReference.h"
#include "K2Node_CallFunction.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "HMS_AnimInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateNodeBase.h"
#include "AnimationTransitionGraph.h"
#include "AnimStateAliasNode.h"
#include "K2Node_VariableGet.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraph/AnimGraphNode_OrientationWarping.h"

FString UHMS_CharacterSetupTools::InspectStateMachineRules(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) { return TEXT("No blueprint"); }
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	FString Result;
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UAnimStateAliasNode* Alias = Cast<UAnimStateAliasNode>(Node))
			{
				Result += FString::Printf(TEXT("Alias %s global=%d:"), *Alias->GetStateName(), Alias->bGlobalAlias);
				for (const auto& State : Alias->GetAliasedStates())
				{
					if (State.IsValid()) { Result += TEXT(" ") + State->GetStateName(); }
				}
				Result += TEXT("\n");
			}
			const UAnimStateTransitionNode* Transition = Cast<UAnimStateTransitionNode>(Node);
			if (!Transition) { continue; }
			Result += FString::Printf(TEXT("\n%s: %s -> %s priority=%d blend=%.3f\n"),
				*Transition->GetPathName(),
				Transition->GetPreviousState() ? *Transition->GetPreviousState()->GetNodeTitle(ENodeTitleType::FullTitle).ToString() : TEXT("None"),
				Transition->GetNextState() ? *Transition->GetNextState()->GetNodeTitle(ENodeTitleType::FullTitle).ToString() : TEXT("None"),
				Transition->PriorityOrder, Transition->CrossfadeDuration);
			const UEdGraph* Rule = Transition->GetBoundGraph();
			if (!Rule) { continue; }
			for (const UEdGraphNode* RuleNode : Rule->Nodes)
			{
				Result += RuleNode->GetName() + TEXT(" ") + RuleNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString() + TEXT("\n");
				for (const UEdGraphPin* Pin : RuleNode->Pins)
				{
					if (Pin->Direction != EGPD_Input) { continue; }
					Result += FString::Printf(TEXT("  %s=%s"), *Pin->PinName.ToString(), *Pin->DefaultValue);
					for (const UEdGraphPin* Link : Pin->LinkedTo)
					{
						Result += FString::Printf(TEXT(" <- %s.%s"), *Link->GetOwningNode()->GetName(), *Link->PinName.ToString());
					}
					Result += TEXT("\n");
				}
			}
		}
	}
	return Result;
}

FString UHMS_CharacterSetupTools::RepairLocomotionTransitions(UAnimBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->ParentClass->IsChildOf(UHMS_AnimInstance::StaticClass())) { return TEXT("Not an HMS animation blueprint"); }
	Blueprint->Modify();
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	FString Result;
	for (UEdGraph* Graph : Graphs)
	{
		UAnimStateNodeBase* IdleTransition = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* State = Cast<UAnimStateNodeBase>(Node); State && State->GetStateName() == TEXT("Idle Transition"))
			{
				IdleTransition = State;
			}
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			auto* Transition = Cast<UAnimStateTransitionNode>(Node);
			if (!Transition || !Transition->GetNextState() || !Transition->GetPreviousState()) { continue; }
			if (IdleTransition && Transition->GetNextState()->GetStateName() == TEXT("Locomotion Transition"))
			{
				if (auto* Alias = Cast<UAnimStateAliasNode>(Transition->GetPreviousState()))
				{
					bool bIncludesIdleLoop = false;
					for (const auto& State : Alias->GetAliasedStates())
					{
						bIncludesIdleLoop |= State.IsValid() && State->GetStateName() == TEXT("Idle Loop");
					}
					if (bIncludesIdleLoop && !Alias->GetAliasedStates().Contains(IdleTransition))
					{
						Alias->Modify(); Alias->GetAliasedStates().Add(IdleTransition);
						Result += TEXT("Added Idle Transition to movement-entry alias: ") + Alias->GetStateName() + TEXT("\n");
					}
				}
			}
			if (Transition->GetPreviousState()->GetStateName() != TEXT("Locomotion Transition")
				|| Transition->GetNextState()->GetStateName() != TEXT("Locomotion Loop")) { continue; }
			UEdGraph* Rule = Transition->GetBoundGraph();
			bool bCirclingRule = false;
			UAnimGraphNode_TransitionResult* RuleResult = nullptr;
			for (UEdGraphNode* RuleNode : Rule->Nodes)
			{
				if (const auto* Variable = Cast<UK2Node_VariableGet>(RuleNode))
				{
					bCirclingRule |= Variable->VariableReference.GetMemberName() == TEXT("Trj_CirclingTime");
				}
				if (auto* Output = Cast<UAnimGraphNode_TransitionResult>(RuleNode)) { RuleResult = Output; }
			}
			if (!bCirclingRule || !RuleResult) { continue; }
			Rule->Modify();
			const TArray<TObjectPtr<UEdGraphNode>> OldNodes = Rule->Nodes;
			for (UEdGraphNode* Old : OldNodes)
			{
				if (Old != RuleResult) { FBlueprintEditorUtils::RemoveNode(Blueprint, Old, true); }
			}
			FGraphNodeCreator<UK2Node_CallFunction> Creator(*Rule);
			UK2Node_CallFunction* Call = Creator.CreateNode();
			Call->SetFromFunction(UHMS_AnimInstance::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UHMS_AnimInstance, ShouldExitStartPivotByCircling)));
			Creator.Finalize();
			Call->NodePosX = RuleResult->NodePosX - 300; Call->NodePosY = RuleResult->NodePosY;
			const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
			if (!Schema->TryCreateConnection(Call->GetReturnValuePin(), RuleResult->FindPinChecked(TEXT("bCanEnterTransition"))))
			{
				return TEXT("Failed to connect circling rule");
			}
			Result += TEXT("Repaired circling rule: ") + Rule->GetPathName() + TEXT("\n");
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Result.IsEmpty() ? TEXT("No legacy transition repairs needed") : Result;
}

FString UHMS_CharacterSetupTools::InspectAnimationBlueprint(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) { return TEXT("No blueprint"); }
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	FString Result;
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (const auto* Ref = Cast<UK2Node_AnimNodeReference>(Node))
			{
				Result += FString::Printf(TEXT("Reference %s = %s\n"), *Node->GetPathName(), *Ref->GetTag().ToString());
			}
			if (auto* AnimNode = Cast<UAnimGraphNode_Base>(Node); AnimNode && !AnimNode->GetTag().IsNone())
			{
				Result += FString::Printf(TEXT("Node %s Tag=%s BlendParametersBound=%d\n"), *Node->GetPathName(), *AnimNode->GetTag().ToString(), AnimNode->HasBinding(TEXT("BlendParameters")));
			}
		}
	}
	return Result;
}

UPhysicsAsset* UHMS_CharacterSetupTools::CreateHumanoidRagdollRig(USkeletalMesh* Mesh, const FString& PackagePath)
{
	if (!Mesh || !FPackageName::IsValidLongPackageName(PackagePath)
		|| FPackageName::DoesPackageExist(PackagePath)) { return nullptr; }
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	const TArray<FName> Names = { TEXT("pelvis"), TEXT("spine_02"), TEXT("spine_04"), TEXT("neck_01"), TEXT("head"),
		TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"),
		TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r") };
	const TArray<FName> Ends = { TEXT("spine_02"), TEXT("spine_04"), TEXT("neck_01"), TEXT("head"), NAME_None,
		TEXT("lowerarm_l"), TEXT("hand_l"), NAME_None, TEXT("lowerarm_r"), TEXT("hand_r"), NAME_None,
		TEXT("calf_l"), TEXT("foot_l"), TEXT("ball_l"), TEXT("calf_r"), TEXT("foot_r"), TEXT("ball_r") };
	const float Radii[] = { 11, 10, 12, 5, 9, 5, 4, 4, 5, 4, 4, 7, 5, 5, 7, 5, 5 };
	for (FName Name : Names) { if (Ref.FindBoneIndex(Name) == INDEX_NONE) { return nullptr; } }
	TArray<FTransform> Pose = Ref.GetRefBonePose();
	for (int32 I=0; I<Pose.Num(); ++I)
	{
		if (const int32 Parent = Ref.GetParentIndex(I); Parent != INDEX_NONE) { Pose[I] = Pose[I] * Pose[Parent]; }
	}
	UPackage* Package = CreatePackage(*PackagePath);
	auto* Asset = NewObject<UPhysicsAsset>(Package, *FPackageName::GetLongPackageAssetName(PackagePath), RF_Public|RF_Standalone|RF_Transactional);
	Asset->SetPreviewMesh(Mesh);
	for (int32 I=0; I<Names.Num(); ++I)
	{
		auto* Body = NewObject<USkeletalBodySetup>(Asset, NAME_None, RF_Transactional);
		Body->BoneName = Names[I];
		Body->PhysicsType = PhysType_Default;
		const FTransform& Start = Pose[Ref.FindBoneIndex(Names[I])];
		const int32 EndIndex = Ref.FindBoneIndex(Ends[I]);
		const FVector Delta = EndIndex != INDEX_NONE ? Start.InverseTransformPosition(Pose[EndIndex].GetLocation()) : FVector::ZeroVector;
		const float Length = Delta.Size();
		FKSphylElem Capsule;
		Capsule.Radius = Radii[I];
		Capsule.Length = FMath::Max(0.f, Length - 2.f * Radii[I]);
		Capsule.Center = Delta * 0.5f;
		Capsule.Rotation = Length > UE_SMALL_NUMBER ? FQuat::FindBetweenNormals(FVector::UpVector, Delta / Length).Rotator() : FRotator::ZeroRotator;
		Body->AggGeom.SphylElems.Add(Capsule);
		Body->DefaultInstance.SetCollisionProfileName(TEXT("Ragdoll"));
		Body->DefaultInstance.LinearDamping = 0.1f;
		Body->DefaultInstance.AngularDamping = 1.f;
		Asset->SkeletalBodySetups.Add(Body);
	}
	Asset->UpdateBodySetupIndexMap();
	for (int32 I=1; I<Names.Num(); ++I)
	{
		const int32 Bone = Ref.FindBoneIndex(Names[I]);
		int32 ParentBone = Ref.GetParentIndex(Bone);
		while (ParentBone != INDEX_NONE && !Names.Contains(Ref.GetBoneName(ParentBone))) { ParentBone = Ref.GetParentIndex(ParentBone); }
		if (ParentBone == INDEX_NONE) { continue; }
		const FName ParentName = Ref.GetBoneName(ParentBone);
		auto* Constraint = NewObject<UPhysicsConstraintTemplate>(Asset, NAME_None, RF_Transactional);
		auto& Instance = Constraint->DefaultInstance;
		Instance.JointName = Names[I]; Instance.ConstraintBone1 = Names[I]; Instance.ConstraintBone2 = ParentName;
		Instance.SetRefFrame(EConstraintFrame::Frame1, FTransform::Identity);
		Instance.SetRefFrame(EConstraintFrame::Frame2, Pose[Bone].GetRelativeTransform(Pose[ParentBone]));
		Instance.SetLinearXLimit(LCM_Locked, 0); Instance.SetLinearYLimit(LCM_Locked, 0); Instance.SetLinearZLimit(LCM_Locked, 0);
		const bool bHinge = Names[I].ToString().StartsWith(TEXT("calf")) || Names[I].ToString().StartsWith(TEXT("lowerarm"));
		Instance.SetAngularSwing1Limit(ACM_Limited, bHinge ? 65.f : 40.f);
		Instance.SetAngularSwing2Limit(ACM_Limited, bHinge ? 12.f : 35.f);
		Instance.SetAngularTwistLimit(ACM_Limited, bHinge ? 10.f : 25.f);
		Instance.SetDisableCollision(true);
		// Serialization writes DefaultProfile, not the currently edited ProfileInstance.
		Constraint->UpdateProfileInstance();
		Asset->ConstraintSetup.Add(Constraint);
		Asset->DisableCollision(I, Names.IndexOfByKey(ParentName));
	}
	Asset->UpdateBoundsBodiesArray();
	Asset->PostEditChange(); Asset->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);
	return Asset;
}

bool UHMS_CharacterSetupTools::ConnectLocomotionInputs(UAnimBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->ParentClass->IsChildOf(UHMS_AnimInstance::StaticClass())) { return false; }
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	UAnimGraphNode_Base* Stack = nullptr;
	UAnimGraphNode_Base* Root = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* AnimNode = Cast<UAnimGraphNode_Base>(Node))
			{
				if (AnimNode->GetTag() == TEXT("State Machine Blend Stack") || AnimNode->GetTag() == TEXT("StateMachineBlendStack")) { Stack = AnimNode; }
				if (AnimNode->GetTag() == TEXT("OffsetRoot")) { Root = AnimNode; }
			}
		}
	}
	if (!Stack || !Root) { return false; }
	UEdGraphPin* BlendPin = Stack->FindPin(TEXT("BlendParameters"));
	if (!BlendPin) { return false; }
	Blueprint->Modify(); Stack->Modify();
	Stack->RemoveBindings(TEXT("BlendParameters"));
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	Schema->BreakPinLinks(*BlendPin, true);
	UK2Node_CallFunction* Getter = nullptr;
	for (UEdGraphNode* Node : Stack->GetGraph()->Nodes)
	{
		auto* Function = Cast<UK2Node_CallFunction>(Node);
		if (Function && Function->GetTargetFunction() == UHMS_AnimInstance::StaticClass()->FindFunctionByName(TEXT("Get_BlendSpaceInputs"))) { Getter = Function; break; }
	}
	if (!Getter)
	{
		Getter = NewObject<UK2Node_CallFunction>(Stack->GetGraph());
		Stack->GetGraph()->AddNode(Getter, false, false);
		Getter->CreateNewGuid();
		Getter->SetFromFunction(UHMS_AnimInstance::StaticClass()->FindFunctionByName(TEXT("Get_BlendSpaceInputs")));
		Getter->NodePosX = Stack->NodePosX - 300; Getter->NodePosY = Stack->NodePosY + 300;
		Getter->AllocateDefaultPins();
	}
	if (!Schema->TryCreateConnection(Getter->FindPinChecked(TEXT("ReturnValue")), BlendPin)) { return false; }
	Root->Modify();
	for (int32 Index = 0; Index < Root->ShowPinForProperties.Num(); ++Index)
	{
		if (Root->ShowPinForProperties[Index].PropertyName == TEXT("bResetEveryFrame"))
		{
			Root->SetPinVisibility(true, Index);
			break;
		}
	}
	UEdGraphPin* ResetPin = Root->FindPin(TEXT("bResetEveryFrame"));
	if (!ResetPin) { return false; }
	Root->RemoveBindings(TEXT("bResetEveryFrame"));
	UK2Node_CallFunction* ResetGetter = nullptr;
	const UFunction* ResetFunction = UHMS_AnimInstance::StaticClass()->FindFunctionByName(TEXT("ShouldResetOffsetRoot"));
	for (UEdGraphNode* Node : Root->GetGraph()->Nodes)
	{
		auto* Function = Cast<UK2Node_CallFunction>(Node);
		if (Function && Function->GetTargetFunction() == ResetFunction) { ResetGetter = Function; break; }
	}
	if (!ResetGetter)
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Root->GetGraph());
		ResetGetter = Creator.CreateNode();
		ResetGetter->SetFromFunction(ResetFunction);
		Creator.Finalize();
		ResetGetter->NodePosX = Root->NodePosX - 300;
		ResetGetter->NodePosY = Root->NodePosY + 400;
	}
	Schema->BreakPinLinks(*ResetPin, true);
	if (!Schema->TryCreateConnection(ResetGetter->GetReturnValuePin(), ResetPin)) { return false; }
	// Migrated property-access bindings can leave the node using its serialized
	// Accumulate default. Explicit pins keep HMS state-dependent rotation and
	// world-space travel direction visible and verifiable in the child Blueprint.
	auto ConnectInput = [&](UAnimGraphNode_Base* Target, FName PinName, FName FunctionName) -> bool
	{
		UEdGraphPin* Input = Target->FindPin(PinName);
		const UFunction* Function = UHMS_AnimInstance::StaticClass()->FindFunctionByName(FunctionName);
		if (!Input || !Function) { return false; }
		UK2Node_CallFunction* InputGetter = nullptr;
		for (UEdGraphNode* Node : Target->GetGraph()->Nodes)
		{
			auto* Candidate = Cast<UK2Node_CallFunction>(Node);
			if (Candidate && Candidate->GetTargetFunction() == Function) { InputGetter = Candidate; break; }
		}
		if (!InputGetter)
		{
			FGraphNodeCreator<UK2Node_CallFunction> Creator(*Target->GetGraph());
			InputGetter = Creator.CreateNode();
			InputGetter->SetFromFunction(Function);
			Creator.Finalize();
			InputGetter->NodePosX = Target->NodePosX - 350;
			InputGetter->NodePosY = Target->NodePosY + 550;
		}
		Target->Modify();
		Target->RemoveBindings(PinName);
		Schema->BreakPinLinks(*Input, true);
		return Schema->TryCreateConnection(InputGetter->GetReturnValuePin(), Input);
	};
	if (!ConnectInput(Root, TEXT("RotationMode"), TEXT("Get_OffsetRootRotationMode"))) { return false; }
	for (UEdGraph* Graph : Graphs)
	{
		const auto Nodes = Graph->Nodes;
		for (UEdGraphNode* Node : Nodes)
		{
			if (auto* Orientation = Cast<UAnimGraphNode_OrientationWarping>(Node))
			{
				if (!ConnectInput(Orientation, TEXT("LocomotionDirection"), TEXT("Get_StrafeWarpDirection"))) { return false; }
			}
		}
	}
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Ref = Cast<UK2Node_AnimNodeReference>(Node))
			{
				if (Graph->GetFName() == TEXT("GetStateMachineBlendStackNodeReference")) { Ref->Modify(); Ref->SetTag(Stack->GetTag()); }
				if (Graph->GetFName() == TEXT("GetOffsetRootNodeReference")) { Ref->Modify(); Ref->SetTag(Root->GetTag()); }
			}
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

int32 UHMS_CharacterSetupTools::ConnectRagdollPoseCorrectionInputs(UAnimBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->ParentClass->IsChildOf(UHMS_AnimInstance::StaticClass())) { return 0; }
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	const UFunction* Function = UHMS_AnimInstance::StaticClass()->FindFunctionByName(TEXT("Get_RetargetCorrectionAlpha"));
	int32 Count = 0;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph->GetFName() != TEXT("AnimGraph")) { continue; }
		UK2Node_CallFunction* Getter = nullptr;
		const TArray<TObjectPtr<UEdGraphNode>> Nodes = Graph->Nodes;
		for (UEdGraphNode* Node : Nodes)
		{
			auto* Correction = Cast<UAnimGraphNode_ModifyBone>(Node);
			if (!Correction || Correction->HasBinding(TEXT("Alpha"))) { continue; }
			UEdGraphPin* Alpha = Correction->FindPin(TEXT("Alpha"));
			if (!Alpha || !Alpha->LinkedTo.IsEmpty() || !FMath::IsNearlyEqual(FCString::Atof(*Alpha->DefaultValue), 1.f)) { continue; }
			Blueprint->Modify(); Graph->Modify(); Correction->Modify();
			if (!Getter)
			{
				FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
				Getter = Creator.CreateNode(); Getter->SetFromFunction(Function); Creator.Finalize();
				Getter->NodePosX = Correction->NodePosX - 300; Getter->NodePosY = Correction->NodePosY + 500;
			}
			if (Schema->TryCreateConnection(Getter->GetReturnValuePin(), Alpha)) { ++Count; }
		}
	}
	if (Count > 0) { FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); }
	return Count;
}

FString UHMS_CharacterSetupTools::InspectRagdollRig(USkeletalMesh* Mesh)
{
	if (!Mesh || !Mesh->GetPhysicsAsset()) { return TEXT("No physics asset"); }
	const UPhysicsAsset* Asset = Mesh->GetPhysicsAsset();
	FString Result = Asset->GetPathName() + TEXT("\n");
	for (const USkeletalBodySetup* Body : Asset->SkeletalBodySetups)
	{
		if (Body) { Result += FString::Printf(TEXT("Body %s boneExists=%d shapes=%d\n"), *Body->BoneName.ToString(), Mesh->GetRefSkeleton().FindBoneIndex(Body->BoneName) != INDEX_NONE, Body->AggGeom.GetElementCount()); }
	}
	for (const UPhysicsConstraintTemplate* Constraint : Asset->ConstraintSetup)
	{
		if (Constraint) { Result += FString::Printf(TEXT("Constraint %s -> %s\n"), *Constraint->DefaultInstance.ConstraintBone1.ToString(), *Constraint->DefaultInstance.ConstraintBone2.ToString()); }
	}
	return Result;
}
