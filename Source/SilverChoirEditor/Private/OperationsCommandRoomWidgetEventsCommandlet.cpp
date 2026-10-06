#include "OperationsCommandRoomWidgetEventsCommandlet.h"

#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

UOperationsCommandRoomWidgetEventsCommandlet::UOperationsCommandRoomWidgetEventsCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

namespace OperationsWidgetEvents
{
constexpr const TCHAR* AssetPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/OperationsCommandRoom/WBP_OperationsCommandRoom");
constexpr const TCHAR* EventsGraphName = TEXT("OperationsCommandRoomEvents");
constexpr const TCHAR* VisibilityFunctionName = TEXT("RefreshCommandTileInfoPanel");
constexpr const TCHAR* Marker = TEXT("OperationsCommandRoomWidgetEvents_v1");

bool Check(bool bCondition, const FString& Message)
{
    if (!bCondition) UE_LOG(LogTemp, Error, TEXT("OPERATIONS_WIDGET_EVENTS_REFUSED %s"), *Message);
    return bCondition;
}

struct FEventSpec
{
    FName Name;
    UClass* Owner;
};

TArray<FEventSpec> EventSpecs()
{
    return {
        {TEXT("LoadSceneUI"), UBaseSceneWidget::StaticClass()},
        {TEXT("UnloadSceneUI"), UBaseSceneWidget::StaticClass()},
        {TEXT("OnCommandTimeChanged"), UOperationsCommandRoomWidget::StaticClass()},
        {TEXT("OnCommandTimeScaleChanged"), UOperationsCommandRoomWidget::StaticClass()},
        {TEXT("OnCommandTimePausedChanged"), UOperationsCommandRoomWidget::StaticClass()},
        {TEXT("OnCommandTileSelectionChanged"), UOperationsCommandRoomWidget::StaticClass()},
        {TEXT("PreConstruct"), UUserWidget::StaticClass()},
    };
}

TArray<UEdGraph*> AllGraphs(UBlueprint* BP)
{
    TArray<UEdGraph*> Result;
    BP->GetAllGraphs(Result);
    return Result;
}

UEdGraph* FindGraph(UBlueprint* BP, FName Name)
{
    for (UEdGraph* Graph : AllGraphs(BP)) if (Graph && Graph->GetFName() == Name) return Graph;
    return nullptr;
}

bool HasConnections(const UEdGraphNode* Node)
{
    for (const UEdGraphPin* Pin : Node->Pins) if (Pin && !Pin->LinkedTo.IsEmpty()) return true;
    return false;
}

TArray<UK2Node_Event*> FindEvents(UBlueprint* BP, FName Name)
{
    TArray<UK2Node_Event*> Result;
    for (UEdGraph* Graph : AllGraphs(BP))
        for (UEdGraphNode* Node : Graph->Nodes)
            if (auto* Event = Cast<UK2Node_Event>(Node))
                if (!Cast<UK2Node_ComponentBoundEvent>(Event) && Event->EventReference.GetMemberName() == Name) Result.Add(Event);
    return Result;
}

TArray<UK2Node_ComponentBoundEvent*> FindButtonEvents(UBlueprint* BP, FName Button)
{
    TArray<UK2Node_ComponentBoundEvent*> Result;
    for (UEdGraph* Graph : AllGraphs(BP))
        for (UEdGraphNode* Node : Graph->Nodes)
            if (auto* Event = Cast<UK2Node_ComponentBoundEvent>(Node))
                if (Event->ComponentPropertyName == Button && Event->DelegatePropertyName == TEXT("OnClicked")) Result.Add(Event);
    return Result;
}

void Inspect(UWidgetBlueprint* BP)
{
    UE_LOG(LogTemp, Display, TEXT("OPERATIONS_WIDGET_EVENTS_ASSET %s parent=%s"), *BP->GetPathName(), *GetNameSafe(BP->ParentClass));
    for (UEdGraph* Graph : AllGraphs(BP))
    {
        UE_LOG(LogTemp, Display, TEXT("OPERATIONS_WIDGET_EVENTS_GRAPH %s nodes=%d"), *Graph->GetName(), Graph->Nodes.Num());
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            const auto* Event = Cast<UK2Node_Event>(Node);
            const auto* Button = Cast<UK2Node_ComponentBoundEvent>(Node);
            const auto* Call = Cast<UK2Node_CallFunction>(Node);
            UE_LOG(LogTemp, Display, TEXT("OPERATIONS_WIDGET_EVENTS_NODE graph=%s class=%s event=%s button=%s call=%s linked=%d"),
                *Graph->GetName(), *Node->GetClass()->GetName(), Event ? *Event->EventReference.GetMemberName().ToString() : TEXT(""),
                Button ? *Button->ComponentPropertyName.ToString() : TEXT(""),
                Call ? *Call->FunctionReference.GetMemberName().ToString() : TEXT(""), HasConnections(Node));
        }
    }
}

