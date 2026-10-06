#include "NewGameHandlerAssetsCommandlet.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_VariableGet.h"
#include "MTS_SubMapDataAsset.h"
#include "UObject/UnrealType.h"
#include "K2Node_Self.h"
#include "Kismet/GameplayStatics.h"
#include "MTS_MapTransitionSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MTS_MapTransitionHandler.h"
#include "Map/GameMainMap/GameMainMapGameMode.h"
#include "Map/MainMenu/MainMenuPlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

UNewGameHandlerAssetsCommandlet::UNewGameHandlerAssetsCommandlet() { IsEditor = true; IsClient = false; LogToConsole = true; }

namespace
{
bool Backup(UObject* Asset)
{
	const FString File = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	const FString Target = FPaths::ProjectSavedDir() / TEXT("NewGameHandlerBackups") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) / FPaths::GetCleanFilename(File);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
	return IFileManager::Get().Copy(*Target, *File) == COPY_OK;
}
bool Save(UObject* Asset)
{
	FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
	Asset->MarkPackageDirty();
	return UPackage::SavePackage(Asset->GetOutermost(), Asset,
		*FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
}
}

int32 UNewGameHandlerAssetsCommandlet::Main(const FString& Params)
{
	auto* BP = LoadObject<UBlueprint>(nullptr, TEXT("/Game/System/Map/MainMenu/MapTransitionHandler/BP_MapTransitionHandler_NewGame"));
	auto* ControllerBP = LoadObject<UBlueprint>(nullptr, TEXT("/Game/System/Map/MainMenu/BP_MainMenuPlayerController"));
	if (!BP || !ControllerBP || !BP->ParentClass->IsChildOf(UMTS_MapTransitionHandler::StaticClass())) { return 1; }
	if (!Backup(BP) || !Backup(ControllerBP)) { return 1; }
	UEdGraph* Graph = BP->UbergraphPages.IsEmpty() ? nullptr : BP->UbergraphPages[0];
	if (!Graph)
	{
		Graph = FBlueprintEditorUtils::CreateNewGraph(BP, TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddUbergraphPage(BP, Graph);
	}
	UK2Node_Event* Event = nullptr;
	for (UEdGraph* Page : BP->UbergraphPages)
	{
		for (UEdGraphNode* Node : Page->Nodes)
		{
			if (auto* Existing = Cast<UK2Node_Event>(Node))
			{
				if (Existing->EventReference.GetMemberName() == GET_FUNCTION_NAME_CHECKED(UMTS_MapTransitionHandler, OnMainMapLoaded)) { Event = Existing; }
			}
		}
	}
	if (!Event)
	{
		FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
		Event = Creator.CreateNode();
		Event->EventReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UMTS_MapTransitionHandler, OnMainMapLoaded), UMTS_MapTransitionHandler::StaticClass());
		Event->bOverrideFunction = true;
		Event->NodePosY = 600;
		Creator.Finalize();
	}

    Graph = Event->GetGraph();
    UK2Node_Event* Ready = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (auto* Existing = Cast<UK2Node_Event>(Node))
        {
            if (Existing->EventReference.GetMemberName() == TEXT("OnSubMapsLoaded")) { Ready = Existing; }
        }
    }
    // Migrate only the two initialization event chains and their pure dependencies.
    TSet<UEdGraphNode*> Remove;
    TFunction<void(UEdGraphNode*)> Gather = [&](UEdGraphNode* Node)
    {
        if (!Node || Cast<UK2Node_Event>(Node) || Remove.Contains(Node)) { return; }
        Remove.Add(Node);
        for (auto* Pin : Node->Pins)
        {
            if (Pin->Direction == EGPD_Input || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                for (auto* Linked : Pin->LinkedTo) { Gather(Linked->GetOwningNode()); }
            }
        }
    };
    for (auto* Root : {Event, Ready})
    {
        if (Root) { for (auto* Pin : Root->FindPinChecked(UEdGraphSchema_K2::PN_Then)->LinkedTo) { Gather(Pin->GetOwningNode()); } }
    }
    for (auto* Node : Remove) { FBlueprintEditorUtils::RemoveNode(BP, Node, true); }
    if (!Ready)
    {
        FGraphNodeCreator<UK2Node_Event> Creator(*Graph); Ready=Creator.CreateNode();
        Ready->EventReference.SetExternalMember(TEXT("OnSubMapsLoaded"),UMTS_MapTransitionHandler::StaticClass());
        Ready->bOverrideFunction=true; Creator.Finalize();
    }
    Event->NodePosX=0; Event->NodePosY=600; Ready->NodePosX=0; Ready->NodePosY=1600;
    const FName ConfigName(TEXT("SubMapConfigs"));
    FEdGraphPinType ArrayType;
    ArrayType.PinCategory=UEdGraphSchema_K2::PC_Struct;
    ArrayType.PinSubCategoryObject=FMTS_SubMapLoadConfig::StaticStruct();
    ArrayType.ContainerType=EPinContainerType::Array;
    if (FBlueprintEditorUtils::FindNewVariableIndex(BP,ConfigName)==INDEX_NONE)
    {
        FBlueprintEditorUtils::AddMemberVariable(BP,ConfigName,ArrayType);
        FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(BP,ConfigName,false);
    }
    const auto* Schema=GetDefault<UEdGraphSchema_K2>();
    auto Link=[&](UEdGraphPin* A,UEdGraphPin* B){ check(Schema->TryCreateConnection(A,B)); };
    auto Call=[&](UClass* Class,FName Function,int32 X,int32 Y)
    {
        FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph); auto* Node=Creator.CreateNode();
        Node->SetFromFunction(Class->FindFunctionByName(Function)); Node->NodePosX=X; Node->NodePosY=Y; Creator.Finalize(); return Node;
    };
    auto Branch=[&](int32 X,int32 Y)
    {
        FGraphNodeCreator<UK2Node_IfThenElse> Creator(*Graph);auto* Node=Creator.CreateNode();Node->NodePosX=X;Node->NodePosY=Y;Creator.Finalize();return Node;
    };
    auto* Load=Call(UMTS_MapTransitionSubsystem::StaticClass(),TEXT("LoadSubMaps"),450,600);
    Link(Event->FindPinChecked(UEdGraphSchema_K2::PN_Then),Load->GetExecPin());
    Link(Event->FindPinChecked(TEXT("TransitionSubsystem")),Load->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    FGraphNodeCreator<UK2Node_VariableGet> GetCreator(*Graph);auto* Get=GetCreator.CreateNode();
    Get->VariableReference.SetSelfMember(ConfigName);Get->NodePosX=0;Get->NodePosY=950;GetCreator.Finalize();
    Link(Get->FindPinChecked(ConfigName),Load->FindPinChecked(TEXT("Configs")));
    Load->NodeComment=TEXT("Edit SubMapConfigs in Class Defaults: ID, weight, map, location, rotation.");Load->bCommentBubbleVisible=true;
    auto* OK=Branch(900,600);Link(Load->GetThenPin(),OK->GetExecPin());Link(Load->GetReturnValuePin(),OK->GetConditionPin());
    auto* Fail=Call(UMTS_MapTransitionSubsystem::StaticClass(),TEXT("ReportMapInitializationFailure"),1250,850);
    Link(OK->GetElsePin(),Fail->GetExecPin());Link(Event->FindPinChecked(TEXT("TransitionSubsystem")),Fail->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    Link(Load->FindPinChecked(TEXT("OutError")),Fail->FindPinChecked(TEXT("Error")));
    auto* GetMode=Call(UGameplayStatics::StaticClass(),TEXT("GetGameMode"),0,1900);
    FGraphNodeCreator<UK2Node_Self> SelfCreator(*Graph);auto* Self=SelfCreator.CreateNode();Self->NodePosX=0;Self->NodePosY=2150;SelfCreator.Finalize();
    Link(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self),GetMode->FindPinChecked(TEXT("WorldContextObject")));
    FGraphNodeCreator<UK2Node_DynamicCast> CastCreator(*Graph);auto* CastNode=CastCreator.CreateNode();CastNode->TargetType=AGameMainMapGameMode::StaticClass();CastNode->NodePosX=450;CastNode->NodePosY=1600;CastCreator.Finalize();
    Link(Ready->FindPinChecked(UEdGraphSchema_K2::PN_Then),CastNode->GetExecPin());Link(GetMode->GetReturnValuePin(),CastNode->GetCastSourcePin());
    auto* Activate=Call(AGameMainMapGameMode::StaticClass(),TEXT("ActivateLoadedMap"),850,1600);
    Link(CastNode->GetValidCastPin(),Activate->GetExecPin());Link(CastNode->GetCastResultPin(),Activate->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    Schema->TrySetDefaultValue(*Activate->FindPinChecked(TEXT("MapID")),TEXT("Base"));
    Schema->TrySetDefaultValue(*Activate->FindPinChecked(TEXT("EntryTransform")),TEXT("(Rotation=(X=0,Y=0,Z=0,W=1),Translation=(X=0,Y=0,Z=200),Scale3D=(X=1,Y=1,Z=1))"));
    auto* ActiveOK=Branch(1250,1600);Link(Activate->GetThenPin(),ActiveOK->GetExecPin());Link(Activate->GetReturnValuePin(),ActiveOK->GetConditionPin());
    auto* Notify=Call(UMTS_MapTransitionSubsystem::StaticClass(),TEXT("NotifyMapLoadCompleted"),1650,1600);
    Link(ActiveOK->GetThenPin(),Notify->GetExecPin());Link(Ready->FindPinChecked(TEXT("TransitionSubsystem")),Notify->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    Schema->TrySetDefaultValue(*Notify->FindPinChecked(TEXT("ReadySource")),TEXT("GameMainMap.BaseReady"));
    auto* ActivateFailed=Call(UMTS_MapTransitionSubsystem::StaticClass(),TEXT("ReportMapInitializationFailure"),1650,1950);
    Link(CastNode->GetInvalidCastPin(),ActivateFailed->GetExecPin());Link(ActiveOK->GetElsePin(),ActivateFailed->GetExecPin());
    Link(Ready->FindPinChecked(TEXT("TransitionSubsystem")),ActivateFailed->FindPinChecked(UEdGraphSchema_K2::PN_Self));
    ActivateFailed->FindPinChecked(TEXT("Error"))->DefaultTextValue=FText::FromString(TEXT("Cannot activate the loaded Base map."));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP);
	if (BP->Status != BS_Error) { CastChecked<UMTS_MapTransitionHandler>(BP->GeneratedClass->GetDefaultObject())->bInitializeSubMapsAfterMainMap=true; }

    if (BP->Status==BS_Error) { return 1; }
    auto* Property=FindFProperty<FArrayProperty>(BP->GeneratedClass,ConfigName);
    if (!Property) { return 1; }
    auto* Configs=Property->ContainerPtrToValuePtr<TArray<FMTS_SubMapLoadConfig>>(BP->GeneratedClass->GetDefaultObject());
    if (Configs->IsEmpty())
    {
        FMTS_SubMapLoadConfig Config;Config.MapID=TEXT("Base");
        Config.MapAsset=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/System/Map/BaseMap/L_BaseTemplate.L_BaseTemplate")));
        Configs->Add(Config);
    }
    if (!Save(BP)) { return 1; }

	auto* Defaults = Cast<AMainMenuPlayerController>(ControllerBP->GeneratedClass->GetDefaultObject());
	if (!Defaults) { return 1; }
	Defaults->NewGameTransitionHandlerClass = BP->GeneratedClass;
	if (!Save(ControllerBP)) { return 1; }
	UE_LOG(LogTemp, Display, TEXT("NEW_GAME_HANDLER_OK: explicit Blueprint configure/load/notify flow"));
	return 0;
}
