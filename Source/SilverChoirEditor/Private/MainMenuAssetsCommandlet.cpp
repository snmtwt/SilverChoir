#include "MainMenuAssetsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Blueprint.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "H5UI_View.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Map/MainMenu/MainMenuGameMode.h"
#include "Map/MainMenu/MainMenuPlayerController.h"
#include "Map/MainMenu/MainMenuWidget.h"
#include "Styling/CoreStyle.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace MainMenuAuthoring
{
const FString Root = TEXT("/Game/System/Map/MainMenu/");

FLinearColor Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }

bool Save(UObject* Asset)
{
	UPackage* Package = Asset->GetOutermost();
	const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	if (FPaths::FileExists(Filename))
	{
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("MainMenuAssetBackups") /
			FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) / FPaths::GetCleanFilename(Filename);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
		if (IFileManager::Get().Copy(*Backup, *Filename) != COPY_OK) { return false; }
	}
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	Package->MarkPackageDirty();
	return UPackage::SavePackage(Package, Asset, *Filename, Args);
}

UWidgetBlueprint* NewWidget(const TCHAR* Name, UClass* Parent)
{
	UPackage* Package = CreatePackage(*(Root + Name));
	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = Parent;
	UWidgetBlueprint* BP = CastChecked<UWidgetBlueprint>(Factory->FactoryCreateNew(
		UWidgetBlueprint::StaticClass(), Package, FName(Name), RF_Public | RF_Standalone, nullptr, GWarn));
	FAssetRegistryModule::AssetCreated(BP);
	return BP;
}

UBlueprint* NewBlueprint(const TCHAR* Name, UClass* Parent)
{
	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(Parent, CreatePackage(*(Root + Name)),
		FName(Name), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	FAssetRegistryModule::AssetCreated(BP);
	return BP;
}

bool Compile(UBlueprint* BP)
{
	if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(BP))
	{
		// Designer normally maintains these when adding/removing controls. Keep
		// stable GUIDs for surviving widgets during explicit layout migrations.
		TSet<FName> Names;
		WidgetBP->ForEachSourceWidget([&Names](UWidget* Widget) { Names.Add(Widget->GetFName()); });
		for (UWidgetAnimation* Animation : WidgetBP->Animations)
		{
			if (Animation) { Names.Add(Animation->GetFName()); }
		}
		for (const FName Name : Names)
		{
			if (!WidgetBP->WidgetVariableNameToGuidMap.Contains(Name))
			{
				WidgetBP->WidgetVariableNameToGuidMap.Add(Name, FGuid::NewGuid());
			}
		}
		for (auto It = WidgetBP->WidgetVariableNameToGuidMap.CreateIterator(); It; ++It)
		{
			if (!Names.Contains(It.Key())) { It.RemoveCurrent(); }
		}
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	return BP->Status != BS_Error;
}

UTextBlock* Text(UWidgetTree* Tree, FName Name, const FText& Value, int32 Size, const TCHAR* Hex, bool bBold = false)
{
	UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Block->SetText(Value);
	Block->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), Size, bBold ? TEXT("Bold") : TEXT("Regular")));
	Block->SetColorAndOpacity(Color(Hex));
	Block->SetVisibility(ESlateVisibility::HitTestInvisible);
	return Block;
}

void Place(UCanvasPanel* Canvas, UWidget* Widget, FVector2D Position, FVector2D Size)
{
	UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Widget);
	Slot->SetPosition(Position);
	Slot->SetSize(Size);
}

void Fill(UOverlay* Overlay, UWidget* Widget)
{
	UOverlaySlot* Slot = Overlay->AddChildToOverlay(Widget);
	Slot->SetHorizontalAlignment(HAlign_Fill);
	Slot->SetVerticalAlignment(VAlign_Fill);
}

