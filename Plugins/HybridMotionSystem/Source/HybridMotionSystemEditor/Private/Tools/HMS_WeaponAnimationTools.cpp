#include "Tools/HMS_WeaponAnimationTools.h"
#include "Animation/HMS_WeaponAnimationLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Chooser.h"
#include "NameColumn.h"
#include "EnumColumn.h"
#include "OutputStructColumn.h"
#include "ObjectChooser_Asset.h"
#include "Engine/UserDefinedEnum.h"
#include "HMS_MovementStruct.h"
#include "PoseSearch/PoseSearchDatabase.h"

int32 UHMS_WeaponAnimationTools::RemapWeaponChooser(UChooserTable* Chooser,
	const TArray<UAnimationAsset*>& Sources, const TArray<UAnimationAsset*>& Replacements)
{
	if (!Chooser || Chooser->GetRootChooser() != Chooser || Sources.IsEmpty()
		|| Sources.Num() != Replacements.Num()) { return -1; }
	TMap<UObject*, UObject*> Map;
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		if (!Sources[Index] || !Replacements[Index] || Sources[Index] == Replacements[Index]
			|| Sources[Index]->GetClass() != Replacements[Index]->GetClass()
			|| Sources[Index]->GetSkeleton() != Replacements[Index]->GetSkeleton()
			|| Map.Contains(Sources[Index])) { return -1; }
		Map.Add(Sources[Index], Replacements[Index]);
	}
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "RemapWeaponChooser", "Remap weapon animation family"));
	TArray<UObject*> Owned; GetObjectsWithOuter(Chooser, Owned, EGetObjectsFlags::IncludeNestedObjects);
	Owned.Add(Chooser);
	for (UObject* Object : Owned) { Object->Modify(); }
	FArchiveReplaceObjectRef<UObject> Replace(Chooser, Map,
		EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
	// Serializing the root walks its owned search data and instanced column rows,
	// but never edits external animation assets or external interaction Choosers.
	Chooser->PostEditChange(); Chooser->Compile(true);
	for (UObject* Object : Owned)
	{
		if (auto* Database = Cast<UPoseSearchDatabase>(Object)) { Database->PostEditChange(); }
	}
	Chooser->MarkPackageDirty();
	return static_cast<int32>(Replace.GetCount());
}

bool UHMS_WeaponAnimationTools::ConfigureWeaponChooserRouter(UChooserTable* Root,
	UChooserTable* Unarmed, UChooserTable* Weapon, FGameplayTag WeaponTag, FName WeaponProperty)
{
	if (!Root || !Unarmed || !Weapon || Root == Unarmed || Root == Weapon || Unarmed == Weapon
		|| !WeaponTag.IsValid() || WeaponProperty.IsNone() || Root->GetRootChooser() != Root
		|| Unarmed->GetRootChooser() != Unarmed || Weapon->GetRootChooser() != Weapon
		|| Root->ContextData.IsEmpty()) { return false; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "WeaponChooserRouter", "Configure weapon Chooser routes"));
	Root->Modify();
	FInstancedStruct Column = FInstancedStruct::Make<FChooserNameColumn>();
	auto& Names = Column.GetMutable<FChooserNameColumn>();
	Names.InputValue = FInstancedStruct::Make<FNameContextProperty>();
	auto& Binding = Names.InputValue.GetMutable<FNameContextProperty>().Binding;
	Binding.ContextIndex = 0; Binding.PropertyBindingChain = {WeaponProperty, TEXT("TagName")};
	FChooserNameRowData Rifle; Rifle.Value = WeaponTag.GetTagName();
	FChooserNameRowData Other; Other.Value = WeaponTag.GetTagName(); Other.Comparison = ENameColumnCellValueComparison::MatchNotEqual;
	Names.RowValues = {Rifle, Other};
	Root->ColumnsStructs = {Column};
	Root->ResultsStructs = {FInstancedStruct::Make<FEvaluateChooser>(Weapon), FInstancedStruct::Make<FEvaluateChooser>(Unarmed)};
	Root->FallbackResult = FInstancedStruct::Make<FEvaluateChooser>(Unarmed);
	Root->DisabledRows.Reset(); Root->CookedResults.Reset();
	Root->NestedChoosers.Reset(); Root->NestedObjects.Reset();
	Root->PostEditChange(); Root->Compile(true); Root->MarkPackageDirty();
	return true;
}

