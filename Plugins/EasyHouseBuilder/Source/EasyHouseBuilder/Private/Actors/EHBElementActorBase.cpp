// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/EHBElementActorBase.h"
#include "Core/EHBActorImportScope.h"
#include "Actors/EHB_Pillar.h"

#include "Components/EHBGeneratedMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Core/EHBBuildingActorBase.h"
#include "Cutting/EHBGeneratedMeshCollector.h"

AEHBElementActorBase::AEHBElementActorBase()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void AEHBElementActorBase::PostActorCreated()
{
	Super::PostActorCreated();
	EnsureElementGuid();
	RefreshLogicalSurfaceIdentities();
	CachedPreEditLocalTransform = GetElementLocalTransform();
	if (OwningBuilding)
	{
		OwningBuilding->RegisterElementActor(this);
	}
}

void AEHBElementActorBase::PostLoad()
{
	Super::PostLoad();
	EnsureElementGuid();
	RefreshLogicalSurfaceIdentities();
	if (FloorRole != EEHBBuildingFloorElementRole::None
		&& FloorAssignmentSource == EEHBFloorAssignmentSource::Unassigned)
	{
		FloorAssignmentSource = EEHBFloorAssignmentSource::ImportedLegacy;
		FloorAssignmentPolicy = EEHBFloorAssignmentPolicy::Explicit;
	}
	CachedPreEditLocalTransform = GetElementLocalTransform();
}

void AEHBElementActorBase::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	RefreshLogicalSurfaceIdentities();
	if (OwningBuilding && !HasAnyFlags(RF_ClassDefaultObject))
	{
		OwningBuilding->RegisterElementActor(this);
		if(OwningBuilding->WallNodeAuthority.Version==2)OwningBuilding->RebuildWallNodeAuthorityGeometry();
	}
}

void AEHBElementActorBase::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (DuplicateMode != EDuplicateMode::PIE && !FEHBActorImportScope::IsActive())
	{
		RegenerateElementGuid();
		if (OwningBuilding)
		{
			OwningBuilding->RegisterElementActor(this);
		}
	}
}

void AEHBElementActorBase::Destroyed()
{
	NotifyElementActorDeleted();
	if (OwningBuilding)
	{
		OwningBuilding->UnregisterElementActor(this);
	}
	Super::Destroyed();
}

#if WITH_EDITOR
void AEHBElementActorBase::PreEditChange(FProperty* PropertyAboutToChange)
{
	CachedPreEditLocalTransform = GetElementLocalTransform();
	Super::PreEditChange(PropertyAboutToChange);
}

void AEHBElementActorBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (FEHBActorImportScope::IsActive()) { Super::PostEditChangeProperty(PropertyChangedEvent); return; }
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ChangedPropertyName = PropertyChangedEvent.GetPropertyName();
	if (ChangedPropertyName == GET_MEMBER_NAME_CHECKED(AEHBElementActorBase, FloorIndex)
		|| ChangedPropertyName == GET_MEMBER_NAME_CHECKED(AEHBElementActorBase, FloorRole))
	{
		FloorIndex = FMath::Max(0, FloorIndex);
		if (FloorRole != EEHBBuildingFloorElementRole::Foundation && FloorIndex <= 0)
		{
			FloorIndex = 0;
			FloorRole = EEHBBuildingFloorElementRole::None;
		}

		if (OwningBuilding)
		{
			OwningBuilding->RecordAuthoredWallNode(Cast<AEHB_Pillar>(this));
			OwningBuilding->RegisterElementActor(this);
		}

		FloorAssignmentPolicy = EEHBFloorAssignmentPolicy::Explicit;
		FloorAssignmentSource = FloorRole == EEHBBuildingFloorElementRole::None
			? EEHBFloorAssignmentSource::Unassigned
			: EEHBFloorAssignmentSource::Explicit;
		bFloorAssignmentConflict = false;
		ConflictingFloorCandidates.Reset();
	}
	else if (ChangedPropertyName != GET_MEMBER_NAME_CHECKED(AEHBElementActorBase, ElementName)
		&& ChangedPropertyName != GET_MEMBER_NAME_CHECKED(AEHBElementActorBase, SemanticTags)
		&& ChangedPropertyName != GET_MEMBER_NAME_CHECKED(AEHBElementActorBase, FloorAssignmentPolicy))
	{
		if(OwningBuilding)OwningBuilding->RecordAuthoredWallNode(Cast<AEHB_Pillar>(this));
		NotifyElementGeometryChanged(true);
	}
}

