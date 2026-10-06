#include "Tools/HMS_CoverAnimationTools.h"
#include "Animation/HMS_CoverAnimationProfile.h"
#include "SmartObject/HMS_SmartObjectInteractionProfile.h"
#include "Animation/AnimSequence.h"
#include "Chooser.h"
#include "GameplayTagColumn.h"
#include "EnumColumn.h"
#include "NameColumn.h"
#include "OutputStructColumn.h"
#include "ObjectChooser_Asset.h"
#include "Engine/UserDefinedEnum.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "ScopedTransaction.h"
#include "HMS_MovementStruct.h"
#include "SmartObjectDefinition.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_Slot.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"

bool UHMS_CoverAnimationTools::RouteCoverAimAfterMontage(UAnimBlueprint* Blueprint, FName SlotName)
{
 if (!Blueprint) { return false; }
 UEdGraphNode* Free = nullptr; UEdGraphNode* Cover = nullptr; UEdGraphNode* Offset = nullptr;
 UAnimGraphNode_SaveCachedPose* Pre = nullptr; UAnimGraphNode_SaveCachedPose* Existing = nullptr;
 UAnimGraphNode_Slot* Slot = nullptr;
 TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
 for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
 {
  if (Node->NodeComment == TEXT("HMS Aim Profiles v1: Free gate")) { Free = Node; }
  if (Node->NodeComment == TEXT("HMS Aim Profiles v1: Cover gate")) { Cover = Node; }
  if (Node->NodeComment == TEXT("HMS Cover Aim: separate peek reference")) { Offset = Node; }
  if (auto* Cache = Cast<UAnimGraphNode_SaveCachedPose>(Node))
  {
   if (Cache->CacheName == TEXT("PreRagdoll")) { Pre = Cache; }
   if (Cache->CacheName == TEXT("HMS_PostCoverMontage")) { Existing = Cache; }
  }
  if (auto* Candidate = Cast<UAnimGraphNode_Slot>(Node); Candidate && Candidate->Node.SlotName == SlotName) { Slot = Candidate; }
 }
 if (Existing) { return true; }
 if (!Free || !Cover || !Offset || !Pre || !Slot || Free->GetGraph() != Slot->GetGraph()
  || Cover->GetGraph() != Slot->GetGraph() || Pre->GetGraph() != Slot->GetGraph()) { return false; }
 auto Input = [](UEdGraphNode* N, FName Name) { return N->FindPin(Name, EGPD_Input); };
 auto Output = [](UEdGraphNode* N) { return N->FindPin(TEXT("Pose"), EGPD_Output); };
 auto* PreIn = Input(Pre, TEXT("Pose")); auto* OffsetIn = Input(Offset, TEXT("BasePose"));
 auto* FalseIn = Input(Cover, TEXT("BlendPose_1")); auto* SlotOut = Output(Slot); auto* CoverOut = Output(Cover);
 if (!PreIn || !OffsetIn || !FalseIn || !SlotOut || !CoverOut || !Output(Free)
  || PreIn->LinkedTo.Num() != 1 || PreIn->LinkedTo[0] != CoverOut || CoverOut->LinkedTo.Num() != 1
  || SlotOut->LinkedTo.Num() != 1) { return false; }
 UEdGraphPin* Destination = SlotOut->LinkedTo[0]; UEdGraph* Graph = Slot->GetGraph();
 const FScopedTransaction Transaction(NSLOCTEXT("HMS", "CoverAimAfterMontage", "Apply cover aim after montage"));
 Blueprint->Modify(); Graph->Modify();
 for (UEdGraphNode* Node : {Free, Cover, Offset, static_cast<UEdGraphNode*>(Pre), static_cast<UEdGraphNode*>(Slot), Destination->GetOwningNode()}) { Node->Modify(); }
 FGraphNodeCreator<UAnimGraphNode_SaveCachedPose> SaveCreator(*Graph); auto* Cache = SaveCreator.CreateNode();
 Cache->CacheName = TEXT("HMS_PostCoverMontage"); SaveCreator.Finalize(); Cache->NodePosX = Slot->NodePosX + 300; Cache->NodePosY = Slot->NodePosY;
 auto Read = [&](int32 Y)
 {
  FGraphNodeCreator<UAnimGraphNode_UseCachedPose> Creator(*Graph); auto* Node = Creator.CreateNode();
  Node->SaveCachedPoseNode = Cache; Creator.Finalize(); Node->NodePosX = Offset->NodePosX - 350; Node->NodePosY = Y; return Node;
 };
 auto* ReadOffset = Read(Offset->NodePosY); auto* ReadBase = Read(Cover->NodePosY + 200);
 const auto* Schema = GetDefault<UEdGraphSchema_K2>();
 Schema->BreakPinLinks(*PreIn, true); Schema->BreakPinLinks(*OffsetIn, true); Schema->BreakPinLinks(*FalseIn, true); Schema->BreakPinLinks(*Destination, true);
 const bool Success = Schema->TryCreateConnection(Output(Free), PreIn)
  && Schema->TryCreateConnection(SlotOut, Input(Cache, TEXT("Pose")))
  && Schema->TryCreateConnection(Output(ReadOffset), OffsetIn)
  && Schema->TryCreateConnection(Output(ReadBase), FalseIn)
  && Schema->TryCreateConnection(CoverOut, Destination);
 if (!Success)
 {
  // Restore the known route if a schema connection unexpectedly fails.
  Schema->BreakPinLinks(*PreIn, true); Schema->BreakPinLinks(*OffsetIn, true); Schema->BreakPinLinks(*FalseIn, true); Schema->BreakPinLinks(*Destination, true);
  FBlueprintEditorUtils::RemoveNode(Blueprint, ReadOffset, true); FBlueprintEditorUtils::RemoveNode(Blueprint, ReadBase, true); FBlueprintEditorUtils::RemoveNode(Blueprint, Cache, true);
  Schema->TryCreateConnection(CoverOut, PreIn); Schema->TryCreateConnection(Output(Free), OffsetIn); Schema->TryCreateConnection(Output(Free), FalseIn); Schema->TryCreateConnection(SlotOut, Destination);
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); return Success;
}

