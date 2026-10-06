// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Definitions/EHBBuildingTypes.h"
#include "Definitions/EHBElementRelations.h"
#include "Core/EHBWallTopology.h"
#include "Core/EHBWallNodeRooms.h"
#include "Definitions/EHBCommittedEdit.h"
#include "Core/EHBRoomDependencyCache.h"
#include "Core/EHBSurfaceRoomCoverage.h"
#include "GameFramework/Actor.h"
#include "EHBBuildingActorBase.generated.h"

class FEHBChangeNotificationBatch;
class AEHBElementActorBase;
class AEHB_Pillar;
class AEHB_Wall;
class UBoxComponent;
class USceneComponent;
class UEHBWallJunctionComponent;

/** Counts explicit dispatches and definition solves, not nested callbacks, reference staging or GPU work. */
struct EASYHOUSEBUILDER_API FEHBConnectedWallRefreshStats
{
	bool bSucceeded = true;
	FName Status;
	int32 DefinitionSolveCalls = 0;
	int32 PillarsVisited = 0;
	int32 WallRefreshCalls = 0;
	int32 JunctionRefreshCalls = 0;
	bool bUsedScopedUpdate = false;
	int32 WallSidesSolved = 0;
	int32 NodeFootprintsSolved = 0;
	int32 UnboundMeshesBuilt = 0;
};

struct EASYHOUSEBUILDER_API FEHBWallCreationEndpoint
{
	FVector WorldLocation = FVector::ZeroVector;
	FVector LocalLocation = FVector::ZeroVector;
	AEHB_Pillar* Pillar = nullptr;
	AEHB_Wall* Wall = nullptr;
	float WallDistance = 0.0f;
	int32 FloorIndex = 1;
	// An explicit V2 logical anchor. Local/world pose, floor and revision must
	// match the current node; an optional Pillar must be its actual binding.
	FGuid NodeGuid;
	int32 ExpectedNodeRevision = INDEX_NONE;
};

struct EASYHOUSEBUILDER_API FEHBWallCreationOptions
{
	/** Applies only to newly authored nodes; existing bindings are always retained. */
	bool bCreatePhysicalColumns = true;
	float WallHeight = 300.0f;
	float WallThickness = 20.0f;
	float PillarHeight = 300.0f;
	float PillarWidth = 20.0f;
	float PillarDepth = 20.0f;
	float EndpointSnapDistance = 30.0f;
	int32 FloorIndex = 1;
	FRotator FreePillarLocalRotation = FRotator::ZeroRotator;
	bool bSnapToIntegerBuildingCoordinates = true;
	FString NewPillarNamePrefix = TEXT("EHB_Pillar");
};

struct EASYHOUSEBUILDER_API FEHBWallCreationResult
{
	TArray<AEHB_Wall*> Walls;
	AEHB_Wall* PrimaryWall = nullptr;
	AEHB_Pillar* StartPillar = nullptr;
	AEHB_Pillar* EndPillar = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEHBElementRelationChangedSignature, FGuid, RelationGuid);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEHBEditCommittedSignature, const FEHBCommittedEdit&, Edit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEHBElementGeometryChangedSignature, FGuid, ElementGuid, bool, bFinished);

USTRUCT(BlueprintType, meta = (DisplayName = "墙柱连接记录"))
struct EASYHOUSEBUILDER_API FEHBPillarWallConnection
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "墙柱关系", meta = (DisplayName = "对端柱子Guid"))
	FGuid OtherPillarGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "墙柱关系", meta = (DisplayName = "连接墙Guid"))
	FGuid WallGuid;

	bool operator==(const FEHBPillarWallConnection& Other) const
	{
		return OtherPillarGuid == Other.OtherPillarGuid && WallGuid == Other.WallGuid;
	}
};

USTRUCT(BlueprintType, meta = (DisplayName = "柱子连接列表"))
struct EASYHOUSEBUILDER_API FEHBPillarWallConnectionList
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "墙柱关系", meta = (DisplayName = "连接列表"))
	TArray<FEHBPillarWallConnection> Connections;
};

USTRUCT(BlueprintType, meta = (DisplayName = "建筑墙连接记录"))
struct EASYHOUSEBUILDER_API FEHBBuildingWallConnection
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "墙柱关系", meta = (DisplayName = "墙Guid"))
	FGuid WallGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "墙柱关系", meta = (DisplayName = "起点柱子Guid"))
	FGuid StartPillarGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "墙柱关系", meta = (DisplayName = "终点柱子Guid"))
	FGuid EndPillarGuid;
};

