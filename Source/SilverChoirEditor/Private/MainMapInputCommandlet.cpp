#include "MainMapInputCommandlet.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_Self.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_EnhancedInputAction.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "FCS_FreeCameraPawn.h"
#include "SubSystem/PlayerSubSystem/PlayerLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

int32 UMainMapInputCommandlet::Main(const FString& Params)
{
    auto* BP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController"));
    auto* Action=LoadObject<UInputAction>(nullptr,TEXT("/Game/System/Input/Common/IA_MouseMiddle"));
    if (!BP || !Action || BP->UbergraphPages.IsEmpty()) return 1;
    if (BP->FunctionGraphs.ContainsByPredicate([](const UEdGraph* G) { return G->GetFName() == TEXT("CommandMap_QueryPointer"); }))
    { UE_LOG(LogTemp,Display,TEXT("MAIN_MAP_INPUT_OWNED_BY_ARCHITECTURE")); return 0; }
    UEdGraph* G=BP->UbergraphPages[0];
    for (UEdGraphNode* N:G->Nodes) if (N->NodeComment==TEXT("MiddleMouseCamera_v1"))
    { UE_LOG(LogTemp,Display,TEXT("MIDDLE_MOUSE_ALREADY_CONFIGURED"));return 0; }
    const auto* S=GetDefault<UEdGraphSchema_K2>(); bool OK=true;
    auto Link=[&](UEdGraphPin* A,UEdGraphPin* B) { if (!A || !B || !S->TryCreateConnection(A,B)) { OK=false;UE_LOG(LogTemp,Error,TEXT("Input graph connection failed")); } };
    auto Call=[&](UClass* C,FName Name,int X,int Y)
    {
        FGraphNodeCreator<UK2Node_CallFunction> Creator(*G);auto* N=Creator.CreateNode();
        N->SetFromFunction(C->FindFunctionByName(Name));N->NodePosX=X;N->NodePosY=Y;Creator.Finalize();return N;
    };
    auto Event=[&](FName Name,int Y)
    {
        for (UEdGraphNode* N:G->Nodes) if (auto* E=Cast<UK2Node_Event>(N)) if (E->EventReference.GetMemberName()==Name) return E;
        FGraphNodeCreator<UK2Node_Event> Creator(*G);auto* N=Creator.CreateNode();
        N->EventReference.SetExternalMember(Name,AActor::StaticClass());N->bOverrideFunction=true;N->NodePosX=0;N->NodePosY=Y;Creator.Finalize();return N;
    };
    auto Append=[&](UK2Node_Event* E,UEdGraphPin* Start,int Y)
    {
        auto* Out=E->FindPinChecked(UEdGraphSchema_K2::PN_Then);auto Old=Out->LinkedTo;Out->BreakAllPinLinks();
        FGraphNodeCreator<UK2Node_ExecutionSequence> Creator(*G);auto* Seq=Creator.CreateNode();Seq->NodePosX=250;Seq->NodePosY=Y;Creator.Finalize();
        Link(Out,Seq->GetExecPin());Link(Seq->GetThenPinGivenIndex(0),Start);
        for (auto* P:Old) Link(Seq->GetThenPinGivenIndex(1),P);
    };
    auto Guard=[&](UEdGraphPin* Object,int X,int Y)
    {
        auto* Valid=Call(UKismetSystemLibrary::StaticClass(),TEXT("IsValid"),X,Y+150);Link(Object,Valid->FindPinChecked(TEXT("Object")));
        FGraphNodeCreator<UK2Node_IfThenElse> Creator(*G);auto* B=Creator.CreateNode();B->NodePosX=X;B->NodePosY=Y;Creator.Finalize();
        Link(Valid->GetReturnValuePin(),B->GetConditionPin());return B;
    };
    FGraphNodeCreator<UK2Node_Self> SelfCreator(*G);auto* Self=SelfCreator.CreateNode();Self->NodePosX=0;Self->NodePosY=1000;SelfCreator.Finalize();
    FGraphNodeCreator<UK2Node_GetSubsystemFromPC> SubCreator(*G);auto* Sub=SubCreator.CreateNode();Sub->Initialize(UEnhancedInputLocalPlayerSubsystem::StaticClass());Sub->NodePosX=250;Sub->NodePosY=1050;SubCreator.Finalize();
    Link(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self),Sub->FindPinChecked(TEXT("PlayerController")));
    FGraphNodeCreator<UK2Node_VariableGet> VarCreator(*G);auto* Mapping=VarCreator.CreateNode();Mapping->VariableReference.SetSelfMember(TEXT("CommonInputMappingContext"));Mapping->NodePosX=650;Mapping->NodePosY=1100;VarCreator.Finalize();
    auto* Add=Call(UEnhancedInputLocalPlayerSubsystem::StaticClass(),TEXT("AddMappingContext"),1000,800);
    Link(Sub->GetResultPin(),Add->FindPinChecked(UEdGraphSchema_K2::PN_Self));Link(Mapping->GetValuePin(),Add->FindPinChecked(TEXT("MappingContext")));
    auto* SubGuard=Guard(Sub->GetResultPin(),550,800);Link(SubGuard->GetThenPin(),Add->GetExecPin());
    Append(Event(TEXT("ReceiveBeginPlay"),800),SubGuard->GetExecPin(),800);
    FGraphNodeCreator<UK2Node_EnhancedInputAction> InputCreator(*G);auto* Input=InputCreator.CreateNode();Input->InputAction=Action;Input->NodePosX=0;Input->NodePosY=1450;Input->NodeComment=TEXT("MiddleMouseCamera_v1");InputCreator.Finalize();
    auto Rotate=[&](bool Enable,int Y)
    {
        auto* Camera=Call(UPlayerLibrary::StaticClass(),TEXT("GetPlayerCamera"),350,Y+300);
        auto* GuardNode=Guard(Camera->GetReturnValuePin(),650,Y);
        auto* R=Call(AFCS_FreeCameraPawn::StaticClass(),TEXT("TriggerMouseRotate"),1000,Y);
        R->FindPinChecked(TEXT("bEnable"))->DefaultValue=Enable?TEXT("true"):TEXT("false");
        Link(Camera->GetReturnValuePin(),R->FindPinChecked(UEdGraphSchema_K2::PN_Self));Link(GuardNode->GetThenPin(),R->GetExecPin());return GuardNode->GetExecPin();
    };
    Link(Input->FindPinChecked(TEXT("Started")),Rotate(true,1450));
    auto* Stop=Rotate(false,2050);
    Link(Input->FindPinChecked(TEXT("Completed")),Stop);Link(Input->FindPinChecked(TEXT("Canceled")),Stop);
    Append(Event(TEXT("ReceiveEndPlay"),2700),Rotate(false,2700),2700);
    if (!OK) return 2;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status==BS_Error) return 3;
    const FString File=FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    const FString Backup=FPaths::ProjectSavedDir()/TEXT("InputSetupBackups")/FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
    IFileManager::Get().MakeDirectory(*Backup,true);
    if (IFileManager::Get().Copy(*(Backup/FPaths::GetCleanFilename(File)),*File)!=COPY_OK) return 4;
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    if (!UPackage::SavePackage(BP->GetOutermost(),BP,*File,Args)) return 5;
    UE_LOG(LogTemp,Display,TEXT("MIDDLE_MOUSE_CONFIGURED: Started -> rotate; Completed/Canceled/EndPlay -> stop"));
    return 0;
}

