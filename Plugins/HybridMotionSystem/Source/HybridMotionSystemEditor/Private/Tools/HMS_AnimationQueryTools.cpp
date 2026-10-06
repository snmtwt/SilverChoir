#include "Tools/HMS_AnimationQueryTools.h"
#include "Animation/AnimBlueprint.h"
#include "Chooser.h"
#include "NameColumn.h"
#include "ChooserIndexArray.h"
#include "ObjectChooser_Asset.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "Serialization/ArchiveReplaceObjectRef.h"

namespace
{
	bool Reaches(UChooserTable* From, const UChooserTable* Target)
	{
		TArray<UChooserTable*> Pending = {From}; TSet<UChooserTable*> Visited;
		while (!Pending.IsEmpty())
		{
			UChooserTable* Table = Pending.Pop(EAllowShrinking::No);
			if (!Table) { continue; }
			if (Table == Target) { return true; }
			if (Visited.Contains(Table)) { continue; } Visited.Add(Table);
			auto Add = [&](const FInstancedStruct& Result)
			{
				if (const auto* Link = Result.GetPtr<FEvaluateChooser>()) { Pending.Add(Link->Chooser); }
				if (const auto* Link = Result.GetPtr<FNestedChooser>()) { Pending.Add(Link->Chooser); }
			};
			for (const auto& Result : Table->ResultsStructs) { Add(Result); } Add(Table->FallbackResult);
		}
		return false;
	}
	bool Compatible(const UChooserTable* A, const UChooserTable* B)
	{
		return A && B && A->ContextData == B->ContextData && A->OutputObjectType == B->OutputObjectType && A->ResultType == B->ResultType;
	}
}

TArray<UChooserTable*> UHMS_AnimationQueryTools::TraceAnimationQueryRoute(UChooserTable* Root, UObject* ContextObject)
{
	TArray<UChooserTable*> Path;
	if (!IsValid(ContextObject)) { return Path; }
	FChooserEvaluationContext Context(ContextObject);
	for (UChooserTable* Table = Root; Table;)
	{
		if (Path.Contains(Table)) { return {}; } Path.Add(Table);
		if (Table->ColumnsStructs.Num() != 1) { return Path; }
		const auto* Column = Table->ColumnsStructs[0].GetPtr<FChooserNameColumn>();
		const auto* Input = Column ? Column->InputValue.GetPtr<FNameContextProperty>() : nullptr;
		if (!Input || Input->Binding.PropertyBindingChain.Num() != 2
			|| (Input->Binding.PropertyBindingChain[0] != TEXT("BehaviorState") && Input->Binding.PropertyBindingChain[0] != TEXT("EquippedWeaponType"))) { return Path; }
		FName Value;
		if (!Column->bDisabled && !Input->GetValue(Context, Value)) { return {}; }
		using FIndex = FChooserIndexArray::FIndexData;
		const uint32 Count = Table->ResultsStructs.Num();
		TArray<FIndex> A, B; A.Init(FIndex(0, 0.f), Count); B.Init(FIndex(0, 0.f), Count);
		FChooserIndexArray Indices(A.GetData(), Count), Filtered(B.GetData(), Count);
		for (uint32 I = 0; I < Count; ++I) { if (!Table->IsRowDisabled(I)) { Indices.Push(FIndex(I, 0.f)); } }
		if (Column->bDisabled) { Filtered = Indices; }
		else { Column->Filter(Context, Indices, Filtered); }
		const auto& Result = Filtered.IsEmpty() ? Table->FallbackResult : Table->ResultsStructs[Filtered[0].Index];
		const auto* Child = Result.GetPtr<FEvaluateChooser>();
		if (!Child || !Child->Chooser) { return {}; } Table = Child->Chooser;
	}
	return {};
}

bool UHMS_AnimationQueryTools::InstallAnimationQueryVariables(UAnimBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->GeneratedClass) { return false; }
	for (FName Name : {FName(TEXT("WeaponPose")), FName(TEXT("EquippedWeaponType")), FName(TEXT("BehaviorState"))})
	{
		if (const auto* P = FindFProperty<FProperty>(Blueprint->GeneratedClass, Name))
		{
			const auto* Tag = CastField<FStructProperty>(P);
			if (!Tag || Tag->Struct != FGameplayTag::StaticStruct()) { return false; }
		}
	}
	const bool Legacy = FindFProperty<FProperty>(Blueprint->GeneratedClass, TEXT("WeaponPose")) != nullptr;
	const bool Weapon = FindFProperty<FProperty>(Blueprint->GeneratedClass, TEXT("EquippedWeaponType")) != nullptr;
	if (Legacy && Weapon) { return false; } // Ambiguous sources must not be silently merged.
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "QueryVariables", "Configure animation query variables"));
	Blueprint->Modify();
	if (Legacy) { FBlueprintEditorUtils::RenameMemberVariable(Blueprint, TEXT("WeaponPose"), TEXT("EquippedWeaponType")); }
	FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_Struct; Type.PinSubCategoryObject = FGameplayTag::StaticStruct();
	if (!Legacy && !Weapon && !FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("EquippedWeaponType"), Type, TEXT("(TagName=\"HMS.Weapon.None\")"))) { return false; }
	if (!FindFProperty<FProperty>(Blueprint->GeneratedClass, TEXT("BehaviorState"))
		&& !FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("BehaviorState"), Type, TEXT("(TagName=\"HMS.Behavior.Relaxed\")"))) { return false; }
	for (FName Name : {FName(TEXT("EquippedWeaponType")), FName(TEXT("BehaviorState"))})
	{
		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, Name, nullptr, NSLOCTEXT("HMS", "QueryCategory", "HMS | Animation Query"));
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Name, nullptr, TEXT("Categories"),
			Name == TEXT("BehaviorState") ? TEXT("HMS.Behavior") : TEXT("HMS.Weapon"));
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error;
}

