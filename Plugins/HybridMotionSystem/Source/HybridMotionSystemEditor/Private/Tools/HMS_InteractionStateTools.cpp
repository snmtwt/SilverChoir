#include "Tools/HMS_InteractionStateTools.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "AnimStateNode.h"
#include "AnimStateAliasNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_Slot.h"
#include "K2Node_CallFunction.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "HMS_AnimInstance.h"
#include "HMS_MovementStruct.h"
#include "Chooser.h"
#include "EnumColumn.h"
#include "GameplayTagColumn.h"
#include "OutputStructColumn.h"
#include "ObjectChooser_Asset.h"
#include "Engine/UserDefinedEnum.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "PoseSearch/Chooser/PoseSearchChooserColumn.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"

int32 UHMS_InteractionStateTools::ReplaceChooserAnimations(UChooserTable* Chooser,
	const TArray<UAnimSequence*>& SourceAnimations, const TArray<UAnimSequence*>& ReplacementAnimations)
{
	if (!IsValid(Chooser) || SourceAnimations.IsEmpty()
		|| SourceAnimations.Num() != ReplacementAnimations.Num()) { return -1; }
	TMap<UObject*, UAnimSequence*> Replacements;
	for (int32 Index = 0; Index < SourceAnimations.Num(); ++Index)
	{
		UAnimSequence* Source = SourceAnimations[Index];
		UAnimSequence* Target = ReplacementAnimations[Index];
		if (!IsValid(Source) || !IsValid(Target) || Replacements.Contains(Source)
			|| !Source->GetSkeleton() || Source->GetSkeleton() != Target->GetSkeleton()) { return -1; }
		Replacements.Add(Source, Target);
	}
	TArray<int32> Rows;
	for (int32 Row = 0; Row < Chooser->ResultsStructs.Num(); ++Row)
	{
		if (const FAssetChooser* Asset = Chooser->ResultsStructs[Row].GetPtr<FAssetChooser>())
		{
			if (UAnimSequence** Target = Replacements.Find(Asset->Asset.Get()); Target && *Target != Asset->Asset)
			{ Rows.Add(Row); }
		}
	}
	if (Rows.IsEmpty())
	{
		ReplaceChooserDatabaseAnimations(Chooser, SourceAnimations, ReplacementAnimations);
		return 0;
	}
	// UE 5.8 exposes RowValues through reflection; EditRowValue is not DLL-exported.
	FArrayProperty* PoseRowsProperty = FindFProperty<FArrayProperty>(FPoseSearchColumn::StaticStruct(), TEXT("RowValues"));
	const FStructProperty* PoseRowProperty = PoseRowsProperty ? CastField<FStructProperty>(PoseRowsProperty->Inner) : nullptr;
	if (!PoseRowProperty || PoseRowProperty->Struct != FPoseSearchColumnRow::StaticStruct()) { return -1; }
	for (const FInstancedStruct& Column : Chooser->ColumnsStructs)
	{
		if (const FPoseSearchColumn* Pose = Column.GetPtr<FPoseSearchColumn>();
			Pose && Pose->GetNumRows() != Chooser->ResultsStructs.Num()) { return -1; }
	}
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "ReplaceChooserAnimations", "Replace Chooser animations"));
	UChooserTable* Root = Chooser->GetRootChooser();
	Root->Modify();
	if (Root != Chooser) { Chooser->Modify(); }
	TSet<UPoseSearchDatabase*> Databases;
	for (int32 Row : Rows)
	{
		FAssetChooser& Asset = Chooser->ResultsStructs[Row].GetMutable<FAssetChooser>();
		UAnimSequence* Replacement = Replacements.FindChecked(Asset.Asset.Get());
		Asset.Asset = Replacement;
		for (FInstancedStruct& Column : Chooser->ColumnsStructs)
		{
			if (FPoseSearchColumn* Pose = Column.GetMutablePtr<FPoseSearchColumn>())
			{
				FScriptArrayHelper PoseRows(PoseRowsProperty, PoseRowsProperty->ContainerPtrToValuePtr<void>(Pose));
				FPoseSearchColumnRow* PoseRow = reinterpret_cast<FPoseSearchColumnRow*>(PoseRows.GetRawPtr(Row));
				PoseRow->Data.AnimAsset = Replacement;
				if (UPoseSearchDatabase* Database = Pose->GetDatabase(Row)) { Databases.Add(Database); }
			}
		}
	}
	// Root property notification rebuilds Pose Search's table-to-database mapping.
	Root->PostEditChange();
	Root->Compile(true);
	Root->MarkPackageDirty();
	for (UPoseSearchDatabase* Database : Databases)
	{
		Database->Modify();
		Database->PostEditChange();
	}
	ReplaceChooserDatabaseAnimations(Chooser, SourceAnimations, ReplacementAnimations);
	return Rows.Num();
}