bool IsOwnedGraph(UEdGraph* Graph)
{
    if (!Graph) return false;
    for (UEdGraphNode* Node : Graph->Nodes)
        if (Cast<UEdGraphNode_Comment>(Node) && Node->NodeComment == Marker) return true;
    return false;
}

bool ValidateExistingMigration(UWidgetBlueprint* BP)
{
    if (!Check(IsOwnedGraph(FindGraph(BP, EventsGraphName)), TEXT("same-name event graph is not owned by this migration; preserving it"))) return false;
    if (!Check(FindGraph(BP, VisibilityFunctionName) != nullptr, TEXT("migration visibility function is missing; inspect and repair manually"))) return false;
    for (const FEventSpec& Spec : EventSpecs())
    {
        const auto Events = FindEvents(BP, Spec.Name);
        UEdGraphPin* Then = Events.Num() == 1 ? Events[0]->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr;
        if (!Check(Then && !Then->LinkedTo.IsEmpty(), FString::Printf(TEXT("event %s is missing, duplicated or disconnected; existing edits will not be overwritten"), *Spec.Name.ToString()))) return false;
    }
    for (FName Name : {FName(TEXT("NormalSpeedButton")), FName(TEXT("FastForwardButton"))})
    {
        const auto Events = FindButtonEvents(BP, Name);
        UEdGraphPin* Then = Events.Num() == 1 ? Events[0]->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr;
        if (!Check(Then && !Then->LinkedTo.IsEmpty(), FString::Printf(TEXT("%s click flow is missing or disconnected; preserving user edits"), *Name.ToString()))) return false;
    }
    return true;
}

bool Preflight(UWidgetBlueprint* BP, TMap<FName, UK2Node_Event*>& EmptyEvents)
{
    if (!Check(BP->ParentClass == UOperationsCommandRoomWidget::StaticClass(), TEXT("expected direct native OperationsCommandRoomWidget parent"))
        || !Check(BP->WidgetTree && BP->WidgetTree->RootWidget, TEXT("Designer layout is missing"))
        || !Check(!FindGraph(BP, EventsGraphName) && !FindGraph(BP, VisibilityFunctionName), TEXT("reserved graph name already exists; no replacement is allowed"))) return false;
    for (FName Legacy : {FName(TEXT("OnSceneUIOpened")), FName(TEXT("OnSceneUIClosed"))})
    {
        if (!Check(!FindGraph(BP, Legacy), FString::Printf(TEXT("legacy function %s exists; its lifecycle requires manual integration"), *Legacy.ToString()))) return false;
        for (UK2Node_Event* Event : FindEvents(BP, Legacy))
            if (!Check(!HasConnections(Event), FString::Printf(TEXT("legacy event %s contains user logic; review its completion timing before migration"), *Legacy.ToString()))) return false;
    }
    for (const FEventSpec& Spec : EventSpecs())
    {
        if (!Check(Spec.Owner->FindFunctionByName(Spec.Name) != nullptr, FString::Printf(TEXT("native event %s is unavailable; build the updated module first"), *Spec.Name.ToString()))) return false;
        if (!Check(!FindGraph(BP, Spec.Name), FString::Printf(TEXT("existing function graph %s must be integrated manually"), *Spec.Name.ToString()))) return false;
        const auto Events = FindEvents(BP, Spec.Name);
        if (!Check(Events.Num() <= 1, FString::Printf(TEXT("event %s is duplicated"), *Spec.Name.ToString()))) return false;
        if (!Events.IsEmpty())
        {
            if (!Check(Events[0]->bOverrideFunction && !HasConnections(Events[0]), FString::Printf(TEXT("event %s already has user logic; it will not be overwritten"), *Spec.Name.ToString()))) return false;
            EmptyEvents.Add(Spec.Name, Events[0]);
        }
    }
    for (FName Button : {FName(TEXT("NormalSpeedButton")), FName(TEXT("FastForwardButton"))})
    {
        if (!Check(Cast<USelectionButtonWidget>(BP->WidgetTree->FindWidget(Button)) != nullptr, FString::Printf(TEXT("Designer button %s is missing or has the wrong type"), *Button.ToString()))
            || !Check(FindFProperty<FObjectProperty>(BP->SkeletonGeneratedClass, Button) != nullptr, FString::Printf(TEXT("button property %s is unavailable"), *Button.ToString()))
            || !Check(FindButtonEvents(BP, Button).IsEmpty(), FString::Printf(TEXT("%s already has a click event; preserving it requires manual integration"), *Button.ToString()))) return false;
    }
    return Check(Cast<UPanelWidget>(BP->WidgetTree->FindWidget(TEXT("TileInfoPanel"))) != nullptr, TEXT("TileInfoPanel is missing or is not a panel"));
}

