#include "PersonnelSceneReturnRepairCommandlet.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/BaseSceneWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

UPersonnelSceneReturnRepairCommandlet::UPersonnelSceneReturnRepairCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

namespace PersonnelSceneReturnRepair
{
constexpr const TCHAR* ScenePath = TEXT("/Game/System/Map/BaseMap/Scene/BP_人员整备室_Scene");
constexpr const TCHAR* InteriorGraphName = TEXT("相机移动到场景上方后，开始进入场景");
constexpr const TCHAR* LoadCallbackName = TEXT("人员整备室界面加载完成");
constexpr const TCHAR* UnloadCallbackName = TEXT("人员整备室界面卸载完成");
constexpr const TCHAR* EnterGateName = TEXT("人员整备室全部加载完成");
constexpr const TCHAR* VisibilityHelperName = TEXT("PersonnelReturn_SetBackButtonVisible");
constexpr const TCHAR* RepairMarker = TEXT("PersonnelReturnRepairV1");

bool Check(bool bCondition, const FString& Message)
{
    if (!bCondition) UE_LOG(LogTemp, Error, TEXT("PERSONNEL_RETURN_REPAIR_CHECK_FAILED %s"), *Message);
    return bCondition;
}

template<class T, class Predicate> T* UniqueNode(UEdGraph* Graph, Predicate Match, const TCHAR* Description)
{
    TArray<T*> Found;
    if (Graph)
        for (UEdGraphNode* Node : Graph->Nodes)
            if (T* Typed = Cast<T>(Node); Typed && Match(Typed)) Found.Add(Typed);
    if (!Check(Found.Num() == 1, FString::Printf(TEXT("%s: expected exactly one match, found %d"), Description, Found.Num()))) return nullptr;
    return Found[0];
}

UEdGraph* FindGraph(UBlueprint* BP, FName Name)
{
    TArray<UEdGraph*> Found;
    for (UEdGraph* Graph : BP->FunctionGraphs)
        if (Graph && Graph->GetFName() == Name) Found.Add(Graph);
    if (!Check(Found.Num() == 1, FString::Printf(TEXT("graph %s: expected one, found %d"), *Name.ToString(), Found.Num()))) return nullptr;
    return Found[0];
}

UK2Node_FunctionEntry* Entry(UEdGraph* Graph)
{
    return UniqueNode<UK2Node_FunctionEntry>(Graph, [](const auto*) { return true; }, TEXT("function entry"));
}

UK2Node_CallFunction* Function(UEdGraph* Graph, FName Name)
{
    return UniqueNode<UK2Node_CallFunction>(Graph, [Name](const auto* Node) { return Node->FunctionReference.GetMemberName() == Name; }, *Name.ToString());
}

UEdGraphPin* GetPin(UEdGraphNode* Node, FName Name)
{
    return Node ? Node->FindPin(Name) : nullptr;
}

bool DelegateCalls(UK2Node_AddDelegate* Binding, FName Callback)
{
    UEdGraphPin* Pin = Binding ? Binding->GetDelegatePin() : nullptr;
    const auto* Delegate = Pin && Pin->LinkedTo.Num() == 1 ? Cast<UK2Node_CreateDelegate>(Pin->LinkedTo[0]->GetOwningNode()) : nullptr;
    return Delegate && Delegate->GetFunctionName() == Callback;
}

struct FTargets
{
    UEdGraph* Enter = nullptr;
    UEdGraph* Leave = nullptr;
    UEdGraph* Interior = nullptr;
    UEdGraph* EnterGate = nullptr;
    UK2Node_FunctionEntry* EnterEntry = nullptr;
    UK2Node_FunctionEntry* LeaveEntry = nullptr;
    UK2Node_CallFunction* CreateUI = nullptr;
    UK2Node_CallFunction* EarlyBack = nullptr;
    UK2Node_CallFunction* NotifyEnter = nullptr;
    UK2Node_AddDelegate* BindLoad = nullptr;
    UK2Node_AddDelegate* BindUnload = nullptr;

