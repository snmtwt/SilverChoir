#include "OperationsPanelUpgradeCommandlet.h"
#include "MainMapBlueprintBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Animation/WidgetAnimation.h"
#include "Factories/TextureFactory.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Kismet/GameplayStatics.h"
#include "Map/BaseMap/BaseSandboxMap.h"
#include "GridStrategyMapSystem/Display3D/GSMTile3D.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsSquadEntryWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"
#include "UIBasic/SelectionButtonWidget.h"

using namespace MainMapBP;
namespace OperationsUpgrade
{
const FString Base = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/");
const FString Folder = Base + TEXT("OperationsCommandRoom/");
FString BackupFolder;
FLinearColor C(const TCHAR* Hex) {return FLinearColor(FColor::FromHex(Hex));}
void Backup(UObject* Obj)
{
    FString File = FPackageName::LongPackageNameToFilename(Obj->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    if (IFileManager::Get().FileExists(*File))
    {
        FString Dest = BackupFolder / Obj->GetOutermost()->GetName().Mid(6) + TEXT(".uasset");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Dest), true);
        check(IFileManager::Get().Copy(*Dest,*File) == COPY_OK);
    }
}
bool Save(UObject* Obj)
{
    Obj->MarkPackageDirty();
    const FString File = FPackageName::LongPackageNameToFilename(Obj->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FSavePackageArgs A; A.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Obj->GetOutermost(), Obj, *File, A);
}
bool CompileWidget(UWidgetBlueprint* BP)
{
    TSet<FName> Names;
    // UMG generates variables for source widgets and animations, not only the live tree.
    BP->ForEachSourceWidget([&](UWidget* W){Names.Add(W->GetFName());});
    for (const auto& Animation : BP->Animations) if (Animation) Names.Add(Animation->GetFName());
    for (FName Name : Names) if (!BP->WidgetVariableNameToGuidMap.Contains(Name)) BP->WidgetVariableNameToGuidMap.Add(Name,FGuid::NewGuid());
    for(auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It)if(!Names.Contains(It.Key()))It.RemoveCurrent();
    return Compile(BP);
}
void RemoveSourceWidget(UWidget* Widget)
{
    Widget->RemoveFromParent();
    // Detached objects still outered to WidgetTree count as source widgets in UE 5.8.
    Widget->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    Widget->SetFlags(RF_Transient);
}
bool Finish(UWidgetBlueprint* BP) {return CompileWidget(BP)&&Save(BP);}
UWidgetBlueprint* NewBP(const FString& Path, UClass* Parent)
{
    if (auto* Existing = LoadObject<UWidgetBlueprint>(nullptr, *Path)) return Existing;
    auto* Factory = NewObject<UWidgetBlueprintFactory>(); Factory->ParentClass = Parent;
    auto* BP = CastChecked<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(), CreatePackage(*Path), *FPackageName::GetShortName(Path), RF_Public | RF_Standalone, nullptr, GWarn));
    FAssetRegistryModule::AssetCreated(BP); return BP;
}
struct Designer
{
    UWidgetTree* T;
    template<class W> W* New(const TCHAR* Name) {return T->ConstructWidget<W>(W::StaticClass(),Name);}
    UCanvasPanelSlot* Place(UCanvasPanel* P, UWidget* W, FAnchors A, FMargin M)
    {auto* S = Cast<UCanvasPanelSlot>(W->Slot); if (W->GetParent()!=P) S=P->AddChildToCanvas(W); S->SetAnchors(A); S->SetOffsets(M); return S;}
    UTextBlock* Text(UCanvasPanel* P,const TCHAR* Name,const TCHAR* Value,int Size,FMargin M,FAnchors A=FAnchors(0,0),const TCHAR* Hex=TEXT("BCDDEE"))
    {
        auto* W=New<UTextBlock>(Name); W->SetText(FText::FromString(Value)); W->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")),Size,TEXT("Regular")));
        W->SetColorAndOpacity(C(Hex)); W->SetVisibility(ESlateVisibility::HitTestInvisible); W->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis); Place(P,W,A,M); return W;
    }
    UImage* Box(UCanvasPanel* P,const TCHAR* Name,const TCHAR* Hex,FMargin M,FAnchors A=FAnchors(0,0))
    {auto* W=New<UImage>(Name); W->SetColorAndOpacity(C(Hex)); W->SetVisibility(ESlateVisibility::HitTestInvisible); Place(P,W,A,M); return W;}
    USelectionButtonWidget* Button(UCanvasPanel* P,const TCHAR* Name,FMargin M,FAnchors A=FAnchors(0,0))
    {
        UClass* Class=LoadClass<USelectionButtonWidget>(nullptr,TEXT("/Game/System/UIBasic/WBP_SelectionButton.WBP_SelectionButton_C"));
        auto* W=T->ConstructWidget<USelectionButtonWidget>(Class,Name);
        W->MinimumSize=FVector2D::ZeroVector; W->ButtonText=FText::GetEmpty(); W->ButtonSubtitle=FText::GetEmpty(); W->ButtonIndex=FText::GetEmpty(); W->bUseTabStyle=true; W->Font.Size=15; W->IconSize=FVector2D(24); Place(P,W,A,M); return W;
    }
    UImage* Image(UCanvasPanel* P,const TCHAR* Name,FMargin M,FAnchors A=FAnchors(0,0))
    {auto* W=New<UImage>(Name); W->SetVisibility(ESlateVisibility::HitTestInvisible); Place(P,W,A,M); return W;}
};
UTexture2D* Icon(const TCHAR* Name, int Kind)
{
    const FString Path=Folder+TEXT("Icons/")+Name;
    if (auto* Existing=LoadObject<UTexture2D>(nullptr,*Path)) return Existing;
    const int W=Kind==3?160:64,H=Kind==3?90:64;
    TArray<FColor> Pixels; Pixels.Init(FColor(255,255,255,0),W*H);
    for(int Y=0;Y<H;++Y)for(int X=0;X<W;++X)
    {
        bool On=false;
        if(Kind==0) On=Y>=10&&Y<54&&((X>=17&&X<27)||(X>=37&&X<47));
        if(Kind==1) On=X>=18&&X<50&&FMath::Abs(Y-32)<= (50-X)*.68;
        if(Kind==2) On=(X>=7&&X<32&&FMath::Abs(Y-32)<(32-X)*.8)||(X>=32&&X<57&&FMath::Abs(Y-32)<(57-X)*.8);
        if(Kind==3)
        {
            On=(X>=25&&X<=135&&Y>=38&&Y<=59)||(X>=49&&X<=112&&Y>=26&&Y<=44)||FMath::Square(X-49)+FMath::Square(Y-60)<100||FMath::Square(X-113)+FMath::Square(Y-60)<100;
            if(FMath::Abs(Y-(91-X*.58))<5)On=false;
            if(FMath::Abs(Y-(82-X*.58))<2 && X>=25&&X<=137)On=true;
        }
        if(On)Pixels[Y*W+X]=Kind==3?FColor(95,143,174):FColor::White;
    }
    auto* Texture=NewObject<UTexture2D>(CreatePackage(*Path),*FPackageName::GetShortName(Path),RF_Public|RF_Standalone);
    Texture->Source.Init(W,H,1,1,TSF_BGRA8,reinterpret_cast<const uint8*>(Pixels.GetData())); Texture->CompressionSettings=TC_EditorIcon; Texture->LODGroup=TEXTUREGROUP_UI; Texture->MipGenSettings=TMGS_NoMipmaps; Texture->SRGB=true; Texture->UpdateResource(); FAssetRegistryModule::AssetCreated(Texture); check(Save(Texture)); return Texture;
}
void ImportNoVehicleIcon()
{
    const FString File=FPaths::ProjectDir()/TEXT("Art/UI/Icons/T_NoVehicle.png");
    checkf(IFileManager::Get().FileExists(*File),TEXT("Missing no-vehicle artwork: %s"),*File);
    const FString Path=Folder+TEXT("Icons/T_NoVehicle");
    if (auto* Existing=LoadObject<UTexture2D>(nullptr,*Path)) Backup(Existing);
    auto* Factory=NewObject<UTextureFactory>();
    Factory->SuppressImportOverwriteDialog(true);
    bool Cancelled=false;
    auto* Texture=CastChecked<UTexture2D>(Factory->FactoryCreateFile(UTexture2D::StaticClass(),CreatePackage(*Path),TEXT("T_NoVehicle"),RF_Public|RF_Standalone,File,nullptr,GWarn,Cancelled));
    check(!Cancelled);
    Texture->CompressionSettings=TC_EditorIcon;Texture->LODGroup=TEXTUREGROUP_UI;Texture->MipGenSettings=TMGS_NoMipmaps;Texture->MaxTextureSize=512;Texture->SRGB=true;
    Texture->PostEditChange();check(Save(Texture));
}
UWidgetBlueprint* TimeButtonBP()
{
    auto* BP=NewBP(Folder+TEXT("Components/WBP_OperationsTimeButton"),USelectionButtonWidget::StaticClass());
    if(BP->WidgetTree->RootWidget)return BP;
    Designer D{BP->WidgetTree};auto* Size=D.New<USizeBox>(TEXT("ButtonSize"));BP->WidgetTree->RootWidget=Size;Size->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* Row=D.New<UHorizontalBox>(TEXT("ButtonContent"));auto* Center=CastChecked<USizeBoxSlot>(Size->AddChild(Row));Center->SetHorizontalAlignment(HAlign_Center);Center->SetVerticalAlignment(VAlign_Center);
    auto* IconSize=D.New<USizeBox>(TEXT("IconSizeBox"));IconSize->SetWidthOverride(24);IconSize->SetHeightOverride(24);Row->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
    IconSize->AddChild(D.New<UImage>(TEXT("IconImage")));
    auto* Label=D.New<UTextBlock>(TEXT("Label"));Row->AddChildToHorizontalBox(Label)->SetVerticalAlignment(VAlign_Center);
    check(Finish(BP));return BP;
}
void RepairTimeButtons(UWidgetBlueprint* Room)
{
    auto* TimeButton=TimeButtonBP();Designer D{Room->WidgetTree};
    int Index=0;
    for(const TCHAR* Name:{TEXT("PauseButton"),TEXT("NormalSpeedButton"),TEXT("FastForwardButton")})
    {
        auto* Old=CastChecked<USelectionButtonWidget>(D.T->FindWidget(Name));
        if(Old->GetClass()!=TimeButton->GeneratedClass)
        {
            auto* Parent=CastChecked<UCanvasPanel>(Old->GetParent());auto* Slot=CastChecked<UCanvasPanelSlot>(Old->Slot);
            const auto Anchors=Slot->GetAnchors();const auto Offsets=Slot->GetOffsets();const auto Alignment=Slot->GetAlignment();const auto Tooltip=Old->GetToolTipText();
            RemoveSourceWidget(Old);
            auto* Button=D.T->ConstructWidget<USelectionButtonWidget>(TimeButton->GeneratedClass.Get(),Name);
            auto* Position=D.Place(Parent,Button,Anchors,Offsets);Position->SetAlignment(Alignment);
            Button->MinimumSize=FVector2D::ZeroVector;Button->Font.Size=14;Button->IconSize=FVector2D(24);Button->bUseTabStyle=true;Button->SetToolTipText(Tooltip);
            Button->ButtonText=Index==2?FText::FromString(TEXT("4×")):FText::GetEmpty();Button->ButtonSubtitle=FText::GetEmpty();Button->ButtonIndex=FText::GetEmpty();
            Button->ContentMode=Index==2?EBasicButtonContent::IconAndText:EBasicButtonContent::IconOnly;
            Button->IconBrush.SetResourceObject(Icon(Index==0?TEXT("T_TimePause"):Index==1?TEXT("T_TimePlay"):TEXT("T_TimeFast"),Index));Button->IconBrush.ImageSize=FVector2D(24);
        }
        ++Index;
    }
    check(Finish(Room));
}
UEdGraph* Events(UBlueprint* BP,const TCHAR* Name)
{auto* Graph=FBlueprintEditorUtils::CreateNewGraph(BP,Name,UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass()); FBlueprintEditorUtils::AddUbergraphPage(BP,Graph); return Graph;}
void Click(UWidgetBlueprint* BP,UEdGraph* Graph,FName Widget,FName Function,UClass* Owner)
{
    FGraph G(BP,Graph); G.Y=Graph->Nodes.Num()*160;
    auto* Field=FindFProperty<FObjectProperty>(BP->SkeletonGeneratedClass,Widget); check(Field);
    auto* Delegate=FindFProperty<FMulticastDelegateProperty>(UBasicButtonWidget::StaticClass(),TEXT("OnClicked"));
    auto* E=G.Node<UK2Node_ComponentBoundEvent>([&](auto* N){N->InitializeComponentBoundEventParams(Field,Delegate);});
    G.Exec(P(E,TEXT("then")),G.Call(Owner,Function));
}
UWidgetBlueprint* MemberBP()
{
    auto* BP=NewBP(Folder+TEXT("Components/WBP_OperationsMemberEntry"),UOperationsMemberEntryWidget::StaticClass());
    if(BP->WidgetTree->RootWidget)return BP;
    Designer D{BP->WidgetTree}; auto* Size=D.New<USizeBox>(TEXT("MemberSize"));Size->SetHeightOverride(94);BP->WidgetTree->RootWidget=Size;
    auto* Panel=D.New<UCanvasPanel>(TEXT("MemberLayout"));Size->AddChild(Panel);
    auto* Portrait=D.T->ConstructWidget<UPersonnelPortraitWidget>(LoadClass<UPersonnelPortraitWidget>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/Components/WBP_PersonnelPortrait.WBP_PersonnelPortrait_C")),TEXT("MemberPortrait"));
    Portrait->PortraitFormat=EPortraitFormat::Full;D.Place(Panel,Portrait,FAnchors(0,0),FMargin(16,7,45,80));
    D.Text(Panel,TEXT("MemberNameText"),TEXT("队员"),14,FMargin(76,22,128,24));
    D.Text(Panel,TEXT("CaptainText"),TEXT("队长"),10,FMargin(76,54,52,22),FAnchors(0,0),TEXT("40D9E8"));
    D.Box(Panel,TEXT("MemberRule"),TEXT("203C4B"),FMargin(14,-1,14,1),FAnchors(0,1,1,1));
    check(Finish(BP));return BP;
}
UWidgetBlueprint* SquadBP(UWidgetBlueprint* Member)
{
    auto* BP=NewBP(Folder+TEXT("Components/WBP_OperationsSquadEntry"),UOperationsSquadEntryWidget::StaticClass());
    if(BP->WidgetTree->RootWidget)return BP;
    Designer D{BP->WidgetTree};auto* Root=D.New<UVerticalBox>(TEXT("SquadLayout")); BP->WidgetTree->RootWidget=Root;
    auto* Size=D.New<USizeBox>(TEXT("HeaderSize"));Size->SetHeightOverride(98);Root->AddChild(Size);
    auto* Panel=D.New<UCanvasPanel>(TEXT("HeaderLayout"));Size->AddChild(Panel);
    auto* Button=D.Button(Panel,TEXT("HeaderButton"),FMargin(0),FAnchors(0,0,1,1)); Button->SetContentMode(EBasicButtonContent::TextOnly);
    D.Image(Panel,TEXT("SquadIconImage"),FMargin(12,28,40,40));
    D.Text(Panel,TEXT("SquadNameText"),TEXT("小队"),14,FMargin(64,25,119,24),FAnchors(0,0),TEXT("ECF4FF"));
    D.Text(Panel,TEXT("MemberCountText"),TEXT("成员 0/4"),11,FMargin(64,57,115,23));
    D.Text(Panel,TEXT("ExpandText"),TEXT("+"),15,FMargin(-141,26,18,24),FAnchors(1,0),TEXT("40D9E8"));
    D.Image(Panel,TEXT("VehicleImage"),FMargin(-126,14,112,63),FAnchors(1,0));
    D.Text(Panel,TEXT("VehicleEmptyText"),TEXT("无载具"),9,FMargin(-126,74,112,18),FAnchors(1,0))->SetJustification(ETextJustify::Center);
    auto* Reveal=D.New<USizeBox>(TEXT("MemberRevealSize"));Reveal->SetHeightOverride(0);Reveal->SetClipping(EWidgetClipping::ClipToBoundsAlways);Root->AddChild(Reveal);
    auto* Members=D.New<UVerticalBox>(TEXT("Members"));auto* MS=CastChecked<USizeBoxSlot>(Reveal->AddChild(Members));MS->SetVerticalAlignment(VAlign_Top);
    check(CompileWidget(BP));auto* Defaults=BP->GeneratedClass->GetDefaultObject<UOperationsSquadEntryWidget>();Defaults->MemberEntryClass=Member->GeneratedClass;Defaults->NoVehicleTexture=Icon(TEXT("T_NoVehicle"),3);
    Click(BP,Events(BP,TEXT("SquadRowEvents")),TEXT("HeaderButton"),TEXT("RequestExpansion"),UOperationsSquadEntryWidget::StaticClass());
    check(Finish(BP));return BP;
}
void SpliceAfter(UBlueprint* BP,UK2Node_CallFunction* Existing,UClass* Owner,FName Function)
{
    FGraph G(BP,Existing->GetGraph());auto* Out=Existing->GetThenPin();TArray<UEdGraphPin*> Links=Out->LinkedTo;Out->BreakAllPinLinks();auto* Node=G.Call(Owner,Function); Node->NodePosX=Existing->NodePosX+300;Node->NodePosY=Existing->NodePosY;G.Exec(Out,Node);for(auto* Target:Links)Link(Node->GetThenPin(),Target);
}
void RoomUI(UWidgetBlueprint* BP,UWidgetBlueprint* Row)
{
    Designer D{BP->WidgetTree};auto FindWidget=[&](const TCHAR* N){auto* W=D.T->FindWidget(N);check(W);return W;};
    auto* Time=CastChecked<UCanvasPanel>(FindWidget(TEXT("TimeControlPanel")));CastChecked<UCanvasPanelSlot>(Time->Slot)->SetSize(FVector2D(0,156));
    auto* Info=CastChecked<UCanvasPanel>(FindWidget(TEXT("TileInfoPanel")));CastChecked<UCanvasPanelSlot>(Info->Slot)->SetOffsets(FMargin(0,168,0,0));
    for(const TCHAR* N:{TEXT("DayProgress"),TEXT("DayStartLabel"),TEXT("DayEndLabel"),TEXT("TimeStateText")})if(auto* W=D.T->FindWidget(N))RemoveSourceWidget(W);
    CastChecked<UTextBlock>(FindWidget(TEXT("TimePanelTitle")))->SetText(FText::FromString(TEXT("时间控制")));
    D.Place(Time,FindWidget(TEXT("DateText")),FAnchors(0,0),FMargin(20,62,175,25));
    D.Place(Time,FindWidget(TEXT("ClockText")),FAnchors(1,0),FMargin(-138,52,118,38));CastChecked<UTextBlock>(FindWidget(TEXT("ClockText")))->SetJustification(ETextJustify::Right);
    auto* Pause=D.Button(Time,TEXT("PauseButton"),FMargin(20,105,100,36));
    auto* Normal=CastChecked<USelectionButtonWidget>(FindWidget(TEXT("NormalSpeedButton"))); D.Place(Time,Normal,FAnchors(0,0),FMargin(130,105,100,36));
    auto* Fast=CastChecked<USelectionButtonWidget>(FindWidget(TEXT("FastForwardButton")));D.Place(Time,Fast,FAnchors(0,0),FMargin(240,105,100,36));
    int I=0;for(auto* B:{Pause,Normal,Fast}){ B->MinimumSize=FVector2D::ZeroVector;B->Font.Size=14;B->IconSize=FVector2D(24);B->IconBrush.SetResourceObject(Icon(I==0?TEXT("T_TimePause"):I==1?TEXT("T_TimePlay"):TEXT("T_TimeFast"),I));B->IconBrush.ImageSize=FVector2D(24);B->ContentMode=I==2?EBasicButtonContent::IconAndText:EBasicButtonContent::IconOnly;++I; }
    Pause->SetToolTipText(FText::FromString(TEXT("暂停游戏时间与角色")));Normal->SetToolTipText(FText::FromString(TEXT("正常：1 秒推进 1 分钟")));Fast->SetToolTipText(FText::FromString(TEXT("循环快进倍率")));
    D.Text(Info,TEXT("TileIdText"),TEXT("T5"),16,FMargin(-80,13,60,26),FAnchors(1,0),TEXT("ECF4FF"))->SetJustification(ETextJustify::Right);
    D.Text(Info,TEXT("SquadHeading"),TEXT("驻留小队"),13,FMargin(20,63,180,25));
    D.Text(Info,TEXT("SquadCountText"),TEXT("0 支"),13,FMargin(-100,63,80,25),FAnchors(1,0))->SetJustification(ETextJustify::Right);
    auto* Content=CastChecked<UOverlay>(FindWidget(TEXT("TileInfoContent")));D.Place(Info,Content,FAnchors(0,0,1,1),FMargin(14,98,14,104));
    auto* Scroll=D.New<UScrollBox>(TEXT("SquadList"));Scroll->SetAnimateWheelScrolling(true);auto* SS=Content->AddChildToOverlay(Scroll);SS->SetHorizontalAlignment(HAlign_Fill);SS->SetVerticalAlignment(VAlign_Fill);
    D.Text(Info,TEXT("EmptySquadsText"),TEXT("暂无驻留小队\n有小队驻留时可进入"),12,FMargin(28,134,300,62))->SetJustification(ETextJustify::Center);
    auto* Enter=D.Button(Info,TEXT("EnterMapButton"),FMargin(16,-76,16,56),FAnchors(0,1,1,1));Enter->ButtonText=FText::FromString(TEXT("进入地图   →"));Enter->ContentMode=EBasicButtonContent::TextOnly;Enter->SetIsEnabled(false);
    check(CompileWidget(BP));auto* Defaults=BP->GeneratedClass->GetDefaultObject<UOperationsCommandRoomWidget>();Defaults->SquadEntryClass=Row->GeneratedClass;Defaults->NormalTimeScale=60.;Defaults->FastForwardMultipliers={4.,8.,16.};Defaults->ClockFormat=FText::FromString(TEXT("{Hour}:{Minute}"));
    // Retain authored lifecycle events; append list refresh to their existing presentation function.
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);for(auto* G:Graphs){const auto Nodes=G->Nodes;for(UEdGraphNode* N:Nodes)if(auto* Call=Cast<UK2Node_CallFunction>(N)) if(Call->FunctionReference.GetMemberName()==TEXT("RefreshCommandTileInfoPanel")) SpliceAfter(BP,Call,UOperationsCommandRoomWidget::StaticClass(),TEXT("RefreshSquadList"));}
    // Name varies only in the early prototype; connect after Prepare too for an explicit initial population.
    for(auto* G:Graphs){const auto Nodes=G->Nodes;for(UEdGraphNode* N:Nodes)if(auto* Call=Cast<UK2Node_CallFunction>(N))if(Call->FunctionReference.GetMemberName()==TEXT("PrepareCommandRoomUI"))SpliceAfter(BP,Call,UOperationsCommandRoomWidget::StaticClass(),TEXT("RefreshSquadList"));}
    auto* Graph=Events(BP,TEXT("OperationsPanelEvents"));Click(BP,Graph,TEXT("PauseButton"),TEXT("PauseTime"),UOperationsCommandRoomWidget::StaticClass());Click(BP,Graph,TEXT("EnterMapButton"),TEXT("RequestEnterSelectedTile"),UOperationsCommandRoomWidget::StaticClass());
    for(FName Name:{FName(TEXT("OnCommandSquadsChanged")),FName(TEXT("OnEnterTileMapRequested"))})
    {
        FGraph G(BP,Graph);auto* E=G.Node<UK2Node_Event>([&](auto* N){N->EventReference.SetExternalMember(Name,UOperationsCommandRoomWidget::StaticClass());N->bOverrideFunction=true;});
        E->NodePosY=Name==TEXT("OnCommandSquadsChanged")?500:900;
        if(Name==TEXT("OnCommandSquadsChanged"))G.Exec(P(E,TEXT("then")),G.Call(UOperationsCommandRoomWidget::StaticClass(),TEXT("RefreshSquadList")));
        else E->NodeComment=TEXT("地图入口扩展点：TileId 与最新 SquadIds 已验证。具体地图通过地图切换插件的子地图 Handler 加载。");
    }
    check(Finish(BP));
}
void Scene(UBlueprint* BP)
{
    Var(BP,TEXT("DefaultSelectedTileId"),Type(UEdGraphSchema_K2::PC_Name),TEXT("T5"),TEXT("进入时默认瓦片"),true,TEXT("作战指挥室|地图"));
    auto* Graph=Declare(BP,TEXT("Room_SelectDefaultTile"));check(Compile(BP)); FGraph G(BP,Graph);
    auto* Get=G.Call(UGameplayStatics::StaticClass(),TEXT("GetActorOfClass"));P(Get,TEXT("ActorClass"))->DefaultObject=ABaseSandboxMap::StaticClass();
    auto* ActorCast=G.CastTo(ABaseSandboxMap::StaticClass(),Get->GetReturnValuePin(),G.Exec(G.Start(),Get));
    auto* Tile=G.Call(AGSMMap3D::StaticClass(),TEXT("GetTileById"));Link(ActorCast->GetCastResultPin(),P(Tile,TEXT("self")));Link(G.Get(TEXT("DefaultSelectedTileId")),P(Tile,TEXT("TileId")));
    auto* Guard=G.Branch(ActorCast->GetValidCastPin(),G.Valid(Tile->GetReturnValuePin()));auto* Select=G.Call(AGSMMap3D::StaticClass(),TEXT("SwitchSelectedTile"));Link(ActorCast->GetCastResultPin(),P(Select,TEXT("self")));Link(Tile->GetReturnValuePin(),P(Select,TEXT("NewSelectedTile")));G.Exec(Guard->GetThenPin(),Select);
    auto* Enter=Find(BP,TEXT("EnterScene"));UK2Node_FunctionEntry* Entry=nullptr;for(UEdGraphNode* N:Enter->Nodes)if(auto* E=Cast<UK2Node_FunctionEntry>(N))Entry=E;check(Entry);auto* Out=P(Entry,TEXT("then"));auto Links=Out->LinkedTo;Out->BreakAllPinLinks();FGraph EG(BP,Enter);auto* Call=EG.Call(TEXT("Room_SelectDefaultTile"));EG.Exec(Out,Call);for(auto* L:Links)Link(Call->GetThenPin(),L);
    check(Compile(BP));check(Save(BP));
}
void VehicleAspect(UWidgetBlueprint* BP,const TCHAR* ImageName)
{
    auto* Picture=CastChecked<UImage>(BP->WidgetTree->FindWidget(ImageName));auto* Fit=CastChecked<UScaleBox>(Picture->GetParent());auto* Slot=CastChecked<UScaleBoxSlot>(Picture->Slot);
    Fit->SetStretch(EStretch::ScaleToFit);Slot->SetHorizontalAlignment(HAlign_Center);Slot->SetVerticalAlignment(VAlign_Center);
    FSlateBrush B=Picture->GetBrush();B.ImageSize=FVector2D(320,180);Picture->SetBrush(B);
    check(Finish(BP));
}
void PortraitViews()
{
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(Base+TEXT("PersonnelPreparationRoom/Components/WBP_PersonnelPortrait")));check(BP);Backup(BP);
    auto* Image=CastChecked<UImage>(BP->WidgetTree->FindWidget(TEXT("PortraitImage")));
    if(!Cast<UScaleBox>(Image->GetParent()))
    {
        auto* Parent=CastChecked<UCanvasPanel>(Image->GetParent());auto* Old=CastChecked<UCanvasPanelSlot>(Image->Slot);
        const auto Anchors=Old->GetAnchors();const auto Offsets=Old->GetOffsets();const auto Alignment=Old->GetAlignment();const int Z=Old->GetZOrder();
        Image->RemoveFromParent();Designer D{BP->WidgetTree};auto* Fit=D.New<UScaleBox>(TEXT("PortraitAspectFit"));Fit->SetStretch(EStretch::ScaleToFit);Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Position=D.Place(Parent,Fit,Anchors,Offsets);Position->SetAlignment(Alignment);Position->SetZOrder(Z);
        auto* Slot=CastChecked<UScaleBoxSlot>(Fit->AddChild(Image));Slot->SetHorizontalAlignment(HAlign_Center);Slot->SetVerticalAlignment(VAlign_Center);
    }
    check(Finish(BP));
    // The identity panel has a full-height portrait; list rows and emblems intentionally remain square.
    auto* Room=LoadObject<UWidgetBlueprint>(nullptr,*(Base+TEXT("PersonnelPreparationRoom/WBP_PersonnelPreparationRoom")));check(Room);Backup(Room);
    auto* Portrait=CastChecked<UPersonnelPortraitWidget>(Room->WidgetTree->FindWidget(TEXT("DetailPortrait")));Portrait->PortraitFormat=EPortraitFormat::Full;
    auto* Slot=CastChecked<UCanvasPanelSlot>(Portrait->Slot);Slot->SetOffsets(FMargin(16,18,81,144));
    if(auto* Caption=Room->WidgetTree->FindWidget(TEXT("IdentityCaption")))CastChecked<UCanvasPanelSlot>(Caption->Slot)->SetOffsets(FMargin(16,164,112,18));
    check(Finish(Room));
}
}
UOperationsPanelUpgradeCommandlet::UOperationsPanelUpgradeCommandlet(){IsClient=false;IsEditor=true;LogToConsole=true;}
int32 UOperationsPanelUpgradeCommandlet::Main(const FString& Params)
{
    using namespace OperationsUpgrade; BackupFolder=FPaths::ProjectSavedDir()/TEXT("OperationsPanelUpgrade/AssetBackup")/FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
    auto* Room=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+TEXT("WBP_OperationsCommandRoom")));
    auto* SceneBP=LoadObject<UBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/Scene/BP_作战指挥室_Scene"));
    auto* Meeting=LoadObject<UWidgetBlueprint>(nullptr,*(Base+TEXT("SquadMeetingRoom/WBP_SquadMeetingRoom")));
    auto* Vehicle=LoadObject<UWidgetBlueprint>(nullptr,*(Base+TEXT("SquadMeetingRoom/Components/WBP_SquadVehicleEntry")));
    check(Room&&SceneBP&&Meeting&&Vehicle);
    if(Params.Contains(TEXT("Inspect")))
    {
        for(auto* BP:{Room,Meeting,Vehicle}) BP->WidgetTree->ForEachWidget([&](UWidget* W){if(auto* I=Cast<UImage>(W)) UE_LOG(LogTemp,Display,TEXT("UI_IMAGE %s.%s size=%s"),*BP->GetName(),*I->GetName(),*I->GetBrush().GetImageSize().ToString());});
        for(int I=1;I<=4;++I){auto* T=LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/System/SubSystem/PlayerUnitSubSystem/Portraits/T_TestPortrait_%02d"),I));if(T)UE_LOG(LogTemp,Display,TEXT("PORTRAIT %s %dx%d"),*T->GetName(),T->GetImportedSize().X,T->GetImportedSize().Y);}
        return 0;
    }
    if(!Params.Contains(TEXT("Apply")))return 1;
    const bool AlreadyHasPanel=Room->WidgetTree->FindWidget(TEXT("SquadList"))!=nullptr;
    for(UObject* Obj:{static_cast<UObject*>(Room),static_cast<UObject*>(SceneBP),static_cast<UObject*>(Meeting),static_cast<UObject*>(Vehicle)})Backup(Obj);
    if(!AlreadyHasPanel)PortraitViews();auto* Member=MemberBP();auto* Squad=SquadBP(Member);if(!AlreadyHasPanel)RoomUI(Room,Squad);if(!Find(SceneBP,TEXT("Room_SelectDefaultTile")))Scene(SceneBP);VehicleAspect(Meeting,TEXT("VehiclePreviewImage"));VehicleAspect(Vehicle,TEXT("VehicleImage"));
    RepairTimeButtons(Room);ImportNoVehicleIcon();
    UE_LOG(LogTemp,Display,TEXT("OPERATIONS_UPGRADE_OK backup=%s"),*BackupFolder);return 0;
}
