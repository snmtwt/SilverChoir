#include "GameMainMapArchitectureCommandlet.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"
#include "K2Node_Variable.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "UObject/UnrealType.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "MainMapPointerBlueprintMigration.h"
#include "MainMapRoomBlueprintMigration.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/MetaData.h"

namespace MainMapArchitecture
{
const TCHAR* PCPath = TEXT("/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController");
const TCHAR* RoomPath = TEXT("/Game/System/Map/BaseMap/Scene/BP_作战指挥室_Scene");
const TCHAR* SquadPath = TEXT("/Game/System/Map/BaseMap/Scene/BP_小队会议室_Scene");
void Inspect(UBlueprint* BP, FString& Text)
{
    Text += FString::Printf(TEXT("ASSET %s PARENT %s\n"), *BP->GetPathName(), *GetPathNameSafe(BP->ParentClass));
    for (const auto& V : BP->NewVariables)
    {
        FString Value;
        if (const auto* P = FindFProperty<FProperty>(BP->GeneratedClass, V.VarName))
            P->ExportText_InContainer(0, Value, BP->GeneratedClass->GetDefaultObject(), nullptr, nullptr, PPF_None);
        Text += FString::Printf(TEXT("VARIABLE %s type=%s sub=%s category=%s value=%s\n"), *V.VarName.ToString(), *V.VarType.PinCategory.ToString(), *GetPathNameSafe(V.VarType.PinSubCategoryObject.Get()), *V.Category.ToString(), *Value);
    }
    if (const auto* P = FindFProperty<FProperty>(BP->GeneratedClass, TEXT("InteriorCameraState")))
    { FString V; P->ExportText_InContainer(0, V, BP->GeneratedClass->GetDefaultObject(), nullptr, nullptr, PPF_None); Text += TEXT("INTERIOR ") + V + TEXT("\n"); }
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    for (auto* G : Graphs)
    {
        Text += FString::Printf(TEXT("GRAPH %s nodes=%d\n"), *G->GetName(), G->Nodes.Num());
        for (UEdGraphNode* N : G->Nodes)
        {
            Text += FString::Printf(TEXT("NODE %s class=%s title=%s comment=%s\n"), *N->GetName(), *N->GetClass()->GetName(), *N->GetNodeTitle(ENodeTitleType::ListView).ToString(), *N->NodeComment);
            for (auto* P : N->Pins)
            {
                FString Links;
                for (auto* L : P->LinkedTo) Links += L->GetOwningNode()->GetName() + TEXT(".") + L->PinName.ToString() + TEXT(" ");
                Text += FString::Printf(TEXT(" PIN %s %s value=%s object=%s links=%s\n"), P->Direction == EGPD_Input ? TEXT("IN") : TEXT("OUT"), *P->PinName.ToString(), *P->DefaultValue, *GetPathNameSafe(P->DefaultObject), *Links);
            }
        }
    }
}
}
UGameMainMapArchitectureCommandlet::UGameMainMapArchitectureCommandlet() { IsClient = false; IsEditor = true; LogToConsole = true; }
int32 UGameMainMapArchitectureCommandlet::Main(const FString& Params)
{
    using namespace MainMapArchitecture;
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("MainMapArchitecture");
    IFileManager::Get().MakeDirectory(*Directory, true);
    for (const TCHAR* Path : {PCPath, RoomPath, SquadPath})
    {
        auto* BP = LoadObject<UBlueprint>(nullptr, Path); if (!BP) return 1;
        FString Text; Inspect(BP, Text);
        FFileHelper::SaveStringToFile(Text, *(Directory / (BP->GetName() + TEXT(".txt"))), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    FString Mappings;
    for (const TCHAR* Path : {TEXT("/Game/System/Input/Common/IMC_Common"), TEXT("/Game/System/Input/Base/IMC_Base"), TEXT("/Game/System/Input/Battle/IMC_Battle")})
    {
        auto* IMC = LoadObject<UInputMappingContext>(nullptr, Path); if (!IMC) return 1;
        Mappings += FString::Printf(TEXT("MAPPING %s\n"), Path);
        for (const auto& M : IMC->GetMappings()) Mappings += FString::Printf(TEXT(" ACTION %s KEY %s\n"), *GetPathNameSafe(M.Action), *M.Key.ToString());
    }
    FFileHelper::SaveStringToFile(Mappings, *(Directory / TEXT("Mappings.txt")));
    UE_LOG(LogTemp, Display, TEXT("MAIN_MAP_ARCHITECTURE_INSPECT_OK"));
    if (FParse::Param(*Params, TEXT("RepairReturnDefaults")))
    {
        using namespace MainMapBP;
        auto* PC = LoadObject<UBlueprint>(nullptr,PCPath);
        const FString Filename=FPackageName::LongPackageNameToFilename(PC->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        const FString Backup=Directory / (TEXT("ReturnsBefore_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".uasset"));
        if (IFileManager::Get().Copy(*Backup,*Filename)!=COPY_OK) return 1;
        TMap<UEdGraphPin*,FString> Expected;
        for (FName Name : {FName(TEXT("CommandMap_BeginPointer")),FName(TEXT("CommandMap_UpdatePointer")),FName(TEXT("CommandMap_QueryPointer"))})
        {
            auto* Graph=Find(PC,Name); if (!Graph) return 1;
            for (UEdGraphNode* Node : Graph->Nodes) if (auto* Result=Cast<UK2Node_FunctionResult>(Node))
            {
                // Each result must own its signature metadata: saving updates its defaults.
                for (auto& Info : Result->UserDefinedPins) Info=MakeShared<FUserPinInfo>(*Info);
                auto* Pin=P(Result,TEXT("ReturnValue"));
                bool Success=false;
                if (Name==TEXT("CommandMap_QueryPointer")) Success=P(Result,TEXT("OutMap"))->LinkedTo.Num()>0;
                else if (Name==TEXT("CommandMap_BeginPointer")) Success=Result->GetFName()==TEXT("K2Node_FunctionResult_1");
                else Success=Result->GetFName()==TEXT("K2Node_FunctionResult_1") || Result->GetFName()==TEXT("K2Node_FunctionResult_2");
                S()->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
                const FString Value=Success?TEXT("true"):TEXT("false"); D(Result,TEXT("ReturnValue"),Value); Expected.Add(Pin,Value);
            }
        }
        OK=true; if (!Compile(PC)) return 1;
        for (const auto& Pair : Expected) if (Pair.Key->DefaultValue!=Pair.Value) return 1;
        PC->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone;
        if (!UPackage::SavePackage(PC->GetOutermost(),PC,*Filename,Args)) return 1;
        UE_LOG(LogTemp,Display,TEXT("MAIN_MAP_RETURN_DEFAULTS_REPAIRED"));
    }
    if (FParse::Param(*Params, TEXT("Validate")))
    {
        MainMapBP::OK = true;
        for (const TCHAR* Path : {PCPath, RoomPath})
        {
            auto* BP = LoadObject<UBlueprint>(nullptr, Path);
            if (!MainMapBP::Compile(BP)) return 1;
            TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
            for (auto* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes) for (auto* Pin : Node->Pins)
                if (Pin->bOrphanedPin)
                { UE_LOG(LogTemp, Error, TEXT("ARCH_ORPHAN_PIN %s.%s.%s"), *Graph->GetName(), *Node->GetName(), *Pin->PinName.ToString()); return 1; }
        }
        UE_LOG(LogTemp, Display, TEXT("MAIN_MAP_ARCHITECTURE_VALIDATE_OK"));
    }
    if (FParse::Param(*Params, TEXT("Apply")))
    {
        auto* PC = LoadObject<UBlueprint>(nullptr, PCPath);
        auto* Room = LoadObject<UBlueprint>(nullptr, RoomPath);
        if (MainMapBP::Find(PC, TEXT("CommandMap_QueryPointer")) && MainMapBP::Find(Room, TEXT("Room_FinalizeLeave")))
        { UE_LOG(LogTemp, Display, TEXT("MAIN_MAP_ARCHITECTURE_ALREADY_APPLIED")); return 0; }
        if (!MainMapBP::Find(PC, TEXT("QueryCommandMapPointer")) && !MainMapBP::Find(PC, TEXT("BeginCommandMapPointer"))) return 1;
        auto* Base = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/System/Input/Base/IMC_Base"));
        const FString WheelPath = TEXT("/Game/System/Input/Base/IA_SandboxZoom");
        auto* Wheel = LoadObject<UInputAction>(nullptr, *WheelPath, nullptr, LOAD_NoWarn);
        const FString Backup = Directory / TEXT("Backups") / (FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")) + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
        TArray<UObject*> Assets = {PC, Room, Base}; if (Wheel) Assets.Add(Wheel);
        for (auto* Asset : Assets)
        {
            const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
            const FString Destination = Backup / Asset->GetOutermost()->GetName().RightChop(6) + TEXT(".uasset");
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true);
            if (IFileManager::Get().Copy(*Destination, *Filename) != COPY_OK) return 1;
        }
        if (!Wheel)
        {
            Wheel = NewObject<UInputAction>(CreatePackage(*WheelPath), TEXT("IA_SandboxZoom"), RF_Public | RF_Standalone);
            FAssetRegistryModule::AssetCreated(Wheel); Assets.Add(Wheel);
        }
        Wheel->ValueType = EInputActionValueType::Axis1D;
        Wheel->bConsumeInput = true; Wheel->bConsumesActionAndAxisMappings = true;
        Wheel->TriggerEventsThatConsumeLegacyKeys = static_cast<int32>(ETriggerEvent::Started) | static_cast<int32>(ETriggerEvent::Triggered) | static_cast<int32>(ETriggerEvent::Completed);
        Wheel->ActionDescription = FText::FromString(TEXT("基地滚轮：蓝图决定缩放沙盘或自由相机"));
        Base->UnmapAllKeysFromAction(Wheel);
        Base->MapKey(Wheel, EKeys::MouseScrollUp);
        auto& Down = Base->MapKey(Wheel, EKeys::MouseScrollDown); Down.Modifiers.Add(NewObject<UInputModifierNegate>(Base));
        MainMapBP::OK = true;
        if (!MainMapPointerMigration::Build(PC, Wheel) || !MainMapRoomMigration::Build(Room, PC)) return 1;
        // Nothing is saved until both Blueprints compile successfully.
        for (auto* Asset : Assets)
        {
            Asset->MarkPackageDirty(); const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
            FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone;
            if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args)) return 1;
        }
        UE_LOG(LogTemp, Display, TEXT("MAIN_MAP_ARCHITECTURE_APPLY_OK backup=%s"), *Backup);
    }
    return 0;
}