bool UHMS_CoverAnimationTools::ConfigurePeekAimReference(UHMS_CoverAnimationProfile* Profile,
 const TArray<float>& Times, const TArray<FVector2D>& Angles)
{
 if (!Profile || Times.Num() < 2 || Times.Num() != Angles.Num()
  || !FMath::IsNearlyZero(Times[0]) || !FMath::IsNearlyEqual(Times.Last(), 1.f)) { return false; }
 for (int32 I = 0; I < Times.Num(); ++I)
 {
  if (!FMath::IsFinite(Times[I]) || Times[I] < 0.f || Times[I] > 1.f || Angles[I].ContainsNaN()
   || (I > 0 && Times[I] <= Times[I - 1])) { return false; }
 }
 const FScopedTransaction Transaction(NSLOCTEXT("HMS", "CoverPeekAimReference", "Configure cover peek aiming reference"));
 Profile->Modify(); Profile->PeekAimReferenceCurve.ExternalCurve = nullptr;
 for (int32 Axis = 0; Axis < 3; ++Axis) { Profile->PeekAimReferenceCurve.GetRichCurve(Axis)->Reset(); }
 float Yaw = Angles[0].X;
 for (int32 I = 0; I < Times.Num(); ++I)
 {
  if (I > 0) { Yaw += FMath::FindDeltaAngleDegrees(Yaw, static_cast<float>(Angles[I].X)); }
  for (int32 Axis = 0; Axis < 2; ++Axis)
  {
   auto* Curve = Profile->PeekAimReferenceCurve.GetRichCurve(Axis);
   const FKeyHandle Key = Curve->AddKey(Times[I], Axis == 0 ? Yaw : static_cast<float>(Angles[I].Y));
   Curve->SetKeyInterpMode(Key, RCIM_Linear);
  }
 }
 Profile->PostEditChange(); Profile->MarkPackageDirty(); return true;
}

