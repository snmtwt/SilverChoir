#include "MapNamingMigrationCommandlet.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "UObject/SavePackage.h"
#include "EdGraph/EdGraph.h"
#include "Serialization/ArchiveUObject.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_BaseMCDelegate.h"
#include "HAL/FileManager.h"
#include "Map/BaseMap/BaseMapWidget.h"
#include "Map/BaseMap/SceneUI/BaseSceneWidget.h"

namespace
{
int32 RepairBaseSceneTypes(bool bVerifyOnly)
{
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FAssetData> Assets; Registry.GetAssetsByPath(TEXT("/Game/System/Map/BaseMap"),Assets,true);
    int32 Errors=0, Count=0;
    const FString BackupRoot=FPaths::ProjectSavedDir()/TEXT("SceneTypeRepairBackups")/FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
    for (const auto& Asset:Assets)
    {
        const FName Kind=Asset.AssetClassPath.GetAssetName();
        if (Asset.IsRedirector() || (Kind!=TEXT("Blueprint") && Kind!=TEXT("WidgetBlueprint"))) continue;
        const FString File=FPackageName::LongPackageNameToFilename(Asset.PackageName.ToString(),FPackageName::GetAssetPackageExtension());
        if (!bVerifyOnly)
        {
            const FString Backup=BackupRoot/Asset.PackageName.ToString().RightChop(6)+FPackageName::GetAssetPackageExtension();
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup),true);
            if (IFileManager::Get().Copy(*Backup,*File)!=COPY_OK) return 1;
        }
        auto* BP=Cast<UBlueprint>(Asset.GetAsset());
        if (!BP) { ++Errors; continue; }
        if (!bVerifyOnly)
        {
            if (Asset.PackageName==TEXT("/Game/System/Map/BaseMap/UI/BP_BaseMapWidget")) BP->ParentClass=UBaseMapWidget::StaticClass();
            TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
            for (auto* Graph:Graphs) for (UEdGraphNode* Node:Graph->Nodes)
            {
                if (auto* Call=Cast<UK2Node_CallFunction>(Node))
                {
                    const FName Name=Call->FunctionReference.GetMemberName();
                    UClass* Owner=nullptr;
                    if (Name==TEXT("CreateSceneUIByTag") || Name==TEXT("GetCurrentSceneUI") || Name==TEXT("ShowSceneUI") || Name==TEXT("UnloadCurrentSceneUI")) Owner=UBaseMapWidget::StaticClass();
                    if (Name==TEXT("NotifyLoadCompleted") || Name==TEXT("NotifyUnloadCompleted")) Owner=UBaseSceneWidget::StaticClass();
                    if (Owner)
                    {
                        Call->FunctionReference.SetExternalMember(Name,Owner);
                        if (auto* Pin=Call->FindPin(TEXT("self"))) Pin->PinType.PinSubCategoryObject=Owner;
                        if (Name==TEXT("CreateSceneUIByTag") || Name==TEXT("GetCurrentSceneUI") || Name==TEXT("ShowSceneUI"))
                            if (auto* Pin=Call->FindPin(TEXT("ReturnValue"))) Pin->PinType.PinSubCategoryObject=UBaseSceneWidget::StaticClass();
                    }
                }
                if (auto* Delegate=Cast<UK2Node_BaseMCDelegate>(Node))
                {
                    const FName Name=Delegate->GetPropertyName();
                    if (Name==TEXT("OnLoadCompleted") || Name==TEXT("OnUnloadCompleted"))
                    {
                        Delegate->SetFromProperty(FindFProperty<FMulticastDelegateProperty>(UBaseSceneWidget::StaticClass(),Name),false,UBaseSceneWidget::StaticClass());
                        if (auto* Pin=Delegate->FindPin(TEXT("self"))) Pin->PinType.PinSubCategoryObject=UBaseSceneWidget::StaticClass();
                    }
                }
            }
            FBlueprintEditorUtils::RefreshAllNodes(BP);
        }
        FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status==BS_Error) { ++Errors; UE_LOG(LogTemp,Error,TEXT("SCENE_TYPE_FAILED %s"),*BP->GetPathName()); continue; }
        if (!bVerifyOnly)
        {
            FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
            if (!UPackage::SavePackage(BP->GetOutermost(),BP,*File,Args)) ++Errors;
        }
        UE_LOG(LogTemp,Display,TEXT("SCENE_TYPE_CHECKED %s"),*BP->GetPathName()); ++Count;
    }
    UE_LOG(LogTemp,Display,TEXT("SCENE_TYPE_%s count=%d errors=%d"),bVerifyOnly?TEXT("COLD_VERIFY"):TEXT("REPAIR"),Count,Errors);
    return Errors?1:0;
}
int32 RefreshSceneWorldContext()
{
	UClass* SceneClass=LoadClass<UObject>(nullptr,TEXT("/Script/SceneManagementSystem.SMS_SceneBase"));
	if (!SceneClass || !SceneClass->GetDefaultObject()->ImplementsGetWorld() || SceneClass->HasMetaDataHierarchical(TEXT("ShowWorldContextPin"))) return 1;
	auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	TArray<FAssetData> Assets; Registry.GetAssetsByPath(TEXT("/Game"),Assets,true);
	int32 Count=0,ContextPins=0;
	for (const auto& Asset:Assets)
	{
		if (Asset.AssetClassPath.GetAssetName()!=TEXT("Blueprint")) continue;
		auto* BP=Cast<UBlueprint>(Asset.GetAsset());
		if (!BP || !BP->ParentClass || !BP->ParentClass->IsChildOf(SceneClass)) continue;
		FBlueprintEditorUtils::RefreshAllNodes(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (BP->Status==BS_Error || !BP->GeneratedClass->GetDefaultObject()->ImplementsGetWorld()) return 1;
		TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
		for (auto* Graph:Graphs) for (UEdGraphNode* Node:Graph->Nodes)
		{
			auto* Call=Cast<UK2Node_CallFunction>(Node);
			UFunction* Function=Call?Call->GetTargetFunction():nullptr;
			if (!Function || !Function->HasMetaData(TEXT("WorldContext"))) continue;
			if (auto* Pin=Call->FindPin(*Function->GetMetaData(TEXT("WorldContext"))))
			{
				if (!Pin->bHidden) { UE_LOG(LogTemp,Error,TEXT("Visible world pin in %s: %s"),*BP->GetPathName(),*Function->GetName()); return 1; }
				++ContextPins;
			}
		}
		FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
		const FString File=FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(BP->GetOutermost(),BP,*File,Args)) return 1;
		++Count;
	}
	UE_LOG(LogTemp,Display,TEXT("SCENE_WORLD_CONTEXT_OK: %d blueprints, %d automatic context pins"),Count,ContextPins);
	return 0;
}

