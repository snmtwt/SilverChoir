#include "CommandMapInputCommandlet.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Self.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Map/BaseMap/BaseSandboxMap.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "UObject/SavePackage.h"

namespace CommandMapInput
{
constexpr const TCHAR* Path = TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController");
constexpr const TCHAR* Marker = TEXT("CommandMapPointer_v1");
constexpr const TCHAR* PickMarker = TEXT("CommandMapPointer_MapPick_v2");
const TArray<FName> Functions = {TEXT("BeginCommandMapPointer"), TEXT("UpdateCommandMapPointer"),
    TEXT("ReleaseCommandMapPointer"), TEXT("CancelCommandMapPointer")};
const FName MapVar(TEXT("CommandMapPointerMap")), TileVar(TEXT("CommandMapPressedTile"));
const FName PositionVar(TEXT("CommandMapPressPosition")), PendingVar(TEXT("bCommandMapPointerPending"));
const FName DraggedVar(TEXT("bCommandMapPointerDragged")), ThresholdVar(TEXT("CommandMapDragThreshold"));
const UEdGraphSchema_K2* Schema() { return GetDefault<UEdGraphSchema_K2>(); }
UClass* PCClass() { return AGameMainMapPlayerController::StaticClass(); }

struct FGraph
{
    UEdGraph* Graph;
    bool& OK;
    FName Name;
    int32 X = 300, Y = 0;
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* InitialResult = nullptr;
    FGraph(UBlueprint* BP, FName InName, bool& InOK) : OK(InOK), Name(InName)
    {
        Graph = FBlueprintEditorUtils::CreateNewGraph(BP, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph(BP, Graph, false, PCClass());
        for (UEdGraphNode* N : Graph->Nodes)
        {
            if (auto* E = Cast<UK2Node_FunctionEntry>(N)) Entry = E;
            if (auto* R = Cast<UK2Node_FunctionResult>(N)) InitialResult = R;
        }
        if (!Entry) { OK = false; return; }
        Entry->BreakAllNodeLinks();
        Entry->MetaData.Category = FText::FromString(TEXT("作战指挥室|地图输入"));
        if (InitialResult) { InitialResult->BreakAllNodeLinks(); Graph->RemoveNode(InitialResult); }
    }
    template<class T> T* Node(TFunctionRef<void(T*)> Setup)
    {
        FGraphNodeCreator<T> Creator(*Graph); auto* N = Creator.CreateNode(); Setup(N);
        N->NodePosX = X; N->NodePosY = Y; X += 300; Creator.Finalize(); return N;
    }
    UEdGraphPin* Pin(UEdGraphNode* N, FName P)
    {
        auto* Result = N ? N->FindPin(P) : nullptr;
        if (!Result) { OK = false; UE_LOG(LogTemp, Error, TEXT("COMMAND_MAP_PIN %s.%s"), *GetNameSafe(N), *P.ToString()); }
        return Result;
    }
    void Link(UEdGraphPin* A, UEdGraphPin* B)
    {
        if (!A || !B || !Schema()->TryCreateConnection(A, B))
        { OK = false; UE_LOG(LogTemp, Error, TEXT("COMMAND_MAP_LINK %s %s -> %s"), *Name.ToString(), A ? *A->PinName.ToString() : TEXT("null"), B ? *B->PinName.ToString() : TEXT("null")); }
    }
    void Default(UEdGraphNode* N, FName P, const TCHAR* Value)
    { if (auto* V = Pin(N, P)) Schema()->TrySetDefaultValue(*V, Value); }
    UEdGraphPin* Start() { return Pin(Entry, UEdGraphSchema_K2::PN_Then); }
    UK2Node_CallFunction* Call(FName Func, UClass* Class = PCClass())
    {
        UFunction* F = Class->FindFunctionByName(Func);
        checkf(F, TEXT("Missing function %s.%s"), *Class->GetName(), *Func.ToString());
        return Node<UK2Node_CallFunction>([&](auto* N) { N->SetFromFunction(F); });
    }
    UEdGraphPin* Exec(UEdGraphPin* From, UEdGraphNode* N)
    { Link(From, Pin(N, UEdGraphSchema_K2::PN_Execute)); return Pin(N, UEdGraphSchema_K2::PN_Then); }
    UEdGraphPin* Invoke(UEdGraphPin* From, FName Func) { return Exec(From, Call(Func)); }
    UEdGraphPin* Get(FName Var)
    { return Node<UK2Node_VariableGet>([&](auto* N) { N->VariableReference.SetSelfMember(Var); })->GetValuePin(); }
    UEdGraphPin* Set(UEdGraphPin* From, FName Var, UEdGraphPin* Value = nullptr, const TCHAR* Literal = nullptr)
    {
        auto* N = Node<UK2Node_VariableSet>([&](auto* V) { V->VariableReference.SetSelfMember(Var); });
        if (Value) Link(Value, Pin(N, Var));
        if (Literal) Default(N, Var, Literal);
        return Exec(From, N);
    }
    UEdGraphPin* Self()
    { return Pin(Node<UK2Node_Self>([](auto*) {}), UEdGraphSchema_K2::PN_Self); }
    UEdGraphPin* Valid(UEdGraphPin* Object)
    { auto* N = Call(TEXT("IsValid"), UKismetSystemLibrary::StaticClass()); Link(Object, Pin(N, TEXT("Object"))); return N->GetReturnValuePin(); }
    UEdGraphPin* Math(FName Func, UEdGraphPin* A, UEdGraphPin* B)
    {
        auto* N = Call(Func, UKismetMathLibrary::StaticClass());
        Link(A, Pin(N, TEXT("A"))); Link(B, Pin(N, TEXT("B"))); return N->GetReturnValuePin();
    }
    UK2Node_IfThenElse* Branch(UEdGraphPin* From, UEdGraphPin* Condition)
    {
        auto* N = Node<UK2Node_IfThenElse>([](auto*) {});
        Link(From, N->GetExecPin()); Link(Condition, N->GetConditionPin()); return N;
    }
    void Return(UEdGraphPin* From, bool Value = false)
    {
        auto* N = Node<UK2Node_FunctionResult>([&](auto* R) { R->FunctionReference.SetExternalMember(Name, PCClass()); });
        if (N->FindPin(UEdGraphSchema_K2::PN_ReturnValue)) Default(N, UEdGraphSchema_K2::PN_ReturnValue, Value ? TEXT("true") : TEXT("false"));
        Link(From, Pin(N, UEdGraphSchema_K2::PN_Execute));
    }
    void Comment(const TCHAR* Text)
    {
        auto* N = NewObject<UEdGraphNode_Comment>(Graph); Graph->AddNode(N, false, false); N->CreateNewGuid();
        N->NodeComment = FString(Text) + TEXT("\n") + Marker; N->NodePosX = -100; N->NodePosY = -250;
        N->NodeWidth = 2300; N->NodeHeight = 190; N->CommentColor = FLinearColor(.025f, .12f, .18f, 1.f);
    }
};

bool AddVariables(UBlueprint* BP)
{
    auto Add = [&](FName Name, FName Type, UObject* Sub, const TCHAR* Default, bool bEditable)
    {
        FEdGraphPinType T; T.PinCategory = Type; T.PinSubCategoryObject = Sub;
        if (Type == UEdGraphSchema_K2::PC_Real) T.PinSubCategory = UEdGraphSchema_K2::PC_Double;
        if (!FBlueprintEditorUtils::AddMemberVariable(BP, Name, T, Default)) return false;
        for (auto& V : BP->NewVariables) if (V.VarName == Name)
        {
            V.Category = FText::FromString(TEXT("作战指挥室|地图输入"));
            if (!bEditable) V.PropertyFlags = (V.PropertyFlags | CPF_Transient) & ~(CPF_Edit | CPF_BlueprintReadOnly);
        }
        return true;
    };
    const bool Added = Add(MapVar, UEdGraphSchema_K2::PC_Object, ABaseSandboxMap::StaticClass(), TEXT("None"), false)
        && Add(TileVar, UEdGraphSchema_K2::PC_Object, AGSMTile3D::StaticClass(), TEXT("None"), false)
        && Add(PositionVar, UEdGraphSchema_K2::PC_Struct, TBaseStructure<FVector2D>::Get(), TEXT("(X=0,Y=0)"), false)
        && Add(PendingVar, UEdGraphSchema_K2::PC_Boolean, nullptr, TEXT("false"), false)
        && Add(DraggedVar, UEdGraphSchema_K2::PC_Boolean, nullptr, TEXT("false"), false)
        && Add(ThresholdVar, UEdGraphSchema_K2::PC_Real, nullptr, TEXT("6.0"), true);
    if (Added)
    {
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, ThresholdVar, nullptr, TEXT("DisplayName"), TEXT("左键拖动阈值（像素）"));
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, ThresholdVar, nullptr, TEXT("ClampMin"), TEXT("1.0"));
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, ThresholdVar, nullptr, TEXT("ToolTip"), TEXT("鼠标离按下位置超过此距离后开始拖拽；开始拖拽后本次抬起不再选中瓦片。"));
    }
    return Added;
}

