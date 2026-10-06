// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHBGableRoof.h"
#include "Actors/EHBHipRoof.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Railing.h"
#include "Actors/EHB_RailingGate.h"
#include "Actors/EHB_Stair.h"
#include "Actors/EHB_Wall.h"
#include "Core/EHBBuildingActorBase.h"
#include "Diagnostics/EHBBuildingPerformanceAnalyzer.h"
#include "Engine/DataTable.h"
#include "Engine/DeveloperSettings.h"
#include "EHBBuildingToolsetSettings.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * 程序化建筑工具集的项目级配置。
 * 该类继承 UDeveloperSettings，会自动显示在 Project Settings 中；
 * 这里集中配置工具运行时要创建的建筑对象、场景元素 Actor、采样表和默认资源。
 */
UCLASS(Config = Game, DefaultConfig, DisplayName = "程序化建筑工具集", meta = (ToolTip = "配置程序化建筑工具集中建筑对象和各类建筑元素的默认类。"))
class EASYHOUSEBUILDER_API UEHBBuildingToolsetSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEHBBuildingToolsetSettings();

	/** 设置面板所属的大分类。Plugins 会显示在项目设置的插件分类下。 */
	virtual FName GetCategoryName() const override;

	/** 设置面板内部唯一节名，用于保存配置和定位项目设置页面。 */
	virtual FName GetSectionName() const override;

#if WITH_EDITOR
	/** 项目设置中显示的中文标题。 */
	virtual FText GetSectionText() const override;

	/** 项目设置中显示的中文说明。 */
	virtual FText GetSectionDescription() const override;
