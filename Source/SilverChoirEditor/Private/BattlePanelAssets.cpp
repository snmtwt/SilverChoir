#include "BattlePanelAssets.h"
#include "MainMapBlueprintBuilder.h"
#include "Map/BattleMap/BattleModePanels.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/BattleMap/BattleVehiclePanelWidget.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Animation/WidgetAnimation.h"
#include "Channels/MovieSceneChannelEditorData.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "K2Node_CallParentFunction.h"

namespace BattleModeIcons
{
bool HasSymbolReferences(const UWidgetBlueprint* BP);
UTexture2D* Import(const FString& SourceFile, const FString& PackagePath);
bool SaveTexture(UTexture2D* Texture);
void SetIcon(UBattleHUDButton* Button, UTexture2D* Texture);
}

namespace BattleSelectionAssets { int32 RetirePersonnelRouting(const FString& Params); }
namespace BattleModeAssets
{
bool Backup(const FString& PackagePath,const FString& BackupFolder);
bool Save(UWidgetBlueprint* BP);
bool Compile(UWidgetBlueprint* BP);
void CopyButtonConfiguration(const UBattleHUDButton* From,UBattleHUDButton* To);
void StyleButton(UBattleHUDButton* Button);
}

namespace BattlePanelAssets
{
const FString Folder=TEXT("/Game/System/Map/BattleMap/UI/Components/");

UWidgetBlueprint* MakeBlueprint(const FString& Path,UClass* Parent)
{
    if(auto* Existing=LoadObject<UWidgetBlueprint>(nullptr,*Path))
        return Existing->ParentClass==Parent?Existing:nullptr;
    auto* Factory=NewObject<UWidgetBlueprintFactory>();Factory->ParentClass=Parent;
    auto* BP=Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(),CreatePackage(*Path),
        *FPackageName::GetShortName(Path),RF_Public|RF_Standalone,nullptr,GWarn));
    if(BP)FAssetRegistryModule::AssetCreated(BP);
    return BP;
}

void AddHooks(UWidgetBlueprint* BP,UClass* Owner,FName Open,FName Close,const TCHAR* Comment)
{
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    TSet<FName> Existing;
    for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)
        if(auto* Event=Cast<UK2Node_Event>(Node))Existing.Add(Event->EventReference.GetMemberName());
    if(Existing.Contains(Open)&&Existing.Contains(Close))return;
    auto* Graph=MainMapBP::Find(BP,TEXT("ControlPanelBusiness"));
    if(!Graph)
    {
        Graph=FBlueprintEditorUtils::CreateNewGraph(BP,TEXT("ControlPanelBusiness"),UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(BP,Graph);
        MainMapBP::FGraph Builder{BP,Graph};Builder.Comment(Comment);
    }
    MainMapBP::FGraph Builder{BP,Graph};Builder.X=0;Builder.Y=200;
    for(FName Name:{Open,Close})
    {
        if(!Existing.Contains(Name))
        {
            auto* Event=Builder.Node<UK2Node_Event>([&](auto* N)
            {
                N->EventReference.SetExternalMember(Name,Owner);N->bOverrideFunction=true;
            });
            if(Owner==UBattleVehiclePanelWidget::StaticClass()&&Name==Close)
            {
                Builder.X=350;
                auto* Parent=Builder.Node<UK2Node_CallParentFunction>([&](auto* N){N->SetFromFunction(Owner->FindFunctionByName(Name));});
                MainMapBP::Link(Event->FindPin(UEdGraphSchema_K2::PN_Then),Parent->GetExecPin());
                for(FName PinName:{FName("InSquadId"),FName("VehicleId")})
                    MainMapBP::Link(Event->FindPin(PinName),Parent->FindPin(PinName));
            }
        }
        Builder.X=0;Builder.Y+=300;
    }
}

UWidgetBlueprint* MakeVehicleButton()
{
    auto* BP=MakeBlueprint(Folder+TEXT("WBP_BattleVehicleButton"),UBattleHUDButton::StaticClass());
    if(!BP)return nullptr;
    if(!BP->WidgetTree->RootWidget)
    {
        UWidgetTree* Tree=BP->WidgetTree;
        auto* Root=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("VehicleButtonRoot"));
        Tree->RootWidget=Root;Root->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Content=Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("VehicleButtonContent"));
        auto* Slot=Root->AddChildToCanvas(Content);Slot->SetAnchors(FAnchors(.5f));Slot->SetAlignment(FVector2D(.5f));Slot->SetAutoSize(true);Slot->SetOffsets(FMargin(0));
        auto* Size=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("VehicleGlyphSize"));
        Size->SetWidthOverride(18.f);Size->SetHeightOverride(20.f);
        auto* SizeSlot=Content->AddChildToVerticalBox(Size);SizeSlot->SetHorizontalAlignment(HAlign_Center);SizeSlot->SetPadding(FMargin(0,0,0,7));
        auto* Symbol=Tree->ConstructWidget<UBattleHUDVisual>(UBattleHUDVisual::StaticClass(),TEXT("Symbol"));
        Symbol->Glyph=EBattleGlyph::Vehicle;Symbol->SetVisibility(ESlateVisibility::HitTestInvisible);Size->AddChild(Symbol);
        auto* Label=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("Label"));
        Label->SetText(FText::FromString(TEXT("车\n辆")));Label->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")),9));
        Label->SetJustification(ETextJustify::Center);Label->SetVisibility(ESlateVisibility::HitTestInvisible);
        Content->AddChildToVerticalBox(Label)->SetHorizontalAlignment(HAlign_Center);
    }
    if(!BattleModeAssets::Compile(BP))return nullptr;
    auto* Defaults=CastChecked<UBattleHUDButton>(BP->GeneratedClass->GetDefaultObject());
    BattleModeAssets::StyleButton(Defaults);Defaults->Glyph=EBattleGlyph::Vehicle;Defaults->bVerticalLabel=true;
    Defaults->ButtonText=FText::FromString(TEXT("车\n辆"));Defaults->Font.Size=9;
    Defaults->DesignSizeMode=EDesignPreviewSizeMode::Custom;Defaults->DesignTimeSize=FVector2D(26,156);
    return BP;
}
}

