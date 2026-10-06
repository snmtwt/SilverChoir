#include "SquadVehicleUIAssetsCommandlet.h"

#include "Animation/WidgetAnimation.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Font.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/SquadMeetingRoomWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/Components/SquadVehicleEntryWidget.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "Sound/SoundBase.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

USquadVehicleUIAssetsCommandlet::USquadVehicleUIAssetsCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

namespace SquadVehicleUIAssets
{
constexpr const TCHAR* RoomPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/WBP_SquadMeetingRoom");
constexpr const TCHAR* EntryPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/Components/WBP_SquadVehicleEntry");
constexpr const TCHAR* ButtonPath = TEXT("/Game/System/UIBasic/WBP_ShellButton");
constexpr float EntryHeight = 286.f;

bool Check(bool bCondition, const FString& Detail)
{
    if (!bCondition) UE_LOG(LogTemp, Error, TEXT("SQUAD_VEHICLE_UI_CHECK_FAILED %s"), *Detail);
    return bCondition;
}

FLinearColor Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }

template<class T> T* Find(UWidgetBlueprint* BP, const TCHAR* Name)
{
    T* Widget = BP && BP->WidgetTree ? Cast<T>(BP->WidgetTree->FindWidget(Name)) : nullptr;
    Check(Widget != nullptr, FString::Printf(TEXT("%s: required widget missing or wrong type: %s"), *GetNameSafe(BP), Name));
    return Widget;
}

FString Placement(const UWidget* Widget)
{
    const auto* Slot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
    if (!Slot) return TEXT("None");
    const FAnchors Anchors = Slot->GetAnchors();
    const FMargin Offsets = Slot->GetOffsets();
    return FString::Printf(TEXT("%s|%g,%g,%g,%g|%g,%g,%g,%g|%s|%d|%d"),
        *GetNameSafe(Widget->GetParent()), Anchors.Minimum.X, Anchors.Minimum.Y, Anchors.Maximum.X, Anchors.Maximum.Y,
        Offsets.Left, Offsets.Top, Offsets.Right, Offsets.Bottom, *Slot->GetAlignment().ToString(), Slot->GetAutoSize(), Slot->GetZOrder());
}

FString GraphFingerprint(UBlueprint* BP)
{
    TArray<FString> Records;
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (const UEdGraph* Graph : Graphs)
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            const FString Prefix = Graph->GetName() + TEXT("|") + Node->NodeGuid.ToString() + TEXT("|");
            Records.Add(Prefix + Node->GetClass()->GetName() + TEXT("|") + Node->NodeComment);
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                const FString PinPrefix = Prefix + Pin->PinName.ToString() + TEXT("|") + FString::FromInt(int32(Pin->Direction)) + TEXT("|");
                Records.Add(PinPrefix + Pin->DefaultValue + TEXT("|") + GetPathNameSafe(Pin->DefaultObject));
                for (const UEdGraphPin* Other : Pin->LinkedTo)
                    Records.Add(PinPrefix + Other->GetOwningNode()->NodeGuid.ToString() + TEXT(".") + Other->PinName.ToString());
            }
        }
    Records.Sort();
    return FString::Join(Records, TEXT("\n"));
}

FString AnimationFingerprint(UWidgetBlueprint* BP)
{
    TArray<FString> Records;
    for (const UWidgetAnimation* Animation : BP->Animations)
    {
        if (!Animation) continue;
        Records.Add(FString::Printf(TEXT("%s|%g|%g|%s"), *Animation->GetName(), Animation->GetStartTime(), Animation->GetEndTime(), *GetPathNameSafe(Animation->MovieScene)));
        for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
            Records.Add(FString::Printf(TEXT("%s|%s|%s|%s|%d"), *Animation->GetName(), *Binding.WidgetName.ToString(),
                *Binding.SlotWidgetName.ToString(), *Binding.AnimationGuid.ToString(), Binding.bIsRootWidget));
    }
    Records.Sort();
    return FString::Join(Records, TEXT("\n"));
}

