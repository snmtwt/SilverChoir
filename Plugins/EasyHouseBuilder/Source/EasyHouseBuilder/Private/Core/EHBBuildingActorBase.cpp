// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EHBBuildingActorBase.h"
#include "Core/EHBRoomRuntimeSubsystem.h"
#include "Core/EHBWallPathPlanning.h"
#include "Core/EHBChangeNotificationBatch.h"
#include "Core/EHBActorImportScope.h"
#include "Core/EHBRoomIdentity.h"
#include "Core/EHBWallNodeRooms.h"
#include "Core/EHBFloorAssignmentPlan.h"

#include "Actors/EHBElementActorBase.h"
#include "Actors/EHB_DoorWindow.h"
#include "Actors/EHB_Floor.h"
#include "Actors/EHB_FloorSlab.h"
#include "Actors/EHB_Pillar.h"
#include "Actors/EHB_Wall.h"
#include "Algo/Sort.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/EHBWallJunctionComponent.h"
#include "Containers/Queue.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Settings/EHBBuildingToolsetSettings.h"

namespace
{


	double CalculateSignedArea2D(const TArray<FVector>& Points)
	{
		if (Points.Num() < 3)
		{
			return 0.0;
		}

		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FVector& A = Points[Index];
			const FVector& B = Points[(Index + 1) % Points.Num()];
			TwiceArea += static_cast<double>(A.X) * static_cast<double>(B.Y)
				- static_cast<double>(B.X) * static_cast<double>(A.Y);
		}

		return TwiceArea * 0.5;
	}

	double CalculateSignedArea2D(const TArray<FVector2d>& Points)
	{
		if (Points.Num() < 3)
		{
			return 0.0;
		}

		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FVector2d& A = Points[Index];
			const FVector2d& B = Points[(Index + 1) % Points.Num()];
			TwiceArea += A.X * B.Y - B.X * A.Y;
		}

		return TwiceArea * 0.5;
	}

	bool IsStructuralFloorRangeRole(EEHBBuildingFloorElementRole Role)
	{
		return Role == EEHBBuildingFloorElementRole::FloorBody
			|| Role == EEHBBuildingFloorElementRole::FloorCeiling
			|| Role == EEHBBuildingFloorElementRole::Foundation;
	}

	bool IsPointOnSegment2D(const FVector2d& Point, const FVector2d& A, const FVector2d& B, double Tolerance)
	{
		const FVector2d Segment = B - A;
		const double SegmentLengthSquared = Segment.SquaredLength();
		if (SegmentLengthSquared <= UE_DOUBLE_SMALL_NUMBER)
		{
			return (Point - A).SquaredLength() <= Tolerance * Tolerance;
		}

		const double Alpha = FVector2d::DotProduct(Point - A, Segment) / SegmentLengthSquared;
		if (Alpha < -Tolerance || Alpha > 1.0 + Tolerance)
		{
			return false;
		}

		const FVector2d Closest = A + Segment * FMath::Clamp(Alpha, 0.0, 1.0);
		return (Point - Closest).SquaredLength() <= Tolerance * Tolerance;
	}

	bool IsPointInsideOrOnPolygon2D(const FVector2d& Point, const TArray<FVector2d>& Polygon, double Tolerance)
	{
		if (Polygon.Num() < 3)
		{
			return false;
		}

		bool bInside = false;
		for (int32 CurrentIndex = 0, PreviousIndex = Polygon.Num() - 1;
			CurrentIndex < Polygon.Num();
			PreviousIndex = CurrentIndex++)
		{
			const FVector2d& Current = Polygon[CurrentIndex];
			const FVector2d& Previous = Polygon[PreviousIndex];
			if (IsPointOnSegment2D(Point, Previous, Current, Tolerance))
			{
				return true;
			}

			const bool bCrosses = (Current.Y > Point.Y) != (Previous.Y > Point.Y);
			if (!bCrosses)
			{
				continue;
			}

			const double IntersectionX =
				(Previous.X - Current.X) * (Point.Y - Current.Y) / (Previous.Y - Current.Y) + Current.X;
			if (Point.X < IntersectionX)
			{
				bInside = !bInside;
			}
		}

		return bInside;
	}

	struct FEHBFloorZRange
	{
		int32 FloorIndex = INDEX_NONE;
		double MinZ = TNumericLimits<double>::Max();
		double MaxZ = -TNumericLimits<double>::Max();

		bool IsValid() const
		{
			return FloorIndex != INDEX_NONE && MinZ <= MaxZ;
		}

		void Include(const FBox& Bounds)
		{
			if (!Bounds.IsValid)
			{
				return;
			}

			MinZ = FMath::Min(MinZ, static_cast<double>(Bounds.Min.Z));
			MaxZ = FMath::Max(MaxZ, static_cast<double>(Bounds.Max.Z));
		}

		bool Contains(double LocalZ, double Tolerance) const
		{
			return IsValid() && LocalZ >= MinZ - Tolerance && LocalZ <= MaxZ + Tolerance;
		}
	};
}

AEHBBuildingActorBase::AEHBBuildingActorBase()
{
	// 建筑对象本身默认不需要逐帧更新；编辑器工具会在用户操作时显式修改数据。
	PrimaryActorTick.bCanEverTick = false;

	// 根组件提供建筑整体变换，后续生成出的组件或可视化辅助对象都应挂在它下面。
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	// 放置范围用于限制墙体、门窗、楼梯等元素的编辑区域。默认给一个足够明显的盒体范围。
	PlacementBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("PlacementBounds"));
	PlacementBounds->SetupAttachment(SceneRoot);
	PlacementBounds->SetBoxExtent(FVector(500.0f, 500.0f, 300.0f));
	PlacementBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlacementBounds->SetHiddenInGame(true);
}

void AEHBBuildingActorBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 构造脚本可能在编辑器中频繁执行，只在 Guid 无效时补齐，不覆盖已有数据。
	EnsureBuildingGuid();
}

void AEHBBuildingActorBase::BeginPlay()
{
 Super::BeginPlay();
 if(auto* Rooms=GetWorld()->GetSubsystem<UEHBRoomRuntimeSubsystem>())Rooms->RegisterBuilding(this);
}

void AEHBBuildingActorBase::PostLoad()
{
	Super::PostLoad();

	// 兼容旧资产：如果旧版本保存的建筑对象没有 Guid，加载时自动生成。
	EnsureBuildingGuid();
	if (RelationshipSchemaVersion < CurrentRelationshipSchemaVersion)
	{
		MigrateLegacyRelationships();
		RelationshipSchemaVersion = CurrentRelationshipSchemaVersion;
	}
	RebuildElementAndRelationshipIndexes();
}

void AEHBBuildingActorBase::PostActorCreated()
{
	Super::PostActorCreated();

	// 编辑器中新建 Actor 后立即分配 Guid，方便后续 UI 或命令系统引用。
	EnsureBuildingGuid();
	RelationshipSchemaVersion = CurrentRelationshipSchemaVersion;
	RebuildElementAndRelationshipIndexes();
}

void AEHBBuildingActorBase::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RebuildElementAndRelationshipIndexes();
  if(auto* World=GetWorld())if(auto* Rooms=World->GetSubsystem<UEHBRoomRuntimeSubsystem>())Rooms->RegisterBuilding(this);
	}
}

void AEHBBuildingActorBase::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (DuplicateMode != EDuplicateMode::PIE && !FEHBActorImportScope::IsActive())
	{
		BuildingGuid = FGuid::NewGuid();
		LastCommittedEdit = FEHBCommittedEdit();
		RelationshipSchemaVersion = CurrentRelationshipSchemaVersion;
		RebuildElementAndRelationshipIndexes();
		MarkPackageDirty();
	}
}

#if WITH_EDITOR
void AEHBBuildingActorBase::PostEditUndo()
{
	Super::PostEditUndo();
	EnsureBuildingGuid();
	RebuildElementAndRelationshipIndexes();
}
#endif

void AEHBBuildingActorBase::EnsureBuildingGuid()
{
	if (!BuildingGuid.IsValid())
	{
		BuildingGuid = FGuid::NewGuid();
	}
}

FBox AEHBBuildingActorBase::GetPlacementBounds() const
{
	if (!PlacementBounds)
	{
		return FBox(ForceInit);
	}

	return FBox::BuildAABB(PlacementBounds->GetComponentLocation(), PlacementBounds->GetScaledBoxExtent());
}

bool AEHBBuildingActorBase::IsLocationInsidePlacementBounds(const FVector& WorldLocation) const
{
	if (!PlacementBounds)
	{
		return false;
	}

	// 将世界坐标转换到范围组件局部空间，再用未缩放盒体范围判断，能正确处理组件旋转和缩放。
	const FVector LocalLocation = PlacementBounds->GetComponentTransform().InverseTransformPosition(WorldLocation);
	const FVector Extent = PlacementBounds->GetUnscaledBoxExtent();

	return FMath::Abs(LocalLocation.X) <= Extent.X
		&& FMath::Abs(LocalLocation.Y) <= Extent.Y
		&& FMath::Abs(LocalLocation.Z) <= Extent.Z;
}

void AEHBBuildingActorBase::RegisterElementToFloor(FGuid ElementGuid, EEHBBuildingElementType ElementType, int32 FloorIndex, EEHBBuildingFloorElementRole FloorRole)
{
	// 楼层索引是 Guid 级别的轻量查询表，同一个元素改楼层时先从旧层移除，避免跨层重复。
	UnregisterElementFromFloors(ElementGuid);

	const bool bValidFoundationAssignment = FloorRole == EEHBBuildingFloorElementRole::Foundation && FloorIndex >= 0;
	const bool bValidRegularAssignment = FloorRole != EEHBBuildingFloorElementRole::None && FloorRole != EEHBBuildingFloorElementRole::Foundation && FloorIndex > 0;
	if (!ElementGuid.IsValid()
		|| ElementType == EEHBBuildingElementType::None
		|| (!bValidFoundationAssignment && !bValidRegularAssignment))
	{
		return;
	}

	FEHBBuildingFloorElementEntry Entry;
	Entry.ElementGuid = ElementGuid;
	Entry.ElementType = ElementType;
	Entry.FloorIndex = FloorIndex;
	Entry.FloorRole = FloorRole;

	FloorElementsByIndex.FindOrAdd(FloorIndex).Elements.Add(Entry);
	MarkPackageDirty();
}

void AEHBBuildingActorBase::UnregisterElementFromFloors(FGuid ElementGuid)
{
	if (!ElementGuid.IsValid())
	{
		return;
	}

	bool bChanged = false;
	TArray<int32> EmptyFloorIndices;
	for (TPair<int32, FEHBBuildingFloorElementList>& Pair : FloorElementsByIndex)
	{
		const int32 RemovedCount = Pair.Value.Elements.RemoveAll(
			[ElementGuid](const FEHBBuildingFloorElementEntry& Entry)
			{
				return Entry.ElementGuid == ElementGuid;
			});
		if (RemovedCount > 0)
		{
			bChanged = true;
		}
		if (Pair.Value.Elements.IsEmpty())
		{
			EmptyFloorIndices.Add(Pair.Key);
		}
	}

	for (const int32 EmptyFloorIndex : EmptyFloorIndices)
	{
		FloorElementsByIndex.Remove(EmptyFloorIndex);
	}

	if (bChanged)
	{
		MarkPackageDirty();
	}
}

TArray<FEHBBuildingFloorElementEntry> AEHBBuildingActorBase::GetFloorElementEntries(int32 FloorIndex) const
{
	if (const FEHBBuildingFloorElementList* FloorList = FloorElementsByIndex.Find(FloorIndex))
	{
		return FloorList->Elements;
	}

	return {};
}

TArray<AEHBElementActorBase*> AEHBBuildingActorBase::GetElementActorsByFloor(int32 FloorIndex) const
{
	TArray<AEHBElementActorBase*> Result;
	const FEHBBuildingFloorElementList* FloorList = FloorElementsByIndex.Find(FloorIndex);
	if (!FloorList)
	{
		return Result;
	}

	for (const FEHBBuildingFloorElementEntry& Entry : FloorList->Elements)
	{
		if (AEHBElementActorBase* ElementActor = FindElementActorByGuid(Entry.ElementGuid))
		{
			Result.Add(ElementActor);
		}
	}

	return Result;
}

int32 AEHBBuildingActorBase::ResolveFloorIndexFromWorldZ(float WorldZ) const
{
	const double ScaleZ = static_cast<double>(GetActorScale3D().Z);
	const double LocalZ = FMath::IsNearlyZero(ScaleZ)
		? static_cast<double>(WorldZ - GetActorLocation().Z)
		: static_cast<double>(WorldZ - GetActorLocation().Z) / ScaleZ;
	return ResolveFloorIndexFromBuildingLocalZ(LocalZ);
}

int32 AEHBBuildingActorBase::ResolveFloorIndexFromWorldLocation(const FVector& WorldLocation) const
{
	const double LocalZ = static_cast<double>(GetActorTransform().InverseTransformPosition(WorldLocation).Z);
	return ResolveFloorIndexFromBuildingLocalZ(LocalZ);
}

int32 AEHBBuildingActorBase::ResolveFloorIndexFromBuildingLocalZ(double LocalZ) const
{
	constexpr double FloorZTolerance = 1.0;

	TMap<int32, FEHBFloorZRange> StructuralRangesByFloor;
	TMap<int32, FEHBFloorZRange> FallbackRangesByFloor;
	for (const TPair<int32, FEHBBuildingFloorElementList>& Pair : FloorElementsByIndex)
	{
		const int32 CandidateFloorIndex = Pair.Key;
		if (CandidateFloorIndex == INDEX_NONE)
		{
			continue;
		}

		for (const FEHBBuildingFloorElementEntry& Entry : Pair.Value.Elements)
		{
			AEHBElementActorBase* ElementActor = FindElementActorByGuid(Entry.ElementGuid);
			if (!ElementActor)
			{
				continue;
			}

			const FBox Bounds = ElementActor->GetBuildingLocalBounds();
			FEHBFloorZRange& FallbackRange = FallbackRangesByFloor.FindOrAdd(CandidateFloorIndex);
			FallbackRange.FloorIndex = CandidateFloorIndex;
			FallbackRange.Include(Bounds);

			if (IsStructuralFloorRangeRole(Entry.FloorRole))
			{
				FEHBFloorZRange& StructuralRange = StructuralRangesByFloor.FindOrAdd(CandidateFloorIndex);
				StructuralRange.FloorIndex = CandidateFloorIndex;
				StructuralRange.Include(Bounds);
			}
		}
	}

	TArray<FEHBFloorZRange> Ranges;
	Ranges.Reserve(FallbackRangesByFloor.Num());
	for (const TPair<int32, FEHBFloorZRange>& Pair : FallbackRangesByFloor)
	{
		const FEHBFloorZRange* StructuralRange = StructuralRangesByFloor.Find(Pair.Key);
		const FEHBFloorZRange& SelectedRange =
			StructuralRange && StructuralRange->IsValid() ? *StructuralRange : Pair.Value;
		if (SelectedRange.IsValid())
		{
			Ranges.Add(SelectedRange);
		}
	}

	Ranges.Sort(
		[](const FEHBFloorZRange& A, const FEHBFloorZRange& B)
		{
			return A.FloorIndex < B.FloorIndex;
		});

	int32 ContainingFloorIndex = INDEX_NONE;
	for (const FEHBFloorZRange& Range : Ranges)
	{
		if (Range.Contains(LocalZ, FloorZTolerance))
		{
			// 楼层边界重叠时将边界高度归给较高楼层，便于顶板上表面查询上层空间。
			ContainingFloorIndex = FMath::Max(ContainingFloorIndex, Range.FloorIndex);
		}
	}

	if (ContainingFloorIndex != INDEX_NONE)
	{
		return ContainingFloorIndex;
	}

	int32 NearestLowerFloorIndex = INDEX_NONE;
	double NearestLowerMinZ = -TNumericLimits<double>::Max();
	int32 NearestUpperFloorIndex = INDEX_NONE;
	double NearestUpperMinZ = TNumericLimits<double>::Max();
	for (const FEHBFloorZRange& Range : Ranges)
	{
		if (Range.MinZ <= LocalZ && Range.MinZ > NearestLowerMinZ)
		{
			NearestLowerMinZ = Range.MinZ;
			NearestLowerFloorIndex = Range.FloorIndex;
		}
		else if (Range.MinZ > LocalZ && Range.MinZ < NearestUpperMinZ)
		{
			NearestUpperMinZ = Range.MinZ;
			NearestUpperFloorIndex = Range.FloorIndex;
		}
	}

	if (NearestLowerFloorIndex != INDEX_NONE)
	{
		return NearestLowerFloorIndex;
	}

	if (NearestUpperFloorIndex != INDEX_NONE)
	{
		return NearestUpperFloorIndex;
	}

	TSet<int32> LoopFloorIndices;
	for (const FEHBBuildingClosedLoop& Loop : ClosedLoops)
	{
		if (Loop.FloorIndex > 0)
		{
			LoopFloorIndices.Add(Loop.FloorIndex);
		}
	}

	if (LoopFloorIndices.Num() == 1)
	{
		for (const int32 LoopFloorIndex : LoopFloorIndices)
		{
			return LoopFloorIndex;
		}
	}

	return INDEX_NONE;
}

void AEHBBuildingActorBase::RebuildElementAndRelationshipIndexes()
{
	if (FEHBActorImportScope::IsActive()) return;
	if (bRebuildingRelationshipIndexes)
	{
		return;
	}

	TGuardValue<bool> RebuildGuard(bRebuildingRelationshipIndexes, true);
	ElementActorByGuid.Reset();
	ElementGeometryRevisionByGuid.Reset();

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		AEHBElementActorBase* ElementActor = Cast<AEHBElementActorBase>(AttachedActor);
		if (!ElementActor || ElementActor->IsActorBeingDestroyed())
		{
			continue;
		}

		ElementActor->OwningBuilding = this;
		ElementActor->EnsureElementGuid();
		if (ElementActorByGuid.Contains(ElementActor->ElementGuid))
		{
			ElementActor->RegenerateElementGuid();
		}
		ElementActorByGuid.Add(ElementActor->ElementGuid, ElementActor);
		ElementGeometryRevisionByGuid.Add(ElementActor->ElementGuid, 0);
	}

	RebuildFloorIndexFromActors();
	for (FEHBElementRelation& Relation : ElementRelations)
	{
		Relation.SourceGeometryRevision =
			Relation.Source.Kind == EEHBRelationEndpointKind::BuildingElement
			? GetElementGeometryRevision(Relation.Source.ElementGuid)
			: 0;
		Relation.TargetGeometryRevision =
			Relation.Target.Kind == EEHBRelationEndpointKind::BuildingElement
			? GetElementGeometryRevision(Relation.Target.ElementGuid)
			: 0;
	}
	RebuildRelationshipIndexes();
	RebuildLegacyTopologyCachesFromRelationships();
	for (const FEHBElementRelation& Relation : ElementRelations)
	{
		if (Relation.Type == EEHBElementRelationType::HostedElement)
		{
			SyncLegacyHostedRelation(Relation);
		}
	}
	RebuildWallNodeAuthorityGeometry();
}

int32 AEHBBuildingActorBase::ClearAllElements()
{
	Modify();

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors, true, true);

	TArray<AEHBElementActorBase*> ElementsToDestroy;
	ElementsToDestroy.Reserve(AttachedActors.Num());
	for (AActor* AttachedActor : AttachedActors)
	{
		AEHBElementActorBase* ElementActor = Cast<AEHBElementActorBase>(AttachedActor);
		if (ElementActor && !ElementActor->IsActorBeingDestroyed())
		{
			ElementsToDestroy.AddUnique(ElementActor);
		}
	}

	const bool bHadRelationshipData =
		ElementRelations.Num() > 0
		|| WallNodeOwnership.Version!=0 || !WallNodeOwnership.Bindings.IsEmpty()
		|| WallNodeAuthority.Version!=0 || !WallNodeAuthority.Nodes.IsEmpty()
		|| PreparedWallNodeDefinitions.Version!=0 || !PreparedWallNodeDefinitions.Nodes.IsEmpty() || !PreparedWallNodeDefinitions.Walls.IsEmpty() || !PreparedWallNodeDefinitions.PillarBindings.IsEmpty()
		|| RelationIndexByGuid.Num() > 0
		|| OutgoingRelationGuidsByElement.Num() > 0
		|| IncomingRelationGuidsByElement.Num() > 0
		|| OutgoingRelationGuidsByNode.Num() > 0
		|| IncomingRelationGuidsByNode.Num() > 0
		|| WallConnectionsByWallGuid.Num() > 0
		|| PillarConnectionsByPillarGuid.Num() > 0
		|| ClosedLoops.Num() > 0
		|| PillarToLoopGuids.Num() > 0
		|| WallToLoopGuids.Num() > 0
		|| FloorElementsByIndex.Num() > 0
		|| ElementActorByGuid.Num() > 0;

	ElementRelations.Reset();
	PreparedWallNodeDefinitions=FEHBPreparedWallNodeDefinitions();
	WallNodeOwnership=FEHBWallNodeOwnership();
	WallNodeAuthority=FEHBWallNodeAuthority();
	RelationIndexByGuid.Reset();
	OutgoingRelationGuidsByElement.Reset();
	IncomingRelationGuidsByElement.Reset();
	OutgoingRelationGuidsByNode.Reset();
	IncomingRelationGuidsByNode.Reset();
	ElementActorByGuid.Reset();
	ElementGeometryRevisionByGuid.Reset();
	FloorElementsByIndex.Reset();
	WallConnectionsByWallGuid.Reset();
	PillarConnectionsByPillarGuid.Reset();
	ClosedLoops.Reset();
	PillarToLoopGuids.Reset();
	WallToLoopGuids.Reset();
	bPendingTopologyCacheRebuild = false;
	bPendingAutomaticFloorResolve = false;

	int32 DestroyedCount = 0;
	{
		TGuardValue<bool> RelationGuard(bUpdatingPillarWallRelations, true);
		for (AEHBElementActorBase* ElementActor : ElementsToDestroy)
		{
			if (!ElementActor || ElementActor->IsActorBeingDestroyed())
			{
				continue;
			}

			ElementActor->Modify();
			if (AEHB_Wall* Wall = Cast<AEHB_Wall>(ElementActor))
			{
				Wall->DoorWindowConnections.Reset();
			}
			ElementActor->OwningBuilding = nullptr;
			if (ElementActor->Destroy())
			{
				++DestroyedCount;
			}
		}
	}

	RebuildElementAndRelationshipIndexes();
	if (DestroyedCount > 0 || bHadRelationshipData)
	{
		++RelationshipGraphRevision;
		MarkPackageDirty();
	}

	return DestroyedCount;
}

