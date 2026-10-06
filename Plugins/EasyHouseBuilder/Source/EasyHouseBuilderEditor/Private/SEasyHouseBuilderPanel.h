// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpFwd.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SCompoundWidget.h"

class AEHBBuildingActorBase;
class AActor;
class FEasyHouseEditorMode;
class FAssetThumbnail;
class FAssetThumbnailPool;
class FDragDropOperation;
class SEditableTextBox;
class SMultiLineEditableText;
class SEHBElementEditorPanel;
class SBox;
class SMultiLineEditableTextBox;
class SScrollBox;
class STextBlock;
class SVerticalBox;
class SWidgetSwitcher;
class SWrapBox;
class UEHBAISettings;
class UDataTable;
class UStaticMesh;
class UWorld;
struct FAssetData;
struct FEHBPillarMeshData;
struct FEHBRailingMeshData;
struct FEHBWallMeshData;

/** 建筑编辑模式左侧工具栏中的面板类型。枚举顺序需要与 WidgetSwitcher 插槽顺序保持一致。 */
enum class EEasyHouseToolPanel : uint8
{
	/** 网格体采样工具，用于从静态网格体中提取墙面等模板数据。 */
	MeshSampling = 0,

	/** 选择或创建当前编辑上下文中的建筑对象。 */
	BuildingSelection,


	/** 地基与层板参数面板。 */
	FoundationAndFloor,

	/** 墙体创建与编辑面板。 */
	Walls,

	/** 门窗或墙面洞口面板。 */
	DoorsAndWindows,

	/** 扶手、栏杆或护栏面板。 */
	Railings,

	/** 屋顶创建与编辑面板。 */
	Roof,

	/** 地板或表面铺装面板。 */
	Floor,

	/** 楼梯创建与编辑面板。 */
	Stairs
};

/** 墙体页卡内部的二级功能页。 */
enum class EEasyHouseWallSubPanel : uint8
{
	WallSurface = 0,
	Pillar
};

enum class EEasyHouseRoofCreationType : uint8
{
	Gable = 0,
	Hip,
	HalfHip,
	TwoSideSampled,
	FourSideSampled,
	FixedSampled
};

/**
 * 建筑编辑模式的主 Slate 面板。
 * 左侧是一列工具按钮，右侧通过 WidgetSwitcher 切换当前工具的参数面板。
 */
class SEasyHouseBuilderPanel : public SCompoundWidget
{
#if WITH_DEV_AUTOMATION_TESTS
	friend class FEHBNodeEditingActivationTest;
#endif
public:
	/** Synchronize the panel after a successful command; selection is handled by the command. */
	void AdoptCopiedBuilding(AEHBBuildingActorBase* Building);
	SLATE_BEGIN_ARGS(SEasyHouseBuilderPanel) {}
	SLATE_END_ARGS()

	/** 构建建筑编辑模式的主面板，包括左侧工具按钮和右侧随工具切换的内容区。 */
	void Construct(const FArguments& InArgs);

	/** 为指定墙面采样表行创建拖拽操作。 */
	TSharedRef<FDragDropOperation> CreateWallSurfaceDragDropOperationForRow(FName RowName);
	TSharedRef<FDragDropOperation> CreatePillarMeshDragDropOperationForRow(FName RowName);
	TSharedRef<FDragDropOperation> CreateRailingMeshDragDropOperationForRow(FName RowName);

	void SetSelectedElementForEditor(AActor* SelectedActor);

private:
	/** 创建左侧竖向工具按钮列表。 */
	TSharedRef<SWidget> BuildToolList();

	/** 创建单个工具按钮。按钮使用 ToggleButton 风格，选中后切换右侧面板。 */
	TSharedRef<SWidget> BuildPanelButton(EEasyHouseToolPanel Panel, const FText& Label, FName IconName);

	/** 根据工具类型创建右侧内容。当前“选择建筑对象”拥有真实功能，其他面板先保留参数区占位。 */
	TSharedRef<SWidget> BuildPanelContent(EEasyHouseToolPanel Panel, const FText& Label);

	/** 创建“选择建筑对象”面板：顶部创建按钮，下方显示当前关卡中所有建筑对象。 */
	TSharedRef<SWidget> BuildBuildingSelectionPanel();

	TSharedRef<SWidget> BuildElementEditorHost();
	void EnsureElementEditorPanel();

	TSharedRef<SWidget> BuildAICreationPanel();
	TSharedRef<SWidget> BuildAIMessageBubble(const FText& Message, bool bIsUserMessage, TSharedPtr<SMultiLineEditableText>* OutMessageTextWidget = nullptr) const;