bool UpgradeBattlePanelAssets()
{
    using namespace BattlePanelAssets;
    const FString CardPath=Folder+TEXT("WBP_人员卡片"), MemberPath=Folder+TEXT("WBP_成员模式");
    const FString VehiclePath=Folder+TEXT("WBP_车辆面板"), ButtonPath=Folder+TEXT("WBP_BattleVehicleButton");
    const FString HUDPath=TEXT("/Game/System/Map/BattleMap/UI/WBP_BattleHUD");
    const FString BackupFolder=FPaths::ProjectSavedDir()/TEXT("BattlePanelInteraction/Backup")/
        (FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits));
    for(const FString& Path:{CardPath,MemberPath,VehiclePath,ButtonPath,HUDPath})
        if(!BattleModeAssets::Backup(Path,BackupFolder))return false;
    if(BattleSelectionAssets::RetirePersonnelRouting(TEXT("-Apply"))!=0)return false;
    auto* Card=LoadObject<UWidgetBlueprint>(nullptr,*CardPath);
    if(!Card||Card->ParentClass!=UBattlePersonnelCardWidget::StaticClass())return false;
    AddHooks(Card,UBattlePersonnelCardWidget::StaticClass(),TEXT("OnControlPanelOpened"),TEXT("OnControlPanelClosed"),
        TEXT("C++ 负责选择人物、每次点击通知成员模式、互斥关闭再打开。这里实现人物控制面板的布局/展开/收起动画；UnitId 与 SquadId 为本次面板上下文。"));
    if(!BattleModeAssets::Compile(Card))return false;
    auto* Vehicle=MakeBlueprint(VehiclePath,UBattleVehiclePanelWidget::StaticClass());
    if(!Vehicle)return false;
    if(!Vehicle->WidgetTree->RootWidget)
    {
        auto* Root=Vehicle->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("VehiclePanelRoot"));
        Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Vehicle->WidgetTree->RootWidget=Root;
    }
    // The authored close hook calls its native parent; the user can move that call after an animation.
    AddHooks(Vehicle,UBattleVehiclePanelWidget::StaticClass(),TEXT("OnPanelOpened"),TEXT("OnPanelClosed"),
        TEXT("车辆内容由你在 Designer 中制作。打开时读取 Vehicle / SquadId；关闭事件可播放收起动画，并在动画结束后将自身 Visibility 设为 Collapsed。当前空面板无需占用宽度。"));
    if(!BattleModeAssets::Compile(Vehicle))return false;
    auto* ButtonBP=MakeVehicleButton();if(!ButtonBP)return false;
    auto* Member=LoadObject<UWidgetBlueprint>(nullptr,*MemberPath);
    if(!Member||Member->ParentClass!=UBattleMemberModeWidget::StaticClass())return false;
    auto* Root=Cast<UHorizontalBox>(Member->WidgetTree->RootWidget);
    auto* Button=Cast<UBattleHUDButton>(Member->WidgetTree->FindWidget(TEXT("VehicleButton")));
    if(!Root||!Button||!Button->GetParent())return false;
    // The inspected member Blueprint has no vehicle business binding. Refuse to remove user-authored routes.
    TArray<UEdGraph*> Graphs;Member->GetAllGraphs(Graphs);
    for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)
        if(auto* Call=Cast<UK2Node_CallFunction>(Node);Call&&Call->FunctionReference.GetMemberName()==TEXT("ToggleVehicleDrawer"))
        {UE_LOG(LogTemp,Error,TEXT("BATTLE_PANEL_UNEXPECTED_LEGACY_VEHICLE_ROUTE"));return false;}
    if(Button->GetClass()!=ButtonBP->GeneratedClass)
    {
        auto* Old=Button;auto* Parent=Old->GetParent();
        if(!Old->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional))return false;
        Button=Member->WidgetTree->ConstructWidget<UBattleHUDButton>(ButtonBP->GeneratedClass.Get(),TEXT("VehicleButton"));
        BattleModeAssets::CopyButtonConfiguration(Old,Button);
        if(!Parent->ReplaceChild(Old,Button))return false;Old->Slot=nullptr;
    }
    BattleModeAssets::StyleButton(Button);Button->bIsVariable=true;Button->Glyph=EBattleGlyph::Vehicle;
    Button->ChoiceID=TEXT("Vehicle");Button->ButtonText=FText::FromString(TEXT("车\n辆"));Button->bVerticalLabel=true;Button->Font.Size=9;
    Button->SetToolTipText(FText::FromString(TEXT("打开小队车辆面板")));
    if(!Member->WidgetTree->FindWidget(TEXT("VehiclePanelContainer")))
    {
        auto* Host=Member->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("VehiclePanelContainer"));
        Host->bIsVariable=true;Host->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        auto* Slot=Cast<UHorizontalBoxSlot>(Root->InsertChildAt(Root->GetChildrenCount()-1,Host));
        if(!Slot)return false;Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));Slot->SetVerticalAlignment(VAlign_Fill);
    }
    if(!BattleModeAssets::Compile(Member))return false;
    CastChecked<UBattleMemberModeWidget>(Member->GeneratedClass->GetDefaultObject())->VehiclePanelClass=Vehicle->GeneratedClass;
    auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,*HUDPath);if(!HUD)return false;
    if(auto* Instance=Cast<UBattleMemberModeWidget>(HUD->WidgetTree->FindWidget(TEXT("MemberModePanel"))))
        Instance->VehiclePanelClass=Vehicle->GeneratedClass;
    if(!BattleModeAssets::Compile(HUD))return false;
    for(auto* BP:{Card,Vehicle,ButtonBP,Member,HUD})if(!BattleModeAssets::Save(BP))return false;
    UE_LOG(LogTemp,Display,TEXT("BATTLE_PANEL_UPGRADE_OK nativeClickRouting=1 vehicleClass=%s emptyHost=1 blueprintHooks=1 backup=%s"),*VehiclePath,*BackupFolder);
    return true;
}

namespace BattleVehiclePresentation
{
const FString MemberPath = BattlePanelAssets::Folder + TEXT("WBP_成员模式");
const FString VehiclePath = BattlePanelAssets::Folder + TEXT("WBP_车辆面板");
const FString PersonnelPath = BattlePanelAssets::Folder + TEXT("WBP_人员卡片");
const FString ButtonPath = BattlePanelAssets::Folder + TEXT("WBP_BattleVehicleButton");
const FString HUDPath = TEXT("/Game/System/Map/BattleMap/UI/WBP_BattleHUD");
const FString TexturePath = TEXT("/Game/System/Map/BattleMap/UI/Textures/T_VehicleFront");

struct FUpgrade
{
    UWidgetBlueprint* Member = nullptr;
    UWidgetBlueprint* Vehicle = nullptr;
    UWidgetBlueprint* Button = nullptr;
    TArray<UWidgetBlueprint*> Dependents;
    TArray<UBlueprint*> Referencers;
    FString Report;
    FString BackupFolder;
    bool bOK = true;

