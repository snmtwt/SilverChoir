#pragma once

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Self.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace MainMapBP
{
inline bool OK = true;
inline const UEdGraphSchema_K2* S() { return GetDefault<UEdGraphSchema_K2>(); }
inline FEdGraphPinType Type(FName Category, UObject* Sub = nullptr, EPinContainerType Container = EPinContainerType::None)
{
    FEdGraphPinType T; T.PinCategory = Category; T.PinSubCategoryObject = Sub; T.ContainerType = Container;
    if (Category == UEdGraphSchema_K2::PC_Real) T.PinSubCategory = UEdGraphSchema_K2::PC_Double;
    return T;
}
inline FEdGraphPinType Bool() { return Type(UEdGraphSchema_K2::PC_Boolean); }
inline FEdGraphPinType Int() { return Type(UEdGraphSchema_K2::PC_Int); }
inline FEdGraphPinType Real() { return Type(UEdGraphSchema_K2::PC_Real); }
inline FEdGraphPinType Obj(UClass* C) { return Type(UEdGraphSchema_K2::PC_Object, C); }
inline FEdGraphPinType Struct(UScriptStruct* C) { return Type(UEdGraphSchema_K2::PC_Struct, C); }
inline UEdGraphPin* P(UEdGraphNode* N, FName Name)
{
    auto* Pin = N ? N->FindPin(Name) : nullptr;
    if (!Pin) { OK = false; UE_LOG(LogTemp, Error, TEXT("ARCH_PIN_MISSING %s.%s graph=%s"), *GetNameSafe(N), *Name.ToString(), N ? *GetNameSafe(N->GetGraph()) : TEXT("null")); }
    return Pin;
}
inline void Link(UEdGraphPin* A, UEdGraphPin* B)
{
    if (!A || !B || !S()->TryCreateConnection(A, B))
    { OK = false; UE_LOG(LogTemp, Error, TEXT("ARCH_LINK_FAILED %s.%s -> %s.%s"), A ? *GetNameSafe(A->GetOwningNode()) : TEXT("null"), A ? *A->PinName.ToString() : TEXT("null"), B ? *GetNameSafe(B->GetOwningNode()) : TEXT("null"), B ? *B->PinName.ToString() : TEXT("null")); }
}
inline void D(UEdGraphNode* N, FName Name, const FString& Value)
{ if (auto* Pin = P(N, Name)) S()->TrySetDefaultValue(*Pin, Value); }
inline UEdGraph* Find(UBlueprint* BP, FName Name)
{ TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs); for (auto* G : Graphs) if (G->GetFName() == Name) return G; return nullptr; }
inline void Var(UBlueprint* BP, FName Name, FEdGraphPinType T, const FString& Value, const TCHAR* Label, bool Editable = false, const TCHAR* Category = TEXT("作战指挥室|流程状态"))
{
    if (BP->NewVariables.ContainsByPredicate([&](const auto& V) { return V.VarName == Name; })) return;
    if (!FBlueprintEditorUtils::AddMemberVariable(BP, Name, T, Value)) { OK = false; UE_LOG(LogTemp, Error, TEXT("ARCH_VARIABLE_FAILED %s.%s"), *BP->GetName(), *Name.ToString()); return; }
    for (auto& V : BP->NewVariables) if (V.VarName == Name)
    {
        V.Category = FText::FromString(Category);
        V.PropertyFlags = (V.PropertyFlags | CPF_BlueprintVisible) & ~CPF_BlueprintReadOnly;
        if (Editable) V.PropertyFlags |= CPF_Edit;
        else V.PropertyFlags = (V.PropertyFlags | CPF_Transient) & ~CPF_Edit;
    }
    FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, Name, nullptr, TEXT("DisplayName"), Label);
}
struct FParam { FName Name; FEdGraphPinType T; FString Default; };
inline UEdGraph* Declare(UBlueprint* BP, FName Name, TArray<FParam> Inputs = {}, TArray<FParam> Outputs = {}, UClass* Override = nullptr)
{
    auto* G = FBlueprintEditorUtils::CreateNewGraph(BP, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(BP, G, Override == nullptr, Override);
    UK2Node_FunctionEntry* Entry = nullptr; UK2Node_FunctionResult* Result = nullptr;
    for (UEdGraphNode* N : G->Nodes)
    { if (auto* E = Cast<UK2Node_FunctionEntry>(N)) Entry = E; if (auto* R = Cast<UK2Node_FunctionResult>(N)) Result = R; }
    check(Entry);
    Entry->MetaData.Category = FText::FromString(TEXT("作战指挥室|蓝图流程"));
    if (!Override) Entry->SetExtraFlags(FUNC_Public | FUNC_BlueprintCallable);
    for (const auto& I : Inputs)
    { Entry->CreateUserDefinedPin(I.Name, I.T, EGPD_Output); if (!I.Default.IsEmpty()) D(Entry, I.Name, I.Default); }
    if (!Outputs.IsEmpty() && !Result)
    { FGraphNodeCreator<UK2Node_FunctionResult> C(*G); Result = C.CreateNode(); Result->FunctionReference.SetSelfMember(Name); C.Finalize(); }
    if (Result)
    {
        for (const auto& O : Outputs)
        { Result->CreateUserDefinedPin(O.Name, O.T, EGPD_Input); if (!O.Default.IsEmpty()) D(Result, O.Name, O.Default); }
        Link(P(Entry, TEXT("then")), P(Result, TEXT("execute")));
    }
    return G;
}
struct FGraph
{
    UBlueprint* BP; UEdGraph* G; UK2Node_FunctionEntry* Entry = nullptr;
    int32 X = 300, Y = 0;
    FGraph(UBlueprint* InBP, UEdGraph* InG) : BP(InBP), G(InG)
    {
        for (UEdGraphNode* N : G->Nodes) if (auto* E = Cast<UK2Node_FunctionEntry>(N)) { Entry = E; E->BreakAllNodeLinks(); }
    }
    FGraph(UBlueprint* InBP, FName Name) : FGraph(InBP, Find(InBP, Name)) { check(G); }
    template<class T> T* Node(TFunctionRef<void(T*)> Setup)
    { FGraphNodeCreator<T> C(*G); auto* N = C.CreateNode(); Setup(N); N->NodePosX = X; N->NodePosY = Y; X += 300; C.Finalize(); return N; }
    void Row(int32 InY) { X = 300; Y = InY; }
    UEdGraphPin* Start() { return P(Entry, TEXT("then")); }
    UEdGraphPin* Exec(UEdGraphPin* From, UEdGraphNode* N)
    { if (N && !N->FindPin(TEXT("execute")) && N->IsA<UK2Node_CallFunction>()) return From; Link(From, P(N, TEXT("execute"))); return P(N, TEXT("then")); }
    UK2Node_CallFunction* Call(UClass* C, FName Name)
    {
        auto* F = C ? C->FindFunctionByName(Name) : nullptr;
        if (!F) { OK = false; UE_LOG(LogTemp, Error, TEXT("ARCH_FUNCTION_MISSING %s.%s"), *GetNameSafe(C), *Name.ToString()); }
        return Node<UK2Node_CallFunction>([&](auto* N) { if (F) N->SetFromFunction(F); });
    }
    UK2Node_CallFunction* Call(FName Name)
    { return Node<UK2Node_CallFunction>([&](auto* N) { N->FunctionReference.SetSelfMember(Name); }); }
    UK2Node_CallArrayFunction* Array(FName Name, UEdGraphPin* Target)
    {
        auto* F = UKismetArrayLibrary::StaticClass()->FindFunctionByName(Name); check(F);
        auto* N = Node<UK2Node_CallArrayFunction>([&](auto* V) { V->SetFromFunction(F); });
        Link(Target, P(N, TEXT("TargetArray"))); return N;
    }
    UEdGraphPin* Invoke(UEdGraphPin* From, FName Name) { return Exec(From, Call(Name)); }
    UEdGraphPin* Self() { return P(Node<UK2Node_Self>([](auto*) {}), TEXT("self")); }
    UEdGraphPin* Get(FName Name, UClass* Owner = nullptr, UEdGraphPin* Target = nullptr)
    {
        auto* N = Node<UK2Node_VariableGet>([&](auto* V) { if (Owner) V->VariableReference.SetExternalMember(Name, Owner); else V->VariableReference.SetSelfMember(Name); });
        if (Target) Link(Target, P(N, TEXT("self"))); return N->GetValuePin();
    }
    UEdGraphPin* Set(UEdGraphPin* From, FName Name, const FString& Literal = TEXT(""), UEdGraphPin* Value = nullptr, UClass* Owner = nullptr, UEdGraphPin* Target = nullptr)
    {
        auto* N = Node<UK2Node_VariableSet>([&](auto* V) { if (Owner) V->VariableReference.SetExternalMember(Name, Owner); else V->VariableReference.SetSelfMember(Name); });
        if (Value) Link(Value, P(N, Name)); else D(N, Name, Literal);
        if (Target) Link(Target, P(N, TEXT("self"))); return Exec(From, N);
    }
    UK2Node_IfThenElse* Branch(UEdGraphPin* From, UEdGraphPin* Condition)
    { auto* N = Node<UK2Node_IfThenElse>([](auto*) {}); Link(From, N->GetExecPin()); Link(Condition, N->GetConditionPin()); return N; }
    UEdGraphPin* Math(FName Name, UEdGraphPin* A, UEdGraphPin* B = nullptr, const TCHAR* BLiteral = nullptr)
    {
        auto* N = Call(UKismetMathLibrary::StaticClass(), Name); Link(A, P(N, TEXT("A")));
        if (B) Link(B, P(N, TEXT("B"))); if (BLiteral) D(N, TEXT("B"), BLiteral); return N->GetReturnValuePin();
    }
    UEdGraphPin* Both(UEdGraphPin* A, UEdGraphPin* B) { return Math(TEXT("BooleanAND"), A, B); }
    UEdGraphPin* Not(UEdGraphPin* A) { return Math(TEXT("Not_PreBool"), A); }
    UEdGraphPin* Valid(UEdGraphPin* A)
    { auto* N = Call(UKismetSystemLibrary::StaticClass(), TEXT("IsValid")); Link(A, P(N, TEXT("Object"))); return N->GetReturnValuePin(); }
    UEdGraphPin* Stage(int32 I) { return Math(TEXT("EqualEqual_IntInt"), Get(TEXT("FlowStage")), nullptr, *FString::FromInt(I)); }
    UK2Node_DynamicCast* CastTo(UClass* C, UEdGraphPin* Object, UEdGraphPin* From)
    { auto* N = Node<UK2Node_DynamicCast>([&](auto* V) { V->TargetType = C; }); Link(Object, N->GetCastSourcePin()); Exec(From, N); return N; }
    UK2Node_ExecutionSequence* Sequence(UEdGraphPin* From, int32 Count = 2)
    { auto* N = Node<UK2Node_ExecutionSequence>([](auto*) {}); for (int I = 2; I < Count; ++I) N->AddInputPin(); Link(From, N->GetExecPin()); return N; }
    UK2Node_CreateDelegate* Delegate(FName Name, UEdGraphPin* Target = nullptr)
    { auto* N = Node<UK2Node_CreateDelegate>([&](auto* V) { V->SetFunction(Name); }); if (Target) Link(Target, N->GetObjectInPin()); return N; }
    UEdGraphPin* Bind(UEdGraphPin* From, UClass* C, UEdGraphPin* Target, FName Name, FName Callback, bool Remove = false)
    {
        UK2Node_BaseMCDelegate* N = Remove
            ? static_cast<UK2Node_BaseMCDelegate*>(Node<UK2Node_RemoveDelegate>([&](auto* V) { V->DelegateReference.SetExternalMember(Name, C); }))
            : static_cast<UK2Node_BaseMCDelegate*>(Node<UK2Node_AddDelegate>([&](auto* V) { V->DelegateReference.SetExternalMember(Name, C); }));
        Link(Target, P(N, TEXT("self"))); auto* Dlg = Delegate(Callback); Link(Dlg->GetDelegateOutPin(), N->GetDelegatePin()); Dlg->HandleAnyChangeWithoutNotifying();
        return Exec(From, N);
    }
    UK2Node_MacroInstance* Loop(UEdGraphPin* From, UEdGraphPin* Array, bool Break = false)
    {
        auto* Macros = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
        UEdGraph* Macro = Find(Macros, Break ? TEXT("ForEachLoopWithBreak") : TEXT("ForEachLoop")); check(Macro);
        auto* N = Node<UK2Node_MacroInstance>([&](auto* V) { V->SetMacroGraph(Macro); });
        Link(Array, P(N, TEXT("Array"))); Link(From, P(N, TEXT("Exec"))); return N;
    }
    UK2Node_BreakStruct* Break(UScriptStruct* T, UEdGraphPin* Value)
    { auto* N = Node<UK2Node_BreakStruct>([&](auto* V) { V->StructType = T; }); Link(Value, P(N, T->GetFName())); return N; }
    void Return(UEdGraphPin* From, TMap<FName, UEdGraphPin*> Values = {}, TMap<FName, FString> Literals = {})
    {
        UK2Node_FunctionResult* Template = nullptr;
        for (UEdGraphNode* N : G->Nodes) if (auto* R = Cast<UK2Node_FunctionResult>(N)) { Template = R; break; }
        auto* N = Node<UK2Node_FunctionResult>([&](auto* R)
        {
            R->FunctionReference = Entry->FunctionReference;
            if (Template) for (const auto& Pin : Template->UserDefinedPins)
                R->UserDefinedPins.Add(MakeShared<FUserPinInfo>(*Pin));
        });
        for (const auto& V : Values) Link(V.Value, P(N, V.Key));
        for (const auto& V : Literals) D(N, V.Key, V.Value);
        Link(From, P(N, TEXT("execute")));
    }
    void Comment(const TCHAR* Text)
    {
        auto* N = NewObject<UEdGraphNode_Comment>(G); G->AddNode(N); N->CreateNewGuid();
        N->NodeComment = FString(Text) + TEXT("\nMainMapBlueprintArchitecture_v1");
        N->NodePosX = -100; N->NodePosY = -250; N->NodeWidth = 2000; N->NodeHeight = 170; N->CommentColor = FLinearColor(.025f,.12f,.18f,1);
    }
};
inline bool Compile(UBlueprint* BP)
{
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status == BS_Error) OK = false; return OK;
}
}