bool UHMS_WeaponAnimationTools::ConfigureCoverPeekChooser(UChooserTable* Root, UChooserTable* Concealed,
	UAnimSequence* PeekIdle, UAnimSequence* ConcealedIdle, UUserDefinedEnum* PeekEnum, UUserDefinedEnum* CoverEnum)
{
	if (!Root || !Concealed || Root == Concealed || !PeekIdle || !ConcealedIdle || !PeekEnum || !CoverEnum
		|| Root->ContextData.Num() < 2 || PeekEnum->NumEnums() < 3 || CoverEnum->NumEnums() < 3) { return false; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "CoverPeekChooser", "Configure cover peek Chooser"));
	Root->Modify();
	auto MakeEnum = [](FName Property, UUserDefinedEnum* Enum)
	{
		FInstancedStruct Result = FInstancedStruct::Make<FEnumColumn>();
		auto& Column = Result.GetMutable<FEnumColumn>();
		Column.InputValue = FInstancedStruct::Make<FEnumContextProperty>();
		auto& Binding = Column.InputValue.GetMutable<FEnumContextProperty>().Binding;
		Binding.ContextIndex = 0; Binding.PropertyBindingChain = {Property}; Binding.Enum = Enum;
		FChooserEnumRowData Active; Active.Value = static_cast<uint8>(Enum->GetValueByIndex(1)); Active.ValueName = Enum->GetNameByIndex(1);
		FChooserEnumRowData Second = Active;
		if (Property == TEXT("CoverPeekState")) { Second.Value = 0; Second.ValueName = Enum->GetNameByIndex(0); }
		Column.RowValues = {Active, Second}; return Result;
	};
	FInstancedStruct WeaponColumn = FInstancedStruct::Make<FChooserNameColumn>();
	auto& Weapon = WeaponColumn.GetMutable<FChooserNameColumn>();
	Weapon.InputValue = FInstancedStruct::Make<FNameContextProperty>();
	auto& WeaponBinding = Weapon.InputValue.GetMutable<FNameContextProperty>().Binding;
	WeaponBinding.ContextIndex = 0; WeaponBinding.PropertyBindingChain = {TEXT("EquippedWeaponType"), TEXT("TagName")};
	FChooserNameRowData Rifle; Rifle.Value = TEXT("HMS.Weapon.Rifle"); Weapon.RowValues = {Rifle, Rifle};
	FInstancedStruct OutputColumn = FInstancedStruct::Make<FOutputStructColumn>();
	auto& Output = OutputColumn.GetMutable<FOutputStructColumn>();
	Output.InputValue = FInstancedStruct::Make<FStructContextProperty>();
	auto& OutputBinding = Output.InputValue.GetMutable<FStructContextProperty>().Binding;
	OutputBinding.ContextIndex = 1; OutputBinding.IsBoundToRoot = true; OutputBinding.StructType = FHMS_ChooserOutputs::StaticStruct();
	Output.DefaultRowValue = FInstancedStruct::Make<FHMS_ChooserOutputs>();
	Output.DefaultRowValue.GetMutable<FHMS_ChooserOutputs>().BlendTime = .35f;
	Output.DefaultRowValue.GetMutable<FHMS_ChooserOutputs>().Tags = {FName(TEXT("Interaction"))};
	Output.RowValues = {Output.DefaultRowValue, Output.DefaultRowValue}; Output.FallbackValue = Output.DefaultRowValue;
	FInstancedStruct Asset = FInstancedStruct::Make<FAssetChooser>(); Asset.GetMutable<FAssetChooser>().Asset = PeekIdle;
	FInstancedStruct HiddenAsset = FInstancedStruct::Make<FAssetChooser>(); HiddenAsset.GetMutable<FAssetChooser>().Asset = ConcealedIdle;
	Root->ColumnsStructs = {MakeEnum(TEXT("CoverPeekState"), PeekEnum), MakeEnum(TEXT("CoverState"), CoverEnum), WeaponColumn, OutputColumn};
	Root->ResultsStructs = {Asset, HiddenAsset}; Root->FallbackResult = FInstancedStruct::Make<FEvaluateChooser>(Concealed);
	Root->DisabledRows.Reset(); Root->CookedResults.Reset();
	Root->PostEditChange(); Root->Compile(true); Root->MarkPackageDirty(); return true;
}

