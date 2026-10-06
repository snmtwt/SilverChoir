// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class FDragDropOperation;
class STextBlock;
class SWrapBox;
class UDataTable;
class UStaticMesh;
struct FAssetData;
struct FEHBDoorWindowMeshData;
enum class EEHBDoorWindowElementKind : uint8;

/**
 * 建筑编辑模式中的“门窗”工具面板。
 * 该面板从门窗采样数据表读取模板，显示为带缩略图的卡片列表，后续会在这里继续接入拖拽到墙体创建门窗的交互。
 */
class SEHBDoorWindowPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SEHBDoorWindowPanel) {}
	SLATE_END_ARGS()

	/** 构建门窗工具面板，并从项目设置中读取默认门窗采样表。 */
	void Construct(const FArguments& InArgs);

	/** 为指定门窗数据表行创建拖拽操作，拖入视口后会生成跟随鼠标的门窗预览。 */
	TSharedRef<FDragDropOperation> CreateDoorWindowDragDropOperationForRow(FName RowName);
	TSharedRef<FDragDropOperation> CreateDefaultDoorWindowDragDropOperation(EEHBDoorWindowElementKind Kind);

private:
	// ===== UI 构建 =====

	/** 创建顶部数据表选择和刷新区域。 */
	TSharedRef<SWidget> BuildTablePicker();
	TSharedRef<SWidget> BuildDefaultDoorWindowButtons();

	/** 创建门窗卡片滚动区域。 */
	TSharedRef<SWidget> BuildCardsArea();

	/** 根据指定行创建一张门窗模板卡片。 */
	TSharedRef<SWidget> BuildDoorWindowCard(FName RowName);

	// ===== 数据表 =====

	/** 数据表资产选择框使用的对象路径。 */
	FString GetDoorWindowTablePath() const;

	/** 数据表选择变化后缓存到项目设置，并刷新卡片。 */
	void HandleDoorWindowTableChanged(const FAssetData& AssetData);

	/** 过滤掉行结构不匹配的门窗数据表。 */
	bool ShouldFilterDoorWindowTable(const FAssetData& AssetData) const;

	/** 点击刷新按钮后重新读取当前数据表行。 */
	FReply HandleRefreshDoorWindowRowsClicked();

	/** 重新从数据表读取行名并刷新卡片区域。 */
	void RefreshDoorWindowRows();

	/** 将当前选择的数据表写入项目设置配置文件。 */
	void SaveDoorWindowTableToConfig() const;

	/** 从项目设置读取默认门窗表。 */
	void LoadDoorWindowTableFromConfig();

	/** 尝试重新加载当前数据表对象，确保外部修改后刷新能读到最新对象。 */
	void ReloadDoorWindowTable();

	// ===== 卡片操作 =====

	/** 删除指定门窗数据表行。 */
	FReply HandleDeleteDoorWindowRow(FName RowName);

	/** 在内容浏览器中定位指定门窗行对应的蓝图资产。 */
	FReply HandleLocateDoorWindowBlueprint(FName RowName) const;

	/** 获取某一行的预览静态网格体。 */
	UStaticMesh* GetPreviewMeshFromRow(const FEHBDoorWindowMeshData* Row) const;

	/** 在状态文本中显示提示。 */
	void SetStatusText(const FText& NewStatus) const;

private:
	/** 当前门窗采样数据表。 */
	TWeakObjectPtr<UDataTable> DoorWindowTable;

	/** 当前数据表中可显示的行名。 */
	TArray<FName> DoorWindowRowNames;

	/** 门窗卡片流式布局容器。 */
	TSharedPtr<SWrapBox> DoorWindowCardsBox;

	/** 门窗页面底部状态提示。 */
	TSharedPtr<STextBlock> StatusTextBlock;

	/** 缩略图池，保证卡片中的资产缩略图能复用并持续显示。 */
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;

	/** 当前卡片持有的缩略图对象。 */
	TArray<TSharedPtr<FAssetThumbnail>> DoorWindowThumbnails;
};
