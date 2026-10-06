#include "MainMapBlueprintBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"

namespace LegacyMapConfigRemoval
{
using namespace MainMapBP;
const TCHAR* ConfigPackage=TEXT("/Game/System/Map/GameMainMap/Data/DA_GameMainMapConfig");
const TCHAR* ModePath=TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapGameMode");
const TCHAR* HandlerPath=TEXT("/Game/System/Map/BattleMap/T5/BP_SubMapHandler_T5");

bool Save(UBlueprint* BP)
{
    if(!Compile(BP))return false;
    BP->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    return UPackage::SavePackage(BP->GetOutermost(),BP,*FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()),Args);
}
void Detach()
{
    auto* Mode=LoadObject<UBlueprint>(nullptr,ModePath);check(Mode);
    if(auto* Config=FindFProperty<FObjectPropertyBase>(Mode->GeneratedClass,TEXT("MapConfig")))
        Config->SetObjectPropertyValue_InContainer(Mode->GeneratedClass->GetDefaultObject(),nullptr);
    check(Save(Mode));
    auto* Handler=LoadObject<UBlueprint>(nullptr,HandlerPath);check(Handler);
    TArray<UEdGraph*> Graphs;Handler->GetAllGraphs(Graphs);
    int32 Removed=0;
    for(auto* Graph:Graphs)for(UEdGraphNode* N:TArray<UEdGraphNode*>(Graph->Nodes))
        if(auto* Call=Cast<UK2Node_CallFunction>(N);Call && Call->FunctionReference.GetMemberName()==TEXT("ReturnPlayerToBase"))
        {
            // Remove only the inspected error-display chain for the retired return operation.
            auto* Execute=P(Call,TEXT("execute"));check(Execute->LinkedTo.Num()==1);
            auto* Event=Cast<UK2Node_Event>(Execute->LinkedTo[0]->GetOwningNode());
            check(Event && Event->EventReference.GetMemberName()==TEXT("OnSubMapUnloading"));
            auto* Then=P(Call,TEXT("then"));check(Then->LinkedTo.Num()==1);
            auto* Branch=Cast<UK2Node_IfThenElse>(Then->LinkedTo[0]->GetOwningNode());check(Branch && Branch->GetThenPin()->LinkedTo.IsEmpty());
            check(Branch->GetElsePin()->LinkedTo.Num()==1);
            auto* Print=Cast<UK2Node_CallFunction>(Branch->GetElsePin()->LinkedTo[0]->GetOwningNode());
            check(Print && Print->FunctionReference.GetMemberName()==TEXT("PrintText") && P(Print,TEXT("then"))->LinkedTo.IsEmpty());
            auto* Error=P(Call,TEXT("OutError"));check(Error->LinkedTo.Num()==1 && Error->LinkedTo[0]->GetOwningNode()==Print);
            for(auto* Node:{static_cast<UEdGraphNode*>(Print),static_cast<UEdGraphNode*>(Branch),static_cast<UEdGraphNode*>(Call)})
                FBlueprintEditorUtils::RemoveNode(Handler,Node,true);
            Event->NodeComment=TEXT("卸载时释放本地图资源。返回基地流程后续单独实现；此处不自动切换活动地图。");Event->bCommentBubbleVisible=true;
            ++Removed;
        }
    check(Save(Handler));
    UE_LOG(LogTemp,Display,TEXT("LEGACY_MAP_CONFIG_DETACHED removedReturnChains=%d"),Removed);
}
int32 Audit(bool bValidate)
{
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FName> Referencers;Registry.GetReferencers(FName(ConfigPackage),Referencers);
    FString Dump;
    for(auto Ref:Referencers)Dump+=TEXT("CONFIG_REFERENCER ")+Ref.ToString()+TEXT("\n");
    TArray<FAssetData> Assets;Registry.GetAssetsByPath(TEXT("/Game"),Assets,true);
    int32 Blueprints=0,References=0;
    for(const auto& Asset:Assets)
    {
        if(!Asset.AssetClassPath.GetAssetName().ToString().EndsWith(TEXT("Blueprint")))continue;
        auto* BP=Cast<UBlueprint>(Asset.GetAsset());if(!BP)continue;++Blueprints;
        TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
        for(auto* G:Graphs)for(UEdGraphNode* N:G->Nodes)
        {
            FName Name;
            if(auto* C=Cast<UK2Node_CallFunction>(N))
            {
                const FName Function=C->FunctionReference.GetMemberName();
                if(Function==TEXT("EnterBattle") || Function==TEXT("UnloadInactiveBattle") || Function==TEXT("ReturnPlayerToBase"))Name=Function;
            }
            if(auto* V=Cast<UK2Node_Variable>(N))
            {
                const FName Variable=V->VariableReference.GetMemberName();
                UClass* Owner=V->VariableReference.GetMemberParentClass();
                if((Variable==TEXT("MapConfig") && Owner && Owner->IsChildOf(AGameMainMapGameMode::StaticClass())) || Variable==TEXT("PendingMapID"))Name=Variable;
            }
            for(auto* Pin:N->Pins)if(auto* Type=Pin->PinType.PinSubCategoryObject.Get();Type && (Type->GetFName()==TEXT("GameMainMapConfig") || Type->GetFName()==TEXT("GameMainMapEntry")))Name=Type->GetFName();
            if(!Name.IsNone()){++References;Dump+=FString::Printf(TEXT("LEGACY_NODE %s %s.%s %s\n"),*BP->GetPathName(),*G->GetName(),*N->GetName(),*Name.ToString());}
        }
        if(bValidate && Asset.PackageName.ToString().StartsWith(TEXT("/Game/System/Map/")))check(Compile(BP));
    }
    const FString File=FPaths::ProjectSavedDir()/TEXT("RemoveLegacyMapConfig/Audit.txt");
    FFileHelper::SaveStringToFile(Dump,*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("LEGACY_MAP_CONFIG_AUDIT blueprints=%d legacyNodes=%d configReferencers=%d"),Blueprints,References,Referencers.Num());
    if(bValidate)check(References==0 && Referencers.IsEmpty());
    return References;
}
}
int32 RunLegacyMapConfigRemoval(const FString& Params)
{
    if(FParse::Param(*Params,TEXT("Detach")))LegacyMapConfigRemoval::Detach();
    LegacyMapConfigRemoval::Audit(FParse::Param(*Params,TEXT("Validate")));
    UE_LOG(LogTemp,Display,TEXT("LEGACY_MAP_CONFIG_TOOL_OK"));return 0;
}
