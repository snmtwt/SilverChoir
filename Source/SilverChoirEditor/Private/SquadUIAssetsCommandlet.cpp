#include "SquadUIAssetsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/WrapBox.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Font.h"
#include "HAL/FileManager.h"
#include "K2Node_CreateDelegate.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelListEntryWidget.h"
#include "Map/BaseMap/SceneUI/PersonnelPreparationRoom/Components/PersonnelPortraitWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/SquadMeetingRoomWidget.h"
#include "Map/BaseMap/SceneUI/SquadMeetingRoom/Components/SquadMemberCardWidget.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sound/SoundBase.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UIBasic/BasicButtonWidget.h"
#include "UIBasic/SelectionButtonWidget.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

USquadUIAssetsCommandlet::USquadUIAssetsCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

namespace SquadUIAssets
{
constexpr const TCHAR* RoomPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/WBP_SquadMeetingRoom");
constexpr const TCHAR* RowPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/Components/WBP_SquadListEntry");
constexpr const TCHAR* MemberPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/Components/WBP_SquadMemberCard");
constexpr const TCHAR* IconOptionPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/Components/WBP_SquadIconOption");
constexpr const TCHAR* ScenePath = TEXT("/Game/System/Map/BaseMap/Scene/BP_小队会议室_Scene");
constexpr const TCHAR* ShellPath = TEXT("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget");
constexpr const TCHAR* PersonnelPath = TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/WBP_PersonnelPreparationRoom");

FLinearColor Color(const TCHAR* Hex) { return FLinearColor(FColor::FromHex(Hex)); }

UWidgetBlueprint* FindOrCreate(const TCHAR* Path, UClass* Parent)
{
    if (auto* Existing = LoadObject<UWidgetBlueprint>(nullptr, Path))
    {
        if (!Existing->ParentClass || !Existing->ParentClass->IsChildOf(Parent))
        {
            UE_LOG(LogTemp, Error, TEXT("Preserving %s: unexpected parent %s."), Path, *GetNameSafe(Existing->ParentClass));
            return nullptr;
        }
        return Existing;
    }
    auto* Factory = NewObject<UWidgetBlueprintFactory>();
    Factory->ParentClass = Parent;
    auto* BP = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(),
        CreatePackage(Path), *FPackageName::GetShortName(Path), RF_Public | RF_Standalone, nullptr, GWarn));
    if (BP) FAssetRegistryModule::AssetCreated(BP);
    return BP;
}

bool Save(UWidgetBlueprint* BP)
{
    if (!BP || !BP->WidgetTree) return false;
    BP->WidgetTree->ForEachWidget([&](UWidget* Widget)
    {
        if (!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
            BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(), FGuid::NewGuid());
    });
    for (UWidgetAnimation* Animation : BP->Animations)
        if (Animation && !BP->WidgetVariableNameToGuidMap.Contains(Animation->GetFName()))
            BP->WidgetVariableNameToGuidMap.Add(Animation->GetFName(), FGuid::NewGuid());
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status == BS_Error) return false;
    const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    if (FPaths::FileExists(Filename))
    {
        const FString Backup = FPaths::ProjectSavedDir() / TEXT("SquadUIBackups") /
            FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) / FPaths::GetCleanFilename(Filename);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
        // A second save in the same pass must not overwrite the original on-disk backup.
        if (!FPaths::FileExists(Backup) && IFileManager::Get().Copy(*Backup, *Filename) != COPY_OK) return false;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    BP->MarkPackageDirty();
    return UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args);
}

void InspectAnimations(UWidgetBlueprint* BP)
{
    if (!BP) return;
    for (const UWidgetAnimation* Animation : BP->Animations)
    {
        if (!Animation || !Animation->MovieScene) continue;
        const UMovieScene* MovieScene = Animation->MovieScene;
        const FFrameRate Rate = MovieScene->GetTickResolution();
        UE_LOG(LogTemp, Display, TEXT("SQUAD_ANIMATION asset=%s name=%s start=%g end=%g duration=%g bindings=%d"),
            *BP->GetPathName(), *Animation->GetName(), Animation->GetStartTime(), Animation->GetEndTime(),
            Animation->GetEndTime() - Animation->GetStartTime(), Animation->AnimationBindings.Num());
        for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
            UE_LOG(LogTemp, Display, TEXT("SQUAD_ANIMATION_BINDING widget=%s slot=%s guid=%s root=%d"),
                *Binding.WidgetName.ToString(), *Binding.SlotWidgetName.ToString(), *Binding.AnimationGuid.ToString(), Binding.bIsRootWidget);
        for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
            for (const UMovieSceneTrack* Track : Binding.GetTracks())
            {
                if (!Track) continue;
                const auto* Property = Cast<UMovieScenePropertyTrack>(Track);
                UE_LOG(LogTemp, Display, TEXT("SQUAD_ANIMATION_TRACK guid=%s class=%s property=%s sections=%d"),
                    *Binding.GetObjectGuid().ToString(), *Track->GetClass()->GetName(),
                    Property ? *Property->GetPropertyPath().ToString() : TEXT(""), Track->GetAllSections().Num());
                for (const UMovieSceneSection* Section : Track->GetAllSections())
                {
                    if (!Section) continue;
                    auto LogAnimationChannel = [&Rate](const TCHAR* Name, const FMovieSceneFloatChannel& Channel)
                    {
                        const auto Data = Channel.GetData();
                        const auto Times = Data.GetTimes();
                        const auto Values = Data.GetValues();
                        for (int32 Index = 0; Index < Times.Num(); ++Index)
                            UE_LOG(LogTemp, Display, TEXT("SQUAD_ANIMATION_KEY channel=%s time=%g value=%g interpolation=%d"),
                                Name, Rate.AsSeconds(Times[Index]), Values[Index].Value, int32(Values[Index].InterpMode));
                    };
                    UE_LOG(LogTemp, Display, TEXT("SQUAD_ANIMATION_SECTION class=%s completion=%d"),
                        *Section->GetClass()->GetName(), int32(Section->GetCompletionMode()));
                    if (const auto* Transform = Cast<UMovieScene2DTransformSection>(Section))
                    {
                        LogAnimationChannel(TEXT("Translation.X"), Transform->Translation[0]);
                        LogAnimationChannel(TEXT("Translation.Y"), Transform->Translation[1]);
                    }
                    else if (const auto* FloatSection = Cast<UMovieSceneFloatSection>(Section))
                        LogAnimationChannel(TEXT("Float"), FloatSection->GetChannel());
                }
            }
    }
}