bool ValidateTree(UWidgetBlueprint* BP)
{
    if (!Check(BP && BP->WidgetTree && BP->GeneratedClass, TEXT("missing widget Blueprint, tree or generated class"))) return false;
    TSet<FName> Names;
    bool bUnique = true;
    BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (Names.Contains(Widget->GetFName())) bUnique = false;
        Names.Add(Widget->GetFName());
    });
    if (!Check(bUnique, TEXT("duplicate widget names: ") + BP->GetName())) return false;
    for (const UWidgetAnimation* Animation : BP->Animations)
    {
        if (!Check(Animation && Animation->MovieScene, TEXT("invalid widget animation"))) return false;
        for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
            if (!Binding.bIsRootWidget && !Check(Names.Contains(Binding.WidgetName), TEXT("missing animation target: ") + Binding.WidgetName.ToString())) return false;
    }
    return true;
}

bool Backup(UWidgetBlueprint* BP)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    if (!FPaths::FileExists(Filename)) return true;
    const FString BackupFile = FPaths::ProjectSavedDir() / TEXT("SquadVehicleUIBackups") /
        (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)) / FPaths::GetCleanFilename(Filename);
    if (!Check(IFileManager::Get().MakeDirectory(*FPaths::GetPath(BackupFile), true), TEXT("cannot create backup directory"))
        || !Check(IFileManager::Get().Copy(*BackupFile, *Filename) == COPY_OK, TEXT("cannot back up original asset"))) return false;
    UE_LOG(LogTemp, Display, TEXT("SQUAD_VEHICLE_UI_BACKUP %s"), *BackupFile);
    return true;
}

bool Compile(UWidgetBlueprint* BP)
{
    BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
            BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
    });
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FCompilerResultsLog Results;
    FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
    return Check(Results.NumErrors == 0 && BP->Status != BS_Error, TEXT("widget Blueprint did not compile: ") + BP->GetName());
}

bool Save(UWidgetBlueprint* BP)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    if (!Check(IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true), TEXT("cannot create asset directory"))) return false;
    BP->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return Check(UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args), TEXT("could not save: ") + BP->GetName());
}

struct FDesigner
{
    UWidgetTree* Tree;
    UClass* ButtonClass;

    template<class T> T* New(const TCHAR* Name) { return Tree->ConstructWidget<T>(T::StaticClass(), Name); }

    UCanvasPanel* Canvas(const TCHAR* Name)
    {
        auto* Result = New<UCanvasPanel>(Name);
        Result->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        return Result;
    }

    UCanvasPanelSlot* Place(UCanvasPanel* Parent, UWidget* Widget, FAnchors Anchors, FMargin Offsets, int32 Z = 0)
    {
        // Keep the existing slot object for existing children (animation bindings can reference it).
        UCanvasPanelSlot* Slot = Widget->GetParent() == Parent ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr;
        if (!Slot)
        {
            Widget->RemoveFromParent();
            Slot = Parent->AddChildToCanvas(Widget);
        }
        Slot->SetAnchors(Anchors);
        Slot->SetOffsets(Offsets);
        Slot->SetAlignment(FVector2D::ZeroVector);
        Slot->SetAutoSize(false);
        Slot->SetZOrder(Z);
        return Slot;
    }

    UImage* Plate(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Hex, FAnchors Anchors, FMargin Offsets, int32 Z = 0)
    {
        auto* Result = New<UImage>(Name);
        Result->SetColorAndOpacity(Color(Hex));
        Result->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(Parent, Result, Anchors, Offsets, Z);
        return Result;
    }