struct FBuilder
{
    UEdGraph* Graph;
    bool bOK = true;
    int32 X = 320;
    int32 Y = 0;

    UEdGraphPin* Pin(UEdGraphNode* Node, FName Name)
    {
        UEdGraphPin* Result = Node ? Node->FindPin(Name) : nullptr;
        if (!Check(Result != nullptr, FString::Printf(TEXT("missing pin %s.%s"), *GetNameSafe(Node), *Name.ToString()))) bOK = false;
        return Result;
    }
    void Link(UEdGraphPin* A, UEdGraphPin* B)
    {
        if (!Check(A && B && GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(A, B), TEXT("could not connect Blueprint nodes"))) bOK = false;
    }
    void Default(UEdGraphNode* Node, FName Name, const TCHAR* Value)
    {
        if (UEdGraphPin* P = Pin(Node, Name)) GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(*P, Value);
    }
    template<class T, class Setup> T* Node(Setup Configure)
    {
        FGraphNodeCreator<T> Creator(*Graph);
        T* Result = Creator.CreateNode();
        Configure(Result);
        Result->NodePosX = X;
        Result->NodePosY = Y;
        X += 300;
        Creator.Finalize();
        return Result;
    }
    UK2Node_CallFunction* Call(UClass* Owner, FName Name)
    {
        UFunction* Function = Owner ? Owner->FindFunctionByName(Name) : nullptr;
        if (Owner && !Check(Function != nullptr, FString::Printf(TEXT("missing function %s.%s"), *Owner->GetName(), *Name.ToString()))) bOK = false;
        return Node<UK2Node_CallFunction>([&](auto* N)
        {
            if (Function) N->SetFromFunction(Function);
            else N->FunctionReference.SetSelfMember(Name);
        });
    }
    UEdGraphPin* Exec(UEdGraphPin* From, UK2Node_CallFunction* To)
    {
        Link(From, To->GetExecPin());
        return To->GetThenPin();
    }
    UEdGraphPin* Invoke(UEdGraphPin* From, UClass* Owner, FName Name) { return Exec(From, Call(Owner, Name)); }
    UEdGraphPin* Parent(UEdGraphPin* From, FName Name)
    {
        UFunction* Function = UBaseSceneWidget::StaticClass()->FindFunctionByName(Name);
        auto* CallParent = Node<UK2Node_CallParentFunction>([&](auto* N) { N->SetFromFunction(Function); });
        CallParent->NodeComment = TEXT("保留基类转发的 OnSceneUIOpened / OnSceneUIClosed 扩展入口。");
        return Exec(From, CallParent);
    }
    UEdGraphPin* Get(FName Name)
    {
        auto* N = Node<UK2Node_VariableGet>([&](auto* V) { V->VariableReference.SetSelfMember(Name); });
        return Pin(N, Name);
    }
    UEdGraphPin* Enabled(UEdGraphPin* From, bool bEnabled)
    {
        auto* N = Call(UWidget::StaticClass(), TEXT("SetIsEnabled"));
        Default(N, TEXT("bInIsEnabled"), bEnabled ? TEXT("true") : TEXT("false"));
        return Exec(From, N);
    }
};

