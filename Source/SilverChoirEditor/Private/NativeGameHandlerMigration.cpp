#include "MainMapBlueprintBuilder.h"
#include "SubSystem/GameMapTransitionSystem/GameMapSubMapHandler.h"
#include "SubSystem/PlayerUnitSubSystem/UnitSquadSpawner.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

// This intentionally has a separate command-line switch from the historical asset builders.
// -NativeGameHandler -Inspect is read-only; only -NativeGameHandler -Apply saves assets.
namespace NativeGameHandlerMigration
{
const TCHAR* HandlerPath = TEXT("/Game/System/Map/BattleMap/T5/BP_SubMapHandler_T5");
const FName OldRegister(TEXT("注册小队生成器"));
const TArray<FName> ConfigNames = {TEXT("LoadRequest"), TEXT("LocalEntryTransform"), TEXT("OverviewSceneTag"), TEXT("SquadIds")};
const FName OldSpawners(TEXT("Squad Spawner List"));

struct FSnapshot
{
    UBlueprint* Blueprint = nullptr;
    TMap<FName, FString> Values;
    TArray<FBPVariableDescription> Variables;
};

struct FContext
{
    UBlueprint* Handler = nullptr;
    TArray<UBlueprint*> Blueprints;
    TMap<UPackage*, UObject*> SaveAssets;
    TArray<FSnapshot> Snapshots;
    TSet<UBlueprint*> Affected;
    TSet<UBlueprint*> Related;
    TSet<FGuid> LegacyMemberGuids;
    FString Report;
    FString BackupRoot;
    bool bOK = true;

    bool Check(bool bCondition, const FString& Message)
    {
        if (!bCondition)
        {
            Report += TEXT("ERROR ") + Message + TEXT("\n");
            UE_LOG(LogTemp, Error, TEXT("NATIVE_GAME_HANDLER %s"), *Message);
            bOK = false;
        }
        return bCondition;
    }

    bool WriteReport(const TCHAR* Name)
    {
        const FString File = FPaths::ProjectSavedDir() / TEXT("SubMapHandlerLifecycle/NativeGameHandler") / Name;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        return FFileHelper::SaveStringToFile(Report, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
};

FString CanonicalGraph(UEdGraph* Graph)
{
    // Exact pin values and edges from the inspected 2026-10-04 graph. Titles are localized,
    // so they are deliberately excluded. Comments and layout are preserved in the backup.
    FString Result;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        Result += FString::Printf(TEXT("NODE %s.%s\n"), *Graph->GetName(), *Node->GetName());
        for (UEdGraphPin* Pin : Node->Pins)
        {
            FString Links;
            for (UEdGraphPin* Link : Pin->LinkedTo)
                Links += Link->GetOwningNode()->GetName() + TEXT(".") + Link->PinName.ToString() + TEXT(" ");
            FString Line = FString::Printf(TEXT(" PIN %s default=%s object=%s links=%s"),
                *Pin->PinName.ToString(), *Pin->DefaultValue, *GetPathNameSafe(Pin->DefaultObject), *Links);
            Result += Line.TrimEnd() + TEXT("\n");
        }
    }
    return Result;
}

FString GraphHash(UEdGraph* Graph)
{
    FTCHARToUTF8 Bytes(*CanonicalGraph(Graph));
    FSHAHash Hash;
    FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash.Hash);
    return Hash.ToString();
}

bool IsHandlerClass(const FContext& C, const UClass* Class)
{
    for (const UClass* It = Class; It; It = It->GetSuperClass())
        if (It->ClassGeneratedBy == C.Handler) return true;
    return false;
}

bool IsHandlerBlueprint(const FContext& C, UBlueprint* BP)
{
    return BP == C.Handler || IsHandlerClass(C, BP->ParentClass);
}

bool RefersToHandler(const FContext& C, UBlueprint* BP, UEdGraphNode* Node, const FMemberReference& Ref)
{
    if (Ref.GetMemberGuid().IsValid() && C.LegacyMemberGuids.Contains(Ref.GetMemberGuid())) return true;
    if (Ref.GetMemberParentClass() == UGameMapSubMapHandler::StaticClass()) return true;
    if (IsHandlerClass(C, Ref.GetMemberParentClass())) return true;
    if (Ref.IsSelfContext() && IsHandlerBlueprint(C, BP)) return true;
    if (UEdGraphPin* Self = Node->FindPin(UEdGraphSchema_K2::PN_Self))
    {
        if (IsHandlerClass(C, Cast<UClass>(Self->PinType.PinSubCategoryObject.Get()))) return true;
        for (UEdGraphPin* Link : Self->LinkedTo)
            if (IsHandlerClass(C, Cast<UClass>(Link->PinType.PinSubCategoryObject.Get()))) return true;
    }
    return false;
}

bool IsMigratedMember(FName Name)
{
    return ConfigNames.Contains(Name) || Name == OldSpawners || Name == OldRegister ||
        Name == TEXT("PrepareTileEntry") || Name == TEXT("RegisterSquadSpawner");
}

void DumpBlueprint(FContext& C, UBlueprint* BP)
{
    C.Report += FString::Printf(TEXT("ASSET %s parent=%s level=%d status=%d\n"), *BP->GetPathName(),
        *GetPathNameSafe(BP->ParentClass), BP->IsA<ULevelScriptBlueprint>(), static_cast<int32>(BP->Status));
    for (const FBPVariableDescription& Var : BP->NewVariables)
    {
        FString CompleteDescription;
        FBPVariableDescription::StaticStruct()->ExportText(CompleteDescription, &Var, nullptr, BP, PPF_None, nullptr);
        C.Report += TEXT("VAR_SNAPSHOT ") + CompleteDescription + TEXT("\n");
        C.Report += FString::Printf(TEXT("VAR %s guid=%s category=%s type=%s/%s object=%s container=%d flags=%llu default=%s\n"),
            *Var.VarName.ToString(), *Var.VarGuid.ToString(), *Var.Category.ToString(), *Var.VarType.PinCategory.ToString(),
            *Var.VarType.PinSubCategory.ToString(), *GetPathNameSafe(Var.VarType.PinSubCategoryObject.Get()),
            static_cast<int32>(Var.VarType.ContainerType), static_cast<unsigned long long>(Var.PropertyFlags), *Var.DefaultValue);
        for (const FBPVariableMetaDataEntry& Meta : Var.MetaDataArray)
            C.Report += FString::Printf(TEXT(" META %s=%s\n"), *Meta.DataKey.ToString(), *Meta.DataValue);
    }
    TArray<UEdGraph*> Graphs;
    BP->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        C.Report += FString::Printf(TEXT("GRAPH %s nodes=%d hash=%s\n"), *Graph->GetName(), Graph->Nodes.Num(), *GraphHash(Graph));
        C.Report += CanonicalGraph(Graph);
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            C.Report += FString::Printf(TEXT(" DETAIL %s.%s class=%s title=%s\n"), *Graph->GetName(), *Node->GetName(),
                *Node->GetClass()->GetPathName(), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
            for (TFieldIterator<FStructProperty> It(Node->GetClass()); It; ++It)
                if (It->Struct == FMemberReference::StaticStruct())
                {
                    const FMemberReference* Ref = It->ContainerPtrToValuePtr<FMemberReference>(Node);
                    C.Report += FString::Printf(TEXT(" MEMBER %s name=%s parent=%s self=%d guid=%s\n"), *It->GetName(),
                        *Ref->GetMemberName().ToString(), *GetPathNameSafe(Ref->GetMemberParentClass()), Ref->IsSelfContext(), *Ref->GetMemberGuid().ToString());
                }
        }
    }
}