bool AEHBBuildingActorBase::QueryRoomDependencies(const TArray<FEHBBuildingClosedLoop>& Rooms,TArray<FEHBRoomDependencyMembers>& Out) const
{
 Out.Reset();TArray<FEHBRoomDependencyBinding> Bindings;
 for(const auto* E:QueryElements(FEHBElementQuery()))
 {
  if(const auto* Floor=Cast<AEHB_Floor>(E))
  {auto& V=Bindings.AddDefaulted_GetRef();V.ElementGuid=Floor->ElementGuid;V.RoomGuid=Floor->RoomLoopGuid;V.Kind=EEHBRoomDependencyKind::Floor;}
  else if(const auto* Slab=Cast<AEHB_FloorSlab>(E))
  {auto& V=Bindings.AddDefaulted_GetRef();V.ElementGuid=Slab->ElementGuid;V.RoomGuid=Slab->RoomFillLoopGuid;V.Kind=EEHBRoomDependencyKind::Slab;if(!V.RoomGuid.IsValid()&&Slab->bHasRoomFillAnchor)V.AnchorWallGuid=Slab->RoomFillAnchorWallGuid;}
 }
 if(!RoomDependencyCache.Update(BuildingGuid,LastCommittedEdit,Bindings,ActiveChangeNotificationBatch!=nullptr))return false;
 for(const auto& Room:Rooms)Out.Add(RoomDependencyCache.Query(Room.LoopGuid,Room.WallGuids));
 return true;
}

void AEHBBuildingActorBase::BeginRelationshipEdit()
{
	++RelationshipEditDepth;
}

void AEHBBuildingActorBase::EndRelationshipEdit(bool bResolveAutomaticFloors)
{
	if (RelationshipEditDepth <= 0)
	{
		RelationshipEditDepth = 0;
		return;
	}

	--RelationshipEditDepth;
	bPendingAutomaticFloorResolve = bPendingAutomaticFloorResolve || bResolveAutomaticFloors;
	if (RelationshipEditDepth > 0)
	{
		return;
	}

	if (bPendingTopologyCacheRebuild)
	{
		bPendingTopologyCacheRebuild = false;
		RebuildLegacyTopologyCachesFromRelationships();
	}
	if (bPendingAutomaticFloorResolve)
	{
		bPendingAutomaticFloorResolve = false;
		ResolveAutomaticFloorAssignments();
	}
}

FGuid AEHBBuildingActorBase::AddOrUpdateElementRelation(
	const FEHBElementRelation& Relation,
	bool bReplaceEquivalent)
{
	FEHBElementRelation Normalized = Relation;
	// Legacy topology writers are adapted at one boundary; other relationship kinds stay physical.
	if(WallNodeOwnership.Version==1&&Normalized.Type==EEHBElementRelationType::TopologyConnection&&Normalized.Target.Kind==EEHBRelationEndpointKind::BuildingElement)
	{
		const FGuid Node=FindNodeForPhysicalPillar(Normalized.Target.ElementGuid);if(!Node.IsValid())return FGuid();
		Normalized.Target.Kind=EEHBRelationEndpointKind::WallNode;Normalized.Target.NodeGuid=Node;Normalized.Target.ElementGuid.Invalidate();
	}
	if((Normalized.Source.Kind==EEHBRelationEndpointKind::WallNode||Normalized.Target.Kind==EEHBRelationEndpointKind::WallNode)
		&&Normalized.Type!=EEHBElementRelationType::TopologyConnection&&Normalized.Type!=EEHBElementRelationType::LogicalDependency)return FGuid();
	const bool bHasNode=Normalized.Source.Kind==EEHBRelationEndpointKind::WallNode||Normalized.Target.Kind==EEHBRelationEndpointKind::WallNode;
	if(bHasNode&&Normalized.Type==EEHBElementRelationType::TopologyConnection
		&&(Normalized.Source.Kind!=EEHBRelationEndpointKind::BuildingElement||Normalized.Target.Kind!=EEHBRelationEndpointKind::WallNode
			||!Cast<AEHB_Wall>(FindElementActorByGuid(Normalized.Source.ElementGuid))
			||(Normalized.Source.SurfaceKind!=EEHBElementSurfaceKind::Start&&Normalized.Source.SurfaceKind!=EEHBElementSurfaceKind::End)))return FGuid();
	if(Normalized.Source.Kind==EEHBRelationEndpointKind::WallNode&&Normalized.Target.Kind==EEHBRelationEndpointKind::WallNode&&Normalized.Source.NodeGuid==Normalized.Target.NodeGuid)return FGuid();
	if (Normalized.Type == EEHBElementRelationType::None
		|| !Normalized.Source.IsValid()
		|| !Normalized.Target.IsValid()
		|| !IsRelationEndpointOwnedByThisBuilding(Normalized.Source)
		|| !IsRelationEndpointOwnedByThisBuilding(Normalized.Target))
	{
		return FGuid();
	}

	if (Normalized.Source.Kind == EEHBRelationEndpointKind::BuildingElement
		&& Normalized.Target.Kind == EEHBRelationEndpointKind::BuildingElement
		&& Normalized.Source.ElementGuid == Normalized.Target.ElementGuid)
	{
		return FGuid();
	}

	if (Normalized.Type == EEHBElementRelationType::StructuralSupport)
	{
		if (Normalized.Source.Kind == EEHBRelationEndpointKind::BuildingElement)
		{
			const AEHBElementActorBase* SourceElement = FindElementActorByGuid(Normalized.Source.ElementGuid);
			if (!SourceElement
				|| !SourceElement->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::CanSupport))
				|| SourceElement->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::SurfaceFinish)))
			{
				return FGuid();
			}
		}

		if (Normalized.Target.Kind == EEHBRelationEndpointKind::BuildingElement)
		{
			const AEHBElementActorBase* TargetElement = FindElementActorByGuid(Normalized.Target.ElementGuid);
			if (!TargetElement
				|| TargetElement->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::SurfaceFinish)))
			{
				return FGuid();
			}
		}
	}

	const int32 ExistingEquivalentIndex = bReplaceEquivalent ? FindEquivalentRelationIndex(Normalized) : INDEX_NONE;
	if (Normalized.Type == EEHBElementRelationType::StructuralSupport
		&& ExistingEquivalentIndex == INDEX_NONE
		&& Normalized.Source.Kind == EEHBRelationEndpointKind::BuildingElement
		&& Normalized.Target.Kind == EEHBRelationEndpointKind::BuildingElement
		&& !FindElementRelationPath(
			Normalized.Target.ElementGuid,
			Normalized.Source.ElementGuid,
			EEHBRelationQueryDirection::Outgoing,
			{ EEHBElementRelationType::StructuralSupport },
			FMath::Max(1, ElementActorByGuid.Num())).IsEmpty())
	{
		return FGuid();
	}

	Normalized.SourceGeometryRevision =
		Normalized.Source.Kind == EEHBRelationEndpointKind::BuildingElement
		? GetElementGeometryRevision(Normalized.Source.ElementGuid)
		: Normalized.Source.Kind==EEHBRelationEndpointKind::WallNode?GetElementGeometryRevision(FindPhysicalPillarForNode(Normalized.Source.NodeGuid)):0;
	Normalized.TargetGeometryRevision =
		Normalized.Target.Kind == EEHBRelationEndpointKind::BuildingElement
		? GetElementGeometryRevision(Normalized.Target.ElementGuid)
		: Normalized.Target.Kind==EEHBRelationEndpointKind::WallNode?GetElementGeometryRevision(FindPhysicalPillarForNode(Normalized.Target.NodeGuid)):0;

	Modify();
	const int32 EquivalentIndex = ExistingEquivalentIndex;
	if (EquivalentIndex != INDEX_NONE)
	{
		FEHBElementRelation& Existing = ElementRelations[EquivalentIndex];
		const FGuid ExistingGuid = Existing.RelationGuid;
		RemoveRelationFromIndexes(Existing);
		Normalized.RelationGuid = ExistingGuid;
		Existing = MoveTemp(Normalized);
		AddRelationToIndexes(Existing, EquivalentIndex);
		++RelationshipGraphRevision;
		MarkPackageDirty();
		if(ActiveChangeNotificationBatch)ActiveChangeNotificationBatch->ChangedRelations.Add(ExistingGuid);
		else OnElementRelationAdded.Broadcast(ExistingGuid);
		if (Existing.Type == EEHBElementRelationType::TopologyConnection)
		{
			if (RelationshipEditDepth > 0)
			{
				bPendingTopologyCacheRebuild = true;
			}
			else
			{
				RebuildLegacyTopologyCachesFromRelationships();
			}
		}
		else if (Existing.Type == EEHBElementRelationType::HostedElement)
		{
			SyncLegacyHostedRelation(Existing);
		}
		return ExistingGuid;
	}

	if (!Normalized.RelationGuid.IsValid() || RelationIndexByGuid.Contains(Normalized.RelationGuid))
	{
		Normalized.RelationGuid = FGuid::NewGuid();
	}

	const int32 NewIndex = ElementRelations.Add(MoveTemp(Normalized));
	AddRelationToIndexes(ElementRelations[NewIndex], NewIndex);
	++RelationshipGraphRevision;
	MarkPackageDirty();
	if(ActiveChangeNotificationBatch)ActiveChangeNotificationBatch->ChangedRelations.Add(ElementRelations[NewIndex].RelationGuid);
	else OnElementRelationAdded.Broadcast(ElementRelations[NewIndex].RelationGuid);
	if (ElementRelations[NewIndex].Type == EEHBElementRelationType::TopologyConnection)
	{
		if (RelationshipEditDepth > 0)
		{
			bPendingTopologyCacheRebuild = true;
		}
		else
		{
			RebuildLegacyTopologyCachesFromRelationships();
		}
	}
	else if (ElementRelations[NewIndex].Type == EEHBElementRelationType::HostedElement)
	{
		SyncLegacyHostedRelation(ElementRelations[NewIndex]);
	}
	return ElementRelations[NewIndex].RelationGuid;
}

bool AEHBBuildingActorBase::RemoveElementRelation(FGuid RelationGuid)
{
 if (!RelationGuid.IsValid()) return false;
 const int32* FoundIndex = RelationIndexByGuid.Find(RelationGuid);
 // Serialized arrays can be restored before dependent actor undo callbacks run.
 // A valid array slot alone is insufficient: it may now name a different relation.
 if (!FoundIndex || !ElementRelations.IsValidIndex(*FoundIndex)
  || ElementRelations[*FoundIndex].RelationGuid != RelationGuid)
 {
  RebuildRelationshipIndexes();
  FoundIndex = RelationIndexByGuid.Find(RelationGuid);
 }
 if (!FoundIndex || !ElementRelations.IsValidIndex(*FoundIndex)
  || ElementRelations[*FoundIndex].RelationGuid != RelationGuid) return false;

	Modify();
	const int32 RemoveIndex = *FoundIndex;
	const FEHBElementRelation RemovedRelation = ElementRelations[RemoveIndex];
	RemoveRelationFromIndexes(RemovedRelation);
	ElementRelations.RemoveAtSwap(RemoveIndex);
	if (ElementRelations.IsValidIndex(RemoveIndex))
	{
		RelationIndexByGuid.Add(ElementRelations[RemoveIndex].RelationGuid, RemoveIndex);
	}

	++RelationshipGraphRevision;
	MarkPackageDirty();
	if(ActiveChangeNotificationBatch)ActiveChangeNotificationBatch->ChangedRelations.Add(RelationGuid);
	else OnElementRelationRemoved.Broadcast(RelationGuid);
	if (RemovedRelation.Type == EEHBElementRelationType::TopologyConnection)
	{
		if (RelationshipEditDepth > 0)
		{
			bPendingTopologyCacheRebuild = true;
		}
		else
		{
			RebuildLegacyTopologyCachesFromRelationships();
		}
	}
	else if (RemovedRelation.Type == EEHBElementRelationType::HostedElement
		&& RemovedRelation.Source.Kind == EEHBRelationEndpointKind::BuildingElement
		&& RemovedRelation.Target.Kind == EEHBRelationEndpointKind::BuildingElement)
	{
		AEHB_Wall* HostWall = Cast<AEHB_Wall>(FindElementActorByGuid(RemovedRelation.Source.ElementGuid));
		AEHB_DoorWindow* HostedDoorWindow =
			Cast<AEHB_DoorWindow>(FindElementActorByGuid(RemovedRelation.Target.ElementGuid));
		if (HostedDoorWindow && HostedDoorWindow->OwningWallGuid == RemovedRelation.Source.ElementGuid)
		{
			HostedDoorWindow->Modify();
			HostedDoorWindow->OwningWallGuid.Invalidate();
			HostedDoorWindow->DistanceFromWallStart = 0.0f;
		}
		if (HostWall && !HostWall->IsActorBeingDestroyed())
		{
			HostWall->Modify();
			HostWall->RemoveDoorWindowConnectionByGuid(RemovedRelation.Target.ElementGuid);
			HostWall->RebuildWallMesh();
		}
	}
	return true;
}

int32 AEHBBuildingActorBase::RemoveAllRelationsForElement(FGuid ElementGuid)
{
	if (!ElementGuid.IsValid())
	{
		return 0;
	}

	TSet<FGuid> RelationGuids;
	TArray<FGuid> IndexedGuids;
	OutgoingRelationGuidsByElement.MultiFind(ElementGuid, IndexedGuids);
	RelationGuids.Append(IndexedGuids);
	IndexedGuids.Reset();
	IncomingRelationGuidsByElement.MultiFind(ElementGuid, IndexedGuids);
	RelationGuids.Append(IndexedGuids);

	int32 RemovedCount = 0;
	for (const FGuid& RelationGuid : RelationGuids)
	{
		RemovedCount += RemoveElementRelation(RelationGuid) ? 1 : 0;
	}
	return RemovedCount;
}

int32 AEHBBuildingActorBase::RemoveRelationsBetweenElements(
	FGuid FirstElementGuid,
	FGuid SecondElementGuid,
	EEHBElementRelationType RelationType)
{
	if (!FirstElementGuid.IsValid() || !SecondElementGuid.IsValid())
	{
		return 0;
	}

	TArray<FGuid> ToRemove;
	for (const FEHBElementRelation& Relation : ElementRelations)
	{
		const bool bMatchesPair =
			(Relation.Source.RefersToElement(FirstElementGuid) && Relation.Target.RefersToElement(SecondElementGuid))
			|| (Relation.Source.RefersToElement(SecondElementGuid) && Relation.Target.RefersToElement(FirstElementGuid))
			|| (Relation.Type==EEHBElementRelationType::TopologyConnection
				&&((Relation.Source.RefersToElement(FirstElementGuid)&&ResolveTopologyPillar(Relation.Target)==SecondElementGuid)
				||(Relation.Source.RefersToElement(SecondElementGuid)&&ResolveTopologyPillar(Relation.Target)==FirstElementGuid)));
		if (bMatchesPair
			&& (RelationType == EEHBElementRelationType::None || Relation.Type == RelationType))
		{
			ToRemove.Add(Relation.RelationGuid);
		}
	}

	int32 RemovedCount = 0;
	for (const FGuid& RelationGuid : ToRemove)
	{
		RemovedCount += RemoveElementRelation(RelationGuid) ? 1 : 0;
	}
	return RemovedCount;
}

FGuid AEHBBuildingActorBase::SetStructuralSupportRelation(
	AEHBElementActorBase* Supporter,
	AEHBElementActorBase* SupportedElement,
	EEHBElementSurfaceKind SupportSurface,
	EEHBElementSurfaceKind SupportedSurface,
	const FVector& ContactPoint,
	const FVector& ContactNormal,
	float ContactArea,
	EEHBRelationOrigin Origin)
{
	if (!Supporter
		|| !SupportedElement
		|| Supporter == SupportedElement
		|| Supporter->OwningBuilding != this
		|| SupportedElement->OwningBuilding != this
		|| !Supporter->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::CanSupport)))
	{
		return FGuid();
	}

	FEHBElementRelation Relation;
	Relation.Type = EEHBElementRelationType::StructuralSupport;
	Relation.Source = FEHBElementRelationEndpoint::MakeElement(
		Supporter->ElementGuid,
		SupportSurface);
	Relation.Target = FEHBElementRelationEndpoint::MakeElement(
		SupportedElement->ElementGuid,
		SupportedSurface);
	Relation.Origin = Origin;
	Relation.bGeometryDependent = true;
	Relation.bAffectsFloorAssignment = true;
	Relation.ContactPoint = ContactPoint;
	Relation.ContactNormal = ContactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	Relation.ContactArea = FMath::Max(0.0f, ContactArea);
	Relation.TargetRelativeToSource =
		SupportedElement->GetActorTransform().GetRelativeTransform(Supporter->GetActorTransform());

	const FGuid RelationGuid = AddOrUpdateElementRelation(Relation, true);
	if (RelationGuid.IsValid())
	{
		if (RelationshipEditDepth > 0)
		{
			bPendingAutomaticFloorResolve = true;
		}
		else
		{
			ResolveAutomaticFloorAssignments();
		}
	}
	return RelationGuid;
}

FGuid AEHBBuildingActorBase::SetExternalStructuralSupportRelation(
	AActor* ExternalActor,
	AEHBElementActorBase* SupportedElement,
	bool bUseWorldGround,
	EEHBElementSurfaceKind SupportedSurface,
	const FVector& ContactPoint,
	const FVector& ContactNormal,
	float ContactArea,
	EEHBRelationOrigin Origin)
{
	if (!SupportedElement
		|| SupportedElement->OwningBuilding != this
		|| (!bUseWorldGround && !ExternalActor))
	{
		return FGuid();
	}

	FEHBElementRelation Relation;
	Relation.Type = EEHBElementRelationType::StructuralSupport;
	Relation.Source.Kind = bUseWorldGround
		? EEHBRelationEndpointKind::WorldGround
		: EEHBRelationEndpointKind::ExternalActor;
	Relation.Source.ExternalActor = bUseWorldGround ? nullptr : ExternalActor;
	Relation.Source.SurfaceKind = EEHBElementSurfaceKind::Top;
	Relation.Target = FEHBElementRelationEndpoint::MakeElement(
		SupportedElement->ElementGuid,
		SupportedSurface);
	Relation.Origin = Origin;
	Relation.bGeometryDependent = !bUseWorldGround;
	Relation.bAffectsFloorAssignment = true;
	Relation.ContactPoint = ContactPoint;
	Relation.ContactNormal = ContactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	Relation.ContactArea = FMath::Max(0.0f, ContactArea);

	const FGuid RelationGuid = AddOrUpdateElementRelation(Relation, true);
	if (RelationGuid.IsValid())
	{
		if (RelationshipEditDepth > 0)
		{
			bPendingAutomaticFloorResolve = true;
		}
		else
		{
			ResolveAutomaticFloorAssignments();
		}
	}
	return RelationGuid;
}

TArray<AEHBElementActorBase*> AEHBBuildingActorBase::GetElementSupporters(FGuid ElementGuid) const
{
	return GetRelatedElementActors(
		ElementGuid,
		EEHBRelationQueryDirection::Incoming,
		{ EEHBElementRelationType::StructuralSupport },
		false,
		1);
}

TArray<AEHBElementActorBase*> AEHBBuildingActorBase::GetElementsSupportedBy(
	FGuid ElementGuid,
	bool bRecursive,
	int32 MaxDepth) const
{
	return GetRelatedElementActors(
		ElementGuid,
		EEHBRelationQueryDirection::Outgoing,
		{ EEHBElementRelationType::StructuralSupport },
		bRecursive,
		MaxDepth);
}

TArray<AEHBElementActorBase*> AEHBBuildingActorBase::GetElementsAffectedBy(
	FGuid ElementGuid,
	bool bRecursive,
	int32 MaxDepth) const
{
	return GetRelatedElementActors(
		ElementGuid,
		EEHBRelationQueryDirection::Outgoing,
		{
			EEHBElementRelationType::StructuralSupport,
			EEHBElementRelationType::HostedElement,
			EEHBElementRelationType::SurfaceFinish,
			EEHBElementRelationType::BoundaryAttachment,
			EEHBElementRelationType::LogicalDependency
		},
		bRecursive,
		MaxDepth);
}

