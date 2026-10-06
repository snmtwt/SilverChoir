#include "Tools/HMS_WeaponAimTools.h"
#include "Animation/HMS_WeaponAimProfile.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Skeleton.h"
#include "Animation/BlendSpace.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

FString UHMS_WeaponAimTools::InstallFreeAimGrip(UAnimBlueprint* Blueprint, FName LeftHand, FName RightHand)
{
 if (!Blueprint || !Blueprint->TargetSkeleton) { return TEXT("Invalid Blueprint"); }
 const FReferenceSkeleton& Skeleton = Blueprint->TargetSkeleton->GetReferenceSkeleton();
 const int32 Hand = Skeleton.FindBoneIndex(LeftHand), Other = Skeleton.FindBoneIndex(RightHand);
 const int32 Elbow = Hand != INDEX_NONE ? Skeleton.GetParentIndex(Hand) : INDEX_NONE;
 if (Other == INDEX_NONE || Elbow == INDEX_NONE || Skeleton.GetParentIndex(Elbow) == INDEX_NONE)
 { return TEXT("Invalid hand chain"); }
 UAnimGraphNode_RotationOffsetBlendSpace* Offset = nullptr;
 TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
 for (UEdGraph* G : Graphs) for (UEdGraphNode* N : G->Nodes)
 {
  if (N->NodeComment == TEXT("HMS Free Aim: grip stabilization")) { return TEXT("Grip already installed"); }
  if (N->NodeComment == TEXT("HMS Free Aim: mesh-space offset")) { Offset = Cast<UAnimGraphNode_RotationOffsetBlendSpace>(N); }
 }
 UEdGraphPin* Output = Offset ? Offset->FindPin(TEXT("Pose"), EGPD_Output) : nullptr;
 if (!Output || Output->LinkedTo.Num() != 1) { return TEXT("Expected a single free AimOffset consumer"); }
 UEdGraphPin* Destination = Output->LinkedTo[0]; UEdGraph* Graph = Offset->GetGraph();
 const FScopedTransaction Transaction(NSLOCTEXT("HMS", "AimGrip", "Stabilize reference grip during AimOffset blending"));
 Blueprint->Modify(); Graph->Modify();
 const auto* Schema = GetDefault<UEdGraphSchema_K2>(); TArray<UEdGraphNode*> Created; bool Valid = true;
 auto Make = [&]<typename T>(int32 X) -> T*
 {
  FGraphNodeCreator<T> Creator(*Graph); auto* N = Creator.CreateNode(); Creator.Finalize(); Created.Add(N);
  N->NodePosX = Offset->NodePosX + X; N->NodePosY = Offset->NodePosY - 400; return N;
 };
 auto* ToComponent = Make.operator()<UAnimGraphNode_LocalToComponentSpace>(0);
 auto* IK = Make.operator()<UAnimGraphNode_TwoBoneIK>(250);
 auto* ToLocal = Make.operator()<UAnimGraphNode_ComponentToLocalSpace>(650);
 IK->NodeComment = TEXT("HMS Free Aim: grip stabilization"); IK->bCommentBubbleVisible = true;
 IK->Node.IKBone.BoneName = LeftHand;
 IK->Node.EffectorLocationSpace = BCS_BoneSpace; IK->Node.EffectorTarget = FBoneSocketTarget(RightHand);
 IK->Node.JointTargetLocationSpace = BCS_BoneSpace; IK->Node.JointTarget = FBoneSocketTarget(Skeleton.GetBoneName(Elbow));
 IK->Node.JointTargetLocation = FVector::ZeroVector;
 IK->Node.bAllowStretching = false; IK->Node.bMaintainEffectorRelRot = false; IK->Node.bTakeRotationFromEffectorSpace = false;
 IK->ReconstructNode();
 FGraphNodeCreator<UK2Node_CallFunction> FC(*Graph); auto* Function = FC.CreateNode();
 Function->SetFromFunction(UHMS_WeaponAimLibrary::StaticClass()->FindFunctionByName(TEXT("ReadAimGrip"))); FC.Finalize(); Created.Add(Function);
 Function->NodePosX = IK->NodePosX - 300; Function->NodePosY = IK->NodePosY + 300;
 FGraphNodeCreator<UK2Node_VariableGet> VC(*Graph); auto* Getter = VC.CreateNode();
 Getter->VariableReference.SetSelfMember(TEXT("FreeAimProfile")); VC.Finalize(); Created.Add(Getter);
 Getter->NodePosX = Function->NodePosX - 280; Getter->NodePosY = Function->NodePosY;
 auto Wire = [&](UEdGraphPin* A, UEdGraphPin* B) { Valid &= A && B && Schema->TryCreateConnection(A, B); };
 Wire(Getter->FindPin(TEXT("FreeAimProfile")), Function->FindPin(TEXT("Profile")));
 Wire(Function->FindPin(TEXT("Alpha")), IK->FindPin(TEXT("Alpha")));
 Wire(Function->FindPin(TEXT("Target")), IK->FindPin(TEXT("EffectorLocation")));
 Wire(Output, ToComponent->FindPin(TEXT("LocalPose")));
 Wire(ToComponent->FindPin(TEXT("ComponentPose")), IK->FindPin(TEXT("ComponentPose")));
 Wire(IK->FindPin(TEXT("Pose")), ToLocal->FindPin(TEXT("ComponentPose")));
 if (!Valid)
 {
  for (UEdGraphNode* N : Created) { FBlueprintEditorUtils::RemoveNode(Blueprint, N, true); }
  return TEXT("Grip connection failed; original route preserved");
 }
 Schema->BreakPinLinks(*Destination, true);
 if (!Schema->TryCreateConnection(ToLocal->FindPin(TEXT("Pose")), Destination))
 {
  for (UEdGraphNode* N : Created) { FBlueprintEditorUtils::RemoveNode(Blueprint, N, true); }
  Schema->TryCreateConnection(Output, Destination); return TEXT("Grip destination failed; original route restored");
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); return TEXT("Installed reference grip stabilization");
}

