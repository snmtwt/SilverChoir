// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/EHBElementActorBase.h"
#include "EHB_RailingGate.generated.h"

class AEHB_Railing;
class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 围栏门 Actor。
 *
 * 设计分工：
 * - 门 Actor 负责门扇表现、开合角度、铰链侧等“门自身状态”。
 * - 扶手 Actor 负责保存门洞连接并在重建时裁剪柱、横杆和围栏板。
 * - 因此删除门 Actor 时，需要通知所属扶手移除 GateConnection。
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "EHB_RailingGate"))
class EASYHOUSEBUILDER_API AEHB_RailingGate : public AEHBElementActorBase
{
	GENERATED_BODY()

public:
	AEHB_RailingGate();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RailingGate|Components")
	TObjectPtr<USceneComponent> GatePivotComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RailingGate|Components")
	TObjectPtr<UStaticMeshComponent> GateLeafComponent;

	/** 所属扶手装配 Guid。加载后通过建筑对象的元素索引重新找到扶手 Actor。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RailingGate|Binding")
	FGuid OwningRailingGuid;

	/** 门洞中心沿扶手路径起点的距离。扶手路径变化时，门可以通过这个一维参数重定位。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RailingGate|Binding")
	float DistanceFromRailingStart = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RailingGate|Dimensions", meta = (ClampMin = "1.0", Units = "cm"))
	float Width = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RailingGate|Dimensions", meta = (ClampMin = "1.0", Units = "cm"))
	float Height = 95.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RailingGate|Dimensions", meta = (ClampMin = "0.1", Units = "cm"))
	float Thickness = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RailingGate|Motion")
	EEHBRailingGateHingeSide HingeSide = EEHBRailingGateHingeSide::Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RailingGate|Motion", meta = (Units = "deg"))
	float CurrentOpenAngle = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RailingGate|Mesh")
	TSoftObjectPtr<UStaticMesh> GateMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RailingGate|Material")
	TSoftObjectPtr<UMaterialInterface> GateMaterial;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Destroyed() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "RailingGate|Configure")
	bool ConfigureOnRailing(
		AEHB_Railing* Railing,
		float InDistanceFromStart,
		float InWidth,
		float InHeight,
		EEHBRailingGateHingeSide InHingeSide);

	/** 只重建门扇表现，不直接裁剪扶手；裁剪由扶手的 GateConnection 完成。 */
	UFUNCTION(BlueprintCallable, Category = "RailingGate|Build")
	bool RebuildGateMesh();

	UFUNCTION(BlueprintCallable, Category = "RailingGate|Binding")
	AEHB_Railing* FindOwningRailing() const;
};