    bool Find(UBlueprint* BP)
    {
        Enter = FindGraph(BP, TEXT("EnterScene"));
        Leave = FindGraph(BP, TEXT("LeaveScene"));
        Interior = FindGraph(BP, InteriorGraphName);
        EnterGate = FindGraph(BP, EnterGateName);
        if (!Enter || !Leave || !Interior || !EnterGate) return false;
        EnterEntry = Entry(Enter);
        LeaveEntry = Entry(Leave);
        CreateUI = Function(Interior, GET_FUNCTION_NAME_CHECKED(UBaseMapWidget, CreateSceneUIByTag));
        EarlyBack = Function(Interior, GET_FUNCTION_NAME_CHECKED(UBaseMapWidget, SetBackButtonVisible));
        NotifyEnter = Function(EnterGate, TEXT("NotifyEnterCompleted"));
        BindLoad = UniqueNode<UK2Node_AddDelegate>(Interior, [](const auto* Node) { return Node->GetPropertyName() == TEXT("OnLoadCompleted"); }, TEXT("load binding"));
        BindUnload = UniqueNode<UK2Node_AddDelegate>(Interior, [](const auto* Node) { return Node->GetPropertyName() == TEXT("OnUnloadCompleted"); }, TEXT("unload binding"));
        return EnterEntry && LeaveEntry && CreateUI && EarlyBack && NotifyEnter && BindLoad && BindUnload;
    }

    bool ValidateOriginal() const
    {
        UEdGraphPin* StartEnter = GetPin(EnterEntry, UEdGraphSchema_K2::PN_Then);
        UEdGraphPin* StartLeave = GetPin(LeaveEntry, UEdGraphSchema_K2::PN_Then);
        UEdGraphPin* NotifyInput = GetPin(NotifyEnter, UEdGraphSchema_K2::PN_Execute);
        UEdGraphPin* LoadThen = GetPin(BindLoad, UEdGraphSchema_K2::PN_Then);
        UEdGraphPin* UnloadInput = GetPin(BindUnload, UEdGraphSchema_K2::PN_Execute);
        UEdGraphPin* UnloadThen = GetPin(BindUnload, UEdGraphSchema_K2::PN_Then);
        UEdGraphPin* LoadTarget = GetPin(BindLoad, UEdGraphSchema_K2::PN_Self);
        UEdGraphPin* UnloadTarget = GetPin(BindUnload, UEdGraphSchema_K2::PN_Self);
        UEdGraphPin* Visible = GetPin(EarlyBack, TEXT("bVisible"));
        return Check(StartEnter && StartEnter->LinkedTo.Num() == 1, TEXT("EnterScene must have one original execution continuation"))
            && Check(StartLeave && StartLeave->LinkedTo.Num() == 1, TEXT("LeaveScene must have one original execution continuation"))
            && Check(NotifyInput && NotifyInput->LinkedTo.Num() == 1, TEXT("NotifyEnterCompleted must have one original execution predecessor"))
            && Check(LoadThen && UnloadInput && LoadThen->LinkedTo.Num() == 1 && LoadThen->LinkedTo[0] == UnloadInput, TEXT("load binding must precede unload binding"))
            && Check(UnloadThen && UnloadThen->LinkedTo.IsEmpty(), TEXT("expected unused tail after unload binding; preserving unexpected user flow"))
            && Check(DelegateCalls(BindLoad, LoadCallbackName) && DelegateCalls(BindUnload, UnloadCallbackName), TEXT("original UI delegate callbacks must match the existing completion functions"))
            && Check(LoadTarget && LoadTarget->LinkedTo.Num() == 1 && LoadTarget->LinkedTo[0] == CreateUI->GetReturnValuePin(), TEXT("load binding target must be the created/reused UI"))
            && Check(UnloadTarget && UnloadTarget->LinkedTo.Num() == 1 && UnloadTarget->LinkedTo[0] == CreateUI->GetReturnValuePin(), TEXT("unload binding target must be the created/reused UI"))
            && Check(Visible && Visible->LinkedTo.IsEmpty(), TEXT("early return-button visibility must be an unconnected literal"));
    }
};

struct FBuilder
{
    UEdGraph* Graph;
    bool bOK = true;

    template<class T, class Setup> T* Node(int32 X, int32 Y, Setup Configure)
    {
        FGraphNodeCreator<T> Creator(*Graph);
        T* Result = Creator.CreateNode();
        Configure(Result);
        Result->NodePosX = X;
        Result->NodePosY = Y;
        Creator.Finalize();
        return Result;
    }