TArray<FEHBElementRelation> AEHBBuildingActorBase::QueryElementRelations(const FEHBRelationQuery& Query) const
{
	TArray<FEHBElementRelation> Result;
	TSet<FGuid> CandidateGuids;

	if(Query.ElementGuid.IsValid()&&Query.NodeGuid.IsValid())return Result;
	if (!Query.ElementGuid.IsValid()&&!Query.NodeGuid.IsValid())
	{
		for (const FEHBElementRelation& Relation : ElementRelations)
		{
			CandidateGuids.Add(Relation.RelationGuid);
		}
	}
	else
	{
		TArray<FGuid> IndexedGuids;
		if (Query.Direction == EEHBRelationQueryDirection::Outgoing
			|| Query.Direction == EEHBRelationQueryDirection::Both)
		{
			if(Query.NodeGuid.IsValid())OutgoingRelationGuidsByNode.MultiFind(Query.NodeGuid,IndexedGuids);
			else OutgoingRelationGuidsByElement.MultiFind(Query.ElementGuid, IndexedGuids);
			CandidateGuids.Append(IndexedGuids);
		}
		IndexedGuids.Reset();
		if (Query.Direction == EEHBRelationQueryDirection::Incoming
			|| Query.Direction == EEHBRelationQueryDirection::Both)
		{
			if(Query.NodeGuid.IsValid())IncomingRelationGuidsByNode.MultiFind(Query.NodeGuid,IndexedGuids);
			else IncomingRelationGuidsByElement.MultiFind(Query.ElementGuid, IndexedGuids);
			CandidateGuids.Append(IndexedGuids);
		}
	}

	for (const FGuid& RelationGuid : CandidateGuids)
	{
		const int32* RelationIndex = RelationIndexByGuid.Find(RelationGuid);
		if (!RelationIndex || !ElementRelations.IsValidIndex(*RelationIndex))
		{
			continue;
		}

		const FEHBElementRelation& Relation = ElementRelations[*RelationIndex];
		if (!Query.bIncludeDisabled && !Relation.bEnabled)
		{
			continue;
		}
		if (!IsRelationTypeInFilter(Relation.Type, Query.Types))
		{
			continue;
		}
		if (!Query.Origins.IsEmpty() && !Query.Origins.Contains(Relation.Origin))
		{
			continue;
		}
		if (!Query.SourceSurfaceKinds.IsEmpty()
			&& !Query.SourceSurfaceKinds.Contains(Relation.Source.SurfaceKind))
		{
			continue;
		}
		if (!Query.TargetSurfaceKinds.IsEmpty()
			&& !Query.TargetSurfaceKinds.Contains(Relation.Target.SurfaceKind))
		{
			continue;
		}
		if (!Query.SourceSurfaceName.IsNone()
			&& Relation.Source.SurfaceName != Query.SourceSurfaceName)
		{
			continue;
		}
		if (!Query.TargetSurfaceName.IsNone()
			&& Relation.Target.SurfaceName != Query.TargetSurfaceName)
		{
			continue;
		}
		if (Query.bGeometryDependentOnly && !Relation.bGeometryDependent)
		{
			continue;
		}
		if (!Query.bIncludeStale && IsElementRelationStale(Relation.RelationGuid))
		{
			continue;
		}
		Result.Add(Relation);
	}

	return Result;
}

TArray<FGuid> AEHBBuildingActorBase::GetRelatedElementGuids(
	FGuid ElementGuid,
	EEHBRelationQueryDirection Direction,
	const TArray<EEHBElementRelationType>& RelationTypes,
	bool bRecursive,
	int32 MaxDepth) const
{
	TArray<FGuid> Result;
	if (!ElementGuid.IsValid())
	{
		return Result;
	}

	const int32 EffectiveMaxDepth = bRecursive ? FMath::Max(1, MaxDepth) : 1;
	TSet<FGuid> Visited;
	Visited.Add(ElementGuid);

	TQueue<TPair<FGuid, int32>> Queue;
	Queue.Enqueue(TPair<FGuid, int32>(ElementGuid, 0));

	TPair<FGuid, int32> Current;
	while (Queue.Dequeue(Current))
	{
		if (Current.Value >= EffectiveMaxDepth)
		{
			continue;
		}

		FEHBRelationQuery Query;
		Query.ElementGuid = Current.Key;
		Query.Direction = Direction;
		Query.Types = RelationTypes;
		Query.bIncludeStale = true;

		for (const FEHBElementRelation& Relation : QueryElementRelations(Query))
		{
			FGuid OtherGuid;
			if (!ShouldTraverseRelation(Relation, Current.Key, Direction, RelationTypes, OtherGuid)
				|| !OtherGuid.IsValid()
				|| Visited.Contains(OtherGuid))
			{
				continue;
			}

			Visited.Add(OtherGuid);
			Result.Add(OtherGuid);
			if (bRecursive)
			{
				Queue.Enqueue(TPair<FGuid, int32>(OtherGuid, Current.Value + 1));
			}
		}
	}

	return Result;
}

TArray<AEHBElementActorBase*> AEHBBuildingActorBase::GetRelatedElementActors(
	FGuid ElementGuid,
	EEHBRelationQueryDirection Direction,
	const TArray<EEHBElementRelationType>& RelationTypes,
	bool bRecursive,
	int32 MaxDepth) const
{
	TArray<AEHBElementActorBase*> Result;
	for (const FGuid& RelatedGuid : GetRelatedElementGuids(
		ElementGuid,
		Direction,
		RelationTypes,
		bRecursive,
		MaxDepth))
	{
		if (AEHBElementActorBase* RelatedActor = FindElementActorByGuid(RelatedGuid))
		{
			Result.Add(RelatedActor);
		}
	}
	return Result;
}

TArray<FGuid> AEHBBuildingActorBase::FindElementRelationPath(
	FGuid StartElementGuid,
	FGuid EndElementGuid,
	EEHBRelationQueryDirection Direction,
	const TArray<EEHBElementRelationType>& RelationTypes,
	int32 MaxDepth) const
{
	if (!StartElementGuid.IsValid() || !EndElementGuid.IsValid())
	{
		return {};
	}
	if (StartElementGuid == EndElementGuid)
	{
		return { StartElementGuid };
	}

	TQueue<TPair<FGuid, int32>> Queue;
	TSet<FGuid> Visited;
	TMap<FGuid, FGuid> Previous;
	Queue.Enqueue(TPair<FGuid, int32>(StartElementGuid, 0));
	Visited.Add(StartElementGuid);

	TPair<FGuid, int32> Current;
	while (Queue.Dequeue(Current))
	{
		if (Current.Value >= FMath::Max(1, MaxDepth))
		{
			continue;
		}

		for (const FGuid& NextGuid : GetRelatedElementGuids(
			Current.Key,
			Direction,
			RelationTypes,
			false,
			1))
		{
			if (Visited.Contains(NextGuid))
			{
				continue;
			}
			Visited.Add(NextGuid);
			Previous.Add(NextGuid, Current.Key);
			if (NextGuid == EndElementGuid)
			{
				TArray<FGuid> Path;
				FGuid Cursor = EndElementGuid;
				Path.Add(Cursor);
				while (Cursor != StartElementGuid)
				{
					const FGuid* PreviousGuid = Previous.Find(Cursor);
					if (!PreviousGuid)
					{
						return {};
					}
					Cursor = *PreviousGuid;
					Path.Add(Cursor);
				}
				Algo::Reverse(Path);
				return Path;
			}
			Queue.Enqueue(TPair<FGuid, int32>(NextGuid, Current.Value + 1));
		}
	}

	return {};
}

TArray<AEHBElementActorBase*> AEHBBuildingActorBase::QueryElements(const FEHBElementQuery& Query) const
{
	TArray<AEHBElementActorBase*> Result;
	for (const TPair<FGuid, TWeakObjectPtr<AEHBElementActorBase>>& Pair : ElementActorByGuid)
	{
		AEHBElementActorBase* Element = Pair.Value.Get();
		if (!Element || Element->IsActorBeingDestroyed())
		{
			continue;
		}
		if (Query.FloorIndex != INDEX_NONE && Element->FloorIndex != Query.FloorIndex)
		{
			continue;
		}
		if (!Query.ElementTypes.IsEmpty() && !Query.ElementTypes.Contains(Element->ElementType))
		{
			continue;
		}
		if (!Query.FloorRoles.IsEmpty() && !Query.FloorRoles.Contains(Element->FloorRole))
		{
			continue;
		}
		if (!Element->HasAllCapabilities(Query.RequiredCapabilities)
			|| (Element->ElementCapabilities & Query.ExcludedCapabilities) != 0)
		{
			continue;
		}

		bool bHasAllTags = true;
		for (const FName RequiredTag : Query.RequiredSemanticTags)
		{
			if (!Element->SemanticTags.Contains(RequiredTag))
			{
				bHasAllTags = false;
				break;
			}
		}
		if (bHasAllTags)
		{
			Result.Add(Element);
		}
	}

	Result.Sort([](const AEHBElementActorBase& A, const AEHBElementActorBase& B)
	{
		if (A.FloorIndex != B.FloorIndex)
		{
			return A.FloorIndex < B.FloorIndex;
		}
		return A.ElementGuid < B.ElementGuid;
	});
	return Result;
}

TArray<AEHBElementActorBase*> AEHBBuildingActorBase::QueryElementsWithoutRelation(
	const FEHBElementQuery& ElementQuery,
	EEHBElementRelationType RelationType,
	EEHBRelationQueryDirection Direction) const
{
	TArray<AEHBElementActorBase*> Result;
	for (AEHBElementActorBase* Element : QueryElements(ElementQuery))
	{
		if (!Element)
		{
			continue;
		}

		FEHBRelationQuery RelationQuery;
		RelationQuery.ElementGuid = Element->ElementGuid;
		RelationQuery.Direction = Direction;
		RelationQuery.Types = { RelationType };
		RelationQuery.bIncludeStale = false;
		if (QueryElementRelations(RelationQuery).IsEmpty())
		{
			Result.Add(Element);
		}
	}
	return Result;
}

bool AEHBBuildingActorBase::IsElementRelationStale(FGuid RelationGuid) const
{
	const int32* RelationIndex = RelationIndexByGuid.Find(RelationGuid);
	if (!RelationIndex || !ElementRelations.IsValidIndex(*RelationIndex))
	{
		return true;
	}

	const FEHBElementRelation& Relation = ElementRelations[*RelationIndex];
	if (!Relation.bGeometryDependent)
	{
		return false;
	}

	const bool bSourceStale =
		Relation.Source.Kind == EEHBRelationEndpointKind::BuildingElement
		&& Relation.SourceGeometryRevision != GetElementGeometryRevision(Relation.Source.ElementGuid);
	const bool bTargetStale =
		Relation.Target.Kind == EEHBRelationEndpointKind::BuildingElement
		&& Relation.TargetGeometryRevision != GetElementGeometryRevision(Relation.Target.ElementGuid);
	return bSourceStale || bTargetStale
		|| (Relation.Source.Kind==EEHBRelationEndpointKind::WallNode&&Relation.SourceGeometryRevision!=GetElementGeometryRevision(FindPhysicalPillarForNode(Relation.Source.NodeGuid)))
		|| (Relation.Target.Kind==EEHBRelationEndpointKind::WallNode&&Relation.TargetGeometryRevision!=GetElementGeometryRevision(FindPhysicalPillarForNode(Relation.Target.NodeGuid)));
}

TArray<FEHBRelationValidationIssue> AEHBBuildingActorBase::ValidateElementRelationshipGraph(
	bool bRepairInvalidRelations,
	bool bRemoveStaleAutoRelations)
{
	TArray<FEHBRelationValidationIssue> Issues;
	TSet<FGuid> SeenRelationGuids;
	TSet<FString> SeenEquivalentKeys;

	for (int32 Index = ElementRelations.Num() - 1; Index >= 0; --Index)
	{
		FEHBElementRelation& Relation = ElementRelations[Index];
		bool bRemove = false;
		auto AddIssue = [&Issues, &Relation](EEHBRelationValidationSeverity Severity, const FString& Message)
		{
			FEHBRelationValidationIssue& Issue = Issues.AddDefaulted_GetRef();
			Issue.Severity = Severity;
			Issue.RelationGuid = Relation.RelationGuid;
			Issue.Message = Message;
		};

		if (!Relation.RelationGuid.IsValid() || SeenRelationGuids.Contains(Relation.RelationGuid))
		{
			AddIssue(EEHBRelationValidationSeverity::Error, TEXT("Relation has an invalid or duplicate Guid."));
			if (bRepairInvalidRelations)
			{
				Relation.RelationGuid = FGuid::NewGuid();
			}
		}
		SeenRelationGuids.Add(Relation.RelationGuid);

		if (Relation.Type == EEHBElementRelationType::None
			|| !Relation.Source.IsValid()
			|| !Relation.Target.IsValid()
			|| !IsRelationEndpointOwnedByThisBuilding(Relation.Source)
			|| !IsRelationEndpointOwnedByThisBuilding(Relation.Target))
		{
			AddIssue(EEHBRelationValidationSeverity::Error, TEXT("Relation has an invalid or foreign endpoint."));
			bRemove = bRepairInvalidRelations;
		}
		else if (Relation.Source.Kind == EEHBRelationEndpointKind::BuildingElement
			&& Relation.Target.Kind == EEHBRelationEndpointKind::BuildingElement
			&& Relation.Source.ElementGuid == Relation.Target.ElementGuid)
		{
			AddIssue(EEHBRelationValidationSeverity::Error, TEXT("Self relations are not allowed."));
			bRemove = bRepairInvalidRelations;
		}

		if (!bRemove && Relation.Type == EEHBElementRelationType::StructuralSupport)
		{
			const AEHBElementActorBase* SourceElement = Relation.Source.Kind == EEHBRelationEndpointKind::BuildingElement
				? FindElementActorByGuid(Relation.Source.ElementGuid)
				: nullptr;
			const AEHBElementActorBase* TargetElement = Relation.Target.Kind == EEHBRelationEndpointKind::BuildingElement
				? FindElementActorByGuid(Relation.Target.ElementGuid)
				: nullptr;
			const bool bInvalidSource =
				Relation.Source.Kind == EEHBRelationEndpointKind::BuildingElement
				&& (!SourceElement
					|| !SourceElement->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::CanSupport))
					|| SourceElement->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::SurfaceFinish)));
			const bool bInvalidTarget =
				Relation.Target.Kind == EEHBRelationEndpointKind::BuildingElement
				&& (!TargetElement
					|| TargetElement->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::SurfaceFinish)));
			if (bInvalidSource || bInvalidTarget)
			{
				AddIssue(EEHBRelationValidationSeverity::Error, TEXT("Structural support relation uses a non-structural or finish endpoint."));
				bRemove = bRepairInvalidRelations;
			}
		}

		auto EndpointKey=[](const FEHBElementRelationEndpoint& Endpoint)
		{
			return FString::Printf(TEXT("%d|%s|%s|%s|%d|%s|%d"),static_cast<int32>(Endpoint.Kind),*Endpoint.ElementGuid.ToString(),*Endpoint.NodeGuid.ToString(),*Endpoint.ExternalActor.ToSoftObjectPath().ToString(),static_cast<int32>(Endpoint.SurfaceKind),*Endpoint.SurfaceName.ToString(),Endpoint.SubIndex);
		};
		const FString EquivalentKey=FString::FromInt(static_cast<int32>(Relation.Type))+TEXT("|")+EndpointKey(Relation.Source)+TEXT("|")+EndpointKey(Relation.Target);
		if((Relation.Source.Kind==EEHBRelationEndpointKind::WallNode||Relation.Target.Kind==EEHBRelationEndpointKind::WallNode)
			&&Relation.Type!=EEHBElementRelationType::TopologyConnection&&Relation.Type!=EEHBElementRelationType::LogicalDependency)
		{AddIssue(EEHBRelationValidationSeverity::Error,TEXT("Connection nodes cannot act as physical support or surface hosts."));bRemove=bRepairInvalidRelations;}

		if (SeenEquivalentKeys.Contains(EquivalentKey))
		{
			AddIssue(EEHBRelationValidationSeverity::Warning, TEXT("Equivalent duplicate relation found."));
			bRemove = bRepairInvalidRelations;
		}
		SeenEquivalentKeys.Add(EquivalentKey);

		if (Relation.Origin == EEHBRelationOrigin::AutoDetected && IsElementRelationStale(Relation.RelationGuid))
		{
			AddIssue(EEHBRelationValidationSeverity::Warning, TEXT("Auto-detected geometry relation is stale."));
			bRemove = bRemove || bRemoveStaleAutoRelations;
		}

		if (bRemove)
		{
			ElementRelations.RemoveAt(Index);
		}
	}

	TMultiMap<FGuid, FGuid> StructuralAdjacency;
	for (const FEHBElementRelation& Relation : ElementRelations)
	{
		if (Relation.bEnabled
			&& Relation.Type == EEHBElementRelationType::StructuralSupport
			&& Relation.Source.Kind == EEHBRelationEndpointKind::BuildingElement
			&& Relation.Target.Kind == EEHBRelationEndpointKind::BuildingElement)
		{
			StructuralAdjacency.Add(Relation.Source.ElementGuid, Relation.Target.ElementGuid);
		}
	}

	TMap<FGuid, uint8> VisitState;
	TFunction<bool(const FGuid&)> HasCycleFrom = [&](const FGuid& ElementGuid)
	{
		uint8& State = VisitState.FindOrAdd(ElementGuid);
		if (State == 1)
		{
			return true;
		}
		if (State == 2)
		{
			return false;
		}

		State = 1;
		TArray<FGuid> Children;
		StructuralAdjacency.MultiFind(ElementGuid, Children);
		for (const FGuid& ChildGuid : Children)
		{
			if (HasCycleFrom(ChildGuid))
			{
				return true;
			}
		}
		State = 2;
		return false;
	};

	for (const TPair<FGuid, TWeakObjectPtr<AEHBElementActorBase>>& Pair : ElementActorByGuid)
	{
		if (HasCycleFrom(Pair.Key))
		{
			FEHBRelationValidationIssue& Issue = Issues.AddDefaulted_GetRef();
			Issue.Severity = EEHBRelationValidationSeverity::Error;
			Issue.Message = TEXT("Structural support graph contains a cycle.");
			break;
		}
	}

	if (bRepairInvalidRelations || bRemoveStaleAutoRelations)
	{
		++RelationshipGraphRevision;
		MarkPackageDirty();
	}
	RebuildRelationshipIndexes();
	return Issues;
}

void AEHBBuildingActorBase::NotifyElementGeometryChanged(FGuid ElementGuid, bool bFinished)
{
	if (!ElementGuid.IsValid())
	{
		return;
	}
	if (bFinished)
	{
		++ElementGeometryRevisionByGuid.FindOrAdd(ElementGuid);
	}
	if(ActiveChangeNotificationBatch)ActiveChangeNotificationBatch->ChangedGeometry.FindOrAdd(ElementGuid) |= bFinished;
	else OnElementGeometryChanged.Broadcast(ElementGuid, bFinished);
}

int32 AEHBBuildingActorBase::GetElementGeometryRevision(FGuid ElementGuid) const
{
	if (const int32* Revision = ElementGeometryRevisionByGuid.Find(ElementGuid))
	{
		return *Revision;
	}
	return 0;
}

#include "EHBFloorAssignmentPlan.inl"

void AEHBBuildingActorBase::ResolveAutomaticFloorAssignments()
{
 TArray<FEHBFloorAssignmentState> Source,Planned;
 for(const auto& Pair:ElementActorByGuid)if(const auto* E=Pair.Value.Get())
 {
  auto& S=Source.AddDefaulted_GetRef();S.ElementGuid=E->ElementGuid;S.Type=E->ElementType;S.FloorIndex=E->FloorIndex;
  S.Role=E->FloorRole;S.Policy=E->FloorAssignmentPolicy;S.Source=E->FloorAssignmentSource;
  S.Candidates=E->ConflictingFloorCandidates;S.Conflict=E->bFloorAssignmentConflict;
 }
 TArray<FEHBElementRelation> Relations;
 for(const auto& R:ElementRelations)if(!IsElementRelationStale(R.RelationGuid))Relations.Add(R);
 FName Status;
 if(!FEHBFloorAssignmentPlan::Build(Source,Relations,Planned,Status))
 {UE_LOG(LogTemp,Warning,TEXT("Floor assignment preparation failed: %s"),*Status.ToString());return;}
 // No actor is written until a complete, converged candidate exists.
 for(const auto& S:Planned)if(auto* E=FindElementActorByGuid(S.ElementGuid))
 {
  if(S.Policy!=EEHBFloorAssignmentPolicy::Automatic||S.Role==EEHBBuildingFloorElementRole::Foundation)continue;
  if(S.Source==EEHBFloorAssignmentSource::Unassigned)E->ClearDerivedFloorAssignment();
  else if(S.Source==EEHBFloorAssignmentSource::DerivedFromSupport)E->ApplyDerivedFloorAssignment(S.FloorIndex,S.Role,S.Candidates);
 }
 RebuildFloorIndexFromActors();
}

// #region 柱子与墙体关系 - 统一入口、查询、删除和闭环缓存

void AEHBBuildingActorBase::RegisterElementActor(AEHBElementActorBase* ElementActor)
{
	if (FEHBActorImportScope::IsActive()) return;
	if (!ElementActor)
	{
		return;
	}

	ElementActor->EnsureElementGuid();
	if (!ElementActor->ElementGuid.IsValid())
	{
		return;
	}

	if (const TWeakObjectPtr<AEHBElementActorBase>* ExistingActorPtr = ElementActorByGuid.Find(ElementActor->ElementGuid))
	{
		if (AEHBElementActorBase* ExistingActor = ExistingActorPtr->Get())
		{
			if (ExistingActor != ElementActor)
			{
				ElementActor->RegenerateElementGuid();
			}
		}
	}

	// Child components may register after the building rebuilt its load-time caches.
	if (ElementActor->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::RoomBoundary)))
		bClosedLoopsNeedRefresh = true;
	ElementActorByGuid.Add(ElementActor->ElementGuid, ElementActor);
	ElementGeometryRevisionByGuid.FindOrAdd(ElementActor->ElementGuid);
	RegisterElementToFloor(
		ElementActor->ElementGuid,
		ElementActor->ElementType,
		ElementActor->FloorIndex,
		ElementActor->FloorRole);
}

