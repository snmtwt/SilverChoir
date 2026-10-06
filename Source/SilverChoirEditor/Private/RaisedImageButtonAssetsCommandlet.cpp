#include "RaisedImageButtonAssetsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "EditorReimportHandler.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "UIBasic/RaisedImageButtonWidget.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

namespace RaisedImageButtonAssets
{
constexpr const TCHAR* ButtonPath = TEXT("/Game/System/UIBasic/WBP_RaisedImageButton");
constexpr const TCHAR* UpPath = TEXT("/Game/System/UIBasic/Textures/T_ArrowUp");
constexpr const TCHAR* DownPath = TEXT("/Game/System/UIBasic/Textures/T_ArrowDown");
constexpr const TCHAR* OwnerKey = TEXT("SilverChoirAssetGenerator");
constexpr const TCHAR* OwnerValue = TEXT("RaisedImageButtonAssets_v1");

struct FRun
{
    FString Report;
    TArray<UObject*> NewAssets;
    bool bOK = true;

    bool Check(bool bCondition, const FString& Detail)
    {
        Report += (bCondition ? TEXT("PASS ") : TEXT("FAIL ")) + Detail + TEXT("\n");
        if (!bCondition)
        {
            bOK = false;
            UE_LOG(LogTemp, Error, TEXT("RAISED_IMAGE_BUTTON %s"), *Detail);
        }
        return bCondition;
    }

    void Note(const FString& Detail)
    {
        Report += Detail + TEXT("\n");
        UE_LOG(LogTemp, Display, TEXT("RAISED_IMAGE_BUTTON %s"), *Detail);
    }

