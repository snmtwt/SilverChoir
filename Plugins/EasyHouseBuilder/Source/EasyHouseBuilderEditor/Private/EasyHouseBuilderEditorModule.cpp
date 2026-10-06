// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "EHBBuildingCopy.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EHBLevelEditorDragDropHandler.h"
#include "Editor.h"
#include "EditorModeRegistry.h"
#include "EasyHouseEditorModeToolkit.h"
#include "EasyHouseEditorMode.h"
#include "EasyHouseBuilderEditorStyle.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Sampling/EHBDoorWindowMeshData.h"
#include "Sampling/EHBPillarMeshData.h"
#include "Sampling/EHBRailingMeshData.h"
#include "Sampling/EHBWallMeshData.h"
#include "Settings/EHBBuildingToolsetSettings.h"
#include "Toolsets/EHBBuildingToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "FEasyHouseBuilderEditorModule"

namespace
{
	constexpr const TCHAR* DefaultSampleDataFolder = TEXT("/Game/BuildingSampleData");

	struct FEHBDefaultSampleTableSpec
	{
		const TCHAR* AssetName = nullptr;
		UScriptStruct* RowStruct = nullptr;
		TSoftObjectPtr<UDataTable> UEHBBuildingToolsetSettings::* SettingMember = nullptr;
	};

	ULevelEditorDragDropHandler* CreateEHBLevelEditorDragDropHandler()
	{
		UObject* Outer = GEditor ? static_cast<UObject*>(GEditor) : GetTransientPackage();
		return NewObject<UEHBLevelEditorDragDropHandler>(Outer);
	}

	FString MakeDefaultSampleTablePackageName(const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s"), DefaultSampleDataFolder, AssetName);
	}

	FString MakeDefaultSampleTableObjectPath(const TCHAR* AssetName)
	{
		return FString::Printf(TEXT("%s/%s.%s"), DefaultSampleDataFolder, AssetName, AssetName);
	}

	UDataTable* LoadOrCreateDefaultSampleTable(const FEHBDefaultSampleTableSpec& Spec)
	{
		if (!Spec.AssetName || !Spec.RowStruct)
		{
			return nullptr;
		}

		const FString ObjectPath = MakeDefaultSampleTableObjectPath(Spec.AssetName);
		if (UDataTable* ExistingTable = LoadObject<UDataTable>(nullptr, *ObjectPath))
		{
			if (ExistingTable->GetRowStruct() != Spec.RowStruct)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("EasyHouseBuilder default sample table %s exists, but its row struct is %s instead of %s."),
					*ObjectPath,
					ExistingTable->GetRowStruct() ? *ExistingTable->GetRowStruct()->GetName() : TEXT("<none>"),
					*Spec.RowStruct->GetName());
				return nullptr;
			}