void Inspect(UBlueprint* BP, bool bInspectDelegateBindings = false)
{
    if (!BP) return;
    UE_LOG(LogTemp, Display, TEXT("SQUAD_ASSET %s parent=%s"), *BP->GetPathName(), *GetNameSafe(BP->ParentClass));
    if (BP->GeneratedClass)
        for (const FBPVariableDescription& Variable : BP->NewVariables)
            if (FProperty* Property = BP->GeneratedClass->FindPropertyByName(Variable.VarName))
            {
                FString Value;
                Property->ExportText_InContainer(0, Value, BP->GeneratedClass->GetDefaultObject(), nullptr, nullptr, PPF_None);
                UE_LOG(LogTemp, Display, TEXT("SCENE_VARIABLE %s type=%s value=%s"), *Variable.VarName.ToString(), *Property->GetCPPType(), *Value);
            }
    if (auto* WidgetBP = Cast<UWidgetBlueprint>(BP); WidgetBP && WidgetBP->WidgetTree)
    {
        WidgetBP->WidgetTree->ForEachWidget([](UWidget* W)
        {
            UE_LOG(LogTemp, Display, TEXT("SQUAD_WIDGET %s %s parent=%s"), *W->GetName(), *W->GetClass()->GetName(), *GetNameSafe(W->GetParent()));
        });
        InspectAnimations(WidgetBP);
    }
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UE_LOG(LogTemp, Display, TEXT("SQUAD_NODE [%s] %s %s"), *Graph->GetName(), *Node->GetName(), *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            if (bInspectDelegateBindings)
                if (const auto* Delegate = Cast<UK2Node_CreateDelegate>(Node))
                {
                    const FName FunctionName = Delegate->GetFunctionName();
                    const UClass* Scope = Delegate->GetScopeClass(true);
                    const UFunction* Function = Scope ? Scope->FindFunctionByName(FunctionName) : nullptr;
                    UE_LOG(LogTemp, Display, TEXT("SQUAD_DELEGATE [%s] node=%s function=%s scope=%s resolved=%s signature=%s"),
                        *Graph->GetName(), *Node->GetName(), *FunctionName.ToString(), *GetPathNameSafe(Scope),
                        *GetPathNameSafe(Function), *GetPathNameSafe(Delegate->GetDelegateSignature()));
                }
            for (UEdGraphPin* Pin : Node->Pins)
            {
                FString Links;
                for (UEdGraphPin* Linked : Pin->LinkedTo) Links += Linked->GetOwningNode()->GetName() + TEXT(".") + Linked->PinName.ToString() + TEXT(" ");
                UE_LOG(LogTemp, Display, TEXT("SQUAD_PIN %s default=%s object=%s links=%s"), *Pin->PinName.ToString(), *Pin->DefaultValue, *GetNameSafe(Pin->DefaultObject), *Links);
            }
        }
}

struct FPanelAnimationSpec
{
    const TCHAR* Name;
    const TCHAR* Widget;
    float Offset;
    bool bVertical;
    bool bEntering;
    float Delay;
};

const FPanelAnimationSpec PanelAnimations[] = {
    {TEXT("小队列表滑入"), TEXT("LeftPanel"), -72.f, false, true, 0.f},
    {TEXT("小队列表滑出"), TEXT("LeftPanel"), -72.f, false, false, 0.f},
    {TEXT("人员列表滑入"), TEXT("LeftPanel"), -72.f, false, true, 0.f},
    {TEXT("人员列表滑出"), TEXT("LeftPanel"), -72.f, false, false, 0.f},
    {TEXT("管理面板滑入"), TEXT("ManagementPanel"), 80.f, false, true, .04f},
    {TEXT("管理面板滑出"), TEXT("ManagementPanel"), 80.f, false, false, 0.f},
    {TEXT("成员名单滑入"), TEXT("MemberStripPanel"), 40.f, true, true, .08f},
    {TEXT("成员名单滑出"), TEXT("MemberStripPanel"), 40.f, true, false, 0.f}
};

UWidgetAnimation* FindAnimation(UWidgetBlueprint* BP, FName Name)
{
    for (UWidgetAnimation* Animation : BP->Animations)
        if (Animation && Animation->GetFName() == Name) return Animation;
    return nullptr;
}