bool BuildVisibilityFunction(UWidgetBlueprint* BP)
{
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(BP, VisibilityFunctionName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph(BP, Graph, true, static_cast<UClass*>(nullptr));
    UK2Node_FunctionEntry* Entry = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes) if (auto* Found = Cast<UK2Node_FunctionEntry>(Node)) Entry = Found;
    if (!Check(Entry != nullptr, TEXT("visibility function entry was not created"))) return false;
    Entry->MetaData.Category = FText::FromString(TEXT("作战指挥室|地图显示"));
    Entry->NodeComment = TEXT("由加载和选中变化事件显式调用。这里可继续添加瓦片信息、动画；正文容器保持空白。");
    FBuilder G{Graph};
    G.Y = 200;
    UEdGraphPin* Panel = G.Get(TEXT("TileInfoPanel"));
    auto* Valid = G.Call(UKismetSystemLibrary::StaticClass(), TEXT("IsValid"));
    G.Link(Panel, G.Pin(Valid, TEXT("Object")));
    G.X = 320; G.Y = 0;
    auto* ValidBranch = G.Node<UK2Node_IfThenElse>([](auto*) {});
    G.Link(G.Pin(Entry, UEdGraphSchema_K2::PN_Then), ValidBranch->GetExecPin());
    G.Link(Valid->GetReturnValuePin(), ValidBranch->GetConditionPin());
    G.Y = 250;
    auto* Selected = G.Call(UOperationsCommandRoomWidget::StaticClass(), TEXT("HasSelectedTile"));
    G.X = 700; G.Y = 0;
    auto* SelectionBranch = G.Node<UK2Node_IfThenElse>([](auto*) {});
    G.Link(ValidBranch->GetThenPin(), SelectionBranch->GetExecPin());
    G.Link(Selected->GetReturnValuePin(), SelectionBranch->GetConditionPin());
    G.X = 1040; G.Y = -80;
    auto* Show = G.Call(UWidget::StaticClass(), TEXT("SetVisibility"));
    G.Link(Panel, G.Pin(Show, UEdGraphSchema_K2::PN_Self));
    G.Default(Show, TEXT("InVisibility"), TEXT("SelfHitTestInvisible"));
    G.Exec(SelectionBranch->GetThenPin(), Show);
    G.X = 1040; G.Y = 160;
    auto* Hide = G.Call(UWidget::StaticClass(), TEXT("SetVisibility"));
    G.Link(Panel, G.Pin(Hide, UEdGraphSchema_K2::PN_Self));
    G.Default(Hide, TEXT("InVisibility"), TEXT("Collapsed"));
    G.Exec(SelectionBranch->GetElsePin(), Hide);
    return G.bOK;
}

UK2Node_Event* CreateEvent(FBuilder& G, const FEventSpec& Spec, const TMap<FName, UK2Node_Event*>& EmptyEvents)
{
    if (UK2Node_Event* const* Existing = EmptyEvents.Find(Spec.Name))
    {
        G.Graph = (*Existing)->GetGraph();
        G.X = (*Existing)->NodePosX + 320;
        G.Y = (*Existing)->NodePosY;
        return *Existing;
    }
    G.X = 0;
    return G.Node<UK2Node_Event>([&](auto* N)
    {
        N->EventReference.SetExternalMember(Spec.Name, Spec.Owner);
        N->bOverrideFunction = true;
    });
}