void AEHBBuildingActorBase::UnregisterElementActor(AEHBElementActorBase* ElementActor)
{
	if (!ElementActor || !ElementActor->ElementGuid.IsValid())
	{
		return;
	}

	if(WallNodeOwnership.Version==1&&Cast<AEHB_Pillar>(ElementActor))
	{
		const FGuid NodeId=FindNodeForPhysicalPillar(ElementActor->ElementGuid);
		TArray<FGuid> NodeRelations;
		for(const auto& Relation:ElementRelations)if(Relation.Source.RefersToNode(NodeId)||Relation.Target.RefersToNode(NodeId))NodeRelations.Add(Relation.RelationGuid);
		BeginRelationshipEdit();for(const FGuid RelationId:NodeRelations)RemoveElementRelation(RelationId);EndRelationshipEdit(false);
		if(NodeId.IsValid()){Modify();WallNodeOwnership.Bindings.RemoveAll([&](const auto& Binding){return Binding.NodeGuid==NodeId;});WallNodeAuthority.Nodes.RemoveAll([&](const auto& Node){return Node.NodeGuid==NodeId;});}
	}
	RemoveAllRelationsForElement(ElementActor->ElementGuid);
	ElementActorByGuid.Remove(ElementActor->ElementGuid);
	ElementGeometryRevisionByGuid.Remove(ElementActor->ElementGuid);
	UnregisterElementFromFloors(ElementActor->ElementGuid);
}

AEHBElementActorBase* AEHBBuildingActorBase::FindElementActorByGuid(FGuid ElementGuid) const
{
	if (!ElementGuid.IsValid())
	{
		return nullptr;
	}

	if (const TWeakObjectPtr<AEHBElementActorBase>* ExistingActorPtr = ElementActorByGuid.Find(ElementGuid))
	{
		if (AEHBElementActorBase* ExistingActor = ExistingActorPtr->Get())
		{
			return ExistingActor;
		}
	}

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (AActor* AttachedActor : AttachedActors)
	{
		AEHBElementActorBase* ElementActor = Cast<AEHBElementActorBase>(AttachedActor);
		if (ElementActor && ElementActor->ElementGuid == ElementGuid)
		{
			ElementActorByGuid.Add(ElementGuid, ElementActor);
			return ElementActor;
		}
	}

	return nullptr;
}

bool AEHBBuildingActorBase::CanConnectPillars(const AEHB_Pillar* FirstPillar, const AEHB_Pillar* SecondPillar) const
{
	if (!FirstPillar || !SecondPillar || FirstPillar == SecondPillar)
	{
		return false;
	}

	if (FirstPillar->OwningBuilding != this || SecondPillar->OwningBuilding != this)
	{
		return false;
	}

	if (!FirstPillar->ElementGuid.IsValid() || !SecondPillar->ElementGuid.IsValid())
	{
		return false;
	}

	if (FirstPillar->FloorIndex > 0
		&& SecondPillar->FloorIndex > 0
		&& FirstPillar->FloorIndex != SecondPillar->FloorIndex)
	{
		return false;
	}

	if (FVector::Dist2D(
		FirstPillar->GetElementLocalTransform().GetLocation(),
		SecondPillar->GetElementLocalTransform().GetLocation()) <= 10.0f)
	{
		return false;
	}

	bool bSameDirection = true;
	if (FindWallBetweenPillars(FirstPillar->ElementGuid, SecondPillar->ElementGuid, bSameDirection))
	{
		return false;
	}

	return CanConnectPillarsByRule(FirstPillar, SecondPillar);
}

bool AEHBBuildingActorBase::CanConnectPillarsByRule_Implementation(const AEHB_Pillar* FirstPillar, const AEHB_Pillar* SecondPillar) const
{
	return true;
}

AEHB_Wall* AEHBBuildingActorBase::ConnectPillars(AEHB_Pillar* FirstPillar, AEHB_Pillar* SecondPillar, float WallHeight, float WallThickness)
{
	if (!FirstPillar || !SecondPillar)
	{
		return nullptr;
	}

	FirstPillar->EnsureElementGuid();
	SecondPillar->EnsureElementGuid();
	RegisterElementActor(FirstPillar);
	RegisterElementActor(SecondPillar);
	RegisterAuthoredWallNode(FirstPillar);
	RegisterAuthoredWallNode(SecondPillar);

	const int32 ConnectionFloorIndex = FirstPillar->FloorIndex > 0
		? FirstPillar->FloorIndex
		: (SecondPillar->FloorIndex > 0 ? SecondPillar->FloorIndex : 1);
	if (!CanConnectPillars(FirstPillar, SecondPillar))
	{
		return nullptr;
	}

	if (!GetWorld())
	{
		return nullptr;
	}

	FVector LocalStart = FirstPillar->GetElementLocalTransform().GetLocation();
	FVector LocalEnd = SecondPillar->GetElementLocalTransform().GetLocation();
	FirstPillar->ResolveWallConnectionPointToward(LocalEnd, WallThickness, LocalStart);
	SecondPillar->ResolveWallConnectionPointToward(LocalStart, WallThickness, LocalEnd);

	const FVector WallDirection = (LocalEnd - LocalStart).GetSafeNormal2D();
	if (WallDirection.IsNearlyZero())
	{
		return nullptr;
	}

	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	TSubclassOf<AEHB_Wall> WallClass = AEHB_Wall::StaticClass();
	if (ToolsetSettings && !ToolsetSettings->WallActorClass.IsNull())
	{
		if (UClass* LoadedWallClass = ToolsetSettings->WallActorClass.LoadSynchronous())
		{
			WallClass = LoadedWallClass;
		}
	}

	const FName ActorName = MakeUniqueObjectName(GetLevel(), WallClass, TEXT("EHB_Wall"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = ActorName;
	SpawnParams.Owner = this;
	SpawnParams.OverrideLevel = GetLevel();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transactional;

	const FVector WallCenter = FMath::Lerp(LocalStart, LocalEnd, 0.5f);
	const FTransform LocalTransform(FRotator(0.0f, WallDirection.Rotation().Yaw, 0.0f), WallCenter);
	const FTransform WorldTransform = LocalTransform * GetActorTransform();

	AEHB_Wall* Wall = GetWorld()->SpawnActor<AEHB_Wall>(WallClass, WorldTransform, SpawnParams);
	if (!Wall)
	{
		return nullptr;
	}

	if (FirstPillar->FloorIndex <= 0 || FirstPillar->FloorRole == EEHBBuildingFloorElementRole::None)
	{
		FirstPillar->SetFloorAssignment(ConnectionFloorIndex, EEHBBuildingFloorElementRole::FloorBody);
	}
	if (SecondPillar->FloorIndex <= 0 || SecondPillar->FloorRole == EEHBBuildingFloorElementRole::None)
	{
		SecondPillar->SetFloorAssignment(ConnectionFloorIndex, EEHBBuildingFloorElementRole::FloorBody);
	}

	Wall->SetFlags(RF_Transactional);
	Wall->Modify();
	Wall->ElementName = ActorName;
	Wall->ConfigureAsSimpleWall(this, FirstPillar, SecondPillar, LocalStart, LocalEnd, WallHeight, WallThickness, true);
	Wall->SetFloorAssignment(ConnectionFloorIndex, EEHBBuildingFloorElementRole::FloorBody);

#if WITH_EDITOR
	Wall->SetActorLabel(ActorName.ToString());
#endif

	RegisterElementActor(Wall);
	RegisterPillarWallConnection(Wall, FirstPillar, SecondPillar);
	MarkPackageDirty();

	return Wall;
}

FVector AEHBBuildingActorBase::RoundBuildingLocalCoordinates(const FVector& LocalLocation) const
{
	return FVector(
		FMath::RoundToDouble(LocalLocation.X),
		FMath::RoundToDouble(LocalLocation.Y),
		FMath::RoundToDouble(LocalLocation.Z));
}

FVector AEHBBuildingActorBase::SnapWorldLocationToIntegerBuildingCoordinates(const FVector& WorldLocation) const
{
	return GetActorTransform().TransformPosition(
		RoundBuildingLocalCoordinates(GetActorTransform().InverseTransformPosition(WorldLocation)));
}

void AEHBBuildingActorBase::SnapPillarToIntegerBuildingCoordinates(AEHB_Pillar* Pillar, bool bFinished)
{
	if (!Pillar || Pillar->OwningBuilding != this || Pillar->IsActorBeingDestroyed())
	{
		return;
	}

	FTransform LocalTransform = Pillar->GetElementLocalTransform();
	const FVector SnappedLocation = RoundBuildingLocalCoordinates(LocalTransform.GetLocation());
	if (LocalTransform.GetLocation().Equals(SnappedLocation, UE_SMALL_NUMBER))
	{
		return;
	}

	Pillar->Modify();
	LocalTransform.SetLocation(SnappedLocation);
	Pillar->SetElementLocalTransform(LocalTransform, bFinished);
}

AEHB_Pillar* AEHBBuildingActorBase::CreatePillarAtLocalLocation(
	const FVector& LocalLocation,
	const FRotator& LocalRotation,
	float PillarHeight,
	float PillarWidth,
	float PillarDepth,
	int32 FloorIndex,
	const FString& NamePrefix,
	bool bSnapToIntegerBuildingCoordinates)
{
	if (!GetWorld())
	{
		return nullptr;
	}

	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	TSubclassOf<AEHB_Pillar> PillarClass = AEHB_Pillar::StaticClass();
	if (ToolsetSettings && !ToolsetSettings->PillarActorClass.IsNull())
	{
		if (UClass* LoadedPillarClass = ToolsetSettings->PillarActorClass.LoadSynchronous())
		{
			PillarClass = LoadedPillarClass;
		}
	}

	const FString SafeNamePrefix = NamePrefix.IsEmpty() ? TEXT("EHB_Pillar") : NamePrefix;
	const FName ActorName = MakeUniqueObjectName(GetLevel(), PillarClass, FName(*SafeNamePrefix));

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = ActorName;
	SpawnParams.Owner = this;
	SpawnParams.OverrideLevel = GetLevel();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transactional;

	const FVector ResolvedLocalLocation = bSnapToIntegerBuildingCoordinates
		? RoundBuildingLocalCoordinates(LocalLocation)
		: LocalLocation;
	const FTransform LocalTransform(LocalRotation, ResolvedLocalLocation);
	const FTransform WorldTransform = LocalTransform * GetActorTransform();
	AEHB_Pillar* Pillar = GetWorld()->SpawnActor<AEHB_Pillar>(PillarClass, WorldTransform, SpawnParams);
	if (!Pillar)
	{
		return nullptr;
	}

	Pillar->SetFlags(RF_Transactional);
	Pillar->Modify();
	Pillar->ElementName = ActorName;
	Pillar->AttachToBuilding(this, LocalTransform);
	Pillar->ConfigureAsPolygonPillar(
		FMath::Max(1.0f, PillarHeight),
		FMath::Max(1.0f, PillarWidth),
		FMath::Max(1.0f, PillarDepth),
		LocalTransform,
		true);
	Pillar->SetFloorAssignment(FMath::Max(1, FloorIndex), EEHBBuildingFloorElementRole::FloorBody);

#if WITH_EDITOR
	Pillar->SetActorLabel(ActorName.ToString());
#endif

	MarkPackageDirty();
	return Pillar;
}

bool AEHBBuildingActorBase::ResolveWallCreationEndpoint(
	const FVector& DesiredWorldLocation,
	float SnapDistance,
	float WallThickness,
	int32 FallbackFloorIndex,
	const AEHB_Pillar* IgnoredPillar,
	FEHBWallCreationEndpoint& OutEndpoint) const
{
	OutEndpoint = FEHBWallCreationEndpoint();
	if (!GetWorld())
	{
		return false;
	}

	const float SafeSnapDistance = FMath::Max(0.0f, SnapDistance);
	const float SafeWallThickness = FMath::Max(1.0f, WallThickness);
	const float HeightTolerance = FMath::Max(45.0f, SafeSnapDistance);
	const FTransform BuildingTransform = GetActorTransform();

	OutEndpoint.WorldLocation = SnapWorldLocationToIntegerBuildingCoordinates(DesiredWorldLocation);
	OutEndpoint.LocalLocation = BuildingTransform.InverseTransformPosition(OutEndpoint.WorldLocation);
	OutEndpoint.FloorIndex = FMath::Max(1, FallbackFloorIndex);

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);

	AEHB_Pillar* BestPillar = nullptr;
	float BestPillarDistanceSquared = FMath::Square(SafeSnapDistance);
	for (AActor* Actor : AttachedActors)
	{
		AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Actor);
		if (!Pillar
			|| Pillar == IgnoredPillar
			|| Pillar->OwningBuilding != this
			|| Pillar->IsActorBeingDestroyed())
		{
			continue;
		}

		const FVector PillarWorldLocation = Pillar->GetActorLocation();
		if (FMath::Abs(PillarWorldLocation.Z - DesiredWorldLocation.Z) > HeightTolerance)
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(PillarWorldLocation, DesiredWorldLocation);
		if (DistanceSquared <= BestPillarDistanceSquared)
		{
			BestPillarDistanceSquared = DistanceSquared;
			BestPillar = Pillar;
		}
	}

	if(WallNodeAuthority.Version==2)
	{
		const FEHBWallNodeDefinition* BestNode=nullptr;double BestDistance=BestPillarDistanceSquared;
		for(const auto& Node:WallNodeAuthority.Nodes)
		{
			if(FindPhysicalPillarForNode(Node.NodeGuid).IsValid()||Node.FloorIndex!=OutEndpoint.FloorIndex)continue;
			const FVector Position=BuildingTransform.TransformPosition(Node.LocalTransform.GetLocation());
			if(FMath::Abs(Position.Z-DesiredWorldLocation.Z)>HeightTolerance)continue;
			const double Distance=FVector::DistSquared2D(Position,DesiredWorldLocation);
			if(Distance<BestDistance||(Distance==BestDistance&&!BestPillar&&(!BestNode||Node.NodeGuid<BestNode->NodeGuid))){BestNode=&Node;BestDistance=Distance;}
		}
		if(BestNode)
		{
			OutEndpoint.NodeGuid=BestNode->NodeGuid;OutEndpoint.ExpectedNodeRevision=BestNode->GeometryRevision;
			OutEndpoint.LocalLocation=BestNode->LocalTransform.GetLocation();OutEndpoint.WorldLocation=BuildingTransform.TransformPosition(OutEndpoint.LocalLocation);OutEndpoint.FloorIndex=BestNode->FloorIndex;return true;
		}
	}
	if (BestPillar)
	{
		OutEndpoint.Pillar = BestPillar;
		OutEndpoint.WorldLocation = BestPillar->GetActorLocation();
		OutEndpoint.LocalLocation = BestPillar->GetElementLocalTransform().GetLocation();
		OutEndpoint.FloorIndex = FMath::Max(1, BestPillar->FloorIndex);
		return true;
	}

	AEHB_Wall* BestWall = nullptr;
	float BestWallDistance = 0.0f;
	FVector BestWallWorldLocation = FVector::ZeroVector;
	float BestWallDistanceSquared = FMath::Square(SafeSnapDistance + SafeWallThickness * 0.5f);
	for (AActor* Actor : AttachedActors)
	{
		AEHB_Wall* Wall = Cast<AEHB_Wall>(Actor);
		if (!Wall
			|| Wall->OwningBuilding != this
			|| Wall->IsActorBeingDestroyed())
		{
			continue;
		}

		const float WallLength = FVector::Dist2D(Wall->LocalStart, Wall->LocalEnd);
		const float MinWallSnapDistance = FMath::Max(10.0f, SafeWallThickness * 0.5f + 1.0f);
		if (WallLength <= MinWallSnapDistance * 2.0f)
		{
			continue;
		}

		const float DistanceFromStart = Wall->CalculateDistanceFromStartForWorldLocation(DesiredWorldLocation);
		if (DistanceFromStart <= MinWallSnapDistance || DistanceFromStart >= WallLength - MinWallSnapDistance)
		{
			continue;
		}

		const FVector WallWorldLocation = Wall->GetWorldLocationOnCenterAxisAtDistance(DistanceFromStart, 0.0f);
		if (FMath::Abs(WallWorldLocation.Z - DesiredWorldLocation.Z) > HeightTolerance)
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared2D(WallWorldLocation, DesiredWorldLocation);
		const float AllowedDistance = SafeSnapDistance + FMath::Max(1.0f, Wall->Thickness) * 0.5f;
		if (DistanceSquared > FMath::Square(AllowedDistance) || DistanceSquared > BestWallDistanceSquared)
		{
			continue;
		}

		BestWallDistanceSquared = DistanceSquared;
		BestWall = Wall;
		BestWallDistance = DistanceFromStart;
		BestWallWorldLocation = WallWorldLocation;
	}

	if (BestWall)
	{
		OutEndpoint.Wall = BestWall;
		OutEndpoint.WallDistance = BestWallDistance;
		OutEndpoint.WorldLocation = BestWallWorldLocation;
		OutEndpoint.LocalLocation = BuildingTransform.InverseTransformPosition(BestWallWorldLocation);
		OutEndpoint.FloorIndex = FMath::Max(1, BestWall->FloorIndex);
	}

	return true;
}

bool AEHBBuildingActorBase::CreateOrReuseWallSegment(
	FEHBWallCreationEndpoint& StartEndpoint,
	FEHBWallCreationEndpoint& EndEndpoint,
	const FEHBWallCreationOptions& Options,
	FEHBWallCreationResult& OutResult)
{
	OutResult = FEHBWallCreationResult();
	// Logical anchors require the candidate/dependency executor; never reinterpret
	// an actor-free anchor as a request for a replacement physical column.
	if(!Options.bCreatePhysicalColumns||StartEndpoint.NodeGuid.IsValid()||EndEndpoint.NodeGuid.IsValid())return false;
	if (!GetWorld())
	{
		return false;
	}

	auto IsValidEndpointWall = [this](const AEHB_Wall* Wall)
	{
		return Wall && Wall->OwningBuilding == this && !Wall->IsActorBeingDestroyed();
	};

	auto IsValidEndpointPillar = [this](const AEHB_Pillar* Pillar)
	{
		return Pillar && Pillar->OwningBuilding == this && !Pillar->IsActorBeingDestroyed();
	};

	if (!IsValidEndpointPillar(StartEndpoint.Pillar))
	{
		StartEndpoint.Pillar = nullptr;
	}
	if (!IsValidEndpointPillar(EndEndpoint.Pillar))
	{
		EndEndpoint.Pillar = nullptr;
	}
	if (StartEndpoint.Pillar)
	{
		StartEndpoint.Wall = nullptr;
	}
	if (EndEndpoint.Pillar)
	{
		EndEndpoint.Wall = nullptr;
	}
	if (!IsValidEndpointWall(StartEndpoint.Wall))
	{
		StartEndpoint.Wall = nullptr;
		StartEndpoint.WallDistance = 0.0f;
	}
	if (!IsValidEndpointWall(EndEndpoint.Wall))
	{
		EndEndpoint.Wall = nullptr;
		EndEndpoint.WallDistance = 0.0f;
	}

	const FTransform BuildingTransform = GetActorTransform();
	const FVector InitialLocalStart = StartEndpoint.Pillar
		? StartEndpoint.Pillar->GetElementLocalTransform().GetLocation()
		: BuildingTransform.InverseTransformPosition(StartEndpoint.WorldLocation);
	const FVector InitialLocalEnd = EndEndpoint.Pillar
		? EndEndpoint.Pillar->GetElementLocalTransform().GetLocation()
		: BuildingTransform.InverseTransformPosition(EndEndpoint.WorldLocation);
	const FVector InitialDirection = (InitialLocalEnd - InitialLocalStart).GetSafeNormal2D();
	if (InitialDirection.IsNearlyZero() || FVector::Dist2D(InitialLocalStart, InitialLocalEnd) <= 10.0f)
	{
		return false;
	}

	const FRotator PillarLocalRotation = Options.FreePillarLocalRotation.IsNearlyZero()
		? FRotator(0.0f, InitialDirection.Rotation().Yaw, 0.0f)
		: Options.FreePillarLocalRotation;
	const float SafeWallHeight = FMath::Max(1.0f, Options.WallHeight);
	const float SafeWallThickness = FMath::Max(1.0f, Options.WallThickness);
	const float SafePillarHeight = FMath::Max(1.0f, Options.PillarHeight);
	const float SafePillarWidth = FMath::Max(1.0f, Options.PillarWidth);
	const float SafePillarDepth = FMath::Max(1.0f, Options.PillarDepth);

	if (!StartEndpoint.Pillar
		&& !EndEndpoint.Pillar
		&& StartEndpoint.Wall
		&& StartEndpoint.Wall == EndEndpoint.Wall)
	{
		TArray<AEHB_Wall*> SplitWalls;
		AEHB_Pillar* StartPillar = nullptr;
		AEHB_Pillar* EndPillar = nullptr;
		if (!InsertTwoPillarsOnWall(
			StartEndpoint.Wall,
			StartEndpoint.WallDistance,
			EndEndpoint.WallDistance,
			SafePillarHeight,
			FMath::Max(SafePillarWidth, SafePillarDepth),
			StartPillar,
			EndPillar,
			SplitWalls))
		{
			return false;
		}

		if (!StartPillar || !EndPillar)
		{
			return false;
		}

		if (Options.bSnapToIntegerBuildingCoordinates)
		{
			SnapPillarToIntegerBuildingCoordinates(StartPillar, true);
			SnapPillarToIntegerBuildingCoordinates(EndPillar, true);
		}
		RefreshWallsConnectedToPillar(StartPillar->ElementGuid, true);
		RefreshWallsConnectedToPillar(EndPillar->ElementGuid, true);

		StartEndpoint.Pillar = StartPillar;
		EndEndpoint.Pillar = EndPillar;
		StartEndpoint.Wall = nullptr;
		EndEndpoint.Wall = nullptr;
		StartEndpoint.LocalLocation = StartPillar->GetElementLocalTransform().GetLocation();
		EndEndpoint.LocalLocation = EndPillar->GetElementLocalTransform().GetLocation();
		StartEndpoint.WorldLocation = StartPillar->GetActorLocation();
		EndEndpoint.WorldLocation = EndPillar->GetActorLocation();
		StartEndpoint.FloorIndex = FMath::Max(1, StartPillar->FloorIndex);
		EndEndpoint.FloorIndex = FMath::Max(1, EndPillar->FloorIndex);
	}

	auto EnsureEndpointPillar =
		[this, &Options, &PillarLocalRotation, &BuildingTransform, SafePillarHeight, SafePillarWidth, SafePillarDepth](FEHBWallCreationEndpoint& Endpoint)
		-> AEHB_Pillar*
	{
		if (Endpoint.Pillar && Endpoint.Pillar->OwningBuilding == this && !Endpoint.Pillar->IsActorBeingDestroyed())
		{
			return Endpoint.Pillar;
		}

		AEHB_Pillar* Pillar = nullptr;
		if (Endpoint.Wall && Endpoint.Wall->OwningBuilding == this && !Endpoint.Wall->IsActorBeingDestroyed())
		{
			TArray<AEHB_Wall*> SplitWalls;
			Pillar = InsertPillarOnWall(
				Endpoint.Wall,
				Endpoint.WallDistance,
				SafePillarHeight,
				FMath::Max(SafePillarWidth, SafePillarDepth),
				SplitWalls);
			if (Pillar && Options.bSnapToIntegerBuildingCoordinates)
			{
				SnapPillarToIntegerBuildingCoordinates(Pillar, true);
			}
			if (Pillar)
			{
				RefreshWallsConnectedToPillar(Pillar->ElementGuid, true);
			}
		}
		else
		{
			FVector LocalLocation = Endpoint.LocalLocation;
			if (LocalLocation.IsNearlyZero() && !Endpoint.WorldLocation.IsNearlyZero())
			{
				LocalLocation = BuildingTransform.InverseTransformPosition(Endpoint.WorldLocation);
			}

			Pillar = CreatePillarAtLocalLocation(
				LocalLocation,
				PillarLocalRotation,
				SafePillarHeight,
				SafePillarWidth,
				SafePillarDepth,
				Endpoint.FloorIndex > 0 ? Endpoint.FloorIndex : Options.FloorIndex,
				Options.NewPillarNamePrefix,
				Options.bSnapToIntegerBuildingCoordinates);
		}

		if (Pillar)
		{
			Pillar->EnsureElementGuid();
			Endpoint.Pillar = Pillar;
			Endpoint.Wall = nullptr;
			Endpoint.WallDistance = 0.0f;
			Endpoint.LocalLocation = Pillar->GetElementLocalTransform().GetLocation();
			Endpoint.WorldLocation = Pillar->GetActorLocation();
			Endpoint.FloorIndex = FMath::Max(1, Pillar->FloorIndex);
		}

		return Pillar;
	};

	AEHB_Pillar* StartPillar = EnsureEndpointPillar(StartEndpoint);
	AEHB_Pillar* EndPillar = EnsureEndpointPillar(EndEndpoint);
	if (!StartPillar || !EndPillar || StartPillar == EndPillar)
	{
		return false;
	}

	StartPillar->EnsureElementGuid();
	EndPillar->EnsureElementGuid();

	const FVector ChainLocalStart = StartPillar->GetElementLocalTransform().GetLocation();
	const FVector ChainLocalEnd = EndPillar->GetElementLocalTransform().GetLocation();
	const FVector ChainDirection = (ChainLocalEnd - ChainLocalStart).GetSafeNormal2D();
	const float ChainLength = FVector::Dist2D(ChainLocalStart, ChainLocalEnd);
	if (ChainDirection.IsNearlyZero() || ChainLength <= 10.0f)
	{
		return false;
	}

	auto PlanOptions = Options; PlanOptions.WallThickness = SafeWallThickness;
 const auto Plan = FEHBWallPathPlanning::BuildForBuilding(this, {StartEndpoint, EndEndpoint}, false, PlanOptions);
 if (!Plan.bSucceeded) return false;
 for (const auto& Segment : Plan.Segments)
 {
  auto* First = Cast<AEHB_Pillar>(FindElementActorByGuid(Plan.Points[Segment.X].ExistingPillarGuid));
  auto* Second = Cast<AEHB_Pillar>(FindElementActorByGuid(Plan.Points[Segment.Y].ExistingPillarGuid));
  if (!First || !Second) return false;
  bool bSameDirection = true;
  AEHB_Wall* Wall = FindWallBetweenPillars(First->ElementGuid, Second->ElementGuid, bSameDirection);
  if (!Wall) Wall = ConnectPillars(First, Second, SafeWallHeight, SafeWallThickness);
  if (!Wall) return false;
  OutResult.Walls.AddUnique(Wall);
  if (!OutResult.PrimaryWall) OutResult.PrimaryWall = Wall;
 }

	OutResult.StartPillar = StartPillar;
	OutResult.EndPillar = EndPillar;
	MarkPackageDirty();
	return OutResult.Walls.Num() > 0;
}

