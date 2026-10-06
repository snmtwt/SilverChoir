// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Actors/EHBElementActorBase.h"
#include "Engine/DataTable.h"
#include "EHB_Railing.generated.h"

class AEHB_RailingGate;
class AEHB_Stair;
class UActorComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UEHBGeneratedMeshComponent;
class USplineMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 扶手起止点的吸附记录。
 *
 * 设计意图：
 * - 扶手本身不作为承重结构，只记录自己“挂接/贴合”到了哪个边界元素或哪个扶手柱。
 * - ElementGuid 指向墙、柱、已有扶手等宿主元素；PostGuid 用于更细粒度地绑定到扶手内部生成柱。
 * - LocalPoint 是扶手 Actor 局部空间下的兜底定位点，后续宿主失效时仍可恢复大致位置。
 */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBRailingAnchor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Anchor")
	FGuid ElementGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Anchor")
	FGuid PostGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Anchor")
	FVector LocalPoint = FVector::ZeroVector;
};

/**
 * 扶手装配内部生成的一根柱。
 *
 * 注意这里不是独立 Actor，而是一条可查询的数据记录加 HISM 实例：
 * - 避免每隔一段扶手就生成一个 Actor，场景规模会更可控。
 * - PostGuid 保持稳定，后续门、端点吸附、实例点击反查都可以用它。
 * - Distance 是沿扶手路径的一维参数，线性扶手和楼梯扶手共用同一套生成逻辑。
 */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBRailingPost
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts")
	FGuid PostGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts", meta = (Units = "cm"))
	float Distance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts")
	int32 StepIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts")
	FVector LocalBaseLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts")
	FRotator LocalRotation = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts")
	bool bExplicit = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts")
	bool bGatePost = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts")
	bool bSuppressInstance = false;
};

USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBRailingPostMeshOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Post Override")
	FGuid PostGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Post Override", meta = (DisplayName = "扶手采样项"))
	FDataTableRowHandle SampledRailingRow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Post Override")
	TSoftObjectPtr<UStaticMesh> PostMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Post Override")
	TSoftObjectPtr<UMaterialInterface> PostMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Post Override", meta = (ClampMin = "0.1", Units = "cm"))
	float PostWidth = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Post Override", meta = (ClampMin = "1.0", Units = "cm"))
	float PostHeight = 100.0f;
};

/**
 * 围栏门和扶手装配之间的连接记录。
 *
 * 这里把“门洞”存放在扶手上，而门 Actor 只负责门扇表现：
 * - 扶手重建时根据 DistanceFromStart/Width 自动挖掉门洞范围内的普通柱、横杆和围栏板。
 * - LeftPostGuid/RightPostGuid 是门洞两侧强制生成的边柱，保证门洞边界稳定。
 * - GateLocalToRailing 用于关系图和保存恢复，不让门只依赖当前世界坐标。
 */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBRailingGateConnection
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Gate")
	FGuid GateGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Gate", meta = (Units = "cm"))
	float DistanceFromStart = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Gate", meta = (ClampMin = "1.0", Units = "cm"))
	float Width = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Gate", meta = (ClampMin = "1.0", Units = "cm"))
	float Height = 95.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Gate")
	EEHBRailingGateHingeSide HingeSide = EEHBRailingGateHingeSide::Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Gate")
	FGuid LeftPostGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Gate")
	FGuid RightPostGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Gate")
	FTransform GateLocalToRailing = FTransform::Identity;
};

/**
 * 扶手路径上的统一采样结果。
 *
 * 所有生成逻辑都先把“任意路径”抽象成 Distance -> 位置/切线/右方向：
 * - 线性扶手直接在线段上采样。
 * - 楼梯扶手委托楼梯的路径采样，因此楼梯被控制点扭曲后，扶手也能自然跟随。
 */
USTRUCT(BlueprintType)
struct EASYHOUSEBUILDER_API FEHBRailingPathSample
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Path", meta = (Units = "cm"))
	float Distance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Path")
	int32 StepIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Path")
	FVector LocalLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Path")
	FVector LocalForward = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Path")
	FVector LocalRight = FVector::RightVector;
};

