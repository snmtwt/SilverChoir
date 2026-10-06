#include "BattlePersonnelCardAssets.h"
#include "MainMapBlueprintBuilder.h"
#include "Map/BattleMap/BattlePersonnelCardWidget.h"
#include "Map/BattleMap/BattleResourceBar.h"
#include "UIBasic/ECGWidget.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "WidgetBlueprint.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Kismet2/CompilerResultsLog.h"

namespace
{
// This asset's renamed controls have no user-authored value graphs. Update the
// metadata references too, retaining animation bindings and existing graph nodes.
UWidget* NameWidget(UWidgetBlueprint* BP,const TCHAR* Old,const TCHAR* New)
{
    if(auto* W=BP->WidgetTree->FindWidget(New))return W;
    auto* W=BP->WidgetTree->FindWidget(Old);if(!W)return nullptr;
    if(!W->Rename(New,BP->WidgetTree,REN_DontCreateRedirectors|REN_NonTransactional))return nullptr;
    if(auto* Id=BP->WidgetVariableNameToGuidMap.Find(Old))
    {const FGuid Saved=*Id;BP->WidgetVariableNameToGuidMap.Remove(Old);BP->WidgetVariableNameToGuidMap.Add(New,Saved);}
    for(auto& B:BP->Bindings)if(B.ObjectName==Old)B.ObjectName=New;
    for(UWidgetAnimation* Animation:BP->Animations)for(auto& B:Animation->AnimationBindings)
    {if(B.WidgetName==Old)B.WidgetName=New;if(B.SlotWidgetName==Old)B.SlotWidgetName=New;}
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
    for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)
        if(auto* V=Cast<UK2Node_Variable>(Node);V&&V->VariableReference.IsSelfContext()&&V->VariableReference.GetMemberName()==Old)
        {V->VariableReference.SetSelfMember(New);V->ReconstructNode();}
    return W;
}

bool UpgradeBar(UWidgetBlueprint* BP,const TCHAR* Name,const TCHAR* SizeName,FLinearColor Color)
{
    auto* Old=Cast<UProgressBar>(BP->WidgetTree->FindWidget(Name));if(!Old)return false;
    auto* Bar=Cast<UBattleResourceBar>(Old);
    if(!Bar)
    {
        auto* Parent=Old->GetParent();if(!Parent)return false;
        const float Percent=Old->GetPercent();
        Old->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
        Bar=BP->WidgetTree->ConstructWidget<UBattleResourceBar>(UBattleResourceBar::StaticClass(),Name);
        auto* Size=BP->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),SizeName);
        Size->SetWidthOverride(7);Size->SetVisibility(ESlateVisibility::HitTestInvisible);
        if(!Parent->ReplaceChild(Old,Size))return false;
        Old->Slot=nullptr;Size->AddChild(Bar);
        if(auto* Slot=Cast<UHorizontalBoxSlot>(Size->Slot)){Slot->SetPadding(FMargin(2,2,3,2));Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);}
        Bar->SetPercent(Percent);
    }
    Bar->SetFillColorAndOpacity(FLinearColor::White);
    Bar->SetGradientColors(FLinearColor(Color.R*.3f,Color.G*.3f,Color.B*.3f,1),Color,FMath::Lerp(Color,FLinearColor::White,.65f).CopyWithNewOpacity(.8f));
    Bar->SetVisibility(ESlateVisibility::HitTestInvisible);return true;
}
}