    void Claim(UObject* Asset)
    {
        Asset->GetOutermost()->GetMetaData().SetValue(Asset, OwnerKey, OwnerValue);
        NewAssets.Add(Asset);
        FAssetRegistryModule::AssetCreated(Asset);
    }
};

FString AssetFile(const TCHAR* PackagePath)
{
    return FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
}

template<class T> T* ReadExisting(FRun& Run, const TCHAR* Path, bool bRequired)
{
    if (!FPackageName::DoesPackageExist(Path))
    {
        if (bRequired) Run.Check(false, FString(TEXT("Missing saved asset: ")) + Path);
        return nullptr;
    }
    UObject* Asset = LoadObject<UObject>(nullptr, Path);
    T* Typed = Cast<T>(Asset);
    if (!Run.Check(Typed != nullptr, FString(TEXT("Asset class: ")) + Path)) return nullptr;
    if (!Run.Check(Asset->GetOutermost()->GetMetaData().GetValue(Asset, OwnerKey) == OwnerValue,
        FString(TEXT("Owned asset; existing foreign assets are never overwritten: ")) + Path)) return nullptr;
    Run.Note(FString(TEXT("Loaded existing owned asset: ")) + Path);
    return Typed;
}

bool HasVisibleTransparentArtwork(UTexture2D* Texture)
{
    if (!Texture || Texture->Source.GetFormat() != TSF_BGRA8) return false;
    TArray64<uint8> Pixels;
    if (!Texture->Source.GetMipData(Pixels, 0)) return false;
    bool bVisible = false;
    bool bTransparent = false;
    for (int64 Index = 3; Index < Pixels.Num(); Index += 4)
    {
        bVisible |= Pixels[Index] > 0;
        bTransparent |= Pixels[Index] == 0;
        if (bVisible && bTransparent) return true;
    }
    return false;
}

bool HasWhiteTintableArtwork(UTexture2D* Texture)
{
    if (!Texture || Texture->Source.GetFormat() != TSF_BGRA8) return false;
    TArray64<uint8> Pixels;
    if (!Texture->Source.GetMipData(Pixels, 0)) return false;
    bool bVisible = false;
    for (int64 Index = 0; Index + 3 < Pixels.Num(); Index += 4)
    {
        if (Pixels[Index + 3] == 0) continue;
        bVisible = true;
        // Coverage belongs in alpha. RGB must stay white even on anti-aliased edge pixels.
        if (Pixels[Index] != 255 || Pixels[Index + 1] != 255 || Pixels[Index + 2] != 255) return false;
    }
    return bVisible;
}

bool ValidateTexture(FRun& Run, UTexture2D* Texture, const TCHAR* Path, bool bRequireWhite = true)
{
    if (!Run.Check(Texture != nullptr, FString(TEXT("Texture exists: ")) + Path)) return false;
    Run.Note(FString::Printf(TEXT("Texture %s source=%dx%d"), Path, Texture->Source.GetSizeX(), Texture->Source.GetSizeY()));
    Run.Check(Texture->CompressionSettings == TC_EditorIcon && Texture->LODGroup == TEXTUREGROUP_UI
        && Texture->MipGenSettings == TMGS_NoMipmaps && Texture->SRGB && !Texture->CompressionNoAlpha
        && Texture->NeverStream && Texture->AddressX == TA_Clamp && Texture->AddressY == TA_Clamp,
        FString(TEXT("UI texture settings, alpha, no mips and clamp: ")) + Path);
    Run.Check(HasVisibleTransparentArtwork(Texture), FString(TEXT("Visible arrow on transparent background: ")) + Path);
    if (bRequireWhite)
        Run.Check(HasWhiteTintableArtwork(Texture), FString(TEXT("Every visible RGB pixel is pure white for widget tinting: ")) + Path);
    return Run.bOK;
}

UTexture2D* ImportTexture(FRun& Run, const FString& SourceFile, const TCHAR* Path)
{
    // All package conflicts and source paths have been checked before the first import.
    auto* Factory = NewObject<UTextureFactory>();
    Factory->SuppressImportOverwriteDialog(true);
    bool bCancelled = false;
    auto* Texture = Cast<UTexture2D>(Factory->FactoryCreateFile(UTexture2D::StaticClass(), CreatePackage(Path),
        *FPackageName::GetShortName(Path), RF_Public | RF_Standalone, SourceFile, nullptr, GWarn, bCancelled));
    if (!Run.Check(Texture && !bCancelled, TEXT("Import: ") + SourceFile)) return nullptr;
    Texture->CompressionSettings = TC_EditorIcon;
    Texture->CompressionNoAlpha = false;
    Texture->LODGroup = TEXTUREGROUP_UI;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->SRGB = true;
    Texture->NeverStream = true;
    Texture->Filter = TF_Bilinear;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->PostEditChange();
    Run.Claim(Texture);
    return Texture;
}

bool Compile(FRun& Run, UWidgetBlueprint* BP, bool bNew)
{
    if (bNew)
    {
        BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            if (!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
                BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
        });
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    }
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
    Run.Note(FString::Printf(TEXT("Blueprint %s compile: errors=%d warnings=%d (existing Blueprints are not saved)"),
        *BP->GetPathName(), Results.NumErrors, Results.NumWarnings));
    return Run.Check(Results.NumErrors == 0 && BP->Status != BS_Error && BP->GeneratedClass,
        TEXT("Widget Blueprint compiles: ") + BP->GetPathName());
}