bool UHMS_CoverAnimationTools::ConfigureSlotRequiredTag(USmartObjectDefinition* Definition,FName SlotName,FGameplayTag RequiredTag)
{
 if (!Definition || SlotName.IsNone()) { return false; }
 for (auto& Slot : Definition->GetMutableSlots())
 {
  if (Slot.Name!=SlotName) { continue; }
  const FScopedTransaction Transaction(NSLOCTEXT("HMS","CoverSlotRequirement","Configure cover slot equipment requirement"));
  Definition->Modify();Slot.UserTagFilter=RequiredTag.IsValid() ? FGameplayTagQuery::MakeQuery_MatchTag(RequiredTag) : FGameplayTagQuery();
  Definition->PostEditChange();Definition->MarkPackageDirty();return true;
 }
 return false;
}

bool UHMS_CoverAnimationTools::ConfigureEntrySideChooser(UChooserTable* Table, FGameplayTag Stance,
 const TArray<FGameplayTag>& SlotTags, const TArray<UPoseSearchDatabase*>& Databases)
{
 if (!Table || Table->GetRootChooser()!=Table || Table->ContextData.IsEmpty() || !Stance.IsValid()
  || SlotTags.IsEmpty() || SlotTags.Num()!=Databases.Num()) { return false; }
 TSet<FGameplayTag> Unique;
 for (int32 I=0;I<SlotTags.Num();++I)
 { if (!SlotTags[I].IsValid() || Unique.Contains(SlotTags[I]) || !Databases[I]) { return false; } Unique.Add(SlotTags[I]); }
 if (!Table->ContextData[0].GetPtr<FContextObjectTypeClass>()) { return false; }
 const FScopedTransaction Transaction(NSLOCTEXT("HMS","EntrySides","Configure entry slot-side Chooser"));
 Table->Modify();Table->ContextData.SetNum(1);
 auto Context=FInstancedStruct::Make<FContextObjectTypeStruct>();
 Context.GetMutable<FContextObjectTypeStruct>().Struct=FHMS_SmartObjectEntryContext::StaticStruct();
 Context.GetMutable<FContextObjectTypeStruct>().Direction=EContextObjectDirection::Read;
 Table->ContextData.Add(Context);
 auto Column=[](FName Name,int32 Index)
 {
  auto Value=FInstancedStruct::Make<FGameplayTagColumn>();auto& C=Value.GetMutable<FGameplayTagColumn>();
  C.InputValue=FInstancedStruct::Make<FGameplayTagContextProperty>();
  auto& Binding=C.InputValue.GetMutable<FGameplayTagContextProperty>().Binding;
  Binding.ContextIndex=Index;Binding.PropertyBindingChain={Name};C.bMatchExact=true;return Value;
 };
 auto StanceColumn=Column(TEXT("Stance"),0), SideColumn=Column(TEXT("SlotTags"),1);
 Table->ResultsStructs.Reset();Table->FallbackResult.Reset();
 for (int32 I=0;I<SlotTags.Num();++I)
 {
  StanceColumn.GetMutable<FGameplayTagColumn>().RowValues.Add(FGameplayTagContainer(Stance));
  SideColumn.GetMutable<FGameplayTagColumn>().RowValues.Add(FGameplayTagContainer(SlotTags[I]));
  auto Result=FInstancedStruct::Make<FAssetChooser>();Result.GetMutable<FAssetChooser>().Asset=Databases[I];Table->ResultsStructs.Add(Result);
 }
 Table->ColumnsStructs={StanceColumn,SideColumn};Table->DisabledRows.Reset();Table->CookedResults.Reset();
 Table->PostEditChange();Table->Compile(true);Table->MarkPackageDirty();return true;
}

