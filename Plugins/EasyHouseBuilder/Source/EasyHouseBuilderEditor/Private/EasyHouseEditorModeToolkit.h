// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Toolkits/BaseToolkit.h"

class AActor;
class AEHBBuildingActorBase;
class AEHB_FloorSlab;
class FSpawnTabArgs;
class SDockTab;
class SEHBElementEditorPanel;
class SEasyHouseBuilderPanel;
class SWidget;

/**
 * 建筑编辑模式的 Toolkit。
 * Toolkit 是 UE 编辑器模式 UI 的承载对象，负责把 Slate 面板挂到模式面板区域。
 */
class FEasyHouseEditorModeToolkit : public FModeToolkit
{
public:
	static void RegisterGlobalElementEditorTabSpawner();
	static void UnregisterGlobalElementEditorTabSpawner();

	/** 初始化 Toolkit 并创建主面板。 */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	/** Toolkit 唯一名称，用于编辑器内部识别。 */
	virtual FName GetToolkitFName() const override;

	/** Toolkit 在 UI 中显示的基础名称。 */
	virtual FText GetBaseToolkitName() const override;

	/** 返回当前激活的建筑编辑模式实例。 */
	virtual FEdMode* GetEditorMode() const override;

	/** 返回要嵌入模式面板的 Slate 内容。 */
	virtual TSharedPtr<SWidget> GetInlineContent() const override;

	void SetSelectedElement(AActor* SelectedActor);
	void SetSelectedFloorSlabCorner(AEHB_FloorSlab* FloorSlab, int32 LoopIndex, int32 PointIndex);
	void ShutdownElementEditor();
	void AdoptCopiedBuilding(AEHBBuildingActorBase* Building);

private:
	void RegisterElementEditorTabSpawner();
	void UnregisterElementEditorTabSpawner();
	void EnsureElementEditorTab();
	TSharedRef<SDockTab> SpawnElementEditorTab(const FSpawnTabArgs& Args);
	void HandleElementEditorTabClosed(TSharedRef<SDockTab> ClosedTab);

	/** 建筑工具集主面板，包含左侧工具列表和右侧参数区。 */
	TSharedPtr<SEasyHouseBuilderPanel> ToolsetWidget;
	TSharedPtr<SEHBElementEditorPanel> ElementEditorPanel;
	TWeakPtr<SDockTab> ElementEditorTab;
	bool bElementEditorTabSpawnerRegistered = false;
};