    UTextBlock* Text(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Caption, int32 FontSize, const TCHAR* Hex, FAnchors Anchors, FMargin Offsets)
    {
        auto* Result = New<UTextBlock>(Name);
        Result->SetText(FText::FromString(Caption));
        Result->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), FontSize, TEXT("Regular")));
        Result->SetColorAndOpacity(Color(Hex));
        Result->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(Parent, Result, Anchors, Offsets, 2);
        return Result;
    }

    UBasicButtonWidget* Button(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Caption, FAnchors Anchors, FMargin Offsets, int32 FontSize = 12)
    {
        auto* Result = Tree->ConstructWidget<UBasicButtonWidget>(ButtonClass, Name);
        Result->ButtonText = FText::FromString(Caption);
        Result->ButtonSubtitle = FText::GetEmpty();
        Result->ButtonIndex = FText::GetEmpty();
        Result->ContentMode = EBasicButtonContent::TextOnly;
        Result->MinimumSize = FVector2D::ZeroVector;
        Result->Font.Size = FontSize;
        Result->SetVisibility(ESlateVisibility::Visible);
        Place(Parent, Result, Anchors, Offsets, 2);
        return Result;
    }

    UImage* VehiclePicture(UCanvasPanel* Parent, const TCHAR* ScaleName, const TCHAR* ImageName, FMargin Offsets)
    {
        auto* Fit = New<UScaleBox>(ScaleName);
        Fit->SetStretch(EStretch::ScaleToFit);
        Fit->SetStretchDirection(EStretchDirection::Both);
        Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(Parent, Fit, FAnchors(0, 0, 1, 0), Offsets, 1);
        auto* Picture = New<UImage>(ImageName);
        FSlateBrush Brush;
        Brush.ImageSize = FVector2D(320, 180);
        Picture->SetBrush(Brush);
        Picture->SetColorAndOpacity(FLinearColor::White);
        Picture->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Slot = CastChecked<UScaleBoxSlot>(Fit->AddChild(Picture));
        // Fill stretches the arranged child after the scale calculation and distorts wide previews.
        Slot->SetHorizontalAlignment(HAlign_Center);
        Slot->SetVerticalAlignment(VAlign_Center);
        return Picture;
    }
};

bool ValidateEntry(UWidgetBlueprint* BP)
{
    return ValidateTree(BP)
        && Check(BP->ParentClass && BP->ParentClass->IsChildOf(USquadVehicleEntryWidget::StaticClass()), TEXT("wrong vehicle entry parent"))
        && Find<USizeBox>(BP, TEXT("VehicleEntrySize"))
        && Find<UImage>(BP, TEXT("VehicleImage"))
        && Find<UTextBlock>(BP, TEXT("VehicleNameText"))
        && Find<UTextBlock>(BP, TEXT("VehicleDetailsText"))
        && Find<UTextBlock>(BP, TEXT("VehicleStatusText"));
}