AEHB_Pillar* AEHBBuildingActorBase::InsertPillarOnWall(AEHB_Wall* SourceWall, float DistanceFromWallStart, float PillarHeight, float PillarWidthDepth, TArray<AEHB_Wall*>& OutNewWalls)
{
	OutNewWalls.Reset();

	if (!SourceWall || SourceWall->OwningBuilding != this || SourceWall->IsActorBeingDestroyed())
	{
		return nullptr;
	}

	SourceWall->EnsureElementGuid();
	RegisterElementActor(SourceWall);

	const float SourceWallLength = FVector::Dist2D(SourceWall->LocalStart, SourceWall->LocalEnd);
	const float MinSplitDistance = FMath::Max(10.0f, FMath::Max(1.0f, PillarWidthDepth) * 0.5f + 1.0f);
	const float ClampedDistance = FMath::Clamp(DistanceFromWallStart, 0.0f, SourceWallLength);
	if (SourceWallLength <= MinSplitDistance * 2.0f
		|| ClampedDistance <= MinSplitDistance
		|| ClampedDistance >= SourceWallLength - MinSplitDistance)
	{
		return nullptr;
	}

	AEHB_Pillar* SourceStartPillar = Cast<AEHB_Pillar>(FindElementActorByGuid(SourceWall->StartPillarGuid));
	AEHB_Pillar* SourceEndPillar = Cast<AEHB_Pillar>(FindElementActorByGuid(SourceWall->EndPillarGuid));
	if (!SourceStartPillar || !SourceEndPillar || SourceStartPillar->OwningBuilding != this || SourceEndPillar->OwningBuilding != this)
	{
		return nullptr;
	}

	bool bSourceSameDirection = true;
	if (FindWallBetweenPillars(SourceWall->StartPillarGuid, SourceWall->EndPillarGuid, bSourceSameDirection) != SourceWall)
	{
		return nullptr;
	}

	AEHB_Pillar* SplitPillar = SpawnSplitPillarForWall(SourceWall, ClampedDistance, PillarHeight, PillarWidthDepth);
	if (!SplitPillar)
	{
		return nullptr;
	}

	SplitPillar->EnsureElementGuid();
	RegisterElementActor(SplitPillar);

	AEHB_Wall* FirstNewWall = ConnectPillars(SourceStartPillar, SplitPillar, SourceWall->Height, SourceWall->Thickness);
	AEHB_Wall* SecondNewWall = ConnectPillars(SplitPillar, SourceEndPillar, SourceWall->Height, SourceWall->Thickness);
	if (!FirstNewWall || !SecondNewWall)
	{
		if (FirstNewWall)
		{
			DisconnectPillars(SourceStartPillar->ElementGuid, SplitPillar->ElementGuid, true);
		}
		if (SecondNewWall)
		{
			DisconnectPillars(SplitPillar->ElementGuid, SourceEndPillar->ElementGuid, true);
		}
		if (!SplitPillar->IsActorBeingDestroyed())
		{
			SplitPillar->Modify();
			SplitPillar->Destroy();
		}
		return nullptr;
	}

	OutNewWalls.Add(FirstNewWall);
	OutNewWalls.Add(SecondNewWall);

	DisconnectPillars(SourceWall->StartPillarGuid, SourceWall->EndPillarGuid, true);
	MarkPackageDirty();

	return SplitPillar;
}

bool AEHBBuildingActorBase::InsertTwoPillarsOnWall(AEHB_Wall* SourceWall, float FirstDistanceFromWallStart, float SecondDistanceFromWallStart, float PillarHeight, float PillarWidthDepth, AEHB_Pillar*& OutFirstPillar, AEHB_Pillar*& OutSecondPillar, TArray<AEHB_Wall*>& OutNewWalls)
{
	OutFirstPillar = nullptr;
	OutSecondPillar = nullptr;
	OutNewWalls.Reset();

	if (!SourceWall || SourceWall->OwningBuilding != this || SourceWall->IsActorBeingDestroyed())
	{
		return false;
	}

	SourceWall->EnsureElementGuid();
	RegisterElementActor(SourceWall);

	const float SourceWallLength = FVector::Dist2D(SourceWall->LocalStart, SourceWall->LocalEnd);
	const float MinSplitDistance = FMath::Max(10.0f, FMath::Max(1.0f, PillarWidthDepth) * 0.5f + 1.0f);
	const float FirstDistance = FMath::Clamp(FirstDistanceFromWallStart, 0.0f, SourceWallLength);
	const float SecondDistance = FMath::Clamp(SecondDistanceFromWallStart, 0.0f, SourceWallLength);
	if (SourceWallLength <= MinSplitDistance * 3.0f
		|| FirstDistance <= MinSplitDistance
		|| FirstDistance >= SourceWallLength - MinSplitDistance
		|| SecondDistance <= MinSplitDistance
		|| SecondDistance >= SourceWallLength - MinSplitDistance
		|| FMath::Abs(FirstDistance - SecondDistance) <= MinSplitDistance)
	{
		return false;
	}

	AEHB_Pillar* SourceStartPillar = Cast<AEHB_Pillar>(FindElementActorByGuid(SourceWall->StartPillarGuid));
	AEHB_Pillar* SourceEndPillar = Cast<AEHB_Pillar>(FindElementActorByGuid(SourceWall->EndPillarGuid));
	if (!SourceStartPillar || !SourceEndPillar || SourceStartPillar->OwningBuilding != this || SourceEndPillar->OwningBuilding != this)
	{
		return false;
	}

	bool bSourceSameDirection = true;
	if (FindWallBetweenPillars(SourceWall->StartPillarGuid, SourceWall->EndPillarGuid, bSourceSameDirection) != SourceWall)
	{
		return false;
	}

	const bool bFirstComesBeforeSecond = FirstDistance < SecondDistance;
	const float NearDistance = bFirstComesBeforeSecond ? FirstDistance : SecondDistance;
	const float FarDistance = bFirstComesBeforeSecond ? SecondDistance : FirstDistance;

	AEHB_Pillar* NearPillar = SpawnSplitPillarForWall(SourceWall, NearDistance, PillarHeight, PillarWidthDepth);
	AEHB_Pillar* FarPillar = SpawnSplitPillarForWall(SourceWall, FarDistance, PillarHeight, PillarWidthDepth);
	if (!NearPillar || !FarPillar)
	{
		if (NearPillar && !NearPillar->IsActorBeingDestroyed())
		{
			NearPillar->Destroy();
		}
		if (FarPillar && !FarPillar->IsActorBeingDestroyed())
		{
			FarPillar->Destroy();
		}
		return false;
	}

	NearPillar->EnsureElementGuid();
	FarPillar->EnsureElementGuid();
	RegisterElementActor(NearPillar);
	RegisterElementActor(FarPillar);

	AEHB_Wall* FirstNewWall = ConnectPillars(SourceStartPillar, NearPillar, SourceWall->Height, SourceWall->Thickness);
	AEHB_Wall* MiddleNewWall = ConnectPillars(NearPillar, FarPillar, SourceWall->Height, SourceWall->Thickness);
	AEHB_Wall* LastNewWall = ConnectPillars(FarPillar, SourceEndPillar, SourceWall->Height, SourceWall->Thickness);
	if (!FirstNewWall || !MiddleNewWall || !LastNewWall)
	{
		if (FirstNewWall)
		{
			DisconnectPillars(SourceStartPillar->ElementGuid, NearPillar->ElementGuid, true);
		}
		if (MiddleNewWall)
		{
			DisconnectPillars(NearPillar->ElementGuid, FarPillar->ElementGuid, true);
		}
		if (LastNewWall)
		{
			DisconnectPillars(FarPillar->ElementGuid, SourceEndPillar->ElementGuid, true);
		}
		if (!NearPillar->IsActorBeingDestroyed())
		{
			NearPillar->Destroy();
		}
		if (!FarPillar->IsActorBeingDestroyed())
		{
			FarPillar->Destroy();
		}
		return false;
	}

	OutNewWalls.Add(FirstNewWall);
	OutNewWalls.Add(MiddleNewWall);
	OutNewWalls.Add(LastNewWall);
	OutFirstPillar = bFirstComesBeforeSecond ? NearPillar : FarPillar;
	OutSecondPillar = bFirstComesBeforeSecond ? FarPillar : NearPillar;

	DisconnectPillars(SourceWall->StartPillarGuid, SourceWall->EndPillarGuid, true);
	MarkPackageDirty();

	return true;
}

bool AEHBBuildingActorBase::DisconnectPillars(FGuid FirstPillarGuid, FGuid SecondPillarGuid, bool bDestroyWallActor)
{
	if (!FirstPillarGuid.IsValid() || !SecondPillarGuid.IsValid() || FirstPillarGuid == SecondPillarGuid)
	{
		return false;
	}

	bool bSameDirection = true;
	AEHB_Wall* Wall = FindWallBetweenPillars(FirstPillarGuid, SecondPillarGuid, bSameDirection);
	FGuid WallGuid = Wall ? Wall->ElementGuid : FGuid();

	const FEHBBuildingWallConnection* FoundConnection = nullptr;
	if (WallGuid.IsValid())
	{
		FoundConnection = WallConnectionsByWallGuid.Find(WallGuid);
	}

	FEHBBuildingWallConnection Connection;
	if (FoundConnection)
	{
		Connection = *FoundConnection;
	}
	else
	{
		for (const TPair<FGuid, FEHBBuildingWallConnection>& Pair : WallConnectionsByWallGuid)
		{
			const FEHBBuildingWallConnection& Candidate = Pair.Value;
			if ((Candidate.StartPillarGuid == FirstPillarGuid && Candidate.EndPillarGuid == SecondPillarGuid)
				|| (Candidate.StartPillarGuid == SecondPillarGuid && Candidate.EndPillarGuid == FirstPillarGuid))
			{
				Connection = Candidate;
				WallGuid = Candidate.WallGuid;
				break;
			}
		}
	}

	if (!Connection.WallGuid.IsValid())
	{
		return false;
	}

	TGuardValue<bool> RelationGuard(bUpdatingPillarWallRelations, true);
	RemovePillarWallConnection(Connection);
	RebuildClosedLoops();

	if (bDestroyWallActor)
	{
		Wall = Wall ? Wall : Cast<AEHB_Wall>(FindElementActorByGuid(Connection.WallGuid));
		if (Wall && !Wall->IsActorBeingDestroyed())
		{
			Wall->Modify();
			Wall->Destroy();
		}
	}

	MarkPackageDirty();
	return true;
}

void AEHBBuildingActorBase::DisconnectAllPillarConnections(FGuid PillarGuid, bool bDestroyWallActors)
{
	if (!PillarGuid.IsValid())
	{
		return;
	}

	TArray<FEHBPillarWallConnection> ConnectionsToRemove;
	if (const FEHBPillarWallConnectionList* ConnectionList = PillarConnectionsByPillarGuid.Find(PillarGuid))
	{
		ConnectionsToRemove = ConnectionList->Connections;
	}

	for (const FEHBPillarWallConnection& Connection : ConnectionsToRemove)
	{
		DisconnectPillars(PillarGuid, Connection.OtherPillarGuid, bDestroyWallActors);
	}

	PillarConnectionsByPillarGuid.Remove(PillarGuid);
	PillarToLoopGuids.Remove(PillarGuid);
	RebuildClosedLoops();
}

void AEHBBuildingActorBase::HandleWallDeleted(AEHB_Wall* Wall)
{
	if (!Wall || bUpdatingPillarWallRelations)
	{
		return;
	}

	DisconnectPillars(Wall->StartPillarGuid, Wall->EndPillarGuid, false);
	UnregisterElementActor(Wall);
}

void AEHBBuildingActorBase::HandlePillarDeleted(AEHB_Pillar* Pillar)
{
	if (!Pillar || bUpdatingPillarWallRelations)
	{
		return;
	}

	DisconnectAllPillarConnections(Pillar->ElementGuid, true);
	UnregisterElementActor(Pillar);
}

void AEHBBuildingActorBase::RefreshWallsConnectedToPillar(FGuid PillarGuid, bool bFinished)
{
 if(WallNodeAuthority.Version==2){RebuildWallNodeAuthorityGeometry();return;}
	RefreshWallsConnectedToPillars({PillarGuid},bFinished);
}

AEHB_Wall* AEHBBuildingActorBase::FindWallBetweenPillars(FGuid FirstPillarGuid, FGuid SecondPillarGuid, bool& bSameDirection) const
{
	bSameDirection = true;

	if (!FirstPillarGuid.IsValid() || !SecondPillarGuid.IsValid())
	{
		return nullptr;
	}

	for (const TPair<FGuid, FEHBBuildingWallConnection>& Pair : WallConnectionsByWallGuid)
	{
		const FEHBBuildingWallConnection& Connection = Pair.Value;
		if (Connection.StartPillarGuid == FirstPillarGuid && Connection.EndPillarGuid == SecondPillarGuid)
		{
			bSameDirection = true;
			return Cast<AEHB_Wall>(FindElementActorByGuid(Connection.WallGuid));
		}

		if (Connection.StartPillarGuid == SecondPillarGuid && Connection.EndPillarGuid == FirstPillarGuid)
		{
			bSameDirection = false;
			return Cast<AEHB_Wall>(FindElementActorByGuid(Connection.WallGuid));
		}
	}

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (AActor* AttachedActor : AttachedActors)
	{
		AEHB_Wall* Wall = Cast<AEHB_Wall>(AttachedActor);
		if (!Wall)
		{
			continue;
		}

		if (Wall->StartPillarGuid == FirstPillarGuid && Wall->EndPillarGuid == SecondPillarGuid)
		{
			bSameDirection = true;
			return Wall;
		}

		if (Wall->StartPillarGuid == SecondPillarGuid && Wall->EndPillarGuid == FirstPillarGuid)
		{
			bSameDirection = false;
			return Wall;
		}
	}

	return nullptr;
}

TArray<FEHBBuildingClosedLoop> AEHBBuildingActorBase::GetClosedLoopsByPillarGuid(FGuid PillarGuid) const
{
	EnsureClosedLoopsCurrent();
	TArray<FEHBBuildingClosedLoop> Result;
	const FEHBGuidList* LoopGuidList = PillarToLoopGuids.Find(PillarGuid);
	if (!LoopGuidList)
	{
		return Result;
	}

	for (const FGuid& LoopGuid : LoopGuidList->Guids)
	{
		const FEHBBuildingClosedLoop* Loop = ClosedLoops.FindByPredicate(
			[LoopGuid](const FEHBBuildingClosedLoop& Candidate)
			{
				return Candidate.LoopGuid == LoopGuid;
			});
		if (Loop)
		{
			Result.Add(*Loop);
		}
	}

	return Result;
}

TArray<FEHBBuildingClosedLoop> AEHBBuildingActorBase::GetClosedLoopsByWallGuid(FGuid WallGuid) const
{
	EnsureClosedLoopsCurrent();
	TArray<FEHBBuildingClosedLoop> Result;
	const FEHBGuidList* LoopGuidList = WallToLoopGuids.Find(WallGuid);
	if (!LoopGuidList)
	{
		return Result;
	}

	for (const FGuid& LoopGuid : LoopGuidList->Guids)
	{
		const FEHBBuildingClosedLoop* Loop = ClosedLoops.FindByPredicate(
			[LoopGuid](const FEHBBuildingClosedLoop& Candidate)
			{
				return Candidate.LoopGuid == LoopGuid;
			});
		if (Loop)
		{
			Result.Add(*Loop);
		}
	}

	return Result;
}

TArray<FEHBBuildingClosedLoop> AEHBBuildingActorBase::GetClosedLoopsByFloor(int32 FloorIndex) const
{
	EnsureClosedLoopsCurrent();
	TArray<FEHBBuildingClosedLoop> Result;
	for (const FEHBBuildingClosedLoop& Loop : ClosedLoops)
	{
		if (Loop.FloorIndex == FloorIndex)
		{
			Result.Add(Loop);
		}
	}
	return Result;
}

bool AEHBBuildingActorBase::IsExteriorWall(const AEHB_Wall* Wall) const
{
	if (!Wall || Wall->OwningBuilding != this || !Wall->ElementGuid.IsValid())
	{
		return false;
	}

	return GetClosedLoopsByWallGuid(Wall->ElementGuid).Num() == 1;
}

FEHBWallPillarSet AEHBBuildingActorBase::GetRoomWallsAndPillars(const FEHBBuildingClosedLoop& RoomLoop) const
{
	FEHBWallPillarSet Result;
	TSet<FGuid> AddedWallGuids;
	TSet<FGuid> AddedPillarGuids;

	for (const FGuid& WallGuid : RoomLoop.WallGuids)
	{
		if (!WallGuid.IsValid() || AddedWallGuids.Contains(WallGuid))
		{
			continue;
		}

		if (AEHB_Wall* Wall = Cast<AEHB_Wall>(FindElementActorByGuid(WallGuid)))
		{
			Result.Walls.Add(Wall);
			AddedWallGuids.Add(WallGuid);
		}
	}

	for (const FGuid& PillarGuid : RoomLoop.PillarGuids)
	{
		if (!PillarGuid.IsValid() || AddedPillarGuids.Contains(PillarGuid))
		{
			continue;
		}

		if (AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(FindElementActorByGuid(PillarGuid)))
		{
			Result.Pillars.Add(Pillar);
			AddedPillarGuids.Add(PillarGuid);
		}
	}

	return Result;
}