#endif

	/** 编辑器创建建筑对象时默认使用的 Actor 类。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "建筑类", meta = (DisplayName = "建筑对象类", ToolTip = "点击“创建建筑对象”按钮时默认生成的建筑 Actor 类。可以配置为 AEHBBuildingActorBase 的任意蓝图或 C++ 子类。"))
	TSoftClassPtr<AEHBBuildingActorBase> BuildingActorClass;

	/** 墙体工具创建独立墙体 Actor 时使用的默认类。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "元素类", meta = (DisplayName = "墙体Actor类", ToolTip = "墙体面板或拖拽创建墙体时默认生成的墙体 Actor 类。可以替换为 AEHB_Wall 的蓝图或 C++ 子类，用于扩展墙面生成、采样墙面、洞口和保存逻辑。"))
	TSoftClassPtr<AEHB_Wall> WallActorClass;

	/** 柱子工具或墙体端点自动创建柱子 Actor 时使用的默认类。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "元素类", meta = (DisplayName = "柱子Actor类", ToolTip = "柱子工具或墙体端点自动创建柱子时使用的默认 Actor 类。可以替换为 AEHB_Pillar 的蓝图或 C++ 子类，用于扩展柱子生成、选中编辑和保存逻辑。"))
	TSoftClassPtr<AEHB_Pillar> PillarActorClass;

	/** 默认创建墙体和柱子时使用的白模材质；为空时将保留组件默认材质。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "材质", meta = (DisplayName = "默认白模材质", ToolTip = "默认创建程序化墙体和柱子时自动应用的材质。建议使用浅色、无纹理或弱纹理材质作为白模/灰盒阶段的统一外观；如果单个墙体或柱子已经设置了覆盖材质，则不会被这里的默认材质替换。"))
	TSoftObjectPtr<UMaterialInterface> DefaultWhiteBoxMaterial;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "材质|默认屋顶", meta = (DisplayName = "默认屋顶坡面材质", ToolTip = "默认屋顶没有单独设置屋面材质时，用于山形屋顶和四坡屋顶坡面主体的材质。"))
	TSoftObjectPtr<UMaterialInterface> DefaultRoofSlopeMaterial;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "材质|默认屋顶", meta = (DisplayName = "默认屋顶侧墙材质", ToolTip = "默认屋顶没有单独设置侧墙材质时，用于山墙端面和四坡屋顶侧面封口的材质。"))
	TSoftObjectPtr<UMaterialInterface> DefaultRoofSideWallMaterial;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "材质|默认屋顶", meta = (DisplayName = "默认屋脊材质", ToolTip = "屋顶实例没有单独设置屋脊材质时，用于自动生成屋脊组件的默认材质。"))
	TSoftObjectPtr<UMaterialInterface> DefaultRoofRidgeMaterial;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "材质|默认屋顶", meta = (DisplayName = "默认檐口材质", ToolTip = "屋顶实例没有单独设置檐口材质时，用于自动生成檐口组件的默认材质。"))
	TSoftObjectPtr<UMaterialInterface> DefaultRoofEaveMaterial;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "材质|默认屋顶", meta = (DisplayName = "默认斜屋脊材质", ToolTip = "屋顶实例没有单独设置斜边材质时，用于山形屋顶斜边和四坡屋顶斜屋脊组件的默认材质。"))
	TSoftObjectPtr<UMaterialInterface> DefaultRoofDiagonalRidgeMaterial;

	/** 门窗采样和门窗工具默认使用的 Actor 类。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "元素类", meta = (DisplayName = "门窗Actor类", ToolTip = "门窗采样生成蓝图时默认选择的父类，也是后续门窗工具创建门窗 Actor 时使用的默认类。可以替换为 AEHB_DoorWindow 的蓝图或 C++ 子类。"))
	TSoftClassPtr<AEHB_DoorWindow> DoorWindowActorClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "元素类", meta = (DisplayName = "默认门Actor类", ToolTip = "门窗页面默认门拖拽创建时使用的 AEHB_DoorWindow 子类。未设置时回退到门窗 Actor 类。"))
	TSoftClassPtr<AEHB_DoorWindow> DefaultDoorActorClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "元素类", meta = (DisplayName = "默认窗Actor类", ToolTip = "门窗页面默认窗拖拽创建时使用的 AEHB_DoorWindow 子类。未设置时回退到门窗 Actor 类。"))
	TSoftClassPtr<AEHB_DoorWindow> DefaultWindowActorClass;

	/** 墙体工具默认读取的墙面采样数据表。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "数据表", meta = (DisplayName = "默认墙面采样表", ToolTip = "墙体工具页中墙面覆盖功能默认读取的墙面采样数据表。选择新的数据表后会写入配置文件，后续打开编辑器模式时自动读取。该数据表的行结构应为“墙面网格体数据 / FEHBWallMeshData”。"))
	TSoftObjectPtr<UDataTable> DefaultWallMeshDataTable;

	/** 墙体工具柱体页默认读取的柱体采样数据表。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "数据表", meta = (DisplayName = "默认柱体采样表", ToolTip = "墙体工具柱体页默认读取的柱体采样数据表。选择新的数据表后会写入配置文件，后续打开编辑器模式时自动读取。该数据表的行结构应为“柱体网格体数据 / FEHBPillarMeshData”。"))
	TSoftObjectPtr<UDataTable> DefaultPillarMeshDataTable;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "数据表", meta = (DisplayName = "默认扶手采样表", ToolTip = "扶手采样页默认读取的扶手采样数据表。该数据表的行结构应为“扶手网格体数据 / FEHBRailingMeshData”。"))
	TSoftObjectPtr<UDataTable> DefaultRailingMeshDataTable;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Data Tables", meta = (DisplayName = "Default Roof Sample Table", ToolTip = "Default data table used by the roof mesh sampling page. Row structure must be FEHBRoofMeshData."))
	TSoftObjectPtr<UDataTable> DefaultRoofMeshDataTable;

	/** 门窗工具默认读取的采样数据表。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "数据表", meta = (DisplayName = "默认门窗采样表", ToolTip = "门窗工具页默认读取的门窗采样数据表。你在门窗页面选择新的数据表后，工具会把它写入配置文件，后续打开编辑器模式时会自动读取。该数据表的行结构应为“门窗网格体数据 / FEHBDoorWindowMeshData”。"))
	TSoftObjectPtr<UDataTable> DefaultDoorWindowMeshDataTable;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "元素类", meta = (DisplayName = "山形屋顶Actor类", ToolTip = "屋顶工具创建默认山形屋顶时使用的 Actor 类。可以替换为 AEHBGableRoof 的蓝图或 C++ 子类。"))
	TSoftClassPtr<AEHBGableRoof> GableRoofActorClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "元素类", meta = (DisplayName = "四坡屋顶Actor类", ToolTip = "屋顶工具创建默认四坡屋顶时使用的 Actor 类。可以替换为 AEHBHipRoof 的蓝图或 C++ 子类。"))
	TSoftClassPtr<AEHBHipRoof> HipRoofActorClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Sampling|Roof", meta = (DisplayName = "Default Roof Surface Tile Mesh", ToolTip = "Default surface tile static mesh used by the roof sampling page."))
	TSoftObjectPtr<UStaticMesh> DefaultRoofSurfaceTileMesh;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Sampling|Roof", meta = (DisplayName = "Default Roof Ridge Tile Mesh", ToolTip = "Default ridge tile static mesh used by the roof sampling page."))
	TSoftObjectPtr<UStaticMesh> DefaultRoofRidgeTileMesh;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Sampling|Roof", meta = (DisplayName = "Default Roof Valley Tile Mesh", ToolTip = "Default valley tile static mesh used by the roof sampling page."))
	TSoftObjectPtr<UStaticMesh> DefaultRoofValleyTileMesh;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "元素类", meta = (DisplayName = "楼梯Actor类", ToolTip = "楼梯工具创建程序化楼梯时使用的 Actor 类。可以替换为 AEHB_Stair 的蓝图或 C++ 子类。"))
	TSoftClassPtr<AEHB_Stair> StairActorClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "元素类", meta = (DisplayName = "扶手Actor类", ToolTip = "扶手工具创建程序化扶手组件时使用的 Actor 类。可以替换为 AEHB_Railing 的蓝图或 C++ 子类。"))
	TSoftClassPtr<AEHB_Railing> RailingActorClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "元素类", meta = (DisplayName = "扶手门Actor类", ToolTip = "扶手工具在扶手上创建门时使用的 Actor 类。可以替换为 AEHB_RailingGate 的蓝图或 C++ 子类。"))
	TSoftClassPtr<AEHB_RailingGate> RailingGateActorClass;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "性能", meta = (DisplayName = "默认性能预算", ToolTip = "建筑诊断用于标记高开销生成内容的默认预算。"))
	FEHBBuildingPerformanceBudget DefaultPerformanceBudget;
};