UWidgetBlueprint* CreateEntry()
{
    if (UWidgetBlueprint* Existing = LoadObject<UWidgetBlueprint>(nullptr, EntryPath))
        return ValidateEntry(Existing) ? Existing : nullptr; // Never rebuild an authored component.
    auto* Factory = NewObject<UWidgetBlueprintFactory>();
    Factory->ParentClass = USquadVehicleEntryWidget::StaticClass();
    auto* BP = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(), CreatePackage(EntryPath),
        *FPackageName::GetShortName(EntryPath), RF_Public | RF_Standalone, nullptr, GWarn));
    if (!Check(BP && BP->WidgetTree, TEXT("could not create vehicle entry Blueprint"))) return nullptr;
    FAssetRegistryModule::AssetCreated(BP);
    FDesigner D{BP->WidgetTree, nullptr};
    auto* Size = D.New<USizeBox>(TEXT("VehicleEntrySize"));
    Size->SetHeightOverride(EntryHeight);
    Size->SetVisibility(ESlateVisibility::HitTestInvisible);
    BP->WidgetTree->RootWidget = Size;
    auto* Layout = D.Canvas(TEXT("VehicleEntryLayout"));
    Layout->SetVisibility(ESlateVisibility::HitTestInvisible);
    Size->AddChild(Layout);
    // The inherited selection button draws the frame/hover. This tree contains only content.
    D.Plate(Layout, TEXT("VehiclePicturePlate"), TEXT("0B1B28"), FAnchors(0, 0, 1, 0), FMargin(10, 10, 10, 176));
    D.VehiclePicture(Layout, TEXT("VehiclePictureFit"), TEXT("VehicleImage"), FMargin(14, 14, 14, 168));
    auto* Name = D.Text(Layout, TEXT("VehicleNameText"), TEXT("车辆名称"), 15, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(14, 198, 14, 25));
    Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    auto* Details = D.Text(Layout, TEXT("VehicleDetailsText"), TEXT("载员 04 / 战略移动"), 11, TEXT("9CB8C8"), FAnchors(0, 0, 1, 0), FMargin(14, 232, 14, 21));
    Details->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    auto* Status = D.Text(Layout, TEXT("VehicleStatusText"), TEXT("点击选择"), 9, TEXT("45BDE7"), FAnchors(0, 0, 1, 0), FMargin(14, 258, 14, 18));
    Status->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
    if (!Compile(BP)) return nullptr;
    auto* Defaults = CastChecked<USquadVehicleEntryWidget>(BP->GeneratedClass->GetDefaultObject());
    Defaults->ContentMode = EBasicButtonContent::TextOnly;
    Defaults->ButtonText = FText::GetEmpty();
    Defaults->ButtonSubtitle = FText::GetEmpty();
    Defaults->ButtonIndex = FText::GetEmpty();
    Defaults->MinimumSize = FVector2D(0, EntryHeight);
    Defaults->bUseTabStyle = false;
    Defaults->BackgroundColor = Color(TEXT("06121C"));
    Defaults->BorderColor = Color(TEXT("234355"));
    Defaults->AccentColor = Color(TEXT("45BDE7"));
    Defaults->HoverSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuHover_Electronic"));
    Defaults->PressSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuPress_Terminal"));
    return ValidateEntry(BP) && Save(BP) ? BP : nullptr;
}

bool ValidateRoom(UWidgetBlueprint* BP, bool bPatched)
{
    if (!ValidateTree(BP)) return false;
    auto* Management = Find<UCanvasPanel>(BP, TEXT("ManagementPanel"));
    auto* Section = Find<UCanvasPanel>(BP, TEXT("VehicleSection"));
    auto* SectionSize = Find<USizeBox>(BP, TEXT("VehicleSectionSize"));
    auto* Sections = Find<UVerticalBox>(BP, TEXT("ManagementSections"));
    if (!Management || !Section || !SectionSize || !Sections
        || !Check(Section->GetParent() == SectionSize && SectionSize->GetParent() == Sections, TEXT("unexpected vehicle section hierarchy"))) return false;
    if (!bPatched) return true;
    auto* Picker = Find<UCanvasPanel>(BP, TEXT("VehiclePickerPanel"));
    auto* Options = Find<UScrollBox>(BP, TEXT("VehicleOptions"));
    auto* Close = Find<UBasicButtonWidget>(BP, TEXT("CloseVehiclePickerButton"));
    auto* Clear = Find<UBasicButtonWidget>(BP, TEXT("ClearVehicleButton"));
    auto* Image = Find<UImage>(BP, TEXT("VehiclePreviewImage"));
    auto* Empty = Find<UTextBlock>(BP, TEXT("VehicleEmptyText"));
    auto* Status = Find<UTextBlock>(BP, TEXT("VehiclePickerStatus"));
    auto* Defaults = BP->GeneratedClass ? Cast<USquadMeetingRoomWidget>(BP->GeneratedClass->GetDefaultObject()) : nullptr;
    return Picker && Options && Close && Clear && Image && Empty && Status && Defaults
        && Check(Picker->GetParent() == Management && Options->GetParent() == Picker && Close->GetParent() == Picker,
            TEXT("vehicle picker must remain inside the animated management panel"))
        && Check(Empty->GetParent() == Picker && Status->GetParent() == Picker && Clear->GetParent() == Section, TEXT("unexpected vehicle controls hierarchy"))
        && Check(Picker->GetVisibility() == ESlateVisibility::Collapsed, TEXT("vehicle picker must start collapsed"))
        && Check(Defaults->VehicleEntryClass && Defaults->VehicleEntryClass->IsChildOf(USquadVehicleEntryWidget::StaticClass()), TEXT("vehicle entry class is not configured"));
}

