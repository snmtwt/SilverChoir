#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FCS_FreeCameraPawn.generated.h"

class USceneComponent;
class USpringArmComponent;
class UCameraComponent;
class UFCS_CameraConfigDataAsset;


DECLARE_DYNAMIC_DELEGATE(FFCS_OnCameraMoveFinished);

USTRUCT(BlueprintType)
struct FREECAMERASYSTEM_API FFCS_CameraState
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Yaw = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Pitch = -60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float TargetArmLength = 2000.0f;

	/** 平移速度（单位：cm/s） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float MoveSpeed = 3000.0f;

	/** 旋转速度（度/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float RotationSpeed = 180.0f;

	/** 缩放速度（cm/s） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float ZoomSpeed = 2000.0f;
};

UCLASS(Blueprintable, meta=(DisplayName="自由相机Pawn"))
class FREECAMERASYSTEM_API AFCS_FreeCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AFCS_FreeCameraPawn();
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void UnPossessed() override;

	/** 关闭后可通过蓝图/Enhanced Input 调用控制函数，避免重复输入。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="自由相机|输入", meta=(DisplayName="启用默认键鼠绑定"))
	bool bEnableDefaultInputBindings = true;
	/** Disable when mouse rotation is driven by external Blueprint/Enhanced Input events. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|Input")
	bool bEnableDefaultMouseRotationBindings = true;

	UFUNCTION(BlueprintPure, Category="Camera|State", meta=(DisplayName="是否正在移动到相机状态"))
	bool IsMovingToCameraState() const { return bIsBlendingToCameraState; }

	UFUNCTION(BlueprintCallable, Category="Camera|State", meta=(DisplayName="取消相机状态移动"))
	void CancelCameraStateMove();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

protected:
	/** Root */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "根组件"))
	USceneComponent* SceneRoot;

	/** Spring arm */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "摄像机臂"))
	USpringArmComponent* SpringArm;

	/** Camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (DisplayName = "摄像机"))
	UCameraComponent* Camera;

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "相机", meta = (DisplayName = "相机配置文件"))
	TObjectPtr<UFCS_CameraConfigDataAsset> CameraConfig = nullptr;

	UFUNCTION(BlueprintCallable, Category = "Camera|Config", meta = (DisplayName = "应用相机配置"))
	void ApplyCameraConfig();

	UFUNCTION(BlueprintPure, Category = "Camera|Config", meta = (DisplayName = "获取相机配置文件"))
	UFCS_CameraConfigDataAsset* GetCameraConfig() const { return CameraConfig.Get(); }

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "相机|工具", meta = (DisplayName = "复制当前相机状态到剪贴板"))
	void CopyCurrentCameraStateToClipboard();



	UFUNCTION(BlueprintCallable, Category = "相机|工具", meta = (DisplayName = "尝试复制当前相机状态到剪贴板"))
	bool TryCopyCurrentCameraStateToClipboard(
		UPARAM(DisplayName = "相机状态文本") FString& OutCameraStateText
	) const;



	UPROPERTY(Transient, BlueprintReadOnly, Category = "Camera|Runtime")
	bool bMouseRotateMode = false;

protected:
	bool bUseZoomInterpolatedMoveSpeed = true;
	float MoveSpeed = 3000.0f;
	float MinMoveSpeed = 1000.0f;
	float MaxMoveSpeed = 5000.0f;
	float EdgeMoveSpeedScale = 1.0f;
	float RotationSpeed = 100.0f;
	float ZoomStep = 200.0f;
	bool bEnableZoomEase = true;
	float ZoomEaseSpeed = 10.0f;
	float MinZoomLength = 0.0f;
	float MaxZoomLength = 5000.0f;
	float TargetZoomLength = 2000.0f;
	bool bEnableEdgeScroll = false;
	float EdgeScrollThreshold = 20.0f;
	bool bUseCameraBounds = false;
	FVector2D MinCameraBounds = FVector2D(-10000.f, -10000.f);
	FVector2D MaxCameraBounds = FVector2D(10000.f, 10000.f);
	float MouseYawSpeed = 0.2f;
	float MousePitchSpeed = 0.2f;
	float MinPitch = -80.0f;
	float MaxPitch = -25.0f;
	bool bHideCursorWhenRotate = false;

	bool bCachedShowMouseCursor = true;
	bool bMouseRotateInitialized = false;
	TSet<FKey> HeldKeys;
	TWeakObjectPtr<APlayerController> RotationController;
	FVector2D LastMouseScreenPosition = FVector2D::ZeroVector;

public:
	/** Blocks manual translation, edge scrolling and zoom; scripted camera-state transitions still run. */
	UFUNCTION(BlueprintCallable, Category="Camera|Control", meta=(DisplayName="禁用相机移动"))
	void SetCameraMovementDisabled(UPARAM(DisplayName="禁用") bool bDisabled = true);

	/** Blocks all manual rotation and ends the current mouse drag; scripted transitions still run. */
	UFUNCTION(BlueprintCallable, Category="Camera|Control", meta=(DisplayName="禁用相机旋转"))
	void SetCameraRotationDisabled(UPARAM(DisplayName="禁用") bool bDisabled = true);

	UFUNCTION(BlueprintPure, Category="Camera|Control", meta=(DisplayName="相机移动是否禁用"))
	bool IsCameraMovementDisabled() const { return bCameraMovementDisabled; }

	UFUNCTION(BlueprintPure, Category="Camera|Control", meta=(DisplayName="相机旋转是否禁用"))
	bool IsCameraRotationDisabled() const { return bCameraRotationDisabled; }

	/** 按当前朝向前后移动，通常绑定 W/S */
	UFUNCTION(BlueprintCallable, Category = "Camera|Control")
	void MoveForward(float Value);

	/** 按当前朝向左右移动，通常绑定 A/D */
	UFUNCTION(BlueprintCallable, Category = "Camera|Control")
	void MoveRight(float Value);

	/** 左右旋转，通常绑定 Q/E */
	UFUNCTION(BlueprintCallable, Category = "Camera|Control")
	void RotateCamera(float Value);

	/** 上下旋转 */
	UFUNCTION(BlueprintCallable, Category = "Camera|Control")
	void RotatePitch(float Value);

	/** 缩放，通常绑定鼠标滚轮 */
	UFUNCTION(BlueprintCallable, Category = "Camera|Control")
	void ZoomCamera(float Value);

	/** 启用/关闭边缘滚动 */
	UFUNCTION(BlueprintCallable, Category = "Camera|Control")
	void SetEdgeScrollEnabled(bool bEnabled);

	/** 设置 Pitch */
	UFUNCTION(BlueprintCallable, Category = "Camera|Control")
	void SetCameraPitch(float NewPitch);

	/** 设置 Pitch 限制 */
	UFUNCTION(BlueprintCallable, Category = "Camera|Control")
	void SetPitchLimits(float InMinPitch, float InMaxPitch);

	/** 获取当前 Pitch 限制，供临时调整后恢复。 */
	UFUNCTION(BlueprintPure, Category = "Camera|Control", meta = (DisplayName = "获取俯仰角限制"))
	void GetPitchLimits(float& OutMinPitch, float& OutMaxPitch) const
	{
		OutMinPitch = MinPitch;
		OutMaxPitch = MaxPitch;
	}

	/** 开启/关闭鼠标旋转模式 */
	UFUNCTION(BlueprintCallable, Category = "Camera|Control")
	void TriggerMouseRotate(bool bEnable);

	/** 手动传入鼠标增量来旋转相机 */
	UFUNCTION(BlueprintCallable, Category = "Camera|Control")
	void RotateCameraByMouseDelta(float DeltaX, float DeltaY);

