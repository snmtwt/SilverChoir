#include "MainMapBlueprintBuilder.h"
#include "MTS_SubMapBlueprintLibrary.h"
#include "MTS_SubMapHandler.h"
#include "MTS_MapLoadingWidget.h"
#include "SMS_SceneLibrary.h"
#include "BlueprintGameplayTagLibrary.h"
#include "Map/GameMainMap/GameMainMapLibrary.h"
#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "SubSystem/GameMapTransitionSystem/GameMainMapSubMapHandler.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"

namespace SubMapLifecycleBP
{
using namespace MainMapBP;
const TCHAR* HandlerPath=TEXT("/Game/System/Map/BattleMap/T5/BP_SubMapHandler_T5");
const TCHAR* RoomPath=TEXT("/Game/System/Map/BaseMap/UI/SceneUI/OperationsCommandRoom/WBP_OperationsCommandRoom");
const TCHAR* RegionTag=TEXT("Map.Region.Tile");
void Backup(UObject* Asset)
{
    const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    if(!IFileManager::Get().FileExists(*File))return;
    const FString Dest=FPaths::ProjectSavedDir()/TEXT("SubMapHandlerLifecycle/Backup/Assets")/Asset->GetOutermost()->GetName().Mid(6)+TEXT(".uasset");
    if(IFileManager::Get().FileExists(*Dest))return;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Dest),true);
    check(IFileManager::Get().Copy(*Dest,*File)==COPY_OK);
}
bool Save(UObject* Asset)
{
    if(auto* Widget=Cast<UWidgetBlueprint>(Asset))
    {
        TSet<FName> Names;Widget->ForEachSourceWidget([&](UWidget* W){Names.Add(W->GetFName());});
        for(const auto& A:Widget->Animations)if(A)Names.Add(A->GetFName());
        for(FName N:Names)if(!Widget->WidgetVariableNameToGuidMap.Contains(N))Widget->WidgetVariableNameToGuidMap.Add(N,FGuid::NewGuid());
        for(auto It=Widget->WidgetVariableNameToGuidMap.CreateIterator();It;++It)if(!Names.Contains(It.Key()))It.RemoveCurrent();
    }
    if(auto* BP=Cast<UBlueprint>(Asset))if(!Compile(BP))return false;
    if(!OK)return false;
    Asset->MarkPackageDirty();
    const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args);
}
template<class T> FString Literal(UScriptStruct* S,const T& Value)
{FString Text;S->ExportText(Text,&Value,nullptr,nullptr,PPF_None,nullptr);return Text;}
UEdGraph* Events(UBlueprint* BP,FName Name)
{
    auto* G=Find(BP,Name);if(G)return G;
    G=FBlueprintEditorUtils::CreateNewGraph(BP,Name,UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(BP,G);return G;
}
UK2Node_Event* Event(FGraph& G,UClass* Owner,FName Name)
{
    return G.Node<UK2Node_Event>([&](auto* E){E->EventReference.SetExternalMember(Name,Owner);E->bOverrideFunction=true;});
}
UK2Node_CallFunction* WorldCall(FGraph& G,UClass* Owner,FName Name)
{
    auto* N=G.Call(Owner,Name);if(auto* World=N->FindPin(TEXT("WorldContextObject")))Link(G.Self(),World);return N;
}
UEdGraphPin* Manager(FGraph& G){return G.Get(TEXT("SceneManager"));}
UEdGraphPin* OwnID(FGraph& G){return G.Call(UMTS_SubMapHandler::StaticClass(),TEXT("GetMapID"))->GetReturnValuePin();}
UEdGraphPin* GameMode(FGraph& G){return WorldCall(G,UGameMainMapLibrary::StaticClass(),TEXT("GetGameMainMapGameMode"))->GetReturnValuePin();}
UEdGraphPin* TagEqual(FGraph& G,UEdGraphPin* A,UEdGraphPin* B)
{auto* N=G.Call(UBlueprintGameplayTagLibrary::StaticClass(),TEXT("EqualEqual_GameplayTag"));Link(A,P(N,TEXT("A")));Link(B,P(N,TEXT("B")));return N->GetReturnValuePin();}
UEdGraphPin* MapInfo(FGraph& G,UEdGraphPin* ID)
{auto* N=WorldCall(G,UMTS_SubMapBlueprintLibrary::StaticClass(),TEXT("GetSubMapByKey"));Link(ID,P(N,TEXT("Key")));return P(N,TEXT("OutMap"));}
void PrintFailure(FGraph& G,UEdGraphPin* From,UEdGraphPin* Error)
{
    auto* Print=WorldCall(G,UKismetSystemLibrary::StaticClass(),TEXT("PrintText"));Link(Error,P(Print,TEXT("InText")));D(Print,TEXT("Duration"),TEXT("8"));G.Exec(From,Print);
}
void CreateHandlerBlueprint(UBlueprint* BP,UMTS_SubMapDataAsset* Asset)
{
    // The source handler was empty. Re-running the commandlet preserves a developed Blueprint.
    if(Find(BP,TEXT("PrepareTileEntry")))return;
    Backup(BP);
    FMTS_SubMapLoadRequest Request;Request.MapAsset=Asset;Request.Location=FVector(100000,0,0);
    Request.LoadingWidgetClass=LoadClass<UMTS_MapLoadingWidget>(nullptr,TEXT("/Game/System/UIBasic/WBP_MenuLoading.WBP_MenuLoading_C"));check(Request.LoadingWidgetClass);
    Request.LoadingTitle=FText::FromString(TEXT("进入 T5 区域"));
    const FTransform Entry(FQuat::Identity,FVector(0,0,200));
    Var(BP,TEXT("LoadRequest"),Struct(FMTS_SubMapLoadRequest::StaticStruct()),Literal(FMTS_SubMapLoadRequest::StaticStruct(),Request),TEXT("子地图加载请求"),true,TEXT("T5|配置"));
    Var(BP,TEXT("LocalEntryTransform"),Struct(TBaseStructure<FTransform>::Get()),Entry.ToString(),TEXT("子地图局部进入点"),true,TEXT("T5|配置"));
    Var(BP,TEXT("OverviewSceneTag"),Struct(FGameplayTag::StaticStruct()),TEXT("(TagName=\"GameScene.BaseOverview\")"),TEXT("离开作战室后的场景"),true,TEXT("T5|配置"));
    Var(BP,TEXT("SquadIds"),Type(UEdGraphSchema_K2::PC_Struct,TBaseStructure<FGuid>::Get(),EPinContainerType::Array),TEXT(""),TEXT("本次进入的小队"),false,TEXT("T5|运行状态"));
    Var(BP,TEXT("SceneManager"),Obj(USMS_SceneManager::StaticClass()),TEXT(""),TEXT("场景管理器"));
    Var(BP,TEXT("LastLoadError"),Type(UEdGraphSchema_K2::PC_Text),TEXT(""),TEXT("最近一次加载错误"));
    for(FName Name:{TEXT("PrepareTileEntry"),TEXT("CommitTileEntry"),TEXT("ActivateTileMap"),TEXT("ReleaseSceneBinding")})Declare(BP,Name);
    Declare(BP,TEXT("OnOverviewReady"),{{TEXT("PreviousSceneTag"),Struct(FGameplayTag::StaticStruct()),{}},{TEXT("CurrentSceneTag"),Struct(FGameplayTag::StaticStruct()),{}}});
    check(Compile(BP));
    {
        FGraph G(BP,TEXT("ReleaseSceneBinding"));
        auto* Valid=G.Branch(G.Start(),G.Valid(Manager(G)));
        auto* End=G.Bind(Valid->GetThenPin(),USMS_SceneManager::StaticClass(),Manager(G),TEXT("OnSceneChanged"),TEXT("OnOverviewReady"),true);
        G.Set(End,TEXT("SceneManager"));
    }
    {
        FGraph G(BP,TEXT("PrepareTileEntry"));
        auto* Get=WorldCall(G,USMS_SceneLibrary::StaticClass(),TEXT("GetSceneManager"));
        auto* Start=G.Set(G.Start(),TEXT("SceneManager"),{},Get->GetReturnValuePin());
        auto* Valid=G.Branch(Start,G.Valid(Manager(G)));
        auto* Current=G.Call(USMS_SceneManager::StaticClass(),TEXT("GetCurrentSceneTag"));Link(Manager(G),P(Current,TEXT("self")));
        auto* Same=G.Branch(Valid->GetThenPin(),TagEqual(G,Current->GetReturnValuePin(),G.Get(TEXT("OverviewSceneTag"))));
        G.Invoke(Same->GetThenPin(),TEXT("CommitTileEntry"));
        auto* Bound=G.Bind(Same->GetElsePin(),USMS_SceneManager::StaticClass(),Manager(G),TEXT("OnSceneChanged"),TEXT("OnOverviewReady"));
        auto* Switch=G.Call(USMS_SceneManager::StaticClass(),TEXT("SwitchScene"));Link(Manager(G),P(Switch,TEXT("self")));Link(G.Get(TEXT("OverviewSceneTag")),P(Switch,TEXT("TargetSceneTag")));
        auto* Accepted=G.Branch(G.Exec(Bound,Switch),Switch->GetReturnValuePin());
        auto* Fail=G.Call(UMTS_SubMapHandler::StaticClass(),TEXT("FailLoading"));D(Fail,TEXT("Error"),TEXT("无法退出当前基地场景，请等待场景切换完成后重试。"));G.Exec(Accepted->GetElsePin(),Fail);
        auto* NoScene=G.Call(UMTS_SubMapHandler::StaticClass(),TEXT("FailLoading"));D(NoScene,TEXT("Error"),TEXT("场景管理器不可用。"));G.Exec(Valid->GetElsePin(),NoScene);
        G.Comment(TEXT("先完成基地场景退出（恢复相机与暂停），再通知子地图初始化完成。可在此增加部署小队等异步步骤。"));
    }
    {
        FGraph G(BP,TEXT("OnOverviewReady"));
        auto* Match=G.Branch(G.Start(),TagEqual(G,P(G.Entry,TEXT("CurrentSceneTag")),G.Get(TEXT("OverviewSceneTag"))));
        G.Invoke(G.Invoke(Match->GetThenPin(),TEXT("ReleaseSceneBinding")),TEXT("CommitTileEntry"));
    }
    {
        FGraph G(BP,TEXT("CommitTileEntry"));
        auto* Info=G.Break(FMTS_SubMapInfo::StaticStruct(),MapInfo(G,OwnID(G)));
        auto* Payload=G.Break(FMTS_MapTransitionPayload::StaticStruct(),P(Info,TEXT("LoadingPayload")));
        auto* Equal=G.Math(TEXT("EqualEqual_ByteByte"),P(Payload,TEXT("Phase")),nullptr,*FString::FromInt(int32(EMTS_MapTransitionPhase::Completed)));
        auto* Branch=G.Branch(G.Start(),Equal);
        G.Invoke(Branch->GetThenPin(),TEXT("ActivateTileMap"));
        G.Exec(Branch->GetElsePin(),G.Call(UMTS_SubMapHandler::StaticClass(),TEXT("FinishLoading")));
        G.Comment(TEXT("新加载必须主动通知完成；已驻留的 T5 直接复用原 Handler 和关卡实例。"));
    }
    {
        FGraph G(BP,TEXT("ActivateTileMap"));
        auto* Info=G.Break(FMTS_SubMapInfo::StaticStruct(),MapInfo(G,OwnID(G)));
        auto* Transform=G.Call(UKismetMathLibrary::StaticClass(),TEXT("ComposeTransforms"));Link(G.Get(TEXT("LocalEntryTransform")),P(Transform,TEXT("A")));Link(P(Info,TEXT("Transform")),P(Transform,TEXT("B")));
        auto* Activate=G.Call(AGameMainMapGameMode::StaticClass(),TEXT("ActivateLoadedMap"));Link(GameMode(G),P(Activate,TEXT("self")));Link(OwnID(G),P(Activate,TEXT("MapID")));Link(Transform->GetReturnValuePin(),P(Activate,TEXT("EntryTransform")));D(Activate,TEXT("MapType"),TEXT("Battle"));
        auto* Result=G.Branch(G.Exec(G.Start(),Activate),Activate->GetReturnValuePin());
        auto* Fail=G.Set(Result->GetElsePin(),TEXT("LastLoadError"),TEXT("地图已加载，但当前主地图状态不允许激活。"));PrintFailure(G,Fail,G.Get(TEXT("LastLoadError")));
    }
    {
        FGraph G(BP,Events(BP,TEXT("SubMapLifecycle")));
        auto* Loaded=Event(G,UMTS_SubMapHandler::StaticClass(),TEXT("OnSubMapLoaded"));G.Invoke(P(Loaded,TEXT("then")),TEXT("PrepareTileEntry"));
        G.Row(450);auto* Ready=Event(G,UMTS_SubMapHandler::StaticClass(),TEXT("OnSubMapReady"));G.Invoke(P(Ready,TEXT("then")),TEXT("ActivateTileMap"));
        G.Row(900);auto* Unload=Event(G,UMTS_SubMapHandler::StaticClass(),TEXT("OnSubMapUnloading"));G.Invoke(P(Unload,TEXT("then")),TEXT("ReleaseSceneBinding"));
        G.Row(1350);auto* Done=Event(G,UMTS_SubMapHandler::StaticClass(),TEXT("OnSubMapUnloaded"));auto* Empty=G.Array(TEXT("Array_Clear"),G.Get(TEXT("SquadIds")));G.Exec(P(Done,TEXT("then")),Empty);
        G.Row(1800);auto* Failed=Event(G,UMTS_SubMapHandler::StaticClass(),TEXT("OnSubMapLoadFailed"));auto* End=G.Set(G.Invoke(P(Failed,TEXT("then")),TEXT("ReleaseSceneBinding")),TEXT("LastLoadError"),{},P(Failed,TEXT("Error")));PrintFailure(G,End,G.Get(TEXT("LastLoadError")));
        G.Comment(TEXT("T5 的完整生命周期由此蓝图控制。子地图加载完成后进行初始化，通知完成才关闭加载界面；卸载事件用于释放小队、事件绑定等。"));
    }
    check(Save(BP));
}
void CreateWidgetRoute(UWidgetBlueprint* BP,UBlueprint* Handler)
{
    if(Find(BP,TEXT("ShowTileEntryError")))
    {
        Backup(BP);
        TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
        for(auto* Graph:Graphs)for(UEdGraphNode* N:Graph->Nodes)
            if(auto* E=Cast<UK2Node_Event>(N);E && E->EventReference.GetMemberName()==TEXT("OnEnterTileMapRequested"))
                E->NodeComment=TEXT("TileId 与最新 SquadIds 已验证。创建或复用 T5 Handler，仅替换带瓦片区域标签的子地图。");
        auto* Error=BP->WidgetTree->FindWidget(TEXT("TileEntryError"));
        if(Error)CastChecked<UCanvasPanelSlot>(Error->Slot)->SetOffsets(FMargin(20,-114,20,34));
        auto* Content=BP->WidgetTree->FindWidget(TEXT("TileInfoContent"));
        if(Content)CastChecked<UCanvasPanelSlot>(Content->Slot)->SetOffsets(FMargin(14,98,14,120));
        check(Save(BP));return;
    }
    Backup(BP);
    auto* Panel=CastChecked<UCanvasPanel>(BP->WidgetTree->FindWidget(TEXT("TileInfoPanel")));
    auto* ErrorText=BP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("TileEntryError"));ErrorText->bIsVariable=true;
    BP->WidgetVariableNameToGuidMap.Add(ErrorText->GetFName(),FGuid::NewGuid());
    ErrorText->SetText(FText());auto Font=ErrorText->GetFont();Font.Size=12;ErrorText->SetFont(Font);ErrorText->SetColorAndOpacity(FSlateColor(FLinearColor(1,.5f,.35f,1)));ErrorText->SetAutoWrapText(true);ErrorText->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* Slot=Panel->AddChildToCanvas(ErrorText);Slot->SetAnchors(FAnchors(0,1,1,1));Slot->SetOffsets(FMargin(20,-114,20,34));
    if(auto* Content=BP->WidgetTree->FindWidget(TEXT("TileInfoContent")))CastChecked<UCanvasPanelSlot>(Content->Slot)->SetOffsets(FMargin(14,98,14,120));
    Var(BP,TEXT("TileRegionTag"),Struct(FGameplayTag::StaticStruct()),TEXT("(TagName=\"Map.Region.Tile\")"),TEXT("待替换的瓦片区域标签"),true,TEXT("瓦片地图|配置"));
    Var(BP,TEXT("T5HandlerClass"),Type(UEdGraphSchema_K2::PC_Class,Handler->GeneratedClass),Handler->GeneratedClass->GetPathName(),TEXT("T5 子地图处理类"),true,TEXT("瓦片地图|配置"));
    Declare(BP,TEXT("ShowTileEntryError"),{{TEXT("Error"),Type(UEdGraphSchema_K2::PC_Text),{}}});check(Compile(BP));
    {
        FGraph G(BP,TEXT("ShowTileEntryError"));auto* Set=G.Call(UTextBlock::StaticClass(),TEXT("SetText"));Link(G.Get(TEXT("TileEntryError")),P(Set,TEXT("self")));Link(P(G.Entry,TEXT("Error")),P(Set,TEXT("InText")));G.Exec(G.Start(),Set);
    }
    UK2Node_Event* Entry=nullptr;TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    for(auto* Graph:Graphs)for(UEdGraphNode* N:Graph->Nodes)if(auto* E=Cast<UK2Node_Event>(N);E && E->EventReference.GetMemberName()==TEXT("OnEnterTileMapRequested"))Entry=E;
    check(Entry && P(Entry,TEXT("then"))->LinkedTo.IsEmpty());
    Entry->NodeComment=TEXT("TileId 与最新 SquadIds 已验证。创建或复用 T5 Handler，仅替换带瓦片区域标签的子地图。");
    FGraph G(BP,Entry->GetGraph());G.Row(400);
    for(UEdGraphNode* N:G.G->Nodes)if(auto* Comment=Cast<UEdGraphNode_Comment>(N))Comment->NodeComment=TEXT("进入当前瓦片：校验 → 查询旧瓦片区域 → 创建并配置 Handler → 带读条异步替换。基地没有瓦片区域标签，因此不参与卸载。");
    auto* Busy=WorldCall(G,UMTS_SubMapBlueprintLibrary::StaticClass(),TEXT("IsSubMapTransitionInProgress"));
    auto* Idle=G.Branch(P(Entry,TEXT("then")),G.Not(Busy->GetReturnValuePin()));
    auto* Clear=G.Call(TEXT("ShowTileEntryError"));auto* Start=G.Exec(Idle->GetThenPin(),Clear);
    auto* Supported=G.Branch(Start,G.Math(TEXT("EqualEqual_NameName"),P(Entry,TEXT("TileId")),nullptr,TEXT("T5")));
    auto* Unsupported=G.Call(TEXT("ShowTileEntryError"));D(Unsupported,TEXT("Error"),TEXT("该瓦片尚未配置子地图。"));G.Exec(Supported->GetElsePin(),Unsupported);
    auto* FindHandler=WorldCall(G,UMTS_SubMapBlueprintLibrary::StaticClass(),TEXT("GetSubMapHandler"));Link(P(Entry,TEXT("TileId")),P(FindHandler,TEXT("MapID")));
    auto* Existing=G.Branch(Supported->GetThenPin(),G.Valid(FindHandler->GetReturnValuePin()));
    auto* Reuse=G.CastTo(Handler->GeneratedClass,FindHandler->GetReturnValuePin(),Existing->GetThenPin());
    auto* ReuseStart=G.Set(Reuse->GetValidCastPin(),TEXT("SquadIds"),{},P(Entry,TEXT("SquadIds")),Handler->GeneratedClass,Reuse->GetCastResultPin());
    auto* Prepare=G.Call(Handler->GeneratedClass,TEXT("PrepareTileEntry"));Link(Reuse->GetCastResultPin(),P(Prepare,TEXT("self")));G.Exec(ReuseStart,Prepare);
    auto* Create=WorldCall(G,UMTS_SubMapBlueprintLibrary::StaticClass(),TEXT("CreateSubMapHandler"));Link(G.Get(TEXT("T5HandlerClass")),P(Create,TEXT("HandlerClass")));
    auto* Cast=G.CastTo(Handler->GeneratedClass,Create->GetReturnValuePin(),G.Exec(Existing->GetElsePin(),Create));
    auto* Configured=G.Set(Cast->GetValidCastPin(),TEXT("SquadIds"),{},P(Entry,TEXT("SquadIds")),Handler->GeneratedClass,Cast->GetCastResultPin());
    auto* Old=WorldCall(G,UMTS_SubMapBlueprintLibrary::StaticClass(),TEXT("GetSubMapIDsByTag"));Link(G.Get(TEXT("TileRegionTag")),P(Old,TEXT("Tag")));
    auto* Load=WorldCall(G,UMTS_SubMapBlueprintLibrary::StaticClass(),TEXT("LoadSubMapByHandlerObject"));Link(G.Get(TEXT("LoadRequest"),Handler->GeneratedClass,Cast->GetCastResultPin()),P(Load,TEXT("Request")));Link(Cast->GetCastResultPin(),P(Load,TEXT("Handler")));Link(Old->GetReturnValuePin(),P(Load,TEXT("UnloadMapIDs")));
    auto* Accepted=G.Branch(G.Exec(Configured,Load),Load->GetReturnValuePin());auto* Error=G.Call(TEXT("ShowTileEntryError"));Link(P(Load,TEXT("OutError")),P(Error,TEXT("Error")));G.Exec(Accepted->GetElsePin(),Error);
    auto* Invalid=G.Call(TEXT("ShowTileEntryError"));D(Invalid,TEXT("Error"),TEXT("子地图处理类配置无效。"));G.Exec(Cast->GetInvalidCastPin(),Invalid);G.Exec(Reuse->GetInvalidCastPin(),Invalid);
    check(Save(BP));
}
void GuardSandboxInput()
{
    auto* BP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController"));check(BP);
    auto* Graph=Find(BP,TEXT("CommandMap_IsRoomReady"));check(Graph);
    for(UEdGraphNode* N:Graph->Nodes)if(auto* C=Cast<UK2Node_CallFunction>(N);C && C->FunctionReference.GetMemberName()==TEXT("IsSubMapTransitionInProgress"))return;
    Backup(BP);
    UK2Node_FunctionEntry* Entry=nullptr;for(UEdGraphNode* N:Graph->Nodes)if(auto* E=Cast<UK2Node_FunctionEntry>(N))Entry=E;check(Entry);
    const auto Links=P(Entry,TEXT("then"))->LinkedTo;FGraph G(BP,Graph);for(auto* LinkTo:Links)Link(P(Entry,TEXT("then")),LinkTo);
    G.Row(1000);auto* Busy=WorldCall(G,UMTS_SubMapBlueprintLibrary::StaticClass(),TEXT("IsSubMapTransitionInProgress"));auto* Idle=G.Not(Busy->GetReturnValuePin());
    const auto Nodes=Graph->Nodes;for(UEdGraphNode* N:Nodes)if(auto* R=Cast<UK2Node_FunctionResult>(N))if(auto* Result=R->FindPin(TEXT("ReturnValue"));Result && !Result->LinkedTo.IsEmpty())
    {auto* Previous=Result->LinkedTo[0];Result->BreakAllPinLinks();Link(G.Both(Previous,Idle),Result);}
    check(Save(BP));
}
}