int32 UHMS_InteractionStateTools::ReplaceChooserDatabaseAnimations(UChooserTable* Chooser,
	const TArray<UAnimSequence*>& SourceAnimations, const TArray<UAnimSequence*>& ReplacementAnimations)
{
	if (!IsValid(Chooser) || SourceAnimations.IsEmpty()
		|| SourceAnimations.Num() != ReplacementAnimations.Num()) { return -1; }
	TMap<UObject*, UAnimSequence*> Replacements;
	for (int32 Index = 0; Index < SourceAnimations.Num(); ++Index)
	{
		UAnimSequence* Source = SourceAnimations[Index];
		UAnimSequence* Target = ReplacementAnimations[Index];
		if (!IsValid(Source) || !IsValid(Target) || Replacements.Contains(Source)
			|| !Source->GetSkeleton() || Source->GetSkeleton() != Target->GetSkeleton()) { return -1; }
		Replacements.Add(Source, Target);
	}
	UChooserTable* Root = Chooser->GetRootChooser();
	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(Root, SubObjects, EGetObjectsFlags::IncludeNestedObjects);
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "ReplaceChooserSearchAnimations", "Replace Chooser search animations"));
	int32 Changed = 0;
	for (UObject* Object : SubObjects)
	{
		UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(Object);
		if (!Database) { continue; }
		bool bChangedDatabase = false;
		for (int32 Index = 0; Index < Database->GetNumAnimationAssets(); ++Index)
		{
			const FPoseSearchDatabaseAnimationAsset* Entry = Database->GetDatabaseAnimationAsset(Index);
			UAnimSequence* const* Replacement = Replacements.Find(Database->GetAnimationAsset(Index));
			if (!Entry || !Replacement || *Replacement == Database->GetAnimationAsset(Index)) { continue; }
			if (Changed == 0) { Root->Modify(); }
			if (!bChangedDatabase) { Database->Modify(); }
			FPoseSearchDatabaseAnimationAsset Updated = *Entry;
			Updated.AnimAsset = *Replacement;
			Database->SetAnimationAssetAt(Updated, Index);
			bChangedDatabase = true;
			++Changed;
		}
		if (bChangedDatabase) { Database->PostEditChange(); }
	}
	if (Changed > 0) { Root->MarkPackageDirty(); }
	return Changed;
}

int32 UHMS_InteractionStateTools::CopyMissingSkeletonBlendProfile(USkeleton* TargetSkeleton,
	USkeleton* SourceSkeleton, FName ProfileName)
{
	if (!IsValid(TargetSkeleton) || !IsValid(SourceSkeleton) || ProfileName.IsNone()) { return -1; }
	if (TargetSkeleton->GetBlendProfile(ProfileName)) { return 0; }
	const UBlendProfile* Source = SourceSkeleton->GetBlendProfile(ProfileName);
	if (!Source) { return -1; }
	TArray<TPair<int32, float>> Entries;
	for (const FBlendProfileBoneEntry& Entry : Source->ProfileEntries)
	{
		const int32 BoneIndex = TargetSkeleton->GetReferenceSkeleton().FindBoneIndex(Entry.BoneReference.BoneName);
		if (BoneIndex != INDEX_NONE)
		{
			if (!FMath::IsFinite(Entry.BlendScale) || Entry.BlendScale < 0.f) { return -1; }
			Entries.Emplace(BoneIndex, Entry.BlendScale);
		}
	}
	if (Entries.IsEmpty()) { return -1; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "CopySkeletonBlendProfile", "Copy skeleton blend profile by bone name"));
	TargetSkeleton->Modify();
	UBlendProfile* Target = TargetSkeleton->CreateNewBlendProfile(ProfileName);
	Target->Modify();
	Target->SetSkeleton(TargetSkeleton);
	Target->Mode = Source->Mode;
	for (const TPair<int32, float>& Entry : Entries)
	{
		Target->SetBoneBlendScale(Entry.Key, Entry.Value, false, true);
	}
	TargetSkeleton->MarkPackageDirty();
	return Entries.Num();
}

