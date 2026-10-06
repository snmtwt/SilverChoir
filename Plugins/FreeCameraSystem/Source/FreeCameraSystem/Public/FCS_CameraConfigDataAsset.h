#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FCS_CameraConfigDataAsset.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "自由相机配置"))
class FREECAMERASYSTEM_API UFCS_CameraConfigDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|初始状态", meta = (DisplayName = "初始弹簧臂长度", ClampMin = "0.0"))
	float InitialTargetArmLength = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|初始状态", meta = (DisplayName = "初始俯仰角"))
	float InitialPitch = -60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|移动", meta = (DisplayName = "使用缩放插值移动速度"))
	bool bUseZoomInterpolatedMoveSpeed = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|移动", meta = (DisplayName = "固定移动速度", ClampMin = "0.0", EditCondition = "!bUseZoomInterpolatedMoveSpeed"))
	float MoveSpeed = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|移动", meta = (DisplayName = "最近距离移动速度", ClampMin = "0.0", EditCondition = "bUseZoomInterpolatedMoveSpeed"))
	float MinMoveSpeed = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|移动", meta = (DisplayName = "最远距离移动速度", ClampMin = "0.0", EditCondition = "bUseZoomInterpolatedMoveSpeed"))
	float MaxMoveSpeed = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|边缘移动", meta = (DisplayName = "启用边缘移动"))
	bool bEnableEdgeScroll = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|边缘移动", meta = (DisplayName = "边缘移动速度倍率", ClampMin = "0.0", EditCondition = "bEnableEdgeScroll"))
	float EdgeMoveSpeedScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|边缘移动", meta = (DisplayName = "边缘检测像素范围", ClampMin = "0.0", EditCondition = "bEnableEdgeScroll"))
	float EdgeScrollThreshold = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|旋转", meta = (DisplayName = "键盘旋转速度", ClampMin = "0.0"))
	float RotationSpeed = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|旋转", meta = (DisplayName = "鼠标水平旋转灵敏度", ClampMin = "0.0"))
	float MouseYawSpeed = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|旋转", meta = (DisplayName = "鼠标俯仰旋转灵敏度", ClampMin = "0.0"))
	float MousePitchSpeed = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|旋转", meta = (DisplayName = "最小俯仰角"))
	float MinPitch = -80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|旋转", meta = (DisplayName = "最大俯仰角"))
	float MaxPitch = -25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|旋转", meta = (DisplayName = "鼠标旋转时隐藏鼠标"))
	bool bHideCursorWhenRotate = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|缩放", meta = (DisplayName = "缩放步长", ClampMin = "0.0"))
	float ZoomStep = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|缩放", meta = (DisplayName = "启用缩放缓动"))
	bool bEnableZoomEase = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|缩放", meta = (DisplayName = "缩放缓动速度", ClampMin = "0.0", EditCondition = "bEnableZoomEase"))
	float ZoomEaseSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|缩放", meta = (DisplayName = "最小弹簧臂长度", ClampMin = "0.0"))
	float MinZoomLength = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|缩放", meta = (DisplayName = "最大弹簧臂长度", ClampMin = "0.0"))
	float MaxZoomLength = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|移动边界", meta = (DisplayName = "启用移动边界"))
	bool bUseCameraBounds = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|移动边界", meta = (DisplayName = "最小移动边界", EditCondition = "bUseCameraBounds"))
	FVector2D MinCameraBounds = FVector2D(-10000.0f, -10000.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|移动边界", meta = (DisplayName = "最大移动边界", EditCondition = "bUseCameraBounds"))
	FVector2D MaxCameraBounds = FVector2D(10000.0f, 10000.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|弹簧臂", meta = (DisplayName = "忽略摄像机臂碰撞"))
	bool bIgnoreCameraArmCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|弹簧臂", meta = (DisplayName = "启用摄像机移动延迟"))
	bool bEnableCameraLag = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|弹簧臂", meta = (DisplayName = "摄像机移动延迟速度", ClampMin = "0.0", EditCondition = "bEnableCameraLag"))
	float CameraLagSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|弹簧臂", meta = (DisplayName = "摄像机最大移动延迟距离", ClampMin = "0.0", EditCondition = "bEnableCameraLag"))
	float CameraLagMaxDistance = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|弹簧臂", meta = (DisplayName = "启用摄像机旋转延迟"))
	bool bEnableCameraRotationLag = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "相机配置|弹簧臂", meta = (DisplayName = "摄像机旋转延迟速度", ClampMin = "0.0", EditCondition = "bEnableCameraRotationLag"))
	float CameraRotationLagSpeed = 10.0f;
};