void AEHBElementActorBase::PostEditMove(bool bFinished)
{
	if (FEHBActorImportScope::IsActive()) { Super::PostEditMove(bFinished); CachedPreEditLocalTransform = GetElementLocalTransform(); return; }
	Super::PostEditMove(bFinished);

	if (!bNotifyWhenMovedInEditor)
	{
		return;
	}

	const FTransform NewLocalTransform = GetElementLocalTransform();
	if (!CachedPreEditLocalTransform.Equals(NewLocalTransform))
	{
		NotifyElementActorMoved(CachedPreEditLocalTransform, NewLocalTransform, bFinished);
		if (bFinished)
		{
			CachedPreEditLocalTransform = GetElementLocalTransform();
		}
	}
}

void AEHBElementActorBase::SynchronizePlannedEditorMove()
{
	CachedPreEditLocalTransform=GetElementLocalTransform();
}

void AEHBElementActorBase::PostEditUndo()
{
	Super::PostEditUndo();
	if(!IsActorBeingDestroyed())bHasNotifiedElementActorDeleted=false;
	EnsureElementGuid();
	RefreshLogicalSurfaceIdentities();
	CachedPreEditLocalTransform = GetElementLocalTransform();
	if (OwningBuilding)
	{
		OwningBuilding->RegisterElementActor(this);
		OwningBuilding->RebuildElementAndRelationshipIndexes();
	}
}
#endif

void AEHBElementActorBase::EnsureElementGuid()
{
	if (!ElementGuid.IsValid())
	{
		ElementGuid = FGuid::NewGuid();
	}
}

void AEHBElementActorBase::RegenerateElementGuid()
{
	ElementGuid = FGuid::NewGuid();
	RefreshLogicalSurfaceIdentities();
	MarkPackageDirty();
}

FEHBBuildingElementHandle AEHBElementActorBase::GetElementHandle() const
{
	FEHBBuildingElementHandle Handle;
	Handle.ElementGuid = ElementGuid;
	Handle.ElementType = ElementType;
	return Handle;
}

void AEHBElementActorBase::AttachToBuilding(AEHBBuildingActorBase* InOwningBuilding, const FTransform& LocalTransform)
{
	if (OwningBuilding && OwningBuilding != InOwningBuilding)
	{
		OwningBuilding->UnregisterElementActor(this);
	}

	OwningBuilding = InOwningBuilding;
	if (!OwningBuilding)
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		SetActorTransform(LocalTransform);
		return;
	}

	AttachToActor(OwningBuilding, FAttachmentTransformRules::KeepWorldTransform);
	SetActorRelativeTransform(LocalTransform);
	OwningBuilding->RegisterElementActor(this);
	OwningBuilding->RegisterAuthoredWallNode(Cast<AEHB_Pillar>(this));
}

void AEHBElementActorBase::SetFloorAssignment(int32 InFloorIndex, EEHBBuildingFloorElementRole InFloorRole)
{
	EnsureElementGuid();

	FloorIndex = FMath::Max(0, InFloorIndex);
	const bool bValidFoundationAssignment = InFloorRole == EEHBBuildingFloorElementRole::Foundation;
	const bool bValidRegularAssignment = InFloorRole != EEHBBuildingFloorElementRole::None && FloorIndex > 0;
	FloorRole = (bValidFoundationAssignment || bValidRegularAssignment)
		? InFloorRole
		: EEHBBuildingFloorElementRole::None;
	FloorAssignmentPolicy = EEHBFloorAssignmentPolicy::Explicit;
	FloorAssignmentSource = FloorRole == EEHBBuildingFloorElementRole::None
		? EEHBFloorAssignmentSource::Unassigned
		: EEHBFloorAssignmentSource::Explicit;
	bFloorAssignmentConflict = false;
	ConflictingFloorCandidates.Reset();

	if (FloorRole == EEHBBuildingFloorElementRole::None || (FloorRole != EEHBBuildingFloorElementRole::Foundation && FloorIndex <= 0))
	{
		FloorIndex = 0;
		FloorRole = EEHBBuildingFloorElementRole::None;
	}

	if (OwningBuilding)
	{
		OwningBuilding->RecordAuthoredWallNode(Cast<AEHB_Pillar>(this));
		OwningBuilding->RegisterElementActor(this);
	}

	MarkPackageDirty();
}