			return ExistingTable;
		}

		if (UObject* ExistingObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath))
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("EasyHouseBuilder default sample asset %s already exists as %s, so no data table was created."),
				*ObjectPath,
				*ExistingObject->GetClass()->GetName());
			return nullptr;
		}

		const FString PackageName = MakeDefaultSampleTablePackageName(Spec.AssetName);
		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			UE_LOG(LogTemp, Warning, TEXT("EasyHouseBuilder default sample table package path is invalid: %s"), *PackageName);
			return nullptr;
		}

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			UE_LOG(LogTemp, Warning, TEXT("EasyHouseBuilder failed to create package for default sample table: %s"), *PackageName);
			return nullptr;
		}

		UDataTable* DataTable = NewObject<UDataTable>(
			Package,
			UDataTable::StaticClass(),
			FName(Spec.AssetName),
			RF_Public | RF_Standalone | RF_Transactional);
		if (!DataTable)
		{
			UE_LOG(LogTemp, Warning, TEXT("EasyHouseBuilder failed to create default sample table asset: %s"), *ObjectPath);
			return nullptr;
		}

		DataTable->RowStruct = Spec.RowStruct;
		DataTable->Modify();
		DataTable->MarkPackageDirty();
		Package->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(DataTable);

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			PackageName,
			FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFileName), true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		SaveArgs.bSlowTask = false;

		if (!UPackage::SavePackage(Package, DataTable, *PackageFileName, SaveArgs))
		{
			UE_LOG(LogTemp, Warning, TEXT("EasyHouseBuilder failed to save default sample table: %s"), *PackageFileName);
		}
		else
		{
			UE_LOG(LogTemp, Display, TEXT("EasyHouseBuilder created default sample table: %s"), *ObjectPath);
		}

		return DataTable;
	}

	void EnsureDefaultSampleTables()
	{
		if (IsRunningCommandlet())
		{
			return;
		}

		UEHBBuildingToolsetSettings* Settings = GetMutableDefault<UEHBBuildingToolsetSettings>();
		if (!Settings)
		{
			return;
		}

		const FEHBDefaultSampleTableSpec Specs[] =
		{
			{ TEXT("DT_EHB_WallSamples"), FEHBWallMeshData::StaticStruct(), &UEHBBuildingToolsetSettings::DefaultWallMeshDataTable },
			{ TEXT("DT_EHB_PillarSamples"), FEHBPillarMeshData::StaticStruct(), &UEHBBuildingToolsetSettings::DefaultPillarMeshDataTable },
			{ TEXT("DT_EHB_RailingSamples"), FEHBRailingMeshData::StaticStruct(), &UEHBBuildingToolsetSettings::DefaultRailingMeshDataTable },
			{ TEXT("DT_EHB_DoorWindowSamples"), FEHBDoorWindowMeshData::StaticStruct(), &UEHBBuildingToolsetSettings::DefaultDoorWindowMeshDataTable },
		};

		bool bConfigChanged = false;
		for (const FEHBDefaultSampleTableSpec& Spec : Specs)
		{
			UDataTable* Table = LoadOrCreateDefaultSampleTable(Spec);
			if (!Table || !Spec.SettingMember)
			{
				continue;
			}

			TSoftObjectPtr<UDataTable>& ConfigValue = (Settings->*Spec.SettingMember);
			if (ConfigValue.IsNull())
			{
				ConfigValue = Table;
				bConfigChanged = true;
			}
		}

		if (bConfigChanged)
		{
			Settings->SaveConfig();
			if (!Settings->TryUpdateDefaultConfigFile(TEXT(""), false))
			{
				UE_LOG(LogTemp, Warning, TEXT("EasyHouseBuilder failed to update project default config for sample tables."));
			}
		}
	}

}

/** 编辑器模块入口，负责注册建筑编辑模式和插件 Slate 图标资源。 */
class FEasyHouseBuilderEditorModule : public IModuleInterface
{
public:
	/** 模块启动时注册 Style 和编辑模式，让“建筑”出现在编辑器模式菜单中。 */
	virtual void StartupModule() override
	{
		if (GEditor)
		{
			GEditor->OnCreateLevelEditorDragDropHandler().BindStatic(&CreateEHBLevelEditorDragDropHandler);
		}

		// 先注册插件自己的 Slate Style，后续编辑模式图标和工具按钮图标都从这里读取。
		FEasyHouseBuilderEditorStyle::Register();
		FEasyHouseEditorModeToolkit::RegisterGlobalElementEditorTabSpawner();

		// 将“建筑”编辑模式注册进 UE 的模式系统，让它出现在左上角模式选择菜单中。
		FEditorModeRegistry::Get().RegisterMode<FEasyHouseEditorMode>(
			FEasyHouseEditorMode::EM_EasyHouseEditorModeId,
			LOCTEXT("EasyHouseModeName", "\u5efa\u7b51"),
			FSlateIcon(
				FEasyHouseBuilderEditorStyle::GetStyleSetName(),
				"EasyHouseBuilder.Mode",
				"EasyHouseBuilder.Mode.Small"),
			true,
			650);

		UToolsetRegistry::RegisterToolsetClass(UEHBBuildingToolset::StaticClass());
		EnsureDefaultSampleTables();
		EHBBuildingCopy::RegisterDuplicateCommand();
	}

	/** 模块关闭时注销编辑模式和 Style，避免热重载或关闭编辑器时残留全局资源。 */
	virtual void ShutdownModule() override
	{
		EHBBuildingCopy::UnregisterDuplicateCommand();
		UToolsetRegistry::UnregisterToolsetClass(UEHBBuildingToolset::StaticClass());

		if (GEditor)
		{
			GEditor->OnCreateLevelEditorDragDropHandler().Unbind();
		}

		// 模块卸载时注销编辑模式，避免热重载或关闭编辑器时留下悬挂模式条目。
		FEditorModeRegistry::Get().UnregisterMode(FEasyHouseEditorMode::EM_EasyHouseEditorModeId);
		FEasyHouseEditorModeToolkit::UnregisterGlobalElementEditorTabSpawner();

		// 注销 Slate Style，释放图标 Brush 等资源。
		FEasyHouseBuilderEditorStyle::Unregister();
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FEasyHouseBuilderEditorModule, EasyHouseBuilderEditor)