bool UpgradeBattlePersonnelCard()
{
    const TCHAR* Path=TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_人员卡片");
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,Path);if(!BP||!BP->WidgetTree)return false;
    auto* Extent=Cast<USizeBox>(BP->WidgetTree->FindWidget(TEXT("SizeBox_202")));
    auto* Root=Cast<UBorder>(BP->WidgetTree->RootWidget);
    if(!Extent||!Root||Extent->GetWidthOverride()!=230||Extent->GetHeightOverride()!=144||Root->GetPadding()!=FMargin(2))
    {UE_LOG(LogTemp,Error,TEXT("PERSONNEL_CARD_SIZE_CHANGED: expected original 230x144 + 2 padding; no asset saved"));return false;}
    const FString File=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
    const FString Backup=FPaths::ProjectSavedDir()/TEXT("PersonnelCardUpgrade/Backup")/FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))/TEXT("WBP_人员卡片.uasset");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup),true);
    if(IFileManager::Get().Copy(*Backup,*File)!=COPY_OK)return false;

    if(!NameWidget(BP,TEXT("人员头像"),TEXT("Portrait"))||!NameWidget(BP,TEXT("体力条"),TEXT("StaminaBar"))
        ||!NameWidget(BP,TEXT("士气条"),TEXT("MoraleBar"))||!NameWidget(BP,TEXT("TextBlock"),TEXT("MemberName"))
        ||!NameWidget(BP,TEXT("TextBlock_72"),TEXT("HealthText"))||!NameWidget(BP,TEXT("WBP_ECG"),TEXT("ECGMonitor"))
        ||!NameWidget(BP,TEXT("WBP_MirrorSlotContainer"),TEXT("HandEquipment")))return false;
    if(!UpgradeBar(BP,TEXT("StaminaBar"),TEXT("StaminaTrackSize"),FLinearColor(FColor(58,177,245)))
        ||!UpgradeBar(BP,TEXT("MoraleBar"),TEXT("MoraleTrackSize"),FLinearColor(FColor(71,211,151))))return false;

    // The native selection path owns mouse release. Retain the old drawer graphs
    // and animations for reference without executing a second width-changing path.
    BP->Bindings.RemoveAll([](const auto& B){return B.FunctionName==TEXT("点击人物面板");});
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Root->SetBrushColor(FLinearColor::Transparent);
    // A rounded-box outline is independent of the fill tint. Remove the old
    // brush entirely so the native frame can display selected/unselected states.
    FSlateBrush EmptyBrush;EmptyBrush.DrawAs=ESlateBrushDrawType::NoDrawType;Root->SetBrush(EmptyBrush);
    auto* Main=Cast<UBorder>(BP->WidgetTree->FindWidget(TEXT("Border_2")));if(Main){Main->SetBrushColor(FLinearColor::Transparent);Main->SetVisibility(ESlateVisibility::SelfHitTestInvisible);}
    for(const TCHAR* Name:{TEXT("Portrait"),TEXT("MemberName"),TEXT("HealthText"),TEXT("ECGMonitor"),TEXT("Border_0")})
        BP->WidgetTree->FindWidget(Name)->SetVisibility(ESlateVisibility::HitTestInvisible);
    for(const TCHAR* Name:{TEXT("MemberName"),TEXT("HealthText")})
    {
        auto* Text=CastChecked<UTextBlock>(BP->WidgetTree->FindWidget(Name));auto Font=Text->GetFont();Font.Size=FCString::Strcmp(Name,TEXT("MemberName"))==0?12:10;
        Text->SetFont(Font);Text->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);Text->SetColorAndOpacity(FLinearColor(FColor(212,233,241)));
    }
    if(auto* Header=Cast<UBorder>(BP->WidgetTree->FindWidget(TEXT("Border_0"))))Header->SetBrushColor(FLinearColor(FColor(10,27,39,220)));
    if(auto* Details=BP->WidgetTree->FindWidget(TEXT("VerticalBox_0")))
        if(auto* Slot=Cast<UHorizontalBoxSlot>(Details->Slot))Slot->SetPadding(FMargin(6,0,2,0));
    auto* ECG=CastChecked<UECGWidget>(BP->WidgetTree->FindWidget(TEXT("ECGMonitor")));
    ECG->WaveColor=FLinearColor(FColor(43,206,224));ECG->HeartRateBPM=72;ECG->bShowGrid=false;ECG->bShowBackground=false;ECG->GlowStrength=.85f;
    BP->ParentClass=UBattlePersonnelCardWidget::StaticClass();
    BP->ForEachSourceWidget([&](UWidget* W){if(!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewGuid());});
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    if(!MainMapBP::Compile(BP))return false;
    auto* CDO=CastChecked<UBattlePersonnelCardWidget>(BP->GeneratedClass->GetDefaultObject());
    CDO->MinimumSize=FVector2D::ZeroVector;CDO->SetVisibility(ESlateVisibility::Visible);CDO->SetIsFocusable(true);
    CDO->ButtonText=FText::GetEmpty();CDO->bUseTabStyle=false;
    CDO->DesignSizeMode=EDesignPreviewSizeMode::Desired;
    // These events are deliberately unconnected business entry points.
    if(!MainMapBP::Find(BP,TEXT("PersonnelBusiness")))
    {
        auto* Graph=FBlueprintEditorUtils::CreateNewGraph(BP,TEXT("PersonnelBusiness"),UEdGraph::StaticClass(),UEdGraphSchema_K2::StaticClass());FBlueprintEditorUtils::AddUbergraphPage(BP,Graph);MainMapBP::FGraph G{BP,Graph};
        G.Comment(TEXT("C++负责数据校验、生命状态、选择样式与点击请求。小队统一调用SetSelected。下方事件可接入真实单位库存与游戏指令；卡片本身不修改单位或物品。"));
        G.X=0;G.Y=200;G.Node<UK2Node_Event>([](auto* E){E->EventReference.SetExternalMember(TEXT("OnMemberDataApplied"),UBattlePersonnelCardWidget::StaticClass());E->bOverrideFunction=true;});
        G.X=0;G.Y=520;G.Node<UK2Node_Event>([](auto* E){E->EventReference.SetExternalMember(TEXT("OnMemberInvoked"),UBattlePersonnelCardWidget::StaticClass());E->bOverrideFunction=true;});
    }
    if(!MainMapBP::Compile(BP))return false;
    BP->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;
    if(!UPackage::SavePackage(BP->GetOutermost(),BP,*File,Save))return false;
    UE_LOG(LogTemp,Display,TEXT("PERSONNEL_CARD_UPGRADE_OK size=234x148 body=230x144 portrait=80x80; native bars, ECG, selection, inventory and Blueprint business events"));return true;
}