int32 ApplySubMapLifecycleBlueprints()
{
    using namespace SubMapLifecycleBP;MainMapBP::OK=true;
    const TCHAR* AssetPath=TEXT("/Game/System/Map/BattleMap/T5/DA_T5Map");
    auto* Asset=LoadObject<UMTS_SubMapDataAsset>(nullptr,AssetPath,nullptr,LOAD_NoWarn);
    if(!Asset){Asset=NewObject<UMTS_SubMapDataAsset>(CreatePackage(AssetPath),TEXT("DA_T5Map"),RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(Asset);}
    Backup(Asset);Asset->MapID=TEXT("T5");Asset->MapName=TEXT("T5");Asset->MapAsset=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/System/Map/BattleMap/L_BattleTemplate.L_BattleTemplate")));Asset->MapTags.AddTag(FGameplayTag::RequestGameplayTag(RegionTag));check(Save(Asset));
    auto* Legacy=LoadObject<UMTS_SubMapDataAsset>(nullptr,TEXT("/Game/System/Map/GameMainMap/Data/DA_BattleExample"));check(Legacy);Backup(Legacy);Legacy->MapTags.AddTag(FGameplayTag::RequestGameplayTag(RegionTag));check(Save(Legacy));
    auto* Handler=LoadObject<UBlueprint>(nullptr,HandlerPath);check(Handler);CreateHandlerBlueprint(Handler,Asset);
    auto* Widget=LoadObject<UWidgetBlueprint>(nullptr,RoomPath);check(Widget);CreateWidgetRoute(Widget,Handler);
    GuardSandboxInput();
    UE_LOG(LogTemp,Display,TEXT("SUBMAP_LIFECYCLE_APPLY_OK"));return MainMapBP::OK?0:1;
}

int32 RefactorSubMapHandlerBlueprints()
{
    using namespace SubMapLifecycleBP;
    MainMapBP::OK=true;
    auto* BP=LoadObject<UBlueprint>(nullptr,HandlerPath);check(BP);
    if(BP->ParentClass==UGameMainMapSubMapHandler::StaticClass())
    {
        UE_LOG(LogTemp,Display,TEXT("SUBMAP_HANDLER_REFACTOR_ALREADY_APPLIED"));return 0;
    }
    check(BP->ParentClass==UMTS_SubMapHandler::StaticClass());
    const FString Source=FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    const FString Dest=FPaths::ProjectSavedDir()/TEXT("SubMapHandlerRefactor/Backup/BP_SubMapHandler_T5.uasset");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Dest),true);
    if(!IFileManager::Get().FileExists(*Dest))check(IFileManager::Get().Copy(*Dest,*Source)==COPY_OK);
    auto ExportConfig=[&](FName Name)
    {
        FString Value;auto* CDO=BP->GeneratedClass->GetDefaultObject();auto* Property=FindFProperty<FProperty>(BP->GeneratedClass,Name);check(Property);
        Property->ExportText_InContainer(0,Value,CDO,nullptr,CDO,PPF_None);return Value;
    };
    TMap<FName,FString> Config;
    for(FName Name:{TEXT("LoadRequest"),TEXT("LocalEntryTransform"),TEXT("OverviewSceneTag"),TEXT("SquadIds")})Config.Add(Name,ExportConfig(Name));
    // Only replace the inspected entry infrastructure. Authored settings and other graphs remain.
    for(FName Name:{TEXT("PrepareTileEntry"),TEXT("CommitTileEntry"),TEXT("ActivateTileMap"),TEXT("ReleaseSceneBinding"),TEXT("ReturnToBaseIfActive"),TEXT("OnOverviewReady"),TEXT("SubMapLifecycle")})
    {if(auto* Graph=Find(BP,Name))FBlueprintEditorUtils::RemoveGraph(BP,Graph,EGraphRemoveFlags::None);}
    FBlueprintEditorUtils::BulkRemoveMemberVariables(BP,{TEXT("SceneManager"),TEXT("LastLoadError")});
    BP->ParentClass=UGameMainMapSubMapHandler::StaticClass();
    Declare(BP,TEXT("PrepareTileEntry"));
    check(Compile(BP));
    auto PrintIfRejected=[](FGraph& G,UK2Node_CallFunction* Call,UEdGraphPin* From)
    {
        auto* Result=G.Branch(G.Exec(From,Call),Call->GetReturnValuePin());
        PrintFailure(G,Result->GetElsePin(),P(Call,TEXT("OutError")));
    };
    auto EntryCall=[](FGraph& G,FName Name)
    {
        auto* N=G.Call(UGameMainMapSubMapHandler::StaticClass(),Name);
        Link(G.Get(TEXT("LocalEntryTransform")),P(N,TEXT("LocalEntryTransform")));
        D(N,TEXT("MapType"),TEXT("Battle"));return N;
    };
    {
        FGraph G(BP,TEXT("PrepareTileEntry"));
        auto* Wait=G.Call(UGameMainMapSubMapHandler::StaticClass(),TEXT("WaitForEntryScene"));
        Link(G.Get(TEXT("OverviewSceneTag")),P(Wait,TEXT("SceneTag")));
        auto* Result=G.Branch(G.Exec(G.Start(),Wait),Wait->GetReturnValuePin());
        auto* Fail=G.Call(UMTS_SubMapHandler::StaticClass(),TEXT("FailLoading"));Link(P(Wait,TEXT("OutError")),P(Fail,TEXT("Error")));
        auto* Reported=G.Branch(G.Exec(Result->GetElsePin(),Fail),Fail->GetReturnValuePin());
        // Resident maps reject FailLoading; still display an entry failure without unloading them.
        PrintFailure(G,Reported->GetElsePin(),P(Wait,TEXT("OutError")));
        G.Comment(TEXT("蓝图决定进入前要切换的场景。C++ 负责等待、重复请求保护和解绑；就绪后触发“进入前场景已就绪”。"));
    }
    {
        FGraph G(BP,Events(BP,TEXT("SubMapLifecycle")));
        auto* Loaded=Event(G,UMTS_SubMapHandler::StaticClass(),TEXT("OnSubMapLoaded"));G.Invoke(P(Loaded,TEXT("then")),TEXT("PrepareTileEntry"));
        G.Row(400);auto* SceneReady=Event(G,UGameMainMapSubMapHandler::StaticClass(),TEXT("OnEntrySceneReady"));
        PrintIfRejected(G,EntryCall(G,TEXT("CommitMapEntry")),P(SceneReady,TEXT("then")));
        SceneReady->NodeComment=TEXT("在此添加本地图的小队部署或自定义初始化。异步完成后再调用“提交子地图进入”。");SceneReady->bCommentBubbleVisible=true;
        G.Row(850);auto* Ready=Event(G,UMTS_SubMapHandler::StaticClass(),TEXT("OnSubMapReady"));PrintIfRejected(G,EntryCall(G,TEXT("ActivateThisMap")),P(Ready,TEXT("then")));
        G.Row(1300);auto* Unload=Event(G,UMTS_SubMapHandler::StaticClass(),TEXT("OnSubMapUnloading"));
        Unload->NodeComment=TEXT("卸载资源的扩展事件。返回基地流程后续实现，此处不自动切换活动地图。");Unload->bCommentBubbleVisible=true;
        G.Row(1750);auto* Done=Event(G,UMTS_SubMapHandler::StaticClass(),TEXT("OnSubMapUnloaded"));G.Exec(P(Done,TEXT("then")),G.Array(TEXT("Array_Clear"),G.Get(TEXT("SquadIds"))));
        G.Row(2150);auto* Failed=Event(G,UMTS_SubMapHandler::StaticClass(),TEXT("OnSubMapLoadFailed"));PrintFailure(G,P(Failed,TEXT("then")),P(Failed,TEXT("Error")));
        G.Comment(TEXT("通用查询、坐标计算、激活、等待与清理由 C++ 父类实现。这里保留 T5 的生命周期编排、小队初始化和错误表现；无需在失败/卸载事件中手动解绑通用回调。"));
    }
    check(Compile(BP));
    for(const auto& Pair:Config)checkf(ExportConfig(Pair.Key)==Pair.Value,TEXT("Handler configuration changed: %s"),*Pair.Key.ToString());
    check(Save(BP));
    // Refresh dependent class pins without changing the widget's authored routing.
    auto* Widget=LoadObject<UWidgetBlueprint>(nullptr,RoomPath);check(Widget);
    FBlueprintEditorUtils::RefreshAllNodes(Widget);check(Save(Widget));
    UE_LOG(LogTemp,Display,TEXT("SUBMAP_HANDLER_REFACTOR_OK preserved configuration; native parent; removed 5 utility graphs and 2 runtime variables"));
    return 0;
}
