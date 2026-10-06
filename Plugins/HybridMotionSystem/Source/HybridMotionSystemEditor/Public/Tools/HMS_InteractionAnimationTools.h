#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HMS_InteractionAnimationTools.generated.h"

class UAnimSequence;
class UPoseSearchDatabase;

/** Authoring settings, in source sequence seconds. These do not change a StateTree or Chooser. */
UCLASS(BlueprintType)
class HYBRIDMOTIONSYSTEMEDITOR_API UHMS_InteractionAnimationSettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="动画", meta=(DisplayName="当前动画"))
	TObjectPtr<UAnimSequence> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="接入范围", meta=(DisplayName="允许接入开始（秒）", ClampMin="0", ToolTip="在转身前保留一小段步态供 Pose Search 匹配；不是必须导航到的固定起点。"))
	float SearchStart = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="接入范围", meta=(DisplayName="允许接入结束（秒）", ClampMin="0", ToolTip="此后仍继续播放，但不能被 Pose Search 选作新的起播时间。"))
	float SearchEnd = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="对齐", meta=(DisplayName="对齐结束 / 接触时刻（秒）", ClampMin="0", ToolTip="Motion Warping 从接入开始持续到此时刻。根位移停止时刻只是初始建议，请按实际贴靠姿态确认。"))
	float ContactTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="对齐", meta=(DisplayName="对齐目标名称"))
	FName WarpTargetName = TEXT("SmartObject");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="生成", meta=(DisplayName="开启根运动"))
	bool bEnableRootMotion = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="生成", meta=(DisplayName="生成根轨迹参考曲线", ToolTip="从已有根骨烘焙位置、Yaw 和到接触时刻的剩余路程；用于检查，不会自动成为 Pose Search 查询轨迹。"))
	bool bBakeTrajectoryCurves = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="查询", meta=(DisplayName="进入查询数据库", ToolTip="可选：选择使用单段 Montage 的 HMS 进入数据库。生成通知后点击更新查询范围，重新读取通知并构建索引。"))
	TObjectPtr<UPoseSearchDatabase> EntryDatabase;
};

/** Shared implementation for the animation editor UI and scripted authoring/validation. */
UCLASS()
class HYBRIDMOTIONSYSTEMEDITOR_API UHMS_InteractionAnimationTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static UHMS_InteractionAnimationSettings* SuggestSettings(UAnimSequence* Animation);

	/** Updates only HMS-owned markers and curves. Undoable; does not save assets. */
	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static bool Generate(UHMS_InteractionAnimationSettings* Settings, FText& Result);

	UFUNCTION(BlueprintCallable, Category="HMS|Editor|Interaction")
	static FText Analyze(UAnimSequence* Animation);

	static void OpenWindow(UAnimSequence* Animation);
};