FEHBWallPillarSet AEHBBuildingActorBase::GetExteriorWallsAndPillarsFromWall(const AEHB_Wall* ReferenceWall) const
{
	FEHBWallPillarSet Result;
	if (!IsExteriorWall(ReferenceWall))
	{
		return Result;
	}

	TSet<FGuid> VisitedWallGuids;
	TSet<FGuid> AddedPillarGuids;
	TQueue<FGuid> PendingWallGuids;
	PendingWallGuids.Enqueue(ReferenceWall->ElementGuid);

	FGuid CurrentWallGuid;
	while (PendingWallGuids.Dequeue(CurrentWallGuid))
	{
		if (!CurrentWallGuid.IsValid() || VisitedWallGuids.Contains(CurrentWallGuid))
		{
			continue;
		}

		AEHB_Wall* Wall = Cast<AEHB_Wall>(FindElementActorByGuid(CurrentWallGuid));
		if (!IsExteriorWall(Wall))
		{
			continue;
		}

		VisitedWallGuids.Add(CurrentWallGuid);
		Result.Walls.Add(Wall);

		const FGuid EndpointGuids[] = { Wall->StartPillarGuid, Wall->EndPillarGuid };
		for (const FGuid& PillarGuid : EndpointGuids)
		{
			if (!PillarGuid.IsValid())
			{
				continue;
			}

			if (!AddedPillarGuids.Contains(PillarGuid))
			{
				if (AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(FindElementActorByGuid(PillarGuid)))
				{
					Result.Pillars.Add(Pillar);
					AddedPillarGuids.Add(PillarGuid);
				}
			}

			if (const FEHBPillarWallConnectionList* Connections = PillarConnectionsByPillarGuid.Find(PillarGuid))
			{
				for (const FEHBPillarWallConnection& Connection : Connections->Connections)
				{
					if (Connection.WallGuid.IsValid() && !VisitedWallGuids.Contains(Connection.WallGuid))
					{
						PendingWallGuids.Enqueue(Connection.WallGuid);
					}
				}
			}
		}
	}

	return Result;
}

bool AEHBBuildingActorBase::BuildClosedLoopPillarPolygon2D(
	const FEHBBuildingClosedLoop& Loop,
	TArray<FVector2d>& OutPolygon) const
{
	OutPolygon.Reset();
	if(HasWallNodeAuthority())
	{
		const auto* Cycle=ClosedLoopNodeCycles.Find(Loop.LoopGuid);if(!Cycle)return false;
		for(FGuid Id:*Cycle){const auto* N=WallNodeAuthority.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});if(!N){OutPolygon.Reset();return false;}const FVector P=N->LocalTransform.GetLocation();OutPolygon.Add(FVector2d(P.X,P.Y));}
		return OutPolygon.Num()>=3&&FMath::Abs(CalculateSignedArea2D(OutPolygon))>UE_DOUBLE_SMALL_NUMBER;
	}
	OutPolygon.Reserve(Loop.PillarGuids.Num());
	for (const FGuid& PillarGuid : Loop.PillarGuids)
	{
		const AEHBElementActorBase* PillarActor = FindElementActorByGuid(PillarGuid);
		if (!PillarActor)
		{
			OutPolygon.Reset();
			return false;
		}

		const FVector PillarLocation = PillarActor->GetElementLocalTransform().GetLocation();
		const FVector2d Point(PillarLocation.X, PillarLocation.Y);
		if (OutPolygon.IsEmpty() || (OutPolygon.Last() - Point).SquaredLength() > 0.01)
		{
			OutPolygon.Add(Point);
		}
	}

	if (OutPolygon.Num() >= 2 && (OutPolygon[0] - OutPolygon.Last()).SquaredLength() <= 0.01)
	{
		OutPolygon.Pop(EAllowShrinking::No);
	}

	return OutPolygon.Num() >= 3 && FMath::Abs(CalculateSignedArea2D(OutPolygon)) > UE_DOUBLE_SMALL_NUMBER;
}

TArray<FEHBBuildingClosedLoop> AEHBBuildingActorBase::FindClosedLoopsContainingBuildingLocalPoint(
	const FVector& BuildingLocalPoint,
	int32 FloorIndex) const
{
	EnsureClosedLoopsCurrent();
	const int32 EffectiveFloorIndex = FloorIndex == -99
		? ResolveFloorIndexFromBuildingLocalZ(static_cast<double>(BuildingLocalPoint.Z))
		: FloorIndex;
	if (EffectiveFloorIndex == INDEX_NONE)
	{
		return {};
	}

	TArray<FEHBBuildingClosedLoop> Result;
	const FVector2d QueryPoint(BuildingLocalPoint.X, BuildingLocalPoint.Y);
	TArray<FVector2d> LoopPolygon;
	for (const FEHBBuildingClosedLoop& Loop : ClosedLoops)
	{
		if (Loop.FloorIndex != EffectiveFloorIndex)
		{
			continue;
		}

		if (!BuildClosedLoopPillarPolygon2D(Loop, LoopPolygon))
		{
			continue;
		}

		if (IsPointInsideOrOnPolygon2D(QueryPoint, LoopPolygon, 0.5))
		{
			Result.Add(Loop);
		}
	}

	return Result;
}

TArray<FEHBBuildingClosedLoop> AEHBBuildingActorBase::FindClosedLoopsContainingWorldLocation(
	const FVector& WorldLocation,
	int32 FloorIndex) const
{
	const FVector BuildingLocalPoint = GetActorTransform().InverseTransformPosition(WorldLocation);
	return FindClosedLoopsContainingBuildingLocalPoint(BuildingLocalPoint, FloorIndex);
}

bool AEHBBuildingActorBase::FindClosedLoopByWorldHit(
	const FVector& WorldLocation,
	const FVector& WorldHitNormal,
	FEHBBuildingClosedLoop& OutClosedLoop,
	int32 FloorIndex,
	float ProbeDistance) const
{
	OutClosedLoop = FEHBBuildingClosedLoop();

	FVector QueryLocation = WorldLocation;
	if (!WorldHitNormal.IsNearlyZero())
	{
		QueryLocation += WorldHitNormal.GetSafeNormal() * FMath::Max(ProbeDistance, 0.0f);
	}

	const TArray<FEHBBuildingClosedLoop> Candidates =
		FindClosedLoopsContainingWorldLocation(QueryLocation, FloorIndex);
	if (Candidates.Num() != 1)
	{
		return false;
	}

	OutClosedLoop = Candidates[0];
	return true;
}

void AEHBBuildingActorBase::EnsureClosedLoopsCurrent() const
{
	if (bClosedLoopsNeedRefresh && !bUpdatingPillarWallRelations && !bRebuildingRelationshipIndexes)
	{
		// Resolve authoritative relations once after child registration settles.
		// No new relationship or room identity is written to the asset.
		const_cast<AEHBBuildingActorBase*>(this)->RebuildLegacyTopologyCachesFromRelationships();
	}
}

void AEHBBuildingActorBase::RebuildClosedLoops()
{
	bClosedLoopsNeedRefresh=false;const auto PreviousLoops=MoveTemp(ClosedLoops);
	ClosedLoopNodeCycles.Reset();
 TMap<FGuid,int32> PreviousLoopIndices;for(int32 I=0;I<PreviousLoops.Num();++I)PreviousLoopIndices.Add(PreviousLoops[I].LoopGuid,I);
 ClosedLoops.Reset();PillarToLoopGuids.Reset();WallToLoopGuids.Reset();
	TMap<FGuid,FVector> Locations;
	TMap<FGuid,FGuid> NodesByPillar, PillarsByNode;
	if (WallNodeOwnership.Version == 1) for (const auto& Binding : WallNodeOwnership.Bindings)
	{ NodesByPillar.Add(Binding.PhysicalPillarGuid,Binding.NodeGuid); PillarsByNode.Add(Binding.NodeGuid,Binding.PhysicalPillarGuid); }
	auto RoomNode = [&](FGuid Pillar) { return WallNodeOwnership.Version == 1 ? NodesByPillar.FindRef(Pillar) : Pillar; };
	TArray<FEHBRoomGraphEdge> Edges;
	TArray<FEHBNodeRoomBoundary> Rooms;
	if(HasWallNodeAuthority())
	{
		const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(this);
		if(Graph.Issues.IsEmpty())
		{
			for(const auto& N:Graph.Nodes)Locations.Add(N.NodeGuid,N.LocalPosition);
			for(const auto& W:Graph.Walls){auto& E=Edges.AddDefaulted_GetRef();E.WallGuid=W.WallGuid;E.StartNodeGuid=W.StartNodeGuid;E.EndNodeGuid=W.EndNodeGuid;const auto* N=Graph.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==W.StartNodeGuid;});E.FloorIndex=N?N->FloorIndex:0;}
			if(WallNodeAuthority.Version==2)
			{
				// Use the same face policy as candidate edits. Open branches are not
				// room boundaries; including their round trip changes the room identity.
				// Read topology only, so openings and surface styles do not block room queries.
				FEHBWallNodeModel Model;Model.Version=1;Model.Nodes=WallNodeAuthority.Nodes;Model.PillarBindings=WallNodeOwnership.Bindings;
				bool bComplete=true;
				for(const auto& W:Graph.Walls)
				{
					const auto* Wall=Cast<AEHB_Wall>(FindElementActorByGuid(W.WallGuid));if(!Wall){bComplete=false;break;}
					auto& Definition=Model.Walls.AddDefaulted_GetRef();Definition.WallGuid=W.WallGuid;Definition.StartNodeGuid=W.StartNodeGuid;Definition.EndNodeGuid=W.EndNodeGuid;Definition.Height=Wall->Height;Definition.Thickness=Wall->Thickness;
				}
				FName Reason;if(bComplete)FEHBWallNodeRooms::Build(BuildingGuid,Model,Rooms,Reason);
			}
		}
	}
	else for(const auto& Pair:WallConnectionsByWallGuid)
	{
		const auto& Connection=Pair.Value;
		for(const FGuid PillarId:{Connection.StartPillarGuid,Connection.EndPillarGuid})
			if(const FGuid NodeId=RoomNode(PillarId);NodeId.IsValid()&&!Locations.Contains(NodeId))
				if(const auto* Pillar=Cast<AEHB_Pillar>(FindElementActorByGuid(PillarId)))
					Locations.Add(NodeId,Pillar->GetElementLocalTransform().GetLocation());
		auto& Edge=Edges.AddDefaulted_GetRef();Edge.WallGuid=Connection.WallGuid;
		Edge.StartNodeGuid=RoomNode(Connection.StartPillarGuid);Edge.EndNodeGuid=RoomNode(Connection.EndPillarGuid);
		if(const auto* Wall=Cast<AEHB_Wall>(FindElementActorByGuid(Edge.WallGuid)))Edge.FloorIndex=Wall->FloorIndex;
	}
	if(WallNodeAuthority.Version!=2)FEHBWallNodeRooms::ExtractFaces(BuildingGuid,Locations,Edges,Rooms);
	for(auto& Room:Rooms)
	{
		auto& Loop=ClosedLoops.AddDefaulted_GetRef();Loop.LoopGuid=Room.RoomGuid;Loop.FloorIndex=Room.FloorIndex;
		Loop.Area=static_cast<float>(Room.Area);Loop.bClockwise=false;
		// Room identity uses logical nodes; the compatibility loop still exposes physical pillars.
		for (FGuid NodeId : Room.NodeGuids) Loop.PillarGuids.Add(WallNodeOwnership.Version == 1 ? PillarsByNode.FindRef(NodeId) : NodeId);
		Loop.WallGuids=MoveTemp(Room.WallGuids);
		auto NodeCycle=Room.NodeGuids;
  // A face walk may start on any edge after transient hash maps rebuild. Keep paired
  // pillar/wall cycles aligned, using an existing room's start when that node survives.
  int32 First=0;
  for(int32 I=1;I<Loop.PillarGuids.Num();++I)if(Loop.PillarGuids[I]<Loop.PillarGuids[First])First=I;
  if(const auto* PreviousIndex=PreviousLoopIndices.Find(Loop.LoopGuid))
  {
   const auto& Previous=PreviousLoops[*PreviousIndex];
   if(Previous.FloorIndex==Loop.FloorIndex&&!Previous.PillarGuids.IsEmpty())
    if(const int32 SavedStart=Loop.PillarGuids.Find(Previous.PillarGuids[0]);SavedStart!=INDEX_NONE)First=SavedStart;
  }
  // Optional bindings cannot provide a stable cycle start: several or all physical
  // IDs may be absent. Canonicalize with logical identity in V2, including cold load.
  if(WallNodeAuthority.Version==2){First=0;for(int32 I=1;I<NodeCycle.Num();++I)if(NodeCycle[I]<NodeCycle[First])First=I;}
  if(First>0&&Loop.PillarGuids.Num()==Loop.WallGuids.Num())
  {
   const auto Pillars=Loop.PillarGuids,Walls=Loop.WallGuids;
   for(int32 I=0;I<Pillars.Num();++I){Loop.PillarGuids[I]=Pillars[(I+First)%Pillars.Num()];Loop.WallGuids[I]=Walls[(I+First)%Walls.Num()];}
  }

		if(First>0){const auto Cycle=NodeCycle;for(int32 I=0;I<Cycle.Num();++I)NodeCycle[I]=Cycle[(I+First)%Cycle.Num()];}
		ClosedLoopNodeCycles.Add(Loop.LoopGuid,MoveTemp(NodeCycle));
		for(const FGuid Id:Loop.PillarGuids)if(Id.IsValid())PillarToLoopGuids.FindOrAdd(Id).Guids.AddUnique(Loop.LoopGuid);
		for(const FGuid Id:Loop.WallGuids)WallToLoopGuids.FindOrAdd(Id).Guids.AddUnique(Loop.LoopGuid);
	}
}

void AEHBBuildingActorBase::RegisterPillarWallConnection(AEHB_Wall* Wall, AEHB_Pillar* StartPillar, AEHB_Pillar* EndPillar)
{
	if (!Wall || !StartPillar || !EndPillar)
	{
		return;
	}

	Wall->EnsureElementGuid();
	StartPillar->EnsureElementGuid();
	EndPillar->EnsureElementGuid();

	FEHBBuildingWallConnection Connection;
	Connection.WallGuid = Wall->ElementGuid;
	Connection.StartPillarGuid = StartPillar->ElementGuid;
	Connection.EndPillarGuid = EndPillar->ElementGuid;

	WallConnectionsByWallGuid.Add(Connection.WallGuid, Connection);
	BeginRelationshipEdit();
	AddOrUpdateTopologyRelation(Wall, StartPillar, EEHBElementSurfaceKind::Start);
	AddOrUpdateTopologyRelation(Wall, EndPillar, EEHBElementSurfaceKind::End);
	EndRelationshipEdit(false);

	FEHBPillarWallConnection StartConnection;
	StartConnection.OtherPillarGuid = EndPillar->ElementGuid;
	StartConnection.WallGuid = Wall->ElementGuid;
	PillarConnectionsByPillarGuid.FindOrAdd(StartPillar->ElementGuid).Connections.AddUnique(StartConnection);

	FEHBPillarWallConnection EndConnection;
	EndConnection.OtherPillarGuid = StartPillar->ElementGuid;
	EndConnection.WallGuid = Wall->ElementGuid;
	PillarConnectionsByPillarGuid.FindOrAdd(EndPillar->ElementGuid).Connections.AddUnique(EndConnection);

	SyncPillarConnectionData(StartPillar, PillarConnectionsByPillarGuid.Find(StartPillar->ElementGuid));
	SyncPillarConnectionData(EndPillar, PillarConnectionsByPillarGuid.Find(EndPillar->ElementGuid));
	// Adding a leg changes both junction faces. Existing incident walls must see
	// those faces now, otherwise their first undo/rebuild changes the saved shape.
	TArray<AEHB_Wall*> IncidentWalls;
	for(const FGuid Id:{StartPillar->ElementGuid,EndPillar->ElementGuid})
		if(const auto* List=PillarConnectionsByPillarGuid.Find(Id))for(const auto& C:List->Connections)
			if(auto* Incident=Cast<AEHB_Wall>(FindElementActorByGuid(C.WallGuid)))IncidentWalls.AddUnique(Incident);
	for(auto* Incident:IncidentWalls)
	{
		Incident->SetFlags(RF_Transactional);Incident->Modify();
		TInlineComponentArray<UActorComponent*> Components(Incident);
		for(auto* Component:Components){Component->SetFlags(RF_Transactional);Component->Modify();}
	}
	StartPillar->RebuildPillarMesh();
	EndPillar->RebuildPillarMesh();
	for(auto* Incident:IncidentWalls)Incident->RefreshFromConnectedPillars(true);

	RebuildClosedLoops();
}

void AEHBBuildingActorBase::RemovePillarWallConnection(const FEHBBuildingWallConnection& Connection)
{
	BeginRelationshipEdit();
	RemoveRelationsBetweenElements(
		Connection.WallGuid,
		Connection.StartPillarGuid,
		EEHBElementRelationType::TopologyConnection);
	RemoveRelationsBetweenElements(
		Connection.WallGuid,
		Connection.EndPillarGuid,
		EEHBElementRelationType::TopologyConnection);
	EndRelationshipEdit(false);
	WallConnectionsByWallGuid.Remove(Connection.WallGuid);

	auto RemoveFromPillar = [this, &Connection](const FGuid& PillarGuid, const FGuid& OtherPillarGuid)
	{
		FEHBPillarWallConnectionList* ConnectionList = PillarConnectionsByPillarGuid.Find(PillarGuid);
		if (!ConnectionList)
		{
			return;
		}

		ConnectionList->Connections.RemoveAll(
			[&Connection, &OtherPillarGuid](const FEHBPillarWallConnection& PillarConnection)
			{
				return PillarConnection.WallGuid == Connection.WallGuid
					|| PillarConnection.OtherPillarGuid == OtherPillarGuid;
			});

		AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(FindElementActorByGuid(PillarGuid));
		SyncPillarConnectionData(Pillar, ConnectionList);
		if (ConnectionList->Connections.IsEmpty())
		{
			PillarConnectionsByPillarGuid.Remove(PillarGuid);
		}
		if (Pillar)
		{
			Pillar->RebuildPillarMesh();
		}
	};

	RemoveFromPillar(Connection.StartPillarGuid, Connection.EndPillarGuid);
	RemoveFromPillar(Connection.EndPillarGuid, Connection.StartPillarGuid);
}

