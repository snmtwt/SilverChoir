// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/EHBArchitecturalSurfaceComponent.h"

#include "Actors/EHBElementActorBase.h"
#include "Core/EHBBuildingActorBase.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

UEHBArchitecturalSurfaceComponent::UEHBArchitecturalSurfaceComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionObjectType(ECC_WorldStatic);
	SetCollisionResponseToAllChannels(ECR_Block);
	ComponentTags.AddUnique(TEXT("EHB_Surface"));
}

void UEHBArchitecturalSurfaceComponent::InitializeSurface(
	AEHBElementActorBase* InOwnerElement,
	EEHBArchitecturalSurfaceRole InRole,
	FName InSurfaceName,
	int32 InSubIndex)
{
	OwnerElement = InOwnerElement ? InOwnerElement : Cast<AEHBElementActorBase>(GetOwner());
	OwningBuilding = OwnerElement ? OwnerElement->OwningBuilding : nullptr;
	OwnerElementGuid = OwnerElement ? OwnerElement->ElementGuid : FGuid();
	SurfaceRole = InRole;
	SurfaceName = InSurfaceName;
	SurfaceSubIndex = InSubIndex;
	EnsureSurfaceGuid();

	if (bEnableSurfaceCollision)
	{
		SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}

bool UEHBArchitecturalSurfaceComponent::RebuildSurfaceMesh()
{
	return false;
}

void UEHBArchitecturalSurfaceComponent::ClearSurfaceMesh()
{
	ClearAllMeshSections();
	NotifySurfaceGeometryChanged(true);
}

FEHBElementRelationEndpoint UEHBArchitecturalSurfaceComponent::MakeRelationEndpoint() const
{
	return FEHBElementRelationEndpoint::MakeElement(
		OwnerElementGuid,
		GetElementSurfaceKind(),
		SurfaceName,
		SurfaceSubIndex);
}

FEHBSurfaceEndpoint UEHBArchitecturalSurfaceComponent::MakeSurfaceEndpoint() const
{
	FEHBSurfaceEndpoint Endpoint;
	Endpoint.OwnerElementGuid = OwnerElementGuid;
	Endpoint.SurfaceKind = GetElementSurfaceKind();
	Endpoint.SurfaceName = SurfaceName;
	Endpoint.SubIndex = SurfaceSubIndex;
	return Endpoint;
}

EEHBElementSurfaceKind UEHBArchitecturalSurfaceComponent::GetElementSurfaceKind() const
{
	switch (SurfaceRole)
	{
	case EEHBArchitecturalSurfaceRole::WallLeftSide:
		return EEHBElementSurfaceKind::LeftSide;
	case EEHBArchitecturalSurfaceRole::WallRightSide:
		return EEHBElementSurfaceKind::RightSide;
	case EEHBArchitecturalSurfaceRole::WallCap:
	case EEHBArchitecturalSurfaceRole::PillarTop:
	case EEHBArchitecturalSurfaceRole::SlabTop:
	case EEHBArchitecturalSurfaceRole::FloorFinishTop:
	case EEHBArchitecturalSurfaceRole::RoofTop:
		return EEHBElementSurfaceKind::Top;
	case EEHBArchitecturalSurfaceRole::SlabBottom:
		return EEHBElementSurfaceKind::Bottom;
	case EEHBArchitecturalSurfaceRole::WallOpeningReveal:
	case EEHBArchitecturalSurfaceRole::SlabHoleSide:
		return EEHBElementSurfaceKind::Opening;
	case EEHBArchitecturalSurfaceRole::PillarSide:
	case EEHBArchitecturalSurfaceRole::SlabOuterSide:
	case EEHBArchitecturalSurfaceRole::RoofGableEnd:
	case EEHBArchitecturalSurfaceRole::RoofSideCap:
	case EEHBArchitecturalSurfaceRole::RoofLinearTrim:
	case EEHBArchitecturalSurfaceRole::RailingPanelSide:
		return EEHBElementSurfaceKind::Side;
	default:
		return EEHBElementSurfaceKind::Custom;
	}
}

FTransform UEHBArchitecturalSurfaceComponent::GetSurfaceToElementTransform() const
{
	return GetRelativeTransform();
}

FTransform UEHBArchitecturalSurfaceComponent::GetSurfaceToBuildingTransform() const
{
	if (!OwningBuilding)
	{
		return GetSurfaceToWorldTransform();
	}
	return GetSurfaceToWorldTransform().GetRelativeTransform(OwningBuilding->GetActorTransform());
}

FTransform UEHBArchitecturalSurfaceComponent::GetSurfaceToWorldTransform() const
{
	return GetComponentTransform();
}

bool UEHBArchitecturalSurfaceComponent::ProjectWorldPointToSurface(
	const FVector& WorldPoint,
	FVector& OutWorldPoint,
	FVector2D& OutSurfaceUV) const
{
	OutWorldPoint = WorldPoint;
	OutSurfaceUV = FVector2D::ZeroVector;
	return false;
}

bool UEHBArchitecturalSurfaceComponent::FindClosestPointOnSurface(
	const FVector& WorldPoint,
	FVector& OutWorldPoint,
	float& OutDistance) const
{
	FVector2D SurfaceUV;
	if (!ProjectWorldPointToSurface(WorldPoint, OutWorldPoint, SurfaceUV))
	{
		OutDistance = 0.0f;
		return false;
	}

	OutDistance = FVector::Distance(WorldPoint, OutWorldPoint);
	return true;
}

bool UEHBArchitecturalSurfaceComponent::BuildSnapCandidates(
	const FVector& WorldPoint,
	TArray<FEHBSurfaceSnapCandidate>& OutCandidates) const
{
	if (!bCanBeSnapTarget)
	{
		return false;
	}

	FVector ClosestPoint;
	float Distance = 0.0f;
	if (!FindClosestPointOnSurface(WorldPoint, ClosestPoint, Distance))
	{
		return false;
	}

	FEHBSurfaceSnapCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
	Candidate.SnapKind = EEHBSurfaceSnapKind::Plane;
	Candidate.WorldLocation = ClosestPoint;
	Candidate.WorldNormal = GetUpVector();
	Candidate.Distance = Distance;
	Candidate.Endpoint = MakeSurfaceEndpoint();
	return true;
}

bool UEHBArchitecturalSurfaceComponent::BuildSideSnapEdges(TArray<FEHBSurfaceSideSnapEdge>& OutEdges) const
{
	return false;
}

TArray<AEHBElementActorBase*> UEHBArchitecturalSurfaceComponent::QueryOverlappingBuildingElements() const
{
	TArray<AEHBElementActorBase*> Result;
	if (!bCanDetectContacts || !GetWorld())
	{
		return Result;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return Result;
	}

	FVector Origin;
	FVector Extent;
	OwnerActor->GetActorBounds(false, Origin, Extent);
	if (Extent.IsNearlyZero())
	{
		return Result;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(EHBSurfaceOverlap), false);
	if (OwnerElement)
	{
		QueryParams.AddIgnoredActor(OwnerElement);
	}

	if (!GetWorld()->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		ObjectParams,
		FCollisionShape::MakeBox(Extent),
		QueryParams))
	{
		return Result;
	}

	TSet<AEHBElementActorBase*> UniqueElements;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AEHBElementActorBase* Element = Cast<AEHBElementActorBase>(Overlap.GetActor());
		if (!Element && Overlap.GetComponent())
		{
			Element = Cast<AEHBElementActorBase>(Overlap.GetComponent()->GetOwner());
		}
		if (Element && Element != OwnerElement && !Element->IsActorBeingDestroyed())
		{
			UniqueElements.Add(Element);
		}
	}

	for (AEHBElementActorBase* Element : UniqueElements)
	{
		Result.Add(Element);
	}
	return Result;
}