UWidgetBlueprint* CreateButton(FRun& Run, UTexture2D* Up)
{
    auto* Factory = NewObject<UWidgetBlueprintFactory>();
    Factory->ParentClass = URaisedImageButtonWidget::StaticClass();
    auto* BP = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(), CreatePackage(ButtonPath),
        *FPackageName::GetShortName(ButtonPath), RF_Public | RF_Standalone, nullptr, GWarn));
    if (!Run.Check(BP && BP->WidgetTree, TEXT("Create raised image button Blueprint"))) return nullptr;
    Run.Claim(BP);
    BP->BlueprintDescription = TEXT("Reusable digital image button. Set IconBrush or SetButtonImage, IconSize and MinimumSize; input, audio, tooltip and states are inherited.");

    UWidgetTree* Tree = BP->WidgetTree;
    auto* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ButtonSize"));
    Size->SetMinDesiredWidth(44.f);
    Size->SetMinDesiredHeight(44.f);
    Tree->RootWidget = Size;
    auto* Face = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("FaceContent"));
    auto* FaceSlot = CastChecked<USizeBoxSlot>(Size->AddChild(Face));
    FaceSlot->SetHorizontalAlignment(HAlign_Fill);
    FaceSlot->SetVerticalAlignment(VAlign_Fill);
    auto* IconSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("IconSizeBox"));
    IconSize->SetWidthOverride(20.f);
    IconSize->SetHeightOverride(20.f);
    auto* IconSlot = Face->AddChildToOverlay(IconSize);
    IconSlot->SetHorizontalAlignment(HAlign_Center);
    IconSlot->SetVerticalAlignment(VAlign_Center);
    auto* Icon = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IconImage"));
    Icon->SetBrushFromTexture(Up);
    IconSize->AddChild(Icon);
    Tree->ForEachWidget([](UWidget* Widget)
    {
        Widget->bIsVariable = true;
        Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
    });

    if (!Compile(Run, BP, true)) return nullptr;
    auto* Defaults = Cast<URaisedImageButtonWidget>(BP->GeneratedClass->GetDefaultObject());
    if (!Run.Check(Defaults != nullptr, TEXT("Native raised image button defaults"))) return nullptr;
    Defaults->ContentMode = EBasicButtonContent::IconOnly;
    Defaults->ButtonText = FText::GetEmpty();
    Defaults->MinimumSize = FVector2D(44, 44);
    Defaults->IconSize = FVector2D(20, 20);
    Defaults->IconBrush.SetResourceObject(Up);
    Defaults->IconBrush.ImageSize = FVector2D(20, 20);
    Defaults->IconBrush.DrawAs = ESlateBrushDrawType::Image;
    Defaults->DesignSizeMode = EDesignPreviewSizeMode::Desired;
    Defaults->DesignTimeSize = FVector2D(44, 44);
    // Use this project's shared UI audio when present; the reusable native class remains asset-independent.
    constexpr const TCHAR* HoverPath = TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuHover_Electronic");
    constexpr const TCHAR* PressPath = TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuPress_Terminal");
    if (!Defaults->HoverSound && FPackageName::DoesPackageExist(HoverPath))
        Defaults->HoverSound = LoadObject<USoundBase>(nullptr, HoverPath);
    if (!Defaults->PressSound && FPackageName::DoesPackageExist(PressPath))
        Defaults->PressSound = LoadObject<USoundBase>(nullptr, PressPath);
    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    return Compile(Run, BP, false) ? BP : nullptr;
}

bool ValidateButton(FRun& Run, UWidgetBlueprint* BP, UTexture2D* Up, bool bRequireOriginalDefaults = false)
{
    if (!Run.Check(BP && BP->WidgetTree && BP->GeneratedClass
        && BP->ParentClass == URaisedImageButtonWidget::StaticClass(), TEXT("Blueprint parent, Designer tree and generated class"))) return false;
    UWidgetTree* Tree = BP->WidgetTree;
    auto* Size = Cast<USizeBox>(Tree->FindWidget(TEXT("ButtonSize")));
    auto* Face = Cast<UOverlay>(Tree->FindWidget(TEXT("FaceContent")));
    auto* IconSize = Cast<USizeBox>(Tree->FindWidget(TEXT("IconSizeBox")));
    auto* Icon = Cast<UImage>(Tree->FindWidget(TEXT("IconImage")));
    if (!Run.Check(Size && Face && IconSize && Icon && Tree->RootWidget == Size
        && Face->GetParent() == Size && IconSize->GetParent() == Face && Icon->GetParent() == IconSize,
        TEXT("Designer hierarchy: ButtonSize > FaceContent > IconSizeBox > IconImage"))) return false;
    int32 WidgetCount = 0;
    Tree->ForEachWidget([&](UWidget* Widget)
    {
        ++WidgetCount;
        Run.Check(Widget->GetVisibility() == ESlateVisibility::HitTestInvisible,
            TEXT("Content does not intercept shared button input: ") + Widget->GetName());
        Run.Check(BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()),
            TEXT("Designer variable GUID: ") + Widget->GetName());
    });
    if (bRequireOriginalDefaults) Run.Check(WidgetCount == 4, TEXT("Minimal editable four-widget tree"));
    const auto* IconSlot = Cast<UOverlaySlot>(IconSize->Slot);
    Run.Check(IconSlot && IconSlot->GetHorizontalAlignment() == HAlign_Center
        && IconSlot->GetVerticalAlignment() == VAlign_Center, TEXT("Image is centered in button face"));
    const auto* Defaults = Cast<URaisedImageButtonWidget>(BP->GeneratedClass->GetDefaultObject());
    if (!Run.Check(Defaults != nullptr, TEXT("Generated defaults inherit native button behavior"))) return false;
    Run.Check(Defaults->ContentMode == EBasicButtonContent::IconOnly, TEXT("Configurable image button content mode"));
    if (bRequireOriginalDefaults)
    {
        Run.Check(Up && Defaults->IconBrush.GetResourceObject() == Up, TEXT("New button previews the configurable up-arrow image"));
        Run.Check(Defaults->MinimumSize.Equals(FVector2D(44, 44)) && Defaults->IconSize.Equals(FVector2D(20, 20)),
            TEXT("New defaults: 44 x 44 button, 20 x 20 image"));
    }
    Run.Note(FString::Printf(TEXT("Preserved configured size=(%.2f, %.2f) image=(%.2f, %.2f) resource=%s"),
        Defaults->MinimumSize.X, Defaults->MinimumSize.Y, Defaults->IconSize.X, Defaults->IconSize.Y,
        *GetPathNameSafe(Defaults->IconBrush.GetResourceObject())));
    Run.Note(FString::Printf(TEXT("Inherited audio: hover=%s press=%s"),
        *GetPathNameSafe(Defaults->HoverSound), *GetPathNameSafe(Defaults->PressSound)));
    Run.Check(Defaults->PressSound != nullptr, TEXT("Inherited immediate press sound is configured"));
    return Run.bOK;
}