int32 UHMS_InteractionStateTools::SetChooserAnimationStartTimeLimit(UChooserTable* Chooser,
	UAnimSequence* Animation, float MaximumStartTime)
{
	if (!IsValid(Chooser) || !IsValid(Animation) || !FMath::IsFinite(MaximumStartTime)
		|| MaximumStartTime < -1.f || MaximumStartTime > Animation->GetPlayLength()) { return -1; }
	TArray<FHMS_ChooserOutputs*> Cells;
	for (FInstancedStruct& Column : Chooser->ColumnsStructs)
	{
		if (FOutputStructColumn* Output = Column.GetMutablePtr<FOutputStructColumn>())
		{
			if (Output->RowValues.Num() != Chooser->ResultsStructs.Num()) { return -1; }
			for (int32 Index = 0; Index < Output->RowValues.Num(); ++Index)
			{
				const FAssetChooser* Result = Chooser->ResultsStructs[Index].GetPtr<FAssetChooser>();
				if (!Result || Result->Asset != Animation) { continue; }
				if (FHMS_ChooserOutputs* Cell = Output->RowValues[Index].GetMutablePtr<FHMS_ChooserOutputs>();
					Cell && Cell->MaximumStartTime != MaximumStartTime) { Cells.Add(Cell); }
			}
		}
	}
	if (Cells.IsEmpty()) { return 0; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "SetChooserStartLimit", "Set Chooser playback entry limit"));
	UChooserTable* Root = Chooser->GetRootChooser();
	Root->Modify();
	if (Root != Chooser) { Chooser->Modify(); }
	for (FHMS_ChooserOutputs* Cell : Cells) { Cell->MaximumStartTime = MaximumStartTime; }
	Chooser->PostEditChange();
	Root->PostEditChange();
	Root->Compile(true);
	Root->MarkPackageDirty();
	return Cells.Num();
}

UK2Node_CallFunction* UHMS_InteractionStateTools::AddFunctionCall(UAnimBlueprint* Blueprint, UEdGraph* Graph, FName FunctionName)
{
	if (!Blueprint || !Graph || FBlueprintEditorUtils::FindBlueprintForGraph(Graph) != Blueprint
		|| !Blueprint->SkeletonGeneratedClass)
	{ return nullptr; }
	UFunction* Function = Blueprint->SkeletonGeneratedClass->FindFunctionByName(FunctionName);
	const bool bLocal = Function != nullptr;
	if (!Function) { Function = LoadObject<UFunction>(nullptr, *FunctionName.ToString()); }
	if (!Function || !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure)) { return nullptr; }
	Graph->Modify();
	FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
	UK2Node_CallFunction* Call = Creator.CreateNode();
	if (bLocal) { Call->FunctionReference.SetSelfMember(FunctionName); }
	else { Call->SetFromFunction(Function); }
	Creator.Finalize();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return Call;
}