void BuildMenuButton(UWidgetBlueprint* BP)
{
	UWidgetTree* Tree = BP->WidgetTree;
	USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ButtonSize"));
	Size->SetVisibility(ESlateVisibility::HitTestInvisible);
	Tree->RootWidget = Size;
	UBorder* Padding = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ContentPadding"));
	Padding->SetBrushColor(FLinearColor::Transparent);
	Padding->SetPadding(FMargin(16, 6, 16, 6));
	Padding->SetVerticalAlignment(VAlign_Center);
	Size->AddChild(Padding);
	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ButtonContent"));
	Padding->AddChild(Row);
	USizeBox* IndexSize = Tree->ConstructWidget<USizeBox>();
	IndexSize->SetWidthOverride(30);
	IndexSize->AddChild(Text(Tree, TEXT("IndexLabel"), FText::FromString(TEXT("01")), 9, TEXT("428EBD")));
	Row->AddChildToHorizontalBox(IndexSize)->SetVerticalAlignment(VAlign_Center);
	USizeBox* IconSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("IconSizeBox"));
	IconSize->AddChild(Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IconImage")));
	Row->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
	UVerticalBox* Copy = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ButtonCopy"));
	UHorizontalBoxSlot* CopySlot = Row->AddChildToHorizontalBox(Copy);
	CopySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	CopySlot->SetVerticalAlignment(VAlign_Center);
	Copy->AddChildToVerticalBox(Text(Tree, TEXT("Label"), NSLOCTEXT("SilverChoirMenu", "ButtonPreview", "开始行动"), 14, TEXT("9CB4C5")));
	Copy->AddChildToVerticalBox(Text(Tree, TEXT("DetailLabel"), NSLOCTEXT("SilverChoirMenu", "ButtonSubtitle", "开启新战役"), 9, TEXT("537C98")))->SetPadding(FMargin(0, 3, 0, 0));
	Row->AddChildToHorizontalBox(Text(Tree, TEXT("ArrowLabel"), FText::FromString(TEXT(">")), 16, TEXT("428EBD")))->SetVerticalAlignment(VAlign_Center);
}

