// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/MeshComponent.h"
#include "Interfaces/Interface_CollisionDataProvider.h"
#include "PhysicsEngine/ConvexElem.h"
#include "EHBGeneratedMeshComponent.generated.h"

class UBodySetup;

/**
 * Tangent data used by EHB generated mesh vertices.
 * The Y tangent is derived from the normal and TangentX, matching Unreal's runtime mesh conventions.
 */
USTRUCT(BlueprintType)
struct FEHBMeshTangent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh")
	FVector TangentX = FVector(1.0, 0.0, 0.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh")
	bool bFlipTangentY = false;

	FEHBMeshTangent() = default;

	FEHBMeshTangent(float X, float Y, float Z)
		: TangentX(X, Y, Z)
	{
	}

	FEHBMeshTangent(const FVector& InTangentX, bool bInFlipTangentY)
		: TangentX(InTangentX)
		, bFlipTangentY(bInFlipTangentY)
	{
	}
};

USTRUCT(BlueprintType)
struct FEHBMeshVertex
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh")
	FVector Normal = FVector::UpVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh")
	FEHBMeshTangent Tangent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh")
	FColor Color = FColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh")
	FVector2D UV0 = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh")
	FVector2D UV1 = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh")
	FVector2D UV2 = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EHB Mesh")
	FVector2D UV3 = FVector2D::ZeroVector;
};

USTRUCT()
struct FEHBMeshSection
{
	GENERATED_BODY()

	UPROPERTY()
	FName SectionName = NAME_None;

	UPROPERTY()
	TArray<FEHBMeshVertex> ProcVertexBuffer;

	UPROPERTY()
	TArray<uint32> ProcIndexBuffer;

	UPROPERTY()
	FBox SectionLocalBox = FBox(ForceInit);

	UPROPERTY()
	bool bEnableCollision = false;

	UPROPERTY()
	bool bSectionVisible = true;

	UPROPERTY()
	uint32 ContentHash = 0;

	UPROPERTY()
	int32 Revision = 0;

	void Reset()
	{
		SectionName = NAME_None;
		ProcVertexBuffer.Empty();
		ProcIndexBuffer.Empty();
		SectionLocalBox.Init();
		bEnableCollision = false;
		bSectionVisible = true;
		ContentHash = 0;
		++Revision;
	}
};

// Temporary source-compatibility aliases for existing EHB code and automation tests.
using FProcMeshTangent = FEHBMeshTangent;
using FProcMeshVertex = FEHBMeshVertex;
using FProcMeshSection = FEHBMeshSection;

USTRUCT(BlueprintType)
struct FEHBGeneratedMeshStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int32 SectionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int32 VisibleSectionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int32 VertexCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int32 TriangleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int32 CollisionSectionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int32 CollisionVertexCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int32 CollisionTriangleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int64 EstimatedVertexBufferBytes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int64 EstimatedIndexBufferBytes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int32 MeshRevision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int64 SubmittedSectionUpdateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "EHB Mesh|Stats")
	int64 SkippedIdenticalSectionUpdateCount = 0;
};

/**
 * Runtime mesh component owned by EasyHouseBuilder.
 *
 * This is the first EHB-native runtime mesh path for generated building geometry. It keeps a familiar
 * section submission API for migration, while adding batch update deferral so generated buildings
 * can avoid repeated bounds/render/collision invalidation during multi-section rebuilds.
 */
UCLASS(ClassGroup = Rendering, HideCategories = (Object, LOD), meta = (BlueprintSpawnableComponent))
class EASYHOUSEBUILDER_API UEHBGeneratedMeshComponent : public UMeshComponent, public IInterface_CollisionDataProvider
{
	GENERATED_BODY()

public:
	UEHBGeneratedMeshComponent(const FObjectInitializer& ObjectInitializer);

 /** Read-only comparison of the actual buffers, defaults, filtered indices, bounds
  * and section collision/visibility. Does not trust a stored content hash. */
 bool MatchesMeshSection(int32 SectionIndex,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,const TArray<FVector>& Normals,
 const TArray<FVector2D>& UV0,const TArray<FVector2D>& UV1,const TArray<FVector2D>& UV2,const TArray<FVector2D>& UV3,
 const TArray<FColor>& VertexColors,const TArray<FEHBMeshTangent>& Tangents,bool bCreateCollision) const;

	UFUNCTION(BlueprintCallable, Category = "Components|EHBGeneratedMesh", meta = (DisplayName = "Create Mesh Section", AutoCreateRefTerm = "Normals,UV0,UV1,UV2,UV3,VertexColors,Tangents", AdvancedDisplay = "UV1,UV2,UV3"))
	void CreateMeshSection_LinearColor(
		int32 SectionIndex,
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UV0,
		const TArray<FVector2D>& UV1,
		const TArray<FVector2D>& UV2,
		const TArray<FVector2D>& UV3,
		const TArray<FLinearColor>& VertexColors,
		const TArray<FEHBMeshTangent>& Tangents,
		bool bCreateCollision,
		UPARAM(DisplayName = "SRGB Conversion") bool bSRGBConversion = false);

	void CreateMeshSection_LinearColor(
		int32 SectionIndex,
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UV0,
		const TArray<FLinearColor>& VertexColors,
		const TArray<FEHBMeshTangent>& Tangents,
		bool bCreateCollision,
		bool bSRGBConversion = false);

	void CreateNamedMeshSection_LinearColor(
		FName SectionName,
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UV0,
		const TArray<FLinearColor>& VertexColors,
		const TArray<FEHBMeshTangent>& Tangents,
		bool bCreateCollision,
		bool bSRGBConversion = false);