void Inspect(UWidgetBlueprint* BP)
{
    UE_LOG(LogTemp, Display, TEXT("SQUAD_VEHICLE_UI_ASSET %s status=%d"), *BP->GetPathName(), int32(BP->Status));
    BP->WidgetTree->ForEachWidget([](UWidget* Widget)
    {
        if (Widget->GetName().Contains(TEXT("Vehicle")))
            UE_LOG(LogTemp, Display, TEXT("SQUAD_VEHICLE_UI_WIDGET %s class=%s parent=%s visibility=%d placement=%s"),
                *Widget->GetName(), *Widget->GetClass()->GetName(), *GetNameSafe(Widget->GetParent()), int32(Widget->GetVisibility()), *Placement(Widget));
    });
}

bool Build(UWidgetBlueprint* BP)
{
    if (!ValidateRoom(BP, false)) return false;
    if (BP->WidgetTree->FindWidget(TEXT("VehiclePickerPanel")))
    {
        auto* Entry = LoadObject<UWidgetBlueprint>(nullptr, EntryPath);
        const bool bValid = ValidateRoom(BP, true) && Entry && ValidateEntry(Entry);
        UE_LOG(LogTemp, Display, TEXT("SQUAD_VEHICLE_UI_ALREADY_PATCHED preserving authored layout"));
        return bValid;
    }
    for (const TCHAR* Name : {TEXT("VehicleOptions"), TEXT("CloseVehiclePickerButton"), TEXT("ClearVehicleButton"), TEXT("VehiclePreviewImage"), TEXT("VehicleEmptyText"), TEXT("VehiclePickerStatus")})
        if (!Check(!BP->WidgetTree->FindWidget(Name), FString::Printf(TEXT("%s already exists; refusing a partial replacement"), Name))) return false;
    auto* Management = Find<UCanvasPanel>(BP, TEXT("ManagementPanel"));
    auto* Vehicle = Find<UCanvasPanel>(BP, TEXT("VehicleSection"));
    auto* VehicleSize = Find<USizeBox>(BP, TEXT("VehicleSectionSize"));
    auto* Label = Find<UTextBlock>(BP, TEXT("VehicleLabel"));
    auto* Description = Find<UTextBlock>(BP, TEXT("VehicleText"));
    auto* Choose = Find<UBasicButtonWidget>(BP, TEXT("ChooseVehicleButton"));
    auto* Hint = Find<UTextBlock>(BP, TEXT("VehicleHint"));
    auto* ButtonBP = LoadObject<UWidgetBlueprint>(nullptr, ButtonPath);
    if (!Management || !Vehicle || !VehicleSize || !Label || !Description || !Choose || !Hint
        || !Check(ButtonBP && ButtonBP->GeneratedClass && ButtonBP->GeneratedClass->IsChildOf(UBasicButtonWidget::StaticClass()), TEXT("shell button class unavailable"))) return false;
    if (!Check(Label->GetParent() == Vehicle && Description->GetParent() == Vehicle && Choose->GetParent() == Vehicle && Hint->GetParent() == Vehicle,
        TEXT("existing vehicle contents changed hierarchy; preserving authored content"))) return false;
    const FString GraphBefore = GraphFingerprint(BP);
    const FString AnimationsBefore = AnimationFingerprint(BP);
    TMap<FName, FString> PreservedPanels;
    for (const TCHAR* Name : {TEXT("LeftPanel"), TEXT("ManagementPanel"), TEXT("MemberStripPanel"), TEXT("ManagementScroll"), TEXT("SaveSquadButton"), TEXT("IconPickerPanel")})
    {
        UWidget* Panel = Find<UWidget>(BP, Name);
        if (!Panel) return false;
        PreservedPanels.Add(Name, Placement(Panel));
    }
    if (!Backup(BP)) return false;
    UWidgetBlueprint* Entry = CreateEntry();
    if (!Entry) return false;
    FDesigner D{BP->WidgetTree, ButtonBP->GeneratedClass.Get()};
    VehicleSize->SetHeightOverride(366);
    D.Place(Vehicle, Label, FAnchors(0, 0, 1, 0), FMargin(0, 20, 0, 28));
    D.Plate(Vehicle, TEXT("VehiclePreviewPlate"), TEXT("0B1B28"), FAnchors(0, 0, 1, 0), FMargin(0, 58, 0, 176));
    D.VehiclePicture(Vehicle, TEXT("VehiclePreviewFit"), TEXT("VehiclePreviewImage"), FMargin(8, 62, 8, 168));
    Description->SetAutoWrapText(true);
    D.Place(Vehicle, Description, FAnchors(0, 0, 1, 0), FMargin(0, 246, 0, 30));
    D.Place(Vehicle, Choose, FAnchors(0, 0, .66f, 0), FMargin(0, 292, 5, 38));
    Choose->ButtonText = FText::FromString(TEXT("选择车辆"));
    Choose->Font.Size = 12;
    D.Button(Vehicle, TEXT("ClearVehicleButton"), TEXT("取消车辆"), FAnchors(.66f, 0, 1, 0), FMargin(5, 292, 0, 38), 10);
    Hint->SetAutoWrapText(true);
    D.Place(Vehicle, Hint, FAnchors(0, 0, 1, 0), FMargin(0, 338, 0, 28));

    // Overlay the scrollable management body only. Base navigation and the save footer stay accessible.
    auto* Picker = D.Canvas(TEXT("VehiclePickerPanel"));
    D.Place(Management, Picker, FAnchors(0, 0, 1, 1), FMargin(12, 82, 12, 106), 50);
    auto* Background = D.Plate(Picker, TEXT("VehiclePickerBackground"), TEXT("071521FF"), FAnchors(0, 0, 1, 1), FMargin(0));
    Background->SetVisibility(ESlateVisibility::Visible);
    D.Plate(Picker, TEXT("VehiclePickerTopRule"), TEXT("45BDE7"), FAnchors(0, 0, 1, 0), FMargin(0, 0, 0, 2));
    D.Plate(Picker, TEXT("VehiclePickerBottomRule"), TEXT("285C75"), FAnchors(0, 1, 1, 1), FMargin(0, -1, 0, 1));
    D.Text(Picker, TEXT("VehiclePickerTitle"), TEXT("选择小队车辆"), 17, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(14, 16, 92, 28));
    D.Text(Picker, TEXT("VehiclePickerCaption"), TEXT("SQUAD VEHICLE / 可用车辆"), 9, TEXT("638EA5"), FAnchors(0, 0, 1, 0), FMargin(14, 46, 14, 18));
    D.Button(Picker, TEXT("CloseVehiclePickerButton"), TEXT("关闭"), FAnchors(1, 0), FMargin(-80, 16, 66, 28), 10);
    auto* Status = D.Text(Picker, TEXT("VehiclePickerStatus"), TEXT("选择车辆后调整载员，保存小队后生效"), 10, TEXT("9CB8C8"), FAnchors(0, 0, 1, 0), FMargin(14, 78, 14, 36));
    Status->SetAutoWrapText(true);
    D.Plate(Picker, TEXT("VehiclePickerDivider"), TEXT("234355"), FAnchors(0, 0, 1, 0), FMargin(14, 121, 14, 1));
    auto* Options = D.New<UScrollBox>(TEXT("VehicleOptions"));
    Options->SetScrollbarThickness(FVector2D(3));
    Options->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
    Options->SetClipping(EWidgetClipping::ClipToBounds);
    D.Place(Picker, Options, FAnchors(0, 0, 1, 1), FMargin(12, 136, 8, 14), 2);
    auto* Empty = D.Text(Picker, TEXT("VehicleEmptyText"), TEXT("暂无可用车辆"), 13, TEXT("7798AC"), FAnchors(0, .5f, 1, .5f), FMargin(20, -12, 20, 48));
    Empty->SetJustification(ETextJustify::Center);
    Empty->SetAutoWrapText(true);
    Picker->SetVisibility(ESlateVisibility::Collapsed);

    if (!Check(GraphBefore == GraphFingerprint(BP), TEXT("existing Blueprint graph changed"))
        || !Check(AnimationsBefore == AnimationFingerprint(BP), TEXT("existing animations changed")) || !Compile(BP)) return false;
    auto* Defaults = CastChecked<USquadMeetingRoomWidget>(BP->GeneratedClass->GetDefaultObject());
    Defaults->VehicleEntryClass = Entry->GeneratedClass.Get();
    for (const auto& Pair : PreservedPanels)
        if (!Check(Pair.Value == Placement(BP->WidgetTree->FindWidget(Pair.Key)), TEXT("changed protected panel placement: ") + Pair.Key.ToString())) return false;
    if (!Check(GraphBefore == GraphFingerprint(BP), TEXT("compiler changed existing Blueprint graph"))
        || !Check(AnimationsBefore == AnimationFingerprint(BP), TEXT("compiler changed existing animation bindings")) || !ValidateRoom(BP, true)) return false;
    if (!Save(BP)) return false;
    Inspect(BP);
    return true;
}

