#include "SquadSceneFlowCommandlet.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "HAL/FileManager.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/BaseSceneWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/SquadMeetingRoomWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

USquadSceneFlowCommandlet::USquadSceneFlowCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

namespace SquadSceneFlow
{
constexpr const TCHAR* ScenePath = TEXT("/Game/System/Map/BaseMap/Scene/BP_小队会议室_Scene");
constexpr const TCHAR* ManagerPath = TEXT("/Game/System/SubSystem/SceneSystem/BP_SceneManager");
constexpr const TCHAR* AboveCallback = TEXT("相机移动到场景上方后，开始进入场景");
constexpr const TCHAR* CameraInCallback = TEXT("进入相机移动完成");
constexpr const TCHAR* DeferredEvent = TEXT("SquadFlow_DeferredLeaveCompleted");

bool bGraphOK = true;
const UEdGraphSchema_K2* Schema() { return GetDefault<UEdGraphSchema_K2>(); }
UEdGraphPin* Pin(UEdGraphNode* Node, FName Name)
{
    auto* Result = Node ? Node->FindPin(Name) : nullptr;
    if (!Result)
    {
        bGraphOK = false;
        UE_LOG(LogTemp, Error, TEXT("SQUAD_FLOW_MISSING_PIN node=%s pin=%s"), *GetNameSafe(Node), *Name.ToString());
    }
    return Result;
}
void Link(UEdGraphPin* A, UEdGraphPin* B)
{
    if (!A || !B || !Schema()->TryCreateConnection(A, B))
    {
        bGraphOK = false;
        UE_LOG(LogTemp, Error, TEXT("SQUAD_FLOW_LINK_FAILED %s.%s -> %s.%s"),
            A ? *A->GetOwningNode()->GetName() : TEXT("null"), A ? *A->PinName.ToString() : TEXT("null"),
            B ? *B->GetOwningNode()->GetName() : TEXT("null"), B ? *B->PinName.ToString() : TEXT("null"));
    }
}
void Default(UEdGraphNode* Node, FName Name, const FString& Value)
{
    if (auto* P = Pin(Node, Name)) Schema()->TrySetDefaultValue(*P, Value);
}

struct FGraph
{
    UBlueprint* BP;
    UEdGraph* Graph;
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    int32 NextX = 300;
    int32 RowY = 0;

    FGraph(UBlueprint* InBP, UEdGraph* InGraph) : BP(InBP), Graph(InGraph)
    {
        for (UEdGraphNode* N : Graph->Nodes)
        {
            if (auto* E = Cast<UK2Node_FunctionEntry>(N)) Entry = E;
            if (auto* R = Cast<UK2Node_FunctionResult>(N)) Result = R;
        }
        if (Entry) { Entry->NodePosX = 0; Entry->NodePosY = 0; Entry->BreakAllNodeLinks(); }
        if (Result)
        {
            Result->BreakAllNodeLinks(); Result->NodePosX = 3500;
            if (Result->FindPin(TEXT("ReturnValue"))) Default(Result, TEXT("ReturnValue"), TEXT("false"));
        }
    }