bool Snapshot(FContext& C, UBlueprint* BP)
{
    if (!C.Check(BP->GeneratedClass != nullptr, TEXT("No generated class: ") + BP->GetPathName())) return false;
    FSnapshot& Saved = C.Snapshots.AddDefaulted_GetRef();
    Saved.Blueprint = BP;
    Saved.Variables = BP->NewVariables;
    UObject* CDO = BP->GeneratedClass->GetDefaultObject();
    for (FName Name : ConfigNames)
    {
        FProperty* Property = FindFProperty<FProperty>(BP->GeneratedClass, Name);
        if (!C.Check(Property != nullptr, TEXT("Missing CDO config ") + BP->GetPathName() + TEXT(".") + Name.ToString())) return false;
        FProperty* NativeProperty = FindFProperty<FProperty>(UGameMapSubMapHandler::StaticClass(), Name);
        if (!C.Check(NativeProperty && Property->SameType(NativeProperty), TEXT("Native config type does not match authored property: ") + Name.ToString())) return false;
        FString Value;
        Property->ExportText_InContainer(0, Value, CDO, nullptr, CDO, PPF_None);
        Saved.Values.Add(Name, Value);
        C.Report += FString::Printf(TEXT("CDO_CONFIG %s.%s type=%s value=%s\n"), *BP->GetPathName(), *Name.ToString(), *Property->GetCPPType(), *Value);
    }
    return true;
}