USTRUCT(BlueprintType, meta = (DisplayName = "建筑闭环房间"))
struct EASYHOUSEBUILDER_API FEHBBuildingClosedLoop
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "闭环房间", meta = (DisplayName = "闭环Guid"))
	FGuid LoopGuid;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "闭环房间", meta = (DisplayName = "所属楼层"))
	int32 FloorIndex = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "闭环房间", meta = (DisplayName = "柱子Guid顺序"))
	TArray<FGuid> PillarGuids;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "闭环房间", meta = (DisplayName = "墙Guid顺序"))
	TArray<FGuid> WallGuids;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "闭环房间", meta = (DisplayName = "是否顺时针"))
	bool bClockwise = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "闭环房间", meta = (DisplayName = "平面面积"))
	float Area = 0.0f;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Guid列表"))
struct EASYHOUSEBUILDER_API FEHBGuidList
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Guid")
	TArray<FGuid> Guids;
};

USTRUCT(BlueprintType, meta = (DisplayName = "Wall Pillar Set"))
struct EASYHOUSEBUILDER_API FEHBWallPillarSet
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall Pillar Set")
	TArray<TObjectPtr<AEHB_Wall>> Walls;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wall Pillar Set")
	TArray<TObjectPtr<AEHB_Pillar>> Pillars;
};

/**
 * 建筑对象 Actor 的运行时基类。
 * 它承担两个核心职责：
 * 1. 作为场景中可被编辑模式选中的建筑根对象；
 * 2. 维护场景元素 Actor 的楼层、Guid 和墙柱拓扑索引。
 */
UCLASS(Abstract, BlueprintType, Blueprintable, meta = (DisplayName = "建筑对象基类", ToolTip = "程序化建筑对象的基础 Actor。它维护建筑唯一标识、元素索引、墙柱拓扑和可放置边界。"))
class EASYHOUSEBUILDER_API AEHBBuildingActorBase : public AActor
{
	GENERATED_BODY()

public:
	AEHBBuildingActorBase();

	/** 构造脚本执行时确保建筑对象拥有稳定 Guid。 */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 资产加载后补齐旧数据可能缺失的 Guid。 */
	virtual void PostLoad() override;
 virtual void BeginPlay() override;

	/** 从编辑器或运行时创建 Actor 后立即补齐 Guid。 */
	virtual void PostActorCreated() override;

	virtual void PostRegisterAllComponents() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;

#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