AEHB_Pillar* AEHBBuildingActorBase::SpawnSplitPillarForWall(const AEHB_Wall* SourceWall, float DistanceFromWallStart, float PillarHeight, float PillarWidthDepth)
{
	if (!SourceWall || !GetWorld())
	{
		return nullptr;
	}

	const UEHBBuildingToolsetSettings* ToolsetSettings = GetDefault<UEHBBuildingToolsetSettings>();
	TSubclassOf<AEHB_Pillar> PillarClass = AEHB_Pillar::StaticClass();
	if (ToolsetSettings && !ToolsetSettings->PillarActorClass.IsNull())
	{
		if (UClass* LoadedPillarClass = ToolsetSettings->PillarActorClass.LoadSynchronous())
		{
			PillarClass = LoadedPillarClass;
		}
	}

	const FVector LocalLocation = SourceWall->GetBuildingLocalLocationOnCenterAxisAtDistance(DistanceFromWallStart, 0.0f);

	const FVector WallDirection = (SourceWall->LocalEnd - SourceWall->LocalStart).GetSafeNormal2D();
	const float LocalYaw = WallDirection.IsNearlyZero() ? 0.0f : WallDirection.Rotation().Yaw;
	const FTransform LocalTransform(FRotator(0.0f, LocalYaw, 0.0f), LocalLocation);
	const FTransform WorldTransform = LocalTransform * GetActorTransform();

	const FName ActorName = MakeUniqueObjectName(GetLevel(), PillarClass, TEXT("EHB_SplitPillar"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = ActorName;
	SpawnParams.Owner = this;
	SpawnParams.OverrideLevel = GetLevel();
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transactional;

	AEHB_Pillar* Pillar = GetWorld()->SpawnActor<AEHB_Pillar>(PillarClass, WorldTransform, SpawnParams);
	if (!Pillar)
	{
		return nullptr;
	}

	Pillar->SetFlags(RF_Transactional);
	Pillar->Modify();
	Pillar->ElementName = ActorName;
	Pillar->AttachToBuilding(this, LocalTransform);
	Pillar->ConfigureAsPolygonPillar(
		FMath::Max(1.0f, PillarHeight),
		FMath::Max(1.0f, PillarWidthDepth),
		FMath::Max(1.0f, PillarWidthDepth),
		LocalTransform,
		true);
	// SceneComponent's normal relative setter moves in world space and derives
	// the relative pose again. Preserve the declared split pose after that normal
	// movement, only when the difference is numerical roundoff. A genuine slab
	// snap is not overwritten. Without this, tiny drift can cross a float wall-end
	// rounding boundary and make the generated support disagree with the plan.
	if (Pillar->GetRootComponent() && Pillar->GetElementLocalTransform().Equals(LocalTransform, 1.e-8))
	{
		Pillar->GetRootComponent()->SetRelativeLocation_Direct(LocalTransform.GetLocation());
		Pillar->GetRootComponent()->SetRelativeRotation_Direct(LocalTransform.Rotator());
		Pillar->GetRootComponent()->UpdateComponentToWorld(EUpdateTransformFlags::None, ETeleportType::TeleportPhysics);
		RecordAuthoredWallNode(Pillar);
		Pillar->RebuildPillarMesh();
	}
	Pillar->SetFloorAssignment(
		SourceWall->FloorIndex > 0 ? SourceWall->FloorIndex : 1,
		EEHBBuildingFloorElementRole::FloorBody);

#if WITH_EDITOR
	Pillar->SetActorLabel(ActorName.ToString());
#endif

	return Pillar;
}

FEHBConnectedWallRefreshStats AEHBBuildingActorBase::RefreshWallsConnectedToPillars(const TArray<FGuid>& PillarGuids,bool bFinished)
{
 if(WallNodeAuthority.Version==2){FEHBConnectedWallRefreshStats Stats;Stats.bSucceeded=RebuildWallNodeAuthorityGeometry();Stats.Status=Stats.bSucceeded?TEXT("Applied"):TEXT("InvalidNodeAuthoritySource");return Stats;}
	return RefreshConnectedWallNodes(PillarGuids,bFinished,nullptr);
}

FEHBConnectedWallRefreshStats AEHBBuildingActorBase::RefreshNodeDefinitionDraft(const FEHBWallNodeMoveDraft& Draft,bool bFinished)
{
	FEHBConnectedWallRefreshStats Stats;
	auto Fail=[&](FName Reason){Stats.bSucceeded=false;Stats.Status=Reason;return Stats;};
	if(!Draft.bSucceeded||!Draft.UpdatePlan.bSucceeded||Draft.bWouldChange!=!Draft.UpdatePlan.MovedNodeGuids.IsEmpty())return Fail(TEXT("InvalidDraft"));
	if(!UEHBWallTopologyLibrary::ValidatePreparedWallNodeDefinitions(Draft.Definitions).IsEmpty())return Fail(TEXT("InvalidDefinitions"));
	FEHBPreparedWallNodeDefinitions Actual;const auto Capture=UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(this,Actual);if(!Capture.bSucceeded)return Fail(Capture.Status);
	if(Actual.Nodes.Num()!=Draft.Definitions.Nodes.Num()||Actual.Walls.Num()!=Draft.Definitions.Walls.Num()||Actual.PillarBindings.Num()!=Draft.Definitions.PillarBindings.Num())return Fail(TEXT("DefinitionSourceChanged"));
	for(const auto& N:Draft.Definitions.Nodes){const auto* A=Actual.Nodes.FindByPredicate([&](const auto& V){return V.NodeGuid==N.NodeGuid;});if(!A||!A->LocalTransform.Equals(N.LocalTransform,0.001)||A->FloorIndex!=N.FloorIndex||A->JunctionDimensions!=N.JunctionDimensions)return Fail(TEXT("DefinitionSourceChanged"));}
	for(const auto& B:Draft.Definitions.PillarBindings){const auto* A=Actual.PillarBindings.FindByPredicate([&](const auto& V){return V.NodeGuid==B.NodeGuid;});if(!A||A->PhysicalPillarGuid!=B.PhysicalPillarGuid)return Fail(TEXT("DefinitionSourceChanged"));}
	for(const auto& W:Draft.Definitions.Walls){const auto* A=Actual.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==W.WallGuid;});if(!A||A->StartNodeGuid!=W.StartNodeGuid||A->EndNodeGuid!=W.EndNodeGuid||A->Thickness!=W.Thickness||A->Height!=W.Height)return Fail(TEXT("DefinitionSourceChanged"));}
	const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(this);
	const auto Plan=UEHBWallTopologyLibrary::BuildWallMoveUpdatePlan(Graph,Draft.UpdatePlan.MovedNodeGuids);
	if(!Plan.bSucceeded||Plan.MovedNodeGuids!=Draft.UpdatePlan.MovedNodeGuids||Plan.JunctionNodeGuids!=Draft.UpdatePlan.JunctionNodeGuids||Plan.WallGuids!=Draft.UpdatePlan.WallGuids)return Fail(TEXT("UpdatePlanChanged"));
	if(!Draft.bWouldChange){Stats.Status=TEXT("NoChange");return Stats;}
	TArray<AEHB_Pillar*> Pillars;TMap<FGuid,AEHB_Wall*> Walls;TSet<FGuid> Requested;
	for(FGuid Id:Plan.JunctionNodeGuids){const auto* B=Actual.PillarBindings.FindByPredicate([&](const auto& V){return V.NodeGuid==Id;});auto* Pillar=B?Cast<AEHB_Pillar>(FindElementActorByGuid(B->PhysicalPillarGuid)):nullptr;if(!Pillar)return Fail(TEXT("MissingPhysicalBinding"));Pillars.Add(Pillar);}
	for(FGuid Id:Plan.WallGuids){auto* Wall=Cast<AEHB_Wall>(FindElementActorByGuid(Id));const auto* D=Draft.Definitions.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Id;});if(!Wall||!D||!Wall->CanApplyNodeDefinition(*D))return Fail(TEXT("UnsupportedWallSource"));Walls.Add(Id,Wall);Requested.Add(Id);}
	TArray<FEHBWallJunctionWallSides> Sides;FName Reason;
	if(!UEHBWallTopologyLibrary::BuildPreparedWallSides(Draft.Definitions,Sides,Reason,&Requested))return Fail(Reason);
	++Stats.DefinitionSolveCalls;
	// Legacy physical footprints read wall reference frames. Publish every final frame first,
	// then restore/rebuild each physical junction once, then generate the wall meshes.
	for(const auto& Side:Sides){auto* Wall=Walls.FindChecked(Side.WallGuid);if(!IsValid(Wall)||Wall->IsActorBeingDestroyed())return Fail(TEXT("SourceChangedDuringApply"));Wall->StageResolvedNodeGeometry(Side,bFinished);}
	for(auto* Pillar:Pillars)
	{
		if(!IsValid(Pillar)||Pillar->IsActorBeingDestroyed())return Fail(TEXT("SourceChangedDuringApply"));
		const auto* Found=PillarConnectionsByPillarGuid.Find(Pillar->ElementGuid);const FEHBPillarWallConnectionList Connections=Found?*Found:FEHBPillarWallConnectionList();
		SyncPillarConnectionData(Pillar,&Connections);Pillar->RebuildPillarMesh();++Stats.PillarsVisited;
	}
	for(const auto& Side:Sides)
	{
		auto* Wall=Walls.FindChecked(Side.WallGuid);const auto* D=Draft.Definitions.Walls.FindByPredicate([&](const auto& V){return V.WallGuid==Side.WallGuid;});
		if(!IsValid(Wall)||Wall->IsActorBeingDestroyed()||!Wall->CanApplyNodeDefinition(*D))return Fail(TEXT("SourceChangedDuringApply"));
		Wall->ApplyResolvedNodeGeometry(Side,bFinished);++Stats.WallRefreshCalls;
	}
	Stats.Status=TEXT("Applied");return Stats;
}

FEHBConnectedWallRefreshStats AEHBBuildingActorBase::RefreshWallMoveNeighborhood(const TArray<FGuid>& MovedPillarGuids,bool bFinished,const FEHBWallMoveUpdatePlan* ExpectedPlan)
{
	TSet<FGuid> Nodes;
	for(FGuid Id:MovedPillarGuids)
	{
		if(!Id.IsValid())continue;
		Nodes.Add(Id);
		if(const auto* Connections=PillarConnectionsByPillarGuid.Find(Id))
			for(const auto& Connection:Connections->Connections)Nodes.Add(Connection.OtherPillarGuid);
	}
	if(ExpectedPlan)
	{
		TSet<FGuid> Roots,Walls;
		for(FGuid Id:MovedPillarGuids)if(Id.IsValid())Roots.Add(Id);
		for(FGuid Id:Nodes)if(const auto* Connections=PillarConnectionsByPillarGuid.Find(Id))
			for(const auto& Connection:Connections->Connections)Walls.Add(Connection.WallGuid);
		auto Matches=[](const TSet<FGuid>& Actual,const TArray<FGuid>& Planned)
		{TSet<FGuid> Unique;for(FGuid Id:Planned){if(!Actual.Contains(Id)||Unique.Contains(Id))return false;Unique.Add(Id);}return Actual.Num()==Planned.Num();};
		if(!ExpectedPlan->bSucceeded||!Matches(Roots,ExpectedPlan->MovedNodeGuids)||!Matches(Nodes,ExpectedPlan->JunctionNodeGuids)||!Matches(Walls,ExpectedPlan->WallGuids))
		{FEHBConnectedWallRefreshStats Rejected;Rejected.bSucceeded=false;return Rejected;}
	}
	return RefreshConnectedWallNodes(MovedPillarGuids,bFinished,&Nodes);
}

FEHBConnectedWallRefreshStats AEHBBuildingActorBase::RefreshConnectedWallNodes(const TArray<FGuid>& PillarGuids,bool bFinished,const TSet<FGuid>* AllowedNodes)
{
	FEHBConnectedWallRefreshStats Stats;
	TSet<FGuid> Visited;
	TArray<FGuid> Pending;
	for(FGuid Root:PillarGuids)
	{
		Pending.Add(Root);
		while(!Pending.IsEmpty())
		{
			const FGuid Id=Pending.Pop(EAllowShrinking::No);
			if(!Id.IsValid()||Visited.Contains(Id)||(AllowedNodes&&!AllowedNodes->Contains(Id)))continue;
			Visited.Add(Id);
			const auto* Found=PillarConnectionsByPillarGuid.Find(Id);
			if(!Found)continue;
			// Mesh notifications can rebuild relationship indexes; do not retain map references across callbacks.
			const FEHBPillarWallConnectionList Connections=*Found;
			++Stats.PillarsVisited;
			if(auto* Pillar=Cast<AEHB_Pillar>(FindElementActorByGuid(Id)))
			{SyncPillarConnectionData(Pillar,&Connections);Pillar->RebuildPillarMesh();}
			for(const auto& Connection:Connections.Connections)
				if(auto* Wall=Cast<AEHB_Wall>(FindElementActorByGuid(Connection.WallGuid)))
				{++Stats.WallRefreshCalls;Wall->RefreshFromConnectedPillars(bFinished);}
			// Reverse push preserves the old depth-first connection order without recursive stack growth.
			for(int32 I=Connections.Connections.Num()-1;I>=0;--I)Pending.Add(Connections.Connections[I].OtherPillarGuid);
		}
	}
	return Stats;
}

void AEHBBuildingActorBase::SyncPillarConnectionData(AEHB_Pillar* Pillar, const FEHBPillarWallConnectionList* ConnectionList) const
{
	if (!Pillar)
	{
		return;
	}

	Pillar->Modify();
	Pillar->ConnectedWallGuids.Reset();
	Pillar->ConnectedPillarGuids.Reset();

	if (!ConnectionList)
	{
		return;
	}

	for (const FEHBPillarWallConnection& Connection : ConnectionList->Connections)
	{
		if (Connection.WallGuid.IsValid())
		{
			Pillar->ConnectedWallGuids.AddUnique(Connection.WallGuid);
		}
		if (Connection.OtherPillarGuid.IsValid())
		{
			Pillar->ConnectedPillarGuids.AddUnique(Connection.OtherPillarGuid);
		}
	}
}

void AEHBBuildingActorBase::RebuildRelationshipIndexes()
{
	RelationIndexByGuid.Reset();
	OutgoingRelationGuidsByElement.Reset();
	IncomingRelationGuidsByElement.Reset();
	OutgoingRelationGuidsByNode.Reset();
	IncomingRelationGuidsByNode.Reset();

	TSet<FGuid> SeenGuids;
	for (int32 Index = 0; Index < ElementRelations.Num(); ++Index)
	{
		FEHBElementRelation& Relation = ElementRelations[Index];
		if (!Relation.RelationGuid.IsValid() || SeenGuids.Contains(Relation.RelationGuid))
		{
			Relation.RelationGuid = FGuid::NewGuid();
		}
		SeenGuids.Add(Relation.RelationGuid);
		AddRelationToIndexes(Relation, Index);
	}
}

void AEHBBuildingActorBase::RebuildFloorIndexFromActors()
{
	FloorElementsByIndex.Reset();
	for (const TPair<FGuid, TWeakObjectPtr<AEHBElementActorBase>>& Pair : ElementActorByGuid)
	{
		const AEHBElementActorBase* Element = Pair.Value.Get();
		if (!Element
			|| Element->FloorRole == EEHBBuildingFloorElementRole::None
			|| (Element->FloorRole != EEHBBuildingFloorElementRole::Foundation && Element->FloorIndex <= 0))
		{
			continue;
		}

		FEHBBuildingFloorElementEntry Entry;
		Entry.ElementGuid = Element->ElementGuid;
		Entry.ElementType = Element->ElementType;
		Entry.FloorIndex = Element->FloorIndex;
		Entry.FloorRole = Element->FloorRole;
		FloorElementsByIndex.FindOrAdd(Element->FloorIndex).Elements.Add(Entry);
	}
}

void AEHBBuildingActorBase::MigrateLegacyRelationships()
{
	auto AddLegacyIfMissing = [this](FEHBElementRelation Relation)
	{
		for (const FEHBElementRelation& Existing : ElementRelations)
		{
			if (Existing.IsEquivalentTo(Relation))
			{
				return;
			}
		}
		Relation.RelationGuid = FGuid::NewGuid();
		Relation.Origin = EEHBRelationOrigin::ImportedLegacy;
		ElementRelations.Add(MoveTemp(Relation));
	};

	for (const TPair<FGuid, FEHBBuildingWallConnection>& Pair : WallConnectionsByWallGuid)
	{
		const FEHBBuildingWallConnection& Connection = Pair.Value;
		if (!Connection.WallGuid.IsValid())
		{
			continue;
		}

		if (Connection.StartPillarGuid.IsValid())
		{
			FEHBElementRelation Relation;
			Relation.Type = EEHBElementRelationType::TopologyConnection;
			Relation.Source = FEHBElementRelationEndpoint::MakeElement(
				Connection.WallGuid,
				EEHBElementSurfaceKind::Start,
				TEXT("Wall.Start"));
			Relation.Target = FEHBElementRelationEndpoint::MakeElement(Connection.StartPillarGuid);
			Relation.bAffectsFloorAssignment = false;
			AddLegacyIfMissing(MoveTemp(Relation));
		}

		if (Connection.EndPillarGuid.IsValid())
		{
			FEHBElementRelation Relation;
			Relation.Type = EEHBElementRelationType::TopologyConnection;
			Relation.Source = FEHBElementRelationEndpoint::MakeElement(
				Connection.WallGuid,
				EEHBElementSurfaceKind::End,
				TEXT("Wall.End"));
			Relation.Target = FEHBElementRelationEndpoint::MakeElement(Connection.EndPillarGuid);
			Relation.bAffectsFloorAssignment = false;
			AddLegacyIfMissing(MoveTemp(Relation));
		}
	}

	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors, true, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		if (const AEHB_Wall* Wall = Cast<AEHB_Wall>(AttachedActor))
		{
			if (Wall->ElementGuid.IsValid() && Wall->StartPillarGuid.IsValid())
			{
				FEHBElementRelation Relation;
				Relation.Type = EEHBElementRelationType::TopologyConnection;
				Relation.Source = FEHBElementRelationEndpoint::MakeElement(
					Wall->ElementGuid,
					EEHBElementSurfaceKind::Start,
					TEXT("Wall.Start"));
				Relation.Target = FEHBElementRelationEndpoint::MakeElement(Wall->StartPillarGuid);
				Relation.bAffectsFloorAssignment = false;
				AddLegacyIfMissing(MoveTemp(Relation));
			}
			if (Wall->ElementGuid.IsValid() && Wall->EndPillarGuid.IsValid())
			{
				FEHBElementRelation Relation;
				Relation.Type = EEHBElementRelationType::TopologyConnection;
				Relation.Source = FEHBElementRelationEndpoint::MakeElement(
					Wall->ElementGuid,
					EEHBElementSurfaceKind::End,
					TEXT("Wall.End"));
				Relation.Target = FEHBElementRelationEndpoint::MakeElement(Wall->EndPillarGuid);
				Relation.bAffectsFloorAssignment = false;
				AddLegacyIfMissing(MoveTemp(Relation));
			}

			for (const FEHBWallDoorWindowConnection& Connection : Wall->DoorWindowConnections)
			{
				if (!Wall->ElementGuid.IsValid() || !Connection.DoorWindowGuid.IsValid())
				{
					continue;
				}
				FEHBElementRelation Relation;
				Relation.Type = EEHBElementRelationType::HostedElement;
				Relation.Source = FEHBElementRelationEndpoint::MakeElement(
					Wall->ElementGuid,
					EEHBElementSurfaceKind::Opening,
					TEXT("Wall.Opening"));
				Relation.Target = FEHBElementRelationEndpoint::MakeElement(Connection.DoorWindowGuid);
				Relation.NumericMetadata.Add(TEXT("DistanceFromWallStart"), Connection.DistanceFromStart);
				Relation.bGeometryDependent = false;
				AddLegacyIfMissing(MoveTemp(Relation));
			}
		}
		else if (const AEHB_DoorWindow* DoorWindow = Cast<AEHB_DoorWindow>(AttachedActor))
		{
			if (!DoorWindow->OwningWallGuid.IsValid() || !DoorWindow->ElementGuid.IsValid())
			{
				continue;
			}
			FEHBElementRelation Relation;
			Relation.Type = EEHBElementRelationType::HostedElement;
			Relation.Source = FEHBElementRelationEndpoint::MakeElement(
				DoorWindow->OwningWallGuid,
				EEHBElementSurfaceKind::Opening,
				TEXT("Wall.Opening"));
			Relation.Target = FEHBElementRelationEndpoint::MakeElement(DoorWindow->ElementGuid);
			Relation.NumericMetadata.Add(TEXT("DistanceFromWallStart"), DoorWindow->DistanceFromWallStart);
			AddLegacyIfMissing(MoveTemp(Relation));
		}
	}
}

void AEHBBuildingActorBase::RebuildLegacyTopologyCachesFromRelationships()
{
	if (bUpdatingPillarWallRelations)
	{
		return;
	}

	TGuardValue<bool> RelationGuard(bUpdatingPillarWallRelations, true);
	if(WallNodeAuthority.Version==2)
	{
		WallConnectionsByWallGuid.Reset();PillarConnectionsByPillarGuid.Reset();
		for(auto* Element:QueryElements(FEHBElementQuery()))if(auto* Wall=Cast<AEHB_Wall>(Element))
		{
			FGuid StartNode,EndNode;int32 Starts=0,Ends=0;
			for(const auto& R:ElementRelations)if(R.bEnabled&&R.Type==EEHBElementRelationType::TopologyConnection&&R.Source.RefersToElement(Wall->ElementGuid)&&R.Target.Kind==EEHBRelationEndpointKind::WallNode&&IsRelationEndpointOwnedByThisBuilding(R.Target))
			{if(R.Source.SurfaceKind==EEHBElementSurfaceKind::Start){StartNode=R.Target.NodeGuid;++Starts;}else if(R.Source.SurfaceKind==EEHBElementSurfaceKind::End){EndNode=R.Target.NodeGuid;++Ends;}}
			Wall->StartPillarGuid=Starts==1?FindPhysicalPillarForNode(StartNode):FGuid();Wall->EndPillarGuid=Ends==1?FindPhysicalPillarForNode(EndNode):FGuid();
			if(Starts!=1||Ends!=1)continue;
			auto& C=WallConnectionsByWallGuid.Add(Wall->ElementGuid);C.WallGuid=Wall->ElementGuid;C.StartPillarGuid=Wall->StartPillarGuid;C.EndPillarGuid=Wall->EndPillarGuid;
			if(C.StartPillarGuid.IsValid()){FEHBPillarWallConnection P;P.WallGuid=C.WallGuid;P.OtherPillarGuid=C.EndPillarGuid;PillarConnectionsByPillarGuid.FindOrAdd(C.StartPillarGuid).Connections.AddUnique(P);}
			if(C.EndPillarGuid.IsValid()){FEHBPillarWallConnection P;P.WallGuid=C.WallGuid;P.OtherPillarGuid=C.StartPillarGuid;PillarConnectionsByPillarGuid.FindOrAdd(C.EndPillarGuid).Connections.AddUnique(P);}
		}
		for(auto* E:QueryElements(FEHBElementQuery()))if(auto* P=Cast<AEHB_Pillar>(E))SyncPillarConnectionData(P,PillarConnectionsByPillarGuid.Find(P->ElementGuid));
		RebuildClosedLoops();return;
	}
	TMap<FGuid, FEHBBuildingWallConnection> RebuiltWallConnections;
	TMap<FGuid,FGuid> PhysicalByNode;
	if(WallNodeOwnership.Version==1)for(const auto& Binding:WallNodeOwnership.Bindings)PhysicalByNode.Add(Binding.NodeGuid,Binding.PhysicalPillarGuid);
	auto ResolveBoundPillar=[&](const FEHBElementRelationEndpoint& Endpoint)->FGuid
	{
		if(!Endpoint.IsValid())return {};
		if(WallNodeOwnership.Version==0&&Endpoint.Kind==EEHBRelationEndpointKind::BuildingElement)return Endpoint.ElementGuid;
		return WallNodeOwnership.Version==1&&Endpoint.Kind==EEHBRelationEndpointKind::WallNode?PhysicalByNode.FindRef(Endpoint.NodeGuid):FGuid();
	};


	for (const FEHBElementRelation& Relation : ElementRelations)
	{
		if (!Relation.bEnabled
			|| Relation.Type != EEHBElementRelationType::TopologyConnection
			|| Relation.Source.Kind != EEHBRelationEndpointKind::BuildingElement
			|| !ResolveBoundPillar(Relation.Target).IsValid())
		{
			continue;
		}

		const AEHB_Wall* Wall = Cast<AEHB_Wall>(FindElementActorByGuid(Relation.Source.ElementGuid));
		const AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(FindElementActorByGuid(ResolveBoundPillar(Relation.Target)));
		if (!Wall
			|| !Pillar
			|| !Wall->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::RoomBoundary))
			|| !Pillar->HasAllCapabilities(static_cast<int32>(EEHBElementCapability::RoomBoundary)))
		{
			continue;
		}

		FEHBBuildingWallConnection& Connection = RebuiltWallConnections.FindOrAdd(Wall->ElementGuid);
		Connection.WallGuid = Wall->ElementGuid;
		if (Relation.Source.SurfaceKind == EEHBElementSurfaceKind::Start)
		{
			Connection.StartPillarGuid = Pillar->ElementGuid;
		}
		else if (Relation.Source.SurfaceKind == EEHBElementSurfaceKind::End)
		{
			Connection.EndPillarGuid = Pillar->ElementGuid;
		}
	}

	for (auto It = RebuiltWallConnections.CreateIterator(); It; ++It)
	{
		if (!It.Value().StartPillarGuid.IsValid() || !It.Value().EndPillarGuid.IsValid())
		{
			It.RemoveCurrent();
		}
	}

	for (const TPair<FGuid, TWeakObjectPtr<AEHBElementActorBase>>& Pair : ElementActorByGuid)
	{
		if (AEHB_Wall* Wall = Cast<AEHB_Wall>(Pair.Value.Get()))
		{
			Wall->Modify();
			Wall->StartPillarGuid.Invalidate();
			Wall->EndPillarGuid.Invalidate();
		}
	}

	WallConnectionsByWallGuid = MoveTemp(RebuiltWallConnections);
	PillarConnectionsByPillarGuid.Reset();
	for (const TPair<FGuid, FEHBBuildingWallConnection>& Pair : WallConnectionsByWallGuid)
	{
		const FEHBBuildingWallConnection& Connection = Pair.Value;

		FEHBPillarWallConnection StartConnection;
		StartConnection.OtherPillarGuid = Connection.EndPillarGuid;
		StartConnection.WallGuid = Connection.WallGuid;
		PillarConnectionsByPillarGuid.FindOrAdd(Connection.StartPillarGuid).Connections.AddUnique(StartConnection);

		FEHBPillarWallConnection EndConnection;
		EndConnection.OtherPillarGuid = Connection.StartPillarGuid;
		EndConnection.WallGuid = Connection.WallGuid;
		PillarConnectionsByPillarGuid.FindOrAdd(Connection.EndPillarGuid).Connections.AddUnique(EndConnection);

		if (AEHB_Wall* Wall = Cast<AEHB_Wall>(FindElementActorByGuid(Connection.WallGuid)))
		{
			Wall->Modify();
			Wall->StartPillarGuid = Connection.StartPillarGuid;
			Wall->EndPillarGuid = Connection.EndPillarGuid;
		}
	}

	for (const TPair<FGuid, TWeakObjectPtr<AEHBElementActorBase>>& Pair : ElementActorByGuid)
	{
		if (AEHB_Pillar* Pillar = Cast<AEHB_Pillar>(Pair.Value.Get()))
		{
			SyncPillarConnectionData(Pillar, PillarConnectionsByPillarGuid.Find(Pillar->ElementGuid));
		}
	}

	RebuildClosedLoops();
}

