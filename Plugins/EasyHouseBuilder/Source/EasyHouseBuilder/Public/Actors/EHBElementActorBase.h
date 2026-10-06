// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Definitions/EHBBuildingTypes.h"
#include "Definitions/EHBElementRelations.h"
#include "Core/EHBLogicalSurface.h"
#include "Interfaces/EHBCuttableTarget.h"
#include "Interfaces/EHBGeneratedMeshDataProvider.h"
#include "GameFramework/Actor.h"
#include "EHBElementActorBase.generated.h"

class AEHBBuildingActorBase;
class USceneComponent;
class UEHBGeneratedMeshComponent;
struct FEHBMeshAggregateData;
#if WITH_EDITOR
struct FPropertyChangedEvent;
#endif

/**
 * 建筑元素 Actor 的基础类。
 * 运行时和编辑器中真实存在、可点选、可保存的建筑元素都应从这里派生。
 */
UCLASS(Abstract, BlueprintType, Blueprintable, meta = (DisplayName = "EHB 元素Actor基类", ToolTip = "所有可在场景中独立点选、移动、创建和保存的建筑元素 Actor 基类。它保存元素唯一标识、显示名称、所属建筑对象和相对建筑对象的变换关系。"))
class EASYHOUSEBUILDER_API AEHBElementActorBase : public AActor, public IEHBGeneratedMeshDataProvider, public IEHBCuttableTarget
{
	GENERATED_BODY()

public:
	/** Serialized separately from generated components; legacy port GUIDs are adopted on load. */
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="建筑元素|表面")
	TArray<FEHBLogicalSurfaceIdentity> LogicalSurfaceIdentities;
	UPROPERTY()
	FGuid LogicalSurfaceIdentityOwner;
	FGuid FindLogicalSurfaceIdentity(FName Name,int32 SubIndex=INDEX_NONE) const;
	/** Called by construction/migration/publish paths, never by a query. */
	FGuid ResolveLogicalSurfaceIdentity(FName Name,int32 SubIndex,FGuid LegacyGuid={});
	void RefreshLogicalSurfaceIdentities();
	/** Authored base surfaces, before external cuts and visual expansion. */
	UFUNCTION(BlueprintCallable,Category="建筑元素|表面")
	bool QueryLogicalBaseSurfaces(TArray<FEHBLogicalSurfaceDefinition>& Surfaces,FName& Status) const;

	// ===== 组件 =====

	/** 元素 Actor 的根组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑元素|组件", meta = (DisplayName = "根组件", ToolTip = "该元素 Actor 的根组件。移动 Actor 时，根组件相对所属建筑对象的变换就是后续保存和加载使用的局部变换。"))
	TObjectPtr<USceneComponent> SceneRoot;

	// ===== 身份与归属 =====

	/** 该元素 Actor 所属的建筑对象。 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "建筑元素|归属", meta = (DisplayName = "所属建筑对象", ToolTip = "该元素 Actor 附加到的建筑对象。保存整栋建筑时，可以从建筑对象收集它下面的所有元素 Actor。"))
	TObjectPtr<AEHBBuildingActorBase> OwningBuilding;

	/** 元素唯一 ID，用于保存、加载、引用和跨元素连接。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑元素|身份", meta = (DisplayName = "元素唯一标识", ToolTip = "该建筑元素的全局唯一标识。用于保存、加载、撤销重做以及跨元素关系。"))
	FGuid ElementGuid;

	/** 元素在编辑器列表或调试信息中显示的名称。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "建筑元素|身份", meta = (DisplayName = "元素名称", ToolTip = "元素在编辑器和调试界面中显示的名称。为空时工具可以使用 Actor 名称或自动名称。"))
	FName ElementName = NAME_None;

	/** 元素类型，由具体子类在构造函数中设置。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑元素|身份", meta = (DisplayName = "元素类型", ToolTip = "当前元素所属的建筑元素类型，例如墙体、柱子、门窗等。该值由具体子类自动设置，用于保存和工具分类。"))
	EEHBBuildingElementType ElementType = EEHBBuildingElementType::None;

	/** 元素所属的楼层序号。地基为 0；一楼为 1。楼层角色为未归属时不参与索引。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "建筑元素|楼层", meta = (DisplayName = "所属楼层", ToolTip = "元素所属的楼层序号。地基为 0；一楼为 1。楼层角色为未归属时不参与建筑对象楼层索引。", ClampMin = "0"))
	int32 FloorIndex = 0;

	/** 元素在所属楼层中的角色，例如墙柱主体、楼层顶板或地基。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "建筑元素|楼层", meta = (DisplayName = "楼层角色", ToolTip = "描述元素在所属楼层中的语义角色，用于后续按层过滤和生成。"))
	EEHBBuildingFloorElementRole FloorRole = EEHBBuildingFloorElementRole::None;

	/** 自动模式由承重关系推导楼层；显式模式保留用户或导入数据指定的楼层。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "建筑元素|楼层", meta = (DisplayName = "楼层分配策略"))
	EEHBFloorAssignmentPolicy FloorAssignmentPolicy = EEHBFloorAssignmentPolicy::Automatic;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑元素|楼层", meta = (DisplayName = "楼层分配来源"))
	EEHBFloorAssignmentSource FloorAssignmentSource = EEHBFloorAssignmentSource::Unassigned;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑元素|楼层", meta = (DisplayName = "存在楼层冲突"))
	bool bFloorAssignmentConflict = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑元素|楼层", meta = (DisplayName = "冲突楼层候选"))
	TArray<int32> ConflictingFloorCandidates;

	/** Generic behavior flags used by relationship and element queries. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "建筑元素|关系", meta = (DisplayName = "元素能力", Bitmask, BitmaskEnum = "/Script/EasyHouseBuilder.EEHBElementCapability"))
	int32 ElementCapabilities = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "建筑元素|Cutting")
	TArray<FEHBCutOperation> CutOperations;

	/** Extensible semantic labels for project-specific queries without changing enums. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "建筑元素|关系", meta = (DisplayName = "语义标签"))
	TArray<FName> SemanticTags;

	// ===== 编辑器行为 =====

	/** 是否在编辑器中移动 Actor 时触发生命周期回调。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "建筑元素|编辑", meta = (DisplayName = "移动时触发生命周期", ToolTip = "开启后，在编辑器视口中移动该 Actor 会触发“元素Actor被移动”事件，后续可用于刷新依赖关系或连接构件。"))
	bool bNotifyWhenMovedInEditor = true;

protected:
	/** Internal source capture for generation from current authored state. Public
	 * queries retain the unpublished-edit consistency barrier. Never creates IDs. */
	bool BuildLogicalBaseSurfacesFromSource(TArray<FEHBLogicalSurfaceDefinition>& Surfaces,FName& Status) const;

	/** 防止删除流程重复触发生命周期。 */
	bool bHasNotifiedElementActorDeleted = false;

	/** 编辑器移动前缓存的局部变换，用于移动结束时传出旧值。 */
	FTransform CachedPreEditLocalTransform = FTransform::Identity;