	TSharedRef<SWidget> BuildFoundationAndFloorPanel();
	TSharedRef<SWidget> BuildRailingsPanel();
	TSharedRef<SWidget> BuildRailingMeshTablePicker();
	TSharedRef<SWidget> BuildRailingMeshCardsArea();
	TSharedRef<SWidget> BuildRailingMeshCard(FName RowName);
	FString GetRailingMeshTablePath() const;
	void HandleRailingMeshTableChanged(const FAssetData& AssetData);
	bool ShouldFilterRailingMeshTable(const FAssetData& AssetData) const;
	FReply HandleRefreshRailingMeshRowsClicked();
	void RefreshRailingMeshRows();
	void SaveRailingMeshTableToConfig() const;
	void LoadRailingMeshTableFromConfig();
	void ReloadRailingMeshTable();
	UStaticMesh* GetPreviewMeshFromRailingMeshRow(const FEHBRailingMeshData* Row) const;
	void SetRailingMeshStatusText(const FText& NewStatus) const;
	TSharedRef<SWidget> BuildFloorPanel();
	TSharedRef<SWidget> BuildRoofPanel();
	TSharedRef<SWidget> BuildRoofCreationTypeSelector();
	TSharedRef<SWidget> BuildRoofCreationTypeButton(EEasyHouseRoofCreationType RoofType, const FText& Label);
	TSharedRef<SWidget> BuildGableRoofElementOptions();
	TSharedRef<SWidget> BuildUnsupportedRoofElementOptions();
	EVisibility GetGableRoofElementOptionsVisibility() const;
	EVisibility GetUnsupportedRoofElementOptionsVisibility() const;
	void HandleRoofCreationTypeChanged(ECheckBoxState NewState, EEasyHouseRoofCreationType RoofType);
	ECheckBoxState IsRoofCreationTypeChecked(EEasyHouseRoofCreationType RoofType) const;
	bool IsSelectedRoofCreationTypeImplemented() const;
	FText GetCreateRoofButtonText() const;
	TSharedRef<SWidget> BuildStairsPanel();

	/** 创建“墙体”面板：包含墙高、墙厚度以及进入视口拖拽创建墙面的按钮。 */
	TSharedRef<SWidget> BuildWallsPanel();

	/** 创建墙体页卡内的二级页签按钮。 */
	TSharedRef<SWidget> BuildWallSubPanelButton(EEasyHouseWallSubPanel Panel, const FText& Label);

	/** 创建墙面覆盖页。 */
	TSharedRef<SWidget> BuildWallSurfacePage();

	/** 创建柱体页的占位内容。 */
	TSharedRef<SWidget> BuildWallPillarPage();

	/** 创建墙面采样表选择区。 */
	TSharedRef<SWidget> BuildWallSurfaceTablePicker();

	/** 创建墙面覆盖模式设置区。 */
	TSharedRef<SWidget> BuildWallSurfaceCoverOptions();

	/** 创建墙面采样项滚动区。 */
	TSharedRef<SWidget> BuildWallSurfaceCardsArea();

	/** 根据墙面采样表行创建一个可拖拽卡片。 */
	TSharedRef<SWidget> BuildWallSurfaceCard(FName RowName);

	/** 刷新建筑对象列表，把当前编辑世界中所有 AEHBBuildingActorBase 子类 Actor 显示成可点击按钮。 */
	void RefreshBuildingList();

	/** 点击“创建建筑对象”时执行，从当前视口中心向前射线检测并在命中点生成建筑对象。 */
	FReply HandleCreateBuildingClicked();
	FReply HandleCopyBuildingClicked();
	FReply HandleEnableWallNodeEditingClicked();

	/** 点击列表项时执行，缓存当前建筑对象并同步选中场景中的 Actor。 */
	FReply HandleSelectBuildingClicked(TWeakObjectPtr<AEHBBuildingActorBase> Building);

	/** 点击“创建墙面”时执行：检查当前建筑对象，并把墙体拖拽创建工具交给编辑模式处理。 */
	FReply HandleCreateWallClicked();