FString MapName(FString Name)
{
	return Name.Replace(TEXT("SceneMap"),TEXT("MapAsset")).Replace(TEXT("Scene"),TEXT("Map"));
}

int32 FinalizeAssets()
{
	auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	TArray<FAssetData> Assets; Registry.GetAssetsByPath(TEXT("/Game"),Assets,true);
	int32 Errors=0,Count=0;
	for (const auto& Asset:Assets)
	{
		if (Asset.IsRedirector()) continue;
		auto* Object=Asset.GetAsset(); if (!Object) { ++Errors; continue; }
		auto* BP=Cast<UBlueprint>(Object); if (!BP) continue;
		TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
		for (auto* Graph:Graphs)
		{
			for (UEdGraphNode* Node:Graph->Nodes)
			{
				for (auto* Pin:Node->Pins)
				{
					if (Pin->DefaultValue==TEXT("GameMainScene.BaseReady")) Pin->DefaultValue=TEXT("GameMainMap.BaseReady");
					if (Pin->bOrphanedPin) { ++Errors; UE_LOG(LogTemp,Error,TEXT("Orphan pin %s in %s"),*Pin->PinName.ToString(),*BP->GetPathName()); }
				}
			}
		}
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (BP->Status==BS_Error) { ++Errors; continue; }
		FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
		const FString File=FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(BP->GetOutermost(),BP,*File,Args)) ++Errors;
		++Count;
	}
	UE_LOG(LogTemp,Display,TEXT("MAP_NAMING_VERIFY_%s %d blueprints"),Errors?TEXT("FAILED"):TEXT("OK"),Count);
	return Errors?1:0;
}

