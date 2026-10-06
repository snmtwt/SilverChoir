#include "MainMapBlueprintBuilder.h"
#include "FCS_FreeCameraPawn.h"
#include "Misc/FileHelper.h"
#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "MTS_MapTransitionBlueprintLibrary.h"
#include "SubSystem/PlayerSubSystem/PlayerLibrary.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"

namespace CameraStateOffsets
{
using namespace MainMapBP;
const TCHAR* NewGamePath=TEXT("/Game/System/Map/MainMenu/MapTransitionHandler/BP_MapTransitionHandler_NewGame");
const TCHAR* ScenePaths[] = {
    TEXT("/Game/System/Map/BaseMap/Scene/BP_基地全景_Scene"),
    TEXT("/Game/System/Map/BaseMap/Scene/BP_团长办公室_Scene"),
    TEXT("/Game/System/Map/BaseMap/Scene/BP_小队会议室_Scene"),
    TEXT("/Game/System/Map/BaseMap/Scene/BP_人员整备室_Scene"),
    TEXT("/Game/System/Map/BaseMap/Scene/BP_作战指挥室_Scene")
};
void Inspect(UBlueprint* BP, FString& Dump)
{
    Dump += FString::Printf(TEXT("ASSET %s parent=%s\n"),*BP->GetPathName(),*GetNameSafe(BP->ParentClass));
    for (TFieldIterator<FStructProperty> It(BP->GeneratedClass); It; ++It)
    {
        if(It->Struct != FFCS_CameraState::StaticStruct()) continue;
        FString Value;It->ExportText_InContainer(0,Value,BP->GeneratedClass->GetDefaultObject(),nullptr,nullptr,PPF_None);
        Dump+=FString::Printf(TEXT("CAMERA %s editable=%d transient=%d value=%s\n"),*It->GetName(),It->HasAnyPropertyFlags(CPF_Edit),It->HasAnyPropertyFlags(CPF_Transient),*Value);
    }
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    for(auto* G:Graphs)for(UEdGraphNode* N:G->Nodes)
    {
        if(auto* Entry=Cast<UK2Node_FunctionEntry>(N))for(const auto& V:Entry->LocalVariables)
            Dump+=FString::Printf(TEXT("LOCAL %s.%s value=%s\n"),*G->GetName(),*V.VarName.ToString(),*V.DefaultValue);
        Dump+=FString::Printf(TEXT("NODE %s.%s %s\n"),*G->GetName(),*N->GetName(),*N->GetNodeTitle(ENodeTitleType::ListView).ToString());
        for(auto* Pin:N->Pins)
        {
            FString Links;for(auto* L:Pin->LinkedTo)Links+=L->GetOwningNode()->GetName()+TEXT(".")+L->PinName.ToString()+TEXT(" ");
            Dump+=FString::Printf(TEXT(" PIN %s default=%s links=%s\n"),*Pin->PinName.ToString(),*Pin->DefaultValue,*Links);
        }
    }
}
void Backup(UBlueprint* BP)
{
    const FString File=FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    const FString Destination=FPaths::ProjectSavedDir()/TEXT("CameraStateTools/Backup/Assets")/(BP->GetName()+TEXT(".uasset"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination),true);
    if(!IFileManager::Get().FileExists(*Destination))check(IFileManager::Get().Copy(*Destination,*File)==COPY_OK);
}
bool Save(UBlueprint* BP)
{
    if(!Compile(BP))return false;
    BP->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    return UPackage::SavePackage(BP->GetOutermost(),BP,*FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args);
}
UK2Node_CallFunction* Call(UEdGraph* G,UClass* Class,FName Function,int32 X,int32 Y)
{
    FGraphNodeCreator<UK2Node_CallFunction> Creator(*G);auto* Node=Creator.CreateNode();
    auto* F=Class->FindFunctionByName(Function);check(F);Node->SetFromFunction(F);
    Node->NodePosX=X;Node->NodePosY=Y;Creator.Finalize();return Node;
}
FString CameraDefaults(UBlueprint* BP)
{
    FString Result;
    for(TFieldIterator<FStructProperty> It(BP->GeneratedClass);It;++It)if(It->Struct==FFCS_CameraState::StaticStruct())
    {FString Value;It->ExportText_InContainer(0,Value,BP->GeneratedClass->GetDefaultObject(),nullptr,nullptr,PPF_None);Result+=It->GetName()+Value;}
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    for(auto* G:Graphs)for(UEdGraphNode* N:G->Nodes)if(auto* E=Cast<UK2Node_FunctionEntry>(N))for(const auto& V:E->LocalVariables)
        if(V.VarType.PinSubCategoryObject.Get()==FFCS_CameraState::StaticStruct())Result+=G->GetName()+V.VarName.ToString()+V.DefaultValue;
    return Result;
}
void ApplyScene(UBlueprint* BP)
{
    const FString OriginalDefaults=CameraDefaults(BP);
    TArray<UEdGraphPin*> Inputs;TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    int32 Existing=0;
    for(auto* G:Graphs)for(UEdGraphNode* N:G->Nodes)if(auto* C=Cast<UK2Node_CallFunction>(N))
    {
        const FName Function=C->FunctionReference.GetMemberName();
        if(Function!=TEXT("MoveToCameraState") && Function!=TEXT("MoveFreeCameraToState"))continue;
        auto* Input=C->FindPin(Function==TEXT("MoveToCameraState")?TEXT("InCameraState"):TEXT("CameraState"));check(Input);
        check(Input->LinkedTo.Num()==1);
        auto* Source=Input->LinkedTo[0]->GetOwningNode();
        if(auto* Added=Cast<UK2Node_CallFunction>(Source);Added && Added->FunctionReference.GetMemberName()==GET_FUNCTION_NAME_CHECKED(UPlayerLibrary,AddCameraStateLocationOffset)){++Existing;continue;}
        // All ten inspected sources are authored member/local variables, never a captured world state.
        auto* Get=Cast<UK2Node_VariableGet>(Source);check(Get);
        const FName Name=Get->VariableReference.GetMemberName();
        check(Name==TEXT("移动到区域上方") || Name==TEXT("相机房间中位置") || Name==TEXT("OverheadCameraState") || Name==TEXT("InteriorCameraState") || Name==TEXT("ExitOverheadCameraState"));
        Inputs.Add(Input);
    }
    if(Inputs.IsEmpty()){check(Compile(BP));UE_LOG(LogTemp,Display,TEXT("CAMERA_OFFSETS_SCENE %s added=0 existing=%d"),*BP->GetName(),Existing);return;}
    Backup(BP);
    Var(BP,TEXT("CameraMapID"),Type(UEdGraphSchema_K2::PC_Name),TEXT("Base"),TEXT("相机所属地图ID"),true,TEXT("场景|相机"));
    check(Compile(BP));
    // Reacquire pins after the new member is reflected; compilation can reconstruct graph nodes.
    Inputs.Reset();Graphs.Reset();BP->GetAllGraphs(Graphs);
    for(auto* G:Graphs)for(UEdGraphNode* N:G->Nodes)if(auto* C=Cast<UK2Node_CallFunction>(N))
    {
        const FName Function=C->FunctionReference.GetMemberName();
        if(Function!=TEXT("MoveToCameraState") && Function!=TEXT("MoveFreeCameraToState"))continue;
        auto* Input=P(C,Function==TEXT("MoveToCameraState")?TEXT("InCameraState"):TEXT("CameraState"));check(Input && Input->LinkedTo.Num()==1);
        if(auto* ExistingOffset=Cast<UK2Node_CallFunction>(Input->LinkedTo[0]->GetOwningNode());ExistingOffset && ExistingOffset->FunctionReference.GetMemberName()==GET_FUNCTION_NAME_CHECKED(UPlayerLibrary,AddCameraStateLocationOffset))continue;
        Inputs.Add(Input);
    }
    for(auto* Input:Inputs)
    {
        auto* Move=Input->GetOwningNode();auto* G=Move->GetGraph();auto* Source=Input->LinkedTo[0];
        auto* Offset=Call(G,UPlayerLibrary::StaticClass(),GET_FUNCTION_NAME_CHECKED(UPlayerLibrary,AddCameraStateLocationOffset),Move->NodePosX-320,Move->NodePosY+380);
        auto* Location=Call(G,UMTS_MapTransitionBlueprintLibrary::StaticClass(),GET_FUNCTION_NAME_CHECKED(UMTS_MapTransitionBlueprintLibrary,GetMapLoadingLocation),Move->NodePosX-660,Move->NodePosY+540);
        FGraphNodeCreator<UK2Node_VariableGet> Creator(*G);auto* ID=Creator.CreateNode();ID->VariableReference.SetSelfMember(TEXT("CameraMapID"));ID->NodePosX=Move->NodePosX-950;ID->NodePosY=Move->NodePosY+540;Creator.Finalize();
        Link(ID->GetValuePin(),P(Location,TEXT("MapID")));
        Input->BreakAllPinLinks();Link(Source,P(Offset,TEXT("CameraState")));Link(P(Location,TEXT("OutLocation")),P(Offset,TEXT("LocationOffset")));Link(Offset->GetReturnValuePin(),Input);
        Offset->NodeComment=TEXT("相机配置保存地图内坐标，执行时只叠加一次实际加载位置。不要把已偏移的结果写回配置变量。");Offset->bCommentBubbleVisible=true;
    }
    check(Compile(BP));checkf(CameraDefaults(BP)==OriginalDefaults,TEXT("Authored camera settings changed: %s"),*BP->GetName());check(Save(BP));
    UE_LOG(LogTemp,Display,TEXT("CAMERA_OFFSETS_SCENE %s added=%d existing=%d"),*BP->GetName(),Inputs.Num(),Existing);
}
void ApplyInitialEntry(UBlueprint* BP)
{
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);int32 Count=0;
    for(auto* G:Graphs)for(UEdGraphNode* N:TArray<UEdGraphNode*>(G->Nodes))if(auto* Activate=Cast<UK2Node_CallFunction>(N))
    {
        if(Activate->FunctionReference.GetMemberName()!=GET_FUNCTION_NAME_CHECKED(AGameMainMapGameMode,ActivateLoadedMap))continue;
        ++Count;auto* Input=P(Activate,TEXT("EntryTransform"));check(Input);
        if(Activate->NodeComment==TEXT("MapEntryOffsetV1"))continue;
        check(Input->LinkedTo.IsEmpty() && Input->SubPins.IsEmpty());Backup(BP);
        FTransform LocalEntry=FTransform::Identity;
        if(!Input->DefaultValue.IsEmpty())check(TBaseStructure<FTransform>::Get()->ImportText(*Input->DefaultValue,&LocalEntry,nullptr,PPF_None,GWarn,TEXT("InitialEntry")));
        auto* Local=Call(G,UKismetMathLibrary::StaticClass(),TEXT("MakeTransform"),Activate->NodePosX-1120,Activate->NodePosY+350);
        const auto VectorDefault=[](FVector V){return FString::Printf(TEXT("%.9f,%.9f,%.9f"),V.X,V.Y,V.Z);};
        D(Local,TEXT("Location"),VectorDefault(LocalEntry.GetLocation()));
        const FRotator Rotation=LocalEntry.Rotator();
        D(Local,TEXT("Rotation"),FString::Printf(TEXT("%.9f,%.9f,%.9f"),Rotation.Pitch,Rotation.Yaw,Rotation.Roll));
        D(Local,TEXT("Scale"),VectorDefault(LocalEntry.GetScale3D()));
        Local->NodeComment=TEXT("初始入场变换（地图内坐标）；实际加载位置由右侧节点叠加。");Local->bCommentBubbleVisible=true;
        auto* Break=Call(G,UKismetMathLibrary::StaticClass(),TEXT("BreakTransform"),Activate->NodePosX-780,Activate->NodePosY+350);
        Link(Local->GetReturnValuePin(),P(Break,TEXT("InTransform")));
        auto* Location=Call(G,UMTS_MapTransitionBlueprintLibrary::StaticClass(),GET_FUNCTION_NAME_CHECKED(UMTS_MapTransitionBlueprintLibrary,GetMapLoadingLocation),Activate->NodePosX-780,Activate->NodePosY+650);
        auto* ID=P(Activate,TEXT("MapID"));if(ID->LinkedTo.Num()==1)Link(ID->LinkedTo[0],P(Location,TEXT("MapID")));else D(Location,TEXT("MapID"),ID->DefaultValue);
        auto* Add=Call(G,UKismetMathLibrary::StaticClass(),TEXT("Add_VectorVector"),Activate->NodePosX-430,Activate->NodePosY+470);
        Link(P(Break,TEXT("Location")),P(Add,TEXT("A")));Link(P(Location,TEXT("OutLocation")),P(Add,TEXT("B")));
        auto* Make=Call(G,UKismetMathLibrary::StaticClass(),TEXT("MakeTransform"),Activate->NodePosX-100,Activate->NodePosY+400);
        Link(Add->GetReturnValuePin(),P(Make,TEXT("Location")));Link(P(Break,TEXT("Rotation")),P(Make,TEXT("Rotation")));Link(P(Break,TEXT("Scale")),P(Make,TEXT("Scale")));Link(Make->GetReturnValuePin(),Input);
        Activate->NodeComment=TEXT("MapEntryOffsetV1");check(Save(BP));
    }
    check(Count==1);
}
}
int32 RunCameraStateOffsetTools(const FString& Params)
{
    FString Dump;
    for(const auto* Path:CameraStateOffsets::ScenePaths)
    {
        auto* BP=LoadObject<UBlueprint>(nullptr,Path);check(BP);
        if(FParse::Param(*Params,TEXT("Apply")))CameraStateOffsets::ApplyScene(BP);
        if(FParse::Param(*Params,TEXT("Validate")))check(MainMapBP::Compile(BP));
        CameraStateOffsets::Inspect(BP,Dump);
    }
    auto* Handler=LoadObject<UBlueprint>(nullptr,CameraStateOffsets::NewGamePath);check(Handler);
    if(FParse::Param(*Params,TEXT("Apply")))CameraStateOffsets::ApplyInitialEntry(Handler);
    if(FParse::Param(*Params,TEXT("Validate")))check(MainMapBP::Compile(Handler));
    CameraStateOffsets::Inspect(Handler,Dump);
    FFileHelper::SaveStringToFile(Dump,*(FPaths::ProjectSavedDir()/TEXT("CameraStateTools/Inspect.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("CAMERA_OFFSETS_INSPECT_OK"));
    if(FParse::Param(*Params,TEXT("Apply")))UE_LOG(LogTemp,Display,TEXT("CAMERA_OFFSETS_APPLY_OK"));
    if(FParse::Param(*Params,TEXT("Validate")))UE_LOG(LogTemp,Display,TEXT("CAMERA_OFFSETS_VALIDATE_OK"));
    return 0;
}
