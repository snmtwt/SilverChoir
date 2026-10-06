#include "ECGAssetsCommandlet.h"
#include "MainMapBlueprintBuilder.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "H5UI_View.h"
#include "UIBasic/ECGWidget.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

UECGAssetsCommandlet::UECGAssetsCommandlet(){IsClient=false;IsEditor=true;LogToConsole=true;}
int32 UECGAssetsCommandlet::Main(const FString& Params)
{
    const TCHAR* Path=TEXT("/Game/System/UIBasic/WBP_ECG");
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,Path);
    if(!BP)
    {
        auto* Factory=NewObject<UWidgetBlueprintFactory>();Factory->ParentClass=UECGWidget::StaticClass();
        BP=CastChecked<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(),CreatePackage(Path),TEXT("WBP_ECG"),RF_Public|RF_Standalone,nullptr,GWarn));
        auto* Root=BP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("ECGRoot"));
        Root->SetVisibility(ESlateVisibility::HitTestInvisible);BP->WidgetTree->RootWidget=Root;
        auto* View=BP->WidgetTree->ConstructWidget<UH5UI_View>(UH5UI_View::StaticClass(),TEXT("ECGView"));
        View->URL=TEXT("coui://uiresources/ECG/ecg.html");View->DefaultSize={320,96};View->bAutoLoad=false;View->bEnableBrowserSubviews=false;View->bEnableJavaScript=true;View->bReceiveInput=false;View->bConsumeInput=false;
        View->SetVisibility(ESlateVisibility::HitTestInvisible);auto* CanvasSlot=Root->AddChildToCanvas(View);CanvasSlot->SetAnchors(FAnchors(0,0,1,1));CanvasSlot->SetOffsets(FMargin(0));
        BP->ForEachSourceWidget([&](UWidget* W){BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewGuid());});
        FAssetRegistryModule::AssetCreated(BP);
    }
    if(!MainMapBP::Compile(BP))return 1;
    auto* Default=CastChecked<UECGWidget>(BP->GeneratedClass->GetDefaultObject());
    Default->DesignSizeMode=EDesignPreviewSizeMode::Custom;Default->DesignTimeSize={320,96};
    BP->MarkPackageDirty();FSavePackageArgs A;A.TopLevelFlags=RF_Public|RF_Standalone;
    const FString Filename=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
    if(!UPackage::SavePackage(BP->GetOutermost(),BP,*Filename,A))return 1;
    UE_LOG(LogTemp,Display,TEXT("ECG_ASSET_OK /Game/System/UIBasic/WBP_ECG: editable color, BPM, intensity; native H5UI Canvas"));return 0;
}
