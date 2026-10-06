// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sampling/EHBDoorWindowMeshData.h"
#include "Sampling/EHBDoorWindowMeshSampler.h"
#include "Sampling/EHBPillarMeshSampler.h"
#include "Sampling/EHBRailingMeshSampler.h"
#include "Sampling/EHBRoofMeshSampler.h"
#include "Sampling/EHBWallMeshSampler.h"
#include "Types/SlateEnums.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SCompoundWidget.h"

class SBox;
class SWidgetSwitcher;
class UClass;
class UDataTable;
class UStaticMesh;
class FEasyHouseEditorMode;
struct FAssetData;

/** 网格体采样面板中的页卡。 */
enum class EEHBMeshSamplingPage : uint8
{
	Wall = 0,
	Pillar,
	Railing,
	Roof,
	DoorWindow
};


/**
 * 建筑编辑模式中的“网格体采样”主面板。
 * 该面板只负责 Slate 交互，把实际几何采样逻辑委托给具体采样器。
 */
class SEHBMeshSamplingPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEHBMeshSamplingPanel) {}
	SLATE_END_ARGS()

	/** 构建网格体采样面板，包含顶部页卡和各页内容。 */
	void Construct(const FArguments& InArgs);

private:
	/** 创建顶部页卡按钮。 */
	TSharedRef<SWidget> BuildSamplingTabButton(EEHBMeshSamplingPage Page, const FText& Label);

	/** 创建墙面采样页面。 */
	TSharedRef<SWidget> BuildWallSamplingPage();

	/** 创建柱体采样页面。 */
	TSharedRef<SWidget> BuildPillarSamplingPage();
	TSharedRef<SWidget> BuildRailingSamplingPage();
	TSharedRef<SWidget> BuildRoofSamplingPage();

	/** 创建门窗采样页面。 */
	TSharedRef<SWidget> BuildDoorWindowSamplingPage();

	/** 创建尚未展开实现的采样页面占位。 */
	TSharedRef<SWidget> BuildReservedSamplingPage(const FText& Label) const;

	/** 创建一个带标签和说明的字段区域。 */
	TSharedRef<SWidget> BuildField(const FText& Label, const FText& ToolTip, const TSharedRef<SWidget>& ValueWidget) const;

	/** 切换顶部页卡。 */
	void HandleSamplingPageChanged(ECheckBoxState NewState, EEHBMeshSamplingPage Page);

	/** 返回指定页卡是否处于选中状态。 */
	ECheckBoxState IsSamplingPageChecked(EEHBMeshSamplingPage Page) const;

	/** 数据表资产选择框使用的对象路径。 */
	FString GetWallDataTablePath() const;

	/** 静态网格体资产选择框使用的对象路径。 */
	FString GetWallStaticMeshPath() const;

	/** 数据表选择变化。 */
	void HandleWallDataTableChanged(const FAssetData& AssetData);

	/** 静态网格体选择变化。 */
	void HandleWallStaticMeshChanged(const FAssetData& AssetData);

	/** 过滤掉行结构不匹配的墙面数据表。 */
	bool ShouldFilterWallDataTable(const FAssetData& AssetData) const;

	/** 名称输入框显示的当前数据表行名。 */
	FText GetWallRowNameText() const;

	/** 名称输入变化后更新数据表行名。 */
	void HandleWallRowNameChanged(const FText& NewText);

	/** 根据当前静态网格体和高级参数刷新墙面检测信息。 */
	void RefreshWallAnalysis();

	/** 静态网格体基础信息显示文本。 */
	FText GetWallMeshInfoText() const;

	/** 采样状态显示文本。 */
	FText GetWallSamplingStatusText() const;

	/** 当前输入是否足以执行采样。 */
	bool CanSampleWall() const;

	/** 点击采样按钮后执行墙面采样并写入数据表。 */
	FReply HandleSampleWallClicked();

	/** 柱体数据表资产选择框使用的对象路径。 */
	FString GetPillarDataTablePath() const;

	/** 柱体静态网格体资产选择框使用的对象路径。 */
	FString GetPillarStaticMeshPath() const;

	/** 柱体数据表选择变化。 */
	void HandlePillarDataTableChanged(const FAssetData& AssetData);

	/** 柱体静态网格体选择变化。 */
	void HandlePillarStaticMeshChanged(const FAssetData& AssetData);

	/** 过滤掉行结构不匹配的柱体数据表。 */
	bool ShouldFilterPillarDataTable(const FAssetData& AssetData) const;

	/** 柱体名称输入框显示的当前数据表行名。 */
	FText GetPillarRowNameText() const;

	/** 柱体名称输入变化后更新数据表行名。 */
	void HandlePillarRowNameChanged(const FText& NewText);

	/** 根据当前柱体静态网格体和高级参数刷新检测信息。 */
	void RefreshPillarAnalysis();

	/** 柱体静态网格体基础信息显示文本。 */
	FText GetPillarMeshInfoText() const;

	/** 柱体采样状态显示文本。 */
	FText GetPillarSamplingStatusText() const;

	/** 当前输入是否足以执行柱体采样。 */
	bool CanSamplePillar() const;

	/** 点击采样按钮后执行柱体采样并写入数据表。 */
	FReply HandleSamplePillarClicked();

	FString GetRailingDataTablePath() const;
	FString GetRailingPostStaticMeshPath() const;
	FString GetRailingRailStaticMeshPath() const;
	FString GetRailingPanelStaticMeshPath() const;
	void HandleRailingDataTableChanged(const FAssetData& AssetData);
	void HandleRailingPostStaticMeshChanged(const FAssetData& AssetData);
	void HandleRailingRailStaticMeshChanged(const FAssetData& AssetData);
	void HandleRailingPanelStaticMeshChanged(const FAssetData& AssetData);
	bool ShouldFilterRailingDataTable(const FAssetData& AssetData) const;
	FText GetRailingRowNameText() const;
	void HandleRailingRowNameChanged(const FText& NewText);
	void RefreshRailingAnalysis();
	FText GetRailingMeshInfoText() const;
	FText GetRailingSamplingStatusText() const;
	bool CanSampleRailing() const;
	FReply HandleSampleRailingClicked();

	FString GetRoofDataTablePath() const;
	FString GetRoofSurfaceStaticMeshPath() const;
	FString GetRoofRidgeStaticMeshPath() const;
	FString GetRoofValleyStaticMeshPath() const;
	void HandleRoofDataTableChanged(const FAssetData& AssetData);
	void HandleRoofSurfaceStaticMeshChanged(const FAssetData& AssetData);
	void HandleRoofRidgeStaticMeshChanged(const FAssetData& AssetData);
	void HandleRoofValleyStaticMeshChanged(const FAssetData& AssetData);
	bool ShouldFilterRoofDataTable(const FAssetData& AssetData) const;
	FText GetRoofRowNameText() const;
	void HandleRoofRowNameChanged(const FText& NewText);
	void RefreshRoofAnalysis();
	FText GetRoofMeshInfoText() const;
	FText GetRoofSamplingStatusText() const;
	bool CanSampleRoof() const;
	FReply HandleSampleRoofClicked();

	/** 门窗数据表资产选择框使用的对象路径。 */
	FString GetDoorWindowDataTablePath() const;

	/** 门窗数据表选择变化。 */
	void HandleDoorWindowDataTableChanged(const FAssetData& AssetData);

	/** 过滤掉行结构不匹配的门窗数据表。 */
	bool ShouldFilterDoorWindowDataTable(const FAssetData& AssetData) const;

	/** 门窗数据表行名输入框显示文本。 */
	FText GetDoorWindowRowNameText() const;

	/** 门窗数据表行名输入变化。 */
	void HandleDoorWindowRowNameChanged(const FText& NewText);

	/** 门窗静态网格体资产选择框使用的对象路径。 */
	FString GetDoorWindowStaticMeshPath() const;

	/** 门窗静态网格体选择变化。 */
	void HandleDoorWindowStaticMeshChanged(const FAssetData& AssetData);

	/** 门窗蓝图父类选择框当前值。 */
	const UClass* GetSelectedDoorWindowBaseClass() const;

	/** 门窗蓝图父类选择变化。 */
	void HandleDoorWindowBaseClassChanged(const UClass* NewClass);

	/** 门窗蓝图资产名称输入框显示文本。 */
	FText GetDoorWindowAssetNameText() const;

	/** 门窗蓝图资产名称输入变化。 */
	void HandleDoorWindowAssetNameChanged(const FText& NewText);

	/** 门窗蓝图资产目录输入框显示文本。 */
	FText GetDoorWindowAssetFolderText() const;

	/** 门窗蓝图资产目录输入变化。 */
	void HandleDoorWindowAssetFolderChanged(const FText& NewText);

	/** 门窗类型切换。 */
	void HandleDoorWindowKindChanged(ECheckBoxState NewState, EEHBDoorWindowElementKind NewKind);

	/** 指定门窗类型是否选中。 */
	ECheckBoxState IsDoorWindowKindChecked(EEHBDoorWindowElementKind TestKind) const;

	/** 根据当前门窗静态网格体和高级参数刷新检测信息。 */
	void RefreshDoorWindowAnalysis();

	/** 门窗静态网格体基础信息显示文本。 */
	FText GetDoorWindowMeshInfoText() const;

	/** 门窗采样状态显示文本。 */
	FText GetDoorWindowSamplingStatusText() const;

	/** 当前输入是否足以生成门窗蓝图。 */
	bool CanCreateDoorWindowBlueprint() const;

	/** 点击按钮后检测门窗网格并生成门窗蓝图。 */
	FReply HandleCreateDoorWindowBlueprintClicked();

	/** 将生成出的门窗蓝图类和洞口参数写入门窗数据表。 */
	bool WriteDoorWindowDataTableRow(const FEHBDoorWindowBlueprintCreationResult& Result, const FEHBDoorWindowBlueprintCreationOptions& CreationOptions, FText& OutErrorMessage);
	FEasyHouseEditorMode* GetActiveEditorMode() const;

	/** 当前激活的网格体采样页。 */
	EEHBMeshSamplingPage ActivePage = EEHBMeshSamplingPage::Wall;

	/** 顶部页卡切换器。 */
	TSharedPtr<SWidgetSwitcher> PageSwitcher;

	/** 墙面采样目标数据表。 */
	TWeakObjectPtr<UDataTable> WallDataTable;

	/** 墙面采样源静态网格体。 */
	TWeakObjectPtr<UStaticMesh> WallStaticMesh;

	/** 用户输入的数据表行名；正面和反面采样会保存在同一行的两个结构体字段中。 */
	FName WallBaseRowName = TEXT("WallMesh");

	/** 墙面采样高级参数。 */
	FEHBWallMeshSamplingOptions WallSamplingOptions;

	/** 当前静态网格体的检测结果缓存。 */
	FEHBWallMeshAnalysis CachedWallAnalysis;

	/** 最近一次检测或采样产生的状态消息。 */
	FText WallSamplingStatus;

	/** 柱体采样目标数据表。 */
	TWeakObjectPtr<UDataTable> PillarDataTable;

	/** 柱体采样源静态网格体。 */
	TWeakObjectPtr<UStaticMesh> PillarStaticMesh;

	/** 用户输入的柱体数据表行名。 */
	FName PillarRowName = TEXT("PillarMesh");

	/** 柱体采样高级参数。 */
	FEHBPillarMeshSamplingOptions PillarSamplingOptions;

	/** 当前柱体静态网格体的检测结果缓存。 */
	FEHBPillarMeshAnalysis CachedPillarAnalysis;

	/** 最近一次柱体检测或采样产生的状态消息。 */
	FText PillarSamplingStatus;

	TWeakObjectPtr<UDataTable> RailingDataTable;
	TWeakObjectPtr<UStaticMesh> RailingPostStaticMesh;
	TWeakObjectPtr<UStaticMesh> RailingRailStaticMesh;
	TWeakObjectPtr<UStaticMesh> RailingPanelStaticMesh;
	FName RailingRowName = TEXT("RailingMesh");
	FEHBRailingMeshSamplingOptions RailingSamplingOptions;
	FEHBRailingMeshAnalysis CachedRailingAnalysis;
	FText RailingSamplingStatus;

	TWeakObjectPtr<UDataTable> RoofDataTable;
	TWeakObjectPtr<UStaticMesh> RoofSurfaceStaticMesh;
	TWeakObjectPtr<UStaticMesh> RoofRidgeStaticMesh;
	TWeakObjectPtr<UStaticMesh> RoofValleyStaticMesh;
	FName RoofRowName = TEXT("RoofMesh");
	FEHBRoofMeshSamplingOptions RoofSamplingOptions;
	FEHBRoofMeshAnalysis CachedRoofAnalysis;
	FText RoofSamplingStatus;

	/** 门窗采样目标数据表。 */
	TWeakObjectPtr<UDataTable> DoorWindowDataTable;

	/** 用户输入的门窗数据表行名。 */
	FName DoorWindowRowName = TEXT("DoorWindowMesh");

	/** 门窗采样源静态网格体。 */
	TWeakObjectPtr<UStaticMesh> DoorWindowStaticMesh;

	/** 门窗蓝图基类，默认来自项目设置中的门窗 Actor 类。 */
	TWeakObjectPtr<UClass> DoorWindowBaseClass;

	/** 生成门窗蓝图的资产目录。 */
	FString DoorWindowAssetFolder = TEXT("/Game/EHB_DoorWindows");

	/** 生成门窗蓝图的资产名称。 */
	FString DoorWindowAssetName = TEXT("BP_DoorWindow");

	/** 当前门窗采样类型。 */
	EEHBDoorWindowElementKind DoorWindowKind = EEHBDoorWindowElementKind::Window;

	/** 窗台高度或洞口底边离地高度。门会强制使用 0。 */
	float DoorWindowSillHeight = 90.0f;

	/** 门窗采样高级参数。 */
	FEHBDoorWindowMeshSamplingOptions DoorWindowSamplingOptions;

	/** 当前门窗静态网格体检测结果缓存。 */
	FEHBDoorWindowMeshAnalysis CachedDoorWindowAnalysis;

	/** 最近一次门窗检测或蓝图生成产生的状态消息。 */
	FText DoorWindowSamplingStatus;
};
