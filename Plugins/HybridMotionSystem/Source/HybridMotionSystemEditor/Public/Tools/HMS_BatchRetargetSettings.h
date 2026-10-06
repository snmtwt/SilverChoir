#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HMS_BatchRetargetSettings.generated.h"

class UIKRetargeter;

/** Settings displayed by the HybridMotionSystem batch animation retarget tool. */
UCLASS(Transient)
class HYBRIDMOTIONSYSTEMEDITOR_API UHMS_BatchRetargetSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Content folder containing source animation assets, for example /Game/Animations/Source. */
	UPROPERTY(EditAnywhere, Category = "路径", meta = (DisplayName = "源动画文件夹", ContentDir))
	FDirectoryPath SourceFolder;

	/** Content folder where retargeted assets will be created. */
	UPROPERTY(EditAnywhere, Category = "路径", meta = (DisplayName = "目标文件夹", ContentDir))
	FDirectoryPath TargetFolder;

	/** IK Retargeter whose source and target preview meshes define the conversion. */
	UPROPERTY(EditAnywhere, Category = "重定向", meta = (DisplayName = "IK 重定向器"))
	TObjectPtr<UIKRetargeter> Retargeter = nullptr;

	UPROPERTY(EditAnywhere, Category = "扫描", meta = (DisplayName = "包含子文件夹"))
	bool bRecursive = true;

	UPROPERTY(EditAnywhere, Category = "输出", meta = (DisplayName = "保留子文件夹结构"))
	bool bPreserveSubfolders = true;

	UPROPERTY(EditAnywhere, Category = "输出", meta = (DisplayName = "覆盖同名资源"))
	bool bOverwriteExistingAssets = false;

	UPROPERTY(EditAnywhere, Category = "输出", meta = (DisplayName = "包含引用的动画资源"))
	bool bIncludeReferencedAssets = false;

	UPROPERTY(EditAnywhere, Category = "输出", meta = (DisplayName = "保留叠加动画标记"))
	bool bRetainAdditiveFlags = true;

	/** 将源动画的 Enable Root Motion、Root Lock 等设置复制到重定向结果。 */
	UPROPERTY(EditAnywhere, Category = "根运动", meta = (DisplayName = "保留源根运动设置"))
	bool bPreserveRootMotionSettings = true;

	/**
	 * 把源动画 root 骨相对首帧的运动轨迹重新映射到目标动画 root 骨。
	 * 适合坐椅、翻越等必须依赖根运动和 Motion Warping 的交互动画。
	 */
	UPROPERTY(EditAnywhere, Category = "根运动", meta = (DisplayName = "保留源 Root 轨迹"))
	bool bPreserveRootMotionTrajectory = true;

	/** 在输出日志中打印源动画与重定向动画的总根运动位移和旋转。 */
	UPROPERTY(EditAnywhere, Category = "根运动", meta = (DisplayName = "验证根运动结果"))
	bool bValidateRootMotion = true;

	UPROPERTY(EditAnywhere, Category = "命名", meta = (DisplayName = "名称前缀"))
	FString Prefix;

	UPROPERTY(EditAnywhere, Category = "命名", meta = (DisplayName = "名称后缀"))
	FString Suffix;

	UPROPERTY(EditAnywhere, Category = "命名", meta = (DisplayName = "查找文本"))
	FString Search;

	UPROPERTY(EditAnywhere, Category = "命名", meta = (DisplayName = "替换文本"))
	FString Replace;
};