    bool Check(bool bValue, const FString& Message)
    {
        if (!bValue)
        {
            bOK = false;
            Report += TEXT("ERROR ") + Message + TEXT("\n");
            UE_LOG(LogTemp, Error, TEXT("VEHICLE_PRESENTATION %s"), *Message);
        }
        return bValue;
    }
    bool Write(const TCHAR* Name)
    {
        const FString File = FPaths::ProjectSavedDir() / TEXT("BattleVehiclePresentation") / Name;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        return FFileHelper::SaveStringToFile(Report, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
};

FString Properties(UObject* Object, const TSet<FName>& Excluded = {})
{
    if (!Object) return TEXT("None");
    FString Result;
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        if (!It->HasAnyPropertyFlags(CPF_Edit) || It->HasAnyPropertyFlags(CPF_Transient)
            || It->HasMetaData(TEXT("BindWidget")) || It->HasMetaData(TEXT("BindWidgetOptional")) || Excluded.Contains(It->GetFName())) continue;
        FString Value;
        It->ExportText_InContainer(0, Value, Object, nullptr, Object, PPF_None);
        Result += It->GetName() + TEXT("=") + Value + TEXT("\n");
    }
    return Result;
}

FString BusinessGraphs(UBlueprint* BP)
{
    FString Result;
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        Result += TEXT("GRAPH ") + Graph->GetName() + TEXT("\n");
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            Result += FString::Printf(TEXT(" NODE %s %s %s %d,%d %s\n"), *Node->NodeGuid.ToString(), *Node->GetName(),
                *Node->GetClass()->GetPathName(), Node->NodePosX, Node->NodePosY, *Node->NodeComment);
            if (const auto* Call = Cast<UK2Node_CallFunction>(Node)) Result += TEXT(" CALL ") + Call->FunctionReference.GetMemberName().ToString() + TEXT("\n");
            if (const auto* Variable = Cast<UK2Node_Variable>(Node)) Result += TEXT(" VARIABLE ") + Variable->VariableReference.GetMemberName().ToString() + TEXT("\n");
            if (const auto* Event = Cast<UK2Node_Event>(Node)) Result += TEXT(" EVENT ") + Event->EventReference.GetMemberName().ToString() + TEXT("\n");
            for (UEdGraphPin* Pin : Node->Pins)
            {
                Result += FString::Printf(TEXT(" PIN %s %d %s %s %s\n"), *Pin->PinName.ToString(), int32(Pin->Direction),
                    *Pin->DefaultValue, *Pin->DefaultTextValue.ToString(), *GetPathNameSafe(Pin->DefaultObject));
                for (UEdGraphPin* Link : Pin->LinkedTo)
                    Result += TEXT("  LINK ") + Link->GetOwningNode()->NodeGuid.ToString() + TEXT(".") + Link->PinName.ToString() + TEXT("\n");
            }
        }
    }
    if (const auto* Widget = Cast<UWidgetBlueprint>(BP))
    {
        for (const auto& Binding : Widget->Bindings)
        {
            FString Text;
            FDelegateEditorBinding::StaticStruct()->ExportText(Text, &Binding, nullptr, const_cast<UWidgetBlueprint*>(Widget), PPF_None, nullptr);
            Result += TEXT("BINDING ") + Text + TEXT("\n");
        }
        for (const UWidgetAnimation* Animation : Widget->Animations)
        {
            Result += TEXT("ANIMATION ") + GetNameSafe(Animation) + TEXT("\n");
            if (Animation) for (const auto& Binding : Animation->AnimationBindings)
            {
                FString Text;
                FWidgetAnimationBinding::StaticStruct()->ExportText(Text, &Binding, nullptr, const_cast<UWidgetBlueprint*>(Widget), PPF_None, nullptr);
                Result += Text + TEXT("\n");
            }
        }
    }
    return Result;
}

bool IsVehicleButton(const FUpgrade& U, const UWidget* Widget)
{
    return Widget && U.Button->GeneratedClass && Widget->IsA(U.Button->GeneratedClass);
}

void DiscoverDependents(FUpgrade& U)
{
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    U.Referencers = {U.Member, U.Button};
    // Direct button referencers are read only, to reject removal of an externally
    // referenced Symbol. The only dependent asset this narrow entry saves is the HUD.
    TArray<FName> Refs;
    Registry.GetReferencers(FName(*ButtonPath), Refs);
    for (FName Package : Refs)
    {
        if (!Package.ToString().StartsWith(TEXT("/Game/"))) continue;
        U.Report += FString::Printf(TEXT("READ_ONLY_REFERENCER %s <- %s\n"), *ButtonPath, *Package.ToString());
        TArray<FAssetData> Assets;
        Registry.GetAssetsByPackageName(Package, Assets);
        for (const FAssetData& Asset : Assets)
        {
            if (!Asset.AssetClassPath.GetAssetName().ToString().EndsWith(TEXT("Blueprint"))) continue;
            UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
            if (!U.Check(BP != nullptr, TEXT("Cannot load referencing Blueprint: ") + Asset.GetObjectPathString())) continue;
            U.Referencers.AddUnique(BP);
        }
    }
    auto* HUD = LoadObject<UWidgetBlueprint>(nullptr, *HUDPath);
    if (U.Check(HUD && HUD->WidgetTree, TEXT("Cannot load the existing battle HUD")))
    {
        U.Dependents.AddUnique(HUD);
        U.Referencers.AddUnique(HUD);
    }
}