bool BuildEventFlows(UWidgetBlueprint* BP, const TMap<FName, UK2Node_Event*>& EmptyEvents)
{
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(BP, EventsGraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(BP, Graph);
    auto* Comment = NewObject<UEdGraphNode_Comment>(Graph);
    Graph->AddNode(Comment, false, false);
    Comment->CreateNewGuid();
    Comment->NodeComment = Marker;
    Comment->NodePosX = 0; Comment->NodePosY = -300; Comment->NodeWidth = 2300; Comment->NodeHeight = 160;
    Comment->CommentColor = FLinearColor(0.025f, 0.12f, 0.18f, 1.0f);

    int32 Row = 0;
    for (const FEventSpec& Spec : EventSpecs())
    {
        FBuilder G{Graph}; G.Y = Row; Row += 520;
        UK2Node_Event* Event = CreateEvent(G, Spec, EmptyEvents);
        UEdGraphPin* Tail = G.Pin(Event, UEdGraphSchema_K2::PN_Then);
        if (Spec.Name == TEXT("LoadSceneUI"))
        {
            Tail = G.Invoke(Tail, UOperationsCommandRoomWidget::StaticClass(), TEXT("PrepareCommandRoomUI"));
            Tail = G.Enabled(Tail, true);
            Tail = G.Invoke(Tail, UOperationsCommandRoomWidget::StaticClass(), TEXT("RefreshTimeDisplay"));
            Tail = G.Invoke(Tail, UOperationsCommandRoomWidget::StaticClass(), TEXT("RefreshTimeControlState"));
            Tail = G.Invoke(Tail, nullptr, VisibilityFunctionName);
            Tail = G.Parent(Tail, Spec.Name);
            auto* Complete = G.Call(UBaseSceneWidget::StaticClass(), TEXT("NotifyLoadCompleted"));
            Complete->NodeComment = TEXT("加载完成。可在此节点之前加入进入动画，并在动画结束后继续执行。");
            G.Exec(Tail, Complete);
        }
        else if (Spec.Name == TEXT("UnloadSceneUI"))
        {
            Tail = G.Invoke(Tail, UOperationsCommandRoomWidget::StaticClass(), TEXT("ReleaseCommandRoomUI"));
            Tail = G.Enabled(Tail, false);
            Tail = G.Invoke(Tail, nullptr, VisibilityFunctionName);
            Tail = G.Parent(Tail, Spec.Name);
            auto* Complete = G.Call(UBaseSceneWidget::StaticClass(), TEXT("NotifyUnloadCompleted"));
            Complete->NodeComment = TEXT("卸载完成。可在此节点之前加入退出动画；数据订阅已经安全释放。");
            G.Exec(Tail, Complete);
        }
        else if (Spec.Name == TEXT("OnCommandTimeChanged")) G.Invoke(Tail, UOperationsCommandRoomWidget::StaticClass(), TEXT("RefreshTimeDisplay"));
        else if (Spec.Name == TEXT("OnCommandTimeScaleChanged") || Spec.Name == TEXT("OnCommandTimePausedChanged")) G.Invoke(Tail, UOperationsCommandRoomWidget::StaticClass(), TEXT("RefreshTimeControlState"));
        else if (Spec.Name == TEXT("OnCommandTileSelectionChanged")) G.Invoke(Tail, nullptr, VisibilityFunctionName);
        else if (Spec.Name == TEXT("PreConstruct")) G.Invoke(Tail, UOperationsCommandRoomWidget::StaticClass(), TEXT("RefreshSpeedLabels"));
        if (!G.bOK) return false;
    }
    auto* ClickDelegate = FindFProperty<FMulticastDelegateProperty>(UBasicButtonWidget::StaticClass(), TEXT("OnClicked"));
    if (!Check(ClickDelegate != nullptr, TEXT("button OnClicked delegate is unavailable"))) return false;
    for (const auto& Pair : TArray<TPair<FName, FName>>{{TEXT("NormalSpeedButton"), TEXT("NormalSpeed")}, {TEXT("FastForwardButton"), TEXT("FastForward")}})
    {
        FBuilder G{Graph}; G.X = 0; G.Y = Row; Row += 420;
        auto* Property = FindFProperty<FObjectProperty>(BP->SkeletonGeneratedClass, Pair.Key);
        auto* Event = G.Node<UK2Node_ComponentBoundEvent>([&](auto* N) { N->InitializeComponentBoundEventParams(Property, ClickDelegate); });
        G.Invoke(G.Pin(Event, UEdGraphSchema_K2::PN_Then), UOperationsCommandRoomWidget::StaticClass(), Pair.Value);
        if (!G.bOK) return false;
    }
    return true;
}

FString NodeSignature(const UEdGraphNode* Node)
{
    FString Result = Node->NodeGuid.ToString() + Node->GetClass()->GetPathName() + Node->NodeComment;
    Result += FString::Printf(TEXT("|%d,%d"), Node->NodePosX, Node->NodePosY);
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin) continue;
        Result += TEXT("|") + Pin->PinName.ToString() + TEXT("=") + Pin->DefaultValue + TEXT("/") + Pin->DefaultTextValue.ToString();
        Result += GetPathNameSafe(Pin->DefaultObject);
        TArray<FString> Links;
        for (const UEdGraphPin* Other : Pin->LinkedTo)
            Links.Add(Other->GetOwningNode()->NodeGuid.ToString() + TEXT(".") + Other->PinName.ToString());
        Links.Sort();
        for (const FString& Link : Links) Result += TEXT("->") + Link;
    }
    return Result;
}

