#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GSMPiece3D.generated.h"

class AGSMTile3D;
class USceneComponent;
class UGSMPieceData;
class UGSMTileData;

/**
 * 网格策略地图棋子 Actor 基类。
 *
 * 由瓦片生成和维护的棋子都需要继承这个类。
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "网格策略地图棋子"))
class GRIDSTRATEGYMAPSYSTEM_API AGSMPiece3D : public AActor
{
	GENERATED_BODY()

public:
	AGSMPiece3D();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;

	UFUNCTION(BlueprintCallable, Category = "GSM|3D展示")
	void BindPieceData(UGSMPieceData* InPieceData);

	UFUNCTION(BlueprintPure, Category = "GSM|3D展示")
	UGSMPieceData* GetPieceData() const { return PieceData; }

	UFUNCTION(BlueprintImplementableEvent, Category = "GSM|3D展示", meta = (DisplayName = "当3D棋子绑定数据"))
	void ReceivePieceDataBound(UGSMPieceData* InPieceData);

	/** 已绑定的数据对象完成一次普通字段更新后调用。 */
	UFUNCTION(BlueprintNativeEvent, Category = "GSM|3D展示", meta = (DisplayName = "当3D棋子数据更新"))
	void OnBoundPieceDataUpdated(UGSMPieceData* UpdatedPieceData);

	/** 已绑定的数据对象成功切换所属瓦片，并且新瓦片上的 3D 棋子完成创建后调用。 */
	UFUNCTION(BlueprintNativeEvent, Category = "GSM|3D展示", meta = (DisplayName = "当3D棋子移动到新瓦片"))
	void OnBoundPieceMovedBetweenTiles(
		UGSMPieceData* MovedPieceData,
		UGSMTileData* PreviousTileData,
		UGSMTileData* CurrentTileData);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "设置棋子ID"))
	void SetPieceId(
		UPARAM(DisplayName = "棋子ID") FName NewPieceId
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "修改棋子ID"))
	bool ModifyPieceId(
		UPARAM(DisplayName = "棋子ID") FName NewPieceId
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋子", meta = (DisplayName = "获取棋子ID"))
	FName GetPieceId() const { return PieceId; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋子", meta = (DisplayName = "获取所属瓦片"))
	AGSMTile3D* GetOwningGridMapTile() const { return OwningTile.Get(); }

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "初始化棋子放置数据"))
	void InitializeGridMapPiece(
		UPARAM(DisplayName = "所属瓦片") AGSMTile3D* InOwningTile,
		UPARAM(DisplayName = "相对瓦片XY") FVector2D InRelativeTileXY,
		UPARAM(DisplayName = "相对瓦片Yaw") float InRelativeTileYaw,
		UPARAM(DisplayName = "默认缩放") float InDefaultScale
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "设置相对瓦片XY"))
	void SetRelativeTileXY(
		UPARAM(DisplayName = "相对瓦片XY") FVector2D NewRelativeTileXY
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋子", meta = (DisplayName = "获取相对瓦片XY"))
	FVector2D GetRelativeTileXY() const { return RelativeTileXY; }

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "设置相对瓦片Z"))
	void SetRelativeTileZ(
		UPARAM(DisplayName = "相对瓦片Z") float NewRelativeTileZ
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋子", meta = (DisplayName = "获取相对瓦片Z"))
	float GetRelativeTileZ() const { return RelativeTileZ; }

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "设置相对瓦片Yaw"))
	void SetRelativeTileYaw(
		UPARAM(DisplayName = "相对瓦片Yaw") float NewRelativeTileYaw
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋子", meta = (DisplayName = "获取相对瓦片Yaw"))
	float GetRelativeTileYaw() const { return RelativeTileYaw; }

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "设置默认缩放"))
	void SetDefaultPieceScale(
		UPARAM(DisplayName = "默认缩放") float NewDefaultScale
	);

	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋子", meta = (DisplayName = "获取默认缩放"))
	float GetDefaultPieceScale() const { return DefaultPieceScale; }

	/**
	 * 将棋子移动到所属瓦片朝向目标瓦片的一侧边缘，并通过瓦片放置流程刷新运行时数据和世界变换。
	 * 返回 false 表示所属瓦片或目标瓦片无效，或者棋子没有注册到所属瓦片。
	 */
	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子|放置", meta = (DisplayName = "移动到所属瓦片朝向目标瓦片的边缘"))
	bool MoveToOwningTileEdgeTowardTile(
		UPARAM(DisplayName = "目标瓦片ID") FName TargetTileId,
		UPARAM(DisplayName = "边缘内缩") float EdgeInset = 0.0f,
		UPARAM(DisplayName = "面向目标瓦片") bool bFaceTargetTile = true
	);

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "应用瓦片刷新变换"))
	void ApplyTilePieceTransform(
		UPARAM(DisplayName = "世界位置") const FVector& WorldLocation,
		UPARAM(DisplayName = "世界旋转") const FRotator& WorldRotation,
		UPARAM(DisplayName = "世界缩放") const FVector& WorldScale,
		UPARAM(DisplayName = "地图缩放") float RuntimeMapScale
	);

	void SetHiddenByGridMapBounds(bool bNewHiddenByGridMapBounds);

	bool IsHiddenByGridMapBounds() const { return bHiddenByGridMapBounds; }

	UFUNCTION(BlueprintCallable, Category = "网格策略地图|棋子", meta = (DisplayName = "禁用棋子接收贴花"))
	void DisableDecalReceivingOnComponents();

	UFUNCTION(BlueprintNativeEvent, Category = "网格策略地图|棋子", meta = (DisplayName = "当棋子变换由瓦片刷新"))
	void OnTilePieceTransformUpdated(
		UPARAM(DisplayName = "世界位置") const FVector& WorldLocation,
		UPARAM(DisplayName = "世界旋转") const FRotator& WorldRotation,
		UPARAM(DisplayName = "世界缩放") const FVector& WorldScale,
		UPARAM(DisplayName = "地图缩放") float RuntimeMapScale
	);

protected:
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "GSM|3D展示")
	TObjectPtr<UGSMPieceData> PieceData;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "根组件"))
	TObjectPtr<USceneComponent> RootSceneComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "网格策略地图|棋子", meta = (ExposeOnSpawn = "true", DisplayName = "棋子ID"))
	FName PieceId = NAME_None;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|棋子", meta = (DisplayName = "所属瓦片"))
	TWeakObjectPtr<AGSMTile3D> OwningTile;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|棋子", meta = (DisplayName = "相对瓦片XY"))
	FVector2D RelativeTileXY = FVector2D::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|棋子", meta = (DisplayName = "相对瓦片Z"))
	float RelativeTileZ = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|棋子", meta = (DisplayName = "相对瓦片Yaw"))
	float RelativeTileYaw = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "网格策略地图|棋子", meta = (DisplayName = "默认缩放"))
	float DefaultPieceScale = 1.0f;

	UPROPERTY(Transient)
	bool bHiddenByGridMapBounds = false;

	UPROPERTY(Transient)
	bool bActorCollisionEnabledBeforeGridMapBoundsHidden = true;
};