bool CheckStructure(FUpgrade& U, bool bRequireUpgraded)
{
    U.Check(IsVehicleButton(U, U.Member->WidgetTree->FindWidget(TEXT("VehicleButton"))),
        TEXT("Member VehicleButton does not use the existing dedicated vehicle-button Blueprint"));
    UWidget* Named = U.Member->WidgetTree->FindWidget(TEXT("VehiclePanel"));
    int32 VehicleCount = 0;
    bool bOwnedVehicleReached = false;
    // This visits the member's authored panel hierarchy and named-slot contents,
    // without entering the private WidgetTrees of nested user widgets. Placement
    // in a scroll box or another same-tree panel is valid at any depth.
    U.Member->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (Widget->GetTypedOuter<UWidgetTree>() != U.Member->WidgetTree) return;
        if (Widget->IsA<UBattleVehiclePanelWidget>()) ++VehicleCount;
        bOwnedVehicleReached |= Widget == Named;
    });
    if (Named)
        U.Check(Named->GetClass() == U.Vehicle->GeneratedClass && bOwnedVehicleReached && VehicleCount == 1,
            TEXT("VehiclePanel name/class/owning-tree conflict or more than one preplaced vehicle; no widgets will be replaced"));
    else U.Check(VehicleCount == 0, TEXT("An existing vehicle has a different name; refusing to add a second vehicle"));
    U.Check(!U.Member->NewVariables.ContainsByPredicate([](const FBPVariableDescription& Var) { return Var.VarName == TEXT("VehiclePanel"); }),
        TEXT("User-authored VehiclePanel Blueprint variable conflicts with the native widget binding"));
    if (bRequireUpgraded)
        U.Check(Named && Named->GetVisibility() == ESlateVisibility::Collapsed && Named->bIsVariable,
            TEXT("Preplaced VehiclePanel is missing or is not a collapsed variable widget"));
    FString Hierarchy;
    TSet<const UWidget*> Ancestors;
    for (const UWidget* Ancestor = Named; Ancestor && !Ancestors.Contains(Ancestor); Ancestor = Ancestor->GetParent())
    {
        Ancestors.Add(Ancestor);
        Hierarchy = Ancestor->GetName() + (Hierarchy.IsEmpty() ? FString() : TEXT(" / ") + Hierarchy);
    }
    U.Report += FString::Printf(TEXT("PREPLACED count=%d widget=%s class=%s parent=%s visibility=%d ownedAndReachable=%d\n"), VehicleCount,
        *GetPathNameSafe(Named), Named ? *Named->GetClass()->GetPathName() : TEXT("None"), *GetNameSafe(Named ? Named->GetParent() : nullptr),
        Named ? int32(Named->GetVisibility()) : -1, bOwnedVehicleReached);
    U.Report += FString::Printf(TEXT("PLACEMENT hierarchy=%s owningTree=%s slotClass=%s\n%s"), *Hierarchy,
        *GetPathNameSafe(Named ? Named->GetTypedOuter<UWidgetTree>() : nullptr), Named && Named->Slot ? *Named->Slot->GetClass()->GetPathName() : TEXT("None"),
        *Properties(Named ? Named->Slot.Get() : nullptr));

    UWidget* RawSize = U.Button->WidgetTree->FindWidget(TEXT("IconSizeBox"));
    if (!RawSize) RawSize = U.Button->WidgetTree->FindWidget(TEXT("VehicleGlyphSize"));
    auto* Size = Cast<USizeBox>(RawSize);
    UWidget* RawSymbol = U.Button->WidgetTree->FindWidget(TEXT("Symbol"));
    auto* Symbol = Cast<UBattleHUDVisual>(RawSymbol);
    UWidget* RawImage = U.Button->WidgetTree->FindWidget(TEXT("IconImage"));
    auto* Image = Cast<UImage>(RawImage);
    U.Check(Size && Size->GetChildrenCount() == 1 && ((Symbol && !RawImage && Symbol->GetParent() == Size)
        || (Image && !RawSymbol && Image->GetParent() == Size)), TEXT("Unexpected vehicle icon structure; expected one existing Symbol or IconImage inside its size box"));
    if (Symbol)
    {
        U.Check(!BattleModeIcons::HasSymbolReferences(U.Button), TEXT("Vehicle button Symbol has custom graph, binding or animation references; refusing removal"));
        for (const FDelegateEditorBinding& Binding : U.Button->Bindings)
            U.Check(Binding.SourceProperty != TEXT("Symbol"), TEXT("Vehicle button binds a property to Symbol; refusing removal"));
        for (UBlueprint* BP : U.Referencers)
        {
            if (BP == U.Button) continue;
            TArray<UEdGraph*> Graphs;
            BP->GetAllGraphs(Graphs);
            for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
                if (const auto* Variable = Cast<UK2Node_Variable>(Node); Variable && Variable->VariableReference.GetMemberName() == TEXT("Symbol"))
                {
                    UClass* Owner = Variable->VariableReference.GetMemberParentClass();
                    UEdGraphPin* Self = Node->FindPin(UEdGraphSchema_K2::PN_Self);
                    UClass* SelfType = Self ? Cast<UClass>(Self->PinType.PinSubCategoryObject.Get()) : nullptr;
                    const bool bOwnReference = (Owner && Owner->IsChildOf(U.Button->GeneratedClass))
                        || (SelfType && SelfType->IsChildOf(U.Button->GeneratedClass))
                        || (Variable->VariableReference.IsSelfContext() && BP->ParentClass && BP->ParentClass->IsChildOf(U.Button->GeneratedClass));
                    U.Check(!bOwnReference, TEXT("External Blueprint references vehicle Symbol: ") + Node->GetPathName());
                }
        }
    }
    if (bRequireUpgraded)
        U.Check(Image && !RawSymbol && Size && FMath::IsNearlyEqual(Size->GetWidthOverride(), 18.f)
            && FMath::IsNearlyEqual(Size->GetHeightOverride(), 18.f), TEXT("Vehicle button does not yet use the 18 x 18 inherited image"));
    U.Report += FString::Printf(TEXT("ICON image=%s symbol=%s size=%s dimensions=%.1fx%.1f\n"), *GetPathNameSafe(Image),
        *GetPathNameSafe(Symbol), *GetPathNameSafe(Size), Size ? Size->GetWidthOverride() : 0.f, Size ? Size->GetHeightOverride() : 0.f);
    return U.bOK;
}

bool HasTransparentPixels(UTexture2D* Texture)
{
    if (!Texture) return false;
    TArray64<uint8> Bytes;
    if (Texture->Source.GetFormat() == TSF_BGRA8 && Texture->Source.GetMipData(Bytes, 0))
    {
        bool bTransparent = false, bVisible = false;
        for (int64 Index = 3; Index < Bytes.Num(); Index += 4)
        {
            bTransparent |= Bytes[Index] < 255;
            bVisible |= Bytes[Index] > 0;
            if (bTransparent && bVisible) return true;
        }
        return false;
    }
    return Texture->HasAlphaChannel();
}

FString AnimationRange(const TRange<FFrameNumber>& Range, const FFrameRate& Rate)
{
    auto BoundText = [&Rate](const TRangeBound<FFrameNumber>& Bound)
    {
        if (Bound.IsOpen()) return FString(TEXT("open"));
        return FString::Printf(TEXT("%d(%.9gs,%s)"), Bound.GetValue().Value, Rate.AsSeconds(Bound.GetValue()),
            Bound.IsInclusive() ? TEXT("inclusive") : TEXT("exclusive"));
    };
    return BoundText(Range.GetLowerBound()) + TEXT(" .. ") + BoundText(Range.GetUpperBound());
}

const TCHAR* CompletionModeName(EMovieSceneCompletionMode Mode)
{
    switch (Mode)
    {
    case EMovieSceneCompletionMode::KeepState: return TEXT("KeepState");
    case EMovieSceneCompletionMode::RestoreState: return TEXT("RestoreState");
    case EMovieSceneCompletionMode::ProjectDefault: return TEXT("ProjectDefault");
    default: return TEXT("Unknown");
    }
}

template<typename ChannelType>
void InspectNumericChannels(FUpgrade& U, const UMovieSceneSection* Section, const FFrameRate& Rate, const TCHAR* TypeName)
{
    const FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
    const auto Channels = Proxy.GetChannels<ChannelType>();
    const auto MetaData = Proxy.GetMetaData<ChannelType>();
    for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
    {
        const ChannelType* Channel = Channels[ChannelIndex];
        if (!Channel) continue;
        const FMovieSceneChannelMetaData* Meta = MetaData.IsValidIndex(ChannelIndex) ? &MetaData[ChannelIndex] : nullptr;
        const auto Default = Channel->GetDefault();
        const auto Data = Channel->GetData();
        const auto Times = Data.GetTimes();
        const auto Values = Data.GetValues();
        const FFrameNumber Offset = Meta ? Meta->GetOffsetTime(Section) : FFrameNumber(0);
        U.Report += FString::Printf(TEXT("  CHANNEL type=%s index=%d name=%s subProperty=%s enabled=%d relative=%d offsetFrame=%d default=%s preInfinity=%d postInfinity=%d keys=%d\n"),
            TypeName, ChannelIndex, Meta ? *Meta->Name.ToString() : TEXT("None"), Meta ? *Meta->SubPropertyPath.ToString() : TEXT("None"),
            Meta ? int32(Meta->bEnabled) : -1, Meta ? int32(Meta->bRelativeToSection) : -1, Offset.Value,
            Default.IsSet() ? *FString::Printf(TEXT("%.17g"), double(Default.GetValue())) : TEXT("unset"),
            int32(Channel->PreInfinityExtrap), int32(Channel->PostInfinityExtrap), Times.Num());
        for (int32 KeyIndex = 0; KeyIndex < Times.Num(); ++KeyIndex)
        {
            const auto& Value = Values[KeyIndex];
            U.Report += FString::Printf(TEXT("   KEY index=%d frame=%d seconds=%.9g absoluteFrame=%d absoluteSeconds=%.9g value=%.17g interp=%d tangentMode=%d arrive=%.9g leave=%.9g arriveWeight=%.9g leaveWeight=%.9g weightMode=%d\n"),
                KeyIndex, Times[KeyIndex].Value, Rate.AsSeconds(Times[KeyIndex]), (Times[KeyIndex] + Offset).Value, Rate.AsSeconds(Times[KeyIndex] + Offset),
                double(Value.Value), int32(Value.InterpMode), int32(Value.TangentMode), double(Value.Tangent.ArriveTangent), double(Value.Tangent.LeaveTangent),
                double(Value.Tangent.ArriveTangentWeight), double(Value.Tangent.LeaveTangentWeight), int32(Value.Tangent.TangentWeightMode));
        }
    }
}