bool SaveNewAssets(FRun& Run)
{
    // Recheck every destination before saving any package. Existing authored assets are never saved.
    for (UObject* Asset : Run.NewAssets)
        Run.Check(!FPackageName::DoesPackageExist(Asset->GetOutermost()->GetName()),
            TEXT("New destination remains unused: ") + Asset->GetOutermost()->GetName());
    if (!Run.bOK) return false;
    for (UObject* Asset : Run.NewAssets)
    {
        const FString Filename = AssetFile(*Asset->GetOutermost()->GetName());
        if (!Run.Check(IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true), TEXT("Asset directory: ") + Filename)) return false;
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Asset->MarkPackageDirty();
        if (!Run.Check(UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args), TEXT("Saved new asset: ") + Filename)) return false;
    }
    return true;
}

bool CompileDirectConsumers(FRun& Run)
{
    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TSet<FName> Packages;
    for (const TCHAR* Path : { ButtonPath, UpPath, DownPath })
    {
        TArray<FName> Referencers;
        Registry.GetReferencers(FName(Path), Referencers);
        for (const FName Package : Referencers) Packages.Add(Package);
    }
    TArray<FName> SortedPackages = Packages.Array();
    SortedPackages.Sort(FNameLexicalLess());
    for (const FName Package : SortedPackages)
    {
        if (Package == FName(ButtonPath)) continue;
        TArray<FAssetData> Assets;
        Registry.GetAssetsByPackageName(Package, Assets);
        for (const FAssetData& Asset : Assets)
        {
            if (Asset.AssetClassPath != UWidgetBlueprint::StaticClass()->GetClassPathName()) continue;
            auto* Consumer = Cast<UWidgetBlueprint>(Asset.GetAsset());
            if (!Run.Check(Consumer != nullptr, TEXT("Load direct button/arrow consumer: ") + Asset.GetObjectPathString())) return false;
            if (!Compile(Run, Consumer, false)) return false;
        }
    }
    Run.Note(TEXT("Direct consumer Blueprint graphs, widget trees, layout and configurations were compiled without saving."));
    return Run.bOK;
}