void BuildCancel(UBlueprint* BP, bool& OK)
{
    FGraph G(BP, Functions[3], OK);
    auto* Map = G.Get(MapVar); auto* Guard = G.Branch(G.Start(), G.Valid(Map));
    auto* End = G.Call(TEXT("EndDragMap"), AGSMMap3D::StaticClass()); G.Link(Map, G.Pin(End, UEdGraphSchema_K2::PN_Self));
    auto* Flow = G.Exec(Guard->GetThenPin(), End);
    // The controller swallowed this release. Consume its legacy suppression now,
    // so a later normal actor click outside this room is not accidentally dropped.
    auto* Consume = G.Call(TEXT("ConsumeTileClickSuppressionAfterDrag"), AGSMMap3D::StaticClass());
    G.Link(Map, G.Pin(Consume, UEdGraphSchema_K2::PN_Self));
    Flow = G.Exec(Flow, Consume);
    auto* Reset = G.Node<UK2Node_VariableSet>([](auto* N) { N->VariableReference.SetSelfMember(PendingVar); });
    G.Default(Reset, PendingVar, TEXT("false"));
    G.Link(Guard->GetElsePin(), G.Pin(Reset, UEdGraphSchema_K2::PN_Execute));
    Flow = G.Exec(Flow, Reset);
    Flow = G.Set(Flow, DraggedVar, nullptr, TEXT("false"));
    Flow = G.Set(Flow, MapVar);
    Flow = G.Set(Flow, TileVar);
    G.Return(Flow);
    G.Comment(TEXT("中止或收尾：结束插件拖拽并清空状态。失焦、鼠标进入UI/移出沙盘、离开指挥室、切图和EndPlay均会取消，不选中瓦片。"));
}
void BuildBegin(UBlueprint* BP, bool& OK)
{
    FGraph G(BP, Functions[0], OK);
    auto* Flow = G.Invoke(G.Start(), Functions[3]);
    auto* Query = G.Call(TEXT("QueryCommandMapPointer"));
    auto* Allowed = G.Branch(G.Exec(Flow, Query), Query->GetReturnValuePin());
    auto* Begin = G.Call(TEXT("BeginDragMapWithMouse"), AGSMMap3D::StaticClass());
    G.Link(G.Pin(Query, TEXT("OutMap")), G.Pin(Begin, UEdGraphSchema_K2::PN_Self));
    G.Link(G.Self(), G.Pin(Begin, TEXT("PlayerController")));
    auto* Begun = G.Branch(G.Exec(Allowed->GetThenPin(), Begin), Begin->GetReturnValuePin());
    Flow = G.Set(Begun->GetThenPin(), MapVar, G.Pin(Query, TEXT("OutMap")));
    Flow = G.Set(Flow, TileVar, G.Pin(Query, TEXT("OutTile")));
    Flow = G.Set(Flow, PositionVar, G.Pin(Query, TEXT("OutScreenPosition")));
    Flow = G.Set(Flow, PendingVar, nullptr, TEXT("true"));
    Flow = G.Set(Flow, DraggedVar, nullptr, TEXT("false"));
    G.Return(Flow, true);
    G.Y = 500; G.X = 1000; G.Return(Allowed->GetElsePin()); G.Return(Begun->GetElsePin());
    G.Comment(TEXT("左键按下：仅接受已进入且UI就绪的作战指挥室沙盘。调用插件开始拖拽以记录锚点，保存瓦片与鼠标位置；此时不移动、不选中。"));
}
void BuildUpdate(UBlueprint* BP, bool& OK)
{
    FGraph G(BP, Functions[1], OK);
    auto* Pending = G.Branch(G.Start(), G.Get(PendingVar));
    auto* Query = G.Call(TEXT("QueryCommandMapPointer"));
    G.Default(Query, TEXT("bResolveTile"), TEXT("false"));
    auto* SameMap = G.Math(TEXT("EqualEqual_ObjectObject"), G.Get(MapVar), G.Pin(Query, TEXT("OutMap")));
    auto* Allowed = G.Branch(G.Exec(Pending->GetThenPin(), Query), G.Math(TEXT("BooleanAND"), Query->GetReturnValuePin(), SameMap));
    auto* Distance = G.Call(TEXT("Distance2D"), UKismetMathLibrary::StaticClass());
    G.Link(G.Get(PositionVar), G.Pin(Distance, TEXT("V1"))); G.Link(G.Pin(Query, TEXT("OutScreenPosition")), G.Pin(Distance, TEXT("V2")));
    auto* Exceeds = G.Math(TEXT("GreaterEqual_DoubleDouble"), Distance->GetReturnValuePin(), G.Get(ThresholdVar));
    auto* Drag = G.Branch(Allowed->GetThenPin(), G.Math(TEXT("BooleanOR"), G.Get(DraggedVar), Exceeds));
    auto* Flow = G.Set(Drag->GetThenPin(), DraggedVar, nullptr, TEXT("true"));
    auto* Move = G.Call(TEXT("DragMapToMousePosition"), AGSMMap3D::StaticClass());
    G.Link(G.Get(MapVar), G.Pin(Move, UEdGraphSchema_K2::PN_Self)); G.Link(G.Self(), G.Pin(Move, TEXT("PlayerController")));
    auto* Moved = G.Branch(G.Exec(Flow, Move), Move->GetReturnValuePin());
    G.Return(Moved->GetThenPin(), true); G.Return(Drag->GetElsePin(), true);
    G.Y = 500; G.X = 900;
    auto* Cancel = G.Call(Functions[3]);
    G.Exec(Allowed->GetElsePin(), Cancel); G.Exec(Moved->GetElsePin(), Cancel);
    G.Return(G.Pin(Cancel, UEdGraphSchema_K2::PN_Then)); G.Return(Pending->GetElsePin());
    G.Comment(TEXT("按住期间每帧更新：超过 CommandMapDragThreshold（默认6像素）后调用插件移动内容，沙盘底座与相机不动。拖动标记保持到抬起，即使鼠标回到起点也不当作点击。\nCommandMapPointer_MapPick_v2"));
}
void BuildRelease(UBlueprint* BP, bool& OK)
{
    FGraph G(BP, Functions[2], OK);
    auto* Update = G.Call(Functions[1]);
    auto* Active = G.Branch(G.Exec(G.Start(), Update), Update->GetReturnValuePin());
    auto* Dragged = G.Branch(Active->GetThenPin(), G.Get(DraggedVar));
    auto* Query = G.Call(TEXT("QueryCommandMapPointer"));
    auto* SameTile = G.Math(TEXT("EqualEqual_ObjectObject"), G.Get(TileVar), G.Pin(Query, TEXT("OutTile")));
    auto* ValidTile = G.Math(TEXT("BooleanAND"), G.Valid(G.Get(TileVar)), SameTile);
    auto* Click = G.Branch(G.Exec(Dragged->GetElsePin(), Query), G.Math(TEXT("BooleanAND"), Query->GetReturnValuePin(), ValidTile));
    auto* Select = G.Call(TEXT("HandleTileClickRequest"), AGSMMap3D::StaticClass());
    G.Link(G.Get(MapVar), G.Pin(Select, UEdGraphSchema_K2::PN_Self));
    G.Link(G.Get(TileVar), G.Pin(Select, TEXT("TileActor")));
    G.Link(G.Pin(Query, TEXT("OutWorldHit")), G.Pin(Select, TEXT("WorldHitLocation")));
    auto* Flow = G.Exec(Click->GetThenPin(), Select);
    auto* Cancel = G.Call(Functions[3]); G.Exec(Flow, Cancel);
    for (auto* End : {Active->GetElsePin(), Dragged->GetThenPin(), Click->GetElsePin()}) G.Exec(End, Cancel);
    G.Return(G.Pin(Cancel, UEdGraphSchema_K2::PN_Then));
    G.Comment(TEXT("左键抬起：先补算最后一次位移（防止快速拖动被判作点击）。仅未拖动且仍在按下的同一有效瓦片时，调用插件处理瓦片点击请求；所有分支最终结束拖拽并清理。"));
}
}