/**
 * 扶手装配 Actor。
 *
 * 总体设计：
 * - 一个 Actor 管理整段扶手，内部用 HISM 承载柱、SplineMesh 承载横杆、EHB generated mesh 承载实体围栏板。
 * - 扶手是非承重边界附件，不参与 StructuralSupport，也不作为房间边界。
 * - 所有可变几何都由路径采样驱动，后续可以继续扩展弧形路径、样条路径、平台边界路径。
 * - 围栏门是独立 Actor，但门洞数据保存在扶手装配上，方便重建时统一裁剪柱、横杆和板。
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "EHB_Railing"))
class EASYHOUSEBUILDER_API AEHB_Railing : public AEHBElementActorBase
{
	GENERATED_BODY()

public:
	AEHB_Railing();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> PostMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> PanelMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> PanelSampleMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Railing|Components")
	TArray<TObjectPtr<USplineMeshComponent>> RailMeshComponents;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Components")
	TObjectPtr<UEHBGeneratedMeshComponent> RailGeneratedMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Railing|Components")
	TArray<TObjectPtr<UStaticMeshComponent>> PostOverrideMeshComponents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Path")
	EEHBRailingPathMode PathMode = EEHBRailingPathMode::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Path")
	EEHBRailingSide RailingSide = EEHBRailingSide::Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Path")
	TObjectPtr<AEHB_Stair> HostedStair;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Path")
	FGuid HostedStairGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Path")
	FVector LinearStart = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Path")
	FVector LinearEnd = FVector(300.0f, 0.0f, 0.0f);

	// Optional source-wall face cut in railing-local space. The logical anchor
	// remains at the column center; only generated horizontal rail geometry trims.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Junction")
	bool bHasStartWallCut = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Junction")
	FVector StartWallCutPoint = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Junction")
	FVector StartWallCutNormal = FVector::ForwardVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts")
	EEHBRailingPostSpacingMode PostSpacingMode = EEHBRailingPostSpacingMode::Distance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts", meta = (ClampMin = "1.0", Units = "cm"))
	float PostSpacing = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts", meta = (ClampMin = "1"))
	int32 StepsPerPost = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts", meta = (ClampMin = "0.1", Units = "cm"))
	float PostWidth = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts", meta = (ClampMin = "1.0", Units = "cm"))
	float PostHeight = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts")
	bool bOmitStartPost = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts")
	bool bOmitEndPost = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Rails", meta = (ClampMin = "1.0", Units = "cm"))
	float RailHeight = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Rails", meta = (ClampMin = "0.1", Units = "cm"))
	float RailThickness = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Rails", meta = (ClampMin = "1.0", Units = "cm"))
	float MaxRailSegmentLength = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Panel")
	EEHBRailingFillMode FillMode = EEHBRailingFillMode::PostsAndRails;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Panel", meta = (ClampMin = "0.0", Units = "cm"))
	float PanelBottomOffset = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Panel", meta = (ClampMin = "1.0", Units = "cm"))
	float PanelTopOffset = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Stair", meta = (ClampMin = "0.0", Units = "cm"))
	float StairSideOffset = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Stair", meta = (Units = "cm"))
	float StairPostBaseHeightOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Mesh")
	TSoftObjectPtr<UStaticMesh> PostMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Mesh")
	TSoftObjectPtr<UStaticMesh> RailMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Mesh", meta = (DisplayName = "扶手采样项"))
	FDataTableRowHandle SampledRailingRow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Material")
	TSoftObjectPtr<UMaterialInterface> PostMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Material")
	TSoftObjectPtr<UMaterialInterface> RailMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Material")
	TSoftObjectPtr<UMaterialInterface> PanelMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts")
	TArray<FEHBRailingPost> GeneratedPosts;

	/** Persistent linear endpoint identity, independent of regenerated post spacing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts")
	FGuid LinearStartPostGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Posts")
	FGuid LinearEndPostGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Railing|Posts")
	TArray<FGuid> PostInstanceGuids;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Railing|Posts")
	TArray<FGuid> PostOverrideComponentGuids;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Posts")
	TArray<FEHBRailingPostMeshOverride> PostMeshOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Anchor")
	FEHBRailingAnchor StartAnchor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Anchor")
	FEHBRailingAnchor EndAnchor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Railing|Gate")
	TArray<FEHBRailingGateConnection> GateConnections;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Railing|Relations")
	TArray<FGuid> RailingRelationGuids;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Destroyed() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintCallable, Category = "Railing|Configure")
	bool ConfigureLinear(
		AEHBBuildingActorBase* InBuilding,
		const FTransform& LocalTransform,
		const FVector& Start,
		const FVector& End,
		int32 InFloorIndex);

	/** 将扶手绑定到楼梯侧边。扶手路径由楼梯提供，因此会跟随楼梯的弯曲/控制点变化。 */
	UFUNCTION(BlueprintCallable, Category = "Railing|Configure")
	bool ConfigureOnStair(
		AEHBBuildingActorBase* InBuilding,
		AEHB_Stair* Stair,
		EEHBRailingSide Side,
		int32 InFloorIndex);

	/** 统一重建入口：规范参数、生成柱数据、刷新柱/横杆/围栏板组件、刷新关系图。 */
	UFUNCTION(BlueprintCallable, Category = "Railing|Build")
	bool RebuildRailing();

	UFUNCTION(BlueprintCallable, Category = "Railing|Build")
	int32 RebuildConnectedRailings(bool bModifyConnectedRailings = true);

	UFUNCTION(BlueprintCallable, Category = "Railing|Rails")
	float GetRailTopHeight() const;

	UFUNCTION(BlueprintCallable, Category = "Railing|Rails")
	void SetRailTopHeight(float InTopHeight);

	UFUNCTION(BlueprintCallable, Category = "Railing|Configure")
	bool ConfigureFromSampledRailingRow(UDataTable* InTable, FName InRowName, bool bFinished = true);

	UFUNCTION(BlueprintCallable, Category = "Railing|Path")
	float GetRailingLength() const;

	/** 在扶手路径上按距离采样。后续生成柱、横杆、围栏板和围栏门都依赖这个函数。 */
	UFUNCTION(BlueprintCallable, Category = "Railing|Path")
	bool EvaluatePathAtDistance(float Distance, FEHBRailingPathSample& OutSample) const;

	UFUNCTION(BlueprintCallable, Category = "Railing|Posts")
	bool FindPostByGuid(FGuid PostGuid, FEHBRailingPost& OutPost) const;

	UFUNCTION(BlueprintCallable, Category = "Railing|Posts")
	bool GetPostGuidForInstanceIndex(int32 InstanceIndex, FGuid& OutPostGuid) const;

	UFUNCTION(BlueprintCallable, Category = "Railing|Posts")
	bool GetPostGuidForOverrideComponent(const UActorComponent* Component, FGuid& OutPostGuid) const;

	UFUNCTION(BlueprintCallable, Category = "Railing|Configure")
	bool ApplyPostMeshSampleToPost(FGuid PostGuid, UDataTable* InTable, FName InRowName, bool bFinished = true);

	UFUNCTION(BlueprintCallable, Category = "Railing|Configure")
	bool ApplyPostMeshSampleToAllPosts(UDataTable* InTable, FName InRowName, bool bFinished = true);

	/** 添加或更新一个围栏门洞，并触发扶手重建。门洞只裁剪扶手表现，不创建承重关系。 */
	UFUNCTION(BlueprintCallable, Category = "Railing|Gate")
	bool AddOrUpdateGateConnection(
		AEHB_RailingGate* Gate,
		float DistanceFromStart,
		float Width,
		float Height,
		EEHBRailingGateHingeSide HingeSide);

	UFUNCTION(BlueprintCallable, Category = "Railing|Gate")
	bool RemoveGateConnection(FGuid GateGuid);

	UFUNCTION(BlueprintCallable, Category = "Railing|Relations")
	void RefreshRailingRelations();

	UFUNCTION(BlueprintCallable, Category = "Railing|Relations")
	void ClearRailingRelations();