	/** 确保建筑对象拥有唯一 ID；如果当前 ID 无效，则自动生成。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象", meta = (DisplayName = "确保建筑唯一标识", ToolTip = "如果建筑对象还没有有效唯一标识，则生成一个新的 Guid。通常创建建筑对象后会自动调用。"))
	void EnsureBuildingGuid();

	/** 返回可放置建筑元素的世界空间包围盒。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象", meta = (DisplayName = "获取放置范围", ToolTip = "返回该建筑对象允许放置元素的世界空间包围盒。编辑器工具会用它判断元素是否位于建筑范围内。"))
	FBox GetPlacementBounds() const;

	/** 判断指定世界坐标是否落在建筑对象的可放置范围内。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象", meta = (DisplayName = "位置是否在放置范围内", ToolTip = "判断一个世界空间位置是否位于该建筑对象的可放置范围内。"))
	bool IsLocationInsidePlacementBounds(const FVector& WorldLocation) const;

	/** 将场景元素 Actor 的 Guid 注册到指定楼层。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|楼层", meta = (DisplayName = "注册元素到楼层"))
	void RegisterElementToFloor(FGuid ElementGuid, EEHBBuildingElementType ElementType, int32 FloorIndex, EEHBBuildingFloorElementRole FloorRole);

	/** 从所有楼层索引中移除指定元素 Guid。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|楼层", meta = (DisplayName = "从楼层注销元素"))
	void UnregisterElementFromFloors(FGuid ElementGuid);

	/** 获取某一层保存的轻量元素索引记录。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|楼层", meta = (DisplayName = "获取楼层元素索引"))
	TArray<FEHBBuildingFloorElementEntry> GetFloorElementEntries(int32 FloorIndex) const;

	/** 获取某一层中的场景元素 Actor，例如墙体、柱子和层板。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|楼层", meta = (DisplayName = "获取楼层Actor元素"))
	TArray<AEHBElementActorBase*> GetElementActorsByFloor(int32 FloorIndex) const;

	/** 根据世界 Z 高度推导楼层。无法推导时返回 INDEX_NONE。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|楼层", meta = (DisplayName = "根据世界Z查询楼层"))
	int32 ResolveFloorIndexFromWorldZ(float WorldZ) const;

	/** 根据世界位置的高度推导楼层。无法推导时返回 INDEX_NONE。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|楼层", meta = (DisplayName = "根据世界位置查询楼层"))
	int32 ResolveFloorIndexFromWorldLocation(const FVector& WorldLocation) const;

	// #region Unified element relationship graph

	/** Rebuilds actor, floor and relation indexes from serialized actors and relation records. */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "重建元素与关系索引"))
	void RebuildElementAndRelationshipIndexes();

	UFUNCTION(BlueprintCallable, Category = "Building|Elements", meta = (DisplayName = "Clear All Building Elements"))
	int32 ClearAllElements();

	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "开始批量修改关系"))
	void BeginRelationshipEdit();

	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "结束批量修改关系"))
	void EndRelationshipEdit(bool bResolveAutomaticFloors = true);

	/**
	 * Adds a relation or updates the equivalent Source/Target/Type record.
	 * The returned Guid is invalid when the relation does not pass validation.
	 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "添加或更新元素关系"))
	FGuid AddOrUpdateElementRelation(const FEHBElementRelation& Relation, bool bReplaceEquivalent = true);

	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "移除元素关系"))
	bool RemoveElementRelation(FGuid RelationGuid);

	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "移除元素的全部关系"))
	int32 RemoveAllRelationsForElement(FGuid ElementGuid);

	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "移除两个元素之间的关系"))
	int32 RemoveRelationsBetweenElements(
		FGuid FirstElementGuid,
		FGuid SecondElementGuid,
		EEHBElementRelationType RelationType = EEHBElementRelationType::None);

	UFUNCTION(BlueprintCallable, Category = "建筑对象|承重", meta = (DisplayName = "设置结构承重关系"))
	FGuid SetStructuralSupportRelation(
		AEHBElementActorBase* Supporter,
		AEHBElementActorBase* SupportedElement,
		EEHBElementSurfaceKind SupportSurface,
		EEHBElementSurfaceKind SupportedSurface,
		const FVector& ContactPoint,
		const FVector& ContactNormal,
		float ContactArea,
		EEHBRelationOrigin Origin);

	/** ExternalActor may be null when bUseWorldGround is true. */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|承重", meta = (DisplayName = "设置外部承重关系"))
	FGuid SetExternalStructuralSupportRelation(
		AActor* ExternalActor,
		AEHBElementActorBase* SupportedElement,
		bool bUseWorldGround,
		EEHBElementSurfaceKind SupportedSurface,
		const FVector& ContactPoint,
		const FVector& ContactNormal,
		float ContactArea,
		EEHBRelationOrigin Origin);

	UFUNCTION(BlueprintCallable, Category = "建筑对象|承重", meta = (DisplayName = "查询元素的支撑者"))
	TArray<AEHBElementActorBase*> GetElementSupporters(FGuid ElementGuid) const;

	UFUNCTION(BlueprintCallable, Category = "建筑对象|承重", meta = (DisplayName = "查询元素支撑的对象"))
	TArray<AEHBElementActorBase*> GetElementsSupportedBy(FGuid ElementGuid, bool bRecursive, int32 MaxDepth) const;

	/** Returns all downstream support, hosting, finish, boundary and logical dependents. */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "查询受元素影响的对象"))
	TArray<AEHBElementActorBase*> GetElementsAffectedBy(FGuid ElementGuid, bool bRecursive, int32 MaxDepth) const;

	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "查询关系"))
	TArray<FEHBElementRelation> QueryElementRelations(const FEHBRelationQuery& Query) const;

	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "查询相关元素Guid"))
	TArray<FGuid> GetRelatedElementGuids(
		FGuid ElementGuid,
		EEHBRelationQueryDirection Direction,
		const TArray<EEHBElementRelationType>& RelationTypes,
		bool bRecursive,
		int32 MaxDepth) const;

	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "查询相关元素Actor"))
	TArray<AEHBElementActorBase*> GetRelatedElementActors(
		FGuid ElementGuid,
		EEHBRelationQueryDirection Direction,
		const TArray<EEHBElementRelationType>& RelationTypes,
		bool bRecursive,
		int32 MaxDepth) const;

	/** Returns the element Guid path, including start and end. Empty means no path. */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "查找元素关系路径"))
	TArray<FGuid> FindElementRelationPath(
		FGuid StartElementGuid,
		FGuid EndElementGuid,
		EEHBRelationQueryDirection Direction,
		const TArray<EEHBElementRelationType>& RelationTypes,
		int32 MaxDepth = 64) const;

	UFUNCTION(BlueprintCallable, Category = "建筑对象|查询", meta = (DisplayName = "按条件查询建筑元素"))
	TArray<AEHBElementActorBase*> QueryElements(const FEHBElementQuery& Query) const;

	/** Useful for unsupported structures, unhosted openings, unfinished surfaces and disconnected accessories. */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|查询", meta = (DisplayName = "查询缺少指定关系的元素"))
	TArray<AEHBElementActorBase*> QueryElementsWithoutRelation(
		const FEHBElementQuery& ElementQuery,
		EEHBElementRelationType RelationType,
		EEHBRelationQueryDirection Direction) const;

	UFUNCTION(BlueprintPure, Category = "建筑对象|关系", meta = (DisplayName = "关系是否因几何变化失效"))
	bool IsElementRelationStale(FGuid RelationGuid) const;

	/** Validates identity, endpoint ownership, duplicate records and stale auto-generated geometry relations. */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|关系", meta = (DisplayName = "校验元素关系图"))
	TArray<FEHBRelationValidationIssue> ValidateElementRelationshipGraph(
		bool bRepairInvalidRelations,
		bool bRemoveStaleAutoRelations);

	/** Called by element actors after transform or shape changes. */
	void NotifyElementGeometryChanged(FGuid ElementGuid, bool bFinished);

	UFUNCTION(BlueprintPure, Category = "建筑对象|关系", meta = (DisplayName = "获取元素几何版本"))
	int32 GetElementGeometryRevision(FGuid ElementGuid) const;

	/** Recomputes elements whose floor policy is Automatic from structural support relations. */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|楼层", meta = (DisplayName = "根据承重关系解析自动楼层"))
	void ResolveAutomaticFloorAssignments();

	// #endregion Unified element relationship graph

	// #region 柱子与墙体关系 - 统一入口、查询、删除和闭环缓存

	/** 注册场景元素 Actor 到建筑对象 Guid 索引。所有通过 Guid 查 Actor 的入口都会优先使用这个索引。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "注册元素Actor"))
	void RegisterElementActor(AEHBElementActorBase* ElementActor);

	/** 从建筑对象 Guid 索引中移除场景元素 Actor。元素 Actor 删除时会调用。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "注销元素Actor"))
	void UnregisterElementActor(AEHBElementActorBase* ElementActor);

	/** 通过 Guid 查询任意建筑元素 Actor，不只限于墙和柱。索引失效时会扫描当前建筑下的附加 Actor。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "按Guid查找元素Actor"))
	AEHBElementActorBase* FindElementActorByGuid(FGuid ElementGuid) const;

	/** 校验两个柱子是否具备连接的基础条件：同属本建筑、Guid 有效、距离有效、尚未连接。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "能否连接两个柱子"))
	bool CanConnectPillars(const AEHB_Pillar* FirstPillar, const AEHB_Pillar* SecondPillar) const;

	/** 预留的连接规则扩展点。后续可以在这里判断阻塞物、楼层、权限或其他建筑规则。 */
	UFUNCTION(BlueprintNativeEvent, Category = "建筑对象|墙柱关系", meta = (DisplayName = "能否通过扩展规则连接柱子"))
	bool CanConnectPillarsByRule(const AEHB_Pillar* FirstPillar, const AEHB_Pillar* SecondPillar) const;
	virtual bool CanConnectPillarsByRule_Implementation(const AEHB_Pillar* FirstPillar, const AEHB_Pillar* SecondPillar) const;

	/** 统一连接入口：校验两个柱子、创建墙体、记录柱子/墙/建筑三方关系并重建闭环房间缓存。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "连接两个柱子并创建墙"))
	AEHB_Wall* ConnectPillars(AEHB_Pillar* FirstPillar, AEHB_Pillar* SecondPillar, float WallHeight, float WallThickness);

	FVector RoundBuildingLocalCoordinates(const FVector& LocalLocation) const;
	FVector SnapWorldLocationToIntegerBuildingCoordinates(const FVector& WorldLocation) const;
	void SnapPillarToIntegerBuildingCoordinates(AEHB_Pillar* Pillar, bool bFinished = true);
	AEHB_Pillar* CreatePillarAtLocalLocation(
		const FVector& LocalLocation,
		const FRotator& LocalRotation,
		float PillarHeight,
		float PillarWidth,
		float PillarDepth,
		int32 FloorIndex,
		const FString& NamePrefix,
		bool bSnapToIntegerBuildingCoordinates = true);
	bool ResolveWallCreationEndpoint(
		const FVector& DesiredWorldLocation,
		float SnapDistance,
		float WallThickness,
		int32 FallbackFloorIndex,
		const AEHB_Pillar* IgnoredPillar,
		FEHBWallCreationEndpoint& OutEndpoint) const;
	bool CreateOrReuseWallSegment(
		FEHBWallCreationEndpoint& StartEndpoint,
		FEHBWallCreationEndpoint& EndEndpoint,
		const FEHBWallCreationOptions& Options,
		FEHBWallCreationResult& OutResult);

	// #region 柱子与墙体关系 - 墙上插柱与拆墙
	/** 在已有墙中心线上插入新柱子，将原墙拆分为两段新墙，并统一刷新墙柱连接关系。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "在墙上插入柱子并拆分墙"))
	AEHB_Pillar* InsertPillarOnWall(AEHB_Wall* SourceWall, float DistanceFromWallStart, float PillarHeight, float PillarWidthDepth, TArray<AEHB_Wall*>& OutNewWalls);

	/** 在同一面已有墙中心线上插入两根新柱子，将原墙拆分为三段新墙；OutFirst/OutSecond 保持传入距离的顺序。 */
	bool InsertTwoPillarsOnWall(AEHB_Wall* SourceWall, float FirstDistanceFromWallStart, float SecondDistanceFromWallStart, float PillarHeight, float PillarWidthDepth, AEHB_Pillar*& OutFirstPillar, AEHB_Pillar*& OutSecondPillar, TArray<AEHB_Wall*>& OutNewWalls);
	// #endregion 柱子与墙体关系 - 墙上插柱与拆墙

	/** 断开两个柱子：删除双方连接记录、建筑连接记录、闭环缓存，并按需销毁它们之间的墙 Actor。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "断开两个柱子"))
	bool DisconnectPillars(FGuid FirstPillarGuid, FGuid SecondPillarGuid, bool bDestroyWallActor = true);

	/** 删除某根柱子的全部墙连接。柱子被删除时调用，会销毁所有相连墙体。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "断开柱子全部连接"))
	void DisconnectAllPillarConnections(FGuid PillarGuid, bool bDestroyWallActors = true);

	/** 墙体删除时调用，用墙上保存的起终点 Guid 清理建筑和柱子的连接关系。墙对象可能已经处于删除流程中。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "处理墙体删除"))
	void HandleWallDeleted(AEHB_Wall* Wall);

	/** 柱子删除时调用，统一销毁与该柱子连接的墙并清理闭环缓存。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "处理柱子删除"))
	void HandlePillarDeleted(AEHB_Pillar* Pillar);

	/** 柱子移动、旋转或尺寸变化时调用，由建筑对象统一刷新所有受影响墙体。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "刷新柱子连接墙"))
	void RefreshWallsConnectedToPillar(FGuid PillarGuid, bool bFinished = true);

	/** Refresh overlapping root components once; retains legacy per-endpoint wall dispatch order. */
	FEHBConnectedWallRefreshStats RefreshWallsConnectedToPillars(const TArray<FGuid>& PillarGuids, bool bFinished = true);
	/** Validated plain straight-wall position edits only; all moved centers must already be applied.
	 * Visits moved nodes and their immediate neighbors, retaining all walls incident to those nodes.
	 * Not suitable for arbitrary geometry, relationship or support changes. Caller owns transaction. */
	/** Caller applies poses and owns transaction/rollback. Validates complete current source and batch before generation. */
	FEHBConnectedWallRefreshStats RefreshNodeDefinitionDraft(const FEHBWallNodeMoveDraft& Draft,bool bFinished=true);
	FEHBConnectedWallRefreshStats RefreshWallMoveNeighborhood(const TArray<FGuid>& MovedPillarGuids, bool bFinished = true, const FEHBWallMoveUpdatePlan* ExpectedPlan = nullptr);

	/** 通过两个柱子 Guid 查询它们之间的墙，并返回传入顺序是否与墙中起点/终点顺序相同。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "查询两个柱子之间的墙"))
	AEHB_Wall* FindWallBetweenPillars(FGuid FirstPillarGuid, FGuid SecondPillarGuid, bool& bSameDirection) const;

	/** 通过任意柱子 Guid 查询它参与的所有闭环房间。共享边界柱子会返回多个闭环。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "查询柱子参与的闭环"))
	TArray<FEHBBuildingClosedLoop> GetClosedLoopsByPillarGuid(FGuid PillarGuid) const;

	/** 通过任意墙 Guid 查询它参与的所有闭环房间。共享墙通常会返回左右两个闭环。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "查询墙参与的闭环"))
	TArray<FEHBBuildingClosedLoop> GetClosedLoopsByWallGuid(FGuid WallGuid) const;

	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "按楼层查询闭环"))
	TArray<FEHBBuildingClosedLoop> GetClosedLoopsByFloor(int32 FloorIndex) const;

	/** Building-local centerline boundary with paired logical nodes and walls; never a physical support identity. */
	UFUNCTION(BlueprintCallable, Category="EasyHouse|Room")
	bool TryGetRoomBoundary(FGuid RoomGuid, int32 FloorIndex, FEHBNodeRoomBoundary& Boundary) const;

	/** A wall is exterior when it bounds exactly one valid closed room loop on its floor. */
	UFUNCTION(BlueprintCallable, Category = "Building|Rooms", meta = (DisplayName = "Is Exterior Wall"))
	bool IsExteriorWall(const AEHB_Wall* Wall) const;

	/** Returns every wall and pillar that belongs to the supplied closed room loop. */
	UFUNCTION(BlueprintCallable, Category = "Building|Rooms", meta = (DisplayName = "Get Room Walls And Pillars"))
	FEHBWallPillarSet GetRoomWallsAndPillars(const FEHBBuildingClosedLoop& RoomLoop) const;

	/** Returns the connected exterior shell containing ReferenceWall. */
	UFUNCTION(BlueprintCallable, Category = "Building|Rooms", meta = (DisplayName = "Get Exterior Walls And Pillars"))
	FEHBWallPillarSet GetExteriorWallsAndPillarsFromWall(const AEHB_Wall* ReferenceWall) const;

	/** 通过世界位置查询包含该点的闭环房间。FloorIndex 为 -99 时会根据世界高度自动推导楼层。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|房间", meta = (DisplayName = "通过世界位置查询包含房间"))
	TArray<FEHBBuildingClosedLoop> FindClosedLoopsContainingWorldLocation(
		const FVector& WorldLocation,
		int32 FloorIndex = -99) const;

	/**
	 * 通用命中点房间查询。
	 * WorldHitNormal 为零向量时直接用 WorldLocation 查询；如果命中多个房间则返回 false。
	 * WorldHitNormal 非零时沿法线偏移 ProbeDistance 后查询，用于墙/柱等边界侧面命中。
	 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|房间", meta = (DisplayName = "通过命中点查询房间"))
	bool FindClosedLoopByWorldHit(
		const FVector& WorldLocation,
		const FVector& WorldHitNormal,
		FEHBBuildingClosedLoop& OutClosedLoop,
		int32 FloorIndex = -99,
		float ProbeDistance = 2.0f) const;

	/** 从当前墙柱连接图重新生成所有平面闭环房间，并重建柱/墙到闭环的查询索引。 */
	UFUNCTION(BlueprintCallable, Category = "建筑对象|墙柱关系", meta = (DisplayName = "重建闭环房间缓存"))
	void RebuildClosedLoops();

	// #endregion 柱子与墙体关系

	/** 建筑对象的根组件，所有可视组件和范围组件都挂在它下面。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "根组件", ToolTip = "建筑对象的场景根组件。移动建筑对象时，所有子组件和元素生成结果都会跟随它移动。"))
	TObjectPtr<USceneComponent> SceneRoot;

	/** 建筑允许放置元素的范围框。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "组件", meta = (DisplayName = "放置范围", ToolTip = "建筑对象允许创建或编辑元素的范围框。后续工具会使用它判断点位是否属于当前建筑对象。"))
	TObjectPtr<UBoxComponent> PlacementBounds;

	/** 建筑对象唯一 ID，用于保存、加载和跨工具引用。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑对象", meta = (DisplayName = "建筑唯一标识", ToolTip = "该建筑对象的全局唯一标识。用于保存、加载以及编辑器工具中的稳定引用。"))
	FGuid BuildingGuid;

	/** 按楼层保存的元素 Guid 索引。Key 为楼层序号，Value 是该层的墙、柱、楼顶板等元素引用。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "建筑关系|楼层", meta = (DisplayName = "楼层元素索引"))
	TMap<int32, FEHBBuildingFloorElementList> FloorElementsByIndex;

	/** The only serialized generic relationship store. Runtime indexes are rebuilt from this array. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑关系|统一关系", meta = (DisplayName = "元素关系图"))
	TArray<FEHBElementRelation> ElementRelations;

	/** Opt-in migration baseline only; not yet the authority for movement or rooms. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑关系|节点迁移")
	FEHBPersistedWallTopology TopologyMigrationBaseline;

	/** Explicit, inactive node migration payload. It never silently becomes an authority on load. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="建筑关系|节点迁移")
	FEHBPreparedWallNodeDefinitions PreparedWallNodeDefinitions;

	/** Explicit connection ownership; never auto-enabled for an old asset. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="建筑关系|节点迁移")
	FEHBWallNodeOwnership WallNodeOwnership;

	/** Enabled only by explicit migration; queries and rebuilds never recapture proxy values. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="建筑关系|节点迁移")
	FEHBWallNodeAuthority WallNodeAuthority;
	bool HasWallNodeAuthority() const { return WallNodeAuthority.Version==1||WallNodeAuthority.Version==2; }
	/** Rebuild derived wall/junction geometry from the live optional-binding model. */
	bool RebuildWallNodeAuthorityGeometry();
	/** Apply an already-installed, unchanged-topology position draft. Caller owns the
	 * composite edit. Invalid/stale drafts do not mutate; cold derived state rebuilds fully. */
	FEHBConnectedWallRefreshStats RefreshWallNodeMoveGeometry(const FEHBWallNodeModel& Source,const FEHBWallNodeModelEditDraft& Draft);
	/** Materialize one native wall from a validated node plan. Caller owns the composite edit,
	 * dependency migration and final geometry rebuild; never creates missing physical columns. */
	AEHB_Wall* CreateWallFromNodePlan(const FEHBNodeConnectedWallDefinition& Definition,const FEHBWallJunctionWallSides& Geometry);
	UEHBWallJunctionComponent* FindWallNodeJunction(FGuid NodeGuid) const;
	FEHBTopologyMigrationResult MigrateWallNodeAuthority(bool bApply);
	/** Authored writes only. Not for load, undo, index rebuild or mesh notifications. */
	void RecordAuthoredWallNode(AEHB_Pillar* Pillar);

	/** Preview/apply typed endpoint migration. Caller owns the edit transaction. */
	FEHBTopologyMigrationResult MigrateWallNodeOwnership(bool bApply);
	void RegisterAuthoredWallNode(AEHB_Pillar* Pillar);
	bool IsChangeNotificationBusy() const { return ActiveChangeNotificationBatch != nullptr || bPublishingChangeNotifications; }
	bool HasUnpublishedEdit() const { return ActiveChangeNotificationBatch != nullptr; }
	FGuid FindNodeForPhysicalPillar(FGuid PillarGuid) const;
	FGuid FindPhysicalPillarForNode(FGuid NodeGuid) const;
	FGuid ResolveTopologyPillar(const FEHBElementRelationEndpoint& Endpoint) const;


	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑关系|统一关系", meta = (DisplayName = "关系图版本"))
	int32 RelationshipGraphRevision = 0;

	/** Serialized schema version for one-time legacy migration. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑关系|统一关系", meta = (DisplayName = "关系数据版本"))
	int32 RelationshipSchemaVersion = 0;

	/** Refresh membership once for a set of rooms; all authored binding fields are checked. */
	bool QueryRoomDependencies(const TArray<FEHBBuildingClosedLoop>& Rooms,TArray<FEHBRoomDependencyMembers>& Out) const;
	/** Fresh spatial query for an uncut horizontal floor/slab in a validated node model.
	 * Independent regions keep their source; this does not bind or edit them.
	 * Intended for edit-driven refresh, not a full-building query every frame. */
	UFUNCTION(BlueprintCallable,Category="EasyHouse|Room")
	bool QuerySurfaceRoomCoverage(FGuid ElementGuid,FEHBSurfaceRoomCoverage& Coverage,FName& Status) const;
	FEHBRoomDependencyCacheStats GetRoomDependencyCacheStats() const { return RoomDependencyCache.GetStats(); }

	/** Latest successful unified edit; undo restores its parent receipt. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="EHB|Editing")
	FEHBCommittedEdit LastCommittedEdit;
	UPROPERTY(BlueprintAssignable,Category="EHB|Editing")
	FEHBEditCommittedSignature OnEditCommitted;

	UPROPERTY(BlueprintAssignable, Category = "建筑关系|事件")
	FEHBElementRelationChangedSignature OnElementRelationAdded;

	UPROPERTY(BlueprintAssignable, Category = "建筑关系|事件")
	FEHBElementRelationChangedSignature OnElementRelationRemoved;

	UPROPERTY(BlueprintAssignable, Category = "建筑关系|事件")
	FEHBElementGeometryChangedSignature OnElementGeometryChanged;

	// #region 柱子与墙体关系 - 建筑级缓存数据

	/** 建筑对象级墙连接表。Key 为墙 Guid，Value 保存墙连接的起点柱和终点柱。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "建筑关系|墙柱", meta = (DisplayName = "墙连接表"))
	TMap<FGuid, FEHBBuildingWallConnection> WallConnectionsByWallGuid;

	/** 建筑对象级柱连接表。Key 为柱 Guid，Value 保存该柱连接到的墙和对端柱。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "建筑关系|墙柱", meta = (DisplayName = "柱连接表"))
	TMap<FGuid, FEHBPillarWallConnectionList> PillarConnectionsByPillarGuid;

	/** 当前建筑拓扑生成出的闭环房间缓存。连接或断开墙体后会重建。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "建筑关系|闭环", meta = (DisplayName = "闭环房间缓存"))
	TArray<FEHBBuildingClosedLoop> ClosedLoops;

	/** 柱子 Guid 到闭环 Guid 的查询索引。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "建筑关系|闭环", meta = (DisplayName = "柱子闭环索引"))
	TMap<FGuid, FEHBGuidList> PillarToLoopGuids;

	/** 墙 Guid 到闭环 Guid 的查询索引。 */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "建筑关系|闭环", meta = (DisplayName = "墙闭环索引"))
	TMap<FGuid, FEHBGuidList> WallToLoopGuids;

	// #endregion 柱子与墙体关系