TArray<UEHBArchitecturalSurfaceComponent*> UEHBArchitecturalSurfaceComponent::QueryNearbySurfaces(float SearchDistance) const
{
	TArray<UEHBArchitecturalSurfaceComponent*> Result;
	const float SafeSearchDistance = FMath::Max(0.0f, SearchDistance);
	if (!OwningBuilding)
	{
		return Result;
	}

	FEHBElementQuery Query;
	const TArray<AEHBElementActorBase*> Elements = OwningBuilding->QueryElements(Query);
	const FVector Origin = Bounds.Origin;
	const float SearchRadius = SafeSearchDistance + Bounds.SphereRadius;

	for (AEHBElementActorBase* Element : Elements)
	{
		if (!Element || Element == OwnerElement)
		{
			continue;
		}

		TArray<UEHBArchitecturalSurfaceComponent*> SurfaceComponents;
		Element->GetComponents(SurfaceComponents);
		for (UEHBArchitecturalSurfaceComponent* Surface : SurfaceComponents)
		{
			if (!Surface || Surface == this)
			{
				continue;
			}

			const float AllowedDistance = SearchRadius + Surface->Bounds.SphereRadius;
			if (FVector::DistSquared(Origin, Surface->Bounds.Origin) <= FMath::Square(AllowedDistance))
			{
				Result.Add(Surface);
			}
		}
	}

	return Result;
}

