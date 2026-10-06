#include "WidgetBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SubSystem/PlayerSubSystem/PlayerManagerBase.h"
#include "SubSystem/PlayerSubSystem/PlayerLibrary.h"
#include "SubSystem/PlayerSubSystem/PlayerCameraPawn.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/PersonnelPreparationRoomWidget.h"
#include "Components/SIS_UnitInventoryComponent.h"
#include "Tools/SIS_InventorySystemBFL.h"

static bool AttachWarehouseInitialInventory(UWidgetBlueprint* BP);
static bool GuardWarehouseContainer(UWidgetBlueprint* BP);
static bool NormalizeWarehouseEmptyExport(UWidgetBlueprint* BP);

bool MigrateWarehouseNativeSession(UWidgetBlueprint* BP)
{
    if (!BP) return false;
    UK2Node_Event* Open=nullptr; UEdGraphNode* Changed=nullptr;
    UK2Node_CallFunction* Restore=nullptr; UK2Node_CallFunction* Commit=nullptr;
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    for (UEdGraph* G:Graphs) for (UEdGraphNode* N:G->Nodes)
    {
        if (N->NodeComment==TEXT("WarehouseNativeSession_v1")) return true;
        if (G->GetFName()==TEXT("OnInventoryPageChanged") && Cast<UK2Node_FunctionEntry>(N)) Changed=N;
        if (auto* E=Cast<UK2Node_Event>(N))
        {
            if (E->EventReference.GetMemberName()==TEXT("OnSceneUIOpened")) Open=E;
            if (E->EventReference.GetMemberName()==TEXT("OnInventoryPageChanged")) Changed=E;
        }
        if (auto* C=Cast<UK2Node_CallFunction>(N))
        {
            if (C->FunctionReference.GetMemberName()==TEXT("RestoreInventoryPageSelection") && C->FindPinChecked(TEXT("PageIndex"))->LinkedTo.Num()>0 &&
                Cast<UK2Node_VariableGet>(C->FindPinChecked(TEXT("PageIndex"))->LinkedTo[0]->GetOwningNode())) Restore=C;
            if (C->FunctionReference.GetMemberName()==TEXT("SetActiveInventoryPage")) Commit=C;
        }
    }
    if (!Open || !Changed || !Restore || !Commit || Restore->GetThenPin()->LinkedTo.Num()!=1) return false;
    auto* Refresh=Cast<UK2Node_CallFunction>(Restore->GetThenPin()->LinkedTo[0]->GetOwningNode());
    if (!Refresh || Refresh->FunctionReference.GetMemberName()!=TEXT("LoadItemsToInventoryWidget")) return false;
    auto Tail=Refresh->GetThenPin()->LinkedTo;
    auto ChangeTail=Commit->GetThenPin()->LinkedTo;
    auto* G=Open->GetGraph(); const auto* S=GetDefault<UEdGraphSchema_K2>();
    FGraphNodeCreator<UK2Node_CallFunction> Creator(*G);auto* Init=Creator.CreateNode();
    Init->SetFromFunction(UPersonnelPreparationRoomWidget::StaticClass()->FindFunctionByName(TEXT("InitializeWarehouseSession")));
    Init->NodePosX=Open->NodePosX+350;Init->NodePosY=Open->NodePosY;Init->NodeComment=TEXT("WarehouseNativeSession_v1");Init->bCommentBubbleVisible=true;Creator.Finalize();
    Open->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    Refresh->GetThenPin()->BreakAllPinLinks();
    Commit->GetThenPin()->BreakAllPinLinks();
    Changed->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    bool OK=S->TryCreateConnection(Open->FindPinChecked(UEdGraphSchema_K2::PN_Then),Init->GetExecPin());
    for (auto* P:Tail) OK=S->TryCreateConnection(Init->GetThenPin(),P)&&OK;
    for (auto* P:ChangeTail) OK=S->TryCreateConnection(Changed->FindPinChecked(UEdGraphSchema_K2::PN_Then),P)&&OK;
    return OK;
}