bool UHMS_WeaponAnimationTools::InstallCoverPeekAimOffset(UAnimBlueprint* Blueprint, UBlendSpace* AimOffset)
{
	if (!Blueprint || !AimOffset || !AimOffset->IsA<UAimOffsetBlendSpace>() || AimOffset->GetSkeleton() != Blueprint->TargetSkeleton) { return false; }
	for (FName Name : {FName(TEXT("CoverPeekState")), FName(TEXT("CoverPeekAimOffset")), FName(TEXT("CoverPeekAimReady")), FName(TEXT("StateMachineState"))})
	{ if (!FindFProperty<FProperty>(Blueprint->GeneratedClass, Name)) { return false; } }
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	UAnimGraphNode_SaveCachedPose* Destination = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->NodeComment == TEXT("HMS Cover Peek Aim Offset")) { return false; }
			if (auto* Cache = Cast<UAnimGraphNode_SaveCachedPose>(Node); Cache && Cache->CacheName == TEXT("PreRagdoll")) { Destination = Cache; }
		}
	}
	UEdGraphPin* Input = Destination ? Destination->FindPin(TEXT("Pose"), EGPD_Input) : nullptr;
	if (!Input || Input->LinkedTo.Num() != 1) { return false; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "CoverPeekAO", "Install cover peek Aim Offset"));
	Blueprint->Modify(); Destination->Modify(); UEdGraph* Graph = Destination->GetGraph(); Graph->Modify();
	UEdGraphPin* Source = Input->LinkedTo[0]; const auto* Schema = GetDefault<UEdGraphSchema_K2>();
	TArray<UEdGraphNode*> Created;
	FGraphNodeCreator<UAnimGraphNode_RotationOffsetBlendSpace> OffsetCreator(*Graph); auto* Offset = OffsetCreator.CreateNode();
	Offset->Node.SetBlendSpace(AimOffset); Offset->Node.bApplyAdditiveInRootSpace = true;
	Offset->Node.AlphaInputType = EAnimAlphaInputType::Bool;
	Offset->Node.AlphaBoolBlend.BlendInTime = .25f; Offset->Node.AlphaBoolBlend.BlendOutTime = .25f;
	OffsetCreator.Finalize(); Offset->ReconstructNode(); Created.Add(Offset);
	Offset->NodeComment = TEXT("HMS Cover Peek Aim Offset"); Offset->bCommentBubbleVisible = true;
	Offset->NodePosX = Destination->NodePosX - 420; Offset->NodePosY = Destination->NodePosY;
	FGraphNodeCreator<UK2Node_CallFunction> FunctionCreator(*Graph); auto* Inputs = FunctionCreator.CreateNode();
	Inputs->SetFromFunction(UHMS_WeaponAnimationLibrary::StaticClass()->FindFunctionByName(TEXT("CalculateCoverPeekAimInputs")));
	FunctionCreator.Finalize(); Created.Add(Inputs); Inputs->NodePosX = Offset->NodePosX - 350; Inputs->NodePosY = Offset->NodePosY + 400;
	bool Valid = true; auto Wire = [&](UEdGraphPin* A, UEdGraphPin* B) { Valid &= A && B && Schema->TryCreateConnection(A, B); };
	const TPair<FName,FName> Bindings[] = {{TEXT("CoverPeekState"),TEXT("PeekState")},{TEXT("CoverPeekAimOffset"),TEXT("AimOffset")},{TEXT("StateMachineState"),TEXT("StateTags")},{TEXT("CoverPeekAimReady"),TEXT("AimReady")}};
	int32 Row = 0;
	for (const auto& Binding : Bindings)
	{
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph); auto* Getter = Creator.CreateNode();
		Getter->VariableReference.SetSelfMember(Binding.Key); Creator.Finalize(); Created.Add(Getter);
		Getter->NodePosX = Inputs->NodePosX - 320; Getter->NodePosY = Inputs->NodePosY + Row++ * 95;
		Wire(Getter->FindPin(Binding.Key), Inputs->FindPin(Binding.Value));
	}
	Wire(Source, Offset->FindPin(TEXT("BasePose")));
	Wire(Inputs->FindPin(TEXT("Enabled")), Offset->FindPin(TEXT("bAlphaBoolEnabled")));
	Wire(Inputs->FindPin(TEXT("Yaw")), Offset->FindPin(TEXT("X")));
	Wire(Inputs->FindPin(TEXT("Pitch")), Offset->FindPin(TEXT("Y")));
	if (!Valid)
	{
		for (UEdGraphNode* Node : Created) { FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true); }
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); return false;
	}
	if (!Schema->TryCreateConnection(Offset->FindPin(TEXT("Pose")), Input))
	{
		for (UEdGraphNode* Node : Created) { FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true); }
		Schema->TryCreateConnection(Source, Input); return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); return true;
}

