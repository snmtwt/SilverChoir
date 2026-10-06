#include "RepairSkeletalItemCommandlet.h"
#include "Object/Items/SkeletalMeshItem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"

int32 URepairSkeletalItemCommandlet::Main(const FString& Params)
{
    FString AssetPath, MeshPath;
    if (!FParse::Value(*Params, TEXT("Asset="), AssetPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Expected -Asset=/Game/... [-Mesh=/Game/...]."));
        return 1;
    }
    FParse::Value(*Params, TEXT("Mesh="), MeshPath);
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    ASkeletalMeshItem* Defaults = Blueprint && Blueprint->GeneratedClass
        ? Cast<ASkeletalMeshItem>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
    USkeletalMeshComponent* MeshComponent = Defaults
        ? Cast<USkeletalMeshComponent>(Defaults->GetRootComponent()) : nullptr;
    USkeletalMesh* Mesh = MeshPath.IsEmpty() ? nullptr : LoadObject<USkeletalMesh>(nullptr, *MeshPath);
    if (!MeshComponent || MeshComponent->GetFName() != TEXT("MeshComponent")
        || MeshComponent->GetOuter() != Defaults || (!MeshPath.IsEmpty() && !Mesh))
    {
        UE_LOG(LogTemp, Error, TEXT("Unexpected item component layout or invalid mesh: %s"), *AssetPath);
        return 1;
    }

    const FString Filename = FPackageName::LongPackageNameToFilename(
        Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    const FString Backup = FPaths::ProjectSavedDir() / TEXT("WearableItemRepair/Backup")
        / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")) / FPaths::GetCleanFilename(Filename);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Backup), true);
    if (IFileManager::Get().Copy(*Backup, *Filename, false) != COPY_OK)
    {
        UE_LOG(LogTemp, Error, TEXT("Could not back up %s"), *Filename);
        return 1;
    }

    Defaults->Modify();
    MeshComponent->Modify();
    Defaults->MeshComponent = MeshComponent;
    if (Mesh) MeshComponent->SetSkeletalMesh(Mesh);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Defaults = Cast<ASkeletalMeshItem>(Blueprint->GeneratedClass->GetDefaultObject());
    if (Blueprint->Status == BS_Error || !Defaults || !Defaults->MeshComponent
        || Defaults->MeshComponent != Defaults->GetRootComponent()
        || (Mesh && Defaults->MeshComponent->GetSkeletalMeshAsset() != Mesh))
    {
        UE_LOG(LogTemp, Error, TEXT("Repair did not survive Blueprint compilation: %s"), *AssetPath);
        return 1;
    }
    Blueprint->MarkPackageDirty();
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    if (!UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs)) return 1;
    UE_LOG(LogTemp, Display, TEXT("SKELETAL_ITEM_REPAIR_OK %s mesh=%s backup=%s"),
        *AssetPath, *GetPathNameSafe(Defaults->MeshComponent->GetSkeletalMeshAsset()), *Backup);
    return 0;
}