int32 MigrateAssets()
{
	auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.SearchAllAssets(true);
	TArray<FAssetData> Assets; Registry.GetAssetsByPath(TEXT("/Game"),Assets,true);
	TArray<UObject*> Loaded;
	for (const auto& Asset:Assets)
	{
		if (Asset.IsRedirector()) continue;
		auto* Object=Asset.GetAsset();
		if (!Object) { UE_LOG(LogTemp,Error,TEXT("Cannot load %s"),*Asset.GetObjectPathString()); return 1; }
		Object->AddToRoot(); Loaded.Add(Object);
	}
	TArray<FString> Moves;
	if (!FFileHelper::LoadFileToStringArray(Moves,*(FPaths::ProjectSavedDir()/TEXT("MapNamingAssets.txt")))) return 1;
	TArray<FAssetRenameData> Renames;
	TMap<FString,FString> PathMoves;
	for (const FString& Move:Moves)
	{
		FString Old,New; if (!Move.Split(TEXT("|"),&Old,&New)) return 1;
		auto* Object=LoadObject<UObject>(nullptr,*(Old+TEXT(".")+FPackageName::GetShortName(Old)));
		if (!Object) return 1;
		Renames.Emplace(Object,FPackageName::GetLongPackagePath(New),FPackageName::GetShortName(New));
		PathMoves.Add(Old+TEXT(".")+FPackageName::GetShortName(Old),New+TEXT(".")+FPackageName::GetShortName(New));
	}
	// The source and project INI paths were migrated before this step. Also update
	// editor-session native defaults loaded from Saved/Config to the same new paths.
	class FUpdateDefaults : public FArchiveUObject
	{
	public:
		const TMap<FString,FString>& Moves;
		explicit FUpdateDefaults(const TMap<FString,FString>& In):Moves(In) { ArIsObjectReferenceCollector=true; ArIsModifyingWeakAndStrongReferences=true; SetIsSaving(true); }
		virtual FArchive& operator<<(FSoftObjectPtr& Value) override
		{
			FSoftObjectPath Path=Value.ToSoftObjectPath(); *this << Path; Value=FSoftObjectPtr(Path); return *this;
		}
		virtual FArchive& operator<<(FSoftObjectPath& Path) override
		{
			if (const FString* New=Moves.Find(Path.GetAssetPathString()))
			{ Path=FSoftObjectPath(*New+ (Path.GetSubPathString().IsEmpty()?FString():TEXT(":")+Path.GetSubPathString())); }
			return *this;
		}
	} UpdateDefaults(PathMoves);
	for (TObjectIterator<UClass> It; It; ++It)
		if (!It->ClassGeneratedBy && It->GetDefaultObject(false)) It->GetDefaultObject(false)->Serialize(UpdateDefaults);
	if (!FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().RenameAssets(Renames))
	{
		UE_LOG(LogTemp,Error,TEXT("Asset rename failed")); return 1;
	}
	int32 Errors=0;
	for (auto* Object:Loaded)
	{
		if (auto* BP=Cast<UBlueprint>(Object))
		{
			TArray<FName> Names;
			for (const auto& Variable:BP->NewVariables) Names.Add(Variable.VarName);
			for (FName Name:Names)
			{
				const FName New(*MapName(Name.ToString()));
				if (Name!=New) FBlueprintEditorUtils::RenameMemberVariable(BP,Name,New);
			}
			FBlueprintEditorUtils::RefreshAllNodes(BP);
			FKismetEditorUtilities::CompileBlueprint(BP);
			if (BP->Status==BS_Error) { ++Errors; UE_LOG(LogTemp,Error,TEXT("Blueprint failed: %s"),*BP->GetPathName()); }
		}
	}
	if (Errors) return 1;
	TMap<UPackage*,UObject*> Packages;
	for (auto* Object:Loaded) Packages.Add(Object->GetOutermost(),Object);
	for (const auto& Entry:Packages)
	{
		UPackage* Package=Entry.Key;
		const bool IsMap=Package->ContainsMap();
		const FString File=FPackageName::LongPackageNameToFilename(Package->GetName(),IsMap?FPackageName::GetMapPackageExtension():FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
		// Preserve the asset's editor-only subobjects (including material expression graphs).
		if (!UPackage::SavePackage(Package,Entry.Value,*File,Args)) ++Errors;
	}
	UE_LOG(LogTemp,Display,TEXT("MAP_NAMING_MIGRATION_%s: %d renamed, %d packages saved"),Errors?TEXT("FAILED"):TEXT("OK"),Renames.Num(),Packages.Num());
	return Errors?1:0;
}
}

int32 UMapNamingMigrationCommandlet::Main(const FString& Params)
{
    if (FParse::Param(*Params,TEXT("RepairBaseSceneTypes"))) return RepairBaseSceneTypes(false);
    if (FParse::Param(*Params,TEXT("VerifyBaseSceneTypes"))) return RepairBaseSceneTypes(true);
	if (FParse::Param(*Params,TEXT("RefreshSceneContext"))) return RefreshSceneWorldContext();
	if (FParse::Param(*Params,TEXT("Migrate"))) return MigrateAssets();
	if (FParse::Param(*Params,TEXT("Finalize"))) return FinalizeAssets();
	TArray<FString> Lines;
	for (TObjectIterator<UField> It; It; ++It)
	{
		UField* Field=*It;
		const FString Package=Field->GetOutermost()->GetName();
		if (Package!=TEXT("/Script/SilverChoir") && Package!=TEXT("/Script/MapTransitionSystem")) { continue; }
		FString Kind;
		if (Cast<UClass>(Field)) Kind=TEXT("Class");
		else if (Cast<UScriptStruct>(Field)) Kind=TEXT("Struct");
		else if (Cast<UEnum>(Field)) Kind=TEXT("Enum");
		else if (Cast<UFunction>(Field)) Kind=TEXT("Function");
		if (!Kind.IsEmpty()) Lines.Add(Kind+TEXT("|")+Field->GetPathName());
		if (auto* Struct=Cast<UStruct>(Field))
		{
			for (TFieldIterator<FProperty> Prop(Struct,EFieldIterationFlags::None); Prop; ++Prop)
				Lines.Add(TEXT("Property|")+Prop->GetPathName());
		}
	}
	Lines.Sort();
	if (!FFileHelper::SaveStringArrayToFile(Lines,*(FPaths::ProjectSavedDir()/TEXT("MapNamingReflection.txt")))) return 1;
	UE_LOG(LogTemp,Display,TEXT("MAP_NAMING_SNAPSHOT_OK %d"),Lines.Num());
	return 0;
}