void AEHBBuildingActorBase::SyncLegacyHostedRelation(const FEHBElementRelation& Relation)
{
	if (!Relation.bEnabled
		|| Relation.Type != EEHBElementRelationType::HostedElement
		|| Relation.Source.Kind != EEHBRelationEndpointKind::BuildingElement
		|| Relation.Target.Kind != EEHBRelationEndpointKind::BuildingElement)
	{
		return;
	}

	AEHB_Wall* HostWall = Cast<AEHB_Wall>(FindElementActorByGuid(Relation.Source.ElementGuid));
	AEHB_DoorWindow* DoorWindow = Cast<AEHB_DoorWindow>(FindElementActorByGuid(Relation.Target.ElementGuid));
	if (!HostWall || !DoorWindow)
	{
		return;
	}

	const double* StoredDistance = Relation.NumericMetadata.Find(TEXT("DistanceFromWallStart"));
	const float DistanceFromStart = StoredDistance
		? static_cast<float>(*StoredDistance)
		: HostWall->CalculateDistanceFromStartForWorldLocation(DoorWindow->GetActorLocation());
	DoorWindow->Modify();
	HostWall->Modify();
	DoorWindow->OwningWallGuid = HostWall->ElementGuid;
	DoorWindow->DistanceFromWallStart = FMath::Max(0.0f, DistanceFromStart);
	HostWall->AddOrUpdateDoorWindowConnection(DoorWindow, DoorWindow->DistanceFromWallStart);
	if (!HostWall->IsActorBeingDestroyed())
	{
		HostWall->RebuildWallMesh();
	}
}

void AEHBBuildingActorBase::AddRelationToIndexes(const FEHBElementRelation& Relation, int32 RelationIndex)
{
	RelationIndexByGuid.Add(Relation.RelationGuid, RelationIndex);
	if (Relation.Source.Kind == EEHBRelationEndpointKind::BuildingElement)
	{
		OutgoingRelationGuidsByElement.Add(Relation.Source.ElementGuid, Relation.RelationGuid);
	}
	else if(Relation.Source.Kind==EEHBRelationEndpointKind::WallNode)OutgoingRelationGuidsByNode.Add(Relation.Source.NodeGuid,Relation.RelationGuid);
	if (Relation.Target.Kind == EEHBRelationEndpointKind::BuildingElement)
	{
		IncomingRelationGuidsByElement.Add(Relation.Target.ElementGuid, Relation.RelationGuid);
	}
	else if(Relation.Target.Kind==EEHBRelationEndpointKind::WallNode)IncomingRelationGuidsByNode.Add(Relation.Target.NodeGuid,Relation.RelationGuid);
}

void AEHBBuildingActorBase::RemoveRelationFromIndexes(const FEHBElementRelation& Relation)
{
	RelationIndexByGuid.Remove(Relation.RelationGuid);
	if (Relation.Source.Kind == EEHBRelationEndpointKind::BuildingElement)
	{
		OutgoingRelationGuidsByElement.RemoveSingle(Relation.Source.ElementGuid, Relation.RelationGuid);
	}
	else if(Relation.Source.Kind==EEHBRelationEndpointKind::WallNode)OutgoingRelationGuidsByNode.RemoveSingle(Relation.Source.NodeGuid,Relation.RelationGuid);
	if (Relation.Target.Kind == EEHBRelationEndpointKind::BuildingElement)
	{
		IncomingRelationGuidsByElement.RemoveSingle(Relation.Target.ElementGuid, Relation.RelationGuid);
	}
	else if(Relation.Target.Kind==EEHBRelationEndpointKind::WallNode)IncomingRelationGuidsByNode.RemoveSingle(Relation.Target.NodeGuid,Relation.RelationGuid);
}

int32 AEHBBuildingActorBase::FindEquivalentRelationIndex(const FEHBElementRelation& Relation) const
{
	for (int32 Index = 0; Index < ElementRelations.Num(); ++Index)
	{
		if (ElementRelations[Index].IsEquivalentTo(Relation))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool AEHBBuildingActorBase::IsRelationEndpointOwnedByThisBuilding(
	const FEHBElementRelationEndpoint& Endpoint) const
{
	if (!Endpoint.IsValid())
	{
		return false;
	}
	// A node ID is never validated by looking up a same-valued legacy pillar ID.
	// Pose authority remains on the bound Actor; node ownership comes only from this registry.
	if(Endpoint.Kind==EEHBRelationEndpointKind::WallNode)
	{
		if(WallNodeAuthority.Version==2)return WallNodeOwnership.Version==1&&WallNodeAuthority.Nodes.ContainsByPredicate([&](const auto& N){return N.NodeGuid==Endpoint.NodeGuid;});
		const auto* Pillar=Cast<AEHB_Pillar>(FindElementActorByGuid(FindPhysicalPillarForNode(Endpoint.NodeGuid)));
		return Pillar&&Pillar->OwningBuilding==this;
	}
	if (Endpoint.Kind != EEHBRelationEndpointKind::BuildingElement)
	{
		return true;
	}

	const AEHBElementActorBase* Element = FindElementActorByGuid(Endpoint.ElementGuid);
	return Element && Element->OwningBuilding == this;
}

bool AEHBBuildingActorBase::IsRelationTypeInFilter(
	EEHBElementRelationType Type,
	const TArray<EEHBElementRelationType>& Filter) const
{
	return Filter.IsEmpty() || Filter.Contains(Type);
}

bool AEHBBuildingActorBase::ShouldTraverseRelation(
	const FEHBElementRelation& Relation,
	const FGuid& CurrentElementGuid,
	EEHBRelationQueryDirection Direction,
	const TArray<EEHBElementRelationType>& RelationTypes,
	FGuid& OutOtherElementGuid) const
{
	OutOtherElementGuid.Invalidate();
	if (!Relation.bEnabled || !IsRelationTypeInFilter(Relation.Type, RelationTypes))
	{
		return false;
	}

	if ((Direction == EEHBRelationQueryDirection::Outgoing
			|| Direction == EEHBRelationQueryDirection::Both)
		&& Relation.Source.RefersToElement(CurrentElementGuid)
		&& Relation.Target.Kind == EEHBRelationEndpointKind::BuildingElement)
	{
		OutOtherElementGuid = Relation.Target.ElementGuid;
		return true;
	}
	if ((Direction == EEHBRelationQueryDirection::Incoming
			|| Direction == EEHBRelationQueryDirection::Both)
		&& Relation.Target.RefersToElement(CurrentElementGuid)
		&& Relation.Source.Kind == EEHBRelationEndpointKind::BuildingElement)
	{
		OutOtherElementGuid = Relation.Source.ElementGuid;
		return true;
	}
	return false;
}

void AEHBBuildingActorBase::AddOrUpdateTopologyRelation(
	const AEHB_Wall* Wall,
	const AEHB_Pillar* Pillar,
	EEHBElementSurfaceKind WallEndpointSurface)
{
	if (!Wall || !Pillar)
	{
		return;
	}

	FEHBElementRelation Relation;
	Relation.Type = EEHBElementRelationType::TopologyConnection;
	Relation.Source = FEHBElementRelationEndpoint::MakeElement(
		Wall->ElementGuid,
		WallEndpointSurface,
		WallEndpointSurface == EEHBElementSurfaceKind::Start ? TEXT("Wall.Start") : TEXT("Wall.End"));
	Relation.Target = FEHBElementRelationEndpoint::MakeElement(Pillar->ElementGuid);
	Relation.Origin = EEHBRelationOrigin::SystemGenerated;
	Relation.bAffectsFloorAssignment = false;
	AddOrUpdateElementRelation(Relation, true);
}

// #endregion 柱子与墙体关系

FGuid AEHBBuildingActorBase::FindNodeForPhysicalPillar(FGuid PillarGuid) const
{
	if(WallNodeOwnership.Version!=1||!PillarGuid.IsValid())return {};
	const auto* Binding=WallNodeOwnership.Bindings.FindByPredicate([&](const auto& Entry){return Entry.PhysicalPillarGuid==PillarGuid;});return Binding?Binding->NodeGuid:FGuid();
}
FGuid AEHBBuildingActorBase::FindPhysicalPillarForNode(FGuid NodeGuid) const
{
	if(WallNodeOwnership.Version!=1||!NodeGuid.IsValid())return {};
	const auto* Binding=WallNodeOwnership.Bindings.FindByPredicate([&](const auto& Entry){return Entry.NodeGuid==NodeGuid;});return Binding?Binding->PhysicalPillarGuid:FGuid();
}
FGuid AEHBBuildingActorBase::ResolveTopologyPillar(const FEHBElementRelationEndpoint& Endpoint) const
{
	if(!Endpoint.IsValid())return {};
	if(WallNodeOwnership.Version==0&&Endpoint.Kind==EEHBRelationEndpointKind::BuildingElement)return Endpoint.ElementGuid;
	if(WallNodeOwnership.Version==1&&Endpoint.Kind==EEHBRelationEndpointKind::WallNode)return FindPhysicalPillarForNode(Endpoint.NodeGuid);
	return {};
}
FEHBTopologyMigrationResult AEHBBuildingActorBase::MigrateWallNodeOwnership(bool bApply)
{
	FEHBTopologyMigrationResult Result;Result.Status=TEXT("UnsupportedOwnershipVersion");
	if(WallNodeOwnership.Version!=0&&WallNodeOwnership.Version!=1)return Result;
	if(WallNodeOwnership.Version==0&&!WallNodeOwnership.Bindings.IsEmpty()){Result.Status=TEXT("UnversionedOwnershipData");return Result;}
	const auto Graph=UEHBWallTopologyLibrary::CaptureWallTopology(this);Result.NodeCount=Graph.Nodes.Num();Result.WallCount=Graph.Walls.Num();
	if(!Graph.Issues.IsEmpty()){Result.Issues=Graph.Issues;Result.Status=TEXT("InvalidTopology");return Result;}
	if(Graph.Nodes.IsEmpty()){Result.Status=TEXT("EmptyTopology");return Result;}
	TSet<FGuid> NodeIds,PillarIds,RelationIds;
	FEHBWallNodeOwnership Candidate;Candidate.Version=1;
	for(const auto& Node:Graph.Nodes)
	{
		if(!Node.NodeGuid.IsValid()||!Node.SourcePillarGuid.IsValid()||NodeIds.Contains(Node.NodeGuid)||PillarIds.Contains(Node.SourcePillarGuid))return Result;
		NodeIds.Add(Node.NodeGuid);PillarIds.Add(Node.SourcePillarGuid);auto& Binding=Candidate.Bindings.AddDefaulted_GetRef();Binding.NodeGuid=Node.NodeGuid;Binding.PhysicalPillarGuid=Node.SourcePillarGuid;
	}
	if(WallNodeOwnership.Version==1&&WallNodeOwnership.Bindings.Num()!=Candidate.Bindings.Num()){Result.Status=TEXT("InvalidNodeOwnership");return Result;}
	TArray<FEHBElementRelation> CandidateRelations=ElementRelations;
	for(auto& Relation:CandidateRelations)
	{
		if(!Relation.RelationGuid.IsValid()||RelationIds.Contains(Relation.RelationGuid)||!Relation.Source.IsValid()||!Relation.Target.IsValid()||!IsRelationEndpointOwnedByThisBuilding(Relation.Source)||!IsRelationEndpointOwnedByThisBuilding(Relation.Target))
		{Result.Status=TEXT("InvalidRelationRecord");return Result;}RelationIds.Add(Relation.RelationGuid);
		if(Relation.Type!=EEHBElementRelationType::TopologyConnection)continue;
		const FGuid Physical=ResolveTopologyPillar(Relation.Target);
		if(Relation.Source.Kind!=EEHBRelationEndpointKind::BuildingElement||!Cast<AEHB_Wall>(FindElementActorByGuid(Relation.Source.ElementGuid))
			||!Relation.Target.ExternalActor.IsNull()||!PillarIds.Contains(Physical)||(Relation.Source.SurfaceKind!=EEHBElementSurfaceKind::Start&&Relation.Source.SurfaceKind!=EEHBElementSurfaceKind::End))
		{Result.Status=TEXT("InvalidTopologyRecord");return Result;}
		if(WallNodeOwnership.Version==0)
		{
			Relation.Target.Kind=EEHBRelationEndpointKind::WallNode;Relation.Target.NodeGuid=Physical;Relation.Target.ElementGuid.Invalidate();
			// Geometry remains on the physical pillar; preserve the recorded revision and all metadata.
		}
	}
	Result.bSucceeded=true;Result.Status=WallNodeOwnership.Version==1?TEXT("AlreadyMigrated"):TEXT("Ready");
	if(!bApply||WallNodeOwnership.Version==1)return Result;
	if(RelationshipGraphRevision==MAX_int32){Result.bSucceeded=false;Result.Status=TEXT("RelationshipRevisionOverflow");return Result;}
	Modify();WallNodeOwnership=MoveTemp(Candidate);ElementRelations=MoveTemp(CandidateRelations);++RelationshipGraphRevision;
	RebuildRelationshipIndexes();RebuildLegacyTopologyCachesFromRelationships();MarkPackageDirty();
	Result.bChanged=true;Result.Status=TEXT("Migrated");return Result;
}

void AEHBBuildingActorBase::RegisterAuthoredWallNode(AEHB_Pillar* Pillar)
{
#if WITH_EDITORONLY_DATA
	if(GIsTransacting)return;
#endif
	if(FEHBActorImportScope::IsActive())return;
	if(WallNodeOwnership.Version!=1||!Pillar||Pillar->OwningBuilding!=this||Pillar->IsActorBeingDestroyed()||!Pillar->ElementGuid.IsValid()||FindNodeForPhysicalPillar(Pillar->ElementGuid).IsValid())return;
	Modify();auto& Binding=WallNodeOwnership.Bindings.AddDefaulted_GetRef();Binding.NodeGuid=Pillar->ElementGuid;Binding.PhysicalPillarGuid=Pillar->ElementGuid;
	if(HasWallNodeAuthority())
	{
		auto& Node=WallNodeAuthority.Nodes.AddDefaulted_GetRef();Node.NodeGuid=Binding.NodeGuid;
		Node.LocalTransform=Pillar->GetElementLocalTransform();Node.FloorIndex=Pillar->FloorIndex;Node.JunctionDimensions=FVector(Pillar->Width,Pillar->Depth,Pillar->Height);
	}
	MarkPackageDirty();
}

FEHBTopologyMigrationResult AEHBBuildingActorBase::MigrateWallNodeAuthority(bool bApply)
{
	FEHBTopologyMigrationResult Result;
	if(WallNodeAuthority.Version!=0&&!HasWallNodeAuthority()){Result.Status=TEXT("UnsupportedNodeAuthorityVersion");return Result;}
	if(WallNodeOwnership.Version!=1){Result.Status=TEXT("RequiresTypedNodeOwnership");return Result;}
	FEHBPreparedWallNodeDefinitions Source;
	Result=UEHBWallTopologyLibrary::CaptureNodeDefinitionSource(this,Source);if(!Result.bSucceeded)return Result;
	Result.Status=HasWallNodeAuthority()?TEXT("AlreadyMigrated"):TEXT("Ready");
	if(!bApply||HasWallNodeAuthority())return Result;
	Modify();WallNodeAuthority.Version=1;WallNodeAuthority.Nodes=MoveTemp(Source.Nodes);MarkPackageDirty();
	Result.bChanged=true;Result.Status=TEXT("Migrated");return Result;
}

void AEHBBuildingActorBase::RecordAuthoredWallNode(AEHB_Pillar* Pillar)
{
	// Undo/redo and text import restore serialized node values. Restoration callbacks
	// are not authored edits, even when UE reports a rounded proxy transform as moved.
#if WITH_EDITORONLY_DATA
	if(GIsTransacting)return;
#endif
	if(FEHBActorImportScope::IsActive())return;
	if(!HasWallNodeAuthority()||!IsValid(Pillar)||Pillar->OwningBuilding!=this||Pillar->IsActorBeingDestroyed())return;
	const FGuid Id=FindNodeForPhysicalPillar(Pillar->ElementGuid);
	auto* Node=WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;});
	// A missing record is corruption, not an invitation to silently initialize on edit.
	if(!Node)return;
	const FTransform Pose=Pillar->GetElementLocalTransform();const FVector Dimensions(Pillar->Width,Pillar->Depth,Pillar->Height);
	if(Node->LocalTransform.GetLocation()==Pose.GetLocation()&&Node->LocalTransform.GetRotation().Equals(Pose.GetRotation(),1.e-12)
		&&Node->LocalTransform.GetScale3D()==Pose.GetScale3D()&&Node->FloorIndex==Pillar->FloorIndex&&Node->JunctionDimensions==Dimensions)return;
	if(Node->GeometryRevision==MAX_int32)return;
	Modify();Node->LocalTransform=Pose;Node->FloorIndex=Pillar->FloorIndex;Node->JunctionDimensions=Dimensions;++Node->GeometryRevision;
	bClosedLoopsNeedRefresh=true;MarkPackageDirty();
}

bool AEHBBuildingActorBase::TryGetRoomBoundary(FGuid RoomGuid,int32 FloorIndex,FEHBNodeRoomBoundary& Boundary) const
{
	Boundary={};EnsureClosedLoopsCurrent();
	const auto* Loop=ClosedLoops.FindByPredicate([&](const auto& R){return R.LoopGuid==RoomGuid&&R.FloorIndex==FloorIndex;});
	const auto* Cycle=ClosedLoopNodeCycles.Find(RoomGuid);
	if(!Loop||!Cycle||Cycle->Num()<3||Cycle->Num()!=Loop->WallGuids.Num())return false;
	FEHBNodeRoomBoundary Result;Result.RoomGuid=RoomGuid;Result.FloorIndex=FloorIndex;Result.NodeGuids=*Cycle;Result.WallGuids=Loop->WallGuids;
	for(FGuid Id:*Cycle)
	{
		if(HasWallNodeAuthority())
		{
			const auto* Node=WallNodeAuthority.Nodes.FindByPredicate([&](const auto& N){return N.NodeGuid==Id;});if(!Node||Node->FloorIndex!=FloorIndex)return false;
			Result.Polygon.Add(Node->LocalTransform.GetLocation());
		}
		else
		{
			const FGuid Physical=WallNodeOwnership.Version==1?FindPhysicalPillarForNode(Id):Id;
			const auto* Pillar=Cast<AEHB_Pillar>(FindElementActorByGuid(Physical));if(!Pillar)return false;
			Result.Polygon.Add(Pillar->GetElementLocalTransform().GetLocation());
		}
	}
	for(int32 I=0;I<Result.Polygon.Num();++I){const auto& A=Result.Polygon[I];const auto& Z=Result.Polygon[(I+1)%Result.Polygon.Num()];Result.Area+=A.X*Z.Y-A.Y*Z.X;}
	Result.Area=FMath::Abs(Result.Area)*0.5;Boundary=MoveTemp(Result);return true;
}