void Scan(FContext& C)
{
    for (const FBPVariableDescription& Var : C.Handler->NewVariables)
        if (Var.VarGuid.IsValid()) C.LegacyMemberGuids.Add(Var.VarGuid);
    for (UEdGraph* Graph : C.Handler->FunctionGraphs)
        if (Graph->GraphGuid.IsValid()) C.LegacyMemberGuids.Add(Graph->GraphGuid);
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FName> Queue = {FName(HandlerPath)};
    TSet<FName> Visited;
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const FName Package = Queue[Index];
        if (Visited.Contains(Package)) continue;
        Visited.Add(Package);
        TArray<FName> Referencers;
        Registry.GetReferencers(Package, Referencers);
        for (FName Ref : Referencers)
        {
            C.Report += FString::Printf(TEXT("DEPENDENCY %s <- %s\n"), *Package.ToString(), *Ref.ToString());
            if (Ref.ToString().StartsWith(TEXT("/Game/")) && !Visited.Contains(Ref)) Queue.AddUnique(Ref);
        }
    }
    TArray<FAssetData> Assets;
    Registry.GetAssetsByPath(TEXT("/Game"), Assets, true);
    for (const FAssetData& Asset : Assets)
    {
        if (Asset.IsRedirector()) continue;
        const FString Kind = Asset.AssetClassPath.GetAssetName().ToString();
        if (Kind.EndsWith(TEXT("Blueprint")))
        {
            UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
            if (!C.Check(BP != nullptr, TEXT("Cannot load Blueprint ") + Asset.GetObjectPathString())) continue;
            C.Blueprints.AddUnique(BP);
            C.SaveAssets.Add(BP->GetOutermost(), BP);
        }
        else if (Kind == TEXT("World"))
        {
            UWorld* World = Cast<UWorld>(Asset.GetAsset());
            if (!C.Check(World != nullptr, TEXT("Cannot load map ") + Asset.GetObjectPathString())) continue;
            C.SaveAssets.Add(World->GetOutermost(), World);
            TArray<ULevel*> Levels;
            for (ULevel* Level : World->GetLevels()) Levels.AddUnique(Level);
            if (World->PersistentLevel) Levels.AddUnique(World->PersistentLevel);
            C.Report += FString::Printf(TEXT("MAP %s levels=%d\n"), *World->GetPathName(), Levels.Num());
            for (ULevel* Level : Levels)
            {
                // The argument means bDontCreate, not bCreate. Never create a level script during inspection.
                ULevelScriptBlueprint* LevelBP = Level->GetLevelScriptBlueprint(true);
                C.Report += FString::Printf(TEXT("LEVEL_SCRIPT %s blueprint=%s\n"), *Level->GetPathName(), *GetPathNameSafe(LevelBP));
                if (LevelBP)
                {
                    C.Blueprints.AddUnique(LevelBP);
                    UWorld* LevelWorld = Cast<UWorld>(Level->GetOuter());
                    C.SaveAssets.Add(LevelBP->GetOutermost(), LevelWorld ? LevelWorld : World);
                }
            }
        }
    }
    C.Blueprints.AddUnique(C.Handler);
    C.SaveAssets.Add(C.Handler->GetOutermost(), C.Handler);
    for (UBlueprint* BP : C.Blueprints)
    {
        C.Report += TEXT("SCANNED ") + BP->GetPathName() + TEXT("\n");
        if (IsHandlerBlueprint(C, BP))
        {
            C.Related.Add(BP);
            Snapshot(C, BP);
        }
        TArray<UEdGraph*> Graphs;
        BP->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
            for (TFieldIterator<FStructProperty> It(Node->GetClass()); It; ++It)
                if (It->Struct == FMemberReference::StaticStruct())
                {
                    const FMemberReference* Ref = It->ContainerPtrToValuePtr<FMemberReference>(Node);
                    if (IsMigratedMember(Ref->GetMemberName()) && RefersToHandler(C, BP, Node, *Ref))
                    {
                        C.Related.Add(BP);
                        C.Report += FString::Printf(TEXT("HANDLER_REFERENCE %s %s.%s %s owner=%s\n"), *BP->GetPathName(),
                            *Graph->GetName(), *Node->GetName(), *Ref->GetMemberName().ToString(), *GetPathNameSafe(Ref->GetMemberParentClass()));
                    }
                }
        if (BP == C.Handler || C.Related.Contains(BP) || BP->IsA<ULevelScriptBlueprint>()) DumpBlueprint(C, BP);
    }
    C.Report += FString::Printf(TEXT("SCAN_COMPLETE blueprints=%d related=%d\n"), C.Blueprints.Num(), C.Related.Num());
}