FString UHMS_InteractionStateTools::AddLogicalInteractionState(UAnimBlueprint* Blueprint,
	FName MachineName, FName StateName, const TArray<FName>& EntryStates, FName MovingState,
	FName IdleState, FName EnterPredicate, FName ExitMovingPredicate, FName ExitIdlePredicate,
	FName EntryCallback, FName UpdateCallback)
{
	if (!Blueprint || !Blueprint->ParentClass->IsChildOf(UHMS_AnimInstance::StaticClass())
		|| !Blueprint->SkeletonGeneratedClass || StateName.IsNone() || EntryStates.IsEmpty())
	{ return TEXT("Error: invalid HMS Blueprint or state configuration"); }
	for (FName Function : {EnterPredicate, ExitMovingPredicate, ExitIdlePredicate, EntryCallback, UpdateCallback})
	{
		if (!Blueprint->SkeletonGeneratedClass->FindFunctionByName(Function))
		{ return TEXT("Error: missing Blueprint function ") + Function.ToString(); }
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	UEdGraph* Machine = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph->GetFName() == MachineName)
		{
			if (Machine) { return TEXT("Error: ambiguous state machine name"); }
			Machine = Graph;
		}
	}
	if (!Machine) { return TEXT("Error: state machine not found"); }
	TMap<FName, UAnimStateNode*> States;
	for (UEdGraphNode* Node : Machine->Nodes)
	{
		if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
		{ States.Add(FName(*State->GetStateName()), State); }
	}
	if (States.Contains(StateName)) { return TEXT("Error: state already exists; no changes made"); }
	TArray<FName> Required = EntryStates;
	Required.Add(MovingState); Required.Add(IdleState);
	for (FName Name : Required)
	{
		if (!States.Contains(Name)) { return TEXT("Error: missing state ") + Name.ToString(); }
	}
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "AddInteractionState", "Add HMS interaction animation state"));
	Blueprint->Modify(); Machine->Modify();
	const auto PreviousNodes = Machine->Nodes;
	for (UEdGraphNode* Node : PreviousNodes)
	{
		if (auto* UnusedAlias = Cast<UAnimStateAliasNode>(Node); UnusedAlias && !UnusedAlias->bGlobalAlias
			&& UnusedAlias->GetAliasedStates().IsEmpty())
		{ FBlueprintEditorUtils::RemoveNode(Blueprint, UnusedAlias, true); }
	}
	FGraphNodeCreator<UAnimStateNode> StateCreator(*Machine);
	UAnimStateNode* State = StateCreator.CreateNode();
	State->NodePosX = 500; State->NodePosY = 850;
	StateCreator.Finalize();
	FBlueprintEditorUtils::RenameGraph(State->BoundGraph, StateName.ToString());
	UAnimGraphNode_StateResult* Result = State->GetResultNodeInsideState();
	Result->StateEntryFunction.SetSelfMember(EntryCallback);
	Result->UpdateFunction.SetSelfMember(UpdateCallback);
	State->bAlwaysResetOnEntry = true;

	FGraphNodeCreator<UAnimStateAliasNode> AliasCreator(*Machine);
	UAnimStateAliasNode* Alias = AliasCreator.CreateNode();
	Alias->StateAliasName = TEXT("Enter ") + StateName.ToString();
	Alias->bGlobalAlias = false;
	Alias->NodePosX = 100; Alias->NodePosY = 850;
	for (FName Name : EntryStates) { Alias->GetAliasedStates().Add(States[Name]); }
	AliasCreator.Finalize();
	const auto AddTransition = [&](UAnimStateNodeBase* From, UAnimStateNodeBase* To, FName Predicate, int32 Priority)
	{
		FGraphNodeCreator<UAnimStateTransitionNode> Creator(*Machine);
		UAnimStateTransitionNode* Transition = Creator.CreateNode();
		Transition->PriorityOrder = Priority;
		// This graph selects animations; the Blend Stack and entry montage provide the visible blend.
		// Overlapping logical updates would let the outgoing state's callback overwrite the new selection.
		Transition->CrossfadeDuration = 0.f;
		Creator.Finalize();
		Transition->CreateConnections(From, To);
		UEdGraph* Rule = Transition->BoundGraph;
		UAnimGraphNode_TransitionResult* Output = nullptr;
		for (UEdGraphNode* Node : Rule->Nodes)
		{ if (auto* Candidate = Cast<UAnimGraphNode_TransitionResult>(Node)) { Output = Candidate; } }
		if (!Output) { return false; }
		FGraphNodeCreator<UK2Node_CallFunction> CallCreator(*Rule);
		UK2Node_CallFunction* Call = CallCreator.CreateNode();
		Call->FunctionReference.SetSelfMember(Predicate);
		Call->NodePosX = Output->NodePosX - 300;
		CallCreator.Finalize();
		return GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(
			Call->GetReturnValuePin(), Output->FindPinChecked(TEXT("bCanEnterTransition")));
	};
	const bool bConnected = AddTransition(Alias, State, EnterPredicate, 0)
		&& AddTransition(State, States[MovingState], ExitMovingPredicate, 0)
		&& AddTransition(State, States[IdleState], ExitIdlePredicate, 1);
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Slot = Cast<UAnimGraphNode_Slot>(Node); Slot && Slot->Node.SlotName == TEXT("DefaultSlot"))
			{
				Slot->Modify();
				Slot->Node.bAlwaysUpdateSourcePose = true;
			}
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return bConnected ? TEXT("Created interaction state, three transitions and source-pose update")
		: TEXT("Error: transition connection failed; undo the transaction and check predicate signatures");
}

