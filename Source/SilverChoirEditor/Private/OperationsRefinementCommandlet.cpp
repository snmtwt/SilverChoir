#include "OperationsRefinementCommandlet.h"
#include "MainMapBlueprintBuilder.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "InputAction.h"
#include "Factories/TextureFactory.h"
#include "Engine/Texture2D.h"
#include "Engine/DataTable.h"
#include "FCS_FreeCameraPawn.h"
#include "FCS_FreeCameraBlueprintLibrary.h"
#include "BlueprintGameplayTagLibrary.h"
#include "Data/Units/UnitStructs.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/PersonnelPreparationRoomWidget.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsSquadEntryWidget.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"

namespace OperationsRefinement
{
using namespace MainMapBP;
const FString UI=TEXT("/Game/System/Map/BaseMap/UI/SceneUI/");
FString BackupRoot;
void Backup(UObject* Asset)
{
    const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    if(!IFileManager::Get().FileExists(*File))return;
    const FString Dest=BackupRoot/Asset->GetOutermost()->GetName().Mid(6)+TEXT(".uasset");
    if(IFileManager::Get().FileExists(*Dest))return;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Dest),true);
    check(IFileManager::Get().Copy(*Dest,*File)==COPY_OK);
}
bool Save(UObject* Asset)
{
    if(auto* BP=Cast<UWidgetBlueprint>(Asset))
    {
        TSet<FName> Names;
        BP->ForEachSourceWidget([&](UWidget* W){Names.Add(W->GetFName());});
        for(const auto& A:BP->Animations)if(A)Names.Add(A->GetFName());
        for(FName N:Names)if(!BP->WidgetVariableNameToGuidMap.Contains(N))BP->WidgetVariableNameToGuidMap.Add(N,FGuid::NewGuid());
        for(auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It)if(!Names.Contains(It.Key()))It.RemoveCurrent();
    }
    if(auto* BP=Cast<UBlueprint>(Asset))if(!Compile(BP))return false;
    Asset->MarkPackageDirty();
    const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,Args);
}
void Inspect(UWidgetBlueprint* BP,FString& Dump)
{
    Dump+=TEXT("ASSET ")+BP->GetPathName()+TEXT("\n");
    BP->ForEachSourceWidget([&](UWidget* W)
    {
        FString Detail;
        if(auto* S=Cast<UCanvasPanelSlot>(W->Slot)){const auto O=S->GetOffsets();Detail=FString::Printf(TEXT(" anchors=%s:%s offsets=(%g,%g,%g,%g)"),*S->GetAnchors().Minimum.ToString(),*S->GetAnchors().Maximum.ToString(),O.Left,O.Top,O.Right,O.Bottom);}
        if(auto* T=Cast<UTextBlock>(W))Detail+=TEXT(" text=")+T->GetText().ToString();
        Dump+=FString::Printf(TEXT("WIDGET %s class=%s parent=%s visibility=%d%s\n"),*W->GetName(),*W->GetClass()->GetName(),*GetNameSafe(W->GetParent()),int32(W->GetVisibility()),*Detail);
    });
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    for(auto* G:Graphs)for(UEdGraphNode* N:G->Nodes)
    {
        Dump+=FString::Printf(TEXT("NODE %s.%s title=%s\n"),*G->GetName(),*N->GetName(),*N->GetNodeTitle(ENodeTitleType::ListView).ToString());
        for(auto* Pin:N->Pins)
        {
            FString Links;for(auto* L:Pin->LinkedTo)Links+=L->GetOwningNode()->GetName()+TEXT(".")+L->PinName.ToString()+TEXT(" ");
            Dump+=FString::Printf(TEXT(" PIN %s value=%s links=%s\n"),*Pin->PinName.ToString(),*Pin->DefaultValue,*Links);
        }
    }
}
void RemoveWidget(UWidgetBlueprint* BP,FName Name)
{
    if(auto* W=BP->WidgetTree->FindWidget(Name))
    {
        // Only remove decorations, never equipment containers or event-bound widgets.
        W->RemoveFromParent();W->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);W->SetFlags(RF_Transient);
    }
}
void MoveWidget(UWidgetBlueprint* BP,FName Name,FAnchors Anchors,FMargin Offsets)
{
    auto* W=BP->WidgetTree->FindWidget(Name);check(W);
    auto* Slot=CastChecked<UCanvasPanelSlot>(W->Slot);Slot->SetAnchors(Anchors);Slot->SetOffsets(Offsets);
}
void RefinePortraitViews(UWidgetBlueprint* Personnel)
{
    Backup(Personnel);
    // Replace the leftover square backing with one deliberately sized portrait frame.
    for(FName Name:{TEXT("PortraitBacking"),TEXT("IdentityCaption"),TEXT("IdentityAccent"),TEXT("SelectedStatus")})RemoveWidget(Personnel,Name);
    MoveWidget(Personnel,TEXT("IdentityPanel"),FAnchors(0,0,1,0),FMargin(0,52,0,288));
    MoveWidget(Personnel,TEXT("DetailPortrait"),FAnchors(0,0),FMargin(20,40,144,192));
    auto* Portrait=CastChecked<UPersonnelPortraitWidget>(Personnel->WidgetTree->FindWidget(TEXT("DetailPortrait")));Portrait->PortraitFormat=EPortraitFormat::Full;
    MoveWidget(Personnel,TEXT("RecordEyebrow"),FAnchors(0,0,1,0),FMargin(20,12,20,18));
    MoveWidget(Personnel,TEXT("SelectedMeta"),FAnchors(0,0,1,0),FMargin(184,48,16,32));
    MoveWidget(Personnel,TEXT("SelectedName"),FAnchors(0,0,1,0),FMargin(184,92,16,30));
    MoveWidget(Personnel,TEXT("SelectedSquad"),FAnchors(0,0,1,0),FMargin(184,138,16,50));
    auto* Squad=CastChecked<UTextBlock>(Personnel->WidgetTree->FindWidget(TEXT("SelectedSquad")));Squad->SetAutoWrapText(true);
    MoveWidget(Personnel,TEXT("LocationStrip"),FAnchors(0,0,1,0),FMargin(20,250,20,26));
    MoveWidget(Personnel,TEXT("LocationMark"),FAnchors(0,0),FMargin(28,260,3,6));
    MoveWidget(Personnel,TEXT("SelectedLocation"),FAnchors(0,0,1,0),FMargin(40,252,24,24));
    MoveWidget(Personnel,TEXT("IdentityContentDivider"),FAnchors(0,0,1,0),FMargin(20,348,20,1));
    MoveWidget(Personnel,TEXT("DetailPages"),FAnchors(0,0,1,1),FMargin(0,366,0,0));
    check(Save(Personnel));

    auto* Member=LoadObject<UWidgetBlueprint>(nullptr,*(UI+TEXT("OperationsCommandRoom/Components/WBP_OperationsMemberEntry")));check(Member);Backup(Member);
    CastChecked<USizeBox>(Member->WidgetTree->RootWidget)->SetHeightOverride(80);
    auto* MemberPortrait=CastChecked<UPersonnelPortraitWidget>(Member->WidgetTree->FindWidget(TEXT("MemberPortrait")));MemberPortrait->PortraitFormat=EPortraitFormat::Square;
    MoveWidget(Member,TEXT("MemberPortrait"),FAnchors(0,0),FMargin(16,8,64,64));
    MoveWidget(Member,TEXT("MemberNameText"),FAnchors(0,0),FMargin(96,17,168,24));
    MoveWidget(Member,TEXT("CaptainText"),FAnchors(0,0),FMargin(96,47,54,22));
    check(Save(Member));
    auto* Row=LoadObject<UWidgetBlueprint>(nullptr,*(UI+TEXT("OperationsCommandRoom/Components/WBP_OperationsSquadEntry")));check(Row);Backup(Row);
    Row->GeneratedClass->GetDefaultObject<UOperationsSquadEntryWidget>()->MemberRowHeight=80;
    check(Save(Row));

    auto* Shared=LoadObject<UWidgetBlueprint>(nullptr,*(UI+TEXT("PersonnelPreparationRoom/Components/WBP_PersonnelPortrait")));check(Shared);Backup(Shared);
    // A fixed-size placeholder remains proportional inside either portrait format.
    MoveWidget(Shared,TEXT("Head"),FAnchors(.5,.5),FMargin(-6,-18,12,12));
    MoveWidget(Shared,TEXT("Shoulders"),FAnchors(.5,.5),FMargin(-14,-2,28,18));
    check(Save(Shared));
}
void RefinePauseInput()
{
    for(const TCHAR* Path:{TEXT("/Game/System/Input/Common/IA_MouseLeft"),TEXT("/Game/System/Input/Common/IA_MouseMiddle"),TEXT("/Game/System/Input/Base/IA_SandboxZoom")})
    {
        auto* Action=LoadObject<UInputAction>(nullptr,Path);check(Action);Backup(Action);
        Action->bTriggerWhenPaused=true;check(Save(Action));
    }
}
void RefineCamera()
{
    auto* BP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/Scene/BP_作战指挥室_Scene"));check(BP);Backup(BP);
    auto* Interior=FindFProperty<FStructProperty>(BP->GeneratedClass,TEXT("InteriorCameraState"));check(Interior);
    FFCS_CameraState Overhead=*Interior->ContainerPtrToValuePtr<FFCS_CameraState>(BP->GeneratedClass->GetDefaultObject());
    Overhead.Pitch=-60;Overhead.TargetArmLength=5000;Overhead.MoveSpeed=6000;Overhead.RotationSpeed=180;Overhead.ZoomSpeed=18000;
    FFCS_CameraState Exit=Overhead;Exit.ZoomSpeed=24000;
    FString AboveDefault,ExitDefault;
    FFCS_CameraState::StaticStruct()->ExportText(AboveDefault,&Overhead,nullptr,nullptr,PPF_None,nullptr);
    FFCS_CameraState::StaticStruct()->ExportText(ExitDefault,&Exit,nullptr,nullptr,PPF_None,nullptr);
    Var(BP,TEXT("OverheadCameraState"),Struct(FFCS_CameraState::StaticStruct()),AboveDefault,TEXT("进入：场景上方相机状态"),true,TEXT("作战指挥室|配置"));
    Var(BP,TEXT("ExitOverheadCameraState"),Struct(FFCS_CameraState::StaticStruct()),ExitDefault,TEXT("离开：场景上方相机状态"),true,TEXT("作战指挥室|配置"));
    check(Compile(BP));
    auto* Above=Find(BP,TEXT("Room_MoveAbove"));check(Above);
    for(auto* N:TArray<UEdGraphNode*>(Above->Nodes))if(!N->IsA<UK2Node_FunctionEntry>()){N->BreakAllNodeLinks();Above->RemoveNode(N);}
    {
        FGraph G(BP,Above);auto* Guard=G.Branch(G.Start(),G.Valid(G.Get(TEXT("RoomCamera"))));
        auto* Move=G.Call(AFCS_FreeCameraPawn::StaticClass(),TEXT("MoveToCameraState"));Link(G.Get(TEXT("RoomCamera")),P(Move,TEXT("self")));Link(G.Get(TEXT("OverheadCameraState")),P(Move,TEXT("InCameraState")));
        auto* Callback=G.Delegate(TEXT("Room_CameraPositioned"));Link(Callback->GetDelegateOutPin(),P(Move,TEXT("MoveFinished")));Callback->HandleAnyChangeWithoutNotifying();
        G.Exec(Guard->GetThenPin(),Move);G.Invoke(Guard->GetElsePin(),TEXT("Room_CameraPositioned"));
        G.Comment(TEXT("第一段使用可编辑的 OverheadCameraState 到达场景上方，完成后才开始内部视角的缩进。"));
    }
    // Drop only the old return-position capture, preserving pitch limits and control locks.
    auto* Cache=Find(BP,TEXT("Room_CacheCamera"));check(Cache);
    for(auto* N:TArray<UEdGraphNode*>(Cache->Nodes))if(auto* Set=Cast<UK2Node_VariableSet>(N))if(Set->VariableReference.GetMemberName()==TEXT("ReturnCameraState"))
    {
        const auto Inputs=P(Set,TEXT("execute"))->LinkedTo,Outputs=P(Set,TEXT("then"))->LinkedTo;
        TArray<UEdGraphNode*> Sources;for(auto* L:P(Set,TEXT("ReturnCameraState"))->LinkedTo)Sources.Add(L->GetOwningNode());
        Set->BreakAllNodeLinks();Cache->RemoveNode(Set);for(auto* In:Inputs)for(auto* Out:Outputs)Link(In,Out);
        for(auto* Source:Sources)if(auto* Call=Cast<UK2Node_CallFunction>(Source))if(Call->FunctionReference.GetMemberName()==TEXT("GetCurrentCameraState")){Call->BreakAllNodeLinks();Cache->RemoveNode(Call);}
    }
    auto* Leave=Find(BP,TEXT("LeaveScene"));check(Leave);
    for(UEdGraphNode* N:TArray<UEdGraphNode*>(Leave->Nodes))
    {
        if(auto* Get=Cast<UK2Node_VariableGet>(N))if(Get->VariableReference.GetMemberName()==TEXT("ReturnCameraState"))
        {
            const auto Targets=Get->GetValuePin()->LinkedTo;
            FGraphNodeCreator<UK2Node_VariableGet> Creator(*Leave);auto* NewGet=Creator.CreateNode();
            NewGet->VariableReference.SetSelfMember(TEXT("ExitOverheadCameraState"));NewGet->NodePosX=Get->NodePosX;NewGet->NodePosY=Get->NodePosY;Creator.Finalize();
            Get->BreakAllNodeLinks();Leave->RemoveNode(Get);for(auto* Target:Targets)Link(NewGet->GetValuePin(),Target);
        }
        if(auto* Comment=Cast<UEdGraphNode_Comment>(N))Comment->NodeComment=TEXT("退出仅移动至独立配置的 ExitOverheadCameraState（更快缩放），不返回进入前位置。等待UI与相机完成后恢复控制锁和俯仰范围。" );
    }
    FBlueprintEditorUtils::RemoveMemberVariable(BP,TEXT("ReturnCameraState"));
    check(Save(BP));
}
void PreserveOverviewReturnView()
{
    auto* BP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/Scene/BP_基地全景_Scene"));check(BP);Backup(BP);
    auto* Graph=Find(BP,TEXT("EnterScene"));check(Graph);
    for(UEdGraphNode* N:Graph->Nodes)if(N->NodeComment==TEXT("OperationsReturnKeepView"))return;
    UK2Node_FunctionEntry* Entry=nullptr;UK2Node_FunctionResult* Result=nullptr;UK2Node_CallFunction* Move=nullptr;
    for(UEdGraphNode* N:Graph->Nodes)
    {
        if(auto* E=Cast<UK2Node_FunctionEntry>(N))Entry=E;
        if(auto* R=Cast<UK2Node_FunctionResult>(N))Result=R;
        if(auto* C=Cast<UK2Node_CallFunction>(N))if(C->FunctionReference.GetMemberParentClass()==UFCS_FreeCameraBlueprintLibrary::StaticClass() && C->FindPin(TEXT("CameraState")))Move=C;
    }
    check(Entry&&Result&&Move);
    const auto EntryLinks=P(Entry,TEXT("then"))->LinkedTo;
    FGraph G(BP,Graph);for(auto* LinkPin:EntryLinks)Link(G.Start(),LinkPin);G.X=2300;G.Y=700;
    auto* Equal=G.Call(UBlueprintGameplayTagLibrary::StaticClass(),TEXT("EqualEqual_GameplayTag"));Link(P(Entry,TEXT("PreviousSceneTag")),P(Equal,TEXT("A")));D(Equal,TEXT("B"),TEXT("(TagName=\"GameScene.OperationsCommandRoom\")"));
    Equal->NodeComment=TEXT("OperationsReturnKeepView");
    const auto Before=P(Move,TEXT("execute"))->LinkedTo,After=P(Move,TEXT("then"))->LinkedTo;P(Move,TEXT("execute"))->BreakAllPinLinks();
    auto* Branch=G.Node<UK2Node_IfThenElse>([](auto*){});Link(Equal->GetReturnValuePin(),P(Branch,TEXT("Condition")));
    for(auto* In:Before)G.Exec(In,Branch);for(auto* Out:After)Link(Branch->GetThenPin(),Out);G.Exec(Branch->GetElsePin(),Move);
    Link(Equal->GetReturnValuePin(),P(Result,TEXT("ReturnValue")));
    G.Comment(TEXT("从作战室返回时保留其退出上方视角，继续恢复基地UI和控制；其他场景仍走原全景相机流程。"));check(Save(BP));
}
void ImportPortraits()
{
    const FString Folder=TEXT("/Game/System/SubSystem/PlayerUnitSubSystem/Portraits/");
    TMap<UTexture2D*,FVector2D> Focus;
    for(int32 Index=1;Index<=4;++Index)
    {
        const FString Name=FString::Printf(TEXT("T_TestPortrait_%02d"),Index),Path=Folder+Name;
        auto* Existing=LoadObject<UTexture2D>(nullptr,*Path);check(Existing);Backup(Existing);
        const FString File=FPaths::ProjectDir()/TEXT("Art/UI/Portraits")/(Name+TEXT(".png"));check(IFileManager::Get().FileExists(*File));
        auto* Factory=NewObject<UTextureFactory>();Factory->SuppressImportOverwriteDialog(true);bool Cancelled=false;
        auto* Texture=CastChecked<UTexture2D>(Factory->FactoryCreateFile(UTexture2D::StaticClass(),CreatePackage(*Path),*Name,RF_Public|RF_Standalone,File,nullptr,GWarn,Cancelled));check(!Cancelled);
        Texture->CompressionSettings=TC_EditorIcon;Texture->LODGroup=TEXTUREGROUP_UI;Texture->MipGenSettings=TMGS_NoMipmaps;Texture->MaxTextureSize=1024;Texture->SRGB=true;Texture->PostEditChange();check(Save(Texture));
        Focus.Add(Texture,FVector2D(.5,.4));
    }
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/System/SubSystem/PlayerUnitSubSystem/DT_UnitTemplates"));check(Table);Backup(Table);
    for(auto& Pair:Table->GetRowMap())
    {
        auto* Row=reinterpret_cast<FUnitTemplate*>(Pair.Value);
        if(const auto* Center=Focus.Find(Row->Profile.PortraitTexture)){Row->Profile.PortraitCropFocus=*Center;Row->Profile.PortraitCropScale=1.f;}
    }
    Table->HandleDataTableChanged();check(Save(Table));
}
}
UOperationsRefinementCommandlet::UOperationsRefinementCommandlet(){IsClient=false;IsEditor=true;LogToConsole=true;}
int32 UOperationsRefinementCommandlet::Main(const FString& Params)
{
    using namespace OperationsRefinement;
    BackupRoot=FPaths::ProjectSavedDir()/TEXT("OperationsRefinement/AssetBackup")/FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
    auto* Personnel=LoadObject<UWidgetBlueprint>(nullptr,*(OperationsRefinement::UI+TEXT("PersonnelPreparationRoom/WBP_PersonnelPreparationRoom")));check(Personnel);
    if(Params.Contains(TEXT("Inspect")))
    {
        FString Dump;Inspect(Personnel,Dump);
        FFileHelper::SaveStringToFile(Dump,*(FPaths::ProjectSavedDir()/TEXT("OperationsRefinement/PersonnelDesigner.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("OPERATIONS_REFINEMENT_INSPECT_OK"));return 0;
    }
    if(!Params.Contains(TEXT("Apply")))return 1;
    if(Params.Contains(TEXT("ApplyPortraits"))){RefinePortraitViews(Personnel);ImportPortraits();UE_LOG(LogTemp,Display,TEXT("OPERATIONS_PORTRAIT_CALIBRATION_OK"));return 0;}
    RefinePauseInput();RefineCamera();PreserveOverviewReturnView();RefinePortraitViews(Personnel);ImportPortraits();
    UE_LOG(LogTemp,Display,TEXT("OPERATIONS_REFINEMENT_APPLY_OK backup=%s"),*BackupRoot);return 0;
}