	FReply HandleCreateFoundationSlabClicked();
	FReply HandleCreateFloorSlabClicked();
	FReply HandleCreateFloorFinishClicked();
	FReply HandleCreateRailingClicked();
	FReply HandleCreateRoofClicked();
	FReply HandleCreateStairClicked();
	FReply HandleAttachAIImagesClicked();
	FReply HandleClearAIImagesClicked();
	FReply HandleSendAIMessageClicked();
	FReply HandleCopyMCPPromptClicked();
	FReply HandleCopyMCPStarterPromptClicked();
	FReply HandleGenerateBuildingFromJsonClicked();
	FReply HandleCopyCurrentBuildingDataClicked();
	void SendAIChatRequest(const FString& UserMessage, const TArray<FString>& ImagePaths);
	void SendHTTPAIChatRequest(const UEHBAISettings* AISettings, const TArray<FString>& ImagePaths);
	void SendLocalCodexChatRequest(const UEHBAISettings* AISettings, const FString& UserMessage, const TArray<FString>& ImagePaths);
	void HandleAIChatResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	void HandleLocalCodexChatBridgeResult(const FString& Output, const FString& ErrorOutput, int32 ReturnCode, bool bStarted);
	EActiveTimerReturnType HandleLocalCodexProgressTimer(double InCurrentTime, float InDeltaTime);
	void PollLocalCodexProgress();
	void HandleLocalCodexProgressLine(const FString& JsonLine);
	void RefreshAIImageAttachmentList();
	FString BuildAIUserVisibleMessage(const FString& UserMessage, const TArray<FString>& ImagePaths) const;
	FString BuildLocalCodexImageAttachmentPromptBlock(const TArray<FString>& ImagePaths) const;
	bool BuildAIImageDataUrl(const FString& ImagePath, FString& OutDataUrl, FText& OutError) const;
	FString GetAIImageMimeType(const FString& ImagePath) const;
	void AppendAIConversationMessage(const FText& Message, bool bIsUserMessage);
	void StartAIResponseMessage(const FText& Message);
	void UpdateAIResponseMessage(const FText& Message, int32 Percent);
	void FinishAIResponseMessage(const FText& Message);
	int32 EstimateAIResponseProgressPercent(const FString& Type, const FString& Message) const;
	FText FormatAIResponseMessage(const FText& Message, int32 Percent, bool bCompleted) const;
	FString LoadAICreationSystemPrompt() const;
	FString BuildEmbeddedAIHiddenPrompt() const;
	FString BuildEmbeddedAIRequestBody(const UEHBAISettings* AISettings, const TArray<FString>& ImagePaths) const;
	FString BuildLocalCodexDeveloperInstructions() const;
	FString BuildLocalCodexChatPrompt(const FString& UserMessage, const TArray<FString>& ImagePaths) const;
	FString GetLocalCodexChatBridgeScriptPath() const;
	FString BuildCurrentBuildingAIContext() const;
	FString BuildDetailedCurrentBuildingAIContext() const;
	FText ExtractAIResponseText(const FString& ResponseContent) const;

	/** 墙体二级页签切换。 */
	void HandleWallSubPanelSelectionChanged(ECheckBoxState NewState, EEasyHouseWallSubPanel Panel);

	/** 返回墙体二级页签按钮是否选中。 */
	ECheckBoxState IsWallSubPanelChecked(EEasyHouseWallSubPanel Panel) const;

	/** 墙面采样表资产选择框使用的对象路径。 */
	FString GetWallSurfaceTablePath() const;

	/** 墙面采样表选择变化。 */
	void HandleWallSurfaceTableChanged(const FAssetData& AssetData);

	/** 过滤掉行结构不匹配的墙面采样表。 */
	bool ShouldFilterWallSurfaceTable(const FAssetData& AssetData) const;

	/** 重新读取墙面采样表行。 */
	FReply HandleRefreshWallSurfaceRowsClicked();

	/** 刷新墙面采样卡片。 */
	void RefreshWallSurfaceRows();

	/** 保存默认墙面采样表到项目配置。 */
	void SaveWallSurfaceTableToConfig() const;

	/** 从项目配置读取默认墙面采样表。 */
	void LoadWallSurfaceTableFromConfig();

	/** 重新加载当前墙面采样表对象。 */
	void ReloadWallSurfaceTable();

	/** 取得某一墙面采样行的预览网格体。 */
	UStaticMesh* GetPreviewMeshFromWallSurfaceRow(const FEHBWallMeshData* Row) const;

	/** 更新墙面页底部状态。 */
	void SetWallSurfaceStatusText(const FText& NewStatus) const;