bool UHMS_CoverAnimationTools::ConfigureCoverIdleChooser(UChooserTable* Table,UUserDefinedEnum* CoverEnum,UUserDefinedEnum* PeekEnum,
 const TArray<uint8>& CoverStates,const TArray<UHMS_CoverAnimationProfile*>& Profiles)
{
 if (!Table || Table->GetRootChooser()!=Table || Table->ContextData.Num()<2 || !CoverEnum || !PeekEnum
  || Profiles.IsEmpty() || Profiles.Num()!=CoverStates.Num() || PeekEnum->NumEnums()<3) { return false; }
 for (int32 I=0;I<Profiles.Num();++I)
 {
  const auto* P=Profiles[I];
  if (!P || !P->ConcealedIdle || !P->PeekIdle || !P->RequiredWeaponType.IsValid()
   || P->ConcealedIdle->GetSkeleton()!=P->PeekIdle->GetSkeleton()
   || CoverStates[I]==0 || CoverEnum->GetIndexByValue(CoverStates[I])==INDEX_NONE) { return false; }
 }
 auto EnumColumn=[](FName Name,UUserDefinedEnum* Enum)
 {
  auto Value=FInstancedStruct::Make<FEnumColumn>();auto& C=Value.GetMutable<FEnumColumn>();
  C.InputValue=FInstancedStruct::Make<FEnumContextProperty>();auto& B=C.InputValue.GetMutable<FEnumContextProperty>().Binding;
  B.ContextIndex=0;B.PropertyBindingChain={Name};B.Enum=Enum;return Value;
 };
 auto Side=EnumColumn(TEXT("CoverState"),CoverEnum),Peek=EnumColumn(TEXT("CoverPeekState"),PeekEnum);
 auto Weapon=FInstancedStruct::Make<FChooserNameColumn>();auto& WC=Weapon.GetMutable<FChooserNameColumn>();
 WC.InputValue=FInstancedStruct::Make<FNameContextProperty>();auto& WB=WC.InputValue.GetMutable<FNameContextProperty>().Binding;
 WB.ContextIndex=0;WB.PropertyBindingChain={TEXT("EquippedWeaponType"),TEXT("TagName")};
 auto Output=FInstancedStruct::Make<FOutputStructColumn>();auto& OC=Output.GetMutable<FOutputStructColumn>();
 OC.InputValue=FInstancedStruct::Make<FStructContextProperty>();auto& OB=OC.InputValue.GetMutable<FStructContextProperty>().Binding;
 OB.ContextIndex=1;OB.IsBoundToRoot=true;OB.StructType=FHMS_ChooserOutputs::StaticStruct();
 OC.DefaultRowValue=FInstancedStruct::Make<FHMS_ChooserOutputs>();OC.DefaultRowValue.GetMutable<FHMS_ChooserOutputs>().BlendTime=.35f;
 OC.DefaultRowValue.GetMutable<FHMS_ChooserOutputs>().Tags={FName(TEXT("Interaction"))};OC.FallbackValue=OC.DefaultRowValue;
 const FScopedTransaction Transaction(NSLOCTEXT("HMS","CoverSides","Configure cover pose Chooser"));
 Table->Modify();Table->ResultsStructs.Reset();
 for (int32 I=0;I<Profiles.Num();++I)
 {
  for (uint8 Active : {uint8(1),uint8(0)})
  {
   FChooserEnumRowData S;S.Value=CoverStates[I];S.ValueName=CoverEnum->GetNameByValue(S.Value);Side.GetMutable<FEnumColumn>().RowValues.Add(S);
   FChooserEnumRowData P;P.Value=Active;P.ValueName=PeekEnum->GetNameByValue(P.Value);Peek.GetMutable<FEnumColumn>().RowValues.Add(P);
   FChooserNameRowData W;W.Value=Profiles[I]->RequiredWeaponType.GetTagName();WC.RowValues.Add(W);OC.RowValues.Add(OC.DefaultRowValue);
   auto Result=FInstancedStruct::Make<FAssetChooser>();Result.GetMutable<FAssetChooser>().Asset=Active ? Profiles[I]->PeekIdle : Profiles[I]->ConcealedIdle;
   Table->ResultsStructs.Add(Result);
  }
 }
 Table->ColumnsStructs={Side,Peek,Weapon,Output};Table->DisabledRows.Reset();Table->CookedResults.Reset();
 Table->PostEditChange();Table->Compile(true);Table->MarkPackageDirty();return true;
}
