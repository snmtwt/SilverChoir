#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FCS_FreeCameraSubsystem.generated.h"

class AFCS_FreeCameraPawn;

UCLASS(meta = (DisplayName = "自由相机子系统"))
class FREECAMERASYSTEM_API UFCS_FreeCameraSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (DisplayName = "注册自由相机"))
	bool RegisterFreeCamera(
		UPARAM(DisplayName = "自由相机") AFCS_FreeCameraPawn* CameraPawn
	);

	UFUNCTION(BlueprintCallable, Category = "自由相机", meta = (DisplayName = "移除自由相机"))
	bool UnregisterFreeCamera(
		UPARAM(DisplayName = "自由相机") AFCS_FreeCameraPawn* CameraPawn
	);

	UFUNCTION(BlueprintPure, Category = "自由相机", meta = (DisplayName = "获取自由相机"))
	AFCS_FreeCameraPawn* GetFreeCamera() const;

	UFUNCTION(BlueprintPure, Category = "自由相机", meta = (DisplayName = "查找自由相机"))
	bool FindFreeCamera(
		UPARAM(DisplayName = "自由相机") AFCS_FreeCameraPawn*& OutCameraPawn
	) const;

	UFUNCTION(BlueprintPure, Category = "自由相机", meta = (DisplayName = "是否存在自由相机"))
	bool HasFreeCamera() const;

private:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}
	virtual void Deinitialize() override
	{
		FreeCamera.Reset();
		Super::Deinitialize();
	}
	UPROPERTY(Transient)
	TWeakObjectPtr<AFCS_FreeCameraPawn> FreeCamera;
};
