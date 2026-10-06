#include "CharacterMigrationCommandlet.h"
#include "Object/Unit/UnitPawnBase.h"
#include "Object/Unit/UnitAIController.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BrushComponent.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Level.h"
#include "Builders/CubeBuilder.h"
#include "ActorFactories/ActorFactory.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "FileHelpers.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_EnhancedInputAction.h"
#include "K2Node_CallFunction.h"
#include "EdGraphNode_Comment.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"

namespace CharacterMigration
{
bool Save(UObject* Asset)
{
    const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),Asset->IsA<UWorld>()?FPackageName::GetMapPackageExtension():FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args);
}
bool Backup(UObject* Asset)
{
    FString File;
    if(!FPackageName::DoesPackageExist(Asset->GetOutermost()->GetName(),&File))return true;
    const FString Folder=FPaths::ProjectSavedDir()/TEXT("CharacterMigration/TargetAssetBackup")/FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
    IFileManager::Get().MakeDirectory(*Folder,true);
    return IFileManager::Get().Copy(*(Folder/FPaths::GetCleanFilename(File)),*File)==COPY_OK;
}
template<class T,class F> T* Node(UEdGraph* G,int32 X,int32 Y,F Setup)
{
    FGraphNodeCreator<T> C(*G);T* N=C.CreateNode();Setup(N);N->NodePosX=X;N->NodePosY=Y;C.Finalize();return N;
}
bool SetupController()
{
    const TCHAR* ActionPackage=TEXT("/Game/System/Input/Battle/IA_BattleMove");
    auto* Action=LoadObject<UInputAction>(nullptr,TEXT("/Game/System/Input/Battle/IA_BattleMove.IA_BattleMove"),nullptr,LOAD_NoWarn);
    if(!Action)
    {
        auto* P=CreatePackage(ActionPackage);Action=NewObject<UInputAction>(P,TEXT("IA_BattleMove"),RF_Public|RF_Standalone);
        Action->ValueType=EInputActionValueType::Boolean;FAssetRegistryModule::AssetCreated(Action);
        if(!Save(Action))return false;
    }
    auto* Mapping=LoadObject<UInputMappingContext>(nullptr,TEXT("/Game/System/Input/Battle/IMC_Battle.IMC_Battle"));
    auto* BP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController"));
    if(!Mapping||!BP||!Backup(Mapping)||!Backup(BP))return false;
    bool bMapped=false;
    for(const auto& M:Mapping->GetMappings())if(M.Action==Action&&M.Key==EKeys::RightMouseButton)bMapped=true;
    if(!bMapped)Mapping->MapKey(Action,EKeys::RightMouseButton);
    if(!Save(Mapping))return false;
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    for(auto* G:Graphs)if(G->GetFName()==TEXT("BattleMoveOrders"))return true;
    auto* G=FBlueprintEditorUtils::CreateNewGraph(BP,TEXT("BattleMoveOrders"),UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(BP,G);
    auto* Input=Node<UK2Node_EnhancedInputAction>(G,0,0,[&](auto* N){N->InputAction=Action;});
    auto* Call=Node<UK2Node_CallFunction>(G,350,0,[&](auto* N){N->SetFromFunction(AGameMainMapPlayerController::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(AGameMainMapPlayerController,MoveSelectedUnitsToCursor)));});
    if(!GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Input->FindPin(TEXT("Started")),Call->GetExecPin()))return false;
    auto* Comment=NewObject<UEdGraphNode_Comment>(G);G->AddNode(Comment);Comment->CreateNewGuid();
    Comment->NodePosX=-20;Comment->NodePosY=-200;Comment->NodeWidth=900;Comment->NodeHeight=170;
    Comment->NodeComment=TEXT("战斗右键下达移动命令。左键仍用于点选/框选。\n输入由蓝图路由，C++ 节点负责 UI/地图过滤、导航投影和所选单位编队目标。\n返回值是接受命令的单位数量；OutError 可接提示界面。");
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
    return BP->Status!=BS_Error&&Save(BP);
}
bool SetupUnit()
{
    auto* BP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/System/Object/Unit/Character/BP_UnitPawn"));
    auto* Anim=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/System/Object/Unit/Character/Animation/Female/ABP_Famale"));
    if(!BP||!Anim||!Backup(BP))return false;
    FKismetEditorUtilities::CompileBlueprint(Anim);
    if(Anim->Status==BS_Error)return false;
    auto* Unit=Cast<AUnitPawnBase>(BP->GeneratedClass->GetDefaultObject());
    if(!Unit||!Unit->MeshComponent)return false;
    Unit->MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    Unit->MeshComponent->SetAnimInstanceClass(Anim->GeneratedClass);
    Unit->AIControllerClass=AUnitAIController::StaticClass();Unit->AutoPossessAI=EAutoPossessAI::PlacedInWorldOrSpawned;
    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
    return BP->Status!=BS_Error&&Save(BP);
}
bool SetupMap(const FString& Path)
{
    UWorld* World=UEditorLoadingAndSavingUtils::LoadMap(Path);
    if(!World||!Backup(World))return false;
    // Invoke through the Python editor commandlet so Landscape editor modules are initialized.
    World->UpdateWorldComponents(true,false);
    FBox FloorBounds(ForceInit);
    for(AActor* Actor:World->PersistentLevel->Actors)
    {
        if(!IsValid(Actor))continue;
        Actor->RegisterAllComponents();
        const FString ClassName=Actor->GetClass()->GetName();
        if(!Actor->IsA<AStaticMeshActor>()&&ClassName!=TEXT("Landscape")&&ClassName!=TEXT("LandscapeStreamingProxy"))continue;
        FVector Center,Extent;Actor->GetActorBounds(false,Center,Extent);
        const FBox B(Center-Extent,Center+Extent);
        UE_LOG(LogTemp,Display,TEXT("CHARACTER_GROUND %s bounds=%s"),*Actor->GetName(),*B.ToString());
        if(B.IsValid&&B.GetSize().X>500&&B.GetSize().Y>500&&B.GetSize().Z<500)FloorBounds+=B;
    }
    if(!FloorBounds.IsValid){UE_LOG(LogTemp,Error,TEXT("CHARACTER_NAV_NO_FLOOR %s"),*Path);return false;}
    ANavMeshBoundsVolume* Volume=nullptr;
    for(AActor* Actor:World->PersistentLevel->Actors)if(auto* Existing=Cast<ANavMeshBoundsVolume>(Actor)){Volume=Existing;break;}
    if(!Volume || Volume->GetComponentsBoundingBox(true).GetExtent().IsNearlyZero())
    {
        if(!Volume) Volume=World->SpawnActor<ANavMeshBoundsVolume>();
        Volume->SetActorLabel(TEXT("BattleNavigationBounds"));
        auto* Builder=NewObject<UCubeBuilder>();
        Builder->X=FloorBounds.GetSize().X;Builder->Y=FloorBounds.GetSize().Y;Builder->Z=1000;
        UActorFactory::CreateBrushForVolumeActor(Volume,Builder);
        Volume->SetActorLocation(FloorBounds.GetCenter()+FVector(0,0,350));
        Volume->GetBrushComponent()->SetCanEverAffectNavigation(false);
    }
    if(Volume->GetComponentsBoundingBox(true).GetExtent().IsNearlyZero()) return false;
    UE_LOG(LogTemp,Display,TEXT("CHARACTER_NAV_BOUNDS %s center=%s extent=%s"),*Path,*Volume->GetActorLocation().ToString(),*Volume->GetComponentsBoundingBox(true).GetExtent().ToString());
    if(auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))Nav->OnNavigationBoundsUpdated(Volume);
    return UEditorLoadingAndSavingUtils::SaveMap(World,Path);
}
}
UCharacterMigrationCommandlet::UCharacterMigrationCommandlet(){IsClient=false;IsEditor=true;LogToConsole=true;}
bool UCharacterMigrationCommandlet::SetupBattleNavigation()
{
    return CharacterMigration::SetupMap(TEXT("/Game/System/Map/BattleMap/L_BattleTemplate"))
        && CharacterMigration::SetupMap(TEXT("/Game/System/Map/BattleMap/T5/L_T5"));
}
int32 UCharacterMigrationCommandlet::Main(const FString& Params)
{
    if(!FParse::Param(*Params,TEXT("MapsOnly"))&&(!CharacterMigration::SetupUnit()||!CharacterMigration::SetupController()))return 1;
    if(!CharacterMigration::SetupMap(TEXT("/Game/System/Map/BattleMap/L_BattleTemplate")))return 2;
    if(!CharacterMigration::SetupMap(TEXT("/Game/System/Map/BattleMap/T5/L_T5")))return 3;
    UE_LOG(LogTemp,Display,TEXT("CHARACTER_MIGRATION_SETUP_PASS"));return 0;
}