public:
	// ===== 构造与生命周期 =====

	AEHBElementActorBase();

	virtual void PostActorCreated() override;
	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void Destroyed() override;

#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditUndo() override;
	/** A planned command already applied and validated this pose; subsequent native gizmo completion must not replay it. */
	virtual void SynchronizePlannedEditorMove();
#endif

	// ===== 身份与归属 =====

	/** 确保元素拥有有效 Guid。 */
	UFUNCTION(BlueprintCallable, Category = "建筑元素|身份", meta = (DisplayName = "确保元素唯一标识", ToolTip = "如果当前元素还没有有效唯一标识，则生成一个新的 Guid。通常创建或加载 Actor 后会自动调用。"))
	void EnsureElementGuid();

	/** Generates a new identity. Used when duplication or registry repair finds a collision. */
	void RegenerateElementGuid();

	/** 返回轻量元素句柄，用于保存和引用。 */
	UFUNCTION(BlueprintCallable, Category = "建筑元素|身份", meta = (DisplayName = "获取元素句柄", ToolTip = "返回包含元素唯一标识和元素类型的轻量句柄，适合 UI、保存数据或跨工具引用。"))
	FEHBBuildingElementHandle GetElementHandle() const;

	/** 把该元素附加到指定建筑对象下，并记录所属建筑对象。 */
	UFUNCTION(BlueprintCallable, Category = "建筑元素|归属", meta = (DisplayName = "附加到建筑对象", ToolTip = "把该元素 Actor 附加到指定建筑对象下，并记录所属建筑对象引用。附加后元素的相对变换就是保存时使用的局部变换。"))
	void AttachToBuilding(AEHBBuildingActorBase* InOwningBuilding, const FTransform& LocalTransform);

	/** 设置元素的楼层归属，并同步刷新所属建筑对象的楼层索引。 */
	UFUNCTION(BlueprintCallable, Category = "建筑元素|楼层", meta = (DisplayName = "设置楼层归属", ToolTip = "设置该元素属于第几层以及在该层中的角色，并立即同步到所属建筑对象。"))
	void SetFloorAssignment(int32 InFloorIndex, EEHBBuildingFloorElementRole InFloorRole);

	/** Returns this element to support-driven automatic floor assignment. */
	UFUNCTION(BlueprintCallable, Category = "建筑元素|楼层", meta = (DisplayName = "启用自动楼层分配"))
	void SetAutomaticFloorAssignment();

	/** Applies a floor value produced by the building relationship graph. */
	void ApplyDerivedFloorAssignment(
		int32 InFloorIndex,
		EEHBBuildingFloorElementRole InFloorRole,
		const TArray<int32>& InConflictingCandidates);
	/** Withdraw only an automatic derived assignment when no valid source remains. */
	bool ClearDerivedFloorAssignment();

	UFUNCTION(BlueprintPure, Category = "建筑元素|关系", meta = (DisplayName = "是否具有全部能力"))
	bool HasAllCapabilities(int32 RequiredCapabilities) const;

	/** Broad-phase bounds in owning-building local space for future contact/support detectors. */
	UFUNCTION(BlueprintPure, Category = "建筑元素|关系", meta = (DisplayName = "获取建筑局部包围盒"))
	FBox GetBuildingLocalBounds() const;

	virtual void GetGeneratedMeshComponents(TArray<UEHBGeneratedMeshComponent*>& OutComponents) const override;
	virtual bool BuildMeshAggregateData(const FTransform& TargetLocalToWorld, FEHBMeshAggregateData& OutData) const override;
	virtual TArray<FEHBCutOperation>& GetMutableCutOperations() override { return CutOperations; }
	virtual const TArray<FEHBCutOperation>& GetCutOperations() const override { return CutOperations; }
	bool AddCutOperation(FEHBCutOperation Operation);
	bool RemoveCutOperation(const FGuid& OperationGuid);
	bool UpdateCutOperation(const FEHBCutOperation& Operation);
	FEHBCutOperation* FindCutOperation(const FGuid& OperationGuid);
	const FEHBCutOperation* FindCutOperation(const FGuid& OperationGuid) const;
	bool RemoveCutPolygonPoint(const FGuid& OperationGuid, const FGuid& PointGuid);
	bool UpdateCutPolygonPoint(const FGuid& OperationGuid, const FGuid& PointGuid, const FVector& NewLocalPosition);

	UFUNCTION(BlueprintCallable, Category = "建筑元素|关系", meta = (DisplayName = "查询元素关系"))
	TArray<FEHBElementRelation> GetElementRelations(
		EEHBRelationQueryDirection Direction,
		const TArray<EEHBElementRelationType>& Types,
		bool bIncludeStale) const;

	UFUNCTION(BlueprintCallable, Category = "建筑元素|关系", meta = (DisplayName = "查询相关元素"))
	TArray<AEHBElementActorBase*> GetRelatedElements(
		EEHBRelationQueryDirection Direction,
		const TArray<EEHBElementRelationType>& Types,
		bool bRecursive,
		int32 MaxDepth) const;

	UFUNCTION(BlueprintCallable, Category = "建筑元素|承重", meta = (DisplayName = "获取支撑自己的元素"))
	TArray<AEHBElementActorBase*> GetSupporters() const;

	UFUNCTION(BlueprintCallable, Category = "建筑元素|承重", meta = (DisplayName = "获取自己支撑的元素"))
	TArray<AEHBElementActorBase*> GetSupportedElements(bool bRecursive, int32 MaxDepth) const;

	UFUNCTION(BlueprintCallable, Category = "建筑元素|关系", meta = (DisplayName = "获取受自己影响的元素"))
	TArray<AEHBElementActorBase*> GetAffectedElements(bool bRecursive, int32 MaxDepth) const;

	// ===== 变换 =====

	/** 获取元素相对所属建筑对象的局部变换。 */
	UFUNCTION(BlueprintCallable, Category = "建筑元素|变换", meta = (DisplayName = "获取元素局部变换", ToolTip = "返回该元素 Actor 相对所属建筑对象的局部变换。保存建筑时应保存这个变换，而不是世界变换。"))
	FTransform GetElementLocalTransform() const;

	/** 设置元素相对所属建筑对象的局部变换。 */
	UFUNCTION(BlueprintCallable, Category = "建筑元素|变换", meta = (DisplayName = "设置元素局部变换", ToolTip = "设置该元素 Actor 相对所属建筑对象的局部变换。如果元素已经附加到建筑对象下，会直接设置 Actor 的相对变换。"))
	void SetElementLocalTransform(const FTransform& NewLocalTransform, bool bFinished = true);

	// ===== 生命周期事件 =====

	/** 手动通知元素 Actor 被移动。 */
	UFUNCTION(BlueprintCallable, Category = "建筑元素|生命周期", meta = (DisplayName = "通知元素Actor被移动", ToolTip = "当编辑器工具或运行时代码移动元素 Actor 后调用。默认会转发到可在蓝图中重写的“元素Actor被移动”事件。"))
	void NotifyElementActorMoved(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished);

	/** 手动通知元素 Actor 被删除。 */
	UFUNCTION(BlueprintCallable, Category = "建筑元素|生命周期", meta = (DisplayName = "通知元素Actor被删除", ToolTip = "当元素 Actor 被删除或销毁前调用。默认会转发到可在蓝图中重写的“元素Actor被删除”事件。"))
	void NotifyElementActorDeleted();

	/** Marks geometry-dependent relations stale without treating the change as a transform move. */
	UFUNCTION(BlueprintCallable, Category = "建筑元素|生命周期", meta = (DisplayName = "通知元素几何已变化"))
	void NotifyElementGeometryChanged(bool bFinished = true);

	/** 元素 Actor 的局部变换发生变化时调用。 */
	UFUNCTION(BlueprintNativeEvent, Category = "建筑元素|生命周期", meta = (DisplayName = "元素Actor被移动", ToolTip = "当元素 Actor 的局部变换发生变化时调用。OldLocalTransform 是移动前变换，NewLocalTransform 是移动后变换，bFinished 表示本次移动是否已经结束。"))
	void OnElementActorMoved(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished);
	virtual void OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished);

	/** 元素 Actor 被删除或销毁时调用。 */
	UFUNCTION(BlueprintNativeEvent, Category = "建筑元素|生命周期", meta = (DisplayName = "元素Actor被删除", ToolTip = "当元素 Actor 被删除或销毁时调用。适合清理连接关系、通知所属建筑对象或释放生成结果。"))
	void OnElementActorDeleted();
	virtual void OnElementActorDeleted_Implementation();
};