void InspectWidgetPresentation(FUpgrade& U, UWidgetBlueprint* BP, const TCHAR* PresentationName,
    const UWidget* PaddingLeftException = nullptr)
{
    // Diagnostics only: read templates and animation channels without constructing
    // widgets, playing animations, compiling, importing, or saving packages.
    const FString Prefix(PresentationName);
    if (!U.Check(BP && BP->WidgetTree && BP->GeneratedClass, TEXT("Cannot inspect presentation: ") + Prefix)) return;
    U.Report += TEXT("\n") + Prefix + TEXT("_PRESENTATION_DETAIL ") + BP->GetPathName() + TEXT("\nCDO\n")
        + Properties(BP->GeneratedClass->GetDefaultObject());
    BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        TSet<FName> Excluded = {TEXT("Slot")};
        if (Widget == PaddingLeftException) Excluded.Add(TEXT("Padding"));
        U.Report += FString::Printf(TEXT("%s_WIDGET %s class=%s parent=%s owningTree=%s\n%s"),
            *Prefix, *Widget->GetName(), *Widget->GetClass()->GetPathName(), *GetNameSafe(Widget->GetParent()),
            *GetPathNameSafe(Widget->GetTypedOuter<UWidgetTree>()), *Properties(Widget, Excluded));
        if (Widget == PaddingLeftException)
        {
            const FMargin Padding = CastChecked<UUserWidget>(Widget)->GetPadding();
            U.Report += FString::Printf(TEXT("PRESERVED_PADDING top=%.9g right=%.9g bottom=%.9g\n"),
                double(Padding.Top), double(Padding.Right), double(Padding.Bottom));
        }
        if (const auto* Size = Cast<USizeBox>(Widget))
            U.Report += FString::Printf(TEXT(" %s_SIZEBOX width=%.9g height=%.9g\n"),
                *Prefix, double(Size->GetWidthOverride()), double(Size->GetHeightOverride()));
        if (Widget->Slot)
            U.Report += TEXT(" ") + Prefix + TEXT("_SLOT ") + Widget->Slot->GetClass()->GetPathName() + TEXT("\n") + Properties(Widget->Slot);
    });
    U.Report += Prefix + TEXT("_BUSINESS_GRAPHS\n") + BusinessGraphs(BP);
    for (const UWidgetAnimation* Animation : BP->Animations)
    {
        if (!Animation || !Animation->MovieScene)
        {
            U.Report += Prefix + TEXT("_ANIMATION missing MovieScene: ") + GetNameSafe(Animation) + TEXT("\n");
            continue;
        }
        const UMovieScene* Scene = Animation->MovieScene;
        const FFrameRate Rate = Scene->GetTickResolution();
        const FFrameRate DisplayRate = Scene->GetDisplayRate();
        U.Report += FString::Printf(TEXT("%s_ANIMATION %s start=%.9g end=%.9g duration=%.9g tickRate=%d/%d displayRate=%d/%d playback=%s defaultCompletion=%s\n"),
            *Prefix, *Animation->GetName(), double(Animation->GetStartTime()), double(Animation->GetEndTime()),
            double(Animation->GetEndTime() - Animation->GetStartTime()), Rate.Numerator, Rate.Denominator,
            DisplayRate.Numerator, DisplayRate.Denominator, *AnimationRange(Scene->GetPlaybackRange(), Rate), CompletionModeName(Animation->DefaultCompletionMode));
        for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
            U.Report += FString::Printf(TEXT(" ANIMATION_TARGET widget=%s slot=%s guid=%s root=%d\n"),
                *Binding.WidgetName.ToString(), *Binding.SlotWidgetName.ToString(), *Binding.AnimationGuid.ToString(), Binding.bIsRootWidget);
        auto InspectTrack = [&](const UMovieSceneTrack* Track, const FString& BindingGuid)
        {
            if (!Track) return;
            const auto* Property = Cast<UMovieScenePropertyTrack>(Track);
            U.Report += FString::Printf(TEXT(" ANIMATION_TRACK %s binding=%s class=%s property=%s path=%s sections=%d\n"),
                *Track->GetName(), *BindingGuid, *Track->GetClass()->GetPathName(),
                Property ? *Property->GetPropertyName().ToString() : TEXT("None"),
                Property ? *Property->GetPropertyPath().ToString() : TEXT("None"), Track->GetAllSections().Num());
            for (const UMovieSceneSection* Section : Track->GetAllSections())
            {
                if (!Section) continue;
                U.Report += FString::Printf(TEXT(" ANIMATION_SECTION %s class=%s range=%s active=%d completion=%s preRoll=%d postRoll=%d\n"),
                    *Section->GetName(), *Section->GetClass()->GetPathName(), *AnimationRange(Section->GetRange(), Rate), Section->IsActive(),
                    CompletionModeName(Section->GetCompletionMode()), Section->GetPreRollFrames(), Section->GetPostRollFrames());
                InspectNumericChannels<FMovieSceneFloatChannel>(U, Section, Rate, TEXT("Float"));
                InspectNumericChannels<FMovieSceneDoubleChannel>(U, Section, Rate, TEXT("Double"));
            }
        };
        for (const FMovieSceneBinding& Binding : Scene->GetBindings())
            for (const UMovieSceneTrack* Track : Binding.GetTracks()) InspectTrack(Track, Binding.GetObjectGuid().ToString());
        for (const UMovieSceneTrack* Track : Scene->GetTracks()) InspectTrack(Track, TEXT("Root"));
    }
    U.Report += TEXT("END_") + Prefix + TEXT("_PRESENTATION_DETAIL\n\n");
}