void AEHBElementActorBase::SetAutomaticFloorAssignment()
{
	FloorAssignmentPolicy = EEHBFloorAssignmentPolicy::Automatic;
	FloorAssignmentSource = EEHBFloorAssignmentSource::Unassigned;
	bFloorAssignmentConflict = false;
	ConflictingFloorCandidates.Reset();
	if (OwningBuilding)
	{
		OwningBuilding->ResolveAutomaticFloorAssignments();
	}
	MarkPackageDirty();
}

void AEHBElementActorBase::ApplyDerivedFloorAssignment(
	int32 InFloorIndex,
	EEHBBuildingFloorElementRole InFloorRole,
	const TArray<int32>& InConflictingCandidates)
{
	if (FloorAssignmentPolicy != EEHBFloorAssignmentPolicy::Automatic)
	{
		return;
	}

	TArray<int32> Candidates = InConflictingCandidates;Candidates.Sort();
	const auto Source = InFloorRole == EEHBBuildingFloorElementRole::None
		? EEHBFloorAssignmentSource::Unassigned
		: EEHBFloorAssignmentSource::DerivedFromSupport;
	if(FloorIndex==FMath::Max(0,InFloorIndex)&&FloorRole==InFloorRole&&FloorAssignmentSource==Source&&ConflictingFloorCandidates==Candidates&&bFloorAssignmentConflict==(Candidates.Num()>1))return;
	Modify();
	FloorIndex = FMath::Max(0, InFloorIndex);
	FloorRole = InFloorRole;
	FloorAssignmentSource = Source;
	ConflictingFloorCandidates = MoveTemp(Candidates);
	bFloorAssignmentConflict = ConflictingFloorCandidates.Num() > 1;
	MarkPackageDirty();
}

bool AEHBElementActorBase::ClearDerivedFloorAssignment()
{
 if(FloorAssignmentPolicy!=EEHBFloorAssignmentPolicy::Automatic||FloorAssignmentSource!=EEHBFloorAssignmentSource::DerivedFromSupport)return false;
 Modify();FloorIndex=0;FloorRole=EEHBBuildingFloorElementRole::None;FloorAssignmentSource=EEHBFloorAssignmentSource::Unassigned;
 ConflictingFloorCandidates.Reset();bFloorAssignmentConflict=false;MarkPackageDirty();return true;
}

bool AEHBElementActorBase::HasAllCapabilities(int32 RequiredCapabilities) const
{
	return (ElementCapabilities & RequiredCapabilities) == RequiredCapabilities;
}

FBox AEHBElementActorBase::GetBuildingLocalBounds() const
{
	const FBox WorldBounds = GetComponentsBoundingBox(true);
	if (!WorldBounds.IsValid)
	{
		return FBox(ForceInit);
	}

	const FTransform WorldToBuilding = OwningBuilding
		? OwningBuilding->GetActorTransform().Inverse()
		: FTransform::Identity;
	FBox LocalBounds(ForceInit);
	for (int32 XIndex = 0; XIndex < 2; ++XIndex)
	{
		for (int32 YIndex = 0; YIndex < 2; ++YIndex)
		{
			for (int32 ZIndex = 0; ZIndex < 2; ++ZIndex)
			{
				LocalBounds += WorldToBuilding.TransformPosition(FVector(
					XIndex == 0 ? WorldBounds.Min.X : WorldBounds.Max.X,
					YIndex == 0 ? WorldBounds.Min.Y : WorldBounds.Max.Y,
					ZIndex == 0 ? WorldBounds.Min.Z : WorldBounds.Max.Z));
			}
		}
	}
	return LocalBounds;
}

void AEHBElementActorBase::GetGeneratedMeshComponents(TArray<UEHBGeneratedMeshComponent*>& OutComponents) const
{
	OutComponents.Reset();
	GetComponents(OutComponents);
}