bool UHMS_WeaponAnimationTools::UseFullBodyWeaponReadyPose(UAnimBlueprint* Blueprint)
{
	if (!Blueprint) { return false; }
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	UAnimGraphNode_SaveCachedPose* Cache = nullptr;
	UAnimGraphNode_BlendListByBool* Posture = nullptr;
	UAnimGraphNode_BlendSpacePlayer* Ready = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* N = Cast<UAnimGraphNode_SaveCachedPose>(Node); N && N->CacheName == TEXT("HMS_PreWeapon")) { Cache = N; }
			if (auto* N = Cast<UAnimGraphNode_BlendListByBool>(Node); N && N->NodeComment.StartsWith(TEXT("Aim rotation mode"))) { Posture = N; }
			if (auto* N = Cast<UAnimGraphNode_BlendSpacePlayer>(Node); N && N->NodeComment == TEXT("Rifle: chest ready")) { Ready = N; }
		}
	}
	if (!Cache || !Posture || Cache->GetGraph() != Posture->GetGraph()) { return false; }
	UEdGraphPin* Input = Posture->FindPin(TEXT("BlendPose_1"));
	if (!Input) { return false; }
	if (Input->LinkedTo.Num() == 1 && Cast<UAnimGraphNode_UseCachedPose>(Input->LinkedTo[0]->GetOwningNode())) { return true; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "FullBodyWeaponReady", "Use full-body rifle ready animations"));
	Blueprint->Modify(); Posture->Modify(); auto* Graph = Cache->GetGraph(); Graph->Modify();
	FGraphNodeCreator<UAnimGraphNode_UseCachedPose> Creator(*Graph); auto* Source = Creator.CreateNode();
	Source->SaveCachedPoseNode = Cache; Creator.Finalize();
	Source->NodePosX = Posture->NodePosX - 330; Source->NodePosY = Posture->NodePosY - 150;
	Source->NodeComment = TEXT("Ready: full-body weapon family from CT_Famale"); Source->bCommentBubbleVisible = true;
	if (!GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Source->FindPin(TEXT("Pose")), Input))
	{ FBlueprintEditorUtils::RemoveNode(Blueprint, Source, true); return false; }
	if (Ready) { FBlueprintEditorUtils::RemoveNode(Blueprint, Ready, true); }
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

UBlendSpace* UHMS_WeaponAnimationTools::CreateWeaponBlendSpace(const FString& PackagePath,
	USkeleton* Skeleton, const TArray<UAnimSequence*>& Animations,
	const TArray<FVector>& SamplePositions, bool AimOffset)
{
	if (!Skeleton || Animations.IsEmpty() || Animations.Num() != SamplePositions.Num()
		|| !FPackageName::IsValidLongPackageName(PackagePath) || FPackageName::DoesPackageExist(PackagePath)
		|| FindPackage(nullptr, *PackagePath)) { return nullptr; }
	for (int32 Index = 0; Index < Animations.Num(); ++Index)
	{
		if (!Animations[Index] || Animations[Index]->GetSkeleton() != Skeleton || SamplePositions[Index].ContainsNaN()
			|| (AimOffset && (Animations[Index]->AdditiveAnimType != AAT_RotationOffsetMeshSpace
				|| !Animations[Index]->IsValidAdditive()))) { return nullptr; }
	}
	UBlendSpace* Space = NewObject<UBlendSpace>(CreatePackage(*PackagePath),
		AimOffset ? UAimOffsetBlendSpace::StaticClass() : UBlendSpace1D::StaticClass(),
		*FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Standalone | RF_Transactional);
	Space->SetSkeleton(Skeleton);
	FStructProperty* Parameters = FindFProperty<FStructProperty>(Space->GetClass(), TEXT("BlendParameters"));
	check(Parameters);
	auto* X = Parameters->ContainerPtrToValuePtr<FBlendParameter>(Space, 0);
	X->DisplayName = AimOffset ? TEXT("Aim Yaw") : TEXT("Ground Speed");
	X->Min = AimOffset ? -75.f : 0.f; X->Max = AimOffset ? 75.f : 300.f; X->GridNum = AimOffset ? 4 : 3;
	if (AimOffset)
	{
		auto* Y = Parameters->ContainerPtrToValuePtr<FBlendParameter>(Space, 1);
		Y->DisplayName = TEXT("Aim Pitch"); Y->Min = -60.f; Y->Max = 60.f; Y->GridNum = 2;
	}
	for (int32 Index = 0; Index < Animations.Num(); ++Index)
	{
		if (Space->AddSample(Animations[Index], SamplePositions[Index]) == INDEX_NONE)
		{
			Space->ClearFlags(RF_Public | RF_Standalone); Space->MarkAsGarbage(); return nullptr;
		}
	}
	Space->ValidateSampleData(); Space->ResampleData(); Space->PostEditChange(); Space->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Space);
	return Space;
}

