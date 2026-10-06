#pragma once

#include "CoreMinimal.h"
#include "GridStrategyMapSystem/Data/GSMTypes.h"
#include "ProceduralMeshComponent.h"
#include "GSMBoardMeshComponent.generated.h"

class UMaterialInterface;
class UGSMBoardMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FGSMBoardMeshRebuilt,
	UGSMBoardMeshComponent*, BoardMeshComponent
);

/**
 * 网格策略地图的棋盘网格组件。
 *
 * 这个组件只负责生成“带四边框和中间凹槽”的棋盘托盘网格，不保存瓦片配置，也不直接生成瓦片 Actor。
 * 地图 Actor 可以读取它缓存的凹槽范围，将瓦片放入凹槽区域，并用凹槽边界控制范围外瓦片的隐藏。
 */
UCLASS(ClassGroup = (GridStrategyMap), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent, DisplayName = "网格策略地图棋盘网格组件"))
class GRIDSTRATEGYMAPSYSTEM_API UGSMBoardMeshComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UGSMBoardMeshComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void OnRegister() override;

	/** 重新根据当前参数生成棋盘网格。编辑器中修改参数后也会自动调用。 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "重新生成棋盘网格"))
	void RebuildBoardMesh();

	/** 清空棋盘网格，但保留当前参数。 */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "清空棋盘网格"))
	void ClearBoardMesh();

	/** 获取中间凹槽的平面尺寸。瓦片区域建议使用这个尺寸作为可摆放范围。 */
	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "获取凹槽尺寸"))
	FVector2D GetGrooveSize() const { return GeneratedGrooveSize; }

	/** 获取中间凹槽的深度。瓦片需要落在槽底时，可以用它计算 Z 位置。 */
	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "获取凹槽深度"))
	float GetGeneratedGrooveDepth() const { return GeneratedGrooveDepth; }

	/** 获取地图本地坐标下的凹槽四边形范围。 */
	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "获取本地凹槽范围"))
	FGSMQuadBounds GetLocalGrooveBounds() const { return GeneratedLocalGrooveBounds; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "获取本地棋盘范围"))
	FGSMQuadBounds GetLocalBoardBounds() const { return GeneratedLocalBoardBounds; }

	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "获取本地棋盘顶面高度"))
	float GetLocalBoardTopZ() const { return 0.0f; }

	/** 判断地图本地坐标是否位于凹槽平面范围内。 */
	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "本地位置是否在凹槽内"))
	bool IsLocalLocationInsideGroove(
		UPARAM(DisplayName = "本地位置") const FVector& LocalLocation
	) const;

	/** 获取凹槽中心点在槽底上的本地位置。 */
	UFUNCTION(BlueprintPure, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "获取凹槽槽底中心"))
	FVector GetLocalGrooveFloorCenter() const;

	UPROPERTY(BlueprintAssignable, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "当棋盘网格重新生成"))
	FGSMBoardMeshRebuilt OnBoardMeshRebuilt;

protected:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void UpdateCachedGrooveMetrics();
	void NotifyOwnerBoardMeshChanged();

	static void AddQuad(
		TArray<FVector>& Vertices,
		TArray<int32>& Triangles,
		TArray<FVector>& Normals,
		TArray<FVector2D>& UV0,
		TArray<FLinearColor>& VertexColors,
		const FVector& A,
		const FVector& B,
		const FVector& C,
		const FVector& D,
		const FVector& DesiredNormal
	);

protected:
	/** 凹槽内部宽度，也就是棋盘中可摆放瓦片区域的 X 尺寸。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|凹槽", meta = (DisplayName = "内部宽度", ClampMin = "1.0", UIMin = "1.0"))
	float InteriorWidth = 1000.0f;

	/** 凹槽内部高度，也就是棋盘中可摆放瓦片区域的 Y 尺寸。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|凹槽", meta = (DisplayName = "内部高度", ClampMin = "1.0", UIMin = "1.0"))
	float InteriorHeight = 800.0f;

	/** 左边框向外占用的宽度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|边框", meta = (DisplayName = "左边宽度", ClampMin = "0.0", UIMin = "0.0"))
	float LeftBorderWidth = 80.0f;

	/** 右边框向外占用的宽度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|边框", meta = (DisplayName = "右边宽度", ClampMin = "0.0", UIMin = "0.0"))
	float RightBorderWidth = 80.0f;

	/** 上边框向外占用的宽度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|边框", meta = (DisplayName = "上边宽度", ClampMin = "0.0", UIMin = "0.0"))
	float TopBorderWidth = 80.0f;

	/** 下边框向外占用的宽度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|边框", meta = (DisplayName = "下边宽度", ClampMin = "0.0", UIMin = "0.0"))
	float BottomBorderWidth = 80.0f;

	/** 棋盘整体厚度。外侧壁会从顶面延伸到这个厚度。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|厚度", meta = (DisplayName = "棋盘整体厚度", ClampMin = "0.1", UIMin = "0.1"))
	float BoardThickness = 36.0f;

	/** 凹槽低于边框顶面的深度。最终深度会被限制在棋盘整体厚度以内。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|厚度", meta = (DisplayName = "凹槽深度", ClampMin = "0.0", UIMin = "0.0"))
	float GrooveDepth = 18.0f;

	/** 棋盘顶部边框材质。材质槽固定为 0。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|表现", meta = (DisplayName = "棋盘材质"))
	TObjectPtr<UMaterialInterface> BoardMaterial;

	/** 棋盘侧壁和底面材质。未设置时使用棋盘材质，材质槽固定为 1。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|表现", meta = (DisplayName = "棋盘侧边材质"))
	TObjectPtr<UMaterialInterface> BoardSideMaterial;

	/** 凹槽槽底平面材质。未设置时使用棋盘材质，材质槽固定为 2。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|表现", meta = (DisplayName = "凹槽平面材质"))
	TObjectPtr<UMaterialInterface> GrooveFloorMaterial;

	/** 是否为棋盘生成碰撞。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|碰撞", meta = (DisplayName = "生成碰撞"))
	bool bGenerateCollision = true;

	/** 是否使用复杂碰撞作为简单碰撞。棋盘是编辑器交互物体时通常建议开启。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|碰撞", meta = (DisplayName = "使用复杂碰撞"))
	bool bBoardUseComplexAsSimpleCollision = true;

	/** 编辑器中修改参数时是否立即重新生成棋盘网格。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "棋盘网格|编辑器", meta = (DisplayName = "编辑器中自动重建"))
	bool bAutoRebuildInEditor = true;

	/** 最近一次生成后的凹槽尺寸。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "棋盘网格|生成结果", meta = (DisplayName = "已生成凹槽尺寸"))
	FVector2D GeneratedGrooveSize = FVector2D(1000.0, 800.0);

	/** 最近一次生成后的棋盘整体尺寸。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "棋盘网格|生成结果", meta = (DisplayName = "已生成棋盘尺寸"))
	FVector2D GeneratedBoardSize = FVector2D(1160.0, 960.0);

	/** 最近一次生成后的有效凹槽深度。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "棋盘网格|生成结果", meta = (DisplayName = "已生成凹槽深度"))
	float GeneratedGrooveDepth = 18.0f;

	/** 最近一次生成后的地图本地凹槽范围。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "棋盘网格|生成结果", meta = (DisplayName = "已生成本地凹槽范围"))
	FGSMQuadBounds GeneratedLocalGrooveBounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "网格策略地图|棋盘网格", meta = (DisplayName = "已生成本地棋盘范围"))
	FGSMQuadBounds GeneratedLocalBoardBounds;
};