    UK2Node_CallFunction* Call(UClass* Owner, FName Name, int32 X, int32 Y)
    {
        UFunction* Target = Owner ? Owner->FindFunctionByName(Name) : nullptr;
        if (Owner && !Check(Target != nullptr, FString::Printf(TEXT("missing function %s.%s"), *Owner->GetName(), *Name.ToString()))) bOK = false;
        return Node<UK2Node_CallFunction>(X, Y, [Target, Name](auto* Result)
        {
            if (Target) Result->SetFromFunction(Target);
            else Result->FunctionReference.SetSelfMember(Name);
        });
    }

    void Link(UEdGraphPin* A, UEdGraphPin* B)
    {
        if (!Check(A && B && GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(A, B), TEXT("failed to connect repair nodes"))) bOK = false;
    }

    void Default(UEdGraphNode* Node, FName Name, const TCHAR* Value)
    {
        UEdGraphPin* Pin = GetPin(Node, Name);
        if (!Check(Pin != nullptr, FString::Printf(TEXT("missing pin %s"), *Name.ToString()))) { bOK = false; return; }
        GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(*Pin, Value);
    }
};

bool CreateVisibilityHelper(UBlueprint* BP)
{
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(BP, VisibilityHelperName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(BP, Graph, true, static_cast<UClass*>(nullptr));
    UK2Node_FunctionEntry* Start = Entry(Graph);
    if (!Start) return false;
    Start->MetaData.Category = FText::FromString(TEXT("人员整备室|进入退出"));
    Start->NodeComment = TEXT("只控制宿主返回按钮，不改变相机、UI或场景状态。缺少宿主时安全返回。");
    FEdGraphPinType BoolType;
    BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
    UEdGraphPin* Visible = Start->CreateUserDefinedPin(TEXT("bVisible"), BoolType, EGPD_Output);
    FBuilder G{Graph};
    auto* PC = G.Call(UGameplayStatics::StaticClass(), GET_FUNCTION_NAME_CHECKED(UGameplayStatics, GetPlayerController), 0, 230);
    G.Default(PC, TEXT("PlayerIndex"), TEXT("0"));
    auto* CastPC = G.Node<UK2Node_DynamicCast>(320, 0, [](auto* Node) { Node->TargetType = AGameMainMapPlayerController::StaticClass(); });
    G.Link(GetPin(Start, UEdGraphSchema_K2::PN_Then), GetPin(CastPC, UEdGraphSchema_K2::PN_Execute));
    G.Link(PC->GetReturnValuePin(), CastPC->GetCastSourcePin());
    auto* Base = G.Node<UK2Node_VariableGet>(620, 250, [](auto* Node)
    {
        Node->VariableReference.SetExternalMember(GET_MEMBER_NAME_CHECKED(AGameMainMapPlayerController, BaseWidget), AGameMainMapPlayerController::StaticClass());
    });
    G.Link(CastPC->GetCastResultPin(), GetPin(Base, UEdGraphSchema_K2::PN_Self));
    auto* Valid = G.Call(UKismetSystemLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid), 870, 250);
    G.Link(GetPin(Base, TEXT("BaseWidget")), GetPin(Valid, TEXT("Object")));
    auto* Branch = G.Node<UK2Node_IfThenElse>(920, 0, [](auto*) {});
    G.Link(CastPC->GetValidCastPin(), GetPin(Branch, UEdGraphSchema_K2::PN_Execute));
    G.Link(Valid->GetReturnValuePin(), Branch->GetConditionPin());
    auto* SetVisible = G.Call(UBaseMapWidget::StaticClass(), GET_FUNCTION_NAME_CHECKED(UBaseMapWidget, SetBackButtonVisible), 1240, 0);
    G.Link(Branch->GetThenPin(), SetVisible->GetExecPin());
    G.Link(GetPin(Base, TEXT("BaseWidget")), GetPin(SetVisible, UEdGraphSchema_K2::PN_Self));
    G.Link(Visible, GetPin(SetVisible, TEXT("bVisible")));
    return G.bOK;
}

bool Compile(UBlueprint* BP)
{
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
    return Check(Results.NumErrors == 0 && BP->Status != BS_Error, TEXT("patched Blueprint must compile without errors before saving"));
}

bool InsertVisibilityAfter(UEdGraphPin* Before, bool bVisible)
{
    if (!Check(Before && Before->LinkedTo.Num() == 1, TEXT("visibility insertion requires exactly one existing connection"))) return false;
    UEdGraphPin* After = Before->LinkedTo[0];
    UEdGraphNode* Source = Before->GetOwningNode();
    FBuilder G{Source->GetGraph()};
    auto* Call = G.Call(nullptr, VisibilityHelperName, Source->NodePosX + 260, Source->NodePosY - 220);
    G.Default(Call, TEXT("bVisible"), bVisible ? TEXT("true") : TEXT("false"));
    Call->NodeComment = bVisible ? TEXT("相机与UI已共同完成进入，此时才允许返回基地。") : TEXT("进入/离开尚未完成时隐藏返回，避免请求被场景切换锁拒绝。");
    Before->BreakLinkTo(After);
    G.Link(Before, Call->GetExecPin());
    G.Link(Call->GetThenPin(), After);
    return G.bOK;
}

void Inspect(UBlueprint* BP)
{
    UE_LOG(LogTemp, Display, TEXT("PERSONNEL_RETURN_ASSET %s status=%d"), *BP->GetPathName(), int32(BP->Status));
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (const auto* Start = Cast<UK2Node_FunctionEntry>(Node))
                for (const FBPVariableDescription& Variable : Start->LocalVariables)
                    UE_LOG(LogTemp, Display, TEXT("PERSONNEL_RETURN_LOCAL [%s] %s=%s"), *Graph->GetName(), *Variable.VarName.ToString(), *Variable.DefaultValue);
            UE_LOG(LogTemp, Display, TEXT("PERSONNEL_RETURN_NODE [%s] %s %s comment=%s"), *Graph->GetName(), *Node->GetName(), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Node->NodeComment);
            if (const auto* Delegate = Cast<UK2Node_CreateDelegate>(Node))
                UE_LOG(LogTemp, Display, TEXT("PERSONNEL_RETURN_DELEGATE %s function=%s"), *Node->GetName(), *Delegate->GetFunctionName().ToString());
            for (UEdGraphPin* Pin : Node->Pins)
            {
                FString Links;
                for (const UEdGraphPin* Other : Pin->LinkedTo) Links += Other->GetOwningNode()->GetName() + TEXT(".") + Other->PinName.ToString() + TEXT(" ");
                UE_LOG(LogTemp, Display, TEXT("PERSONNEL_RETURN_PIN %s default=%s links=%s"), *Pin->PinName.ToString(), *Pin->DefaultValue, *Links);
            }
        }
}