bool CreateLifecycleFixture(UWidgetBlueprint* Source)
{
    if (!ValidateExistingMigration(Source)) return false;
    const FString Mount = TEXT("/OperationsTest/");
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("OperationsCommandRoomBlueprintEvents/Fixtures/"));
    if (!Check(!FPackageName::MountPointExists(Mount), TEXT("test mount already exists; refusing to replace it"))
        || !Check(IFileManager::Get().MakeDirectory(*Directory, true), TEXT("could not create fixture directory"))) return false;
    FPackageName::RegisterMountPoint(Mount, Directory);
    ON_SCOPE_EXIT { FPackageName::UnRegisterMountPoint(Mount, Directory); };
    UPackage* Package = CreatePackage(TEXT("/OperationsTest/WBP_OperationsCommandRoomDelayed"));
    // DuplicateObject invokes UWidgetBlueprint::PostDuplicate, including generated-class and binding rebasing.
    auto* Fixture = DuplicateObject<UWidgetBlueprint>(Source, Package, TEXT("WBP_OperationsCommandRoomDelayed"));
    if (!Check(Fixture && Fixture->WidgetTree && Fixture->WidgetTree != Source->WidgetTree, TEXT("could not independently duplicate Widget Blueprint"))) return false;
    Fixture->SetFlags(RF_Public | RF_Standalone);
    const auto Graphs = AllGraphs(Fixture);
    UWidgetTree* Tree = Fixture->WidgetTree;
    const auto Animations = Fixture->Animations;
    TMap<FName, UK2Node_CallFunction*> Completion;
    TSet<UEdGraphNode*> ChangedNodes;
    TMap<UEdGraphNode*, FString> PreservedNodes;
    for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
    {
        PreservedNodes.Add(Node, NodeSignature(Node));
        auto* Call = Cast<UK2Node_CallFunction>(Node);
        const FName Name = Call ? Call->FunctionReference.GetMemberName() : NAME_None;
        if (Name != TEXT("NotifyLoadCompleted") && Name != TEXT("NotifyUnloadCompleted")) continue;
        if (!Check(!Completion.Contains(Name) && Call->GetExecPin() && Call->GetExecPin()->LinkedTo.Num() == 1,
            TEXT("expected exactly one connected call for each completion notification"))) return false;
        Completion.Add(Name, Call);
        ChangedNodes.Add(Call);
        ChangedNodes.Add(Call->GetExecPin()->LinkedTo[0]->GetOwningNode());
    }
    if (!Check(Completion.Num() == 2, TEXT("both completion calls must exist in the real Blueprint copy"))) return false;
    for (const auto& Pair : Completion) Pair.Value->GetExecPin()->BreakAllPinLinks();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Fixture);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(Fixture, EBlueprintCompileOptions::None, &Results);
    if (!Check(Results.NumErrors == 0 && Fixture->Status != BS_Error && Fixture->GeneratedClass
        && Fixture->GeneratedClass->GetOutermost() == Package, TEXT("delayed fixture compilation failed"))) return false;
    if (!Check(Fixture->WidgetTree == Tree && Fixture->Animations == Animations && AllGraphs(Fixture) == Graphs,
        TEXT("fixture Designer, animations or graph collection changed unexpectedly"))) return false;
    for (const auto& Pair : PreservedNodes)
        if (!Check(Pair.Key->GetGraph()->Nodes.Contains(Pair.Key) && (ChangedNodes.Contains(Pair.Key) || NodeSignature(Pair.Key) == Pair.Value),
            TEXT("fixture changed an unrelated graph node"))) return false;
    for (const auto& Pair : Completion)
        if (!Check(Pair.Value->GetExecPin()->LinkedTo.IsEmpty(), TEXT("fixture completion input is still connected"))) return false;
    const FString Filename = Directory / TEXT("WBP_OperationsCommandRoomDelayed.uasset");
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
    if (!Check(UPackage::SavePackage(Package, Fixture, *Filename, Args), TEXT("could not save delayed fixture"))) return false;
    UE_LOG(LogTemp, Display, TEXT("OPERATIONS_WIDGET_LIFECYCLE_FIXTURE_OK disconnected=2 mount=%s directory=%s class=%s file=%s"),
        *Mount, *Directory, *Fixture->GeneratedClass->GetPathName(), *Filename);
    return true;
}
}