bool ValidateAnimation(UWidgetBlueprint* BP, UWidgetAnimation* Animation, const FPanelAnimationSpec& Spec)
{
    if (!Animation || !Animation->MovieScene || Animation->GetEndTime() <= Animation->GetStartTime()) return false;
    bool bExpectedTarget = false;
    for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
    {
        UWidget* Widget = BP->WidgetTree->FindWidget(Binding.WidgetName);
        if ((!Binding.bIsRootWidget && !Widget) || !Animation->MovieScene->FindBinding(Binding.AnimationGuid)) return false;
        if (!Binding.SlotWidgetName.IsNone() && (!Widget || !Widget->Slot)) return false;
        if (Binding.WidgetName == Spec.Widget && !Binding.bIsRootWidget && Binding.SlotWidgetName.IsNone()) bExpectedTarget = true;
    }
    const UMovieScene* MovieScene = Animation->MovieScene;
    for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
    {
        if (!Animation->AnimationBindings.ContainsByPredicate([&Binding](const FWidgetAnimationBinding& WidgetBinding)
            { return WidgetBinding.AnimationGuid == Binding.GetObjectGuid(); })) return false;
        if (Binding.GetTracks().IsEmpty()) return false;
        for (const UMovieSceneTrack* Track : Binding.GetTracks())
        {
            if (!Track || Track->GetAllSections().IsEmpty()) return false;
            for (const UMovieSceneSection* Section : Track->GetAllSections()) if (!Section) return false;
        }
    }
    return bExpectedTarget;
}

void TweenChannel(FMovieSceneFloatChannel& Channel, float StartValue, float EndValue,
    FFrameNumber Start, FFrameNumber End, bool bEntering)
{
    Channel.SetDefault(StartValue);
    // Cubic Hermite tangents reproduce ease-out/ease-in without overshooting.
    // A constant leading segment keeps delayed panels invisible at their start offset.
    if (Start.Value > 0) Channel.AddConstantKey(FFrameNumber(0), StartValue);
    const float Slope = 3.f * (EndValue - StartValue) / float(End.Value - Start.Value);
    FMovieSceneTangentData FirstTangent, LastTangent;
    FirstTangent.ArriveTangent = 0.f;
    FirstTangent.LeaveTangent = bEntering ? Slope : 0.f;
    LastTangent.ArriveTangent = bEntering ? 0.f : Slope;
    LastTangent.LeaveTangent = LastTangent.ArriveTangent;
    Channel.AddCubicKey(Start, StartValue, RCTM_Break, FirstTangent);
    Channel.AddCubicKey(End, EndValue, RCTM_Break, LastTangent);
}

UWidgetAnimation* AuthorAnimation(UWidgetBlueprint* BP, const FPanelAnimationSpec& Spec)
{
    auto* Widget = BP->WidgetTree->FindWidget(Spec.Widget);
    if (!Widget) return nullptr;
    auto* Animation = NewObject<UWidgetAnimation>(BP, Spec.Name, RF_Transactional);
    Animation->SetDisplayLabel(Spec.Name);
    auto* MovieScene = NewObject<UMovieScene>(Animation, Spec.Name, RF_Transactional);
    Animation->MovieScene = MovieScene;
    const FFrameRate Rate(60000, 1);
    MovieScene->SetTickResolutionDirectly(Rate);
    MovieScene->SetDisplayRate(FFrameRate(60, 1));
    const float Duration = Spec.bEntering ? .30f : .22f;
    const FFrameNumber Start = Rate.AsFrameTime(Spec.Delay).RoundToFrame();
    const FFrameNumber End = Rate.AsFrameTime(Spec.Delay + Duration).RoundToFrame();
    const TRange<FFrameNumber> Range(FFrameNumber(0), End + 1);
    MovieScene->SetPlaybackRange(Range);
    MovieScene->GetEditorData().WorkStart = 0;
    MovieScene->GetEditorData().WorkEnd = Spec.Delay + Duration;

    const FGuid Guid = MovieScene->AddPossessable(Widget->GetName(), Widget->GetClass());
    FWidgetAnimationBinding Binding;
    Binding.WidgetName = Widget->GetFName();
    Binding.AnimationGuid = Guid;
    Animation->AnimationBindings.Add(Binding);

    auto* TransformTrack = MovieScene->AddTrack<UMovieScene2DTransformTrack>(Guid);
    TransformTrack->SetPropertyNameAndPath(TEXT("RenderTransform"), TEXT("RenderTransform"));
    auto* TransformSection = CastChecked<UMovieScene2DTransformSection>(TransformTrack->CreateNewSection());
    TransformSection->SetRange(Range);
    TransformSection->SetCompletionMode(EMovieSceneCompletionMode::KeepState);
    TransformSection->SetMask(FMovieScene2DTransformMask(Spec.bVertical
        ? EMovieScene2DTransformChannel::TranslationY : EMovieScene2DTransformChannel::TranslationX));
    TweenChannel(TransformSection->Translation[Spec.bVertical ? 1 : 0], Spec.bEntering ? Spec.Offset : 0.f,
        Spec.bEntering ? 0.f : Spec.Offset, Start, End, Spec.bEntering);
    TransformTrack->AddSection(*TransformSection);

    auto* OpacityTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(Guid);
    OpacityTrack->SetPropertyNameAndPath(TEXT("RenderOpacity"), TEXT("RenderOpacity"));
    auto* OpacitySection = CastChecked<UMovieSceneFloatSection>(OpacityTrack->CreateNewSection());
    OpacitySection->SetRange(Range);
    OpacitySection->SetCompletionMode(EMovieSceneCompletionMode::KeepState);
    TweenChannel(OpacitySection->GetChannel(), Spec.bEntering ? 0.f : 1.f,
        Spec.bEntering ? 1.f : 0.f, Start, End, Spec.bEntering);
    OpacityTrack->AddSection(*OpacitySection);
    BP->Animations.Add(Animation);
    BP->WidgetVariableNameToGuidMap.FindOrAdd(Animation->GetFName()) = FGuid::NewGuid();
    return Animation;
}

