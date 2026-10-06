#include "BattleUnitSelectionAssetsCommandlet.h"

#include "Map/BattleMap/BattleUnitSelectionComponent.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EnhancedInputAction.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_VariableGet.h"
#include "InputAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

namespace BattleSelectionAssets
{
constexpr const TCHAR* ControllerPath = TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController");
constexpr const TCHAR* ActionPath = TEXT("/Game/System/Input/Common/IA_MouseLeft.IA_MouseLeft");
constexpr const TCHAR* Marker = TEXT("BattleUnitSelection_v1");
const FName GraphName(TEXT("BattleUnitSelection"));
const FName InputPins[] = {TEXT("Started"), TEXT("Completed"), TEXT("Canceled")};
const FName Events[] = {TEXT("BattleSelection_Begin"), TEXT("BattleSelection_Complete"), TEXT("BattleSelection_Cancel")};
const FName Functions[] = {TEXT("BeginBoxSelection"), TEXT("CompleteBoxSelection"), TEXT("CancelBoxSelection")};

bool Link(UEdGraphPin* From, UEdGraphPin* To)
{
    if (From && To && GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(From, To)) return true;
    UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_LINK_FAILED %s -> %s"),
        From ? *From->PinName.ToString() : TEXT("null"), To ? *To->PinName.ToString() : TEXT("null"));
    return false;
}

template<class T, class SetupType>
T* Node(UEdGraph* Graph, int32 X, int32 Y, SetupType&& Setup)
{
    FGraphNodeCreator<T> Creator(*Graph);
    auto* Result = Creator.CreateNode();
    Setup(Result);
    Result->NodePosX = X;
    Result->NodePosY = Y;
    Creator.Finalize();
    return Result;
}

bool Compile(UBlueprint* BP)
{
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status != BS_Error) return true;
    UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_BLUEPRINT_COMPILE_FAILED"));
    return false;
}

bool VerifyInstalled(UEdGraph* Graph, UK2Node_EnhancedInputAction* Input)
{
    if (!Graph->Nodes.ContainsByPredicate([](const UEdGraphNode* N)
        { return N->IsA<UEdGraphNode_Comment>() && N->NodeComment.Contains(Marker); })) return false;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        UK2Node_CustomEvent* Event = nullptr;
        for (UEdGraphNode* N : Graph->Nodes)
            if (auto* Candidate = Cast<UK2Node_CustomEvent>(N); Candidate && Candidate->CustomFunctionName == Events[Index])
            {
                if (Event) return false;
                Event = Candidate;
            }
        auto* Then = Event ? Event->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr;
        if (!Then || Then->LinkedTo.Num() != 1) return false;
        auto* ComponentCall = Cast<UK2Node_CallFunction>(Then->LinkedTo[0]->GetOwningNode());
        if (!ComponentCall || ComponentCall->FunctionReference.GetMemberName() != Functions[Index]) return false;
        auto* Target = ComponentCall->FindPin(UEdGraphSchema_K2::PN_Self);
        if (!Target || Target->LinkedTo.Num() != 1) return false;
        auto* ComponentGet = Cast<UK2Node_VariableGet>(Target->LinkedTo[0]->GetOwningNode());
        if (!ComponentGet || ComponentGet->VariableReference.GetMemberName() != TEXT("UnitSelection")) return false;

        auto* Pin = Input->FindPin(InputPins[Index]);
        if (!Pin || Pin->LinkedTo.Num() != 1) return false;
        auto* Sequence = Cast<UK2Node_ExecutionSequence>(Pin->LinkedTo[0]->GetOwningNode());
        if (!Sequence || Sequence->NodeComment != FString(Marker) + TEXT("_") + InputPins[Index].ToString()) return false;
        auto* NewFlow = Sequence->GetThenPinGivenIndex(1);
        if (!NewFlow || NewFlow->LinkedTo.Num() != 1) return false;
        auto* RoutedCall = Cast<UK2Node_CallFunction>(NewFlow->LinkedTo[0]->GetOwningNode());
        if (!RoutedCall || RoutedCall->FunctionReference.GetMemberName() != Events[Index]) return false;
    }
    return true;
}
}

UBattleUnitSelectionAssetsCommandlet::UBattleUnitSelectionAssetsCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