bool CheckReviewedGraphs(FContext& C)
{
    if (C.Handler->ParentClass == UGameMapSubMapHandler::StaticClass())
    {
        C.Check(C.Handler->NewVariables.IsEmpty(), TEXT("Migrated handler still declares Blueprint variables"));
        TArray<UEdGraph*> Graphs;
        C.Handler->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
            C.Check(Graph->Nodes.IsEmpty(), TEXT("Migrated handler has unexpected executable graph: ") + Graph->GetName());
        return C.bOK;
    }
    const TMap<FName, FString> Expected = {
        {TEXT("PrepareTileEntry"), TEXT("E81AF7F3162C3FD83E974764B926C95D1DFACDA6")},
        {OldRegister, TEXT("03FF68F6F4D5A7CBCB31515A264209936AD3E9D3")},
        {TEXT("SubMapLifecycle"), TEXT("CA6570C14447F15F6834D43C7DEAB2490E718176")}
    };
    const TMap<FString, FName> ExpectedCalls = {
        {TEXT("PrepareTileEntry.K2Node_CallFunction_0"), TEXT("WaitForEntryScene")},
        {TEXT("PrepareTileEntry.K2Node_CallFunction_1"), TEXT("FailLoading")},
        {TEXT("PrepareTileEntry.K2Node_CallFunction_2"), TEXT("PrintText")},
        {TEXT("注册小队生成器.K2Node_CallArrayFunction_0"), TEXT("Array_Add")},
        {TEXT("注册小队生成器.K2Node_CallFunction_0"), TEXT("PrintString")},
        {TEXT("SubMapLifecycle.K2Node_CallFunction_0"), TEXT("PrepareTileEntry")},
        {TEXT("SubMapLifecycle.K2Node_CallFunction_1"), TEXT("CommitMapEntry")},
        {TEXT("SubMapLifecycle.K2Node_CallFunction_2"), TEXT("PrintText")},
        {TEXT("SubMapLifecycle.K2Node_CallFunction_3"), TEXT("ActivateThisMap")},
        {TEXT("SubMapLifecycle.K2Node_CallFunction_4"), TEXT("PrintText")},
        {TEXT("SubMapLifecycle.K2Node_CallFunction_5"), TEXT("GetGameMainMapPlayerController")},
        {TEXT("SubMapLifecycle.K2Node_CallFunction_6"), TEXT("InitializeBattleUI")},
        {TEXT("SubMapLifecycle.K2Node_CallFunction_7"), TEXT("PrintText")},
        {TEXT("SubMapLifecycle.K2Node_CallArrayFunction_0"), TEXT("Array_Clear")}
    };
    const TArray<FName> ExpectedEvents = {TEXT("OnSubMapLoaded"), TEXT("OnEntrySceneReady"), TEXT("OnSubMapReady"),
        TEXT("OnSubMapUnloading"), TEXT("OnSubMapUnloaded"), TEXT("OnSubMapLoadFailed")};
    TSet<FName> Found;
    TArray<UEdGraph*> Graphs;
    C.Handler->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        const FString* Hash = Expected.Find(Graph->GetFName());
        if (!Hash)
        {
            C.Check(Graph->Nodes.IsEmpty(), TEXT("Unreviewed graph; preserving and refusing migration: ") + Graph->GetPathName());
            continue;
        }
        Found.Add(Graph->GetFName());
        C.Check(GraphHash(Graph).Equals(*Hash, ESearchCase::IgnoreCase),
            TEXT("Reviewed graph has changed; inspect before updating migration: ") + Graph->GetPathName() + TEXT(" hash=") + GraphHash(Graph));
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            C.Check(Node->GetName().StartsWith(Node->GetClass()->GetName() + TEXT("_")), TEXT("Unreviewed node class: ") + Node->GetPathName());
            if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
            {
                const FName* Function = ExpectedCalls.Find(Graph->GetName() + TEXT(".") + Node->GetName());
                C.Check(Function && Call->FunctionReference.GetMemberName() == *Function, TEXT("Unreviewed function call: ") + Node->GetPathName());
            }
            if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
            {
                const int32 Index = FCString::Atoi(*Node->GetName().RightChop(FString(TEXT("K2Node_Event_")).Len()));
                C.Check(ExpectedEvents.IsValidIndex(Index) && Event->EventReference.GetMemberName() == ExpectedEvents[Index],
                    TEXT("Unreviewed event: ") + Node->GetPathName());
            }
            if (UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node))
                C.Check(Macro->GetMacroGraph() && Macro->GetMacroGraph()->GetPathName() == TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:IsValid"),
                    TEXT("Unreviewed macro: ") + Node->GetPathName());
            if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
            {
                const FName Name = Variable->VariableReference.GetMemberName();
                const bool bKnown = ConfigNames.Contains(Name) || Name == OldSpawners || Name == TEXT("BattleWidget");
                C.Check(bKnown && Variable->FindPin(Name, EGPD_Output), TEXT("Unreviewed variable getter: ") + Node->GetPathName());
            }
        }
    }
    C.Check(Found.Num() == Expected.Num(), TEXT("One or more reviewed handler graphs are missing"));
    for (const FBPVariableDescription& Var : C.Handler->NewVariables)
        C.Check(ConfigNames.Contains(Var.VarName) || Var.VarName == OldSpawners, TEXT("Unreviewed variable: ") + Var.VarName.ToString());
    C.Check(C.Handler->NewVariables.Num() == 5, TEXT("Expected exactly the five inspected handler variables"));
    return C.bOK;
}

