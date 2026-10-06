#include "MenuLoadingAssetsCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Engine/Font.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "UIBasic/MenuLoadingWidget.h"
#include "Map/MainMenu/MainMenuPlayerController.h"

UMenuLoadingAssetsCommandlet::UMenuLoadingAssetsCommandlet() { IsClient = false; IsEditor = true; LogToConsole = true; }

namespace
{
bool SaveLoadingAsset(UObject* Asset)
{
	UPackage* Package = Asset->GetOutermost();
	const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	if (FPaths::FileExists(File))
	{
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("MainMenuAssetBackups") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S-loading")) / FPaths::GetCleanFilename(File);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
		if (IFileManager::Get().Copy(*Backup, *File) != COPY_OK) { return false; }
	}
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Package->MarkPackageDirty();
	return UPackage::SavePackage(Package, Asset, *File, Args);
}
}

int32 UMenuLoadingAssetsCommandlet::Main(const FString& Params)
{
	const TCHAR* Path = TEXT("/Game/System/UIBasic/WBP_MenuLoading");
	auto* BP = LoadObject<UWidgetBlueprint>(nullptr, Path);
	if (!BP)
	{
		auto* Menu = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/System/Map/MainMenu/UI/WBP_MainMenu"));
		if (!Menu || !Menu->WidgetTree) { return 1; }
		auto* Factory = NewObject<UWidgetBlueprintFactory>();
		Factory->ParentClass = UMenuLoadingWidget::StaticClass();
		BP = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(), CreatePackage(Path), TEXT("WBP_MenuLoading"), RF_Public | RF_Standalone, nullptr, GWarn));
		if (!BP) { return 1; }
		// Copy current Designer artwork, including exact font materials/colors/anchors.
		BP->WidgetTree = DuplicateObject<UWidgetTree>(Menu->WidgetTree, BP, TEXT("LoadingWidgetTree"));
		for (const TCHAR* Name : {TEXT("MenuButtons"), TEXT("MenuHeading"), TEXT("MenuHint"), TEXT("MenuRule")})
		{
			if (UWidget* Widget = BP->WidgetTree->FindWidget(FName(Name))) { Widget->RemoveFromParent(); }
		}
		auto* Frame = Cast<UCanvasPanel>(BP->WidgetTree->FindWidget(TEXT("ContentFrame")));
		if (!Frame) { return 1; }
		auto* Panel = BP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("LoadingPanel"));
		auto* PanelSlot = Frame->AddChildToCanvas(Panel);
		PanelSlot->SetAnchors(FAnchors(.9f, .5f));
		PanelSlot->SetAlignment(FVector2D(1, .5));
		PanelSlot->SetPosition(FVector2D(-34, 0));
		PanelSlot->SetSize(FVector2D(300, 200));
		auto Text = [&](const TCHAR* Name, const TCHAR* Value, int32 Size, const TCHAR* Hex, FVector2D Position, FVector2D Extent)
		{
			auto* Block = BP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
			Block->SetText(FText::FromString(Value));
			Block->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), Size, TEXT("Regular")));
			Block->SetColorAndOpacity(FLinearColor(FColor::FromHex(Hex)));
			Block->SetVisibility(ESlateVisibility::HitTestInvisible);
			auto* TextSlot = Panel->AddChildToCanvas(Block);
			TextSlot->SetPosition(Position); TextSlot->SetSize(Extent);
			return Block;
		};
		Text(TEXT("LoadingEyebrow"), TEXT("SC / DEPLOYMENT SEQUENCE"), 9, TEXT("537C98"), {0, 0}, {300, 22});
		Text(TEXT("LoadingTitle"), TEXT("正在部署"), 20, TEXT("DCE9F5"), {0, 31}, {300, 38});
		Text(TEXT("LoadingPercent"), TEXT("00%"), 34, TEXT("276F98"), {0, 83}, {300, 58});
		Text(TEXT("LoadingStatus"), TEXT("建立地图连接…"), 11, TEXT("9CB4C5"), {0, 169}, {300, 30})->SetAutoWrapText(true);
		auto* Progress = BP->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("LoadingProgress"));
		Progress->SetFillColorAndOpacity(FLinearColor(FColor::FromHex(TEXT("329ACA"))));
		auto* BarSlot = Panel->AddChildToCanvas(Progress);
		BarSlot->SetPosition({0, 150}); BarSlot->SetSize({300, 3});
		BP->WidgetTree->ForEachWidget([&](UWidget* Widget) { BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid()); });
		FAssetRegistryModule::AssetCreated(BP);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (BP->Status == BS_Error || !SaveLoadingAsset(BP)) { return 1; }
	}
	BP->WidgetVariableNameToGuidMap.Reset();
	BP->WidgetTree->ForEachWidget([&](UWidget* Widget) { BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid()); });
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	if (BP->Status == BS_Error || !SaveLoadingAsset(BP)) { return 1; }
	if (!BP->GeneratedClass || !BP->GeneratedClass->IsChildOf(UMenuLoadingWidget::StaticClass())) { return 1; }
	auto* ControllerBP = LoadObject<UBlueprint>(nullptr, TEXT("/Game/System/Map/MainMenu/BP_MainMenuPlayerController"));
	if (!ControllerBP || !ControllerBP->GeneratedClass) { return 1; }
	auto* Defaults = Cast<AMainMenuPlayerController>(ControllerBP->GeneratedClass->GetDefaultObject());
	if (!Defaults) { return 1; }
	Defaults->LoadingWidgetClass = BP->GeneratedClass;
	Defaults->NewGameMap = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/System/Map/GameMainMap/GameMainMap.GameMainMap")));
	if (!SaveLoadingAsset(ControllerBP)) { return 1; }
	UE_LOG(LogTemp, Display, TEXT("MENU_LOADING_ASSETS_OK"));
	return 0;
}
