#pragma once

#include "CoreMinimal.h"

namespace EHBRailingFeedback
{
	inline FText Describe(const FString& Status)
	{
		if (Status == TEXT("Ready") || Status.StartsWith(TEXT("Committed")))
			return NSLOCTEXT("EHBRailing", "Ready", "墙中扶手：水平拖拽，接近世界八方向时吸附；起点使用建筑柱；Esc 取消");
		if (Status == TEXT("Initialized") || Status == TEXT("AlreadyInitialized"))
			return NSLOCTEXT("EHBRailing", "Initialized", "墙柱连接记录已准备好。启用扶手创建工具后，从墙体中部侧面拖出扶手；具体位置仍需通过检查。");
		if (Status == TEXT("RequiresCoherentBaseline"))
			return NSLOCTEXT("EHBRailing", "Prepare", "请先点击扶手面板中的“准备墙中扶手”，再开始拖拽。");
		if (Status == TEXT("SourceChanged") || Status == TEXT("BaselineSourceChanged"))
			return NSLOCTEXT("EHBRailing", "ChangedBaseline", "墙柱连接已与原记录不同，不能自动覆盖。请撤销造成变化的编辑，或检查建筑连接记录。");
		if (Status == TEXT("UnsupportedVersion") || Status == TEXT("UnversionedData") || Status == TEXT("InvalidBaseline"))
			return NSLOCTEXT("EHBRailing", "InvalidBaseline", "建筑连接记录的版本或内容不受支持，请检查记录；本操作不会覆盖现有数据。");
		if (Status == TEXT("TargetNotSelected") || Status == TEXT("MissingBuilding"))
			return NSLOCTEXT("EHBRailing", "Selection", "请先选择当前要编辑的建筑，再重试。");
		if (Status == TEXT("RequiresIndependentEditorTransaction"))
			return NSLOCTEXT("EHBRailing", "Busy", "请先结束当前编辑或游戏预览，再重试。");
		if (Status == TEXT("EmptyTopology") || Status == TEXT("InvalidSource"))
			return NSLOCTEXT("EHBRailing", "InvalidSource", "当前建筑没有可用的墙柱连接，或存在连接冲突，请先检查墙和柱子。");
		if (Status == TEXT("StaleSource") || Status == TEXT("MissingWall"))
			return NSLOCTEXT("EHBRailing", "Stale", "原墙或建筑已改变，请重新拖拽。");
		if (Status == TEXT("UnsupportedTransform"))
			return NSLOCTEXT("EHBRailing", "Transform", "此操作暂只支持缩放为 1、未倾斜的建筑及墙体；不要直接缩放已有建筑来绕过检查。");
		if (Status.StartsWith(TEXT("Unsupported")) || Status == TEXT("RequiresDependencyMigration"))
			return NSLOCTEXT("EHBRailing", "Unsupported", "建筑含曲墙、采样、楼梯、层板或其他尚未支持的依赖，暂不能从此处拆墙创建。");
		if (Status.Contains(TEXT("Opening")))
			return NSLOCTEXT("EHBRailing", "Opening", "插柱位置与门窗冲突，或门窗绑定需要检查；请避开洞口并检查门窗位置与宿主。");
		if (Status == TEXT("TooCloseToEndpoint") || Status.StartsWith(TEXT("Insufficient")))
			return NSLOCTEXT("EHBRailing", "Clearance", "新建筑柱距墙端或其他构件太近，请调整起点位置。");
		if (Status == TEXT("RailingIntersectsSourceJunction"))
			return NSLOCTEXT("EHBRailing", "SourceJunction", "扶手起点超出柱面，或首根扶手柱距墙太近。请增大离墙角度或柱间距，再重新拖拽。");
		if (Status.StartsWith(TEXT("RailingIntersects")))
			return NSLOCTEXT("EHBRailing", "Intersection", "扶手路径与已有墙柱或扶手重叠，请缩短距离或换方向。");
		if (Status == TEXT("InvalidHorizontalRailingDimensions") || Status == TEXT("InvalidInput") || Status == TEXT("RequiresPerpendicularRailing"))
			return NSLOCTEXT("EHBRailing", "Dimensions", "请拉开距离，并检查扶手高度、厚度和柱间距；新扶手须水平且垂直离开墙面。");
		if (Status == TEXT("SplitFailedRolledBack"))
			return NSLOCTEXT("EHBRailing", "RolledBack", "本次创建失败，已撤销本次更改。");
		if (Status == TEXT("SplitFailedRollbackFailed"))
			return NSLOCTEXT("EHBRailing", "RollbackFailed", "创建失败且未能自动恢复，请停止编辑并检查建筑状态。");
		return NSLOCTEXT("EHBRailing", "Other", "建筑关联检查未通过，请检查墙柱和扶手的连接后重试。");
	}
}
