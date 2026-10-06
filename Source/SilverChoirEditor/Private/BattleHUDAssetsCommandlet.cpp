#include "BattleHUDAssetsCommandlet.h"
#include "BattlePersonnelCardAssets.h"
#include "BattleModeAssets.h"
#include "BattlePanelAssets.h"
#include "BattleSquadEntryAssets.h"
#include "MainMapBlueprintBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "UObject/SavePackage.h"
#include "K2Node_SwitchEnum.h"
#include "Map/BattleMap/BattleHUDWidgets.h"
#include "Map/BattleMap/BattleMapWidget.h"
#include "Map/GameMainMap/GameMainMapPlayerController.h"

using namespace MainMapBP;
namespace BattleAssets
{
const FString Folder=TEXT("/Game/System/Map/BattleMap/UI/");
FLinearColor C(const TCHAR* Hex){return FLinearColor(FColor::FromHex(Hex));}
bool Save(UObject* Asset)
{
    Asset->MarkPackageDirty();const FString File=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    if(FPaths::FileExists(File))
    {
        const FString Backup=FPaths::ProjectSavedDir()/TEXT("BattleHUD/Backup")/FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))/Asset->GetOutermost()->GetName().Mid(6)+TEXT(".uasset");
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup),true);if(IFileManager::Get().Copy(*Backup,*File)!=COPY_OK)return false;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs A;A.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(Asset->GetOutermost(),Asset,*File,A);
}
bool Finish(UWidgetBlueprint* BP)
{
    BP->ForEachSourceWidget([&](UWidget* W){if(!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewGuid());});
    return Compile(BP)&&Save(BP);
}
UWidgetBlueprint* NewBP(const FString& Name,UClass* Parent)
{
    const FString Path=Folder+Name;
    if(auto* Existing=LoadObject<UWidgetBlueprint>(nullptr,*Path))return Existing;
    auto* F=NewObject<UWidgetBlueprintFactory>();F->ParentClass=Parent;
    auto* BP=CastChecked<UWidgetBlueprint>(F->FactoryCreateNew(UWidgetBlueprint::StaticClass(),CreatePackage(*Path),*FPackageName::GetShortName(Path),RF_Public|RF_Standalone,nullptr,GWarn));FAssetRegistryModule::AssetCreated(BP);return BP;
}
struct Designer
{
    UWidgetTree* T;
    template<class W>W* New(const TCHAR* Name){return T->ConstructWidget<W>(W::StaticClass(),Name);}
    UCanvasPanel* Root(const TCHAR* Name){auto* P=New<UCanvasPanel>(Name);P->SetVisibility(ESlateVisibility::SelfHitTestInvisible);T->RootWidget=P;return P;}
    UCanvasPanelSlot* At(UCanvasPanel* P,UWidget* W,FMargin M,FAnchors A=FAnchors(0,0))
    {auto* S=P->AddChildToCanvas(W);S->SetAnchors(A);S->SetOffsets(M);return S;}
    UCanvasPanel* Panel(UCanvasPanel* P,const TCHAR* Name,FMargin M,FAnchors A=FAnchors(0,0))
    {auto* W=New<UCanvasPanel>(Name);W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);At(P,W,M,A);return W;}
    UImage* Box(UCanvasPanel* P,const TCHAR* Name,const TCHAR* Color,FMargin M,FAnchors A=FAnchors(0,0))
    {auto* W=New<UImage>(Name);W->SetColorAndOpacity(C(Color));W->SetVisibility(ESlateVisibility::HitTestInvisible);At(P,W,M,A);return W;}
    void Frame(UCanvasPanel* P,const FString& Prefix,bool Accent=false)
    {
        Box(P,*(Prefix+TEXT("Plate")),TEXT("071520F7"),FMargin(0),FAnchors(0,0,1,1));
        const TCHAR* Line=Accent?TEXT("24B7CDD0"):TEXT("245368C0");
        Box(P,*(Prefix+TEXT("Top")),Line,FMargin(0,0,0,1),FAnchors(0,0,1,0));Box(P,*(Prefix+TEXT("Bottom")),Line,FMargin(0,-1,0,1),FAnchors(0,1,1,1));
        Box(P,*(Prefix+TEXT("Left")),Line,FMargin(0,0,1,0),FAnchors(0,0,0,1));Box(P,*(Prefix+TEXT("Right")),Line,FMargin(-1,0,1,0),FAnchors(1,0,1,1));
    }
    UTextBlock* Text(UCanvasPanel* P,const TCHAR* Name,const TCHAR* Value,int Size,FMargin M,FAnchors A=FAnchors(0,0),const TCHAR* Color=TEXT("C3DFEA"))
    {
        auto* W=New<UTextBlock>(Name);W->SetText(FText::FromString(Value));W->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")),Size));W->SetColorAndOpacity(C(Color));W->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);W->SetVisibility(ESlateVisibility::HitTestInvisible);At(P,W,M,A);return W;
    }
    UBattleHUDVisual* Visual(UCanvasPanel* P,const TCHAR* Name,EBattleGlyph G,FMargin M,FAnchors A=FAnchors(0,0))
    {auto* W=New<UBattleHUDVisual>(Name);W->Glyph=G;W->SetVisibility(ESlateVisibility::HitTestInvisible);At(P,W,M,A);return W;}
    UBattleHUDButton* Button(UCanvasPanel* P,UClass* Class,const TCHAR* Name,const TCHAR* Choice,EBattleGlyph G,const TCHAR* Tip,FMargin M,FAnchors A=FAnchors(0,0))
    {
        auto* W=T->ConstructWidget<UBattleHUDButton>(Class,Name);W->ChoiceID=Choice;W->Glyph=G;W->MinimumSize=FVector2D::ZeroVector;W->SetToolTipText(FText::FromString(Tip));At(P,W,M,A);return W;
    }
};
void ButtonDefaults(UBasicButtonWidget* B)
{
    if(auto* Selection=Cast<USelectionButtonWidget>(B))Selection->bUseTabStyle=false;
    B->MinimumSize=FVector2D::ZeroVector;B->BackgroundColor=C(TEXT("071823F5"));B->BorderColor=C(TEXT("275D74"));B->AccentColor=C(TEXT("10D6EB"));B->ButtonForegroundColor=C(TEXT("C3DDE9"));B->Font.Size=11;B->ButtonText=FText::GetEmpty();B->ButtonIndex=FText::GetEmpty();B->ButtonSubtitle=FText::GetEmpty();
}
UWidgetBlueprint* MakeButton()
{
    auto* BP=NewBP(TEXT("Components/WBP_BattleIconButton"),UBattleHUDButton::StaticClass());if(BP->WidgetTree->RootWidget)return BP;
    Designer D{BP->WidgetTree};auto* R=D.Root(TEXT("ButtonRoot"));R->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* Symbol=D.Visual(R,TEXT("Symbol"),EBattleGlyph::Person,FMargin(-10,-16,20,20),FAnchors(.5,.5));Symbol->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* Label=D.Text(R,TEXT("Label"),TEXT(""),10,FMargin(2,-17,2,16),FAnchors(0,1,1,1));Label->SetJustification(ETextJustify::Center);
    check(Finish(BP));ButtonDefaults(CastChecked<UBasicButtonWidget>(BP->GeneratedClass->GetDefaultObject()));check(Save(BP));return BP;
}
UWidgetBlueprint* MakeMember()
{
    auto* BP=NewBP(TEXT("Components/WBP_BattleMemberCard"),UBattleMemberCardWidget::StaticClass());if(BP->WidgetTree->RootWidget)return BP;
    Designer D{BP->WidgetTree};auto* R=D.Root(TEXT("MemberCardRoot"));R->SetVisibility(ESlateVisibility::HitTestInvisible);
    D.Text(R,TEXT("MemberName"),TEXT("成员"),14,FMargin(9,7,8,20),FAnchors(0,0,1,0));
    auto* Portrait=D.New<UImage>(TEXT("Portrait"));Portrait->SetVisibility(ESlateVisibility::HitTestInvisible);D.At(R,Portrait,FMargin(9,33,52,52));
    for(bool Morale:{false,true})
    {
        auto* B=D.New<UProgressBar>(Morale?TEXT("MoraleBar"):TEXT("StaminaBar"));B->SetBarFillType(EProgressBarFillType::BottomToTop);B->SetPercent(.75);B->SetFillColorAndOpacity(C(Morale?TEXT("40D387"):TEXT("399DEB")));B->SetBorderPadding({0,0});
        FProgressBarStyle Style;Style.BackgroundImage.DrawAs=ESlateBrushDrawType::Image;Style.BackgroundImage.TintColor=C(TEXT("173447"));Style.FillImage.DrawAs=ESlateBrushDrawType::Image;Style.FillImage.TintColor=FLinearColor::White;B->SetWidgetStyle(Style);B->SetVisibility(ESlateVisibility::HitTestInvisible);D.At(R,B,FMargin(Morale?75:67,33,4,52));
    }
    auto* ECG=D.Visual(R,TEXT("Heartbeat"),EBattleGlyph::Person,FMargin(91,34,10,32),FAnchors(0,0,1,0));ECG->Kind=EBattleVisualKind::ECG;
    auto* Health=D.Text(R,TEXT("HealthText"),TEXT("稳定"),11,FMargin(91,70,10,16),FAnchors(0,0,1,0),TEXT("37CEE2"));Health->SetJustification(ETextJustify::Center);
    auto* Joined=D.Panel(R,TEXT("MergedHands"),FMargin(8,-45,8,36),FAnchors(0,1,1,1));D.Frame(Joined,TEXT("Linked"));D.Visual(Joined,TEXT("LinkedWeapon"),EBattleGlyph::Rifle,FMargin(22,4,22,5),FAnchors(0,0,1,1));
    auto* Split=D.Panel(R,TEXT("SplitHands"),FMargin(8,-45,8,36),FAnchors(0,1,1,1));
    auto* Left=D.Panel(Split,TEXT("LeftHandFrame"),FMargin(0,0,3,0),FAnchors(0,0,.5,1));D.Frame(Left,TEXT("LeftHand"));D.Visual(Left,TEXT("LeftWeapon"),EBattleGlyph::EmptyHand,FMargin(18,4,18,4),FAnchors(0,0,1,1));
    auto* Right=D.Panel(Split,TEXT("RightHandFrame"),FMargin(3,0,0,0),FAnchors(.5,0,1,1));D.Frame(Right,TEXT("RightHand"));D.Visual(Right,TEXT("RightWeapon"),EBattleGlyph::Pistol,FMargin(16,4,16,4),FAnchors(0,0,1,1));
    check(Finish(BP));ButtonDefaults(CastChecked<UBasicButtonWidget>(BP->GeneratedClass->GetDefaultObject()));check(Save(BP));return BP;
}
UBattleHUDTestData* MakeData()
{
    const FString Path=Folder+TEXT("Data/DA_BattleHUDTestUnits");if(auto* Existing=LoadObject<UBattleHUDTestData>(nullptr,*Path))return Existing;
    auto* Data=NewObject<UBattleHUDTestData>(CreatePackage(*Path),TEXT("DA_BattleHUDTestUnits"),RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(Data);
    auto* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/System/SubSystem/PlayerUnitSubSystem/DT_UnitTemplates"));TArray<FUnitTemplate*> Templates;if(Table)Table->GetAllRows(TEXT("BattleHUD"),Templates);
    const TCHAR* Names[]={TEXT("白露"),TEXT("磐石"),TEXT("寒锋"),TEXT("夜莺"),TEXT("银针"),TEXT("脉冲")};
    const int Portraits[]={2,4,1,3,2,1};
    for(int S=0;S<3;++S)
    {
        FBattleSquadView Squad;Squad.SquadId=FGuid(0xBA770001,S+1,1,1);Squad.Name=FText::FromString(S==0?TEXT("第一小队"):S==1?TEXT("第二小队"):TEXT("第三小队"));
        const int Count=S==0?6:S==1?4:3;
        for(int I=0;I<Count;++I)
        {
            FBattleMemberView M;M.UnitId=FGuid(0xBA770010,S+1,I+1,1);if(Templates.IsValidIndex(I))M.Profile=Templates[I]->Profile;
            M.Profile.CodeName=FText::FromString(S==0?Names[I]:*FString::Printf(TEXT("%s %d"),S==1?TEXT("巡逻员"):TEXT("支援员"),I+1));
            if(!M.Profile.PortraitTexture)M.Profile.PortraitTexture=LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/System/SubSystem/PlayerUnitSubSystem/Portraits/T_TestPortrait_%02d"),Portraits[I]));
            M.Stamina=.92f-I*.095f;M.Morale=.88f-I*.06f;M.Health=I==3?.48f:I==5?.24f:1.f;M.MapPosition={.34+I*.054,.38+(I%3)*.065};
            M.RightItemId=FGuid(0xBA770020,S+1,I+1,1);
            if(I!=3&&I!=5){M.LeftItemId=M.RightItemId;M.LeftFallback=EBattleGlyph::Rifle;M.RightFallback=EBattleGlyph::Rifle;}
            Squad.Members.Add(M);
        }
        if(S!=2){Squad.Vehicle.VehicleId=FGuid(0xBA770030,S+1,1,1);Squad.Vehicle.Name=FText::FromString(S==0?TEXT("六座越野车"):TEXT("四座轿车"));Squad.Vehicle.Seats=S==0?6:4;Squad.Vehicle.Fuel=.72f;Squad.Vehicle.Condition=.86f;Squad.Vehicle.Image=LoadObject<UTexture2D>(nullptr,S==0?TEXT("/Game/System/Data/Vehicles/Images/T_Vehicle_SUV_6Seat"):TEXT("/Game/System/Data/Vehicles/Images/T_Vehicle_Sedan_4Seat"));}
        Data->Squads.Add(Squad);
    }
    check(Save(Data));return Data;
}

void LayoutHUD(UWidgetBlueprint* BP,UClass* ButtonClass)
{
    Designer D{BP->WidgetTree};auto* R=D.Root(TEXT("HUDLayer"));R->SetVisibility(ESlateVisibility::Visible);
    auto* Dock=D.Panel(R,TEXT("DockPanel"),FMargin(-900,-164,1800,148),FAnchors(.5,1));
    auto* Rail=D.New<UVerticalBox>(TEXT("SquadRail"));D.At(Dock,Rail,FMargin(0,0,40,148));
    D.Text(R,TEXT("SquadNameText"),TEXT("第一小队"),12,FMargin(24,-187,240,18),FAnchors(0,1),TEXT("82B4CB"));
    auto* Feedback=D.Text(R,TEXT("FeedbackText"),TEXT(""),14,FMargin(-350,-219,700,22),FAnchors(.5,1),TEXT("71D2E8"));Feedback->SetJustification(ETextJustify::Center);Feedback->SetVisibility(ESlateVisibility::Collapsed);
    auto* Drawer=D.Panel(Dock,TEXT("MemberDrawerPanel"),FMargin(650,0,180,148));D.Frame(Drawer,TEXT("MemberDrawer"),true);Drawer->SetClipping(EWidgetClipping::ClipToBounds);Drawer->SetVisibility(ESlateVisibility::Collapsed);
    D.Text(Drawer,TEXT("CommandMemberName"),TEXT("成员"),12,FMargin(8,5,130,18));D.Button(Drawer,ButtonClass,TEXT("CloseMemberDrawer"),TEXT("CloseDrawer"),EBattleGlyph::Chevron,TEXT("收起人员指令"),FMargin(153,3,22,20));
    const TCHAR* Choices[]={TEXT("Stand"),TEXT("Crouch"),TEXT("Prone"),TEXT("Backpack"),TEXT("Stealth"),TEXT("Interact"),TEXT("Pickup"),TEXT("Quick1"),TEXT("Quick2"),TEXT("Quick3"),TEXT("Quick4")};
    const TCHAR* Tips[]={TEXT("站立"),TEXT("蹲伏"),TEXT("匍匐"),TEXT("打开背包"),TEXT("切换潜行"),TEXT("选择交互目标"),TEXT("拾取物品 / 拖拽队友"),TEXT("医疗包"),TEXT("破片手雷"),TEXT("烟雾弹"),TEXT("工具组")};
    const EBattleGlyph Glyphs[]={EBattleGlyph::Standing,EBattleGlyph::Crouching,EBattleGlyph::Prone,EBattleGlyph::Backpack,EBattleGlyph::Stealth,EBattleGlyph::Hand,EBattleGlyph::Pickup,EBattleGlyph::Medkit,EBattleGlyph::Grenade,EBattleGlyph::Smoke,EBattleGlyph::Tool};
    for(int I=0;I<11;++I)
    {
        const int Row=I<3?0:I<7?1:2;const int Col=I<3?I:I<7?I-3:I-7;const float W=Row==0?52:38;const float H=Row==2?40:32;
        auto* B=D.Button(Drawer,ButtonClass,*FString::Printf(TEXT("Command_%s"),Choices[I]),Choices[I],Glyphs[I],Tips[I],FMargin(8+Col*(W+4),28+Row*37,W,H));
        if(Row==2)B->ButtonText=FText::FromString(FString::Printf(TEXT("%d · 0"),Col+1));
    }
    auto* Vehicle=D.Panel(Dock,TEXT("VehicleDrawerPanel"),FMargin(1300,0,290,148));D.Frame(Vehicle,TEXT("VehicleDrawer"),true);Vehicle->SetClipping(EWidgetClipping::ClipToBounds);Vehicle->SetVisibility(ESlateVisibility::Collapsed);
    D.Text(Vehicle,TEXT("VehicleName"),TEXT("小队车辆"),13,FMargin(8,5,220,20));D.Button(Vehicle,ButtonClass,TEXT("CloseVehicleDrawer"),TEXT("CloseDrawer"),EBattleGlyph::Chevron,TEXT("收起车辆信息"),FMargin(260,4,22,20));
    auto* VI=D.New<UImage>(TEXT("VehicleImage"));VI->SetVisibility(ESlateVisibility::HitTestInvisible);D.At(Vehicle,VI,FMargin(8,30,136,76.5));
    D.Text(Vehicle,TEXT("VehicleStats"),TEXT("车况  86%\n燃油  72%\n乘员  0 / 6"),11,FMargin(156,34,127,68));
    const TCHAR* VC[]={TEXT("BoardVehicle"),TEXT("LeaveVehicle"),TEXT("VehicleCargo"),TEXT("LocateVehicle")};const TCHAR* VT[]={TEXT("登车"),TEXT("下车"),TEXT("货舱"),TEXT("定位")};const EBattleGlyph VG[]={EBattleGlyph::Enter,EBattleGlyph::Exit,EBattleGlyph::Cargo,EBattleGlyph::Locate};
    for(int I=0;I<4;++I){auto* B=D.Button(Vehicle,ButtonClass,*FString::Printf(TEXT("Vehicle_%d"),I),VC[I],VG[I],VT[I],FMargin(8+I*69,102,64,40));B->ButtonText=FText::FromString(VT[I]);}
    auto* VB=D.Button(Dock,ButtonClass,TEXT("VehicleButton"),TEXT("Vehicle"),EBattleGlyph::Vehicle,TEXT("小队车辆"),FMargin(1720,0,32,148));VB->ButtonText=FText::FromString(TEXT("车\n辆"));VB->bVerticalLabel=true;
    auto* Modes=D.Panel(Dock,TEXT("ModeRail"),FMargin(1760,0,40,148));
    auto* MemberMode=D.Button(Modes,ButtonClass,TEXT("MemberModeButton"),TEXT("MemberMode"),EBattleGlyph::Person,TEXT("成员控制"),FMargin(0,0,40,72));MemberMode->ButtonText=FText::FromString(TEXT("成员"));
    auto* SquadMode=D.Button(Modes,ButtonClass,TEXT("SquadModeButton"),TEXT("SquadMode"),EBattleGlyph::Squad,TEXT("小队控制（后续接入）"),FMargin(0,76,40,72));SquadMode->ButtonText=FText::FromString(TEXT("小队"));
    auto* Map=D.Panel(R,TEXT("MinimapPanel"),FMargin(-272,16,256,210),FAnchors(1,0));D.Frame(Map,TEXT("Pad"));D.Text(Map,TEXT("MinimapCaption"),TEXT("TACTICAL / 战术示意"),10,FMargin(9,7,230,18),FAnchors(),TEXT("84B5CC"));
    auto* Tactical=D.Visual(Map,TEXT("TacticalMap"),EBattleGlyph::Locate,FMargin(8,29,240,142));Tactical->Kind=EBattleVisualKind::MiniMap;Tactical->SetClipping(EWidgetClipping::ClipToBounds);
    const TCHAR* MC[]={TEXT("MapFollow"),TEXT("MapLayers"),TEXT("MapZoomIn"),TEXT("MapZoomOut"),TEXT("MapCollapse")};const TCHAR* MT[]={TEXT("跟随选中成员"),TEXT("网格图层"),TEXT("放大"),TEXT("缩小"),TEXT("收起 / 展开小地图")};const EBattleGlyph MG[]={EBattleGlyph::Locate,EBattleGlyph::Layers,EBattleGlyph::Plus,EBattleGlyph::Minus,EBattleGlyph::Chevron};
    for(int I=0;I<5;++I)D.Button(Map,ButtonClass,*FString::Printf(TEXT("Map_%d"),I),MC[I],MG[I],MT[I],FMargin(8+I*49,-31,44,25),FAnchors(0,1));
    auto* Modal=D.Panel(R,TEXT("ModalPanel"),FMargin(-230,-140,460,280),FAnchors(.5,.5));D.Frame(Modal,TEXT("Modal"),true);Modal->SetVisibility(ESlateVisibility::Collapsed);CastChecked<UCanvasPanelSlot>(Modal->Slot)->SetZOrder(30);
    D.Text(Modal,TEXT("ModalTitle"),TEXT("测试预览"),19,FMargin(20,17,380,30));D.Text(Modal,TEXT("ModalBody"),TEXT(""),14,FMargin(20,70,420,176));auto* Close=D.Button(Modal,ButtonClass,TEXT("CloseModalButton"),TEXT("CloseModal"),EBattleGlyph::Exit,TEXT("关闭"),FMargin(412,13,32,32));Close->ButtonText=FText::GetEmpty();
}

UK2Node_Event* Event(FGraph& G,UClass* Owner,FName Name)
{return G.Node<UK2Node_Event>([&](auto* E){E->EventReference.SetExternalMember(Name,Owner);E->bOverrideFunction=true;});}
void BusinessGraphs(UWidgetBlueprint* BP,UBattleHUDTestData* Data)
{
    Var(BP,TEXT("UseTestUnits"),Bool(),TEXT("true"),TEXT("使用测试单位"),true,TEXT("战斗UI|数据来源"));
    Var(BP,TEXT("TestUnitData"),Obj(UBattleHUDTestData::StaticClass()),Data->GetPathName(),TEXT("测试小队数据"),true,TEXT("战斗UI|数据来源"));
    check(Compile(BP));
    auto* Graph=FBlueprintEditorUtils::CreateNewGraph(BP,TEXT("BattleUIBusiness"),UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());FBlueprintEditorUtils::AddUbergraphPage(BP,Graph);FGraph G{BP,Graph};
    G.Comment(TEXT("战斗UI业务接线：C++负责布局、状态和绘制。测试数据不写入正式存档。替换各操作分支可接入真实单位、背包、场景交互和车辆。小队控制模式后续实现。"));
    G.X=0;auto* Init=Event(G,UMapWidgetBase::StaticClass(),TEXT("InitializeMapUI"));auto* Branch=G.Branch(P(Init,TEXT("then")),G.Get(TEXT("UseTestUnits")));
    auto* Initialize=G.Call(UBattleMapWidget::StaticClass(),TEXT("InitializeTestHUD"));Link(G.Get(TEXT("TestUnitData")),P(Initialize,TEXT("Data")));G.Exec(Branch->GetThenPin(),Initialize);
    G.Row(400);auto* Missing=G.Call(UBattleMapWidget::StaticClass(),TEXT("ShowFeedback"));D(Missing,TEXT("Message"),TEXT("请在蓝图接入战斗小队数据"));G.Exec(Branch->GetElsePin(),Missing);
    G.Row(800);G.X=0;auto* Request=Event(G,UBattleMapWidget::StaticClass(),TEXT("OnBattleCommandRequested"));auto* Switch=G.Node<UK2Node_SwitchEnum>([](auto* N){N->SetEnum(StaticEnum<EBattleCommand>());});Link(P(Request,TEXT("then")),Switch->GetExecPin());Link(P(Request,TEXT("Command")),Switch->GetSelectionPin());
    for(int I=0;I<=int(EBattleCommand::MapCollapse);++I)
    {
        const auto Cmd=EBattleCommand(I);G.X=900;G.Y=800+I*180;
        UEdGraphPin* From=nullptr;const FString EnumName=StaticEnum<EBattleCommand>()->GetNameStringByValue(I);
        for(auto* Pin:Switch->Pins)if(Pin->Direction==EGPD_Output&&(Pin->PinName.ToString()==EnumName||Pin->PinName.ToString().EndsWith(TEXT("::")+EnumName))){From=Pin;break;}
        check(From);
        // Keep business policy editable here. Test mutation helpers affect UI snapshots only.
        UK2Node_CallFunction* Call=nullptr;
        if(I<=int(EBattleCommand::Prone))
        {Call=G.Call(UBattleMapWidget::StaticClass(),TEXT("SetDisplayedPosture"));Link(P(Request,TEXT("UnitId")),P(Call,TEXT("UnitId")));D(Call,TEXT("Posture"),I==0?TEXT("Standing"):I==1?TEXT("Crouched"):TEXT("Prone"));}
        else if(Cmd==EBattleCommand::Stealth){Call=G.Call(UBattleMapWidget::StaticClass(),TEXT("ToggleDisplayedStealth"));Link(P(Request,TEXT("UnitId")),P(Call,TEXT("UnitId")));}
        else if(Cmd==EBattleCommand::Backpack){Call=G.Call(UBattleMapWidget::StaticClass(),TEXT("ShowTestBackpack"));Link(P(Request,TEXT("UnitId")),P(Call,TEXT("UnitId")));}
        else if(Cmd==EBattleCommand::Interact||Cmd==EBattleCommand::Pickup){Call=G.Call(UBattleMapWidget::StaticClass(),TEXT("BeginTargeting"));D(Call,TEXT("Command"),EnumName);}
        else if(I>=int(EBattleCommand::Quick1)&&I<=int(EBattleCommand::Quick4)){Call=G.Call(UBattleMapWidget::StaticClass(),TEXT("UseTestQuickItem"));Link(P(Request,TEXT("UnitId")),P(Call,TEXT("UnitId")));D(Call,TEXT("SlotIndex"),FString::FromInt(I-int(EBattleCommand::Quick1)));}
        else if(I>=int(EBattleCommand::BoardVehicle)&&I<=int(EBattleCommand::LocateVehicle)){Call=G.Call(UBattleMapWidget::StaticClass(),TEXT("ApplyTestVehicleCommand"));D(Call,TEXT("Command"),EnumName);}
        else if(I>=int(EBattleCommand::MapFollow)){Call=G.Call(UBattleMapWidget::StaticClass(),TEXT("ApplyMinimapCommand"));D(Call,TEXT("Command"),EnumName);}
        else{Call=G.Call(UBattleMapWidget::StaticClass(),TEXT("ShowFeedback"));D(Call,TEXT("Message"),Cmd==EBattleCommand::MemberMode?TEXT("当前为成员控制模式"):TEXT("小队控制模式预留：在蓝图接入整队指令"));}
        G.Exec(From,Call);Call->NodeComment=TEXT("业务入口：可替换为真实单位/插件调用；当前仅演示UI状态");Call->bCommentBubbleVisible=true;
    }
    G.Row(5400);G.X=0;auto* Target=Event(G,UBattleMapWidget::StaticClass(),TEXT("OnBattleTargetConfirmed"));auto* Feedback=G.Call(UBattleMapWidget::StaticClass(),TEXT("ShowFeedback"));D(Feedback,TEXT("Message"),TEXT("测试：已选中目标。真实交互 / 拾取 / 拖拽在此蓝图事件中接入。"));G.Exec(P(Target,TEXT("then")),Feedback);
}
}
UBattleHUDAssetsCommandlet::UBattleHUDAssetsCommandlet(){IsClient=false;IsEditor=true;LogToConsole=true;}
int32 UBattleHUDAssetsCommandlet::Main(const FString& Params)
{
    using namespace BattleAssets;
    if(FParse::Param(*Params,TEXT("PanelLayoutRepair")))return RunBattlePanelLayoutRepair(Params);
    if(FParse::Param(*Params,TEXT("VehiclePresentationUpgrade")))return RunBattleVehiclePresentationUpgrade(Params);
    if(FParse::Param(*Params,TEXT("PanelInteractionUpgrade")))return UpgradeBattlePanelAssets()?0:1;
    if(FParse::Param(*Params,TEXT("ModeIconsUpgrade")))return UpgradeBattleModeIcons()?0:1;
    if(FParse::Param(*Params,TEXT("SquadEntryUpgrade")))return UpgradeBattleSquadEntryAssets()?0:1;
    if(Params.Contains(TEXT("ModeUpgrade")))return UpgradeBattleModeAssets()?0:1;
    if(Params.Contains(TEXT("ModeInspect")))
    {
        FString Dump;
        auto Properties=[&](UObject* Object)
        {
            if(!Object)return;
            for(TFieldIterator<FProperty> It(Object->GetClass());It;++It)
            {
                auto* Property=*It;if(!Property->HasAnyPropertyFlags(CPF_Edit|CPF_BlueprintVisible))continue;
                FString Value;Property->ExportText_InContainer(0,Value,Object,nullptr,Object,PPF_None);
                Dump+=TEXT("  ")+Property->GetName()+TEXT("=")+Value+TEXT("\n");
            }
        };
        for(const TCHAR* Path:{TEXT("/Game/System/Map/BattleMap/BP_BattleMapWidget"),TEXT("/Game/System/Map/BattleMap/UI/WBP_BattleHUD"),
            TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_BattleModeButton"),
            TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_小队项"),
            TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_成员模式"),TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_小队模式"),
            TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController")})
        {
            auto* BP=LoadObject<UBlueprint>(nullptr,Path);if(!BP)continue;
            Dump+=FString::Printf(TEXT("\nASSET %s Parent=%s\n"),Path,*GetPathNameSafe(BP->ParentClass));
            Properties(BP->GeneratedClass->GetDefaultObject());
            if(auto* WidgetBP=Cast<UWidgetBlueprint>(BP))WidgetBP->WidgetTree->ForEachWidget([&](UWidget* W)
            {
                Dump+=FString::Printf(TEXT("\nWIDGET %s Class=%s Parent=%s\n"),*W->GetName(),*W->GetClass()->GetPathName(),*GetNameSafe(W->GetParent()));
                Properties(W);if(W->Slot){Dump+=TEXT(" SLOT\n");Properties(W->Slot);}
            });
            TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
            for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)
            {
                Dump+=FString::Printf(TEXT("NODE %s.%s %s\n"),*Graph->GetName(),*Node->GetName(),*Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                for(auto* Pin:Node->Pins)
                {
                    FString Links;for(auto* Link:Pin->LinkedTo)Links+=Link->GetOwningNode()->GetName()+TEXT(".")+Link->PinName.ToString()+TEXT(" ");
                    Dump+=FString::Printf(TEXT(" PIN %s default=%s object=%s links=%s\n"),*Pin->PinName.ToString(),*Pin->DefaultValue,*GetPathNameSafe(Pin->DefaultObject),*Links);
                }
            }
        }
        const FString Out=FPaths::ProjectSavedDir()/TEXT("BattleModes/Inspect.txt");IFileManager::Get().MakeDirectory(*FPaths::GetPath(Out),true);FFileHelper::SaveStringToFile(Dump,*Out);
        UE_LOG(LogTemp,Display,TEXT("BATTLE_MODES_INSPECT_OK %s"),*Out);return 0;
    }
    if(FParse::Param(*Params,TEXT("CardBindingsRepair")))return RepairBattlePersonnelCardBindings(true)?0:1;
    if(FParse::Param(*Params,TEXT("CardBindingsInspect")))return RepairBattlePersonnelCardBindings(false)?0:1;
    if(Params.Contains(TEXT("CardUpgrade")))return UpgradeBattlePersonnelCard()?0:1;
    if(Params.Contains(TEXT("CardInspect")))
    {
        const TCHAR* CardPath=TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_人员卡片");
        auto* BP=LoadObject<UWidgetBlueprint>(nullptr,CardPath);if(!BP)return 1;
        FString Dump=FString::Printf(TEXT("Asset=%s Parent=%s\n"),CardPath,*GetPathNameSafe(BP->ParentClass));
        auto Properties=[&](UObject* Object)
        {
            if(!Object)return;
            for(TFieldIterator<FProperty> It(Object->GetClass());It;++It)
            {
                auto* P=*It;if(!P->HasAnyPropertyFlags(CPF_Edit|CPF_BlueprintVisible))continue;
                FString V;P->ExportText_InContainer(0,V,Object,nullptr,Object,PPF_None);
                Dump+=TEXT("  ")+P->GetName()+TEXT("=")+V+TEXT("\n");
            }
        };
        Properties(BP->GeneratedClass->GetDefaultObject());
        BP->WidgetTree->ForEachWidget([&](UWidget* W)
        {
            Dump+=FString::Printf(TEXT("\nWIDGET %s Class=%s Parent=%s\n"),*W->GetName(),*W->GetClass()->GetPathName(),*GetNameSafe(W->GetParent()));
            Properties(W);if(W->Slot){Dump+=TEXT(" SLOT\n");Properties(W->Slot);}
        });
        TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
        for(auto* G:Graphs){Dump+=TEXT("\nGRAPH ")+G->GetName()+TEXT("\n");for(UEdGraphNode* N:G->Nodes){Dump+=N->GetNodeTitle(ENodeTitleType::FullTitle).ToString()+TEXT("\n");for(auto* Pin:N->Pins)if(!Pin->LinkedTo.IsEmpty())Dump+=TEXT("  linked ")+Pin->PinName.ToString()+TEXT("\n");}}
        const FString Out=FPaths::ProjectSavedDir()/TEXT("PersonnelCardUpgrade/Inspect.txt");IFileManager::Get().MakeDirectory(*FPaths::GetPath(Out),true);FFileHelper::SaveStringToFile(Dump,*Out);
        UE_LOG(LogTemp,Display,TEXT("PERSONNEL_CARD_INSPECT %s"),*Out);return 0;
    }
    if(Params.Contains(TEXT("Polish")))
    {
        for(const TCHAR* Name:{TEXT("Components/WBP_BattleIconButton"),TEXT("Components/WBP_BattleMemberCard")})
        {
            auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+Name));if(!BP)return 1;
            if(auto* Label=BP->WidgetTree->FindWidget(TEXT("Label")))
                CastChecked<UCanvasPanelSlot>(Label->Slot)->SetOffsets(FMargin(2,-17,2,16));
            CastChecked<USelectionButtonWidget>(BP->GeneratedClass->GetDefaultObject())->bUseTabStyle=false;
            if(!Finish(BP))return 1;
        }
        auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+TEXT("WBP_BattleHUD")));if(!HUD)return 1;
        HUD->ForEachSourceWidget([](UWidget* W){if(auto* B=Cast<UBattleHUDButton>(W))B->bUseTabStyle=false;});
        auto* Vehicle=CastChecked<UBattleHUDButton>(HUD->WidgetTree->FindWidget(TEXT("VehicleButton")));
        Vehicle->ButtonText=FText::FromString(TEXT("车\n辆"));Vehicle->bVerticalLabel=true;
        for(int32 I=0;I<4;++I)
        {
            auto* Quick=CastChecked<UBattleHUDButton>(HUD->WidgetTree->FindWidget(*FString::Printf(TEXT("Command_Quick%d"),I+1)));
            Quick->ButtonText=FText::FromString(FString::Printf(TEXT("%d · 0"),I+1));
            auto* Action=HUD->WidgetTree->FindWidget(*FString::Printf(TEXT("Vehicle_%d"),I));
            CastChecked<UCanvasPanelSlot>(Action->Slot)->SetOffsets(FMargin(8+I*69,102,64,40));
        }
        if(!Finish(HUD))return 1;
        UE_LOG(LogTemp,Display,TEXT("BATTLE_UI_POLISH_OK frames, compact buttons and vertical vehicle label; business graph preserved"));return 0;
    }
    if(Params.Contains(TEXT("Inspect")))
    {
        for(const TCHAR* Path:{TEXT("/Game/System/Map/BattleMap/BP_BattleMapWidget"),TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController")})
        {auto* BP=LoadObject<UBlueprint>(nullptr,Path);if(!BP)continue;UE_LOG(LogTemp,Display,TEXT("BATTLE_UI_INSPECT %s parent=%s"),Path,*GetNameSafe(BP->ParentClass));if(auto* W=Cast<UWidgetBlueprint>(BP))W->ForEachSourceWidget([](UWidget* Widget){UE_LOG(LogTemp,Display,TEXT("BATTLE_WIDGET %s"),*Widget->GetName());});}
        return 0;
    }
    auto* Button=MakeButton();auto* Member=MakeMember();auto* Data=MakeData();auto* HUD=NewBP(TEXT("WBP_BattleHUD"),UBattleMapWidget::StaticClass());
    if(!HUD->WidgetTree->RootWidget){LayoutHUD(HUD,Button->GeneratedClass);BusinessGraphs(HUD,Data);if(!Finish(HUD))return 1;}
    auto* Defaults=CastChecked<UBattleMapWidget>(HUD->GeneratedClass->GetDefaultObject());Defaults->MemberCardClass=Member->GeneratedClass;Defaults->ButtonClass=Button->GeneratedClass;
    if(auto* P=FindFProperty<FBoolProperty>(HUD->GeneratedClass,TEXT("UseTestUnits")))P->SetPropertyValue_InContainer(Defaults,true);
    if(auto* P=FindFProperty<FObjectProperty>(HUD->GeneratedClass,TEXT("TestUnitData")))P->SetObjectPropertyValue_InContainer(Defaults,Data);
    if(!Save(HUD))return 1;
    auto* PC=LoadObject<UBlueprint>(nullptr,TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController"));if(!PC||!Compile(PC))return 1;
    CastChecked<AGameMainMapPlayerController>(PC->GeneratedClass->GetDefaultObject())->BattleWidgetClass=HUD->GeneratedClass;if(!Save(PC))return 1;
    if(!Finish(Button)||!Finish(Member)||!Finish(HUD))return 1;
    UE_LOG(LogTemp,Display,TEXT("BATTLE_UI_ASSETS_OK editable HUD + card + button, test squads, Blueprint business graph, controller assignment"));return 0;
}
