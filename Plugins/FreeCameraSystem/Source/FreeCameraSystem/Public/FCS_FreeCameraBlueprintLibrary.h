#pragma once

#include "CoreMinimal.h"
#include "FCS_FreeCameraPawn.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FCS_FreeCameraBlueprintLibrary.generated.h"

class UFCS_FreeCameraSubsystem;

UCLASS(meta = (DisplayName = "自由相机函数库"))
class FREECAMERASYSTEM_API UFCS_FreeCameraBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** true 禁止所有手动平移、边缘移动和缩放；false 恢复。自动相机状态过渡不受影响。返回是否找到相机并设置成功。 */
	UFUNCTION(BlueprintCallable, Category="自由相机|控制开关", meta=(WorldContext="WorldContextObject", DefaultToSelf="WorldContextObject", DisplayName="禁用相机移动"))
	static bool SetFreeCameraMovementDisabled(const UObject* WorldContextObject, UPARAM(DisplayName="禁用") bool bDisabled = true);

	/** true 禁止键盘、鼠标和手动节点旋转；false 恢复。自动相机状态过渡不受影响。返回是否设置成功。 */
	UFUNCTION(BlueprintCallable, Category="自由相机|控制开关", meta=(WorldContext="WorldContextObject", DefaultToSelf="WorldContextObject", DisplayName="禁用相机旋转"))
	static bool SetFreeCameraRotationDisabled(const UObject* WorldContextObject, UPARAM(DisplayName="禁用") bool bDisabled = true);

	/** 没有注册相机时返回 false，可配合“查找自由相机”区分。 */
	UFUNCTION(BlueprintPure, Category="自由相机|控制开关", meta=(WorldContext="WorldContextObject", DefaultToSelf="WorldContextObject", DisplayName="相机移动是否禁用"))
	static bool IsFreeCameraMovementDisabled(const UObject* WorldContextObject);

	/** 没有注册相机时返回 false，可配合“查找自由相机”区分。 */
	UFUNCTION(BlueprintPure, Category="自由相机|控制开关", meta=(WorldContext="WorldContextObject", DefaultToSelf="WorldContextObject", DisplayName="相机旋转是否禁用"))
	static bool IsFreeCameraRotationDisabled(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "获取自由相机子系统"))
	static UFCS_FreeCameraSubsystem* GetFreeCameraSubsystem(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject
	);

	UFUNCTION(BlueprintPure, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "获取自由相机"))
	static AFCS_FreeCameraPawn* GetFreeCamera(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject
	);

	UFUNCTION(BlueprintPure, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "查找自由相机"))
	static bool FindFreeCamera(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "自由相机") AFCS_FreeCameraPawn*& OutCameraPawn
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "相机前后移动"))
	static bool MoveFreeCameraForward(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "输入值") float Value
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "相机左右移动"))
	static bool MoveFreeCameraRight(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "输入值") float Value
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "相机水平旋转"))
	static bool RotateFreeCamera(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "输入值") float Value
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "相机俯仰旋转"))
	static bool RotateFreeCameraPitch(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "输入值") float Value
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "相机缩放"))
	static bool ZoomFreeCamera(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "输入值") float Value
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "设置相机边缘滚动"))
	static bool SetFreeCameraEdgeScrollEnabled(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "启用") bool bEnabled
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "设置相机Pitch"))
	static bool SetFreeCameraPitch(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "Pitch") float NewPitch
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "设置相机Pitch限制"))
	static bool SetFreeCameraPitchLimits(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "最小Pitch") float MinPitch,
		UPARAM(DisplayName = "最大Pitch") float MaxPitch
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "触发相机鼠标旋转"))
	static bool TriggerFreeCameraMouseRotate(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "启用") bool bEnable
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "按鼠标增量旋转相机"))
	static bool RotateFreeCameraByMouseDelta(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "Delta X") float DeltaX,
		UPARAM(DisplayName = "Delta Y") float DeltaY
	);

	UFUNCTION(BlueprintPure, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "获取自由相机状态"))
	static bool GetFreeCameraState(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "相机状态") FFCS_CameraState& OutCameraState
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject", DisplayName = "移动自由相机到状态"))
	static bool MoveFreeCameraToState(
		UPARAM(DisplayName = "World Context Object") const UObject* WorldContextObject,
		UPARAM(DisplayName = "相机状态") const FFCS_CameraState& CameraState,
		UPARAM(DisplayName = "完成回调") FFCS_OnCameraMoveFinished MoveFinished
	);

	/** Instantly applies position, yaw, pitch and arm length, even while paused. False means no change was made. */
	UFUNCTION(BlueprintCallable, Category="自由相机", meta=(WorldContext="WorldContextObject", DefaultToSelf="WorldContextObject", DisplayName="立即设置自由相机状态"))
	static bool SetFreeCameraStateInstant(const UObject* WorldContextObject,
		UPARAM(DisplayName="相机状态") const FFCS_CameraState& CameraState,
		UPARAM(DisplayName="切换后恢复相机臂缓动") bool bRestoreSpringArmLag = false);
};