void BuildMenu(UWidgetBlueprint* BP, UClass* ButtonClass)
{
	UWidgetTree* Tree = BP->WidgetTree;
	UOverlay* RootOverlay = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("MenuRoot"));
	Tree->RootWidget = RootOverlay;
	UBorder* Backdrop = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
	Backdrop->SetBrushColor(Color(TEXT("020407")));
	Backdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
	Fill(RootOverlay, Backdrop);

	// Background fills the viewport independently of foreground UI density.
	UH5UI_View* Background = Tree->ConstructWidget<UH5UI_View>(UH5UI_View::StaticClass(), TEXT("BackgroundView"));
	Background->URL = TEXT("coui://uiresources/MainMenu/background.html");
	Background->bReceiveInput = false;
	Background->bConsumeInput = false;
	Background->bEnableJavaScript = false;
	Background->bEnableBrowserSubviews = false;
	Background->bAutoLoad = false;
	Background->SetVisibility(ESlateVisibility::HitTestInvisible);
	Fill(RootOverlay, Background);

	USafeZone* Safe = Tree->ConstructWidget<USafeZone>(USafeZone::StaticClass(), TEXT("ScreenSafeArea"));
	Fill(RootOverlay, Safe);
	UCanvasPanel* Canvas = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Foreground"));
	Safe->AddChild(Canvas);
	auto Anchor = [](UCanvasPanel* Parent, UWidget* Widget, FVector2D Point, FVector2D Align, FVector2D Size, FVector2D Offset = FVector2D::ZeroVector)
	{
		UCanvasPanelSlot* Slot = Parent->AddChildToCanvas(Widget);
		Slot->SetAnchors(FAnchors(Point.X, Point.Y));
		Slot->SetAlignment(Align);
		Slot->SetPosition(Offset);
		Slot->SetSize(Size);
	};
	auto AddAnchoredText = [&](FName Name, const FText& Value, FVector2D Point, FVector2D Align,
		FVector2D Size, int32 FontSize, const TCHAR* Hex, FVector2D Offset = FVector2D::ZeroVector)
	{
		UTextBlock* Block = Text(Tree, Name, Value, FontSize, Hex);
		Anchor(Canvas, Block, Point, Align, Size, Offset);
		return Block;
	};

	// The base adjusts only this frame width. The actual layout stays in Designer.
	UCanvasPanel* ContentFrame = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ContentFrame"));
	Anchor(Canvas, ContentFrame, {0.5, 0.5}, {0.5, 0.5}, {1920, 1080});
	auto AddContentText = [&](FName Name, const FText& Value, FVector2D Point, FVector2D Align,
		FVector2D Size, int32 FontSize, const TCHAR* Hex, FVector2D Offset = FVector2D::ZeroVector)
	{
		UTextBlock* Block = Text(Tree, Name, Value, FontSize, Hex);
		Anchor(ContentFrame, Block, Point, Align, Size, Offset);
		return Block;
	};
	// Content anchors supply responsive spacing; the panel keeps a compact size.
	UCanvasPanel* Composition = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MenuComposition"));
	Anchor(ContentFrame, Composition, {0.05, 0.5}, {0, 0.5}, {352, 240});
	auto AddText = [&](FName Name, const FText& Value, FVector2D Position, FVector2D Size, int32 FontSize, const TCHAR* Hex, bool Bold = false)
	{
		UTextBlock* Block = Text(Tree, Name, Value, FontSize, Hex, Bold);
		Place(Composition, Block, Position, Size);
		return Block;
	};
	auto Line = [&](FName Name, FVector2D Position, FVector2D Size, const TCHAR* Hex)
	{
		UBorder* Border = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		Border->SetBrushColor(Color(Hex));
		Border->SetVisibility(ESlateVisibility::HitTestInvisible);
		Place(Composition, Border, Position, Size);
	};

	AddAnchoredText(TEXT("BrandTitle"), FText::FromString(TEXT("S I L V E R   C H O I R")), {0.04, 0.032}, {0, 0.5}, {260, 22}, 10, TEXT("8DA9BD"));
	AddAnchoredText(TEXT("TerminalTitle"), FText::FromString(TEXT("SC // LOCAL TERMINAL")), {0.96, 0.032}, {1, 0.5}, {240, 22}, 9, TEXT("48667F"))->SetJustification(ETextJustify::Right);

	AddText(TEXT("Chapter"), FText::FromString(TEXT("SC / 01")), {0, 0}, {65, 20}, 9, TEXT("5791B5"));
	Line(TEXT("ChapterRule"), {72, 8}, {118, 1}, TEXT("173E59"));
	AddText(TEXT("Era"), NSLOCTEXT("SilverChoirMenu", "Era", "战术纪元 2049"), {210, 0}, {130, 20}, 9, TEXT("48667F"));
	AddText(TEXT("TitleSilver"), FText::FromString(TEXT("SILVER")), {-3, 35}, {340, 66}, 44, TEXT("DCE9F5"));
	AddText(TEXT("TitleChoir"), FText::FromString(TEXT("CHOIR")), {-3, 94}, {340, 66}, 44, TEXT("276F98"), true);
	AddText(TEXT("TitleChinese"), NSLOCTEXT("SilverChoirMenu", "TitleChinese", "银 色 唱 诗 班"), {0, 175}, {340, 28}, 14, TEXT("8AA6BB"));
	AddText(TEXT("Kicker"), NSLOCTEXT("SilverChoirMenu", "Kicker", "寂静之后，唯有回响"), {0, 215}, {340, 24}, 10, TEXT("48667F"));
	AddContentText(TEXT("MenuHeading"), NSLOCTEXT("SilverChoirMenu", "MainTerminal", "主终端"), {0.95, 0.5}, {1, 0.5}, {300, 22}, 10, TEXT("6997B5"), {0, -142});
	AddContentText(TEXT("MenuHint"), FText::FromString(TEXT("SELECT COMMAND")), {0.95, 0.5}, {1, 0.5}, {132, 20}, 8, TEXT("365970"), {0, -140})->SetJustification(ETextJustify::Right);
	UBorder* MenuRule = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuRule"));
	MenuRule->SetBrushColor(Color(TEXT("143047")));
	MenuRule->SetVisibility(ESlateVisibility::HitTestInvisible);
	Anchor(ContentFrame, MenuRule, {0.95, 0.5}, {1, 0.5}, {300, 1}, {0, -116});

	UVerticalBox* Menu = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MenuButtons"));
	Anchor(ContentFrame, Menu, {0.95, 0.5}, {1, 0.5}, {300, 224}, {0, 15});
	auto AddButton = [&](FName Name, const TCHAR* Index, const FText& Label)
	{
		UBasicButtonWidget* Button = Tree->ConstructWidget<UBasicButtonWidget>(ButtonClass, Name);
		Button->ButtonText = Label;
		Button->ButtonIndex = FText::FromString(Index);
		Button->ButtonSubtitle = FText::GetEmpty();
		Button->MinimumSize = {300, 48};
		Button->Font = FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), 14, TEXT("Regular"));
		Button->BackgroundColor = Color(TEXT("050D15"));
		Button->BorderColor = Color(TEXT("123149"));
		Menu->AddChildToVerticalBox(Button)->SetPadding(FMargin(0, 0, 0, 8));
	};
	AddButton(TEXT("NewGameButton"), TEXT("01"), NSLOCTEXT("SilverChoirMenu", "StartAction", "开始行动"));
	AddButton(TEXT("LoadGameButton"), TEXT("02"), NSLOCTEXT("SilverChoirMenu", "LoadAction", "继续任务"));
	AddButton(TEXT("SettingsButton"), TEXT("03"), NSLOCTEXT("SilverChoirMenu", "SettingsAction", "系统设置"));
	AddButton(TEXT("QuitButton"), TEXT("04"), NSLOCTEXT("SilverChoirMenu", "QuitAction", "断开连接"));

	// Ring labels are viewport anchored, independent of the menu column.
	AddContentText(TEXT("SignalCoreText"), FText::FromString(TEXT("SC")), {0.5, 0.5}, {0.5, 0.5}, {90, 35}, 20, TEXT("5184A4"))->SetJustification(ETextJustify::Center);
	AddContentText(TEXT("SignalCoreCaption"), FText::FromString(TEXT("S T A N D B Y")), {0.5, 0.5}, {0.5, 0}, {120, 20}, 7, TEXT("31536C"), {0, 21})->SetJustification(ETextJustify::Center);
	AddContentText(TEXT("SignalTelemetry"), FText::FromString(TEXT("CN-07  /  SIGNAL 98.6%")), {0.5, 0.78}, {0.5, 0.5}, {290, 22}, 9, TEXT("365970"))->SetJustification(ETextJustify::Center);
	AddAnchoredText(TEXT("FooterStatus"), NSLOCTEXT("SilverChoirMenu", "Awaiting", "等待指令"), {0.04, 0.969}, {0, 0.5}, {260, 22}, 9, TEXT("4C7F9D"));
	AddAnchoredText(TEXT("FooterVersion"), FText::FromString(TEXT("SC // 0.1    /    STRATEGIC COMMAND")), {0.96, 0.969}, {1, 0.5}, {340, 22}, 8, TEXT("365970"))->SetJustification(ETextJustify::Right);
}
}