bool AddMissingAnimations(UWidgetBlueprint* BP)
{
    if (!BP || !BP->WidgetTree) return false;
    // Validate all existing assets before touching any animation or saving a package.
    for (const FPanelAnimationSpec& Spec : PanelAnimations)
    {
        if (!BP->WidgetTree->FindWidget(Spec.Widget))
        {
            UE_LOG(LogTemp, Error, TEXT("Missing animation target %s; no assets saved."), Spec.Widget);
            return false;
        }
        if (auto* Existing = FindAnimation(BP, Spec.Name); Existing && !ValidateAnimation(BP, Existing, Spec))
        {
            UE_LOG(LogTemp, Error, TEXT("Existing animation %s has an invalid binding/range; preserving it and refusing to save."), Spec.Name);
            return false;
        }
    }
    bool bAdded = false;
    bool bRepairedGuid = false;
    for (const FPanelAnimationSpec& Spec : PanelAnimations)
    {
        if (FindAnimation(BP, Spec.Name))
        {
            FGuid& VariableGuid = BP->WidgetVariableNameToGuidMap.FindOrAdd(Spec.Name);
            if (!VariableGuid.IsValid()) { VariableGuid = FGuid::NewGuid(); bRepairedGuid = true; }
            continue;
        }
        if (!ValidateAnimation(BP, AuthorAnimation(BP, Spec), Spec))
        {
            UE_LOG(LogTemp, Error, TEXT("Animation %s failed validation; no assets saved."), Spec.Name);
            return false;
        }
        bAdded = true;
    }
    if ((bAdded || bRepairedGuid) && !Save(BP)) return false;
    InspectAnimations(BP);
    UE_LOG(LogTemp, Display, TEXT("SQUAD_UI_ANIMATIONS_OK added=%d count=%d"), bAdded, BP->Animations.Num());
    return true;
}

struct FDesigner
{
    UWidgetTree* Tree;
    UClass* ButtonClass;
    UClass* TabClass;

    UCanvasPanel* Canvas(const TCHAR* Name)
    {
        auto* W = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), Name);
        W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        return W;
    }

    UCanvasPanelSlot* Place(UCanvasPanel* Parent, UWidget* Widget, FAnchors Anchors, FMargin Offsets, int32 Z = 0)
    {
        auto* Slot = Parent->AddChildToCanvas(Widget);
        Slot->SetAnchors(Anchors);
        Slot->SetOffsets(Offsets);
        Slot->SetZOrder(Z);
        return Slot;
    }

    UImage* Plate(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Hex, FAnchors Anchors, FMargin Offsets)
    {
        auto* W = Tree->ConstructWidget<UImage>(UImage::StaticClass(), Name);
        W->SetColorAndOpacity(Color(Hex));
        W->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(Parent, W, Anchors, Offsets);
        return W;
    }

    UTextBlock* Text(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Value, int32 Size,
        const TCHAR* Hex, FAnchors Anchors, FMargin Offsets, bool Wrap = false)
    {
        auto* W = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
        W->SetText(FText::FromString(Value));
        W->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), Size, TEXT("Regular")));
        W->SetColorAndOpacity(Color(Hex));
        W->SetAutoWrapText(Wrap);
        W->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(Parent, W, Anchors, Offsets);
        return W;
    }

    UBasicButtonWidget* Button(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Label,
        FAnchors Anchors, FMargin Offsets, int32 FontSize = 12)
    {
        auto* W = Tree->ConstructWidget<UBasicButtonWidget>(ButtonClass, Name);
        W->ButtonText = FText::FromString(Label);
        W->Font.Size = FontSize;
        W->MinimumSize = FVector2D(0, 32);
        Place(Parent, W, Anchors, Offsets);
        return W;
    }

    USelectionButtonWidget* Tab(UCanvasPanel* Parent, const TCHAR* Name, const TCHAR* Label,
        const TCHAR* ID, FAnchors Anchors, FMargin Offsets)
    {
        auto* W = Tree->ConstructWidget<USelectionButtonWidget>(TabClass, Name);
        W->ButtonText = FText::FromString(Label);
        W->ChoiceID = ID;
        W->MinimumSize = FVector2D(0, 36);
        W->Font.Size = 13;
        Place(Parent, W, Anchors, Offsets);
        return W;
    }

    UCanvasPanel* Section(UVerticalBox* Stack, const TCHAR* Name, float Height, float BottomPadding = 16)
    {
        auto* Frame = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(*(FString(Name) + TEXT("Size"))));
        Frame->SetHeightOverride(Height);
        Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        auto* Slot = Stack->AddChildToVerticalBox(Frame);
        Slot->SetPadding(FMargin(0, 0, 0, BottomPadding));
        Slot->SetHorizontalAlignment(HAlign_Fill);
        auto* W = Canvas(Name);
        Frame->AddChild(W);
        return W;
    }
};

bool CanAuthor(UWidgetBlueprint* BP, FName Marker)
{
    if (BP->WidgetTree->FindWidget(Marker)) return true;
    UWidget* Root = BP->WidgetTree->RootWidget;
    if (!Root) return true;
    if (auto* Panel = Cast<UPanelWidget>(Root); Panel && Panel->GetChildrenCount() == 0) return true;
    UE_LOG(LogTemp, Error, TEXT("Preserving authored layout in %s: non-empty root has no squad layout marker."), *BP->GetPathName());
    return false;
}