void Inspect(FUpgrade& U, bool bRequireUpgraded)
{
    InspectWidgetPresentation(U, U.Vehicle, TEXT("VEHICLE"));
    InspectWidgetPresentation(U, LoadObject<UWidgetBlueprint>(nullptr, *PersonnelPath), TEXT("PERSONNEL"));
    UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TexturePath);
    CheckStructure(U, bRequireUpgraded);
    auto InspectButton = [&](UBattleHUDButton* Button, const FString& Owner)
    {
        U.Report += FString::Printf(TEXT("BUTTON %s class=%s icon=%s size=%s mode=%d text=%s min=%s slot=%s\n"),
            *Owner, *Button->GetClass()->GetPathName(), *GetPathNameSafe(Button->IconBrush.GetResourceObject()), *Button->IconSize.ToString(),
            int32(Button->ContentMode), *Button->ButtonText.ToString(), *Button->MinimumSize.ToString(), *Properties(Button->Slot));
        if (bRequireUpgraded)
            U.Check(Texture && Button->IconBrush.GetResourceObject() == Texture && Button->IconSize.Equals(FVector2D(18, 18))
                && Button->ContentMode == EBasicButtonContent::IconAndText, TEXT("Vehicle button instance still has the old icon: ") + Owner);
    };
    InspectButton(CastChecked<UBattleHUDButton>(U.Button->GeneratedClass->GetDefaultObject()), U.Button->GetPathName() + TEXT(".CDO"));
    TArray<UWidgetBlueprint*> Owners = U.Dependents;
    Owners.AddUnique(U.Member);
    for (UWidgetBlueprint* BP : Owners)
    {
        U.Report += FString::Printf(TEXT("ASSET %s parent=%s status=%d\n"), *BP->GetPathName(), *GetPathNameSafe(BP->ParentClass), int32(BP->Status));
        BP->WidgetTree->ForEachWidgetAndDescendants([&](UWidget* Widget)
        {
            if (IsVehicleButton(U, Widget)) InspectButton(CastChecked<UBattleHUDButton>(Widget), Widget->GetPathName());
        });
    }
    U.Report += FString::Printf(TEXT("TEXTURE %s exists=%d UI=%d neverStream=%d noAlpha=%d alphaPixels=%d size=%dx%d\n"), *TexturePath,
        Texture != nullptr, Texture && Texture->LODGroup == TEXTUREGROUP_UI, Texture && Texture->NeverStream,
        Texture && Texture->CompressionNoAlpha, HasTransparentPixels(Texture), Texture ? Texture->Source.GetSizeX() : 0, Texture ? Texture->Source.GetSizeY() : 0);
    if (bRequireUpgraded)
        U.Check(Texture && Texture->LODGroup == TEXTUREGROUP_UI && Texture->NeverStream && !Texture->CompressionNoAlpha
            && Texture->MipGenSettings == TMGS_NoMipmaps && HasTransparentPixels(Texture), TEXT("Vehicle front texture lacks the required UI settings or transparent source pixels"));
}

struct FPreservedBlueprint
{
    UWidgetBlueprint* Blueprint = nullptr;
    FString Graphs;
    TMap<FName, FString> Widgets;
    FString ButtonDefaults;
};

FString PreservedWidgetProperties(UWidget* Widget)
{
    TSet<FName> Excluded;
    if (Widget->IsA<UBattleHUDButton>()) Excluded = {TEXT("IconBrush"), TEXT("IconSize"), TEXT("ContentMode")};
    if (Widget->GetFName() == TEXT("VehicleGlyphSize") || Widget->GetFName() == TEXT("IconSizeBox"))
    {
        Excluded.Add(TEXT("WidthOverride")); Excluded.Add(TEXT("HeightOverride"));
        Excluded.Add(TEXT("bOverride_WidthOverride")); Excluded.Add(TEXT("bOverride_HeightOverride"));
    }
    FString Result = Widget->GetClass()->GetPathName() + TEXT("\n") + Properties(Widget, Excluded)
        + TEXT("PARENT=") + GetNameSafe(Widget->GetParent()) + TEXT("\nSLOT=") + Properties(Widget->Slot);
    return Result;
}

bool BackupPackage(FUpgrade& U, const FString& PackagePath)
{
    const FString Base = FPackageName::LongPackageNameToFilename(PackagePath);
    for (const TCHAR* Extension : {TEXT(".uasset"), TEXT(".uexp"), TEXT(".ubulk")})
    {
        const FString Source = Base + Extension;
        if (!FPaths::FileExists(Source)) continue;
        const FString Destination = U.BackupFolder / PackagePath.RightChop(1) + Extension;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
        if (!U.Check(IFileManager::Get().Copy(*Destination, *Source) == COPY_OK, TEXT("Backup failed: ") + Source)) return false;
        U.Report += TEXT("BACKUP_FILE ") + Source + TEXT(" -> ") + Destination + TEXT("\n");
    }
    return true;
}

