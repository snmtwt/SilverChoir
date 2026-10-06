#include "BaseUIAssetsCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundBase.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "UIBasic/BasicButtonWidget.h"
#include "H5UI_View.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/SquadMeetingRoomWidget.h"
#include "Map/BaseMap/SceneUI/CommanderOffice/CommanderOfficeWidget.h"
#include "Map/BaseMap/SceneUI/OperationsCommandRoom/OperationsCommandRoomWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/PersonnelPreparationRoomWidget.h"

UBaseUIAssetsCommandlet::UBaseUIAssetsCommandlet() { IsClient=false; IsEditor=true; LogToConsole=true; }
namespace
{
UWidgetBlueprint* MakeBP(const FString& Path, UClass* Parent)
{
    if (auto* Existing=LoadObject<UWidgetBlueprint>(nullptr,*Path)) return Existing;
    auto* Factory=NewObject<UWidgetBlueprintFactory>();
    Factory->ParentClass=Parent;
    auto* BP=Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(),CreatePackage(*Path),*FPackageName::GetShortName(Path),RF_Public|RF_Standalone,nullptr,GWarn));
    if (BP) FAssetRegistryModule::AssetCreated(BP);
    return BP;
}
bool SaveBP(UWidgetBlueprint* BP)
{
    BP->WidgetTree->ForEachWidget([&](UWidget* W) { if (!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName())) BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewGuid()); });
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status==BS_Error) return false;
    const FString File=FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    if (FPaths::FileExists(File))
    {
        const FString Backup=FPaths::ProjectSavedDir()/TEXT("BaseUIBackups")/FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))/FPaths::GetCleanFilename(File);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup),true);
        if (IFileManager::Get().Copy(*Backup,*File)!=COPY_OK) return false;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
    BP->MarkPackageDirty();
    return UPackage::SavePackage(BP->GetOutermost(),BP,*File,Args);
}
}
int32 UBaseUIAssetsCommandlet::Main(const FString& Params)
{
    auto* Shell=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget"));
    auto* Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/System/UIBasic/Textures/T_ValCurrency"));
    if (!Shell || !Shell->WidgetTree || !Texture) return 1;
    Shell->WidgetTree->ForEachWidget([](UWidget* W) { UE_LOG(LogTemp,Display,TEXT("BASE_EXISTING_WIDGET %s %s"),*W->GetName(),*W->GetClass()->GetName()); });
    if (Params.Contains(TEXT("Inspect"))) return 0;
    if (Params.Contains(TEXT("Polish")))
    {
        UWidgetTree* Tree=Shell->WidgetTree;
        auto* Amount=Cast<UTextBlock>(Tree->FindWidget(TEXT("CurrencyText")));
        if (!Amount) return 1;
        Amount->SetJustification(ETextJustify::Left);
        auto* AmountSlot=Cast<UCanvasPanelSlot>(Amount->Slot);
        if (!AmountSlot) return 1;
        AmountSlot->SetPosition(FVector2D(-210,37));
        AmountSlot->SetSize(FVector2D(178,40));
        for (const bool bFooter : {false,true})
        {
            auto* Panel=Cast<UCanvasPanel>(Tree->FindWidget(bFooter?TEXT("FooterPanel"):TEXT("HeaderPanel")));
            if (!Panel) return 1;
            const FName Name=bFooter?TEXT("FooterEffects"):TEXT("HeaderEffects");
            auto* View=Cast<UH5UI_View>(Tree->FindWidget(Name));
            if (!View) View=Tree->ConstructWidget<UH5UI_View>(UH5UI_View::StaticClass(),Name);
            View->URL=bFooter?TEXT("coui://uiresources/BaseShell/footer.html"):TEXT("coui://uiresources/BaseShell/header.html");
            View->bAutoLoad=true; View->bReceiveInput=false; View->bConsumeInput=false;
            View->bEnableJavaScript=false; View->bEnableBrowserSubviews=false;
            View->TargetFrameRate=30; View->RenderScale=1;
            View->DefaultSize=FVector2D(1920,bFooter?60:88);
            View->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto* Slot=Cast<UCanvasPanelSlot>(View->Slot);
            if (!Slot) Slot=Panel->AddChildToCanvas(View);
            Slot->SetAnchors(FAnchors(0,0,1,1)); Slot->SetOffsets(FMargin(0)); Slot->SetZOrder(1);
            // Keep effects above the plate but below all native text and input.
            for (UWidget* Child:Panel->GetAllChildren())
                if (Child!=View && !Child->GetName().EndsWith(TEXT("Background")))
                    if (auto* ChildSlot=Cast<UCanvasPanelSlot>(Child->Slot)) ChildSlot->SetZOrder(2);
        }
        if (!SaveBP(Shell)) return 1;
        UE_LOG(LogTemp,Display,TEXT("BASE_UI_POLISH_OK"));
        return 0;
    }
    auto* Action=MakeBP(TEXT("/Game/System/UIBasic/WBP_ShellButton"),UBasicButtonWidget::StaticClass());
    if (!Action || !SaveBP(Action)) return 1;
    auto* ActionDefaults=Cast<UBasicButtonWidget>(Action->GeneratedClass->GetDefaultObject());
    ActionDefaults->MinimumSize=FVector2D(160,36);
    ActionDefaults->Font.Size=12;
    ActionDefaults->ButtonText=FText::FromString(TEXT("返回基地"));
    ActionDefaults->HoverSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuHover_Electronic"));
    ActionDefaults->PressSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuPress_Terminal"));
    if (!SaveBP(Action)) return 1;

    struct FRoom { const TCHAR* Name; const TCHAR* Tag; UClass* Class; };
    const FRoom Rooms[]={
        {TEXT("WBP_SquadMeetingRoom"),TEXT("GameScene.SquadMeetingRoom"),USquadMeetingRoomWidget::StaticClass()},
        {TEXT("WBP_CommanderOffice"),TEXT("GameScene.CommanderOffice"),UCommanderOfficeWidget::StaticClass()},
        {TEXT("WBP_OperationsCommandRoom"),TEXT("GameScene.OperationsCommandRoom"),UOperationsCommandRoomWidget::StaticClass()},
        {TEXT("WBP_PersonnelPreparationRoom"),TEXT("GameScene.PersonnelPreparationRoom"),UPersonnelPreparationRoomWidget::StaticClass()}
    };
    for (const FRoom& Room: Rooms)
    {
        const FString Folder=FString(Room.Name).RightChop(4);
        auto* BP=MakeBP(FString(TEXT("/Game/System/Map/BaseMap/UI/SceneUI/"))+Folder+TEXT("/")+Room.Name,Room.Class);
        if (!BP) return 1;
        if (!BP->WidgetTree->RootWidget)
        {
            auto* Root=BP->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("ContentRoot"));
            Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            BP->WidgetTree->RootWidget=Root;
        }
        if (!SaveBP(BP)) return 1;
        Cast<UBaseSceneWidget>(BP->GeneratedClass->GetDefaultObject())->SceneTag=FGameplayTag::RequestGameplayTag(Room.Tag);
        if (!SaveBP(BP)) return 1;
    }
    // Do not reconstruct any user-authored graphs or previously edited shell.
    if (!Shell->WidgetTree->FindWidget(TEXT("BaseShellSafeZone")))
    {
        UWidgetTree* Tree=Shell->WidgetTree;
        UWidget* OldRoot=Tree->RootWidget;
        auto* Root=Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("BaseShellRoot"));
        Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        Tree->RootWidget=Root;
        auto Fill=[](UOverlaySlot* Slot) { Slot->SetHorizontalAlignment(HAlign_Fill); Slot->SetVerticalAlignment(VAlign_Fill); };
        if (OldRoot) Fill(Root->AddChildToOverlay(OldRoot));
        auto* Safe=Tree->ConstructWidget<USafeZone>(USafeZone::StaticClass(),TEXT("BaseShellSafeZone"));
        Safe->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        Fill(Root->AddChildToOverlay(Safe));
        auto* Canvas=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("ShellCanvas"));
        Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible); Safe->AddChild(Canvas);
        auto* Content=Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("SceneContent"));
        Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        auto* CS=Canvas->AddChildToCanvas(Content); CS->SetAnchors(FAnchors(0,0,1,1)); CS->SetOffsets(FMargin(32,104,32,76));
        auto Panel=[&](const TCHAR* Name,bool Bottom,float Height)
        {
            auto* P=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),Name);
            P->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
            auto* S=Canvas->AddChildToCanvas(P); S->SetAnchors(FAnchors(0,Bottom?1:0,1,Bottom?1:0));
            S->SetOffsets(FMargin(0,Bottom?-Height:0,0,Height)); S->SetZOrder(10); return P;
        };
        auto* Header=Panel(TEXT("HeaderPanel"),false,88);
        auto* Footer=Panel(TEXT("FooterPanel"),true,60);
        auto Image=[&](UCanvasPanel* P,const TCHAR* Name,FLinearColor Color,FAnchors Anchors,FMargin Offsets,UTexture2D* Tex=nullptr)
        {
            auto* W=Tree->ConstructWidget<UImage>(UImage::StaticClass(),Name);
            if (Tex) W->SetBrushFromTexture(Tex);
            W->SetColorAndOpacity(Color); W->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto* S=P->AddChildToCanvas(W); S->SetAnchors(Anchors); S->SetOffsets(Offsets); return W;
        };
        auto Color=[](const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); };
        Image(Header,TEXT("HeaderBackground"),Color(TEXT("040C14F5")),FAnchors(0,0,1,1),FMargin(0));
        Image(Footer,TEXT("FooterBackground"),Color(TEXT("040C14F5")),FAnchors(0,0,1,1),FMargin(0));
        Image(Header,TEXT("HeaderRule"),Color(TEXT("234657")),FAnchors(0,1,1,1),FMargin(28,-1,28,1));
        Image(Footer,TEXT("FooterRule"),Color(TEXT("234657")),FAnchors(0,0,1,0),FMargin(28,0,28,1));
        auto Text=[&](UCanvasPanel* P,const TCHAR* Name,const TCHAR* Value,int Size,const TCHAR* Hex,FVector2D Pos,FVector2D Extent,float AnchorX=0)
        {
            auto* W=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
            W->SetText(FText::FromString(Value)); W->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")),Size,TEXT("Regular")));
            W->SetColorAndOpacity(Color(Hex)); W->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto* S=P->AddChildToCanvas(W); S->SetAnchors(FAnchors(AnchorX,0)); S->SetPosition(Pos); S->SetSize(Extent); return W;
        };
        Text(Header,TEXT("BrandText"),TEXT("S I L V E R   C H O I R  /  BASE OPERATIONS"),9,TEXT("668B9F"),{32,12},{420,18});
        Text(Header,TEXT("TitleText"),TEXT("基地控制中心"),25,TEXT("DCE9F5"),{32,33},{430,42});
        Image(Header,TEXT("CurrencyIcon"),Color(TEXT("C8DFEB")),FAnchors(1,0),FMargin(-262,32,36,36),Texture);
        Text(Header,TEXT("CurrencyCaption"),TEXT("瓦尔 / VAL"),10,TEXT("668B9F"),{-210,17},{178,20},1);
        Text(Header,TEXT("CurrencyText"),TEXT("0"),23,TEXT("DCE9F5"),{-210,37},{178,40},1)->SetJustification(ETextJustify::Left);
        auto* Back=Tree->ConstructWidget<UBasicButtonWidget>(Action->GeneratedClass.Get(),TEXT("BackButton"));
        Back->ButtonText=FText::FromString(TEXT("返回基地")); Back->MinimumSize={144,34}; Back->Font.Size=12;
        auto* BS=Footer->AddChildToCanvas(Back); BS->SetPosition({32,13}); BS->SetSize({144,34});
        Text(Footer,TEXT("StatusIndicator"),TEXT("●"),10,TEXT("329ACA"),{-60,23},{16,20},.5f);
        Text(Footer,TEXT("StatusText"),TEXT("基地在线"),11,TEXT("8EADBE"),{-38,21},{220,24},.5f);
        Text(Footer,TEXT("TerminalLabel"),TEXT("SC / BASE TERMINAL"),9,TEXT("537C98"),{-220,23},{188,20},1)->SetJustification(ETextJustify::Right);
        // Existing modal content must stay above the newly inserted shell.
        if (auto* Modal=Tree->FindWidget(TEXT("ModalLayer")))
        {
            Modal->RemoveFromParent(); Fill(Root->AddChildToOverlay(Modal));
        }
    }
    if (!SaveBP(Shell)) return 1;
    UE_LOG(LogTemp,Display,TEXT("BASE_UI_ASSETS_OK"));
    return 0;
}