struct FEdit
{
    UBlueprint* Blueprint = nullptr;
    UEdGraphNode* Node = nullptr;
    FName Name;
    bool bFunction = false;
};

TArray<FEdit> PlanEdits(FContext& C)
{
    TArray<FEdit> Edits;
    for (UBlueprint* BP : C.Blueprints)
    {
        if (BP == C.Handler) continue; // Only the reviewed graphs will be removed.
        TArray<UEdGraph*> Graphs;
        BP->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
        {
            for (TFieldIterator<FStructProperty> It(Node->GetClass()); It; ++It)
            {
                if (It->Struct != FMemberReference::StaticStruct()) continue;
                const FMemberReference* Ref = It->ContainerPtrToValuePtr<FMemberReference>(Node);
                const FName Name = Ref->GetMemberName();
                if (!IsMigratedMember(Name) || !RefersToHandler(C, BP, Node, *Ref)) continue;
                if (Ref->GetMemberParentClass() == UGameMapSubMapHandler::StaticClass() && Name != OldRegister && Name != OldSpawners) continue;
                const bool bFunction = Node->IsA<UK2Node_CallFunction>();
                const bool bSupported = (bFunction && (Name == OldRegister || Name == TEXT("PrepareTileEntry"))) ||
                    (Node->IsA<UK2Node_Variable>() && ConfigNames.Contains(Name));
                if (!C.Check(bSupported, TEXT("Unsupported old handler reference; preserve and review: ") + Node->GetPathName() + TEXT(".") + Name.ToString())) continue;
                Edits.Add({BP, Node, Name == OldRegister ? FName(TEXT("RegisterSquadSpawner")) : Name, bFunction});
                C.Affected.Add(BP);
            }
            if (UK2Node_CreateDelegate* Delegate = Cast<UK2Node_CreateDelegate>(Node))
                C.Check(Delegate->GetFunctionName() != OldRegister, TEXT("Unreviewed delegate binding to retired registration function: ") + Node->GetPathName());
        }
    }
    if (C.Handler->ParentClass != UGameMapSubMapHandler::StaticClass())
        for (const FSnapshot& Saved : C.Snapshots) C.Affected.Add(Saved.Blueprint);
    return Edits;
}