bool BackupPackages(FRun& Run, const TArray<UObject*>& Assets, const FString& BackupDir)
{
    if (!Run.Check(IFileManager::Get().MakeDirectory(*BackupDir, true), TEXT("Create pre-upgrade backup: ") + BackupDir)) return false;
    for (UObject* Asset : Assets)
    {
        const FString Filename = AssetFile(*Asset->GetOutermost()->GetName());
        if (!Run.Check(FPaths::FileExists(Filename), TEXT("Existing package is on disk: ") + Filename)) return false;
        if (!Run.Check(!IFileManager::Get().IsReadOnly(*Filename), TEXT("Existing package is writable: ") + Filename)) return false;
        for (const TCHAR* Extension : { TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl") })
        {
            const FString Source = FPaths::ChangeExtension(Filename, Extension);
            if (!FPaths::FileExists(Source)) continue;
            const FString Destination = BackupDir / FPaths::GetCleanFilename(Source);
            if (!Run.Check(IFileManager::Get().Copy(*Destination, *Source, false) == COPY_OK,
                TEXT("Backup before any reimport/save: ") + Destination)) return false;
        }
    }
    return true;
}

bool ReimportWhiteArrow(FRun& Run, UTexture2D* Texture, const FString& Source, const TCHAR* Path)
{
    const FString ObjectPath = Texture->GetPathName();
    TMap<FName, FString> OriginalMetadata;
    if (const TMap<FName, FString>* Metadata = FMetaData::GetMapForObject(Texture)) OriginalMetadata = *Metadata;
    // The legacy texture reimport handler returns the original UObject, preserving settings/references.
    FReimportManager* Reimport = FReimportManager::Instance();
    if (!Run.Check(Reimport->CanReimport(Texture), TEXT("Existing texture has a registered reimport handler: ") + ObjectPath)) return false;
    Reimport->UpdateReimportPaths(Texture, { Source });
    if (!Run.Check(Reimport->Reimport(Texture, false, false, Source, nullptr, INDEX_NONE, false, true),
        TEXT("Reimport owned white arrow in place: ") + Source)) return false;
    Texture->GetOutermost()->GetMetaData().SetObjectValues(Texture, MoveTemp(OriginalMetadata));
    Run.Check(Texture->GetPathName() == ObjectPath && LoadObject<UTexture2D>(nullptr, Path) == Texture,
        TEXT("Texture UObject identity and all consumer references retained: ") + ObjectPath);
    Run.Check(Texture->GetOutermost()->GetMetaData().GetValue(Texture, OwnerKey) == OwnerValue,
        TEXT("Original asset ownership metadata retained: ") + ObjectPath);
    return ValidateTexture(Run, Texture, Path);
}

bool Upgrade(FRun& Run)
{
    auto* Up = ReadExisting<UTexture2D>(Run, UpPath, true);
    auto* Down = ReadExisting<UTexture2D>(Run, DownPath, true);
    auto* Button = ReadExisting<UWidgetBlueprint>(Run, ButtonPath, true);
    if (!Run.bOK) return false;
    // The old artwork may be colored. Everything else must be sound before it is replaced.
    ValidateTexture(Run, Up, UpPath, false);
    ValidateTexture(Run, Down, DownPath, false);
    if (!Compile(Run, Button, false) || !ValidateButton(Run, Button, Up) || !Run.bOK) return false;
    const FString UpSource = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("UI/UIBasic/Icons/ArrowUp.png"));
    const FString DownSource = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("UI/UIBasic/Icons/ArrowDown.png"));
    Run.Check(FPaths::FileExists(UpSource), TEXT("Replacement PNG exists: ") + UpSource);
    Run.Check(FPaths::FileExists(DownSource), TEXT("Replacement PNG exists: ") + DownSource);
    if (!Run.bOK) return false;
    const FString BackupDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("RaisedImageButton/Backups")
        / (TEXT("Digital_") + FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")) + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    if (!BackupPackages(Run, { Up, Down, Button }, BackupDir)) return false;
    if (!ReimportWhiteArrow(Run, Up, UpSource, UpPath) || !ReimportWhiteArrow(Run, Down, DownSource, DownPath)) return false;
    if (!CompileDirectConsumers(Run) || !ValidateButton(Run, Button, Up)) return false;
    for (UTexture2D* Texture : { Up, Down })
    {
        const FString Filename = AssetFile(*Texture->GetOutermost()->GetName());
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Texture->MarkPackageDirty();
        if (!Run.Check(UPackage::SavePackage(Texture->GetOutermost(), Texture, *Filename, Args), TEXT("Save upgraded owned texture: ") + Filename))
        {
            Run.Note(TEXT("Upgrade save failed. Pre-upgrade package copies remain available at: ") + BackupDir);
            return false;
        }
    }
    Run.Note(TEXT("Only the two owned arrow textures were saved. Existing Blueprint graphs, trees, instance overrides, layout, icon configuration and sounds were preserved."));
    Run.Note(TEXT("Pre-upgrade packages: ") + BackupDir);
    return Run.bOK;
}

bool Execute(FRun& Run, bool bApply)
{
    auto* Up = ReadExisting<UTexture2D>(Run, UpPath, !bApply);
    auto* Down = ReadExisting<UTexture2D>(Run, DownPath, !bApply);
    auto* Button = ReadExisting<UWidgetBlueprint>(Run, ButtonPath, !bApply);
    if (!Run.bOK) return false;
    if (Up) ValidateTexture(Run, Up, UpPath);
    if (Down) ValidateTexture(Run, Down, DownPath);
    if (Button && (!Compile(Run, Button, false) || !ValidateButton(Run, Button, Up))) return false;
    if (!Run.bOK) return false;
    if (!bApply) return CompileDirectConsumers(Run);

    const FString UpSource = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("UI/UIBasic/Icons/ArrowUp.png"));
    const FString DownSource = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("UI/UIBasic/Icons/ArrowDown.png"));
    if (!Up) Run.Check(FPaths::FileExists(UpSource), TEXT("Source PNG exists: ") + UpSource);
    if (!Down) Run.Check(FPaths::FileExists(DownSource), TEXT("Source PNG exists: ") + DownSource);
    if (!Run.bOK) return false;
    if (!Up) Up = ImportTexture(Run, UpSource, UpPath);
    if (!Down) Down = ImportTexture(Run, DownSource, DownPath);
    ValidateTexture(Run, Up, UpPath);
    ValidateTexture(Run, Down, DownPath);
    if (!Run.bOK) return false;
    const bool bNewButton = !Button;
    if (!Button) Button = CreateButton(Run, Up);
    return ValidateButton(Run, Button, Up, bNewButton) && SaveNewAssets(Run);
}
}

