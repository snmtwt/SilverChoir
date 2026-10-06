// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/EHBBuildingToolsetSettings.h"

#include "EHB_Building.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"

#define LOCTEXT_NAMESPACE "EHBBuildingToolsetSettings"

UEHBBuildingToolsetSettings::UEHBBuildingToolsetSettings()
{
	// 默认配置指向插件自带的基础类。项目可以在 Project Settings 中替换为蓝图子类或自定义 C++ 子类。
	BuildingActorClass = AEHB_Building::StaticClass();
	WallActorClass = AEHB_Wall::StaticClass();
	PillarActorClass = AEHB_Pillar::StaticClass();
	const FSoftObjectPath DefaultMaterialPath(TEXT("/EasyHouseBuilder/Materials/M_EHB_DefaultMaterial.M_EHB_DefaultMaterial"));
	DefaultWhiteBoxMaterial = TSoftObjectPtr<UMaterialInterface>(DefaultMaterialPath);
	DefaultRoofSlopeMaterial = TSoftObjectPtr<UMaterialInterface>(DefaultMaterialPath);
	DefaultRoofSideWallMaterial = TSoftObjectPtr<UMaterialInterface>(DefaultMaterialPath);
	DefaultRoofRidgeMaterial = TSoftObjectPtr<UMaterialInterface>(DefaultMaterialPath);
	DefaultRoofEaveMaterial = TSoftObjectPtr<UMaterialInterface>(DefaultMaterialPath);
	DefaultRoofDiagonalRidgeMaterial = TSoftObjectPtr<UMaterialInterface>(DefaultMaterialPath);
	DoorWindowActorClass = AEHB_DoorWindow::StaticClass();
	DefaultDoorActorClass = AEHB_DoorWindow::StaticClass();
	DefaultWindowActorClass = AEHB_DoorWindow::StaticClass();
	GableRoofActorClass = AEHBGableRoof::StaticClass();
	HipRoofActorClass = AEHBHipRoof::StaticClass();
	DefaultRoofMeshDataTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/BuildingSampleData/DT_EHB_RoofSamples.DT_EHB_RoofSamples")));
	DefaultRoofSurfaceTileMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/ModularJAP/Meshes/RoofParts/BlueRoof_Short.BlueRoof_Short")));
	DefaultRoofRidgeTileMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/ModularJAP/Meshes/RoofParts/BlueRooftop_Long.BlueRooftop_Long")));
	StairActorClass = AEHB_Stair::StaticClass();
	RailingActorClass = AEHB_Railing::StaticClass();
	RailingGateActorClass = AEHB_RailingGate::StaticClass();
}

FName UEHBBuildingToolsetSettings::GetCategoryName() const
{
	// 放到 Project Settings 的 Plugins 分类下，符合插件项目设置的常规位置。
	return TEXT("Plugins");
}

FName UEHBBuildingToolsetSettings::GetSectionName() const
{
	// 使用稳定的英文节名写入配置文件，避免中文路径影响 INI 配置兼容性。
	return TEXT("EasyHouseBuilder");
}

#if WITH_EDITOR
FText UEHBBuildingToolsetSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "\u7a0b\u5e8f\u5316\u5efa\u7b51\u5de5\u5177\u96c6");
}

FText UEHBBuildingToolsetSettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "\u914d\u7f6e\u7a0b\u5e8f\u5316\u5efa\u7b51\u5de5\u5177\u5728\u521b\u5efa\u5efa\u7b51\u5bf9\u8c61\u548c\u5404\u7c7b\u5efa\u7b51\u5143\u7d20\u65f6\u4f7f\u7528\u7684\u9ed8\u8ba4\u7c7b\u6216\u81ea\u5b9a\u4e49\u5b50\u7c7b\u3002");
}
#endif

#undef LOCTEXT_NAMESPACE