bool FixPreviewAspect(UWidgetBlueprint* Room)
{
    auto* Entry = LoadObject<UWidgetBlueprint>(nullptr, EntryPath);
    if (!ValidateRoom(Room, true) || !Entry || !ValidateEntry(Entry)) return false;

    struct FAspectPatch
    {
        UWidgetBlueprint* BP;
        const TCHAR* FitName;
        const TCHAR* ImageName;
        UScaleBoxSlot* Slot = nullptr;
        FString GraphBefore;
        FString AnimationsBefore;
        TMap<FName, FString> PlacementsBefore;
    };
    FAspectPatch Patches[] = {
        {Entry, TEXT("VehiclePictureFit"), TEXT("VehicleImage")},
        {Room, TEXT("VehiclePreviewFit"), TEXT("VehiclePreviewImage")}
    };
    // Validate and back up both assets before touching either designer tree.
    for (FAspectPatch& Patch : Patches)
    {
        auto* Fit = Find<UScaleBox>(Patch.BP, Patch.FitName);
        auto* Picture = Find<UImage>(Patch.BP, Patch.ImageName);
        Patch.Slot = Picture ? Cast<UScaleBoxSlot>(Picture->Slot) : nullptr;
        if (!Check(Fit && Picture && Patch.Slot && Picture->GetParent() == Fit && Fit->GetContent() == Picture,
            TEXT("unexpected preview hierarchy; preserving authored content: ") + Patch.BP->GetName())
            || !Check(Fit->GetStretch() == EStretch::ScaleToFit,
                TEXT("preview scale mode changed; preserving authored content: ") + Patch.BP->GetName())) return false;
        Patch.GraphBefore = GraphFingerprint(Patch.BP);
        Patch.AnimationsBefore = AnimationFingerprint(Patch.BP);
        Patch.BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            Patch.PlacementsBefore.Add(Widget->GetFName(), Placement(Widget));
        });
    }
    for (const FAspectPatch& Patch : Patches)
        if (!Backup(Patch.BP)) return false;
    for (FAspectPatch& Patch : Patches)
    {
        Patch.Slot->SetHorizontalAlignment(HAlign_Center);
        Patch.Slot->SetVerticalAlignment(VAlign_Center);
        if (!Compile(Patch.BP)
            || !Check(Patch.GraphBefore == GraphFingerprint(Patch.BP), TEXT("preview migration changed Blueprint graph"))
            || !Check(Patch.AnimationsBefore == AnimationFingerprint(Patch.BP), TEXT("preview migration changed animations"))) return false;
        for (const auto& Pair : Patch.PlacementsBefore)
            if (!Check(Pair.Value == Placement(Patch.BP->WidgetTree->FindWidget(Pair.Key)),
                TEXT("preview migration changed widget placement: ") + Pair.Key.ToString())) return false;
        auto* Picture = Find<UImage>(Patch.BP, Patch.ImageName);
        auto* Slot = Picture ? Cast<UScaleBoxSlot>(Picture->Slot) : nullptr;
        if (!Check(Slot && Slot->GetHorizontalAlignment() == HAlign_Center && Slot->GetVerticalAlignment() == VAlign_Center,
            TEXT("preview aspect alignment did not persist"))) return false;
    }
    if (!ValidateEntry(Entry) || !ValidateRoom(Room, true)) return false;
    for (const FAspectPatch& Patch : Patches)
        if (!Save(Patch.BP)) return false;
    return true;
}
}