bool AEHBElementActorBase::BuildMeshAggregateData(
	const FTransform& TargetLocalToWorld,
	FEHBMeshAggregateData& OutData) const
{
	TArray<UEHBGeneratedMeshComponent*> Components;
	GetGeneratedMeshComponents(Components);
	return FEHBGeneratedMeshCollector::CollectFromComponents(
		Components,
		TargetLocalToWorld,
		OutData,
		ElementGuid);
}

bool AEHBElementActorBase::AddCutOperation(FEHBCutOperation Operation)
{
	Operation.EnsureGuids();
	Modify();
	CutOperations.Add(MoveTemp(Operation));
	MarkPackageDirty();
	NotifyElementGeometryChanged(true);
	return true;
}

bool AEHBElementActorBase::RemoveCutOperation(const FGuid& OperationGuid)
{
	if (!OperationGuid.IsValid())
	{
		return false;
	}

	Modify();
	const int32 RemovedCount = CutOperations.RemoveAll(
		[OperationGuid](const FEHBCutOperation& Operation)
		{
			return Operation.OperationGuid == OperationGuid;
		});
	if (RemovedCount <= 0)
	{
		return false;
	}

	MarkPackageDirty();
	NotifyElementGeometryChanged(true);
	return true;
}

bool AEHBElementActorBase::UpdateCutOperation(const FEHBCutOperation& Operation)
{
	if (!Operation.OperationGuid.IsValid())
	{
		return false;
	}

	FEHBCutOperation* ExistingOperation = FindCutOperation(Operation.OperationGuid);
	if (!ExistingOperation)
	{
		return false;
	}

	Modify();
	*ExistingOperation = Operation;
	ExistingOperation->EnsureGuids();
	MarkPackageDirty();
	NotifyElementGeometryChanged(true);
	return true;
}

FEHBCutOperation* AEHBElementActorBase::FindCutOperation(const FGuid& OperationGuid)
{
	if (!OperationGuid.IsValid())
	{
		return nullptr;
	}

	return CutOperations.FindByPredicate(
		[OperationGuid](const FEHBCutOperation& Operation)
		{
			return Operation.OperationGuid == OperationGuid;
		});
}

const FEHBCutOperation* AEHBElementActorBase::FindCutOperation(const FGuid& OperationGuid) const
{
	if (!OperationGuid.IsValid())
	{
		return nullptr;
	}

	return CutOperations.FindByPredicate(
		[OperationGuid](const FEHBCutOperation& Operation)
		{
			return Operation.OperationGuid == OperationGuid;
		});
}

bool AEHBElementActorBase::RemoveCutPolygonPoint(const FGuid& OperationGuid, const FGuid& PointGuid)
{
	FEHBCutOperation* Operation = FindCutOperation(OperationGuid);
	if (!Operation || !PointGuid.IsValid())
	{
		return false;
	}

	Modify();
	FEHBCutPolygon& Polygon = Operation->Source.ExplicitPolygon;
	const int32 RemovedCount = Polygon.Points.RemoveAll(
		[PointGuid](const FEHBCutPolygonPoint& Point)
		{
			return Point.PointGuid == PointGuid;
		});
	if (RemovedCount <= 0)
	{
		return false;
	}

	if (Polygon.Points.Num() < 3)
	{
		CutOperations.RemoveAll(
			[OperationGuid](const FEHBCutOperation& Candidate)
			{
				return Candidate.OperationGuid == OperationGuid;
			});
	}

	MarkPackageDirty();
	NotifyElementGeometryChanged(true);
	return true;
}

bool AEHBElementActorBase::UpdateCutPolygonPoint(
	const FGuid& OperationGuid,
	const FGuid& PointGuid,
	const FVector& NewLocalPosition)
{
	FEHBCutOperation* Operation = FindCutOperation(OperationGuid);
	if (!Operation || !PointGuid.IsValid())
	{
		return false;
	}

	FEHBCutPolygonPoint* Point = Operation->Source.ExplicitPolygon.Points.FindByPredicate(
		[PointGuid](const FEHBCutPolygonPoint& Candidate)
		{
			return Candidate.PointGuid == PointGuid;
		});
	if (!Point)
	{
		return false;
	}

	Modify();
	Point->LocalPosition = NewLocalPosition;
	MarkPackageDirty();
	NotifyElementGeometryChanged(true);
	return true;
}