UCommandMapInputCommandlet::UCommandMapInputCommandlet() { IsClient = false; IsEditor = true; LogToConsole = true; }
int32 UCommandMapInputCommandlet::Main(const FString& Params)
{
    using namespace CommandMapInput;
    auto* BP = LoadObject<UBlueprint>(nullptr, Path);
    if (!BP || BP->ParentClass != PCClass()) return 1;
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    if (Graphs.ContainsByPredicate([](const UEdGraph* G) { return G->GetFName() == TEXT("CommandMap_QueryPointer"); }))
    { UE_LOG(LogTemp, Display, TEXT("COMMAND_MAP_INPUT_BLUEPRINT_OWNED use GameMainMapArchitecture for inspection")); return 0; }
    int32 Owned = 0; bool Conflict = false;
    for (UEdGraph* Graph : Graphs)
    {
        UE_LOG(LogTemp, Display, TEXT("COMMAND_MAP_GRAPH %s nodes=%d"), *Graph->GetName(), Graph->Nodes.Num());
        if (!Functions.Contains(Graph->GetFName())) continue;
        const bool HasMarker = Graph->Nodes.ContainsByPredicate([](const UEdGraphNode* N)
        { return N->IsA<UEdGraphNode_Comment>() && N->NodeComment.Contains(Marker); });
        if (HasMarker) ++Owned; else Conflict = true;
    }
    const bool Already = Owned == Functions.Num() && !Conflict;
    UK2Node_CallFunction* UpdateQuery = nullptr;
    UEdGraphNode* UpdateComment = nullptr;
    bool HasPickMarker = false;
    if (Already)
        for (UEdGraph* Graph : Graphs) if (Graph->GetFName() == Functions[1])
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node->IsA<UEdGraphNode_Comment>() && Node->NodeComment.Contains(Marker))
                { UpdateComment = Node; HasPickMarker |= Node->NodeComment.Contains(PickMarker); }
                if (auto* Call = Cast<UK2Node_CallFunction>(Node); Call && Call->FunctionReference.GetMemberName() == TEXT("QueryCommandMapPointer"))
                {
                    if (UpdateQuery) Conflict = true;
                    UpdateQuery = Call;
                }
            }
    if (Already && !UpdateQuery) Conflict = true;
    const bool UpgradeQuery = Already && !HasPickMarker;
    for (const auto& V : BP->NewVariables)
        if (!Already && TArray<FName>{MapVar, TileVar, PositionVar, PendingVar, DraggedVar, ThresholdVar}.Contains(V.VarName)) Conflict = true;
    if (Conflict || (Owned != 0 && !Already))
    { UE_LOG(LogTemp, Error, TEXT("COMMAND_MAP_INPUT_CONFLICT no asset changed")); return 1; }
    UE_LOG(LogTemp, Display, TEXT("COMMAND_MAP_INPUT_PLAN add=%d upgradeQuery=%d preserveExistingGraphs=1"), !Already, UpgradeQuery);
    if (!FParse::Param(*Params, TEXT("Apply"))) return 0;
    if (Already && !UpgradeQuery) { UE_LOG(LogTemp, Display, TEXT("COMMAND_MAP_INPUT_ALREADY_APPLIED")); return 0; }
    const FString File = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
    const FString Backup = FPaths::ProjectSavedDir() / TEXT("CommandMapInput/Backups")
        / (FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*Backup, true);
    if (IFileManager::Get().Copy(*(Backup / FPaths::GetCleanFilename(File)), *File) != COPY_OK) return 1;
    bool OK = true;
    if (Already)
    {
        UpdateQuery->ReconstructNode();
        auto* NewPin = UpdateQuery->FindPin(TEXT("bResolveTile"));
        if (!NewPin) return 1;
        Schema()->TrySetDefaultValue(*NewPin, TEXT("false"));
        if (UpdateComment) UpdateComment->NodeComment += FString(TEXT("\n")) + PickMarker;
    }
    else
    {
        if (!AddVariables(BP)) return 1;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        BuildCancel(BP, OK); BuildBegin(BP, OK); BuildUpdate(BP, OK); BuildRelease(BP, OK);
    }
    if (!OK) return 1;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status == BS_Error) return 1;
    BP->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
    if (!UPackage::SavePackage(BP->GetOutermost(), BP, *File, Args)) return 1;
    UE_LOG(LogTemp, Display, TEXT("COMMAND_MAP_INPUT_APPLY_OK functions=4 threshold=6 backup=%s"), *Backup);
    return 0;
}