    template<class T> T* Node(TFunctionRef<void(T*)> Setup)
    {
        FGraphNodeCreator<T> Creator(*Graph);
        auto* N = Creator.CreateNode();
        Setup(N);
        N->NodePosX = NextX;
        N->NodePosY = RowY;
        NextX += 340;
        Creator.Finalize();
        return N;
    }
    void Row(int32 Y) { NextX = 300; RowY = Y; }
    UEdGraphPin* Start() { return Pin(Entry, UEdGraphSchema_K2::PN_Then); }
    UEdGraphPin* Exec(UEdGraphPin* From, UEdGraphNode* To)
    {
        Link(From, Pin(To, UEdGraphSchema_K2::PN_Execute));
        return Pin(To, UEdGraphSchema_K2::PN_Then);
    }
    UK2Node_CallFunction* Call(UClass* Owner, FName Function)
    {
        auto* F = Owner ? Owner->FindFunctionByName(Function) : nullptr;
        if (!F)
        {
            bGraphOK = false;
            UE_LOG(LogTemp, Error, TEXT("SQUAD_FLOW_MISSING_FUNCTION %s.%s"), *GetNameSafe(Owner), *Function.ToString());
        }
        return Node<UK2Node_CallFunction>([&](UK2Node_CallFunction* N) { if (F) N->SetFromFunction(F); });
    }
    UK2Node_CallFunction* SelfCall(FName Name)
    {
        return Node<UK2Node_CallFunction>([&](UK2Node_CallFunction* N) { N->FunctionReference.SetSelfMember(Name); });
    }
    UEdGraphPin* Invoke(UEdGraphPin* From, FName Name) { return Exec(From, SelfCall(Name)); }
    UK2Node_VariableGet* Get(FName Name, UClass* Owner = nullptr, UEdGraphPin* Target = nullptr)
    {
        auto* N = Node<UK2Node_VariableGet>([&](UK2Node_VariableGet* V)
        {
            if (Owner) V->VariableReference.SetExternalMember(Name, Owner);
            else V->VariableReference.SetSelfMember(Name);
        });
        if (Target) Link(Target, Pin(N, UEdGraphSchema_K2::PN_Self));
        return N;
    }
    UEdGraphPin* Value(FName Name) { return Pin(Get(Name), Name); }
    UEdGraphPin* Set(UEdGraphPin* From, FName Name, const FString& Literal, UEdGraphPin* ValuePin = nullptr)
    {
        auto* N = Node<UK2Node_VariableSet>([&](UK2Node_VariableSet* V) { V->VariableReference.SetSelfMember(Name); });
        if (ValuePin) Link(ValuePin, Pin(N, Name)); else Default(N, Name, Literal);
        return Exec(From, N);
    }
    UK2Node_IfThenElse* Branch(UEdGraphPin* From, UEdGraphPin* Condition)
    {
        auto* N = Node<UK2Node_IfThenElse>([](UK2Node_IfThenElse*) {});
        Exec(From, N); Link(Condition, Pin(N, TEXT("Condition"))); return N;
    }
    UEdGraphPin* StageIs(int32 Stage)
    {
        auto* Equal = Call(UKismetMathLibrary::StaticClass(), TEXT("EqualEqual_IntInt"));
        Link(Value(TEXT("FlowStage")), Pin(Equal, TEXT("A")));
        Default(Equal, TEXT("B"), FString::FromInt(Stage));
        return Pin(Equal, TEXT("ReturnValue"));
    }
    UEdGraphPin* Both(UEdGraphPin* A, UEdGraphPin* B)
    {
        auto* N = Call(UKismetMathLibrary::StaticClass(), TEXT("BooleanAND"));
        Link(A, Pin(N, TEXT("A"))); Link(B, Pin(N, TEXT("B"))); return Pin(N, TEXT("ReturnValue"));
    }
    UEdGraphPin* IsValid(UEdGraphPin* Object)
    {
        auto* N = Call(UKismetSystemLibrary::StaticClass(), TEXT("IsValid"));
        Link(Object, Pin(N, TEXT("Object"))); return Pin(N, TEXT("ReturnValue"));
    }
    UK2Node_DynamicCast* CastTo(UClass* Target, UEdGraphPin* Object, UEdGraphPin* From)
    {
        auto* N = Node<UK2Node_DynamicCast>([&](UK2Node_DynamicCast* C) { C->TargetType = Target; });
        Link(Object, N->GetCastSourcePin()); Exec(From, N); return N;
    }
    UK2Node_ExecutionSequence* Sequence(UEdGraphPin* From, int32 Count)
    {
        auto* N = Node<UK2Node_ExecutionSequence>([](UK2Node_ExecutionSequence*) {});
        for (int32 Index = 2; Index < Count; ++Index) N->AddInputPin();
        Link(From, Pin(N, UEdGraphSchema_K2::PN_Execute)); return N;
    }
    UK2Node_CreateDelegate* Delegate(FName Function)
    {
        return Node<UK2Node_CreateDelegate>([&](UK2Node_CreateDelegate* N) { N->SetFunction(Function); });
    }
    UEdGraphPin* Bind(UEdGraphPin* From, FName EventName, FName Callback, bool bRemove = false)
    {
        UK2Node_BaseMCDelegate* N = bRemove
            ? static_cast<UK2Node_BaseMCDelegate*>(Node<UK2Node_RemoveDelegate>([&](UK2Node_RemoveDelegate* D)
                { D->DelegateReference.SetExternalMember(EventName, UBaseSceneWidget::StaticClass()); }))
            : static_cast<UK2Node_BaseMCDelegate*>(Node<UK2Node_AddDelegate>([&](UK2Node_AddDelegate* D)
                { D->DelegateReference.SetExternalMember(EventName, UBaseSceneWidget::StaticClass()); }));
        Link(Value(TEXT("RoomUI")), Pin(N, UEdGraphSchema_K2::PN_Self));
        auto* D = Delegate(Callback);
        Link(D->GetDelegateOutPin(), N->GetDelegatePin());
        D->HandleAnyChangeWithoutNotifying();
        return Exec(From, N);
    }
    UEdGraphPin* Error(UEdGraphPin* From, const TCHAR* Message)
    {
        auto* AfterSet = Set(From, TEXT("LastFlowError"), Message);
        auto* Print = Call(UKismetSystemLibrary::StaticClass(), TEXT("PrintString"));
        Default(Print, TEXT("InString"), FString(TEXT("[小队会议室] ")) + Message);
        Default(Print, TEXT("bPrintToScreen"), TEXT("false"));
        Default(Print, TEXT("bPrintToLog"), TEXT("true"));
        return Exec(AfterSet, Print);
    }
    void Return(UEdGraphPin* From)
    {
        if (Result) Link(From, Pin(Result, UEdGraphSchema_K2::PN_Execute));
    }
    void Comment(const TCHAR* Text)
    {
        auto* C = NewObject<UEdGraphNode_Comment>(Graph);
        Graph->AddNode(C, false, false); C->CreateNewGuid(); C->NodeComment = Text;
        C->NodePosX = -80; C->NodePosY = -230; C->NodeWidth = 1800; C->NodeHeight = 160;
        C->CommentColor = FLinearColor(.025f, .12f, .18f, 1);
    }
};

UEdGraph* FindGraph(UBlueprint* BP, FName Name)
{
    for (UEdGraph* G : BP->FunctionGraphs) if (G && G->GetFName() == Name) return G;
    return nullptr;
}
FString CameraDefault(UBlueprint* BP, FName GraphName, FName LocalName, FName ExistingMember)
{
    // Repeat runs preserve the user's already-promoted camera settings.
    if (const FStructProperty* P = FindFProperty<FStructProperty>(BP->GeneratedClass, ExistingMember))
    {
        FString Value;
        P->ExportText_InContainer(0, Value, BP->GeneratedClass->GetDefaultObject(), nullptr, nullptr, PPF_None);
        if (!Value.IsEmpty()) return Value;
    }
    if (auto* G = FindGraph(BP, GraphName))
        for (UEdGraphNode* N : G->Nodes)
            if (auto* E = Cast<UK2Node_FunctionEntry>(N))
                for (const FBPVariableDescription& V : E->LocalVariables)
                    if (V.VarName == LocalName) return V.DefaultValue;
    return FString();
}
void Member(UBlueprint* BP, FName Name, FName Type, UObject* Subtype, const FString& Value,
    const TCHAR* Display, bool bEditable)
{
    FEdGraphPinType PinType;
    PinType.PinCategory = Type; PinType.PinSubCategoryObject = Subtype;
    if (!BP->NewVariables.ContainsByPredicate([&](const FBPVariableDescription& V) { return V.VarName == Name; }))
        FBlueprintEditorUtils::AddMemberVariable(BP, Name, PinType, Value);
    for (FBPVariableDescription& V : BP->NewVariables)
    {
        if (V.VarName != Name) continue;
        V.Category = FText::FromString(bEditable ? TEXT("小队会议室|配置") : TEXT("小队会议室|流程状态"));
        V.PropertyFlags |= CPF_BlueprintVisible;
        if (bEditable) V.PropertyFlags |= CPF_Edit;
        else V.PropertyFlags = (V.PropertyFlags | CPF_Transient) & ~(CPF_Edit | CPF_BlueprintReadOnly);
        // State is reset in Enter/Leave. Camera defaults are explicitly copied from the existing room only.
        if (Name == TEXT("OverheadCameraState") || Name == TEXT("InteriorCameraState")) V.DefaultValue = Value;
    }
    FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, Name, nullptr, TEXT("DisplayName"), Display);
}
UEdGraph* PrepareFunction(UBlueprint* BP, FName Name, UClass* Signature = nullptr)
{
    auto* Graph = FindGraph(BP, Name);
    if (!Graph)
    {
        Graph = FBlueprintEditorUtils::CreateNewGraph(BP, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph(BP, Graph, Signature == nullptr, Signature);
    }
    const TArray<TObjectPtr<UEdGraphNode>> OldNodes = Graph->Nodes;
    for (UEdGraphNode* N : OldNodes)
    {
        if (auto* Entry = Cast<UK2Node_FunctionEntry>(N))
        {
            Entry->BreakAllNodeLinks();
            Entry->LocalVariables.Reset();
            Entry->MetaData.Category = FText::FromString(TEXT("小队会议室|进入退出"));
        }
        else if (Cast<UK2Node_FunctionResult>(N)) N->BreakAllNodeLinks();
        else { N->BreakAllNodeLinks(); Graph->RemoveNode(N); }
    }
    FGraph G(BP, Graph);
    if (G.Result) G.Return(G.Start());
    return Graph;
}
void CameraMove(FGraph& G, UEdGraphPin* From, FName StateName, FName Callback, const TCHAR* Failure, bool bReturn)
{
    auto* Move = G.Call(UFCS_FreeCameraBlueprintLibrary::StaticClass(), TEXT("MoveFreeCameraToState"));
    Link(G.Value(StateName), Pin(Move, TEXT("CameraState")));
    auto* D = G.Delegate(Callback);
    Link(D->GetDelegateOutPin(), Pin(Move, TEXT("MoveFinished"))); D->HandleAnyChangeWithoutNotifying();
    auto* Branch = G.Branch(G.Exec(From, Move), Pin(Move, TEXT("ReturnValue")));
    auto* Failed = G.Invoke(G.Error(Pin(Branch, TEXT("else")), Failure), Callback);
    if (bReturn) { G.Return(Pin(Branch, TEXT("then"))); G.Return(Failed); }
}

void Inspect(UBlueprint* BP)
{
    for (const FBPVariableDescription& V : BP->NewVariables)
        UE_LOG(LogTemp, Display, TEXT("SQUAD_FLOW_VARIABLE %s default=%s"), *V.VarName.ToString(), *V.DefaultValue);
    TArray<UEdGraph*> Graphs = BP->FunctionGraphs;
    Graphs.Append(BP->UbergraphPages);
    for (UEdGraph* G : Graphs)
        for (UEdGraphNode* N : G->Nodes)
        {
            UE_LOG(LogTemp, Display, TEXT("SQUAD_FLOW_NODE [%s] %s %s"), *G->GetName(), *N->GetName(), *N->GetNodeTitle(ENodeTitleType::ListView).ToString());
            for (UEdGraphPin* P : N->Pins)
            {
                FString Links;
                for (UEdGraphPin* Other : P->LinkedTo) Links += Other->GetOwningNode()->GetName() + TEXT(".") + Other->PinName.ToString() + TEXT(" ");
                UE_LOG(LogTemp, Display, TEXT("SQUAD_FLOW_PIN %s default=%s links=%s"), *P->PinName.ToString(), *P->DefaultValue, *Links);
            }
        }
}

bool Build(UBlueprint* BP)
{
    bGraphOK = true;
    UClass* SceneClass = BP->ParentClass;
    UClass* Library = LoadClass<UObject>(nullptr, TEXT("/Script/SceneManagementSystem.SMS_SceneLibrary"));
    auto* ManagerBP = LoadObject<UBlueprint>(nullptr, ManagerPath);
    if (!Library || !ManagerBP || !ManagerBP->GeneratedClass) return false;
    const FString Above = CameraDefault(BP, TEXT("EnterScene"), TEXT("移动到区域上方"), TEXT("OverheadCameraState"));
    const FString Interior = CameraDefault(BP, AboveCallback, TEXT("相机房间中位置"), TEXT("InteriorCameraState"));
    if (Above.IsEmpty() || Interior.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("SQUAD_FLOW_CAMERA_DEFAULT_MISSING: preserving asset rather than inventing room camera transforms."));
        return false;
    }
    UE_LOG(LogTemp, Display, TEXT("SQUAD_FLOW_PRESERVED_OVERHEAD %s"), *Above);
    UE_LOG(LogTemp, Display, TEXT("SQUAD_FLOW_PRESERVED_INTERIOR %s"), *Interior);
    const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    const FString Backup = FPaths::ProjectSavedDir() / TEXT("SquadSceneFlowBackups") /
        FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) / FPaths::GetCleanFilename(Filename);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
    if (IFileManager::Get().Copy(*Backup, *Filename) != COPY_OK) return false;
    UE_LOG(LogTemp, Display, TEXT("SQUAD_FLOW_BACKUP %s"), *Backup);

    Member(BP, TEXT("CurrentTileId"), UEdGraphSchema_K2::PC_Name, nullptr, TEXT("T5"), TEXT("当前战略瓦片ID"), true);
    Member(BP, TEXT("OverheadCameraState"), UEdGraphSchema_K2::PC_Struct, FFCS_CameraState::StaticStruct(), Above, TEXT("区域上方相机状态"), true);
    Member(BP, TEXT("InteriorCameraState"), UEdGraphSchema_K2::PC_Struct, FFCS_CameraState::StaticStruct(), Interior, TEXT("房间内部相机状态"), true);
    Member(BP, TEXT("BaseUI"), UEdGraphSchema_K2::PC_Object, UBaseMapWidget::StaticClass(), TEXT("None"), TEXT("基地UI缓存"), false);
    Member(BP, TEXT("RoomUI"), UEdGraphSchema_K2::PC_Object, USquadMeetingRoomWidget::StaticClass(), TEXT("None"), TEXT("小队会议室UI缓存"), false);
    Member(BP, TEXT("UiFinish"), UEdGraphSchema_K2::PC_Boolean, nullptr, TEXT("false"), TEXT("UI过渡已完成"), false);
    Member(BP, TEXT("CameraFinish"), UEdGraphSchema_K2::PC_Boolean, nullptr, TEXT("false"), TEXT("相机过渡已完成"), false);
    Member(BP, TEXT("EntryRoomStarted"), UEdGraphSchema_K2::PC_Boolean, nullptr, TEXT("false"), TEXT("房间进入流程已启动"), false);
    Member(BP, TEXT("FlowStage"), UEdGraphSchema_K2::PC_Int, nullptr, TEXT("0"), TEXT("阶段：0未开始/1进入/2活动/3退出/4结束"), false);
    Member(BP, TEXT("LastFlowError"), UEdGraphSchema_K2::PC_String, nullptr, TEXT(""), TEXT("最近流程异常"), false);

    TMap<FName, UEdGraph*> Functions;
    const TArray<FName> Names = {
        TEXT("EnterScene"), TEXT("LeaveScene"), AboveCallback, CameraInCallback,
        TEXT("SquadFlow_ResolveBaseUI"), TEXT("SquadFlow_HideBackButton"), TEXT("SquadFlow_ShowBackButton"),
        TEXT("SquadFlow_CreateRoomUI"), TEXT("SquadFlow_HideRegionBlocks"),
        TEXT("SquadFlow_UILoadCompleted"), TEXT("SquadFlow_UIUnloadCompleted"),
        TEXT("SquadFlow_CameraLeaveCompleted"), TEXT("SquadFlow_TryFinishEnter"), TEXT("SquadFlow_TryFinishLeave"),
        TEXT("SquadFlow_UnloadOwnUI"), TEXT("SquadFlow_UnbindUI") };
    for (FName Name : Names)
        Functions.Add(Name, PrepareFunction(BP, Name, Name == TEXT("EnterScene") || Name == TEXT("LeaveScene") ? SceneClass : nullptr));

    // A separate, owned event page makes the final notification explicitly latent and editable.
    UEdGraph* EventGraph = nullptr;
    for (UEdGraph* G : BP->UbergraphPages) if (G->GetFName() == TEXT("SquadRoomLifecycleEvents")) EventGraph = G;
    if (!EventGraph)
    {
        EventGraph = FBlueprintEditorUtils::CreateNewGraph(BP, TEXT("SquadRoomLifecycleEvents"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(BP, EventGraph);
    }
    else
    {
        const auto Existing = EventGraph->Nodes;
        for (UEdGraphNode* N : Existing) { N->BreakAllNodeLinks(); EventGraph->RemoveNode(N); }
    }
    FGraph EventBuilder(BP, EventGraph);
    auto* CompleteEvent = EventBuilder.Node<UK2Node_CustomEvent>([](UK2Node_CustomEvent* N) { N->CustomFunctionName = DeferredEvent; });
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status == BS_Error) return false;

    {
        FGraph G(BP, Functions[TEXT("SquadFlow_ResolveBaseUI")]);
        G.Comment(TEXT("缓存玩家控制器的基地UI；缺少控制器或基地UI时记录异常，不让场景永久等待。"));
        auto* Start = G.Set(G.Start(), TEXT("BaseUI"), TEXT("None"));
        auto* PC = G.Call(UGameplayStatics::StaticClass(), TEXT("GetPlayerController"));
        auto* Cast = G.CastTo(AGameMainMapPlayerController::StaticClass(), Pin(PC, TEXT("ReturnValue")), Start);
        auto* Base = G.Get(TEXT("BaseWidget"), AGameMainMapPlayerController::StaticClass(), Cast->GetCastResultPin());
        auto* Valid = G.Branch(Pin(Cast, TEXT("then")), G.IsValid(Pin(Base, TEXT("BaseWidget"))));
        G.Set(Pin(Valid, TEXT("then")), TEXT("BaseUI"), TEXT(""), Pin(Base, TEXT("BaseWidget")));
        G.Row(650); G.Error(Pin(Valid, TEXT("else")), TEXT("基地UI尚未创建，界面进入步骤将跳过。"));
        G.Row(1050); G.Error(Cast->GetInvalidCastPin(), TEXT("当前玩家控制器不是 GameMainMapPlayerController，无法获取基地UI。"));
    }
    for (bool bVisible : {false, true})
    {
        FGraph G(BP, Functions[bVisible ? TEXT("SquadFlow_ShowBackButton") : TEXT("SquadFlow_HideBackButton")]);
        G.Comment(bVisible ? TEXT("相机与UI都进入完成后，才开放返回基地。") : TEXT("进入/退出过渡期间隐藏返回按钮，避免重复请求。"));
        auto* Branch = G.Branch(G.Start(), G.IsValid(G.Value(TEXT("BaseUI"))));
        auto* SetVisible = G.Call(UBaseMapWidget::StaticClass(), TEXT("SetBackButtonVisible"));
        Link(G.Value(TEXT("BaseUI")), Pin(SetVisible, TEXT("self")));
        Default(SetVisible, TEXT("bVisible"), bVisible ? TEXT("true") : TEXT("false"));
        G.Exec(Pin(Branch, TEXT("then")), SetVisible);
    }
    {
        FGraph G(BP, Functions[TEXT("EnterScene")]);
        G.Comment(TEXT("进入第一段：清理旧标志、缓存基地UI、隐藏返回按钮，移动到小队会议室上方。返回 false，等待相机和UI全部完成。"));
        auto* P = G.Set(G.Start(), TEXT("FlowStage"), TEXT("1"));
        P = G.Set(P, TEXT("UiFinish"), TEXT("false")); P = G.Set(P, TEXT("CameraFinish"), TEXT("false"));
        P = G.Set(P, TEXT("EntryRoomStarted"), TEXT("false")); P = G.Set(P, TEXT("LastFlowError"), TEXT(""));
        P = G.Invoke(P, TEXT("SquadFlow_UnbindUI")); P = G.Set(P, TEXT("RoomUI"), TEXT("None"));
        P = G.Invoke(P, TEXT("SquadFlow_ResolveBaseUI")); P = G.Invoke(P, TEXT("SquadFlow_HideBackButton"));
        CameraMove(G, P, TEXT("OverheadCameraState"), AboveCallback, TEXT("相机移动到区域上方未启动，继续创建小队会议室UI。"), true);
    }
    {
        FGraph G(BP, Functions[AboveCallback]);
        G.Comment(TEXT("进入第二段：仅在进入阶段执行一次。并行进入房间相机、创建并绑定UI、隐藏区域块。"));
        auto* Not = G.Call(UKismetMathLibrary::StaticClass(), TEXT("Not_PreBool"));
        Link(G.Value(TEXT("EntryRoomStarted")), Pin(Not, TEXT("A")));
        auto* Guard = G.Branch(G.Start(), G.Both(G.StageIs(1), Pin(Not, TEXT("ReturnValue"))));
        auto* Seq = G.Sequence(G.Set(Pin(Guard, TEXT("then")), TEXT("EntryRoomStarted"), TEXT("true")), 3);
        G.Row(450); CameraMove(G, Seq->GetThenPinGivenIndex(0), TEXT("InteriorCameraState"), CameraInCallback, TEXT("相机进入房间未启动，按相机已完成继续，避免卡住场景。"), false);
        G.Row(1050); G.Invoke(Seq->GetThenPinGivenIndex(1), TEXT("SquadFlow_CreateRoomUI"));
        G.Row(1400); G.Invoke(Seq->GetThenPinGivenIndex(2), TEXT("SquadFlow_HideRegionBlocks"));
    }
    {
        FGraph G(BP, Functions[TEXT("SquadFlow_CreateRoomUI")]);
        G.Comment(TEXT("根据场景Tag创建UI → 缓存强引用 → 绑定加载/卸载完成 → 传入当前瓦片ID加载小队。配置失败也推进流程并记录异常。"));
        auto* Valid = G.Branch(G.Start(), G.IsValid(G.Value(TEXT("BaseUI"))));
        auto* Create = G.Call(UBaseMapWidget::StaticClass(), TEXT("CreateSceneUIByTag"));
        Link(G.Value(TEXT("BaseUI")), Pin(Create, TEXT("self")));
        Link(G.Value(TEXT("SceneTag")), Pin(Create, TEXT("SceneTag")));
        auto* Cast = G.CastTo(USquadMeetingRoomWidget::StaticClass(), Pin(Create, TEXT("ReturnValue")), G.Exec(Pin(Valid, TEXT("then")), Create));
        auto* P = G.Set(Pin(Cast, TEXT("then")), TEXT("RoomUI"), TEXT(""), Cast->GetCastResultPin());
        P = G.Bind(P, TEXT("OnLoadCompleted"), TEXT("SquadFlow_UILoadCompleted"));
        P = G.Bind(P, TEXT("OnUnloadCompleted"), TEXT("SquadFlow_UIUnloadCompleted"));
        auto* Load = G.Call(USquadMeetingRoomWidget::StaticClass(), TEXT("LoadSquadList"));
        Link(G.Value(TEXT("RoomUI")), Pin(Load, TEXT("self"))); Link(G.Value(TEXT("CurrentTileId")), Pin(Load, TEXT("TileId")));
        auto* Loaded = G.Branch(G.Exec(P, Load), Pin(Load, TEXT("ReturnValue")));
        auto* LoadFailed = G.Error(Pin(Loaded, TEXT("else")), TEXT("小队列表加载失败，请检查当前瓦片ID；仍等待UI动画正常完成。"));
        // The host may reuse an already-loaded widget for the same tag. Its original load event
        // has already fired, so checking lifecycle state after binding closes that missed-event window.
        auto* AlreadyLoaded = G.Call(UBaseSceneWidget::StaticClass(), TEXT("IsSceneUILoaded"));
        Link(G.Value(TEXT("RoomUI")), Pin(AlreadyLoaded, TEXT("self")));
        auto* Ready = G.Branch(Pin(Loaded, TEXT("then")), Pin(AlreadyLoaded, TEXT("ReturnValue")));
        Link(LoadFailed, Pin(Ready, TEXT("execute")));
        G.Invoke(Pin(Ready, TEXT("then")), TEXT("SquadFlow_UILoadCompleted"));
        G.Row(1150);
        G.Invoke(G.Error(Pin(Valid, TEXT("else")), TEXT("缺少基地UI，已跳过小队UI创建。")), TEXT("SquadFlow_UILoadCompleted"));
        G.Row(1600);
        G.Invoke(G.Error(Cast->GetInvalidCastPin(), TEXT("小队会议室Tag未映射到正确UI或创建失败，请检查基地UI的SceneUIClasses。")), TEXT("SquadFlow_UILoadCompleted"));
    }
    {
        FGraph G(BP, Functions[TEXT("SquadFlow_HideRegionBlocks")]);
        G.Comment(TEXT("沿用 BP_SceneManager 的隐藏全部区域块函数，不在会议室C++中管理区域。退出后由下一个场景决定区域显示。"));
        auto* GetManager = G.Call(Library, TEXT("GetSceneManager"));
        auto* Cast = G.CastTo(ManagerBP->GeneratedClass, Pin(GetManager, TEXT("ReturnValue")), G.Start());
        auto* Hide = G.Call(ManagerBP->GeneratedClass, TEXT("隐藏全部区域块"));
        Link(Cast->GetCastResultPin(), Pin(Hide, TEXT("self"))); G.Exec(Pin(Cast, TEXT("then")), Hide);
        G.Row(650); G.Error(Cast->GetInvalidCastPin(), TEXT("未找到 BP_SceneManager，无法隐藏区域块。"));
    }
    const auto Callback = [&](FName Name, int32 Stage, FName Flag, FName Finish, const TCHAR* Description)
    {
        FGraph G(BP, Functions[Name]); G.Comment(Description);
        auto* Guard = G.Branch(G.Start(), G.StageIs(Stage));
        G.Invoke(G.Set(Pin(Guard, TEXT("then")), Flag, TEXT("true")), Finish);
    };
    Callback(CameraInCallback, 1, TEXT("CameraFinish"), TEXT("SquadFlow_TryFinishEnter"), TEXT("只接受当前进入阶段的相机回调；旧相机回调不会推进退出流程。"));
    Callback(TEXT("SquadFlow_UILoadCompleted"), 1, TEXT("UiFinish"), TEXT("SquadFlow_TryFinishEnter"), TEXT("UI滑入动画结束后到这里；与相机完成汇合，不能在创建UI后立刻通知场景完成。"));
    Callback(TEXT("SquadFlow_CameraLeaveCompleted"), 3, TEXT("CameraFinish"), TEXT("SquadFlow_TryFinishLeave"), TEXT("相机退回区域上方后，等待会议室UI完成滑出。"));
    Callback(TEXT("SquadFlow_UIUnloadCompleted"), 3, TEXT("UiFinish"), TEXT("SquadFlow_TryFinishLeave"), TEXT("UI滑出完成后到这里。通知场景离开必须等下一帧，避免在基地UI卸载回调内部重入创建下一界面。"));
    {
        FGraph G(BP, Functions[TEXT("SquadFlow_TryFinishEnter")]);
        G.Comment(TEXT("汇合：进入阶段 + 相机完成 + UI完成。先切换阶段防重入，再显示返回按钮，最后只通知一次场景进入完成。"));
        auto* Done = G.Both(G.Value(TEXT("UiFinish")), G.Value(TEXT("CameraFinish")));
        auto* Guard = G.Branch(G.Start(), G.Both(G.StageIs(1), Done));
        auto* P = G.Set(Pin(Guard, TEXT("then")), TEXT("FlowStage"), TEXT("2"));
        P = G.Invoke(P, TEXT("SquadFlow_ShowBackButton"));
        G.Exec(P, G.Call(SceneClass, TEXT("NotifyEnterCompleted")));
    }
    {
        FGraph G(BP, Functions[TEXT("LeaveScene")]);
        G.Comment(TEXT("退出：重置两项完成标志、隐藏返回按钮，并行相机退到区域上方与卸载自身UI。保持 false 直到两项都完成。"));
        auto* P = G.Set(G.Start(), TEXT("FlowStage"), TEXT("3"));
        P = G.Set(P, TEXT("UiFinish"), TEXT("false")); P = G.Set(P, TEXT("CameraFinish"), TEXT("false"));
        P = G.Invoke(P, TEXT("SquadFlow_HideBackButton"));
        auto* Seq = G.Sequence(P, 3);
        G.Row(450); CameraMove(G, Seq->GetThenPinGivenIndex(0), TEXT("OverheadCameraState"), TEXT("SquadFlow_CameraLeaveCompleted"), TEXT("退出相机移动未启动，继续等待UI退出，避免无法返回基地。"), false);
        G.Row(1100); G.Invoke(Seq->GetThenPinGivenIndex(1), TEXT("SquadFlow_UnloadOwnUI"));
        G.Row(1500); G.Return(Seq->GetThenPinGivenIndex(2));
    }
    {
        FGraph G(BP, Functions[TEXT("SquadFlow_UnloadOwnUI")]);
        G.Comment(TEXT("仅当基地UI当前子界面就是缓存的小队会议室时才卸载；缺失或已被替换时推进完成，绝不卸载其它场景的UI。"));
        auto* Valid = G.Branch(G.Start(), G.Both(G.IsValid(G.Value(TEXT("BaseUI"))), G.IsValid(G.Value(TEXT("RoomUI")))));
        auto* Current = G.Call(UBaseMapWidget::StaticClass(), TEXT("GetCurrentSceneUI"));
        Link(G.Value(TEXT("BaseUI")), Pin(Current, TEXT("self")));
        auto* Equal = G.Call(UKismetMathLibrary::StaticClass(), TEXT("EqualEqual_ObjectObject"));
        Link(Pin(Current, TEXT("ReturnValue")), Pin(Equal, TEXT("A"))); Link(G.Value(TEXT("RoomUI")), Pin(Equal, TEXT("B")));
        auto* Own = G.Branch(Pin(Valid, TEXT("then")), Pin(Equal, TEXT("ReturnValue")));
        auto* Unload = G.Call(UBaseMapWidget::StaticClass(), TEXT("UnloadCurrentSceneUI"));
        Link(G.Value(TEXT("BaseUI")), Pin(Unload, TEXT("self"))); G.Exec(Pin(Own, TEXT("then")), Unload);
        G.Row(800); G.Invoke(G.Error(Pin(Valid, TEXT("else")), TEXT("退出时会议室UI或基地UI已不存在，按UI退出完成继续。")), TEXT("SquadFlow_UIUnloadCompleted"));
        G.Row(1250); G.Invoke(G.Error(Pin(Own, TEXT("else")), TEXT("基地当前界面已被替换，保留其它界面并完成小队会议室退出。")), TEXT("SquadFlow_UIUnloadCompleted"));
    }
    {
        FGraph G(BP, Functions[TEXT("SquadFlow_UnbindUI")]);
        G.Comment(TEXT("仅解绑本场景绑定的两个委托；不清空UI上其他系统的监听。"));
        auto* Valid = G.Branch(G.Start(), G.IsValid(G.Value(TEXT("RoomUI"))));
        auto* P = G.Bind(Pin(Valid, TEXT("then")), TEXT("OnLoadCompleted"), TEXT("SquadFlow_UILoadCompleted"), true);
        G.Bind(P, TEXT("OnUnloadCompleted"), TEXT("SquadFlow_UIUnloadCompleted"), true);
    }
    {
        FGraph G(BP, Functions[TEXT("SquadFlow_TryFinishLeave")]);
        G.Comment(TEXT("退出汇合：先标记结束，解绑UI事件并清空缓存。下一帧最后通知场景管理器，之后不再访问已离开的场景世界。"));
        auto* Done = G.Both(G.Value(TEXT("UiFinish")), G.Value(TEXT("CameraFinish")));
        auto* Guard = G.Branch(G.Start(), G.Both(G.StageIs(3), Done));
        auto* P = G.Set(Pin(Guard, TEXT("then")), TEXT("FlowStage"), TEXT("4"));
        P = G.Invoke(P, TEXT("SquadFlow_UnbindUI"));
        P = G.Set(P, TEXT("RoomUI"), TEXT("None")); P = G.Set(P, TEXT("BaseUI"), TEXT("None"));
        G.Invoke(P, DeferredEvent);
    }
    {
        FGraph G(BP, EventGraph);
        G.Comment(TEXT("卸载完成的安全边界：等待下一帧，使 BaseWidget 先释放卸载锁、移除旧UI。通知离开后可能马上进入下一场景，因此通知必须为最后一步。"));
        auto* Delay = G.Call(UKismetSystemLibrary::StaticClass(), TEXT("DelayUntilNextTick"));
        auto* Guard = G.Branch(G.Exec(Pin(CompleteEvent, TEXT("then")), Delay), G.StageIs(4));
        G.Exec(Pin(Guard, TEXT("then")), G.Call(SceneClass, TEXT("NotifyLeaveCompleted")));
    }

    if (!bGraphOK) return false;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status == BS_Error) return false;
    BP->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args);
}
}

int32 USquadSceneFlowCommandlet::Main(const FString& Params)
{
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, SquadSceneFlow::ScenePath);
    if (!BP || !BP->GeneratedClass) return 1;
    if (FParse::Param(*Params, TEXT("Inspect")))
    {
        SquadSceneFlow::Inspect(BP);
        UE_LOG(LogTemp, Display, TEXT("SQUAD_SCENE_FLOW_INSPECT_OK"));
        return 0;
    }
    if (!FParse::Param(*Params, TEXT("Build")))
    {
        UE_LOG(LogTemp, Error, TEXT("Use -run=SquadSceneFlow -Inspect or -Build."));
        return 1;
    }
    const bool bSuccess = SquadSceneFlow::Build(BP);
    UE_LOG(LogTemp, Display, TEXT("SQUAD_SCENE_FLOW_%s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));
    return bSuccess ? 0 : 1;
}