int32 USquadVehicleUIAssetsCommandlet::Main(const FString& Params)
{
    auto* Room = LoadObject<UWidgetBlueprint>(nullptr, SquadVehicleUIAssets::RoomPath);
    if (!SquadVehicleUIAssets::Check(Room && Room->WidgetTree && Room->GeneratedClass, TEXT("squad room Blueprint unavailable"))) return 1;
    if (FParse::Param(*Params, TEXT("FixPreviewAspect")))
    {
        const bool bSuccess = SquadVehicleUIAssets::FixPreviewAspect(Room);
        UE_LOG(LogTemp, Display, TEXT("SQUAD_VEHICLE_UI_ASPECT_%s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));
        return bSuccess ? 0 : 1;
    }
    if (FParse::Param(*Params, TEXT("Inspect")))
    {
        SquadVehicleUIAssets::Inspect(Room);
        const bool bPatched = Room->WidgetTree->FindWidget(TEXT("VehiclePickerPanel")) != nullptr;
        bool bValid = SquadVehicleUIAssets::ValidateRoom(Room, bPatched);
        if (bPatched)
        {
            auto* Entry = LoadObject<UWidgetBlueprint>(nullptr, SquadVehicleUIAssets::EntryPath);
            bValid &= Entry && SquadVehicleUIAssets::ValidateEntry(Entry);
            if (Entry) SquadVehicleUIAssets::Inspect(Entry);
        }
        UE_LOG(LogTemp, Display, TEXT("SQUAD_VEHICLE_UI_INSPECT_%s patched=%d"), bValid ? TEXT("OK") : TEXT("FAILED"), bPatched);
        return bValid ? 0 : 1;
    }
    if (!FParse::Param(*Params, TEXT("Build")))
    {
        UE_LOG(LogTemp, Error, TEXT("Use -run=SquadVehicleUIAssets -Inspect, -Build or -FixPreviewAspect."));
        return 1;
    }
    const bool bSuccess = SquadVehicleUIAssets::Build(Room);
    UE_LOG(LogTemp, Display, TEXT("SQUAD_VEHICLE_UI_%s"), bSuccess ? TEXT("OK") : TEXT("FAILED"));
    return bSuccess ? 0 : 1;
}
