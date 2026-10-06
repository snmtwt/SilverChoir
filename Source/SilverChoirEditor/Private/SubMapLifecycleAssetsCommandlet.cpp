#include "SubMapLifecycleAssetsCommandlet.h"
#include "MainMapBlueprintBuilder.h"
#include "Misc/FileHelper.h"
#include "MTS_SubMapHandler.h"
#include "MTS_SubMapDataAsset.h"
#include "MTS_MapLoadingWidget.h"

int32 ApplySubMapLifecycleBlueprints();
int32 RefactorSubMapHandlerBlueprints();
int32 RunCameraStateOffsetTools(const FString& Params);
int32 RunLegacyMapConfigRemoval(const FString& Params);
int32 RunNativeGameHandlerMigration(const FString& Params);

USubMapLifecycleAssetsCommandlet::USubMapLifecycleAssetsCommandlet(){IsClient=false;IsEditor=true;LogToConsole=true;}
int32 USubMapLifecycleAssetsCommandlet::Main(const FString& Params)
{
    if(FParse::Param(*Params,TEXT("NativeGameHandler")))return RunNativeGameHandlerMigration(Params);
    if(FParse::Param(*Params,TEXT("LegacyMapConfig")))return RunLegacyMapConfigRemoval(Params);
    if(FParse::Param(*Params,TEXT("CameraOffsets")))return RunCameraStateOffsetTools(Params);
    if(FParse::Param(*Params,TEXT("Apply")))return ApplySubMapLifecycleBlueprints();
    if(FParse::Param(*Params,TEXT("RefactorHandlers")))return RefactorSubMapHandlerBlueprints();
    FString Dump;
    for(const TCHAR* Path:{TEXT("/Game/System/Map/BattleMap/T5/BP_SubMapHandler_T5"),
        TEXT("/Game/System/Map/BaseMap/UI/SceneUI/OperationsCommandRoom/WBP_OperationsCommandRoom"),
        TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController")})
    {
        auto* BP=LoadObject<UBlueprint>(nullptr,Path);if(!BP){Dump+=FString(Path)+TEXT(" MISSING\n");continue;}
        if(FParse::Param(*Params,TEXT("Validate")) && !MainMapBP::Compile(BP))return 1;
        Dump+=FString::Printf(TEXT("ASSET %s parent=%s\n"),Path,*GetNameSafe(BP->ParentClass));
        for(const auto& V:BP->NewVariables)Dump+=FString::Printf(TEXT("VAR %s default=%s\n"),*V.VarName.ToString(),*V.DefaultValue);
        TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
        for(auto* G:Graphs)for(UEdGraphNode* N:G->Nodes)
        {
            Dump+=FString::Printf(TEXT("NODE %s.%s %s\n"),*G->GetName(),*N->GetName(),*N->GetNodeTitle(ENodeTitleType::ListView).ToString());
            for(auto* Pin:N->Pins)
            {
                FString Links;for(auto* L:Pin->LinkedTo)Links+=L->GetOwningNode()->GetName()+TEXT(".")+L->PinName.ToString()+TEXT(" ");
                Dump+=FString::Printf(TEXT(" PIN %s default=%s object=%s links=%s\n"),*Pin->PinName.ToString(),*Pin->DefaultValue,*GetPathNameSafe(Pin->DefaultObject),*Links);
            }
        }
    }
    const FString File=FPaths::ProjectSavedDir()/TEXT("SubMapHandlerLifecycle/Inspect.txt");
    FFileHelper::SaveStringToFile(Dump,*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp,Display,TEXT("SUBMAP_LIFECYCLE_INSPECT_OK"));
    if(FParse::Param(*Params,TEXT("Validate")))UE_LOG(LogTemp,Display,TEXT("SUBMAP_LIFECYCLE_VALIDATE_OK"));
    return 0;
}