	TSharedRef<SWidget> BuildPillarMeshTablePicker();
	TSharedRef<SWidget> BuildPillarMeshCardsArea();
	TSharedRef<SWidget> BuildPillarMeshCard(FName RowName);
	FString GetPillarMeshTablePath() const;
	void HandlePillarMeshTableChanged(const FAssetData& AssetData);
	bool ShouldFilterPillarMeshTable(const FAssetData& AssetData) const;
	FReply HandleRefreshPillarMeshRowsClicked();
	void RefreshPillarMeshRows();
	void SavePillarMeshTableToConfig() const;
	void LoadPillarMeshTableFromConfig();
	void ReloadPillarMeshTable();
	UStaticMesh* GetPreviewMeshFromPillarMeshRow(const FEHBPillarMeshData* Row) const;
	void SetPillarMeshStatusText(const FText& NewStatus) const;

	/** 获取当前编辑器世界；无有效编辑世界时返回空。 */
	UWorld* GetEditorWorld() const;

	/** 计算建筑对象生成位置：优先使用当前视口中心射线命中点，未命中时使用相机前方备用点。 */
	bool GetViewportCenterPlacementLocation(UWorld* World, FVector& OutLocation) const;

	/** 将指定建筑对象设为当前上下文，并同步 UE 编辑器选择集。 */
	void SetActiveBuilding(AEHBBuildingActorBase* Building);

	/** 返回当前激活的程序化建筑编辑模式，供面板把工具参数同步给视口交互逻辑。 */
	TSharedRef<SWidget> BuildCreationAssistControls(bool bWall=false);
	FEasyHouseEditorMode* GetActiveEditorMode() const;

	/** 取得建筑对象在列表中的显示文字。 */
	FText GetBuildingDisplayText(const AEHBBuildingActorBase* Building) const;

	/** 统一切换右侧面板，并在进入选择建筑对象页时刷新列表。 */
	void SwitchToPanel(EEasyHouseToolPanel Panel);

	/** 工具按钮选中状态变化时切换右侧面板。 */
	void HandlePanelSelectionChanged(ECheckBoxState NewState, EEasyHouseToolPanel Panel);

	/** 返回指定工具按钮是否处于选中状态。 */
	ECheckBoxState IsPanelChecked(EEasyHouseToolPanel Panel) const;
	bool IsPanelEnabled(EEasyHouseToolPanel Panel) const;
	FText GetPanelButtonToolTipText(EEasyHouseToolPanel Panel, FText Label) const;

	/** 当前右侧显示的工具面板。 */
	EEasyHouseToolPanel ActivePanel = EEasyHouseToolPanel::MeshSampling;

	/** 当前选中的建筑对象，后续所有元素创建操作都会以它作为上下文。 */
	TWeakObjectPtr<AEHBBuildingActorBase> ActiveBuilding;

	/** 缓存当前关卡查询到的建筑对象，便于列表按钮持有弱引用。 */
	TArray<TWeakObjectPtr<AEHBBuildingActorBase>> CachedBuildingActors;

	/** 右侧面板切换器。 */
	TSharedPtr<SWidgetSwitcher> PanelSwitcher;

	/** “选择建筑对象”页面中的建筑列表容器，刷新时会重建子项。 */
	TSharedPtr<SVerticalBox> BuildingListBox;
	TSharedPtr<SBox> ElementEditorHost;
	TSharedPtr<SEHBElementEditorPanel> ElementEditorPanel;

	/** 选择建筑对象页面中的提示文本，用于从其他工具页跳转过来时告诉用户缺少当前建筑对象。 */
	TSharedPtr<STextBlock> BuildingSelectionHintText;

	TSharedPtr<SScrollBox> AIConversationScrollBox;
	TSharedPtr<SVerticalBox> AIMessageListBox;
	TSharedPtr<SEditableTextBox> AIInputTextBox;
	TSharedPtr<SVerticalBox> AIImageAttachmentListBox;
	TSharedPtr<SMultiLineEditableTextBox> AIJsonTestTextBox;
	TSharedPtr<STextBlock> AIJsonGenerationStatusText;
	TSharedPtr<SMultiLineEditableText> ActiveAIResponseTextWidget;
	TArray<FString> PendingAIImagePaths;
	TArray<TPair<bool, FString>> EmbeddedAIConversationHistory;
	FString LocalCodexThreadId;
	FString LocalCodexProgressPath;
	int32 LocalCodexProgressLineCount = 0;
	int32 ActiveAIProgressPercent = 0;
	bool bAIRequestInFlight = false;

	static constexpr int32 AIResponseProgressMaxInFlightPercent = 95;

	/** 墙体拖拽创建工具使用的默认墙高，单位为厘米。 */
	float WallCreationHeight = 300.0f;