bool UHMS_AnimationQueryTools::ConfigureAnimationTagRouter(UChooserTable* Root, FName Property,
	const TArray<FGameplayTag>& Values, const TArray<UChooserTable*>& Children, UChooserTable* Fallback)
{
	if (!Root || Root->GetRootChooser() != Root || Property.IsNone() || Values.IsEmpty()
		|| Values.Num() != Children.Num() || Root->ContextData.IsEmpty()) { return false; }
	const auto* Context = Root->ContextData[0].GetPtr<FContextObjectTypeClass>();
	const auto* Tag = Context && Context->Class ? FindFProperty<FStructProperty>(Context->Class, Property) : nullptr;
	if (!Tag || Tag->Struct != FGameplayTag::StaticStruct()) { return false; }
	TSet<FGameplayTag> Unique;
	for (int32 I = 0; I < Children.Num(); ++I)
	{
		if (!Values[I].IsValid() || Unique.Contains(Values[I]) || !Compatible(Root, Children[I]) || Reaches(Children[I], Root)) { return false; }
		Unique.Add(Values[I]);
	}
	if (Fallback && (!Compatible(Root, Fallback) || Reaches(Fallback, Root))) { return false; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "TagRouter", "Configure animation tag routes"));
	Root->Modify();
	FInstancedStruct Filter = FInstancedStruct::Make<FChooserNameColumn>();
	auto& Column = Filter.GetMutable<FChooserNameColumn>();
	Column.InputValue = FInstancedStruct::Make<FNameContextProperty>();
	auto& Binding = Column.InputValue.GetMutable<FNameContextProperty>().Binding;
	Binding.ContextIndex = 0; Binding.PropertyBindingChain = {Property, TEXT("TagName")};
	Binding.DisplayName = Property == TEXT("BehaviorState") ? TEXT("行为状态 / Behavior State")
		: Property == TEXT("EquippedWeaponType") ? TEXT("装备武器类型 / Equipped Weapon Type") : Property.ToString();
	Column.EditorColumnWidth = 360.f;
	Root->ResultsStructs.Reset();
	for (int32 I = 0; I < Values.Num(); ++I)
	{
		FChooserNameRowData Row; Row.Value = Values[I].GetTagName(); Column.RowValues.Add(Row);
		Root->ResultsStructs.Add(FInstancedStruct::Make<FEvaluateChooser>(Children[I]));
	}
	Root->ColumnsStructs = {Filter};
	Root->FallbackResult = Fallback ? FInstancedStruct::Make<FEvaluateChooser>(Fallback) : FInstancedStruct();
	Root->NestedChoosers.Reset(); Root->NestedObjects.Reset(); Root->DisabledRows.Reset(); Root->CookedResults.Reset();
	Root->PostEditChange(); Root->Compile(true); Root->MarkPackageDirty(); return true;
}

int32 UHMS_AnimationQueryTools::RenameQueryTagBinding(UChooserTable* Root, FName OldName, FName NewName)
{
	if (!Root || OldName.IsNone() || NewName.IsNone() || OldName == NewName) { return -1; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "RenameQueryTag", "Migrate query tag bindings"));
	TArray<UObject*> Owned; GetObjectsWithOuter(Root, Owned, EGetObjectsFlags::IncludeNestedObjects); Owned.Add(Root);
	int32 Changed = 0;
	for (UObject* Object : Owned)
	{
		auto* Table = Cast<UChooserTable>(Object); if (!Table) { continue; }
		for (auto& Data : Table->ColumnsStructs)
		{
			auto* Column = Data.GetMutablePtr<FChooserNameColumn>();
			auto* Input = Column ? Column->InputValue.GetMutablePtr<FNameContextProperty>() : nullptr;
			if (Input && !Input->Binding.PropertyBindingChain.IsEmpty() && Input->Binding.PropertyBindingChain[0] == OldName)
			{ Table->Modify(); Input->Binding.PropertyBindingChain[0] = NewName; ++Changed; }
		}
		Table->Compile(true);
	}
	if (Changed) { Root->PostEditChange(); Root->MarkPackageDirty(); } return Changed;
}

bool UHMS_AnimationQueryTools::ReplaceQueryChild(UChooserTable* Root, UChooserTable* OldChild, UChooserTable* NewChild)
{
	if (!Root || !OldChild || !Compatible(OldChild, NewChild) || Reaches(NewChild, Root)) { return false; }
	const FScopedTransaction Transaction(NSLOCTEXT("HMS", "ReplaceQueryChild", "Replace query child table"));
	TArray<UObject*> Owned; GetObjectsWithOuter(Root, Owned, EGetObjectsFlags::IncludeNestedObjects); Owned.Add(Root);
	for (UObject* Object : Owned) { Object->Modify(); }
	TMap<UObject*, UObject*> Map; Map.Add(OldChild, NewChild);
	FArchiveReplaceObjectRef<UObject> Replace(Root, Map, EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
	Root->PostEditChange(); Root->Compile(true); Root->MarkPackageDirty(); return Replace.GetCount() > 0;
}