bool UHMS_WeaponAnimationTools::RefreshWeaponBlendSpace(UBlendSpace* BlendSpace)
{
	if (!BlendSpace || BlendSpace->GetBlendSamples().IsEmpty()) { return false; }
	const int32 Count = BlendSpace->GetBlendSamples().Num();
	for (const FBlendSample& Sample : BlendSpace->GetBlendSamples())
	{
		if (!Sample.Animation || Sample.Animation->GetSkeleton() != BlendSpace->GetSkeleton()
			|| (BlendSpace->IsA<UAimOffsetBlendSpace>() && (!Sample.Animation->IsValidAdditive()
				|| Sample.Animation->AdditiveAnimType != AAT_RotationOffsetMeshSpace))) { return false; }
	}
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "RefreshWeaponBlendSpace", "Refresh weapon blend space"));
	BlendSpace->Modify(); BlendSpace->ValidateSampleData(); BlendSpace->ResampleData();
	BlendSpace->PostEditChange(); BlendSpace->MarkPackageDirty();
	return Count == BlendSpace->GetBlendSamples().Num();
}

FString UHMS_WeaponAnimationTools::InstallWeaponLayer(UAnimBlueprint* Blueprint, FName BeforeCachedPose,
	UBlendSpace* Ready, UBlendSpace* Aim, UBlendSpace* AimOffset, FName UpperBodyBone, FGameplayTag LayerPose)
{
	if (!Blueprint || !Ready || !Aim || !AimOffset || !LayerPose.IsValid() || !Blueprint->TargetSkeleton
		|| Blueprint->TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(UpperBodyBone) == INDEX_NONE
		|| Ready->GetSkeleton() != Blueprint->TargetSkeleton || Aim->GetSkeleton() != Blueprint->TargetSkeleton
		|| AimOffset->GetSkeleton() != Blueprint->TargetSkeleton || !AimOffset->IsA<UAimOffsetBlendSpace>())
	{ return TEXT("Invalid blueprint, skeleton, bone, tag or animation assets"); }
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	UAnimGraphNode_SaveCachedPose* Destination = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Cache = Cast<UAnimGraphNode_SaveCachedPose>(Node))
			{
				if (Cache->CacheName == TEXT("HMS_PreWeapon")) { return TEXT("Weapon layer already installed; edit the existing nodes"); }
				if (Cache->CacheName == BeforeCachedPose.ToString()) { Destination = Cache; }
			}
		}
	}
	if (!Destination) { return TEXT("Destination cached pose not found"); }
	UEdGraphPin* Input = Destination->FindPin(TEXT("Pose"), EGPD_Input);
	if (!Input || Input->LinkedTo.Num() != 1) { return TEXT("Destination needs one connected local pose"); }
	for (FName Name : {FName(TEXT("RotationMode")), FName(TEXT("AO")), FName(TEXT("Speed2D")), FName(TEXT("StateMachineState"))})
	{
		if (!FindFProperty<FProperty>(Blueprint->GeneratedClass, Name)) { return TEXT("Missing HMS locomotion input: ") + Name.ToString(); }
	}
	if (const FProperty* Existing = FindFProperty<FProperty>(Blueprint->GeneratedClass, TEXT("EquippedWeaponType")))
	{
		const FStructProperty* Tag = CastField<FStructProperty>(Existing);
		if (!Tag || Tag->Struct != FGameplayTag::StaticStruct()) { return TEXT("EquippedWeaponType exists with an incompatible type"); }
	}
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "WeaponLayer", "Install HMS weapon animation layer"));
	Blueprint->Modify(); Destination->Modify(); UEdGraph* Graph = Destination->GetGraph(); Graph->Modify();
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (!FindFProperty<FProperty>(Blueprint->GeneratedClass, TEXT("EquippedWeaponType")))
	{
		FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Struct; Type.PinSubCategoryObject = FGameplayTag::StaticStruct();
		FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("EquippedWeaponType"), Type, TEXT("(TagName=\"HMS.Weapon.None\")"));
		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, TEXT("EquippedWeaponType"), nullptr, NSLOCTEXT("HMS", "WeaponCategory", "HMS | Weapon"));
	}
	TArray<UEdGraphNode*> Created;
	auto Make = [&]<typename T>(const FString& Comment, int32 X, int32 Y) -> T*
	{
		FGraphNodeCreator<T> Creator(*Graph); T* Node = Creator.CreateNode(); Creator.Finalize();
		Node->NodeComment = Comment; Node->bCommentBubbleVisible = true;
		Node->NodePosX = Destination->NodePosX - 1600 + X; Node->NodePosY = Destination->NodePosY + 1000 + Y;
		Created.Add(Node); return Node;
	};
	bool Valid = true;
	auto Wire = [&](UEdGraphPin* A, UEdGraphPin* B) { Valid &= A && B && Schema->TryCreateConnection(A, B); };
	auto* Cache = Make.operator()<UAnimGraphNode_SaveCachedPose>(TEXT("HMS: original locomotion, curves and root motion"), 0, 0);
	Cache->CacheName = TEXT("HMS_PreWeapon"); Cache->Node.CachePoseName = TEXT("HMS_PreWeapon");
	auto* Base = Make.operator()<UAnimGraphNode_UseCachedPose>(TEXT("Legs stay in locomotion"), 750, 0); Base->SaveCachedPoseNode = Cache;
	auto* Unarmed = Make.operator()<UAnimGraphNode_UseCachedPose>(TEXT("No weapon"), 1050, 450); Unarmed->SaveCachedPoseNode = Cache;
	auto* ReadyPlayer = Make.operator()<UAnimGraphNode_BlendSpacePlayer>(TEXT("Rifle: chest ready"), 0, 250);
	ReadyPlayer->Node.SetBlendSpace(Ready); ReadyPlayer->Node.SetLoop(true); ReadyPlayer->ReconstructNode();
	auto* AimPlayer = Make.operator()<UAnimGraphNode_BlendSpacePlayer>(TEXT("Rifle: aimed hold"), 0, 550);
	AimPlayer->Node.SetBlendSpace(Aim); AimPlayer->Node.SetLoop(true); AimPlayer->ReconstructNode();
	auto* Offset = Make.operator()<UAnimGraphNode_RotationOffsetBlendSpace>(TEXT("Rifle Aim Offset: yaw +/-75, pitch +/-60"), 330, 550);
	Offset->Node.SetBlendSpace(AimOffset); Offset->Node.bApplyAdditiveInRootSpace = true; Offset->ReconstructNode();
	auto* Posture = Make.operator()<UAnimGraphNode_BlendListByBool>(TEXT("Aim rotation mode raises the weapon (0.25 s)"), 680, 300);
	auto* Layer = Make.operator()<UAnimGraphNode_LayeredBoneBlend>(TEXT("Upper body only; retain locomotion curves and root motion"), 1020, 80);
	Layer->Node.LayerSetup.SetNum(1); Layer->Node.LayerSetup[0].BranchFilters.Reset();
	FBranchFilter Filter; Filter.BoneName = UpperBodyBone; Filter.BlendDepth = 3; Layer->Node.LayerSetup[0].BranchFilters.Add(Filter);
	Layer->Node.bMeshSpaceRotationBlend = true; Layer->Node.bRootSpaceRotationBlend = true;
	Layer->Node.CurveBlendOption = ECurveBlendOption::UseBasePose; Layer->Node.bBlendRootMotionBasedOnRootBone = true;
	auto* Equipped = Make.operator()<UAnimGraphNode_BlendListByBool>(TEXT("EquippedWeaponType tag switch (0.25 s); interaction poses have priority"), 1400, 160);
	FGraphNodeCreator<UK2Node_CallFunction> FunctionCreator(*Graph); auto* Inputs = FunctionCreator.CreateNode();
	Inputs->SetFromFunction(UHMS_WeaponAnimationLibrary::StaticClass()->FindFunctionByName(TEXT("CalculateWeaponLayerInputs"))); FunctionCreator.Finalize();
	Created.Add(Inputs); Inputs->NodePosX = Cache->NodePosX; Inputs->NodePosY = Cache->NodePosY + 1050;
	FString TagText; FGameplayTag::StaticStruct()->ExportText(TagText, &LayerPose, nullptr, nullptr, PPF_None, nullptr);
	Schema->TrySetDefaultValue(*Inputs->FindPin(TEXT("LayerPose")), TagText);
	const TPair<FName, FName> Bindings[] = {{TEXT("EquippedWeaponType"),TEXT("WeaponPose")},{TEXT("RotationMode"),TEXT("RotationMode")},
		{TEXT("AO"),TEXT("AimOffset")},{TEXT("Speed2D"),TEXT("GroundSpeed")},{TEXT("StateMachineState"),TEXT("StateTags")}};
	int32 Row = 0;
	for (const auto& Binding : Bindings)
	{
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph); auto* Getter = Creator.CreateNode();
		Getter->VariableReference.SetSelfMember(Binding.Key); Creator.Finalize(); Created.Add(Getter);
		Getter->NodePosX = Inputs->NodePosX - 320; Getter->NodePosY = Inputs->NodePosY + Row++ * 95;
		Wire(Getter->FindPin(Binding.Key), Inputs->FindPin(Binding.Value));
	}
	Wire(Input->LinkedTo[0], Cache->FindPin(TEXT("Pose"), EGPD_Input));
	Wire(Base->FindPin(TEXT("Pose"), EGPD_Output), Layer->FindPin(TEXT("BasePose")));
	Wire(ReadyPlayer->FindPin(TEXT("Pose")), Posture->FindPin(TEXT("BlendPose_1")));
	Wire(AimPlayer->FindPin(TEXT("Pose")), Offset->FindPin(TEXT("BasePose")));
	Wire(Offset->FindPin(TEXT("Pose")), Posture->FindPin(TEXT("BlendPose_0")));
	Wire(Posture->FindPin(TEXT("Pose")), Layer->FindPin(TEXT("BlendPoses_0")));
	Wire(Layer->FindPin(TEXT("Pose")), Equipped->FindPin(TEXT("BlendPose_0")));
	Wire(Unarmed->FindPin(TEXT("Pose")), Equipped->FindPin(TEXT("BlendPose_1")));
	Wire(Inputs->FindPin(TEXT("Enabled")), Equipped->FindPin(TEXT("bActiveValue")));
	Wire(Inputs->FindPin(TEXT("Aiming")), Posture->FindPin(TEXT("bActiveValue")));
	Wire(Inputs->FindPin(TEXT("Speed")), ReadyPlayer->FindPin(TEXT("X")));
	Wire(Inputs->FindPin(TEXT("Speed")), AimPlayer->FindPin(TEXT("X")));
	Wire(Inputs->FindPin(TEXT("Yaw")), Offset->FindPin(TEXT("X")));
	Wire(Inputs->FindPin(TEXT("Pitch")), Offset->FindPin(TEXT("Y")));
	for (auto* Blend : {Posture, Equipped})
	{
		for (const FName PinName : {FName(TEXT("BlendTime_0")), FName(TEXT("BlendTime_1"))})
		{
			if (UEdGraphPin* Pin = Blend->FindPin(PinName)) { Schema->TrySetDefaultValue(*Pin, TEXT("0.25")); } else { Valid = false; }
		}
	}
	if (!Valid)
	{
		for (UEdGraphNode* Node : Created) { FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true); }
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return TEXT("Weapon node connection failed; original pose route preserved");
	}
	Schema->BreakPinLinks(*Input, true); Wire(Equipped->FindPin(TEXT("Pose")), Input);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return TEXT("Installed EquippedWeaponType, rifle ready/aim layer and Aim Offset before ") + BeforeCachedPose.ToString();
}