TArray<FEHBElementRelation> AEHBElementActorBase::GetElementRelations(
	EEHBRelationQueryDirection Direction,
	const TArray<EEHBElementRelationType>& Types,
	bool bIncludeStale) const
{
	if (!OwningBuilding || !ElementGuid.IsValid())
	{
		return {};
	}

	FEHBRelationQuery Query;
	Query.ElementGuid = ElementGuid;
	Query.Direction = Direction;
	Query.Types = Types;
	Query.bIncludeStale = bIncludeStale;
	return OwningBuilding->QueryElementRelations(Query);
}

TArray<AEHBElementActorBase*> AEHBElementActorBase::GetRelatedElements(
	EEHBRelationQueryDirection Direction,
	const TArray<EEHBElementRelationType>& Types,
	bool bRecursive,
	int32 MaxDepth) const
{
	if (!OwningBuilding || !ElementGuid.IsValid())
	{
		return {};
	}
	return OwningBuilding->GetRelatedElementActors(
		ElementGuid,
		Direction,
		Types,
		bRecursive,
		MaxDepth);
}

TArray<AEHBElementActorBase*> AEHBElementActorBase::GetSupporters() const
{
	return OwningBuilding && ElementGuid.IsValid()
		? OwningBuilding->GetElementSupporters(ElementGuid)
		: TArray<AEHBElementActorBase*>();
}

TArray<AEHBElementActorBase*> AEHBElementActorBase::GetSupportedElements(
	bool bRecursive,
	int32 MaxDepth) const
{
	return OwningBuilding && ElementGuid.IsValid()
		? OwningBuilding->GetElementsSupportedBy(ElementGuid, bRecursive, MaxDepth)
		: TArray<AEHBElementActorBase*>();
}

TArray<AEHBElementActorBase*> AEHBElementActorBase::GetAffectedElements(
	bool bRecursive,
	int32 MaxDepth) const
{
	return OwningBuilding && ElementGuid.IsValid()
		? OwningBuilding->GetElementsAffectedBy(ElementGuid, bRecursive, MaxDepth)
		: TArray<AEHBElementActorBase*>();
}

FTransform AEHBElementActorBase::GetElementLocalTransform() const
{
	return RootComponent ? RootComponent->GetRelativeTransform() : FTransform::Identity;
}

void AEHBElementActorBase::SetElementLocalTransform(const FTransform& NewLocalTransform, bool bFinished)
{
	const FTransform OldLocalTransform = GetElementLocalTransform();
	SetActorRelativeTransform(NewLocalTransform);
	if(OwningBuilding)OwningBuilding->RecordAuthoredWallNode(Cast<AEHB_Pillar>(this));

	if (!OldLocalTransform.Equals(NewLocalTransform))
	{
		NotifyElementActorMoved(OldLocalTransform, NewLocalTransform, bFinished);
	}
}

void AEHBElementActorBase::NotifyElementActorMoved(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished)
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	if(OwningBuilding)OwningBuilding->RecordAuthoredWallNode(Cast<AEHB_Pillar>(this));
	OnElementActorMoved(OldLocalTransform, NewLocalTransform, bFinished);
}

void AEHBElementActorBase::NotifyElementActorDeleted()
{
	if (HasAnyFlags(RF_ClassDefaultObject) || bHasNotifiedElementActorDeleted)
	{
		return;
	}

	bHasNotifiedElementActorDeleted = true;
	OnElementActorDeleted();
}

void AEHBElementActorBase::NotifyElementGeometryChanged(bool bFinished)
{
	if (OwningBuilding && ElementGuid.IsValid() && !HasAnyFlags(RF_ClassDefaultObject))
	{
		OwningBuilding->NotifyElementGeometryChanged(ElementGuid, bFinished);
	}
}

void AEHBElementActorBase::OnElementActorMoved_Implementation(const FTransform& OldLocalTransform, const FTransform& NewLocalTransform, bool bFinished)
{
	NotifyElementGeometryChanged(bFinished);
}

void AEHBElementActorBase::OnElementActorDeleted_Implementation()
{
}