void UEHBArchitecturalSurfaceComponent::NotifySurfaceGeometryChanged(bool bFinished)
{
	++SurfaceGeometryRevision;
	ResolveOwnerReferences();
	if (OwnerElement)
	{
		OwnerElement->NotifyElementGeometryChanged(bFinished);
	}
}

bool UEHBArchitecturalSurfaceComponent::SubmitMeshBuildResult(
	FName SectionBaseName,
	const FEHBSurfaceMeshBuildResult& BuildResult,
	UMaterialInterface* FallbackMaterial,
	bool bCreateCollision)
{
	if (!BuildResult.IsValidMesh())
	{
		ClearAllMeshSections();
		NotifySurfaceGeometryChanged(true);
		return false;
	}

	TArray<FVector> Normals = BuildResult.Normals;
	if (Normals.Num() != BuildResult.Vertices.Num())
	{
		Normals.Init(FVector::UpVector, BuildResult.Vertices.Num());
	}

	TArray<FVector2D> UV0 = BuildResult.UV0;
	if (UV0.Num() != BuildResult.Vertices.Num())
	{
		UV0.Init(FVector2D::ZeroVector, BuildResult.Vertices.Num());
	}

	TArray<FLinearColor> VertexColors;
	TArray<FEHBMeshTangent> Tangents;
	VertexColors.Init(FLinearColor::White, BuildResult.Vertices.Num());
	Tangents.Init(FEHBMeshTangent(), BuildResult.Vertices.Num());

	UMaterialInterface* MaterialOverride = SurfaceMaterialOverride.LoadSynchronous();
	if (!MaterialOverride)
	{
		MaterialOverride = FallbackMaterial;
	}

	FEHBScopedGeneratedMeshUpdate ScopedMeshUpdate(this);
	EmptyOverrideMaterials();

	const FName EffectiveSectionBaseName = SectionBaseName.IsNone()
		? (SurfaceName.IsNone() ? FName(TEXT("Surface")) : SurfaceName)
		: SectionBaseName;

	if (BuildResult.TriangleMaterialIndices.IsEmpty())
	{
		CreateMeshSection_LinearColor(
			0,
			BuildResult.Vertices,
			BuildResult.Triangles,
			Normals,
			UV0,
			VertexColors,
			Tangents,
			bCreateCollision && bEnableSurfaceCollision);
		SetMeshSectionName(0, EffectiveSectionBaseName);
		ClearMeshSectionsFrom(1);
		if (MaterialOverride)
		{
			SetMaterialIfChanged(0, MaterialOverride);
		}
		SetCollisionEnabled(bEnableSurfaceCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		NotifySurfaceGeometryChanged(true);
		return true;
	}

	TMap<int32, TArray<int32>> TrianglesByMaterialIndex;
	TArray<int32> SortedMaterialIndices;
	const int32 TriangleCount = BuildResult.Triangles.Num() / 3;
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
	{
		const int32 MaterialIndex = BuildResult.TriangleMaterialIndices.IsValidIndex(TriangleIndex)
			? FMath::Max(0, BuildResult.TriangleMaterialIndices[TriangleIndex])
			: 0;
		TArray<int32>& SectionTriangles = TrianglesByMaterialIndex.FindOrAdd(MaterialIndex);
		if (SectionTriangles.IsEmpty())
		{
			SortedMaterialIndices.Add(MaterialIndex);
		}
		SectionTriangles.Add(BuildResult.Triangles[TriangleIndex * 3]);
		SectionTriangles.Add(BuildResult.Triangles[TriangleIndex * 3 + 1]);
		SectionTriangles.Add(BuildResult.Triangles[TriangleIndex * 3 + 2]);
	}

	SortedMaterialIndices.Sort();
	for (int32 SectionIndex = 0; SectionIndex < SortedMaterialIndices.Num(); ++SectionIndex)
	{
		const int32 MaterialIndex = SortedMaterialIndices[SectionIndex];
		const TArray<int32>* SectionTriangles = TrianglesByMaterialIndex.Find(MaterialIndex);
		if (!SectionTriangles || SectionTriangles->Num() < 3)
		{
			continue;
		}

		CreateMeshSection_LinearColor(
			SectionIndex,
			BuildResult.Vertices,
			*SectionTriangles,
			Normals,
			UV0,
			VertexColors,
			Tangents,
			bCreateCollision && bEnableSurfaceCollision);

		SetMeshSectionName(
			SectionIndex,
			FName(*FString::Printf(TEXT("%s_%d"), *EffectiveSectionBaseName.ToString(), MaterialIndex)));

		UMaterialInterface* SectionMaterial = MaterialOverride;
		if (!SectionMaterial && BuildResult.SourceMaterials.IsValidIndex(MaterialIndex))
		{
			SectionMaterial = BuildResult.SourceMaterials[MaterialIndex].LoadSynchronous();
		}
		if (SectionMaterial)
		{
			SetMaterialIfChanged(SectionIndex, SectionMaterial);
		}
	}

	ClearMeshSectionsFrom(SortedMaterialIndices.Num());
	SetCollisionEnabled(bEnableSurfaceCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	NotifySurfaceGeometryChanged(true);
	return true;
}

bool UEHBArchitecturalSurfaceComponent::ApplyMaterialToSurface(
	UMaterialInterface* Material,
	const FEHBSurfaceMaterialApplyOptions& Options)
{
	if (!Material)
	{
		return false;
	}

	Modify();
	if (Options.bPersistAsSurfaceOverride)
	{
		SurfaceMaterialOverride = Material;
	}

	const int32 SlotCount = FMath::Max(GetNumMaterials(), 1);
	bool bChanged = false;
	const int32 SlotsToApply = Options.bApplyAllSections ? SlotCount : 1;
	for (int32 MaterialIndex = 0; MaterialIndex < SlotsToApply; ++MaterialIndex)
	{
		bChanged |= SetMaterialIfChanged(MaterialIndex, Material);
	}

	if (bChanged || Options.bPersistAsSurfaceOverride)
	{
		MarkPackageDirty();
		if (AActor* Owner = GetOwner())
		{
			Owner->MarkPackageDirty();
		}
	}

	return true;
}

void UEHBArchitecturalSurfaceComponent::OnRegister()
{
	Super::OnRegister();
	ResolveOwnerReferences();
	EnsureSurfaceGuid();
}

void UEHBArchitecturalSurfaceComponent::PostLoad()
{
	Super::PostLoad();
	ResolveOwnerReferences();
	EnsureSurfaceGuid();
}

void UEHBArchitecturalSurfaceComponent::ResolveOwnerReferences()
{
	if (!OwnerElement)
	{
		OwnerElement = Cast<AEHBElementActorBase>(GetOwner());
	}
	if (OwnerElement)
	{
		OwnerElementGuid = OwnerElement->ElementGuid;
		OwningBuilding = OwnerElement->OwningBuilding;
	}
}

void UEHBArchitecturalSurfaceComponent::EnsureSurfaceGuid()
{
	if(IsTemplate())return;
	if(auto* Element=Cast<AEHBElementActorBase>(GetOwner());Element&&!SurfaceName.IsNone())
	{
		const FGuid Identity=Element->ResolveLogicalSurfaceIdentity(SurfaceName,SurfaceSubIndex,SurfaceGuid);
		if(Identity.IsValid()){SurfaceGuid=Identity;return;}
	}
	if (!SurfaceGuid.IsValid())
	{
		SurfaceGuid = FGuid::NewGuid();
	}
}