namespace BattleSelectionAssets
{
int32 MigrateController(const FString& Params)
{
    auto* BP = LoadObject<UBlueprint>(nullptr, ControllerPath);
    if (!BP || !BP->ParentClass || !BP->ParentClass->IsChildOf(AGameMainMapPlayerController::StaticClass())) return 1;
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    UK2Node_EnhancedInputAction* Left = nullptr;
    UEdGraph* ExistingGraph = nullptr;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph->GetFName() == GraphName) ExistingGraph = Graph;
        for (UEdGraphNode* N : Graph->Nodes)
            if (auto* Input = Cast<UK2Node_EnhancedInputAction>(N);
                Input && Input->InputAction && Input->InputAction->GetPathName() == ActionPath)
            {
                if (Left) { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_DUPLICATE_LEFT_ACTION no asset changed")); return 2; }
                Left = Input;
            }
    }
    if (!Left) { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_LEFT_ACTION_MISSING no asset changed")); return 2; }
    if (ExistingGraph)
    {
        if (!VerifyInstalled(ExistingGraph, Left))
        { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_EXISTING_GRAPH_CONFLICT no asset changed")); return 2; }
        UE_LOG(LogTemp, Display, TEXT("BATTLE_SELECTION_ALREADY_APPLIED graph=BattleUnitSelection commonAction=IA_MouseLeft"));
        return 0;
    }
    for (FName Name : Events)
        if (BP->GeneratedClass->FindFunctionByName(Name))
        { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_EVENT_NAME_CONFLICT %s no asset changed"), *Name.ToString()); return 2; }
    for (FName Name : InputPins)
    {
        auto* Pin = Left->FindPin(Name);
        if (!Pin || Pin->LinkedTo.Num() > 1)
        { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_UNEXPECTED_ACTION_LINKS %s no asset changed"), *Name.ToString()); return 2; }
        UE_LOG(LogTemp, Display, TEXT("BATTLE_SELECTION_PLAN input=%s oldLinks=%d"), *Name.ToString(), Pin->LinkedTo.Num());
    }
    if (!FParse::Param(*Params, TEXT("Apply")))
    { UE_LOG(LogTemp, Display, TEXT("BATTLE_SELECTION_INSPECT_ONLY use -Apply to append Blueprint routing")); return 0; }

    const FString File = FPackageName::LongPackageNameToFilename(ControllerPath, FPackageName::GetAssetPackageExtension());
    const FString BackupFolder = FPaths::ProjectSavedDir() / TEXT("BattleUnitSelection/Backups") /
        (FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*BackupFolder, true);
    if (IFileManager::Get().Copy(*(BackupFolder / FPaths::GetCleanFilename(File)), *File) != COPY_OK)
    { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_BACKUP_FAILED no asset changed")); return 3; }

    auto* Graph = FBlueprintEditorUtils::CreateNewGraph(BP, GraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(BP, Graph);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto* Event = Node<UK2Node_CustomEvent>(Graph, 0, Index * 320, [&](auto* N) { N->CustomFunctionName = Events[Index]; });
        auto* Component = Node<UK2Node_VariableGet>(Graph, 0, Index * 320 + 130,
            [](auto* N) { N->VariableReference.SetSelfMember(TEXT("UnitSelection")); });
        auto* Function = UBattleUnitSelectionComponent::StaticClass()->FindFunctionByName(Functions[Index]);
        if (!Function) return 4;
        auto* Call = Node<UK2Node_CallFunction>(Graph, 350, Index * 320, [&](auto* N) { N->SetFromFunction(Function); });
        if (!Link(Event->FindPin(UEdGraphSchema_K2::PN_Then), Call->GetExecPin()) ||
            !Link(Component->GetValuePin(), Call->FindPin(UEdGraphSchema_K2::PN_Self))) return 4;
    }
    auto* Comment = NewObject<UEdGraphNode_Comment>(Graph);
    Graph->AddNode(Comment, false, false);
    Comment->CreateNewGuid();
    Comment->NodeComment = FString(TEXT("战斗单位框选：左键按下开始，抬起确认，取消时不改变选择。\n")) +
        TEXT("C++ 组件负责矩形、单位筛选和差量选中事件；后续游戏业务在本图接入。\n") + Marker;
    Comment->NodePosX = -40; Comment->NodePosY = -200;
    Comment->NodeWidth = 900; Comment->NodeHeight = 170;
    Comment->CommentColor = FLinearColor(.025f, .12f, .18f, 1.f);
    if (!Compile(BP)) return 4;

    // Reuse the existing input event. Original routing remains the first sequence branch,
    // including the user's Base/Battle switches and all sandbox behavior.
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto* Pin = Left->FindPin(InputPins[Index]);
        const TArray<UEdGraphPin*> Previous = Pin->LinkedTo;
        auto* Sequence = Node<UK2Node_ExecutionSequence>(Left->GetGraph(), Left->NodePosX + 280, Left->NodePosY + 900 + Index * 260,
            [&](auto* N) { N->NodeComment = FString(Marker) + TEXT("_") + InputPins[Index].ToString(); });
        auto* Call = Node<UK2Node_CallFunction>(Left->GetGraph(), Left->NodePosX + 550, Left->NodePosY + 900 + Index * 260,
            [&](auto* N) { N->FunctionReference.SetSelfMember(Events[Index]); });
        Pin->BreakAllPinLinks();
        if (!Link(Pin, Sequence->GetExecPin())) return 4;
        for (auto* Original : Previous) if (!Link(Sequence->GetThenPinGivenIndex(0), Original)) return 4;
        if (!Link(Sequence->GetThenPinGivenIndex(1), Call->GetExecPin())) return 4;
    }
    if (!Compile(BP) || !VerifyInstalled(Graph, Left)) return 4;
    BP->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(BP->GetOutermost(), BP, *File, Args)) return 5;
    UE_LOG(LogTemp, Display, TEXT("BATTLE_SELECTION_APPLY_OK backup=%s graph=BattleUnitSelection input=IA_MouseLeft originalLinksPreserved=1"), *BackupFolder);
    return 0;
}

constexpr const TCHAR* CardPath = TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_人员卡片");
constexpr const TCHAR* CardMarker = TEXT("BattlePersonnelSelection_v1");
const FName CardGraphName(TEXT("UnitSelectionBusiness"));

bool HasLink(const UEdGraphPin* From, const UEdGraphPin* To)
{ return From && To && From->LinkedTo.Num() == 1 && From->LinkedTo[0] == To; }

bool VerifyCardRouting(UK2Node_Event* Event, UK2Node_ExecutionSequence* Sequence)
{
    if (!Event || !Sequence || Sequence->NodeComment != CardMarker ||
        !HasLink(Event->FindPin(UEdGraphSchema_K2::PN_Then), Sequence->GetExecPin())) return false;
    auto* NewFlow = Sequence->GetThenPinGivenIndex(1);
    auto* CastPC = NewFlow && NewFlow->LinkedTo.Num() == 1
        ? Cast<UK2Node_DynamicCast>(NewFlow->LinkedTo[0]->GetOwningNode()) : nullptr;
    if (!CastPC || CastPC->TargetType != AGameMainMapPlayerController::StaticClass() ||
        !HasLink(NewFlow, CastPC->GetExecPin())) return false;
    auto* Source = CastPC->GetCastSourcePin();
    auto* Owner = Source && Source->LinkedTo.Num() == 1
        ? Cast<UK2Node_CallFunction>(Source->LinkedTo[0]->GetOwningNode()) : nullptr;
    if (!Owner || Owner->FunctionReference.GetMemberName() != FName(TEXT("GetOwningPlayer")) ||
        !HasLink(Source, Owner->GetReturnValuePin())) return false;
    auto* Success = CastPC->GetValidCastPin();
    auto* Select = Success && Success->LinkedTo.Num() == 1
        ? Cast<UK2Node_CallFunction>(Success->LinkedTo[0]->GetOwningNode()) : nullptr;
    if (!Select || Select->FunctionReference.GetMemberName() != TEXT("SelectUnitById") ||
        !HasLink(Success, Select->GetExecPin()) || !HasLink(Select->FindPin(TEXT("UnitId")), Event->FindPin(TEXT("UnitId")))) return false;
    auto* Target = Select->FindPin(UEdGraphSchema_K2::PN_Self);
    auto* Component = Target && Target->LinkedTo.Num() == 1
        ? Cast<UK2Node_VariableGet>(Target->LinkedTo[0]->GetOwningNode()) : nullptr;
    return Component && Component->VariableReference.GetMemberName() == TEXT("UnitSelection") &&
        HasLink(Target, Component->GetValuePin()) &&
        HasLink(Component->FindPin(UEdGraphSchema_K2::PN_Self), CastPC->GetCastResultPin());
}

int32 MigratePersonnelCard(const FString& Params)
{
    auto* BP = LoadObject<UBlueprint>(nullptr, CardPath);
    if (!BP || !BP->ParentClass || !BP->ParentClass->IsChildOf(UBattlePersonnelCardWidget::StaticClass()))
    { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_CARD_MISSING_OR_WRONG_PARENT")); return 6; }
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    UK2Node_Event* Invoked = nullptr;
    UK2Node_ExecutionSequence* Installed = nullptr;
    UEdGraph* ReservedGraph = nullptr;
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph->GetFName() == CardGraphName) ReservedGraph = Graph;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (auto* Event = Cast<UK2Node_Event>(Node);
                Event && Event->EventReference.GetMemberName() == GET_FUNCTION_NAME_CHECKED(UBattlePersonnelCardWidget, OnMemberInvoked))
            {
                if (Invoked) { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_CARD_DUPLICATE_EVENT no card asset changed")); return 6; }
                Invoked = Event;
            }
            if (auto* Sequence = Cast<UK2Node_ExecutionSequence>(Node); Sequence && Sequence->NodeComment == CardMarker)
            {
                if (Installed) { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_CARD_DUPLICATE_ROUTING no card asset changed")); return 6; }
                Installed = Sequence;
            }
        }
    }
    if (Installed)
    {
        if (!VerifyCardRouting(Invoked, Installed))
        { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_CARD_ROUTING_CONFLICT no card asset changed")); return 6; }
        UE_LOG(LogTemp, Display, TEXT("BATTLE_SELECTION_CARD_ALREADY_APPLIED event=OnMemberInvoked selection=SelectUnitById"));
        return 0;
    }
    if (!Invoked && ReservedGraph)
    { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_CARD_GRAPH_CONFLICT no card asset changed")); return 6; }
    if (Invoked)
    {
        auto* Then = Invoked->FindPin(UEdGraphSchema_K2::PN_Then);
        if (!Then || Then->LinkedTo.Num() > 1 || !Invoked->FindPin(TEXT("UnitId")))
        { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_CARD_UNEXPECTED_EVENT_PINS no card asset changed")); return 6; }
    }
    auto* GetOwnerFunction = UUserWidget::StaticClass()->FindFunctionByName(TEXT("GetOwningPlayer"));
    auto* SelectFunction = UBattleUnitSelectionComponent::StaticClass()->FindFunctionByName(TEXT("SelectUnitById"));
    if (!GetOwnerFunction || !SelectFunction)
    { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_CARD_FUNCTION_MISSING no card asset changed")); return 6; }
    UE_LOG(LogTemp, Display, TEXT("BATTLE_SELECTION_CARD_PLAN event=OnMemberInvoked append=SelectUnitById originalLinksPreserved=1"));
    if (!FParse::Param(*Params, TEXT("Apply"))) return 0;

    const FString File = FPackageName::LongPackageNameToFilename(CardPath, FPackageName::GetAssetPackageExtension());
    const FString BackupFolder = FPaths::ProjectSavedDir() / TEXT("BattleUnitSelection/Backups") /
        (FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*BackupFolder, true);
    if (IFileManager::Get().Copy(*(BackupFolder / FPaths::GetCleanFilename(File)), *File) != COPY_OK)
    { UE_LOG(LogTemp, Error, TEXT("BATTLE_SELECTION_CARD_BACKUP_FAILED no card asset changed")); return 7; }

    if (!Invoked)
    {
        auto* Graph = FBlueprintEditorUtils::CreateNewGraph(BP, CardGraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(BP, Graph);
        Invoked = Node<UK2Node_Event>(Graph, 0, 0, [](auto* N)
        {
            N->EventReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UBattlePersonnelCardWidget, OnMemberInvoked), UBattlePersonnelCardWidget::StaticClass());
            N->bOverrideFunction = true;
        });
    }
    auto* Graph = Invoked->GetGraph();
    const int32 X = Invoked->NodePosX + 280, Y = Invoked->NodePosY + 360;
    auto* Sequence = Node<UK2Node_ExecutionSequence>(Graph, X, Y, [](auto* N) { N->NodeComment = CardMarker; });
    auto* Owner = Node<UK2Node_CallFunction>(Graph, X, Y + 210, [&](auto* N) { N->SetFromFunction(GetOwnerFunction); });
    auto* CastPC = Node<UK2Node_DynamicCast>(Graph, X + 270, Y, [](auto* N) { N->TargetType = AGameMainMapPlayerController::StaticClass(); });
    CastPC->SetPurity(false);
    auto* Component = Node<UK2Node_VariableGet>(Graph, X + 550, Y + 210, [](auto* N)
    {
        N->VariableReference.SetExternalMember(GET_MEMBER_NAME_CHECKED(AGameMainMapPlayerController, UnitSelection), AGameMainMapPlayerController::StaticClass());
    });
    auto* Select = Node<UK2Node_CallFunction>(Graph, X + 800, Y, [&](auto* N) { N->SetFromFunction(SelectFunction); });
    auto* Then = Invoked->FindPin(UEdGraphSchema_K2::PN_Then);
    const TArray<UEdGraphPin*> Previous = Then->LinkedTo;
    Then->BreakAllPinLinks();
    if (!Link(Then, Sequence->GetExecPin())) return 8;
    for (auto* Original : Previous) if (!Link(Sequence->GetThenPinGivenIndex(0), Original)) return 8;
    if (!Link(Sequence->GetThenPinGivenIndex(1), CastPC->GetExecPin()) ||
        !Link(Owner->GetReturnValuePin(), CastPC->GetCastSourcePin()) ||
        !Link(CastPC->GetCastResultPin(), Component->FindPin(UEdGraphSchema_K2::PN_Self)) ||
        !Link(CastPC->GetValidCastPin(), Select->GetExecPin()) ||
        !Link(Component->GetValuePin(), Select->FindPin(UEdGraphSchema_K2::PN_Self)) ||
        !Link(Invoked->FindPin(TEXT("UnitId")), Select->FindPin(TEXT("UnitId")))) return 8;
    auto* Comment = NewObject<UEdGraphNode_Comment>(Graph);
    Graph->AddNode(Comment, false, false);
    Comment->CreateNewGuid();
    Comment->NodeComment = TEXT("人员点击业务：将 UnitId 交给当前玩家的单位选择组件。\n已有事件流程保留在 Sequence 的 Then 0；数据事件同步 Pawn 与卡片外观。");
    Comment->NodePosX = X - 30; Comment->NodePosY = Y - 140;
    Comment->NodeWidth = 1220; Comment->NodeHeight = 110;
    Comment->CommentColor = FLinearColor(.025f, .12f, .18f, 1.f);
    if (!Compile(BP) || !VerifyCardRouting(Invoked, Sequence)) return 8;
    BP->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(BP->GetOutermost(), BP, *File, Args)) return 9;
    UE_LOG(LogTemp, Display, TEXT("BATTLE_SELECTION_CARD_APPLY_OK backup=%s event=OnMemberInvoked selection=SelectUnitById originalLinksPreserved=1"), *BackupFolder);
    return 0;
}
}

namespace BattleSelectionAssets
{
int32 RetirePersonnelRouting(const FString& Params)
{
    auto* BP=LoadObject<UBlueprint>(nullptr,CardPath);
    if(!BP)return 1;
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    UK2Node_Event* Event=nullptr;
    UK2Node_ExecutionSequence* Sequence=nullptr;
    for(auto* Graph:Graphs)for(UEdGraphNode* N:Graph->Nodes)
    {
        if(auto* E=Cast<UK2Node_Event>(N);E&&E->EventReference.GetMemberName()==TEXT("OnMemberInvoked"))Event=E;
        if(auto* S=Cast<UK2Node_ExecutionSequence>(N);S&&S->NodeComment==CardMarker)Sequence=S;
    }
    if(!Sequence){UE_LOG(LogTemp,Display,TEXT("BATTLE_SELECTION_CARD_NATIVE_ALREADY"));return 0;}
    if(!VerifyCardRouting(Event,Sequence))return 2;
    auto* CastPC=CastChecked<UK2Node_DynamicCast>(Sequence->GetThenPinGivenIndex(1)->LinkedTo[0]->GetOwningNode());
    auto* Owner=CastPC->GetCastSourcePin()->LinkedTo[0]->GetOwningNode();
    auto* Select=CastChecked<UK2Node_CallFunction>(CastPC->GetValidCastPin()->LinkedTo[0]->GetOwningNode());
    auto* Component=Select->FindPin(UEdGraphSchema_K2::PN_Self)->LinkedTo[0]->GetOwningNode();
    if(!Select->GetThenPin()->LinkedTo.IsEmpty())
    {UE_LOG(LogTemp,Error,TEXT("BATTLE_SELECTION_CARD_NATIVE_CUSTOM_CONTINUATION preserve controller-cast business manually"));return 2;}
    // Preserve customized graphs rather than deleting nodes that acquired other responsibilities.
    const TSet<UEdGraphNode*> Generated={Sequence,CastPC,Owner,Select,Component};
    for(auto* N:Generated)for(auto* P:N->Pins)for(auto* Linked:P->LinkedTo)
        if(!Generated.Contains(Linked->GetOwningNode())&&Linked->GetOwningNode()!=Event
            &&P!=Sequence->GetThenPinGivenIndex(0)&&P!=Select->GetThenPin())
        {UE_LOG(LogTemp,Error,TEXT("BATTLE_SELECTION_CARD_NATIVE_CONFLICT %s"),*N->GetName());return 2;}
    if(!FParse::Param(*Params,TEXT("Apply")))return 0;
    const FString File=FPackageName::LongPackageNameToFilename(CardPath,FPackageName::GetAssetPackageExtension());
    const FString Backup=FPaths::ProjectSavedDir()/TEXT("BattlePanelInteraction/Backup")/FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
    IFileManager::Get().MakeDirectory(*Backup,true);
    if(IFileManager::Get().Copy(*(Backup/FPaths::GetCleanFilename(File)),*File)!=COPY_OK)return 3;
    const TArray<UEdGraphPin*> Continuations=Select->GetThenPin()->LinkedTo;
    const TArray<UEdGraphPin*> Original=Sequence->GetThenPinGivenIndex(0)->LinkedTo;
    if(Continuations.IsEmpty())
    {
        Event->FindPin(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
        for(auto* Pin:Original)if(!Link(Event->FindPin(UEdGraphSchema_K2::PN_Then),Pin))return 4;
        FBlueprintEditorUtils::RemoveNode(BP,Sequence,true);
    }
    else
    {
        Sequence->GetThenPinGivenIndex(1)->BreakAllPinLinks();
        for(auto* Pin:Continuations)if(!Link(Sequence->GetThenPinGivenIndex(1),Pin))return 4;
        Sequence->NodeComment=TEXT("保留原人员点击业务；单位选中现由 C++ 点击基类完成");
    }
    for(auto* N:{static_cast<UEdGraphNode*>(CastPC),Owner,static_cast<UEdGraphNode*>(Select),Component})
        FBlueprintEditorUtils::RemoveNode(BP,N,true);
    for(auto* Graph:Graphs)for(auto* N:TArray<UEdGraphNode*>(Graph->Nodes))
        if(N->IsA<UEdGraphNode_Comment>()&&N->NodeComment.StartsWith(TEXT("人员点击业务：将 UnitId")))
            N->NodeComment=TEXT("单位选择已由人员卡 C++ 基类处理。这里保留点击后的附加业务；控制面板请使用 ControlPanelBusiness 中的打开/关闭事件。");
    if(!Compile(BP))return 4;
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    if(!UPackage::SavePackage(BP->GetOutermost(),BP,*File,Args))return 5;
    UE_LOG(LogTemp,Display,TEXT("BATTLE_SELECTION_CARD_NATIVE_OK originalBusinessPreserved=1 backup=%s"),*Backup);
    return 0;
}
}

int32 UBattleUnitSelectionAssetsCommandlet::Main(const FString& Params)
{
    const int32 ControllerResult = BattleSelectionAssets::MigrateController(Params);
    if (ControllerResult != 0) return ControllerResult;
    return BattleSelectionAssets::RetirePersonnelRouting(Params);
}