	/** 墙体拖拽创建工具使用的默认墙厚度，单位为厘米。 */
	float WallCreationThickness = 20.0f;
	bool bWallCreationPhysicalColumns = true;
	float FloorSlabCreationSize = 500.0f;
	float FloorSlabCreationThickness = 20.0f;
	float FloorFinishCreationSize = 300.0f;
	float StairCreationHeight = 280.0f;
	float StairCreationWidth = 150.0f;
	float StairCreationTreadDepth = 30.0f;
	bool bStairCreationGenerateTreads = true;
	bool bStairCreationFillRisers = true;
	bool bStairCreationFillBottomPart = false;
	bool bStairCreationGenerateSides = true;
	bool bStairCreationGenerateSideGuards = false;
	bool bStairCreationGenerateRailing = false;
	bool bStairCreationGenerateLeftRailing = true;
	bool bStairCreationGenerateRightRailing = true;
	int32 StairCreationRailingStepsPerPost = 2;
	float StairCreationRailingEdgeInset = 10.0f;
	float StairCreationRailingPostForwardOffset = 0.0f;
	float RailingCreationHeight = 100.0f;
	float RailingCreationPostSpacing = 120.0f;
	float RailingCreationThickness = 8.0f;
	TSharedPtr<STextBlock> RailingCreationStatusText;
	TWeakObjectPtr<UDataTable> RailingMeshTable;
	TArray<FName> RailingMeshRowNames;
	TSharedPtr<SWrapBox> RailingMeshCardsBox;
	TSharedPtr<STextBlock> RailingMeshStatusText;
	TSharedPtr<FAssetThumbnailPool> RailingMeshThumbnailPool;
	TArray<TSharedPtr<FAssetThumbnail>> RailingMeshThumbnails;
	bool bApplyRailingMeshToSinglePost = false;

	/** 墙体面板底部的状态提示，用于反馈工具是否已经进入视口拖拽状态。 */
	TSharedPtr<STextBlock> WallCreationStatusText;

	/** 当前墙体页卡内部显示的二级页。 */
	EEasyHouseWallSubPanel ActiveWallSubPanel = EEasyHouseWallSubPanel::WallSurface;

	/** 墙体页卡内部的二级内容切换器。 */
	TSharedPtr<SWidgetSwitcher> WallSubPanelSwitcher;

	/** 当前墙面覆盖使用的采样表。 */
	TWeakObjectPtr<UDataTable> WallSurfaceTable;

	/** 当前墙面采样表中的可显示行名。 */
	TArray<FName> WallSurfaceRowNames;

	/** 墙面采样卡片流式布局容器。 */
	TSharedPtr<SWrapBox> WallSurfaceCardsBox;

	/** 墙面覆盖页底部状态提示。 */
	TSharedPtr<STextBlock> WallSurfaceStatusText;

	/** 墙面采样卡片缩略图池。 */
	TSharedPtr<FAssetThumbnailPool> WallSurfaceThumbnailPool;

	/** 当前墙面采样卡片持有的缩略图对象。 */
	TArray<TSharedPtr<FAssetThumbnail>> WallSurfaceThumbnails;

	/** 覆盖模式：true 时同时覆盖左右两侧，false 时只覆盖拖拽命中的一侧。 */
	bool bWallSurfaceCoverBothSides = true;

	/** 覆盖模式：交换采样表行中的正面和反面。 */
	bool bWallSurfaceFlipSampleSides = false;

	TWeakObjectPtr<UDataTable> PillarMeshTable;
	TArray<FName> PillarMeshRowNames;
	TSharedPtr<SWrapBox> PillarMeshCardsBox;
	TSharedPtr<STextBlock> PillarMeshStatusText;
	TSharedPtr<FAssetThumbnailPool> PillarMeshThumbnailPool;
	TArray<TSharedPtr<FAssetThumbnail>> PillarMeshThumbnails;

	EEasyHouseRoofCreationType ActiveRoofCreationType = EEasyHouseRoofCreationType::Gable;
	float RoofCreationLength = 600.0f;
	float RoofCreationWidth = 500.0f;
	float RoofCreationPitchDegrees = 25.0f;
	float RoofCreationThickness = 20.0f;
	float RoofCreationEaveOffset = 0.0f;
	bool bRoofCreationGenerateRidge = true;
	bool bRoofCreationGenerateEaves = true;
	bool bRoofCreationGenerateGableRakes = true;
	bool bRoofCreationGenerateGableEndWalls = true;
	float RoofCreationGableEndWallBoundaryInset = 0.0f;
};