private:
	mutable FEHBRoomDependencyCache RoomDependencyCache;
	friend class FEHBChangeNotificationBatch;
	FEHBChangeNotificationBatch* ActiveChangeNotificationBatch = nullptr;
	bool bPublishingChangeNotifications = false;
	FEHBConnectedWallRefreshStats RefreshConnectedWallNodes(const TArray<FGuid>& Roots,bool bFinished,const TSet<FGuid>* AllowedNodes);
	static constexpr int32 CurrentRelationshipSchemaVersion = 1;

	/** 运行时/编辑器临时 Actor Guid 索引。它不序列化，加载后按需扫描附加 Actor 重建。 */
	mutable TMap<FGuid, TWeakObjectPtr<AEHBElementActorBase>> ElementActorByGuid;

	TMap<FGuid, int32> RelationIndexByGuid;
	TMultiMap<FGuid, FGuid> OutgoingRelationGuidsByElement;
	TMultiMap<FGuid, FGuid> IncomingRelationGuidsByElement;
	TMultiMap<FGuid, FGuid> OutgoingRelationGuidsByNode;
	TMultiMap<FGuid, FGuid> IncomingRelationGuidsByNode;
	TMap<FGuid, int32> ElementGeometryRevisionByGuid;

	bool bUpdatingPillarWallRelations = false;
	bool bRebuildingRelationshipIndexes = false;
	bool bClosedLoopsNeedRefresh = true;
	TMap<FGuid,TArray<FGuid>> ClosedLoopNodeCycles;
	UPROPERTY(Transient,NonTransactional) TMap<FGuid,TObjectPtr<UEHBWallJunctionComponent>> WallNodeJunctions;
	bool bRebuildingWallNodeGeometry=false;
	bool RebuildWallNodeAuthorityGeometryImpl(const FEHBWallMoveUpdatePlan* Plan,const FString* ExpectedSource,FEHBConnectedWallRefreshStats* Stats);
	// Derived, nonserialized baseline, never a model authority or a geometry cache.
	FString LastRenderedWallNodeSource;
	TMap<FGuid,int32> LastRenderedWallNodeElementRevisions;
	TMap<FGuid,FTransform> LastRenderedWallNodeWallTransforms;
	void EnsureClosedLoopsCurrent() const;
	int32 RelationshipEditDepth = 0;
	bool bPendingTopologyCacheRebuild = false;
	bool bPendingAutomaticFloorResolve = false;

	void RegisterPillarWallConnection(AEHB_Wall* Wall, AEHB_Pillar* StartPillar, AEHB_Pillar* EndPillar);
	void RemovePillarWallConnection(const FEHBBuildingWallConnection& Connection);
	AEHB_Pillar* SpawnSplitPillarForWall(const AEHB_Wall* SourceWall, float DistanceFromWallStart, float PillarHeight, float PillarWidthDepth);
	void SyncPillarConnectionData(AEHB_Pillar* Pillar, const FEHBPillarWallConnectionList* ConnectionList) const;

	void RebuildRelationshipIndexes();
	void RebuildFloorIndexFromActors();
	void MigrateLegacyRelationships();
	void RebuildLegacyTopologyCachesFromRelationships();
	void SyncLegacyHostedRelation(const FEHBElementRelation& Relation);
	void AddRelationToIndexes(const FEHBElementRelation& Relation, int32 RelationIndex);
	void RemoveRelationFromIndexes(const FEHBElementRelation& Relation);
	int32 FindEquivalentRelationIndex(const FEHBElementRelation& Relation) const;
	bool IsRelationEndpointOwnedByThisBuilding(const FEHBElementRelationEndpoint& Endpoint) const;
	bool IsRelationTypeInFilter(EEHBElementRelationType Type, const TArray<EEHBElementRelationType>& Filter) const;
	bool ShouldTraverseRelation(
		const FEHBElementRelation& Relation,
		const FGuid& CurrentElementGuid,
		EEHBRelationQueryDirection Direction,
		const TArray<EEHBElementRelationType>& RelationTypes,
		FGuid& OutOtherElementGuid) const;
	int32 ResolveFloorIndexFromBuildingLocalZ(double LocalZ) const;
	bool BuildClosedLoopPillarPolygon2D(const FEHBBuildingClosedLoop& Loop, TArray<FVector2d>& OutPolygon) const;
	TArray<FEHBBuildingClosedLoop> FindClosedLoopsContainingBuildingLocalPoint(
		const FVector& BuildingLocalPoint,
		int32 FloorIndex) const;
	void AddOrUpdateTopologyRelation(
		const AEHB_Wall* Wall,
		const AEHB_Pillar* Pillar,
		EEHBElementSurfaceKind WallEndpointSurface);
};