URaisedImageButtonAssetsCommandlet::URaisedImageButtonAssetsCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 URaisedImageButtonAssetsCommandlet::Main(const FString& Params)
{
    using namespace RaisedImageButtonAssets;
    const bool bApply = FParse::Param(*Params, TEXT("Apply"));
    const bool bValidate = FParse::Param(*Params, TEXT("Validate"));
    const bool bUpgrade = FParse::Param(*Params, TEXT("Upgrade"));
    if (int32(bApply) + int32(bValidate) + int32(bUpgrade) != 1)
    {
        UE_LOG(LogTemp, Error, TEXT("Use -run=RaisedImageButtonAssets with exactly one of -Apply, -Validate or -Upgrade. Validate never saves; Apply creates missing assets only; Upgrade backs up and reimports owned arrow textures only."));
        return 2;
    }
    FRun Run;
    Run.Note(bUpgrade ? TEXT("Mode: Upgrade (back up owned packages, reimport white arrows in place, preserve all Blueprint content)")
        : bApply ? TEXT("Mode: Apply (create missing assets; never overwrite existing assets)") : TEXT("Mode: Validate (load and compile saved assets; no asset saves)"));
    const bool bSucceeded = (bUpgrade ? Upgrade(Run) : Execute(Run, bApply)) && Run.bOK;
    Run.Note(bSucceeded ? TEXT("RESULT: PASS") : TEXT("RESULT: FAIL"));
    const FString ReportDir = FPaths::ProjectSavedDir() / TEXT("RaisedImageButton");
    IFileManager::Get().MakeDirectory(*ReportDir, true);
    const FString ReportFile = ReportDir / (bUpgrade ? TEXT("Upgrade.txt") : bApply ? TEXT("Apply.txt") : TEXT("Validate.txt"));
    if (!FFileHelper::SaveStringToFile(Run.Report, *ReportFile))
    {
        UE_LOG(LogTemp, Error, TEXT("RAISED_IMAGE_BUTTON could not write report: %s"), *ReportFile);
        return 1;
    }
    UE_LOG(LogTemp, Display, TEXT("RAISED_IMAGE_BUTTON_REPORT %s"), *ReportFile);
    return bSucceeded ? 0 : 1;
}