private:
	/** 生成稳定的柱数据。这里先处理路径采样，再叠加围栏门的边柱和开口裁剪。 */
	bool EvaluateRailPathAtDistance(float Distance, FEHBRailingPathSample& OutSample) const;
	bool BuildPostData(TArray<FEHBRailingPost>& OutPosts);
	void RebuildPostInstances();
	void RebuildPostOverrideMeshes();
	void RebuildRailMeshes();
	void RebuildPanelMesh();
	void TrimRailMeshComponents(int32 DesiredCount);
	USplineMeshComponent* GetOrCreateRailMeshComponent(int32 SegmentIndex);
	void TrimPostOverrideMeshComponents(int32 DesiredCount);
	UStaticMeshComponent* GetOrCreatePostOverrideMeshComponent(int32 ComponentIndex);
	const FEHBRailingPostMeshOverride* FindPostMeshOverride(FGuid PostGuid) const;
	bool HasPostMeshOverride(FGuid PostGuid) const;
	float GetRailMeshTopOffset() const;
	float ComputeRailEndpointMiterExtension(const FVector& RailDirectionAwayFromJoint, const FVector& OtherRailDirectionAwayFromJoint, float SegmentLength) const;
	bool ComputeRailEndpointMiterTangent(const FVector& SegmentDirection, const FVector& OtherRailDirectionAwayFromJoint, bool bStartEndpoint, FVector& OutMiterTangent) const;
	bool FindCompatibleRailEndpointConnection(bool bStartEndpoint, FVector& OutOtherRailDirectionLocal) const;
	bool FindCompatibleEndpointOnRailing(const AEHB_Railing* OtherRailing, const FVector& EndpointWorld, FVector& OutOtherRailDirectionLocal) const;
	bool FindCompatibleEndpointOnStairEmbeddedRailing(const AEHB_Stair* Stair, const FVector& EndpointWorld, FVector& OutOtherRailDirectionLocal) const;
	bool IsEndpointNearRailingEndpoint(const AEHB_Railing* OtherRailing, const FVector& EndpointWorld) const;
	bool IsRailEndpointConnectedToCompatibleRailing(bool bStartEndpoint) const;
	bool IsRailStyleCompatibleWith(const AEHB_Railing* OtherRailing) const;
	bool IsRailStyleCompatibleWithStair(const AEHB_Stair* Stair) const;
	bool IsRailingSampleCompatibleWith(const AEHB_Railing* OtherRailing) const;
	bool IsRailingSampleCompatibleWithStair(const AEHB_Stair* Stair) const;
	bool HasRailingSampleIdentity() const;
	bool TryGetRailingSampleTemplateGuid(FGuid& OutTemplateGuid) const;
	FVector GetRailingEndpointWorldLocation(bool bStartEndpoint) const;
	bool IsDistanceInsideGateOpening(float Distance) const;
	bool IsDistanceNearGatePost(float Distance, FGuid& OutGatePostGuid, bool& bOutLeftPost) const;
	void NormalizeGateConnections();
	void ApplyMaterials();
};