bool RepairBattlePersonnelCardBindings(bool bApply)
{
    const TCHAR* Path=TEXT("/Game/System/Map/BattleMap/UI/Components/WBP_人员卡片");
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,Path);
    if(!BP||!BP->WidgetTree||!BP->GeneratedClass||!BP->SkeletonGeneratedClass)return false;
    TArray<int32> Broken;
    for(int32 Index=0;Index<BP->Bindings.Num();++Index)
    {
        const auto& Binding=BP->Bindings[Index];
        FString Serialized;
        FDelegateEditorBinding::StaticStruct()->ExportText(Serialized,&Binding,nullptr,nullptr,PPF_None,nullptr);
        UE_LOG(LogTemp,Display,TEXT("PERSONNEL_CARD_BINDING index=%d %s"),Index,*Serialized);
        if((Binding.ObjectName!=TEXT("Border_1")&&Binding.ObjectName!=TEXT("Border_2"))
            ||Binding.PropertyName!=GET_MEMBER_NAME_CHECKED(UBorder,OnMouseButtonDownEvent))continue;
        auto* Border=Cast<UBorder>(BP->WidgetTree->FindWidget(FName(*Binding.ObjectName)));
        if(!Border)continue;
        bool bBroken=false;
        if(!Binding.SourcePath.IsEmpty())
        {
            // UE resolves a path by its saved member GUID, not just FunctionName.
            // A deleted function can leave a nonempty path whose last member resolves to None.
            FEditorPropertyPath Rebased=Binding.SourcePath;
            Rebased.Rebase(BP);
            auto* Delegate=FindFProperty<FDelegateProperty>(Border->GetClass(),Binding.PropertyName);
            FText Reason;
            bBroken=Delegate&&!Rebased.Validate(Delegate,Reason)
                &&Rebased.Segments.Last().GetMemberName().IsNone();
            UE_LOG(LogTemp,Display,TEXT("PERSONNEL_CARD_BINDING_RESOLVED widget=%s member=%s invalid=%d reason=%s"),
                *Binding.ObjectName,*Rebased.Segments.Last().GetMemberName().ToString(),bBroken,*Reason.ToString());
        }
        else
        {
            const FName ResolvedFunction=Binding.Kind==EBindingKind::Function&&Binding.MemberGuid.IsValid()
                ? BP->GetFieldNameFromClassByGuid<UFunction>(BP->SkeletonGeneratedClass,Binding.MemberGuid)
                : Binding.FunctionName;
            bBroken=ResolvedFunction.IsNone();
        }
        if(bBroken)Broken.Add(Index);
    }
    if(!bApply&&!Broken.IsEmpty())
    {
        UE_LOG(LogTemp,Display,TEXT("PERSONNEL_CARD_BINDINGS_INSPECT broken=%d total=%d; use -CardBindingsRepair"),Broken.Num(),BP->Bindings.Num());
        return true;
    }
    FString Backup;
    const FString File=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
    if(!Broken.IsEmpty())
    {
        Backup=FPaths::ProjectSavedDir()/TEXT("PersonnelCardBindings/Backup")/
            (FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits))/FPaths::GetCleanFilename(File);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup),true);
        if(IFileManager::Get().Copy(*Backup,*File)!=COPY_OK)return false;
        for(int32 Index=Broken.Num()-1;Index>=0;--Index)
        {
            const auto& Binding=BP->Bindings[Broken[Index]];
            UE_LOG(LogTemp,Display,TEXT("PERSONNEL_CARD_BINDING_REMOVED %s.%s"),*Binding.ObjectName,*Binding.PropertyName.ToString());
            BP->Bindings.RemoveAt(Broken[Index]);
        }
    }
    FCompilerResultsLog Results;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP,EBlueprintCompileOptions::None,&Results);
    if(Results.NumErrors>0||BP->Status==BS_Error)
    {UE_LOG(LogTemp,Error,TEXT("PERSONNEL_CARD_BINDINGS_COMPILE_FAILED errors=%d; asset not saved"),Results.NumErrors);return false;}
    if(bApply&&!Broken.IsEmpty())
    {
        BP->MarkPackageDirty();FSavePackageArgs Save;Save.TopLevelFlags=RF_Public|RF_Standalone;
        if(!UPackage::SavePackage(BP->GetOutermost(),BP,*File,Save))return false;
    }
    UE_LOG(LogTemp,Display,TEXT("PERSONNEL_CARD_BINDINGS_OK removed=%d remaining=%d errors=%d warnings=%d saved=%d backup=%s"),
        Broken.Num(),BP->Bindings.Num(),Results.NumErrors,Results.NumWarnings,bApply&&!Broken.IsEmpty(),*Backup);
    return true;
}