bool Apply(FUpgrade& U)
{
    const FString SourceFile = FPaths::ProjectContentDir() / TEXT("UI/Battle/Icons/VehicleFront.png");
    if (!U.Check(FPaths::FileExists(SourceFile), TEXT("Missing generated vehicle artwork: ") + SourceFile) || !CheckStructure(U, false)) return false;
    const UWidget* AuthoredVehicle = U.Member->WidgetTree->FindWidget(TEXT("VehiclePanel"));
    const UPanelWidget* LegacyHost = Cast<UPanelWidget>(U.Member->WidgetTree->FindWidget(TEXT("VehiclePanelContainer")));
    // Inspect/Validate accept the current authored placement. This legacy import
    // operation must stop before importing or changing anything after a user move.
    if (!U.Check(LegacyHost && (!AuthoredVehicle || AuthoredVehicle->GetParent() == LegacyHost),
        TEXT("VehiclePanel uses a custom authored placement or its legacy container was removed; use -Inspect/-Validate. This upgrade will not move the panel or change its artwork."))) return false;
    TArray<UWidgetBlueprint*> Changed = {U.Button, U.Member};
    for (UWidgetBlueprint* BP : U.Dependents) Changed.AddUnique(BP);
    TArray<FPreservedBlueprint> Preserved;
    // Include the vehicle asset in the read-only snapshots: its user business/layout must remain untouched.
    TArray<UWidgetBlueprint*> Observed = Changed;
    Observed.AddUnique(U.Vehicle);
    for (UWidgetBlueprint* BP : Observed)
    {
        FPreservedBlueprint& Saved = Preserved.AddDefaulted_GetRef();
        Saved.Blueprint = BP;
        Saved.Graphs = BusinessGraphs(BP);
        BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            if (BP == U.Button && (Widget->GetFName() == TEXT("Symbol") || Widget->GetFName() == TEXT("IconImage"))) return;
            if (BP == U.Member && Widget->GetFName() == TEXT("VehiclePanel")) return;
            Saved.Widgets.Add(Widget->GetFName(), PreservedWidgetProperties(Widget));
        });
        if (BP == U.Button) Saved.ButtonDefaults = Properties(BP->GeneratedClass->GetDefaultObject(), {TEXT("IconBrush"), TEXT("IconSize"), TEXT("ContentMode")});
    }
    U.BackupFolder = FPaths::ProjectSavedDir() / TEXT("BattleVehiclePresentation/Backup") /
        (FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    for (UWidgetBlueprint* BP : Changed)
        if (!BackupPackage(U, BP->GetOutermost()->GetName())) return false;
    if (!BackupPackage(U, TexturePath)) return false;
    U.Report += TEXT("BACKUP ") + U.BackupFolder + TEXT("\n");
    for (const FPreservedBlueprint& Saved : Preserved)
        U.Report += TEXT("BEFORE ") + Saved.Blueprint->GetPathName() + TEXT("\n") + Saved.Graphs;
    if (!U.Check(FFileHelper::SaveStringToFile(U.Report, *(U.BackupFolder / TEXT("Before.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM), TEXT("Could not persist pre-migration report"))) return false;

    UTexture2D* Texture = BattleModeIcons::Import(SourceFile, TexturePath);
    if (!U.Check(Texture && HasTransparentPixels(Texture), TEXT("Texture import failed or PNG has no visible transparent artwork"))) return false;
    auto* Size = Cast<USizeBox>(U.Button->WidgetTree->FindWidget(TEXT("IconSizeBox")));
    if (!Size) Size = CastChecked<USizeBox>(U.Button->WidgetTree->FindWidget(TEXT("VehicleGlyphSize")));
    auto* Image = Cast<UImage>(U.Button->WidgetTree->FindWidget(TEXT("IconImage")));
    if (!Image)
    {
        UWidget* Symbol = U.Button->WidgetTree->FindWidget(TEXT("Symbol"));
        if (!U.Check(Size->RemoveChild(Symbol) && Symbol->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional), TEXT("Could not remove the unreferenced legacy vehicle Symbol"))) return false;
        U.Button->WidgetVariableNameToGuidMap.Remove(TEXT("Symbol"));
        Image = U.Button->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IconImage"));
        Size->AddChild(Image);
    }
    // Keep the authored size-box name, parent/slot and vertical label; IconImage alone binds
    // the inherited brush/tint path. No graph/animation references need a rename.
    Size->SetWidthOverride(18.f);
    Size->SetHeightOverride(18.f);
    Image->bIsVariable = true;
    Image->SetVisibility(ESlateVisibility::HitTestInvisible);
    Image->SetBrushFromTexture(Texture);
    Image->SetColorAndOpacity(CastChecked<UBattleHUDButton>(U.Button->GeneratedClass->GetDefaultObject())->ButtonForegroundColor);
    if (!U.Check(BattleModeAssets::Compile(U.Button), TEXT("Vehicle button compile failed"))) return false;
    BattleModeIcons::SetIcon(CastChecked<UBattleHUDButton>(U.Button->GeneratedClass->GetDefaultObject()), Texture);

    auto* Host = CastChecked<UPanelWidget>(U.Member->WidgetTree->FindWidget(TEXT("VehiclePanelContainer")));
    auto* Placed = Cast<UBattleVehiclePanelWidget>(U.Member->WidgetTree->FindWidget(TEXT("VehiclePanel")));
    if (!Placed)
    {
        Placed = U.Member->WidgetTree->ConstructWidget<UBattleVehiclePanelWidget>(U.Vehicle->GeneratedClass.Get(), TEXT("VehiclePanel"));
        if (!U.Check(Placed && Host->AddChild(Placed), TEXT("Could not insert the existing vehicle Blueprint into its existing container"))) return false;
        if (auto* Slot = Cast<UHorizontalBoxSlot>(Placed->Slot))
        {
            Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
            Slot->SetHorizontalAlignment(HAlign_Fill);
            Slot->SetVerticalAlignment(VAlign_Fill);
        }
    }
    Placed->bIsVariable = true;
    Placed->SetVisibility(ESlateVisibility::Collapsed);
    // Member/HUD instance overrides must receive the new brush too; all other button settings stay intact.
    auto UpdateInstances = [&](UWidgetBlueprint* BP)
    {
        BP->WidgetTree->ForEachWidgetAndDescendants([&](UWidget* Widget)
        {
            if (IsVehicleButton(U, Widget)) BattleModeIcons::SetIcon(CastChecked<UBattleHUDButton>(Widget), Texture);
        });
    };
    UpdateInstances(U.Member);
    if (!U.Check(BattleModeAssets::Compile(U.Member), TEXT("Member mode compile failed"))) return false;
    for (UWidgetBlueprint* BP : U.Dependents)
    {
        UpdateInstances(BP);
        if (!U.Check(BattleModeAssets::Compile(BP), TEXT("Related HUD compile failed: ") + BP->GetPathName())) return false;
    }
    for (const FPreservedBlueprint& Saved : Preserved)
    {
        U.Check(BusinessGraphs(Saved.Blueprint) == Saved.Graphs, TEXT("Compilation changed user business graphs/bindings: ") + Saved.Blueprint->GetPathName());
        for (const auto& Pair : Saved.Widgets)
        {
            UWidget* Widget = Saved.Blueprint->WidgetTree->FindWidget(Pair.Key);
            U.Check(Widget && PreservedWidgetProperties(Widget) == Pair.Value,
                TEXT("Unrelated authored layout/options changed: ") + Saved.Blueprint->GetPathName() + TEXT(".") + Pair.Key.ToString());
        }
        if (Saved.Blueprint == U.Button)
            U.Check(Properties(U.Button->GeneratedClass->GetDefaultObject(), {TEXT("IconBrush"), TEXT("IconSize"), TEXT("ContentMode")}) == Saved.ButtonDefaults,
                TEXT("Button text, dimensions, sounds or style defaults changed unexpectedly"));
    }
    Inspect(U, true);
    if (!U.bOK) return false;
    if (!U.Check(BattleModeIcons::SaveTexture(Texture), TEXT("Texture save failed"))) return false;
    for (UWidgetBlueprint* BP : Changed)
        if (!U.Check(BattleModeAssets::Save(BP), TEXT("Blueprint save failed: ") + BP->GetPathName())) return false;
    U.Report += TEXT("SAVED narrow upgrade; personnel and vehicle business assets were not saved\n");
    return true;
}
}

int32 RunBattleVehiclePresentationUpgrade(const FString& Params)
{
    using namespace BattleVehiclePresentation;
    FUpgrade U;
    U.Member = LoadObject<UWidgetBlueprint>(nullptr, *MemberPath);
    U.Vehicle = LoadObject<UWidgetBlueprint>(nullptr, *VehiclePath);
    U.Button = LoadObject<UWidgetBlueprint>(nullptr, *ButtonPath);
    if (!U.Check(U.Member && U.Member->WidgetTree && U.Member->GeneratedClass && U.Member->GeneratedClass->IsChildOf(UBattleMemberModeWidget::StaticClass())
        && U.Vehicle && U.Vehicle->WidgetTree && U.Vehicle->GeneratedClass && U.Vehicle->GeneratedClass->IsChildOf(UBattleVehiclePanelWidget::StaticClass())
        && U.Button && U.Button->WidgetTree && U.Button->GeneratedClass && U.Button->GeneratedClass->IsChildOf(UBattleHUDButton::StaticClass()),
        TEXT("Requires existing member, vehicle and dedicated vehicle-button Blueprints")))
    {
        U.Write(TEXT("Inspect.txt"));
        return 1;
    }
    DiscoverDependents(U);
    const bool bValidate = FParse::Param(*Params, TEXT("Validate"));
    const bool bInspect = FParse::Param(*Params, TEXT("Inspect"));
    Inspect(U, bValidate);
    if (!U.Write(TEXT("Inspect.txt"))) return 1;
    if (!bInspect && !bValidate && U.bOK)
    {
        Apply(U);
        U.Write(TEXT("Apply.txt"));
    }
    if (bValidate && U.bOK)
    {
        U.Check(MainMapBP::Compile(U.Button) && MainMapBP::Compile(U.Member), TEXT("Cold vehicle presentation compile failed"));
        for (UWidgetBlueprint* BP : U.Dependents) U.Check(MainMapBP::Compile(BP), TEXT("Cold dependent compile failed: ") + BP->GetPathName());
        Inspect(U, true);
        U.Write(TEXT("Validate.txt"));
    }
    UE_LOG(LogTemp, Display, TEXT("VEHICLE_PRESENTATION_%s mode=%s dependents=%d backup=%s"),
        U.bOK ? TEXT("OK") : TEXT("FAILED"), bValidate ? TEXT("VALIDATE") : bInspect ? TEXT("INSPECT") : TEXT("UPGRADE"), U.Dependents.Num(), *U.BackupFolder);
    return U.bOK ? 0 : 1;
}

int32 RunBattlePanelLayoutRepair(const FString& Params)
{
    using namespace BattleVehiclePresentation;
    FUpgrade U;
    const bool bApply = FParse::Param(*Params, TEXT("Apply"));
    const bool bInspect = FParse::Param(*Params, TEXT("Inspect"));
    const bool bValidate = FParse::Param(*Params, TEXT("Validate"));
    const FString ReportFolder = FPaths::ProjectSavedDir() / TEXT("BattlePanelLayoutRepair");
    auto Write = [&](const TCHAR* Name, const FString& Text)
    {
        IFileManager::Get().MakeDirectory(*ReportFolder, true);
        return U.Check(FFileHelper::SaveStringToFile(Text, *(ReportFolder / Name),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM), TEXT("Cannot write layout repair report"));
    };
    U.Member = LoadObject<UWidgetBlueprint>(nullptr, *MemberPath);
    auto* HUD = LoadObject<UWidgetBlueprint>(nullptr, *HUDPath);
    U.Vehicle = LoadObject<UWidgetBlueprint>(nullptr, *VehiclePath);
    auto* Personnel = LoadObject<UWidgetBlueprint>(nullptr, *PersonnelPath);
    U.Check(!(bApply && (bInspect || bValidate)), TEXT("Choose -Apply, -Inspect or -Validate separately"));
    for (UWidgetBlueprint* BP : {U.Member, HUD, U.Vehicle, Personnel})
        U.Check(BP && BP->WidgetTree && BP->GeneratedClass, TEXT("Missing authored Blueprint/tree/class: ") + GetNameSafe(BP));
    auto* Vehicle = U.Member && U.Member->WidgetTree
        ? Cast<UBattleVehiclePanelWidget>(U.Member->WidgetTree->FindWidget(TEXT("VehiclePanel"))) : nullptr;
    U.Check(Vehicle && Vehicle->GetTypedOuter<UWidgetTree>() == U.Member->WidgetTree,
        TEXT("Expected the existing typed VehiclePanel instance owned by WBP_成员模式"));
    if (!U.bOK) { Write(TEXT("Inspect.txt"), U.Report); return 1; }

    const FMargin BeforePadding = Vehicle->GetPadding();
    const FMargin DefaultPadding = Vehicle->GetClass()->GetDefaultObject<UBattleVehiclePanelWidget>()->GetPadding();
    U.Report += FString::Printf(TEXT("TARGET %s instancePadding=(%.9g,%.9g,%.9g,%.9g) classPadding=(%.9g,%.9g,%.9g,%.9g)\n"),
        *Vehicle->GetPathName(), double(BeforePadding.Left), double(BeforePadding.Top), double(BeforePadding.Right), double(BeforePadding.Bottom),
        double(DefaultPadding.Left), double(DefaultPadding.Top), double(DefaultPadding.Right), double(DefaultPadding.Bottom));
    auto Snapshot = [&]()
    {
        FUpgrade SnapshotReport;
        // Only the targeted instance's left padding is excluded. Its other three
        // margins, all hierarchy/slots, business graphs and animation keys stay checked.
        InspectWidgetPresentation(SnapshotReport, U.Member, TEXT("MEMBER"), U.Member->WidgetTree->FindWidget(TEXT("VehiclePanel")));
        InspectWidgetPresentation(SnapshotReport, HUD, TEXT("HUD"));
        InspectWidgetPresentation(SnapshotReport, U.Vehicle, TEXT("VEHICLE"));
        InspectWidgetPresentation(SnapshotReport, Personnel, TEXT("PERSONNEL"));
        U.Check(SnapshotReport.bOK, TEXT("Cannot snapshot authored presentation"));
        return SnapshotReport.Report;
    };
    const FString Before = Snapshot();
    if (!Write(TEXT("Before.txt"), Before) || !Write(TEXT("Inspect.txt"), U.Report)) return 1;
    if (bApply)
    {
        // Deliberately reject already-repaired or later user-authored values.
        if (!U.Check(BeforePadding.Left == 230.f && DefaultPadding.Left == 0.f,
            TEXT("Apply requires the diagnosed instance Padding.Left=230 and class default=0; refusing to overwrite another value")))
        { Write(TEXT("Apply.txt"), U.Report); return 1; }
        U.BackupFolder = ReportFolder / TEXT("Backup") /
            (FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
        if (!BackupPackage(U, U.Member->GetOutermost()->GetName())) { Write(TEXT("Apply.txt"), U.Report); return 1; }
        if (!U.Check(FFileHelper::SaveStringToFile(Before, *(U.BackupFolder / TEXT("Before.txt")),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM), TEXT("Cannot persist backup snapshot"))) return 1;
        FMargin Repaired = BeforePadding;
        Repaired.Left = 0.f;
        Vehicle->SetPadding(Repaired);
        U.Report += TEXT("CHANGED VehiclePanel.Padding.Left: 230 -> 0 (other margins retained)\n");
    }
    if (bApply || bValidate)
    {
        U.Check(MainMapBP::Compile(U.Member), TEXT("Member mode cold compile failed"));
        U.Check(MainMapBP::Compile(HUD), TEXT("Battle HUD cold compile failed"));
        Vehicle = Cast<UBattleVehiclePanelWidget>(U.Member->WidgetTree->FindWidget(TEXT("VehiclePanel")));
        U.Check(Vehicle && Vehicle->GetPadding().Left == 0.f, TEXT("VehiclePanel left padding is not zero"));
        const FString After = Snapshot();
        U.Check(Before == After, TEXT("Unrelated authored layout, defaults, business graph or animation changed; refusing to save"));
        Write(TEXT("After.txt"), After);
        if (bApply && U.bOK)
        {
            U.Check(BattleModeAssets::Save(U.Member), TEXT("Member mode save failed"));
            if (U.bOK) U.Report += TEXT("SAVED only WBP_成员模式; HUD compiled without saving; vehicle/personnel assets untouched\n");
        }
    }
    Write(bApply ? TEXT("Apply.txt") : bValidate ? TEXT("Validate.txt") : TEXT("Inspect.txt"), U.Report);
    UE_LOG(LogTemp, Display, TEXT("BATTLE_PANEL_LAYOUT_REPAIR_%s mode=%s backup=%s"), U.bOK ? TEXT("OK") : TEXT("FAILED"),
        bApply ? TEXT("APPLY") : bValidate ? TEXT("VALIDATE") : TEXT("INSPECT"), *U.BackupFolder);
    return U.bOK ? 0 : 1;
}