	void CreateMeshSection(
		int32 SectionIndex,
		const TArray<FVector>& Vertices,
		const TArray<int32>& Triangles,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UV0,
		const TArray<FVector2D>& UV1,
		const TArray<FVector2D>& UV2,
		const TArray<FVector2D>& UV3,
		const TArray<FColor>& VertexColors,
		const TArray<FEHBMeshTangent>& Tangents,
		bool bCreateCollision);

	void UpdateMeshSection_LinearColor(
		int32 SectionIndex,
		const TArray<FVector>& Vertices,
		const TArray<FVector>& Normals,
		const TArray<FVector2D>& UV0,
		const TArray<FVector2D>& UV1,
		const TArray<FVector2D>& UV2,
		const TArray<FVector2D>& UV3,
		const TArray<FLinearColor>& VertexColors,
		const TArray<FEHBMeshTangent>& Tangents,
		bool bSRGBConversion = false);

	void ClearMeshSection(int32 SectionIndex);
	void ClearMeshSectionsFrom(int32 FirstSectionIndex);
	void ClearAllMeshSections();

	void SetMeshSectionVisible(int32 SectionIndex, bool bNewVisibility);
	bool IsMeshSectionVisible(int32 SectionIndex) const;
	int32 GetNumSections() const;
	bool SetMaterialIfChanged(int32 ElementIndex, UMaterialInterface* Material);

	int32 FindMeshSectionIndex(FName SectionName) const;
	int32 FindOrAddMeshSection(FName SectionName);
	void SetMeshSectionName(int32 SectionIndex, FName SectionName);

	FEHBMeshSection* GetProcMeshSection(int32 SectionIndex);
	const FEHBMeshSection* GetProcMeshSection(int32 SectionIndex) const;
	void SetProcMeshSection(int32 SectionIndex, const FEHBMeshSection& Section);

	void BeginMeshUpdate();
	void EndMeshUpdate();

	void AddCollisionConvexMesh(TArray<FVector> ConvexVerts);
	void ClearCollisionConvexMeshes();
	void SetCollisionConvexMeshes(const TArray<TArray<FVector>>& ConvexMeshes);

	virtual bool GetTriMeshSizeEstimates(struct FTriMeshCollisionDataEstimates& OutTriMeshEstimates, bool bInUseAllTriData) const override;
	virtual bool GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData) override;
	virtual bool ContainsPhysicsTriMeshData(bool InUseAllTriData) const override;
	virtual bool WantsNegXTriMesh() override { return false; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EHB Mesh|Collision")
	bool bUseComplexAsSimpleCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EHB Mesh|Collision")
	bool bUseAsyncCooking = true;

	UPROPERTY(Instanced)
	TObjectPtr<UBodySetup> MeshBodySetup;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual UBodySetup* GetBodySetup() override;
	virtual UMaterialInterface* GetMaterialFromCollisionFaceIndex(int32 FaceIndex, int32& SectionIndex) const override;
	virtual int32 GetNumMaterials() const override;
	virtual void GetStreamingRenderAssetInfo(FStreamingTextureLevelContext& LevelContext, TArray<FStreamingRenderAssetPrimitiveInfo>& OutStreamingRenderAssets) const override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	const TArray<FEHBMeshSection>& GetMeshSections() const { return MeshSections; }
	int32 GetMeshRevision() const { return MeshRevision; }
	uint64 GetSubmittedSectionUpdateCount() const { return SubmittedSectionUpdateCount; }
	uint64 GetSkippedIdenticalSectionUpdateCount() const { return SkippedIdenticalSectionUpdateCount; }
	UFUNCTION(BlueprintCallable, Category = "Components|EHBGeneratedMesh")
	FEHBGeneratedMeshStats GetMeshStats() const;
	UFUNCTION(BlueprintCallable, Category = "Components|EHBGeneratedMesh")
	void ResetMeshUpdateStats();

private:
	void RebuildSectionNameMap() const;
	void MarkSectionNameMapDirty() const;
	void UpdateLocalBounds();
	void RequestMeshRefresh(bool bNeedsCollisionUpdate);
	void FlushDeferredMeshRefresh();
	void CreateMeshBodySetup();
	void UpdateCollision();
	void FinishPhysicsAsyncCook(bool bSuccess, UBodySetup* FinishedBodySetup);
	UBodySetup* CreateBodySetupHelper();

	UPROPERTY()
	TArray<FEHBMeshSection> MeshSections;

	UPROPERTY()
	TArray<FKConvexElem> CollisionConvexElems;

	UPROPERTY()
	FBoxSphereBounds LocalBounds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBodySetup>> AsyncBodySetupQueue;

	mutable TMap<FName, int32> SectionNameToIndex;
	mutable bool bSectionNameMapDirty = true;

	int32 MeshUpdateDepth = 0;
	int32 MeshRevision = 0;
	uint64 SubmittedSectionUpdateCount = 0;
	uint64 SkippedIdenticalSectionUpdateCount = 0;
	bool bDeferredBoundsUpdate = false;
	bool bDeferredRenderUpdate = false;
	bool bDeferredCollisionUpdate = false;
};

class FEHBScopedGeneratedMeshUpdate
{
public:
	explicit FEHBScopedGeneratedMeshUpdate(UEHBGeneratedMeshComponent* InComponent)
		: Component(InComponent)
	{
		if (Component)
		{
			Component->BeginMeshUpdate();
		}
	}

	~FEHBScopedGeneratedMeshUpdate()
	{
		if (Component)
		{
			Component->EndMeshUpdate();
		}
	}

private:
	UEHBGeneratedMeshComponent* Component = nullptr;
};