bool Repair(UBlueprint* BP)
{
    // Never silently replace an existing function or append a second copy of this patch.
    for (const UEdGraph* Graph : BP->FunctionGraphs)
        if (Graph && Graph->GetFName() == VisibilityHelperName)
            return Check(false, TEXT("repair helper already exists; use -Inspect to review the previously patched asset"));
    FTargets Targets;
    if (!Targets.Find(BP) || !Targets.ValidateOriginal()) return false;

    const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    const FString Backup = FPaths::ProjectSavedDir() / TEXT("PersonnelSceneReturnBackups") /
        (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)) / FPaths::GetCleanFilename(Filename);
    if (!Check(IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true), TEXT("cannot create backup directory"))
        || !Check(IFileManager::Get().Copy(*Backup, *Filename) == COPY_OK, TEXT("cannot back up original Blueprint"))) return false;
    UE_LOG(LogTemp, Display, TEXT("PERSONNEL_RETURN_BACKUP %s"), *Backup);

    // Keep every existing graph/node and all local camera-state defaults. Only splice the required links.
    TSet<FGuid> OriginalNodes;
    TMap<FString, FString> OriginalLocalDefaults;
    for (const UEdGraph* Graph : BP->FunctionGraphs)
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            OriginalNodes.Add(Node->NodeGuid);
            if (const auto* Start = Cast<UK2Node_FunctionEntry>(Node))
                for (const FBPVariableDescription& Variable : Start->LocalVariables)
                    OriginalLocalDefaults.Add(Graph->GetName() + TEXT(".") + Variable.VarName.ToString(), Variable.DefaultValue);
        }

    if (!CreateVisibilityHelper(BP) || !Compile(BP) || !Targets.Find(BP) || !Targets.ValidateOriginal()) return false;
    if (!InsertVisibilityAfter(GetPin(Targets.EnterEntry, UEdGraphSchema_K2::PN_Then), false)
        || !InsertVisibilityAfter(GetPin(Targets.LeaveEntry, UEdGraphSchema_K2::PN_Then), false)
        || !InsertVisibilityAfter(Targets.NotifyEnter->GetExecPin()->LinkedTo[0], true)) return false;

    FBuilder G{Targets.Interior};
    G.Default(Targets.EarlyBack, TEXT("bVisible"), TEXT("false"));
    auto* Loaded = G.Call(UBaseSceneWidget::StaticClass(), GET_FUNCTION_NAME_CHECKED(UBaseSceneWidget, IsSceneUILoaded), Targets.BindUnload->NodePosX + 340, Targets.BindUnload->NodePosY + 240);
    G.Link(Targets.CreateUI->GetReturnValuePin(), GetPin(Loaded, UEdGraphSchema_K2::PN_Self));
    auto* Branch = G.Node<UK2Node_IfThenElse>(Targets.BindUnload->NodePosX + 620, Targets.BindUnload->NodePosY, [](auto*) {});
    Branch->NodeComment = RepairMarker;
    Branch->bCommentBubbleVisible = true;
    G.Link(GetPin(Targets.BindUnload, UEdGraphSchema_K2::PN_Then), Branch->GetExecPin());
    G.Link(Loaded->GetReturnValuePin(), Branch->GetConditionPin());
    auto* Complete = G.Call(nullptr, LoadCallbackName, Targets.BindUnload->NodePosX + 940, Targets.BindUnload->NodePosY);
    Complete->NodeComment = TEXT("宿主复用已加载UI时不会重播OnLoadCompleted；先绑定两个事件，再补记已完成。仍在加载则继续等待事件。");
    G.Link(Branch->GetThenPin(), Complete->GetExecPin());
    if (!G.bOK || !Compile(BP)) return false;

    for (const UEdGraph* Graph : BP->FunctionGraphs)
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            OriginalNodes.Remove(Node->NodeGuid);
            if (const auto* Start = Cast<UK2Node_FunctionEntry>(Node))
                for (const FBPVariableDescription& Variable : Start->LocalVariables)
                {
                    const FString Key = Graph->GetName() + TEXT(".") + Variable.VarName.ToString();
                    if (const FString* Before = OriginalLocalDefaults.Find(Key))
                    {
                        if (!Check(*Before == Variable.DefaultValue, TEXT("original local-variable default changed: ") + Key)) return false;
                        OriginalLocalDefaults.Remove(Key);
                    }
                }
        }
    if (!Check(OriginalNodes.IsEmpty() && OriginalLocalDefaults.IsEmpty(), TEXT("an original graph node or local variable was lost"))) return false;

    BP->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    if (!Check(UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args), TEXT("cannot save repaired Blueprint"))) return false;
    Inspect(BP);
    return true;
}
}

int32 UPersonnelSceneReturnRepairCommandlet::Main(const FString& Params)
{
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, PersonnelSceneReturnRepair::ScenePath);
    if (!PersonnelSceneReturnRepair::Check(BP && BP->GeneratedClass, TEXT("personnel scene Blueprint could not be loaded"))) return 1;
    if (FParse::Param(*Params, TEXT("Inspect")))
    {
        PersonnelSceneReturnRepair::Inspect(BP);
        UE_LOG(LogTemp, Display, TEXT("PERSONNEL_RETURN_REPAIR_INSPECT_OK"));
        return 0;
    }
    if (!FParse::Param(*Params, TEXT("Repair")))
    {
        UE_LOG(LogTemp, Error, TEXT("Use -run=PersonnelSceneReturnRepair -Inspect or -Repair."));
        return 1;
    }
    const bool bSuccess = PersonnelSceneReturnRepair::Repair(BP);
    UE_LOG(LogTemp, Display, TEXT("PERSONNEL_RETURN_REPAIR_%s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));
    return bSuccess ? 0 : 1;
}