int32 UOperationsCommandRoomWidgetEventsCommandlet::Main(const FString& Params)
{
    using namespace OperationsWidgetEvents;
    const bool bApply = FParse::Param(*Params, TEXT("Apply"));
    const bool bInspect = FParse::Param(*Params, TEXT("Inspect"));
    const bool bCreateFixture = FParse::Param(*Params, TEXT("CreateLifecycleFixture"));
    if (!Check(int32(bApply) + int32(bInspect) + int32(bCreateFixture) == 1, TEXT("choose exactly one of -Inspect, -Apply or -CreateLifecycleFixture"))) return 1;
    auto* BP = LoadObject<UWidgetBlueprint>(nullptr, AssetPath);
    if (!Check(BP != nullptr, TEXT("command-room Widget Blueprint could not be loaded"))) return 1;
    Inspect(BP);
    if (bCreateFixture) return CreateLifecycleFixture(BP) ? 0 : 1;
    if (!bApply)
    {
        TMap<FName, UK2Node_Event*> EmptyEvents;
        const bool bReady = FindGraph(BP, EventsGraphName) ? ValidateExistingMigration(BP) : Preflight(BP, EmptyEvents);
        UE_LOG(LogTemp, Display, TEXT("OPERATIONS_WIDGET_EVENTS_INSPECT_READY ready=%d (read only)"), bReady);
        return bReady ? 0 : 1;
    }
    if (FindGraph(BP, EventsGraphName))
    {
        if (!ValidateExistingMigration(BP)) return 1;
        UE_LOG(LogTemp, Display, TEXT("OPERATIONS_WIDGET_EVENTS_ALREADY_APPLIED Existing graphs preserved; no package saved."));
        return 0;
    }
    TMap<FName, UK2Node_Event*> EmptyEvents;
    if (!Preflight(BP, EmptyEvents)) return 1;

    // Back up the exact on-disk package before the first authored mutation.
    const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    const FString Backup = FPaths::ProjectSavedDir() / TEXT("OperationsCommandRoomBlueprintEvents/Backups") /
        (FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)) / FPaths::GetCleanFilename(Filename);
    if (!Check(IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true)
        && IFileManager::Get().Copy(*Backup, *Filename) == COPY_OK, TEXT("could not back up the original Widget Blueprint"))) return 1;

    UWidgetTree* OriginalTree = BP->WidgetTree;
    const auto OriginalAnimations = BP->Animations;
    const auto OriginalGraphs = AllGraphs(BP);
    TMap<UEdGraphNode*, FString> OriginalNodes;
    TSet<UEdGraphNode*> AdoptedEvents;
    for (const auto& Pair : EmptyEvents) AdoptedEvents.Add(Pair.Value);
    for (UEdGraph* Graph : OriginalGraphs)
        for (UEdGraphNode* Node : Graph->Nodes) OriginalNodes.Add(Node, NodeSignature(Node));

    BP->Modify();
    if (!BuildVisibilityFunction(BP)) return 1;
    // Populate the skeleton signature before creating calls to the new Blueprint function.
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    if (!BuildEventFlows(BP, EmptyEvents)) return 1;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
    if (!Check(Results.NumErrors == 0 && BP->Status != BS_Error, TEXT("new event flows did not compile; original asset remains unchanged on disk"))) return 1;
    if (!Check(BP->WidgetTree == OriginalTree && BP->Animations == OriginalAnimations, TEXT("Designer or animation identity changed unexpectedly; refusing to save"))) return 1;
    const auto CurrentGraphs = AllGraphs(BP);
    for (UEdGraph* Graph : OriginalGraphs)
        if (!Check(CurrentGraphs.Contains(Graph), TEXT("an original graph disappeared; refusing to save"))) return 1;
    for (const auto& Pair : OriginalNodes)
    {
        if (!Check(Pair.Key->GetGraph()->Nodes.Contains(Pair.Key), TEXT("an original node disappeared; refusing to save"))) return 1;
        if (!AdoptedEvents.Contains(Pair.Key) && !Check(NodeSignature(Pair.Key) == Pair.Value, TEXT("an unrelated original node changed; refusing to save"))) return 1;
    }
    if (!ValidateExistingMigration(BP)) return 1;
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    BP->MarkPackageDirty();
    if (!Check(UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args), TEXT("could not save migrated Widget Blueprint"))) return 1;
    Inspect(BP);
    UE_LOG(LogTemp, Display, TEXT("OPERATIONS_WIDGET_EVENTS_OK backup=%s"), *Backup);
    return 0;
}