FString PackageFilename(UPackage* Package, UObject* Asset)
{
    return FPackageName::LongPackageNameToFilename(Package->GetName(),
        Asset->IsA<UWorld>() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
}

bool Backup(FContext& C)
{
    C.BackupRoot = FPaths::ProjectSavedDir() / TEXT("SubMapHandlerLifecycle/NativeGameHandler/Backup") /
        (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TSet<UPackage*> Packages;
    for (UBlueprint* BP : C.Affected) Packages.Add(BP->GetOutermost());
    for (UPackage* Package : Packages)
    {
        UObject* Asset = C.SaveAssets.FindRef(Package);
        if (!C.Check(Asset && Asset->GetOutermost() == Package, TEXT("No save target for ") + Package->GetName())) return false;
        const FString Source = PackageFilename(Package, Asset);
        const FString Destination = C.BackupRoot / Package->GetName().RightChop(6) + FPaths::GetExtension(Source, true);
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
        if (!C.Check(IFileManager::Get().Copy(*Destination, *Source, false, true) == COPY_OK, TEXT("Backup failed: ") + Source)) return false;
        C.Report += FString::Printf(TEXT("BACKUP %s -> %s\n"), *Source, *Destination);
        for (const TCHAR* Extension : {TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl")})
        {
            const FString Sidecar = FPaths::ChangeExtension(Source, Extension);
            if (IFileManager::Get().FileExists(*Sidecar))
                if (!C.Check(IFileManager::Get().Copy(*FPaths::ChangeExtension(Destination, Extension), *Sidecar, false, true) == COPY_OK,
                    TEXT("Sidecar backup failed: ") + Sidecar)) return false;
        }
    }
    return C.Check(FFileHelper::SaveStringToFile(C.Report, *(C.BackupRoot / TEXT("Before.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM),
        TEXT("Cannot save pre-migration configuration and graph report"));
}

struct FPinSnapshot
{
    FName Name;
    EEdGraphPinDirection Direction;
    FString Default;
    FString AutoDefault;
    FText TextDefault;
    TWeakObjectPtr<UObject> ObjectDefault;
    TArray<TPair<UEdGraphNode*, FName>> Links;
};

TArray<FPinSnapshot> CapturePins(UEdGraphNode* Node)
{
    TArray<FPinSnapshot> Pins;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        FPinSnapshot& Saved = Pins.AddDefaulted_GetRef();
        Saved.Name = Pin->PinName;
        Saved.Direction = Pin->Direction;
        Saved.Default = Pin->DefaultValue;
        Saved.AutoDefault = Pin->AutogeneratedDefaultValue;
        Saved.TextDefault = Pin->DefaultTextValue;
        Saved.ObjectDefault = Pin->DefaultObject;
        for (UEdGraphPin* Link : Pin->LinkedTo) Saved.Links.Emplace(Link->GetOwningNode(), Link->PinName);
    }
    return Pins;
}

bool Rebind(FContext& C, const FEdit& Edit, const TArray<FPinSnapshot>& Pins)
{
    Edit.Node->Modify();
    if (Edit.bFunction)
    {
        UFunction* Function = UGameMapSubMapHandler::StaticClass()->FindFunctionByName(Edit.Name);
        if (!C.Check(Function != nullptr, TEXT("Missing native function: ") + Edit.Name.ToString())) return false;
        CastChecked<UK2Node_CallFunction>(Edit.Node)->SetFromFunction(Function);
        CastChecked<UK2Node_CallFunction>(Edit.Node)->FunctionReference.SetExternalMember(Edit.Name, UGameMapSubMapHandler::StaticClass());
    }
    else CastChecked<UK2Node_Variable>(Edit.Node)->VariableReference.SetExternalMember(Edit.Name, UGameMapSubMapHandler::StaticClass());
    Edit.Node->ReconstructNode();
    for (const FPinSnapshot& Saved : Pins)
    {
        UEdGraphPin* Pin = Edit.Node->FindPin(Saved.Name, Saved.Direction);
        if (!C.Check(Pin && !Pin->bOrphanedPin, TEXT("Reconstruction lost pin: ") + Edit.Node->GetPathName() + TEXT(".") + Saved.Name.ToString())) return false;
        // Reconstruct should match the unchanged argument names. Explicitly preserve user literals.
        Pin->DefaultValue = Saved.Default;
        Pin->AutogeneratedDefaultValue = Saved.AutoDefault;
        Pin->DefaultObject = Saved.ObjectDefault.Get();
        Pin->DefaultTextValue = Saved.TextDefault;
        for (const auto& Link : Saved.Links)
        {
            UEdGraphPin* Other = Link.Key->FindPin(Link.Value, Saved.Direction == EGPD_Input ? EGPD_Output : EGPD_Input);
            if (Other && !Pin->LinkedTo.Contains(Other))
                GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Pin, Other);
            if (!C.Check(Other && Pin->LinkedTo.Contains(Other), TEXT("Reconstruction disconnected edge: ") + Edit.Node->GetPathName() + TEXT(".") + Saved.Name.ToString())) return false;
        }
        if (!C.Check(Pin->LinkedTo.Num() == Saved.Links.Num() && Pin->DefaultTextValue.EqualTo(Saved.TextDefault),
            TEXT("Reconstruction changed pin data: ") + Edit.Node->GetPathName() + TEXT(".") + Saved.Name.ToString())) return false;
    }
    for (UEdGraphPin* Pin : Edit.Node->Pins)
        if (!C.Check(!Pin->bOrphanedPin, TEXT("Orphaned pin after native rebind: ") + Edit.Node->GetPathName())) return false;
    if (Edit.Name == TEXT("RegisterSquadSpawner") && Edit.Blueprint->GeneratedClass
        && Edit.Blueprint->GeneratedClass->IsChildOf(AUnitSquadSpawner::StaticClass()))
    {
        UEdGraphPin* SpawnerPin = Edit.Node->FindPin(TEXT("SquadSpawner"));
        if (SpawnerPin && SpawnerPin->LinkedTo.IsEmpty() && !SpawnerPin->DefaultObject)
        {
            auto* SelfNode = NewObject<UK2Node_Self>(Edit.Node->GetGraph());
            Edit.Node->GetGraph()->AddNode(SelfNode, false, false);
            SelfNode->CreateNewGuid();
            SelfNode->PostPlacedNewNode();
            SelfNode->AllocateDefaultPins();
            SelfNode->NodePosX = Edit.Node->NodePosX - 190;
            SelfNode->NodePosY = Edit.Node->NodePosY + 150;
            if (!C.Check(GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(SelfNode->FindPin(UEdGraphSchema_K2::PN_Self), SpawnerPin),
                TEXT("Cannot connect squad generator self reference"))) return false;
            C.Report += TEXT("REPAIRED empty SquadSpawner input -> self: ") + Edit.Node->GetPathName() + TEXT("\n");
        }
        UEdGraphPin* Target = Edit.Node->FindPin(UEdGraphSchema_K2::PN_Self);
        if (Target && Target->LinkedTo.Num() == 1)
        {
            auto* CastNode = Cast<UK2Node_DynamicCast>(Target->LinkedTo[0]->GetOwningNode());
            if (CastNode && IsHandlerClass(C, CastNode->TargetType) && Target->LinkedTo[0]->LinkedTo.Num() == 1)
            {
                // The cast is only used by registration; generalize it without changing execution order.
                TArray<FPinSnapshot> CastPins = CapturePins(CastNode);
                const FName PreviousResult = CastNode->GetCastResultPin()->PinName;
                Target->BreakAllPinLinks();
                CastNode->TargetType = UGameMapSubMapHandler::StaticClass();
                CastNode->ReconstructNode();
                for (const FPinSnapshot& Saved : CastPins)
                {
                    if (Saved.Name == PreviousResult) continue;
                    UEdGraphPin* Pin = CastNode->FindPin(Saved.Name, Saved.Direction);
                    if (!C.Check(Pin != nullptr, TEXT("Generic cast lost a pin"))) return false;
                    for (const auto& Link : Saved.Links)
                    {
                        auto* Other = Link.Key->FindPin(Link.Value, Saved.Direction == EGPD_Input ? EGPD_Output : EGPD_Input);
                        if (!C.Check(Other && (Pin->LinkedTo.Contains(Other) || GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(Pin, Other)),
                            TEXT("Generic cast lost an existing link"))) return false;
                    }
                }
                if (!C.Check(GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(CastNode->GetCastResultPin(), Target), TEXT("Cannot connect generic map handler cast"))) return false;
                C.Report += TEXT("GENERALIZED squad generator cast -> GameMapSubMapHandler\n");
            }
        }
    }
    C.Report += FString::Printf(TEXT("REBOUND %s -> %s.%s\n"), *Edit.Node->GetPathName(), *UGameMapSubMapHandler::StaticClass()->GetPathName(), *Edit.Name.ToString());
    return true;
}

bool Compile(FContext& C, UBlueprint* BP)
{
    FKismetEditorUtilities::CompileBlueprint(BP);
    return C.Check(BP->Status != BS_Error && BP->GeneratedClass != nullptr, TEXT("Blueprint compilation failed: ") + BP->GetPathName());
}

bool RestoreConfig(FContext& C, const FSnapshot& Saved)
{
    UObject* CDO = Saved.Blueprint->GeneratedClass->GetDefaultObject();
    for (const auto& Pair : Saved.Values)
    {
        FProperty* Property = FindFProperty<FProperty>(Saved.Blueprint->GeneratedClass, Pair.Key);
        if (!C.Check(Property && Property->ImportText_InContainer(*Pair.Value, CDO, CDO, PPF_None), TEXT("Cannot restore config: ") + Pair.Key.ToString())) return false;
        FString Actual;
        Property->ExportText_InContainer(0, Actual, CDO, nullptr, CDO, PPF_None);
        if (!C.Check(Actual == Pair.Value, TEXT("CDO config changed: ") + Saved.Blueprint->GetPathName() + TEXT(".") + Pair.Key.ToString() + TEXT(" expected=") + Pair.Value + TEXT(" actual=") + Actual)) return false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Saved.Blueprint);
    return true;
}

bool VerifyConfig(FContext& C)
{
    for (const FSnapshot& Saved : C.Snapshots)
    {
        UObject* CDO = Saved.Blueprint->GeneratedClass ? Saved.Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
        if (!C.Check(CDO != nullptr, TEXT("Missing CDO after compile: ") + Saved.Blueprint->GetPathName())) continue;
        for (const auto& Pair : Saved.Values)
        {
            FProperty* Property = FindFProperty<FProperty>(Saved.Blueprint->GeneratedClass, Pair.Key);
            FString Actual;
            if (Property) Property->ExportText_InContainer(0, Actual, CDO, nullptr, CDO, PPF_None);
            C.Check(Property && Actual == Pair.Value, TEXT("Configuration changed during verification: ") + Saved.Blueprint->GetPathName() + TEXT(".") + Pair.Key.ToString());
        }
    }
    return C.bOK;
}

bool Verify(FContext& C, bool bCompile)
{
    C.Check(C.Handler->ParentClass == UGameMapSubMapHandler::StaticClass(), TEXT("Handler parent is not native GameMapSubMapHandler"));
    CheckReviewedGraphs(C);
    for (UBlueprint* BP : C.Blueprints)
    {
        TArray<UEdGraph*> Graphs;
        BP->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
        {
            for (TFieldIterator<FStructProperty> It(Node->GetClass()); It; ++It)
            {
                if (It->Struct != FMemberReference::StaticStruct()) continue;
                const FMemberReference* Ref = It->ContainerPtrToValuePtr<FMemberReference>(Node);
                const FName Name = Ref->GetMemberName();
                if (!IsMigratedMember(Name) || !RefersToHandler(C, BP, Node, *Ref)) continue;
                C.Check(Name != OldRegister && Name != OldSpawners && Ref->GetMemberParentClass() == UGameMapSubMapHandler::StaticClass(),
                    TEXT("Legacy Blueprint member reference remains: ") + Node->GetPathName() + TEXT(".") + Name.ToString());
            }
        }
    }
    if (bCompile)
        for (UBlueprint* BP : C.Related) Compile(C, BP);
    VerifyConfig(C);
    return C.bOK;
}

bool Apply(FContext& C, const TArray<FEdit>& Edits)
{
    if (C.Affected.IsEmpty())
    {
        C.Report += TEXT("ALREADY_MIGRATED no asset writes required\n");
        return Verify(C, true);
    }
    if (!Backup(C)) return false;
    TMap<UEdGraphNode*, TArray<FPinSnapshot>> OriginalPins;
    for (const FEdit& Edit : Edits) OriginalPins.Add(Edit.Node, CapturePins(Edit.Node));
    // Everything below changes memory only until every graph and config has passed verification.
    if (C.Handler->ParentClass != UGameMapSubMapHandler::StaticClass())
    {
        C.Handler->Modify();
        TArray<UEdGraph*> Graphs;
        C.Handler->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
            if (Graph->GetFName() == TEXT("PrepareTileEntry") || Graph->GetFName() == OldRegister || Graph->GetFName() == TEXT("SubMapLifecycle"))
                FBlueprintEditorUtils::RemoveGraph(C.Handler, Graph, EGraphRemoveFlags::None);
        // Do not call BulkRemoveMemberVariables: it also deletes usage nodes in dependent BPs.
        C.Handler->NewVariables.RemoveAll([](const FBPVariableDescription& Var) { return ConfigNames.Contains(Var.VarName) || Var.VarName == OldSpawners; });
        C.Handler->ParentClass = UGameMapSubMapHandler::StaticClass();
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(C.Handler);
        if (!Compile(C, C.Handler)) return false;
    }
    for (const FEdit& Edit : Edits) if (!Rebind(C, Edit, OriginalPins.FindChecked(Edit.Node))) return false;
    // Compile base before dependents; this also updates pins whose object type remains the T5 subclass.
    TArray<UBlueprint*> Ordered = C.Affected.Array();
    Ordered.Sort([](const UBlueprint& A, const UBlueprint& B)
    {
        auto Depth = [](const UBlueprint& BP) { int32 Result = 0; for (const UClass* Class = BP.ParentClass; Class; Class = Class->GetSuperClass()) ++Result; return Result; };
        return Depth(A) < Depth(B);
    });
    for (UBlueprint* BP : Ordered)
    {
        FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
        if (!Compile(C, BP)) return false;
        for (const FSnapshot& Saved : C.Snapshots)
            if (Saved.Blueprint == BP && !RestoreConfig(C, Saved)) return false;
    }
    if (!Verify(C, false)) return false;
    for (const FSnapshot& Saved : C.Snapshots) if (!RestoreConfig(C, Saved)) return false;
    TSet<UPackage*> SavedPackages;
    for (UBlueprint* BP : Ordered)
    {
        UPackage* Package = BP->GetOutermost();
        if (SavedPackages.Contains(Package)) continue;
        SavedPackages.Add(Package);
        UObject* Asset = C.SaveAssets.FindRef(Package);
        Package->MarkPackageDirty();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        const FString File = PackageFilename(Package, Asset);
        if (!C.Check(UPackage::SavePackage(Package, Asset, *File, Args), TEXT("Save failed; backups are at ") + C.BackupRoot + TEXT(": ") + File)) return false;
        C.Report += TEXT("SAVED ") + File + TEXT("\n");
    }
    DumpBlueprint(C, C.Handler);
    return C.bOK;
}
}

int32 RunNativeGameHandlerMigration(const FString& Params)
{
    using namespace NativeGameHandlerMigration;
    FContext C;
    C.Handler = LoadObject<UBlueprint>(nullptr, HandlerPath);
    if (!C.Check(C.Handler != nullptr, TEXT("Cannot load T5 handler"))) return 1;
    Scan(C);
    const TArray<FEdit> Edits = PlanEdits(C);
    CheckReviewedGraphs(C);
    C.Report += FString::Printf(TEXT("PLAN nodeEdits=%d affectedBlueprints=%d\n"), Edits.Num(), C.Affected.Num());
    if (!C.WriteReport(TEXT("Inspect.txt"))) return 1;
    if (FParse::Param(*Params, TEXT("Apply")))
    {
        if (C.bOK) Apply(C, Edits);
        C.WriteReport(TEXT("Apply.txt"));
    }
    else if (FParse::Param(*Params, TEXT("Validate")))
    {
        Verify(C, true);
        C.WriteReport(TEXT("Validate.txt"));
    }
    UE_LOG(LogTemp, Display, TEXT("NATIVE_GAME_HANDLER_%s edits=%d affected=%d"), C.bOK ? TEXT("OK") : TEXT("FAILED"), Edits.Num(), C.Affected.Num());
    return C.bOK ? 0 : 1;
}