protected:
	float GetCurrentMoveSpeed() const;
	void AddCameraMovement(const FVector& WorldDirection, float ScaleValue, float Speed);
	void HandleEdgeScroll(float DeltaTime);
	void HandleMouseRotate();
	void UpdateZoomEase(float DeltaTime);
	void ClampCameraLocation();
	void ClampSpringArmPitch();

	APlayerController* GetOwningPlayerController() const;

	bool GetMousePositionInViewport(APlayerController* PC, float& MouseX, float& MouseY, FVector2D& ViewportSize) const;

protected:
	bool bIsBlendingToCameraState = false;
	FFCS_CameraState CameraStateBlendStart;
	FFCS_CameraState CameraStateBlendTarget;
	float CameraStateBlendElapsed = 0.0f;

public:
	/** 获取当前相机状态 */
	UFUNCTION(BlueprintPure, Category = "Camera|State")
	FFCS_CameraState GetCurrentCameraState() const;

	FFCS_OnCameraMoveFinished PendingCallback;

	/** 根据指定相机状态移动到目标位置 */
	UFUNCTION(BlueprintCallable, Category = "Camera|State")
	void MoveToCameraState(const FFCS_CameraState& InCameraState, FFCS_OnCameraMoveFinished MoveFinished);

	/** Synchronous camera cut, including the spring-arm socket. Cancels the old move/callback and zoom remainder.
	 * Respects configured bounds/pitch/arm limits; ignores manual control locks and state movement speeds.
	 * Restore preserves the arm's previous lag flags after flushing history; false leaves both flags disabled. */
	UFUNCTION(BlueprintCallable, Category="Camera|State", meta=(DisplayName="立即设置相机状态"))
	bool SetCameraStateInstant(const FFCS_CameraState& InCameraState,
		UPARAM(DisplayName="切换后恢复相机臂缓动") bool bRestoreSpringArmLag = false);

protected:
	void UpdateCameraStateBlend(float DeltaTime);
	void ApplyCameraState(const FFCS_CameraState& InCameraState);

private:
	// Runtime gates are intentionally independent of default input bindings and camera configuration.
	UPROPERTY(Transient)
	bool bCameraMovementDisabled = false;
	UPROPERTY(Transient)
	bool bCameraRotationDisabled = false;
};