FString UHMS_WeaponAimTools::InstallAimProfiles(UAnimBlueprint* Blueprint, UHMS_WeaponAimProfile* FreeAim,
 UHMS_WeaponAimProfile* CoverAim, FName UpperBodyBone)
{
 if (!Blueprint || !Blueprint->GeneratedClass || !FreeAim || !CoverAim
  || FreeAim->Context != EHMS_AimContext::FreeAim || CoverAim->Context != EHMS_AimContext::CoverPeek
  || !FreeAim->IsUsable(Blueprint->TargetSkeleton) || !CoverAim->IsUsable(Blueprint->TargetSkeleton)
  || !Blueprint->TargetSkeleton || Blueprint->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(UpperBodyBone) == INDEX_NONE)
 { return TEXT("Invalid profiles, skeleton or upper-body bone"); }
 const FName Variables[] = {TEXT("EquippedWeaponType"), TEXT("RotationMode"), TEXT("AO"), TEXT("Speed2D"),
  TEXT("StateMachineState"), TEXT("CoverPeekState"), TEXT("CoverPeekAimReady"), TEXT("CoverPeekAimOffset")};
 for (FName Name : Variables)
 { if (!FindFProperty<FProperty>(Blueprint->GeneratedClass, Name)) { return TEXT("Missing Blueprint input: ") + Name.ToString(); } }
 for (FName Name : {FName(TEXT("FreeAimProfile")), FName(TEXT("CoverAimProfile"))})
 {
  if (FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, Name))
  {
   const auto* Object = CastField<FObjectPropertyBase>(Property);
   if (!Object || Object->PropertyClass != UHMS_WeaponAimProfile::StaticClass()) { return TEXT("Incompatible profile variable: ") + Name.ToString(); }
  }
 }
 TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
 UAnimGraphNode_SaveCachedPose* Cache = nullptr;
 UAnimGraphNode_SaveCachedPose* Destination = nullptr;
 bool Installed = false;
 for (UEdGraph* G : Graphs) for (UEdGraphNode* N : G->Nodes)
 {
  Installed |= N->NodeComment == TEXT("HMS Aim Profiles v1: Cover gate");
  if (auto* C = Cast<UAnimGraphNode_SaveCachedPose>(N))
  {
   if (C->CacheName == TEXT("HMS_PreWeapon")) { Cache = C; }
   if (C->CacheName == TEXT("PreRagdoll")) { Destination = C; }
  }
 }
 UEdGraphPin* Input = Destination ? Destination->FindPin(TEXT("Pose"), EGPD_Input) : nullptr;
 if (!Cache || !Input || Input->LinkedTo.Num() != 1 || Cache->GetGraph() != Destination->GetGraph())
 { return TEXT("Expected HMS_PreWeapon and connected PreRagdoll caches"); }
 if (!Installed && Input->LinkedTo[0]->GetOwningNode()->NodeComment != TEXT("HMS Cover Peek Aim Offset"))
 { return TEXT("Unknown pose route; automatic upgrade refused to preserve custom nodes"); }
 const FScopedTransaction Transaction(NSLOCTEXT("HMS", "AimProfiles", "Configure independent free and cover aim profiles"));
 Blueprint->Modify();
 auto ProfileVariable = [&](FName Name, UHMS_WeaponAimProfile* Profile)
 {
  FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Object; Type.PinSubCategoryObject = UHMS_WeaponAimProfile::StaticClass();
  const FString Value = Profile->GetPathName();
  if (!FindFProperty<FProperty>(Blueprint->GeneratedClass, Name)) { FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, Value); }
  for (FBPVariableDescription& V : Blueprint->NewVariables) if (V.VarName == Name) { V.DefaultValue = Value; V.PropertyFlags &= ~CPF_DisableEditOnInstance; }
  FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, Name, nullptr, NSLOCTEXT("HMS", "AimCategory", "HMS | Aim Profiles"));
  if (auto* Property = FindFProperty<FObjectPropertyBase>(Blueprint->GeneratedClass, Name))
  { Blueprint->GeneratedClass->GetDefaultObject()->Modify(); Property->SetObjectPropertyValue_InContainer(Blueprint->GeneratedClass->GetDefaultObject(), Profile); }
 };
 ProfileVariable(TEXT("FreeAimProfile"), FreeAim); ProfileVariable(TEXT("CoverAimProfile"), CoverAim);
 if (Installed) { FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); return TEXT("Updated existing aim profile defaults"); }

 UEdGraph* Graph = Destination->GetGraph(); Graph->Modify();
 const auto* Schema = GetDefault<UEdGraphSchema_K2>();
 TArray<UEdGraphNode*> Created; bool Valid = true;
 auto Make = [&]<typename T>(const FString& Comment, int32 X, int32 Y) -> T*
 {
  FGraphNodeCreator<T> Creator(*Graph); T* N = Creator.CreateNode(); Creator.Finalize();
  N->NodeComment = Comment; N->bCommentBubbleVisible = true;
  N->NodePosX = Destination->NodePosX - 1800 + X; N->NodePosY = Destination->NodePosY + 1800 + Y;
  Created.Add(N); return N;
 };
 auto Wire = [&](UEdGraphPin* A, UEdGraphPin* B) { Valid &= A && B && Schema->TryCreateConnection(A, B); };
 auto ExposeAsset = [&](UAnimGraphNode_Base* Node)
 {
  for (int32 I = 0; I < Node->ShowPinForProperties.Num(); ++I)
   if (Node->ShowPinForProperties[I].PropertyName == TEXT("BlendSpace")) { Node->SetPinVisibility(true, I); break; }
  Valid &= Node->FindPin(TEXT("BlendSpace")) != nullptr;
 };
 auto BasePose = [&](int32 X, int32 Y)
 {
  auto* N = Make.operator()<UAnimGraphNode_UseCachedPose>(TEXT("HMS: full-body Chooser / locomotion"), X, Y);
  N->SaveCachedPoseNode = Cache; return N;
 };
 auto Function = [&](FName Name, int32 X, int32 Y)
 {
  FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph); auto* N = Creator.CreateNode();
  N->SetFromFunction(UHMS_WeaponAimLibrary::StaticClass()->FindFunctionByName(Name)); Creator.Finalize(); Created.Add(N);
  N->NodePosX = Destination->NodePosX - 2200 + X; N->NodePosY = Destination->NodePosY + 2650 + Y; return N;
 };
 auto Bind = [&](UK2Node_CallFunction* FunctionNode, FName Variable, FName Pin, int32 Row)
 {
  FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph); auto* N = Creator.CreateNode();
  N->VariableReference.SetSelfMember(Variable); Creator.Finalize(); Created.Add(N);
  N->NodePosX = FunctionNode->NodePosX - 320; N->NodePosY = FunctionNode->NodePosY + Row * 90;
  Wire(N->FindPin(Variable), FunctionNode->FindPin(Pin));
 };
 auto* Free = Function(TEXT("CalculateFreeAimInputs"), 0, 0);
 auto* Cover = Function(TEXT("CalculateCoverAimInputs"), 1500, 0);
 const TPair<FName,FName> FreeBindings[] = {{TEXT("FreeAimProfile"),TEXT("Profile")}, {TEXT("EquippedWeaponType"),TEXT("EquippedWeaponType")},
  {TEXT("RotationMode"),TEXT("RotationMode")}, {TEXT("AO"),TEXT("AimAngles")}, {TEXT("Speed2D"),TEXT("GroundSpeed")}, {TEXT("StateMachineState"),TEXT("StateTags")}};
 const TPair<FName,FName> CoverBindings[] = {{TEXT("CoverAimProfile"),TEXT("Profile")}, {TEXT("EquippedWeaponType"),TEXT("EquippedWeaponType")},
  {TEXT("CoverPeekState"),TEXT("PeekState")}, {TEXT("CoverPeekAimReady"),TEXT("AimReady")}, {TEXT("CoverPeekAimOffset"),TEXT("AimAngles")}, {TEXT("StateMachineState"),TEXT("StateTags")}};
 int32 Row = 0; for (auto B : FreeBindings) { Bind(Free, B.Key, B.Value, Row++); }
 Row = 0; for (auto B : CoverBindings) { Bind(Cover, B.Key, B.Value, Row++); }
 auto* Hold = Make.operator()<UAnimGraphNode_BlendSpacePlayer>(TEXT("HMS Free Aim: reference grip, subtle idle / movement"), 0, 200);
 Hold->Node.SetBlendSpace(FreeAim->HoldingPose); Hold->Node.SetLoop(true); Hold->ReconstructNode(); ExposeAsset(Hold);
 auto Offset = [&](UHMS_WeaponAimProfile* Profile, UK2Node_CallFunction* FunctionNode, FString Comment, int32 X, int32 Y)
 {
  auto* N = Make.operator()<UAnimGraphNode_RotationOffsetBlendSpace>(Comment, X, Y);
  N->Node.SetBlendSpace(Profile->AimOffset); N->Node.bApplyAdditiveInRootSpace = true; N->ReconstructNode(); ExposeAsset(N);
  Wire(FunctionNode->FindPin(TEXT("OffsetAsset")), N->FindPin(TEXT("BlendSpace")));
  Wire(FunctionNode->FindPin(TEXT("Yaw")), N->FindPin(TEXT("X"))); Wire(FunctionNode->FindPin(TEXT("Pitch")), N->FindPin(TEXT("Y")));
  return N;
 };
 auto* FreeOffset = Offset(FreeAim, Free, TEXT("HMS Free Aim: mesh-space offset"), 320, 200);
 auto* Layer = Make.operator()<UAnimGraphNode_LayeredBoneBlend>(TEXT("HMS Aim: upper body only; preserve legs, curves and root motion"), 650, 150);
 Layer->Node.LayerSetup.SetNum(1); Layer->Node.LayerSetup[0].BranchFilters.Reset();
 FBranchFilter Filter; Filter.BoneName = UpperBodyBone; Filter.BlendDepth = 1; Layer->Node.LayerSetup[0].BranchFilters.Add(Filter);
 Layer->Node.bMeshSpaceRotationBlend = true; Layer->Node.bRootSpaceRotationBlend = true;
 Layer->Node.CurveBlendOption = ECurveBlendOption::UseBasePose; Layer->Node.bBlendRootMotionBasedOnRootBone = true;
 auto Gate = [&](UK2Node_CallFunction* FunctionNode, FString Comment, int32 X, int32 Y)
 {
  auto* N = Make.operator()<UAnimGraphNode_BlendListByBool>(Comment, X, Y);
  Wire(FunctionNode->FindPin(TEXT("Enabled")), N->FindPin(TEXT("bActiveValue")));
  Wire(FunctionNode->FindPin(TEXT("BlendIn")), N->FindPin(TEXT("BlendTime_0")));
  Wire(FunctionNode->FindPin(TEXT("BlendOut")), N->FindPin(TEXT("BlendTime_1"))); return N;
 };
 auto* FreeGate = Gate(Free, TEXT("HMS Aim Profiles v1: Free gate"), 980, 150);
 auto* CoverOffset = Offset(CoverAim, Cover, TEXT("HMS Cover Aim: separate peek reference"), 1300, 150);
 auto* CoverGate = Gate(Cover, TEXT("HMS Aim Profiles v1: Cover gate"), 1630, 150);
 Wire(Free->FindPin(TEXT("HoldingPose")), Hold->FindPin(TEXT("BlendSpace")));
 Wire(Free->FindPin(TEXT("Speed")), Hold->FindPin(TEXT("X")));
 Wire(Hold->FindPin(TEXT("Pose")), FreeOffset->FindPin(TEXT("BasePose")));
 Wire(FreeOffset->FindPin(TEXT("Pose")), Layer->FindPin(TEXT("BlendPoses_0")));
 Wire(BasePose(300, -150)->FindPin(TEXT("Pose")), Layer->FindPin(TEXT("BasePose")));
 Wire(Layer->FindPin(TEXT("Pose")), FreeGate->FindPin(TEXT("BlendPose_0")));
 Wire(BasePose(650, 450)->FindPin(TEXT("Pose")), FreeGate->FindPin(TEXT("BlendPose_1")));
 Wire(FreeGate->FindPin(TEXT("Pose")), CoverOffset->FindPin(TEXT("BasePose")));
 Wire(FreeGate->FindPin(TEXT("Pose")), CoverGate->FindPin(TEXT("BlendPose_1")));
 Wire(CoverOffset->FindPin(TEXT("Pose")), CoverGate->FindPin(TEXT("BlendPose_0")));
 if (!Valid)
 {
  for (UEdGraphNode* N : Created) { FBlueprintEditorUtils::RemoveNode(Blueprint, N, true); }
  FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
  return TEXT("Aim connection failed; original pose route preserved");
 }
 // Only retire the old upstream layer if its outputs are not shared by other graph consumers.
 TSet<UEdGraphNode*> Old;
 TFunction<void(UEdGraphNode*)> Gather = [&](UEdGraphNode* N)
 {
  if (!N || N == Cache || Old.Contains(N)) { return; }
  Old.Add(N);
  for (UEdGraphPin* P : N->Pins) if (P->Direction == EGPD_Input)
   for (UEdGraphPin* L : P->LinkedTo) { Gather(L->GetOwningNode()); }
 };
 Gather(Input->LinkedTo[0]->GetOwningNode());
 bool Changed = true;
 while (Changed)
 {
  Changed = false;
  for (UEdGraphNode* N : Old.Array())
   for (UEdGraphPin* P : N->Pins) if (P->Direction == EGPD_Output)
    for (UEdGraphPin* L : P->LinkedTo) if (L != Input && !Old.Contains(L->GetOwningNode()))
    { Changed |= Old.Remove(N) > 0; }
 }
 Destination->Modify(); Schema->BreakPinLinks(*Input, true); Wire(CoverGate->FindPin(TEXT("Pose")), Input);
 for (UEdGraphNode* N : Old) { FBlueprintEditorUtils::RemoveNode(Blueprint, N, true); }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
 return TEXT("Installed independent FreeAimProfile and CoverAimProfile; retired obsolete weapon layer nodes");
}