UChooserTable* UHMS_InteractionStateTools::CreateInteractionIdleChooser(UChooserTable* RootChooser,
	const FString& PackagePath, FGameplayTag StateTag, FName EnumProperty, UUserDefinedEnum* StateEnum,
	const TArray<UAnimSequence*>& IdleAnimations, float BlendTime)
{
	if (!RootChooser || !StateEnum || !StateTag.IsValid() || EnumProperty.IsNone()
		|| !FPackageName::IsValidLongPackageName(PackagePath) || FPackageName::DoesPackageExist(PackagePath)
		|| FindPackage(nullptr, *PackagePath) || RootChooser->ColumnsStructs.Num() != 1
		|| RootChooser->ContextData.Num() < 2 || !FMath::IsFinite(BlendTime)) { return nullptr; }
	auto* Tags = RootChooser->ColumnsStructs[0].GetMutablePtr<FGameplayTagColumn>();
	if (!Tags || Tags->RowValues.Num() != RootChooser->ResultsStructs.Num()) { return nullptr; }
	for (const FGameplayTagContainer& Row : Tags->RowValues)
	{ if (Row.IsEmpty() || Row.HasTagExact(StateTag)) { return nullptr; } }
	const int32 NumStates = StateEnum->NumEnums() - 2; // exclude None and generated MAX
	if (NumStates < 1 || IdleAnimations.Num() != NumStates) { return nullptr; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "CreateIdleChooser", "Create HMS interaction idle Chooser"));
	RootChooser->Modify();
	UChooserTable* Chooser = NewObject<UChooserTable>(CreatePackage(*PackagePath),
		*FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Standalone | RF_Transactional);
	Chooser->ContextData = RootChooser->ContextData;
	Chooser->OutputObjectType = UAnimationAsset::StaticClass();
	FInstancedStruct EnumColumn = FInstancedStruct::Make<FEnumColumn>();
	auto& Enum = EnumColumn.GetMutable<FEnumColumn>();
	Enum.InputValue = FInstancedStruct::Make<FEnumContextProperty>();
	auto& EnumBinding = Enum.InputValue.GetMutable<FEnumContextProperty>().Binding;
	EnumBinding.ContextIndex = 0;
	EnumBinding.PropertyBindingChain = {EnumProperty};
	EnumBinding.Enum = StateEnum;
	FInstancedStruct OutputColumn = FInstancedStruct::Make<FOutputStructColumn>();
	auto& Output = OutputColumn.GetMutable<FOutputStructColumn>();
	Output.InputValue = FInstancedStruct::Make<FStructContextProperty>();
	auto& OutputBinding = Output.InputValue.GetMutable<FStructContextProperty>().Binding;
	OutputBinding.ContextIndex = 1;
	OutputBinding.IsBoundToRoot = true;
	OutputBinding.StructType = FHMS_ChooserOutputs::StaticStruct();
	Output.DefaultRowValue = FInstancedStruct::Make<FHMS_ChooserOutputs>();
	Output.DefaultRowValue.GetMutable<FHMS_ChooserOutputs>().BlendTime = FMath::Clamp(BlendTime, 0.f, 2.f);
	Output.DefaultRowValue.GetMutable<FHMS_ChooserOutputs>().Tags = {FName(TEXT("Interaction"))};
	Output.FallbackValue = Output.DefaultRowValue;
	for (int32 Index = 0; Index < NumStates; ++Index)
	{
		FChooserEnumRowData Row;
		Row.Value = static_cast<uint8>(StateEnum->GetValueByIndex(Index + 1));
		Row.ValueName = StateEnum->GetNameByIndex(Index + 1);
		Enum.RowValues.Add(Row);
		Output.RowValues.Add(Output.DefaultRowValue);
		FInstancedStruct Asset = FInstancedStruct::Make<FAssetChooser>();
		Asset.GetMutable<FAssetChooser>().Asset = IdleAnimations[Index];
		Chooser->ResultsStructs.Add(Asset);
	}
	Chooser->ColumnsStructs = {EnumColumn, OutputColumn};
	FInstancedStruct Route = FInstancedStruct::Make<FEvaluateChooser>();
	Route.GetMutable<FEvaluateChooser>().Chooser = Chooser;
	RootChooser->ResultsStructs.Add(Route);
	Tags->RowValues.Add(FGameplayTagContainer(StateTag));
	Chooser->Compile(true); RootChooser->Compile(true);
	Chooser->MarkPackageDirty(); RootChooser->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Chooser);
	return Chooser;
}

FString UHMS_InteractionStateTools::DescribeInteractionChooser(UChooserTable* Chooser)
{
	if (!Chooser) { return TEXT("No Chooser"); }
	FString Result;
	for (int32 Index = 0; Index < Chooser->ResultsStructs.Num(); ++Index)
	{
		FString Value;
		Chooser->ResultsStructs[Index].ExportTextItem(Value, FInstancedStruct(), nullptr, PPF_None, nullptr);
		Result += FString::Printf(TEXT("Row %d: %s\n"), Index, *Value);
	}
	return Result;
}
