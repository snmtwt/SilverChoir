#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "GSMEditorToolSubsystem.generated.h"

class AGSMTile3D;
class UGSMMapDataAsset;

UENUM(BlueprintType)
enum class EGSMValidationSeverity : uint8
{
	Info UMETA(DisplayName = "信息"),
	Warning UMETA(DisplayName = "警告"),
	Error UMETA(DisplayName = "错误")
};

USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEMEDITOR_API FGSMValidationIssue
{
	GENERATED_BODY()

	/** 问题严重级别。错误会让整体校验结果变为无效，警告和信息只用于提示。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|校验", meta = (DisplayName = "严重级别", ToolTip = "问题严重级别。错误会让整体校验结果变为无效。"))
	EGSMValidationSeverity Severity = EGSMValidationSeverity::Info;

	/** 问题关联的瓦片标识。为空表示问题属于整个配置资产。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|校验", meta = (DisplayName = "瓦片标识", ToolTip = "问题关联的瓦片标识。为空表示问题属于整个配置资产。"))
	FName TileId = NAME_None;

	/** 面向用户显示的校验消息。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|校验", meta = (DisplayName = "消息", ToolTip = "面向用户显示的校验消息。"))
	FText Message;
};

USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEMEDITOR_API FGSMValidationResult
{
	GENERATED_BODY()

	/** 配置是否通过校验。只要存在错误级别问题，该值就会变为 false。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|校验", meta = (DisplayName = "是否有效", ToolTip = "配置是否通过校验。只要存在错误级别问题，该值就会变为 false。"))
	bool bIsValid = true;

	/** 所有校验问题。编辑器工具可逐条展示。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|校验", meta = (DisplayName = "问题列表", ToolTip = "所有校验问题。编辑器工具可逐条展示。"))
	TArray<FGSMValidationIssue> Issues;

	/** 校验摘要文本。用于状态栏或日志快速显示。 */
	UPROPERTY(BlueprintReadOnly, Category = "网格策略地图|校验", meta = (DisplayName = "摘要", ToolTip = "校验摘要文本。用于状态栏或日志快速显示。"))
	FText Summary;

	void Reset();
	void AddIssue(EGSMValidationSeverity Severity, FName TileId, const FText& Message);
};

USTRUCT(BlueprintType)
struct GRIDSTRATEGYMAPSYSTEMEDITOR_API FGSMRectSquareGenerateSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|生成", meta = (DisplayName = "列数", ClampMin = "1"))
	int32 Columns = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|生成", meta = (DisplayName = "行数", ClampMin = "1"))
	int32 Rows = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|生成", meta = (DisplayName = "起始位置"))
	FVector Origin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|生成", meta = (DisplayName = "区域标识"))
	FName RegionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|生成", meta = (DisplayName = "瓦片对象类"))
	TSubclassOf<AGSMTile3D> TileActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|生成", meta = (DisplayName = "清空已有瓦片"))
	bool bClearExistingTiles = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|生成", meta = (DisplayName = "自动填写步行邻接"))
	bool bAutoFillWalkingNeighbors = true;
};

/**
 * 网格策略地图编辑器配置工具。
 *
 * 这是插件的第一版配置工具入口，可在 Editor Utility Blueprint/Widget 中通过
 * “Get Editor Subsystem” 获取并调用。
 */
UCLASS(meta = (DisplayName = "网格策略地图配置工具"))
class GRIDSTRATEGYMAPSYSTEMEDITOR_API UGSMEditorToolSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/** 在配置资产中生成矩形排列的四边形瓦片，并标记资产已修改。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|配置工具", meta = (DisplayName = "生成矩形四边形地图", ToolTip = "在配置资产中生成矩形排列的四边形瓦片，并标记资产已修改。"))
	bool GenerateRectangularSquareMap(
		UPARAM(DisplayName = "地图配置") UGSMMapDataAsset* MapConfig,
		UPARAM(DisplayName = "生成设置") const FGSMRectSquareGenerateSettings& Settings
	);

	/** 根据瓦片格子坐标自动填写步行邻接列表。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|配置工具", meta = (DisplayName = "自动填写步行邻接", ToolTip = "根据瓦片格子坐标自动填写步行邻接列表，并标记资产已修改。"))
	bool AutoFillWalkingNeighbors(
		UPARAM(DisplayName = "地图配置") UGSMMapDataAsset* MapConfig
	);

	/** 在两个瓦片之间添加双向道路连接。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|配置工具", meta = (DisplayName = "添加双向道路连接", ToolTip = "在两个瓦片之间添加双向道路连接，并标记资产已修改。"))
	bool AddBidirectionalRoadConnection(
		UPARAM(DisplayName = "地图配置") UGSMMapDataAsset* MapConfig,
		UPARAM(DisplayName = "瓦片A") FName TileAId,
		UPARAM(DisplayName = "瓦片B") FName TileBId,
		UPARAM(DisplayName = "代价覆盖") float CostOverride
	);

	/** 校验地图配置资产中的瓦片标识、格子坐标和导航引用。 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|配置工具", meta = (DisplayName = "校验地图配置", ToolTip = "校验地图配置资产中的瓦片标识、格子坐标和导航引用。"))
	bool ValidateMapConfig(
		UPARAM(DisplayName = "地图配置") UGSMMapDataAsset* MapConfig,
		UPARAM(DisplayName = "校验结果") FGSMValidationResult& OutResult
	) const;

protected:
	static FVector SquareGridToWorld(FIntPoint GridCoordinate, int32 ColumnCount, int32 RowCount, const FVector& Origin);
	static void MarkConfigModified(UGSMMapDataAsset* MapConfig);
};
