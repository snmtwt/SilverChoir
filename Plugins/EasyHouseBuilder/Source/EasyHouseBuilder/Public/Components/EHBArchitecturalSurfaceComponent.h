// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/EHBGeneratedMeshComponent.h"
#include "Geometry/EHBSurfaceGeometryTypes.h"
#include "Geometry/EHBSurfaceInteractionTypes.h"
#include "UObject/Interface.h"
#include "EHBArchitecturalSurfaceComponent.generated.h"

class AEHBBuildingActorBase;
class AEHBElementActorBase;
class UMaterialInterface;

UINTERFACE(BlueprintType)
class EASYHOUSEBUILDER_API UEHBSurfaceMaterialTarget : public UInterface
{
	GENERATED_BODY()
};

class EASYHOUSEBUILDER_API IEHBSurfaceMaterialTarget
{
	GENERATED_BODY()

public:
	virtual bool ApplyMaterialToSurface(
		UMaterialInterface* Material,
		const FEHBSurfaceMaterialApplyOptions& Options) = 0;
};

UCLASS(ClassGroup = Rendering, HideCategories = (Object, LOD), meta = (BlueprintSpawnableComponent))
class EASYHOUSEBUILDER_API UEHBArchitecturalSurfaceComponent
	: public UEHBGeneratedMeshComponent
	, public IEHBSurfaceMaterialTarget
{
	GENERATED_BODY()

public:
	UEHBArchitecturalSurfaceComponent(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EHB Surface|Identity")
	FGuid SurfaceGuid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Identity")
	EEHBArchitecturalSurfaceRole SurfaceRole = EEHBArchitecturalSurfaceRole::Unknown;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Identity")
	FName SurfaceName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Identity")
	int32 SurfaceSubIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EHB Surface|Owner")
	FGuid OwnerElementGuid;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "EHB Surface|Owner")
	TObjectPtr<AEHBElementActorBase> OwnerElement;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "EHB Surface|Owner")
	TObjectPtr<AEHBBuildingActorBase> OwningBuilding;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "EHB Surface|Geometry")
	int32 SurfaceGeometryRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Collision")
	bool bEnableSurfaceCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Interaction")
	bool bCanBeSnapTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Interaction")
	bool bCanDetectContacts = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Surface|Material")
	TSoftObjectPtr<UMaterialInterface> SurfaceMaterialOverride;

	void InitializeSurface(
		AEHBElementActorBase* InOwnerElement,
		EEHBArchitecturalSurfaceRole InRole,
		FName InSurfaceName,
		int32 InSubIndex);

	UFUNCTION(BlueprintCallable, Category = "EHB Surface|Geometry")
	virtual bool RebuildSurfaceMesh();

	UFUNCTION(BlueprintCallable, Category = "EHB Surface|Geometry")
	virtual void ClearSurfaceMesh();

	UFUNCTION(BlueprintPure, Category = "EHB Surface|Relation")
	FEHBElementRelationEndpoint MakeRelationEndpoint() const;

	UFUNCTION(BlueprintPure, Category = "EHB Surface|Relation")
	FEHBSurfaceEndpoint MakeSurfaceEndpoint() const;

	UFUNCTION(BlueprintPure, Category = "EHB Surface|Relation")
	EEHBElementSurfaceKind GetElementSurfaceKind() const;

	UFUNCTION(BlueprintPure, Category = "EHB Surface|Transform")
	FTransform GetSurfaceToElementTransform() const;

	UFUNCTION(BlueprintPure, Category = "EHB Surface|Transform")
	FTransform GetSurfaceToBuildingTransform() const;

	UFUNCTION(BlueprintPure, Category = "EHB Surface|Transform")
	FTransform GetSurfaceToWorldTransform() const;

	virtual bool ProjectWorldPointToSurface(
		const FVector& WorldPoint,
		FVector& OutWorldPoint,
		FVector2D& OutSurfaceUV) const;

	virtual bool FindClosestPointOnSurface(
		const FVector& WorldPoint,
		FVector& OutWorldPoint,
		float& OutDistance) const;

	virtual bool BuildSnapCandidates(
		const FVector& WorldPoint,
		TArray<FEHBSurfaceSnapCandidate>& OutCandidates) const;

	virtual bool BuildSideSnapEdges(TArray<FEHBSurfaceSideSnapEdge>& OutEdges) const;

	virtual TArray<AEHBElementActorBase*> QueryOverlappingBuildingElements() const;
	virtual TArray<UEHBArchitecturalSurfaceComponent*> QueryNearbySurfaces(float SearchDistance) const;

	void NotifySurfaceGeometryChanged(bool bFinished);

	bool SubmitMeshBuildResult(
		FName SectionBaseName,
		const FEHBSurfaceMeshBuildResult& BuildResult,
		UMaterialInterface* FallbackMaterial,
		bool bCreateCollision);

	virtual bool ApplyMaterialToSurface(
		UMaterialInterface* Material,
		const FEHBSurfaceMaterialApplyOptions& Options) override;

	virtual void OnRegister() override;
	virtual void PostLoad() override;

protected:
	void ResolveOwnerReferences();
	void EnsureSurfaceGuid();
};