void AttachRoot(UWidgetBlueprint* BP, UCanvasPanel* Layout)
{
    if (auto* ExistingOverlay = Cast<UOverlay>(BP->WidgetTree->RootWidget))
    {
        auto* Slot = ExistingOverlay->AddChildToOverlay(Layout);
        Slot->SetHorizontalAlignment(HAlign_Fill);
        Slot->SetVerticalAlignment(VAlign_Fill);
    }
    else if (auto* ExistingCanvas = Cast<UCanvasPanel>(BP->WidgetTree->RootWidget))
    {
        auto* Slot = ExistingCanvas->AddChildToCanvas(Layout);
        Slot->SetAnchors(FAnchors(0, 0, 1, 1));
        Slot->SetOffsets(FMargin(0));
    }
    else BP->WidgetTree->RootWidget = Layout;
}
}

int32 USquadUIAssetsCommandlet::Main(const FString& Params)
{
    using namespace SquadUIAssets;
    if (Params.Contains(TEXT("SceneReference")))
    {
        Inspect(LoadObject<UBlueprint>(nullptr, TEXT("/Game/System/Map/BaseMap/Scene/BP_人员整备室_Scene")), true);
        Inspect(LoadObject<UBlueprint>(nullptr, ScenePath), true);
        Inspect(LoadObject<UBlueprint>(nullptr, TEXT("/Game/System/Map/BaseMap/Scene/BP_基地全景_Scene")), true);
        Inspect(LoadObject<UBlueprint>(nullptr, TEXT("/Game/System/SubSystem/SceneSystem/BP_SceneManager")), true);
        Inspect(LoadObject<UBlueprint>(nullptr, ShellPath), true);
        UE_LOG(LogTemp, Display, TEXT("SQUAD_SCENE_REFERENCE_OK"));
        return 0;
    }
    if (Params.Contains(TEXT("Inspect")))
    {
        Inspect(LoadObject<UBlueprint>(nullptr, RoomPath));
        Inspect(LoadObject<UBlueprint>(nullptr, ScenePath));
        InspectAnimations(LoadObject<UWidgetBlueprint>(nullptr, PersonnelPath));
        if (auto* Shell = LoadObject<UWidgetBlueprint>(nullptr, ShellPath); Shell && Shell->GeneratedClass)
            for (const auto& Pair : CastChecked<UBaseMapWidget>(Shell->GeneratedClass->GetDefaultObject())->SceneUIClasses)
                UE_LOG(LogTemp, Display, TEXT("SQUAD_SHELL_MAPPING %s -> %s"), *Pair.Key.ToString(), *GetNameSafe(Pair.Value.Get()));
        UE_LOG(LogTemp, Display, TEXT("SQUAD_UI_INSPECT_OK"));
        return 0;
    }
    if (Params.Contains(TEXT("Animations")))
        return AddMissingAnimations(LoadObject<UWidgetBlueprint>(nullptr, RoomPath)) ? 0 : 1;
    if (!Params.Contains(TEXT("Build")))
    {
        UE_LOG(LogTemp, Error, TEXT("Specify -Inspect (read-only), -Animations (add missing UMG animations), or -Build (author missing squad UI only)."));
        return 1;
    }

    auto* Room = FindOrCreate(RoomPath, USquadMeetingRoomWidget::StaticClass());
    auto* Row = FindOrCreate(RowPath, USelectionButtonWidget::StaticClass());
    auto* Member = FindOrCreate(MemberPath, USquadMemberCardWidget::StaticClass());
    auto* IconOption = FindOrCreate(IconOptionPath, USelectionButtonWidget::StaticClass());
    auto* Button = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/System/UIBasic/WBP_ShellButton"));
    auto* Tab = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/System/UIBasic/WBP_SelectionButton"));
    auto* Portrait = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/Components/WBP_PersonnelPortrait"));
    auto* PersonnelRow = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/Components/WBP_PersonnelListEntry"));
    if (!Room || !Row || !Member || !IconOption || !Button || !Tab || !Portrait || !PersonnelRow ||
        !CanAuthor(Room, TEXT("SquadRoomLayout")) || !CanAuthor(Row, TEXT("SquadRowLayout")) ||
        !CanAuthor(Member, TEXT("MemberCardLayout")) || !CanAuthor(IconOption, TEXT("IconOptionLayout"))) return 1;

    if (!IconOption->WidgetTree->FindWidget(TEXT("IconOptionLayout")))
    {
        FDesigner D{IconOption->WidgetTree, Button->GeneratedClass.Get(), Tab->GeneratedClass.Get()};
        auto* Size = D.Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ButtonSize"));
        Size->SetWidthOverride(62);
        Size->SetHeightOverride(62);
        Size->SetVisibility(ESlateVisibility::HitTestInvisible);
        D.Tree->RootWidget = Size;
        auto* Layout = D.Canvas(TEXT("IconOptionLayout"));
        Layout->SetVisibility(ESlateVisibility::HitTestInvisible);
        Size->AddChild(Layout);
        D.Plate(Layout, TEXT("IconImage"), TEXT("FFFFFFFF"), FAnchors(0, 0, 1, 1), FMargin(8));
    }
    if (!Save(IconOption)) return 1;
    auto* IconDefaults = CastChecked<USelectionButtonWidget>(IconOption->GeneratedClass->GetDefaultObject());
    IconDefaults->ContentMode = EBasicButtonContent::IconOnly;
    IconDefaults->MinimumSize = FVector2D(62, 62);
    IconDefaults->bUseTabStyle = false;
    IconDefaults->HoverSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuHover_Electronic"));
    IconDefaults->PressSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuPress_Terminal"));
    if (!Save(IconOption)) return 1;

    if (!Row->WidgetTree->FindWidget(TEXT("SquadRowLayout")))
    {
        FDesigner D{Row->WidgetTree, Button->GeneratedClass.Get(), Tab->GeneratedClass.Get()};
        auto* Size = D.Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ButtonSize"));
        Size->SetHeightOverride(92);
        Size->SetVisibility(ESlateVisibility::HitTestInvisible);
        D.Tree->RootWidget = Size;
        auto* Layout = D.Canvas(TEXT("SquadRowLayout"));
        Layout->SetVisibility(ESlateVisibility::HitTestInvisible);
        Size->AddChild(Layout);
        D.Plate(Layout, TEXT("SquadIconPlate"), TEXT("102635"), FAnchors(0, 0), FMargin(12, 16, 60, 60));
        auto* Icon = D.Plate(Layout, TEXT("IconImage"), TEXT("FFFFFFFF"), FAnchors(0, 0), FMargin(16, 20, 52, 52));
        Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
        D.Text(Layout, TEXT("Label"), TEXT("未命名小队"), 16, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(86, 23, 12, 28));
        D.Text(Layout, TEXT("DetailLabel"), TEXT("00 名成员 / 当前瓦片"), 10, TEXT("7798AC"), FAnchors(0, 0, 1, 0), FMargin(86, 54, 12, 22));
    }
    if (!Save(Row)) return 1;
    auto* RowDefaults = CastChecked<USelectionButtonWidget>(Row->GeneratedClass->GetDefaultObject());
    RowDefaults->ContentMode = EBasicButtonContent::IconAndText;
    RowDefaults->MinimumSize = FVector2D(0, 92);
    RowDefaults->Font.Size = 16;
    RowDefaults->bUseTabStyle = false;
    RowDefaults->HoverSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuHover_Electronic"));
    RowDefaults->PressSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/System/Map/MainMenu/Audio/UI_MenuPress_Terminal"));
    if (!Save(Row)) return 1;

    if (!Member->WidgetTree->FindWidget(TEXT("MemberCardLayout")))
    {
        FDesigner D{Member->WidgetTree, Button->GeneratedClass.Get(), Tab->GeneratedClass.Get()};
        auto* Size = D.Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MemberCardSize"));
        Size->SetWidthOverride(164);
        Size->SetHeightOverride(164);
        Size->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        D.Tree->RootWidget = Size;
        auto* Layout = D.Canvas(TEXT("MemberCardLayout"));
        Size->AddChild(Layout);
        D.Plate(Layout, TEXT("MemberPlate"), TEXT("0B1B28F5"), FAnchors(0, 0, 1, 1), FMargin(0));
        D.Plate(Layout, TEXT("MemberTopRule"), TEXT("285C75"), FAnchors(0, 0, 1, 0), FMargin(0, 0, 0, 1));
        auto* Face = D.Tree->ConstructWidget<UPersonnelPortraitWidget>(Portrait->GeneratedClass.Get(), TEXT("MemberPortrait"));
        Face->SetVisibility(ESlateVisibility::HitTestInvisible);
        D.Place(Layout, Face, FAnchors(0, 0), FMargin(10, 12, 54, 54));
        D.Text(Layout, TEXT("MemberName"), TEXT("人员代号"), 13, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(74, 17, 8, 28));
        D.Text(Layout, TEXT("CaptainLabel"), TEXT("队长"), 10, TEXT("45BDE7"), FAnchors(0, 0, 1, 0), FMargin(74, 45, 8, 20));
        D.Button(Layout, TEXT("PromoteButton"), TEXT("提升为队长"), FAnchors(0, 0, 1, 0), FMargin(10, 80, 10, 32), 10);
        D.Button(Layout, TEXT("RemoveButton"), TEXT("移出小队"), FAnchors(0, 0, 1, 0), FMargin(10, 120, 10, 32), 10);
    }
    if (!Save(Member)) return 1;

    if (!Room->WidgetTree->FindWidget(TEXT("SquadRoomLayout")))
    {
        FDesigner D{Room->WidgetTree, Button->GeneratedClass.Get(), Tab->GeneratedClass.Get()};
        auto* Root = D.Canvas(TEXT("SquadRoomLayout"));
        AttachRoot(Room, Root);
        auto* Left = D.Canvas(TEXT("LeftPanel"));
        D.Place(Root, Left, FAnchors(0, 0, .28f, 1), FMargin(0, 0, 12, 0));
        D.Plate(Left, TEXT("LeftPlate"), TEXT("06111BF5"), FAnchors(0, 0, 1, 1), FMargin(0));
        D.Plate(Left, TEXT("LeftTopAccent"), TEXT("329ACA"), FAnchors(0, 0), FMargin(0, 4, 3, 27));
        D.Text(Left, TEXT("ListTitle"), TEXT("小队名册"), 20, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(18, 4, 18, 34));
        D.Text(Left, TEXT("CurrentTileText"), TEXT("当前瓦片  —"), 10, TEXT("7798AC"), FAnchors(0, 0, 1, 0), FMargin(18, 46, 18, 22));
        D.Tab(Left, TEXT("StandbyFilter"), TEXT("待命"), TEXT("Standby"), FAnchors(0, 0, .5f, 0), FMargin(16, 80, 2, 36));
        D.Tab(Left, TEXT("AllFilter"), TEXT("全部"), TEXT("All"), FAnchors(.5f, 0, 1, 0), FMargin(2, 80, 16, 36));
        auto* Pages = D.Tree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("LeftPages"));
        Pages->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        D.Place(Left, Pages, FAnchors(0, 0, 1, 1), FMargin(16, 136, 8, 42));
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto* Page = D.Canvas(Index == 0 ? TEXT("SquadRosterPage") : TEXT("PersonnelRosterPage"));
            auto* PageSlot = CastChecked<UWidgetSwitcherSlot>(Pages->AddChild(Page));
            PageSlot->SetHorizontalAlignment(HAlign_Fill);
            PageSlot->SetVerticalAlignment(VAlign_Fill);
            auto* List = D.Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), Index == 0 ? TEXT("SquadList") : TEXT("PersonnelList"));
            List->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
            List->SetScrollbarThickness(FVector2D(3, 3));
            D.Place(Page, List, FAnchors(0, 0, 1, 1), FMargin(0, 0, 0, 58));
            D.Button(Page, Index == 0 ? TEXT("CreateSquadButton") : TEXT("BackToSquadsButton"),
                Index == 0 ? TEXT("+   新建小队") : TEXT("‹   返回小队列表"), FAnchors(0, 1, 1, 1), FMargin(0, -42, 8, 38));
        }
        Pages->SetActiveWidgetIndex(0);
        D.Text(Left, TEXT("EmptyListText"), TEXT("当前瓦片还没有小队\n点击下方按钮建立第一支小队"), 12, TEXT("7798AC"), FAnchors(0, 0, 1, 0), FMargin(24, 158, 24, 80), true);
        D.Text(Left, TEXT("RosterCaption"), TEXT("SQUAD ROSTER / 小队调度"), 8, TEXT("537C98"), FAnchors(0, 1, 1, 1), FMargin(18, -26, 18, 20));

        auto* Prompt = D.Canvas(TEXT("SelectionPrompt"));
        D.Place(Root, Prompt, FAnchors(.28f, .35f, 1, .65f), FMargin(12, 0, 18, 0));
        D.Text(Prompt, TEXT("SelectionTitle"), TEXT("选择一支小队"), 24, TEXT("B9D2E1"), FAnchors(0, .35f, 1, .35f), FMargin(18, 0, 18, 40))->SetJustification(ETextJustify::Center);
        D.Text(Prompt, TEXT("SelectionHint"), TEXT("编组人员 · 设置队长 · 规划行动"), 12, TEXT("638EA5"), FAnchors(0, .35f, 1, .35f), FMargin(18, 50, 18, 28))->SetJustification(ETextJustify::Center);

        auto* Management = D.Canvas(TEXT("ManagementPanel"));
        D.Place(Root, Management, FAnchors(.72f, 0, 1, 1), FMargin(12, 0, 0, 0));
        D.Plate(Management, TEXT("ManagementPlate"), TEXT("06111BF5"), FAnchors(0, 0, 1, 1), FMargin(0));
        D.Text(Management, TEXT("ManagementTitle"), TEXT("小队管理"), 20, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(18, 8, 18, 34));
        D.Text(Management, TEXT("ManagementCaption"), TEXT("SQUAD CONFIGURATION"), 8, TEXT("537C98"), FAnchors(0, 0, 1, 0), FMargin(18, 45, 18, 18));
        D.Plate(Management, TEXT("ManagementRule"), TEXT("234355"), FAnchors(0, 0, 1, 0), FMargin(18, 72, 18, 1));
        auto* Scroll = D.Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ManagementScroll"));
        Scroll->SetScrollbarThickness(FVector2D(3, 3));
        D.Place(Management, Scroll, FAnchors(0, 0, 1, 1), FMargin(18, 88, 10, 14));
        auto* Stack = D.Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ManagementSections"));
        Stack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        auto* StackSlot = CastChecked<UScrollBoxSlot>(Scroll->AddChild(Stack));
        StackSlot->SetPadding(FMargin(0, 0, 8, 0));
        StackSlot->SetHorizontalAlignment(HAlign_Fill);

        auto* Identity = D.Section(Stack, TEXT("SquadIdentitySection"), 90);
        D.Plate(Identity, TEXT("CurrentIconPlate"), TEXT("102635"), FAnchors(0, 0), FMargin(0, 0, 78, 78));
        D.Plate(Identity, TEXT("SquadIconImage"), TEXT("FFFFFFFF"), FAnchors(0, 0), FMargin(6, 6, 66, 66));
        D.Text(Identity, TEXT("IdentityLabel"), TEXT("当前小队"), 15, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(96, 7, 0, 26));
        D.Text(Identity, TEXT("SquadLocationText"), TEXT("当前位置  —"), 10, TEXT("7798AC"), FAnchors(0, 0, 1, 0), FMargin(96, 45, 0, 30), true);

        auto* Name = D.Section(Stack, TEXT("SquadNameSection"), 132);
        D.Text(Name, TEXT("NameLabel"), TEXT("小队名称"), 13, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(0, 0, 0, 24));
        auto* NameInput = D.Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SquadNameInput"));
        NameInput->SetHintText(FText::FromString(TEXT("输入小队名称")));
        NameInput->SetSelectAllTextWhenFocused(true);
        NameInput->SetClearKeyboardFocusOnCommit(true);
        FEditableTextBoxStyle InputStyle = NameInput->GetWidgetStyle();
        InputStyle.SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")), 13));
        InputStyle.SetForegroundColor(FSlateColor(Color(TEXT("DCE9F5"))));
        InputStyle.SetBackgroundColor(FSlateColor(Color(TEXT("0B1B28"))));
        InputStyle.SetPadding(FMargin(10, 6));
        NameInput->SetWidgetStyle(InputStyle);
        D.Place(Name, NameInput, FAnchors(0, 0, 1, 0), FMargin(0, 30, 0, 40));
        D.Button(Name, TEXT("SaveSquadNameButton"), TEXT("保存名称"), FAnchors(0, 0, 1, 0), FMargin(0, 84, 0, 36));

        auto* IconsTitle = D.Section(Stack, TEXT("IconTitleSection"), 28, 8);
        D.Text(IconsTitle, TEXT("IconLabel"), TEXT("小队标识"), 13, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(0, 0, 0, 26));
        auto* Icons = D.Tree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("IconOptions"));
        Icons->SetInnerSlotPadding(FVector2D(8, 8));
        Stack->AddChildToVerticalBox(Icons)->SetPadding(FMargin(0, 0, 0, 12));
        auto* PortraitOption = D.Section(Stack, TEXT("CaptainPortraitSection"), 58);
        D.Button(PortraitOption, TEXT("UseCaptainPortraitButton"), TEXT("使用队长头像作为标识"), FAnchors(0, 0, 1, 0), FMargin(0, 0, 0, 38), 11);

        auto* Vehicle = D.Section(Stack, TEXT("VehicleSection"), 160);
        D.Plate(Vehicle, TEXT("VehicleTopRule"), TEXT("234355"), FAnchors(0, 0, 1, 0), FMargin(0, 0, 0, 1));
        D.Text(Vehicle, TEXT("VehicleLabel"), TEXT("小队车辆"), 13, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(0, 20, 0, 28));
        D.Text(Vehicle, TEXT("VehicleText"), TEXT("尚未分配车辆"), 12, TEXT("7798AC"), FAnchors(0, 0, 1, 0), FMargin(0, 54, 0, 28));
        D.Button(Vehicle, TEXT("ChooseVehicleButton"), TEXT("选择车辆"), FAnchors(0, 0, 1, 0), FMargin(0, 94, 0, 38));
        D.Text(Vehicle, TEXT("VehicleHint"), TEXT("未选车辆默认4人 · 选车后按座位数编组"), 9, TEXT("537C98"), FAnchors(0, 0, 1, 0), FMargin(0, 140, 0, 20));

        auto* Members = D.Canvas(TEXT("MemberStripPanel"));
        D.Place(Root, Members, FAnchors(.28f, 1, .72f, 1), FMargin(12, -248, 12, 248));
        D.Plate(Members, TEXT("MemberStripPlate"), TEXT("06111BD9"), FAnchors(0, 0, 1, 1), FMargin(0));
        D.Plate(Members, TEXT("MemberStripRule"), TEXT("285C75"), FAnchors(0, 0, 1, 0), FMargin(0, 0, 0, 1));
        D.Text(Members, TEXT("MemberCountText"), TEXT("小队成员  00"), 16, TEXT("DCE9F5"), FAnchors(0, 0, 1, 0), FMargin(16, 12, 16, 30));
        auto* MemberList = D.Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("MemberList"));
        MemberList->SetOrientation(Orient_Horizontal);
        MemberList->SetScrollbarThickness(FVector2D(3, 3));
        D.Place(Members, MemberList, FAnchors(0, 0, 1, 1), FMargin(14, 54, 8, 12));
        D.Text(Root, TEXT("ErrorText"), TEXT(""), 12, TEXT("E0AD8C"), FAnchors(.28f, 0, .72f, 0), FMargin(24, 20, 24, 80), true)->SetVisibility(ESlateVisibility::Collapsed);
        Management->SetVisibility(ESlateVisibility::Collapsed);
        Members->SetVisibility(ESlateVisibility::Collapsed);
    }
    if (!Save(Room)) return 1;
    auto* Defaults = CastChecked<USquadMeetingRoomWidget>(Room->GeneratedClass->GetDefaultObject());
    Defaults->SceneTag = FGameplayTag::RequestGameplayTag(TEXT("GameScene.SquadMeetingRoom"));
    if (!Defaults->SquadRowClass) Defaults->SquadRowClass = Row->GeneratedClass.Get();
    if (!Defaults->PersonnelRowClass) Defaults->PersonnelRowClass = PersonnelRow->GeneratedClass.Get();
    if (!Defaults->MemberCardClass) Defaults->MemberCardClass = Member->GeneratedClass.Get();
    if (!Defaults->IconButtonClass) Defaults->IconButtonClass = IconOption->GeneratedClass.Get();
    if (!Save(Room)) return 1;

    auto* Shell = LoadObject<UWidgetBlueprint>(nullptr, ShellPath);
    if (!Shell || !Shell->GeneratedClass) return 1;
    auto* ShellDefaults = CastChecked<UBaseMapWidget>(Shell->GeneratedClass->GetDefaultObject());
    const FGameplayTag Tag = Defaults->SceneTag;
    auto* ExistingClass = ShellDefaults->SceneUIClasses.Find(Tag);
    if (!ExistingClass || !ExistingClass->Get())
    {
        ShellDefaults->SceneUIClasses.Add(Tag, Room->GeneratedClass.Get());
        if (!Save(Shell)) return 1;
    }
    else if (ExistingClass->Get() != Room->GeneratedClass.Get())
        UE_LOG(LogTemp, Warning, TEXT("Preserved user SceneUIClasses mapping for %s: %s"), *Tag.ToString(), *GetNameSafe(ExistingClass->Get()));
    UE_LOG(LogTemp, Display, TEXT("SQUAD_UI_ASSETS_OK"));
    return 0;
}