UMainMenuAssetsCommandlet::UMainMenuAssetsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMainMenuAssetsCommandlet::Main(const FString& Params)
{
	using namespace MainMenuAuthoring;
	if (FParse::Param(*Params, TEXT("Redesign")))
	{
		UWidgetBlueprint* Button = LoadObject<UWidgetBlueprint>(nullptr, *(Root + TEXT("WBP_MenuButton")));
		UWidgetBlueprint* Menu = LoadObject<UWidgetBlueprint>(nullptr, *(Root + TEXT("WBP_MainMenu")));
		if (!Button || !Menu) { return 1; }
		auto ResetDesignerTree = [](UWidgetBlueprint* BP)
		{
			BP->WidgetTree->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			BP->WidgetTree = NewObject<UWidgetTree>(BP, TEXT("WidgetTree"), RF_Transactional);
		};
		ResetDesignerTree(Button);
		BuildMenuButton(Button);
		if (!Compile(Button) || !Save(Button)) { return 1; }
		ResetDesignerTree(Menu);
		BuildMenu(Menu, Button->GeneratedClass);
		if (!Compile(Menu) || !Save(Menu)) { return 1; }
		UE_LOG(LogTemp, Display, TEXT("Saved responsive fullscreen background and compact menu Designer layouts; previous assets backed up."));
		return 0;
	}
	if (FParse::Param(*Params, TEXT("FixFonts")))
	{
		UFont* AssetFont = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
		if (!AssetFont) { return 1; }
		for (const TCHAR* Name : {TEXT("WBP_MenuButton"), TEXT("WBP_MainMenu")})
		{
			UWidgetBlueprint* BP = LoadObject<UWidgetBlueprint>(nullptr, *(Root + Name));
			if (!BP) { return 1; }
			BP->WidgetTree->ForEachWidget([AssetFont](UWidget* Widget)
			{
				if (UTextBlock* Block = Cast<UTextBlock>(Widget))
				{
					const FSlateFontInfo Old = Block->GetFont();
					Block->SetFont(FSlateFontInfo(AssetFont, Old.Size, Old.TypefaceFontName));
				}
				if (UBasicButtonWidget* Button = Cast<UBasicButtonWidget>(Widget))
				{
					Button->Font = FSlateFontInfo(AssetFont, Button->Font.Size, TEXT("Regular"));
				}
			});
			if (!Compile(BP) || !Save(BP)) { return 1; }
		}
		return 0;
	}
	if (FParse::Param(*Params, TEXT("Verify")))
	{
		UWidgetBlueprint* Menu = LoadObject<UWidgetBlueprint>(nullptr, *(Root + TEXT("WBP_MainMenu")));
		UWidgetBlueprint* Button = LoadObject<UWidgetBlueprint>(nullptr, *(Root + TEXT("WBP_MenuButton")));
		UBlueprint* Controller = LoadObject<UBlueprint>(nullptr, *(Root + TEXT("BP_MainMenuPlayerController")));
		UBlueprint* GameMode = LoadObject<UBlueprint>(nullptr, *(Root + TEXT("BP_MainMenuGameMode")));
		if (!Menu || !Button || !Controller || !GameMode || !Compile(Button) || !Compile(Menu)) { return 1; }
		if (Menu->Status != BS_UpToDate || Button->Status != BS_UpToDate) { return 1; }
		for (const FName Name : {FName("BackgroundView"), FName("NewGameButton"), FName("LoadGameButton"), FName("SettingsButton"), FName("QuitButton")})
		{
			if (!Menu->WidgetTree->FindWidget(Name)) { return 1; }
		}
		if (Controller->GeneratedClass->GetDefaultObject<AMainMenuPlayerController>()->MainMenuWidgetClass != Menu->GeneratedClass) { return 1; }
		if (GameMode->GeneratedClass->GetDefaultObject<AMainMenuGameMode>()->PlayerControllerClass != Controller->GeneratedClass) { return 1; }
		UE_LOG(LogTemp, Display, TEXT("Verified saved Designer trees, required widget bindings, clean Blueprint compilation and GameMode -> Controller -> Widget class defaults."));
		return 0;
	}
	// Existing assets are intentionally not regenerated: subsequent layout editing belongs to Designer.
	for (const TCHAR* Name : {TEXT("WBP_MenuButton"), TEXT("WBP_MainMenu"), TEXT("BP_MainMenuPlayerController"), TEXT("BP_MainMenuGameMode")})
	{
		if (FPackageName::DoesPackageExist(Root + Name))
		{
			UE_LOG(LogTemp, Error, TEXT("Asset already exists; refusing to overwrite Designer work: %s%s"), *Root, Name);
			return 1;
		}
	}
	UWidgetBlueprint* ButtonBP = NewWidget(TEXT("WBP_MenuButton"), UBasicButtonWidget::StaticClass());
	BuildMenuButton(ButtonBP);
	if (!Compile(ButtonBP) || !Save(ButtonBP)) { return 1; }
	UWidgetBlueprint* MenuBP = NewWidget(TEXT("WBP_MainMenu"), UMainMenuWidget::StaticClass());
	BuildMenu(MenuBP, ButtonBP->GeneratedClass);
	if (!Compile(MenuBP) || !Save(MenuBP)) { return 1; }
	UBlueprint* ControllerBP = NewBlueprint(TEXT("BP_MainMenuPlayerController"), AMainMenuPlayerController::StaticClass());
	if (!Compile(ControllerBP)) { return 1; }
	ControllerBP->GeneratedClass->GetDefaultObject<AMainMenuPlayerController>()->MainMenuWidgetClass = MenuBP->GeneratedClass;
	if (!Save(ControllerBP)) { return 1; }
	UBlueprint* GameModeBP = NewBlueprint(TEXT("BP_MainMenuGameMode"), AMainMenuGameMode::StaticClass());
	if (!Compile(GameModeBP)) { return 1; }
	GameModeBP->GeneratedClass->GetDefaultObject<AMainMenuGameMode>()->PlayerControllerClass = ControllerBP->GeneratedClass;
	if (!Save(GameModeBP)) { return 1; }
	const FString MapPath = Root + TEXT("MainMenu");
	const FString MapFile = FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension());
	const FString Backup = FPaths::ProjectSavedDir() / TEXT("MainMenuAssetBackups") /
		FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) / TEXT("MainMenu.umap");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
	if (IFileManager::Get().Copy(*Backup, *MapFile) != COPY_OK) { return 1; }
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapFile);
	if (!World) { return 1; }
	World->GetWorldSettings()->DefaultGameMode = GameModeBP->GeneratedClass;
	if (!UEditorLoadingAndSavingUtils::SaveMap(World, MapPath)) { return 1; }
	UE_LOG(LogTemp, Display, TEXT("Saved editable menu Blueprints and assigned the menu map GameMode."));
	return 0;
}