// Insert before the existing initial registration; preserve the user's graph and layout.
bool FixWarehouseInitialPage(UWidgetBlueprint* BP)
{
    if (!BP) return false;
    UK2Node_CallFunction* Register=nullptr;
    for (UEdGraph* G:BP->UbergraphPages) for (UEdGraphNode* N:G->Nodes)
    {
        if (N->NodeComment==TEXT("WarehouseInitialPage_v2")) return true;
        if (auto* E=Cast<UK2Node_Event>(N); E && E->EventReference.GetMemberName()==TEXT("OnSceneUIOpened"))
        {
            auto* Then=E->FindPinChecked(UEdGraphSchema_K2::PN_Then);
            if (Then->LinkedTo.Num()==1) Register=Cast<UK2Node_CallFunction>(Then->LinkedTo[0]->GetOwningNode());
        }
    }
    if (!Register || Register->GetExecPin()->LinkedTo.IsEmpty()) return false;
    auto* Target=Register->FindPinChecked(UEdGraphSchema_K2::PN_Self);
    if (Target->LinkedTo.IsEmpty()) return false;
    auto* Inventory=Target->LinkedTo[0];
    if (!Register->FindPin(TEXT("InventorySlotContainer"))) return false;
    auto* Graph=Register->GetGraph(); const auto* S=GetDefault<UEdGraphSchema_K2>(); bool OK=true;
    auto Link=[&](UEdGraphPin* A,UEdGraphPin* B){ OK=S->TryCreateConnection(A,B)&&OK; };
    auto Call=[&](UClass* Owner,FName Name,int X,int Y){ FGraphNodeCreator<UK2Node_CallFunction> C(*Graph);auto* N=C.CreateNode();N->SetFromFunction(Owner->FindFunctionByName(Name));N->NodePosX=X;N->NodePosY=Y;C.Finalize();return N; };
    auto Branch=[&](int X,int Y){ FGraphNodeCreator<UK2Node_IfThenElse> C(*Graph);auto* N=C.CreateNode();N->NodePosX=X;N->NodePosY=Y;C.Finalize();return N; };
    auto* Manager=Call(UPlayerLibrary::StaticClass(),TEXT("GetPlayerManager"),3000,5600);
    auto* Valid=Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"),3250,5600);
    Link(Manager->GetReturnValuePin(),Valid->FindPinChecked(TEXT("Object")));
    auto* Guard=Branch(3500,5400);Guard->NodeComment=TEXT("WarehouseInitialPage_v2");
    Guard->bCommentBubbleVisible=true;
    auto Old=Register->GetExecPin()->LinkedTo;Register->GetExecPin()->BreakAllPinLinks();
    for (auto* P:Old) Link(P,Guard->GetExecPin());
    Link(Valid->GetReturnValuePin(),Guard->GetConditionPin());
    auto* InventoryValid=Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"),3500,6100);
    Link(Inventory,InventoryValid->FindPinChecked(TEXT("Object")));
    auto* InventoryGuard=Branch(3700,5200);Link(Guard->GetThenPin(),InventoryGuard->GetExecPin());Link(InventoryValid->GetReturnValuePin(),InventoryGuard->GetConditionPin());
    auto* Check=Call(UPlayerManagerBase::StaticClass(),TEXT("IsInventoryPageInitialized"),3500,5800);
    Link(Manager->GetReturnValuePin(),Check->FindPinChecked(UEdGraphSchema_K2::PN_Self));Link(Inventory,Check->FindPinChecked(TEXT("InventoryComponent")));
    auto* Ready=Branch(3800,5400);Link(InventoryGuard->GetThenPin(),Ready->GetExecPin());Link(Check->GetReturnValuePin(),Ready->GetConditionPin());
    Link(Ready->GetThenPin(),Register->GetExecPin());
    FGraphNodeCreator<UK2Node_VariableGet> VC(*Graph);auto* Active=VC.CreateNode();Active->VariableReference.SetExternalMember(TEXT("ActiveInventoryPage"),UPlayerManagerBase::StaticClass());Active->NodePosX=3800;Active->NodePosY=5800;VC.Finalize();
    Link(Manager->GetReturnValuePin(),Active->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    auto* Read=Call(UPlayerManagerBase::StaticClass(),TEXT("ReadInventoryPage"),4100,5800);
    Link(Manager->GetReturnValuePin(),Read->FindPinChecked(UEdGraphSchema_K2::PN_Self));Link(Active->GetValuePin(),Read->FindPinChecked(TEXT("PageIndex")));
    auto* HasPage=Branch(4100,5400);Link(Ready->GetElsePin(),HasPage->GetExecPin());Link(Read->GetReturnValuePin(),HasPage->GetConditionPin());
    auto* Load=Call(USIS_InventorySystemBFL::StaticClass(),TEXT("LoadItemDatasToInventoryComponent"),4400,5400);
    Link(HasPage->GetThenPin(),Load->GetExecPin());Link(Inventory,Load->FindPinChecked(TEXT("InventoryComponent")));Link(Read->FindPinChecked(TEXT("Items")),Load->FindPinChecked(TEXT("InItemDataArray")));
    Load->FindPinChecked(TEXT("bCreateNewItemId"))->DefaultValue=TEXT("false");
    auto* Mark=Call(UPlayerManagerBase::StaticClass(),TEXT("MarkInventoryPageInitialized"),4800,5400);
    Link(Load->GetThenPin(),Mark->GetExecPin());Link(Manager->GetReturnValuePin(),Mark->FindPinChecked(UEdGraphSchema_K2::PN_Self));Link(Inventory,Mark->FindPinChecked(TEXT("InventoryComponent")));Link(Mark->GetThenPin(),Register->GetExecPin());
    return OK;
}

// Author the business sequence as editable K2 nodes, not runtime C++ orchestration.
bool BuildWarehouseInventoryFlow(UWidgetBlueprint* BP)
{
    if (!BP || BP->UbergraphPages.IsEmpty()) return false;
    UEdGraph* Graph=BP->UbergraphPages[0];
    const auto* Schema=GetDefault<UEdGraphSchema_K2>();
    bool OK=true;
    for (UEdGraph* G:BP->UbergraphPages) for (UEdGraphNode* N:G->Nodes)
        if (N->NodeComment==TEXT("WarehouseInventoryFlow_v1")) return AttachWarehouseInitialInventory(BP);
    auto Link=[&](UEdGraphPin* A,UEdGraphPin* B){ if (!A || !B || !Schema->TryCreateConnection(A,B)) { OK=false; UE_LOG(LogTemp,Error,TEXT("Inventory flow link failed: %s -> %s"),*GetNameSafe(A?A->GetOwningNode():nullptr),*GetNameSafe(B?B->GetOwningNode():nullptr)); } };
    auto Call=[&](UClass* Owner,FName Name,int X,int Y)
    {
        FGraphNodeCreator<UK2Node_CallFunction> C(*Graph); auto* N=C.CreateNode(); N->SetFromFunction(Owner->FindFunctionByName(Name)); N->NodePosX=X; N->NodePosY=Y; C.Finalize(); return N;
    };
    auto Var=[&](UClass* Owner,FName Name,int X,int Y)
    {
        FGraphNodeCreator<UK2Node_VariableGet> C(*Graph); auto* N=C.CreateNode(); N->VariableReference.SetExternalMember(Name,Owner); N->NodePosX=X;N->NodePosY=Y;C.Finalize();return N;
    };
    auto Branch=[&](UEdGraphPin* Exec,UEdGraphPin* Condition,int X,int Y)
    {
        FGraphNodeCreator<UK2Node_IfThenElse> C(*Graph);auto* N=C.CreateNode();N->NodePosX=X;N->NodePosY=Y;C.Finalize();Link(Exec,N->GetExecPin());Link(Condition,N->GetConditionPin());return N;
    };
    auto Event=[&](FName Name,UClass* Owner,int Y)
    {
        for (UEdGraph* G:BP->UbergraphPages) for (UEdGraphNode* N:G->Nodes)
            if (auto* E=Cast<UK2Node_Event>(N);E && E->EventReference.GetMemberName()==Name) { Graph=G; E->ReconstructNode(); return E; }
        FGraphNodeCreator<UK2Node_Event> C(*Graph);auto* E=C.CreateNode();E->EventReference.SetExternalMember(Name,Owner);E->bOverrideFunction=true;E->NodePosY=Y;C.Finalize();return E;
    };
    auto* E=Event(TEXT("OnInventoryPageChanged"),UPersonnelPreparationRoomWidget::StaticClass(),2500);
    auto Old=E->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo;
    E->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    E->NodeComment=TEXT("WarehouseInventoryFlow_v1");
    const int Y=E->NodePosY+300;
    auto* Manager=Call(UPlayerLibrary::StaticClass(),TEXT("GetPlayerManager"),0,Y+400);
    auto* Camera=Call(UPlayerLibrary::StaticClass(),TEXT("GetPlayerCamera"),0,Y+650);
    auto* Inventory=Var(APlayerCameraPawn::StaticClass(),TEXT("UnitInventoryComponent"),300,Y+650);
    Link(Camera->GetReturnValuePin(),Inventory->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    auto* MValid=Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"),300,Y+400);
    Link(Manager->GetReturnValuePin(),MValid->FindPinChecked(TEXT("Object")));
    auto* MB=Branch(E->FindPinChecked(UEdGraphSchema_K2::PN_Then),MValid->GetReturnValuePin(),400,Y);
    auto* CValid=Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"),300,Y+800);
    Link(Camera->GetReturnValuePin(),CValid->FindPinChecked(TEXT("Object")));
    auto* CB=Branch(MB->GetThenPin(),CValid->GetReturnValuePin(),650,Y);
    auto* Valid=Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"),650,Y+650);
    Link(Inventory->GetValuePin(),Valid->FindPinChecked(TEXT("Object")));
    auto* VB=Branch(CB->GetThenPin(),Valid->GetReturnValuePin(),900,Y);
    auto* Export=Call(USIS_InventorySystemBFL::StaticClass(),TEXT("GetAllItemDatasFromInventoryComponent"),1150,Y);
    Link(VB->GetThenPin(),Export->GetExecPin());Link(Inventory->GetValuePin(),Export->FindPinChecked(TEXT("InventoryComponent")));
    Export->FindPinChecked(TEXT("bIncludeAbilityData"))->DefaultValue=TEXT("true");
    auto* EB=Branch(Export->GetThenPin(),Export->FindPinChecked(TEXT("bIsSuccess")),1500,Y);
    auto* Save=Call(UPlayerManagerBase::StaticClass(),TEXT("WriteInventoryPage"),1750,Y);
    Link(EB->GetThenPin(),Save->GetExecPin());Link(Manager->GetReturnValuePin(),Save->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    Link(E->FindPinChecked(TEXT("PreviousPageIndex")),Save->FindPinChecked(TEXT("PageIndex")));Link(Export->FindPinChecked(TEXT("OutItemDataArray")),Save->FindPinChecked(TEXT("Items")));
    auto* SB=Branch(Save->GetThenPin(),Save->GetReturnValuePin(),2100,Y);
    auto* Read=Call(UPlayerManagerBase::StaticClass(),TEXT("ReadInventoryPage"),2100,Y+350);
    Link(Manager->GetReturnValuePin(),Read->FindPinChecked(UEdGraphSchema_K2::PN_Self));Link(E->FindPinChecked(TEXT("PageIndex")),Read->FindPinChecked(TEXT("PageIndex")));
    auto* RB=Branch(SB->GetThenPin(),Read->GetReturnValuePin(),2350,Y);
    auto* Load=Call(USIS_InventorySystemBFL::StaticClass(),TEXT("LoadItemDatasToInventoryComponent"),2600,Y);
    Link(RB->GetThenPin(),Load->GetExecPin());Link(Inventory->GetValuePin(),Load->FindPinChecked(TEXT("InventoryComponent")));Link(Read->FindPinChecked(TEXT("Items")),Load->FindPinChecked(TEXT("InItemDataArray")));
    Load->FindPinChecked(TEXT("bCreateNewItemId"))->DefaultValue=TEXT("false");
    auto* Register=Call(USIS_UnitInventoryComponent::StaticClass(),TEXT("RegisterInventorySlotContainer"),2950,Y);
    Link(Load->GetThenPin(),Register->GetExecPin());Link(Inventory->GetValuePin(),Register->FindPinChecked(UEdGraphSchema_K2::PN_Self));Link(E->FindPinChecked(TEXT("InventoryContainer")),Register->FindPinChecked(TEXT("InventorySlotContainer")));
    auto* Refresh=Call(USIS_UnitInventoryComponent::StaticClass(),TEXT("LoadItemsToInventoryWidget"),3300,Y);
    Link(Register->GetThenPin(),Refresh->GetExecPin());Link(Inventory->GetValuePin(),Refresh->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    auto* Commit=Call(UPlayerManagerBase::StaticClass(),TEXT("SetActiveInventoryPage"),3650,Y);
    Link(Refresh->GetThenPin(),Commit->GetExecPin());Link(Manager->GetReturnValuePin(),Commit->FindPinChecked(UEdGraphSchema_K2::PN_Self));Link(E->FindPinChecked(TEXT("PageIndex")),Commit->FindPinChecked(TEXT("PageIndex")));
    for (auto* P:Old) Link(Commit->GetThenPin(),P);
    auto* Restore=Call(UPersonnelPreparationRoomWidget::StaticClass(),TEXT("RestoreInventoryPageSelection"),2100,Y+1000);
    Link(E->FindPinChecked(TEXT("PreviousPageIndex")),Restore->FindPinChecked(TEXT("PageIndex")));
    for (auto* B:{MB,CB,VB,EB,SB,RB}) Link(B->GetElsePin(),Restore->GetExecPin());
    // A new UI instance resumes the page currently loaded in the player's component.
    auto* Construct=Event(TEXT("Construct"),UUserWidget::StaticClass(),4500);
    auto ConstructOld=Construct->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo;
    Construct->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    auto* CM=Call(UPlayerLibrary::StaticClass(),TEXT("GetPlayerManager"),0,4800);
    auto* CV=Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"),300,4800);Link(CM->GetReturnValuePin(),CV->FindPinChecked(TEXT("Object")));
    auto* InitBranch=Branch(Construct->FindPinChecked(UEdGraphSchema_K2::PN_Then),CV->GetReturnValuePin(),550,4500);
    auto* Active=Var(UPlayerManagerBase::StaticClass(),TEXT("ActiveInventoryPage"),550,4800);Link(CM->GetReturnValuePin(),Active->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    auto* Init=Call(UPersonnelPreparationRoomWidget::StaticClass(),TEXT("RestoreInventoryPageSelection"),850,4500);
    Link(InitBranch->GetThenPin(),Init->GetExecPin());Link(Active->GetValuePin(),Init->FindPinChecked(TEXT("PageIndex")));
    for(auto* P:ConstructOld) { Link(Init->GetThenPin(),P);Link(InitBranch->GetElsePin(),P); }
    return OK && AttachWarehouseInitialInventory(BP);
}

static bool AttachWarehouseInitialInventory(UWidgetBlueprint* BP)
{
    UK2Node_CallFunction* Init=nullptr;
    for(UEdGraph* G:BP->UbergraphPages) for(UEdGraphNode* N:G->Nodes)
    {
        if(N->NodeComment==TEXT("WarehouseInventoryInit_v1")) return GuardWarehouseContainer(BP);
        auto* C=Cast<UK2Node_CallFunction>(N);
        if(C && C->FunctionReference.GetMemberName()==TEXT("RestoreInventoryPageSelection") && C->NodePosY==4500) Init=C;
    }
    if(!Init) return false;
    auto* Graph=Init->GetGraph();const auto* Schema=GetDefault<UEdGraphSchema_K2>();bool OK=true;
    auto Link=[&](UEdGraphPin* A,UEdGraphPin* B){OK=Schema->TryCreateConnection(A,B)&&OK;};
    auto Call=[&](UClass* Owner,FName Name,int X,int Y){FGraphNodeCreator<UK2Node_CallFunction> C(*Graph);auto* N=C.CreateNode();N->SetFromFunction(Owner->FindFunctionByName(Name));N->NodePosX=X;N->NodePosY=Y;C.Finalize();return N;};
    auto* Camera=Call(UPlayerLibrary::StaticClass(),TEXT("GetPlayerCamera"),1100,4800);
    auto* Valid=Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"),1400,4800);Link(Camera->GetReturnValuePin(),Valid->FindPinChecked(TEXT("Object")));
    FGraphNodeCreator<UK2Node_IfThenElse> BC(*Graph);auto* Branch=BC.CreateNode();Branch->NodePosX=1400;Branch->NodePosY=4500;BC.Finalize();
    auto Old=Init->GetThenPin()->LinkedTo;Init->GetThenPin()->BreakAllPinLinks();Link(Init->GetThenPin(),Branch->GetExecPin());Link(Valid->GetReturnValuePin(),Branch->GetConditionPin());
    FGraphNodeCreator<UK2Node_VariableGet> VC(*Graph);auto* Inventory=VC.CreateNode();Inventory->VariableReference.SetExternalMember(TEXT("UnitInventoryComponent"),APlayerCameraPawn::StaticClass());Inventory->NodePosX=1700;Inventory->NodePosY=4800;VC.Finalize();Link(Camera->GetReturnValuePin(),Inventory->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    auto* IValid=Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"),2000,4800);Link(Inventory->GetValuePin(),IValid->FindPinChecked(TEXT("Object")));
    FGraphNodeCreator<UK2Node_IfThenElse> IC(*Graph);auto* IB=IC.CreateNode();IB->NodePosX=1750;IB->NodePosY=4500;IC.Finalize();Link(Branch->GetThenPin(),IB->GetExecPin());Link(IValid->GetReturnValuePin(),IB->GetConditionPin());
    auto* Container=Call(UPersonnelPreparationRoomWidget::StaticClass(),TEXT("GetWarehouseInventoryContainer"),2050,5000);
    auto* Register=Call(USIS_UnitInventoryComponent::StaticClass(),TEXT("RegisterInventorySlotContainer"),2100,4500);
    Register->NodeComment=TEXT("WarehouseInventoryInit_v1");
    Link(IB->GetThenPin(),Register->GetExecPin());Link(Inventory->GetValuePin(),Register->FindPinChecked(UEdGraphSchema_K2::PN_Self));Link(Container->GetReturnValuePin(),Register->FindPinChecked(TEXT("InventorySlotContainer")));
    auto* Refresh=Call(USIS_UnitInventoryComponent::StaticClass(),TEXT("LoadItemsToInventoryWidget"),2450,4500);Link(Register->GetThenPin(),Refresh->GetExecPin());Link(Inventory->GetValuePin(),Refresh->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    for(auto* P:Old){Link(Refresh->GetThenPin(),P);Link(Branch->GetElsePin(),P);Link(IB->GetElsePin(),P);}
    return OK && GuardWarehouseContainer(BP);
}

static bool GuardWarehouseContainer(UWidgetBlueprint* BP)
{
    UK2Node_CallFunction* Register=nullptr;
    for(UEdGraph* G:BP->UbergraphPages) for(UEdGraphNode* N:G->Nodes)
    {
        if(N->NodeComment==TEXT("WarehouseContainerGuard_v1")) return NormalizeWarehouseEmptyExport(BP);
        if(N->NodeComment==TEXT("WarehouseInventoryInit_v1")) Register=Cast<UK2Node_CallFunction>(N);
    }
    if(!Register) return false;
    auto* Input=Register->FindPinChecked(TEXT("InventorySlotContainer"));
    if(Input->LinkedTo.IsEmpty()) return false;
    auto* Graph=Register->GetGraph();const auto* S=GetDefault<UEdGraphSchema_K2>();bool OK=true;
    FGraphNodeCreator<UK2Node_CallFunction> VC(*Graph);auto* Valid=VC.CreateNode();Valid->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("IsValid")));Valid->NodePosX=2100;Valid->NodePosY=5200;VC.Finalize();
    OK=S->TryCreateConnection(Input->LinkedTo[0],Valid->FindPinChecked(TEXT("Object")))&&OK;
    FGraphNodeCreator<UK2Node_IfThenElse> BC(*Graph);auto* B=BC.CreateNode();B->NodePosX=2100;B->NodePosY=4300;B->NodeComment=TEXT("WarehouseContainerGuard_v1");BC.Finalize();
    auto Old=Register->GetExecPin()->LinkedTo;Register->GetExecPin()->BreakAllPinLinks();
    for(auto* P:Old) OK=S->TryCreateConnection(P,B->GetExecPin())&&OK;
    OK=S->TryCreateConnection(Valid->GetReturnValuePin(),B->GetConditionPin())&&OK;
    return S->TryCreateConnection(B->GetThenPin(),Register->GetExecPin()) && OK && NormalizeWarehouseEmptyExport(BP);
}

static bool NormalizeWarehouseEmptyExport(UWidgetBlueprint* BP)
{
    for(UEdGraph* G:BP->UbergraphPages) for(UEdGraphNode* N:G->Nodes)
    {
        auto* Export=Cast<UK2Node_CallFunction>(N);
        if(!Export || Export->FunctionReference.GetMemberName()!=TEXT("GetAllItemDatasFromInventoryComponent")) continue;
        if(Export->NodeComment==TEXT("SIS: Empty inventory is a valid empty page")) return true;
        if(Export->GetThenPin()->LinkedTo.IsEmpty()) continue;
        auto* Branch=Cast<UK2Node_IfThenElse>(Export->GetThenPin()->LinkedTo[0]->GetOwningNode());
        if(!Branch) continue;
        auto Next=Branch->GetThenPin()->LinkedTo;
        Export->GetThenPin()->BreakAllPinLinks();Branch->BreakAllNodeLinks();
        bool OK=true;
        for(auto* P:Next) OK=GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Export->GetThenPin(),P)&&OK;
        FBlueprintEditorUtils::RemoveNode(BP,Branch,true);
        Export->NodeComment=TEXT("SIS: Empty inventory is a valid empty page");Export->bCommentBubbleVisible=true;
        return OK;
    }
    return false;
}
